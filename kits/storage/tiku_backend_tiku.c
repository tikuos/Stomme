/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_backend_tiku.c - a live device as a browsable volume.
 *
 * Built on the session and namespace client in kits/device: the manifest
 * supplies structure, typed descriptors and capabilities in one read,
 * `cat` and `write` supply values, `ls` supplies the contents of a dynamic
 * store such as /data, and /sys/vfs/events says what changed.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_model.h"
#include "tiku_ident.h"
#include "tiku_state.h"

#include "tiku_ns.h"
#include "tiku_session.h"
#include "tiku_tx.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    unsigned            last_dropped;   /* device drop count when last asked */
    tiku_session_t *sess;
    tiku_ns_t      *ns;
    tiku_store_t    *store;
    tiku_identity_t  ident;
    int                  leased;        /* a client holds the shell       */
    char                 devid[TIKU_DEVID_MAX];
    /* Every operation takes this lock, including those answered from
     * memory: a worker's reload rebuilds the namespace while the loop
     * reads it for a stat. */
    pthread_mutex_t      hand;
} tiku_impl_t;

/** @brief Whether a client holds the shell; the namespace is closed while
 *         it does. */
static int
shell_is_leased(const tiku_backend_t *b)
{
    const tiku_impl_t *impl = (b != NULL) ? b->impl : NULL;

    return impl != NULL && impl->leased;
}

/** @brief Read one identity node, empty string when absent. */
static void
read_ident_field(tiku_ns_t *ns, const char *path, char *out, size_t max)
{
    const tiku_node_t *n;

    out[0] = '\0';
    if (tiku_ns_read(ns, path) != 0) {
        return;
    }
    n = tiku_ns_find(ns, path);
    if (n != NULL && n->value_valid) {
        size_t i = 0;
        while (n->value[i] != '\0' && n->value[i] != '\n' && i + 1u < max) {
            out[i] = n->value[i];
            i++;
        }
        out[i] = '\0';
    }
}

/** @brief Whether @p path is /data or inside it, the device's dynamic
 *         store. */
static int
in_store(const char *path)
{
    return (strcmp(path, "/data") == 0 || strncmp(path, "/data/", 6) == 0);
}

/**
 * @brief Decide a namespace node's kind.
 *
 * The device root is a VOLUME.  A device directory is VIRTUAL_DIR, read-only
 * in the UI, unless it is in the dynamic store, where it is a DIRECTORY that
 * accepts new files.  The manifest does not mark dynamic directories, so the
 * store is recognised by its path, /data.
 */
static tiku_kind_t
kind_of(const tiku_node_t *n)
{
    if (n->path[0] == '/' && n->path[1] == '\0') {
        return TIKU_KIND_VOLUME;
    }
    if (in_store(n->path)) {
        /* Everything in the dynamic store is a file or a folder: a saved
         * BASIC program is a file, not a live value. */
        return n->is_dir ? TIKU_KIND_DIRECTORY : TIKU_KIND_FILE;
    }
    return n->is_dir ? TIKU_KIND_VIRTUAL_DIR : TIKU_KIND_DEVICE_NODE;
}

/** @brief Copy a namespace node into a Model. */
static void
from_node(tiku_backend_t *b, const tiku_node_t *n,
          tiku_model_t *m)
{
    uint64_t h = 1469598103934665603ULL;
    const char *p;

    memset(m, 0, sizeof *m);
    /* Copied with an explicit bound against the Model's field sizes. */
    {
        size_t pl = strlen(n->path), nl = strlen(n->name);
        if (pl >= sizeof m->path) { pl = sizeof m->path - 1u; }
        if (nl >= sizeof m->name) { nl = sizeof m->name - 1u; }
        memcpy(m->path, n->path, pl); m->path[pl] = '\0';
        memcpy(m->name, n->name, nl); m->name[nl] = '\0';
    }
    m->kind = kind_of(n);
    m->backend = b;
    m->facts.perm = 0u;
    if (n->perm & TIKU_NS_P_READ)  { m->facts.perm |= TIKU_P_READ; }
    if (n->perm & TIKU_NS_P_WRITE) { m->facts.perm |= TIKU_P_WRITE; }
    /* On a container, WRITE means entries can be created in it.  The
     * manifest's perm column describes a node's own read and write
     * handlers, and a directory has none, so every device directory
     * arrives "--": a store directory gets read and write here, a
     * namespace directory and the root read only. */
    if (m->kind == TIKU_KIND_DIRECTORY) {
        m->facts.perm |= TIKU_P_READ | TIKU_P_WRITE;
    } else if (m->kind == TIKU_KIND_VIRTUAL_DIR ||
               m->kind == TIKU_KIND_VOLUME) {
        m->facts.perm |= TIKU_P_READ;
    }
    snprintf(m->facts.req_cap, sizeof m->facts.req_cap, "%s", n->cap);
    snprintf(m->facts.meta, sizeof m->facts.meta, "%s", n->meta);
    /* The ls walk fills the capability in as "?" when the manifest was cut;
     * cap_known is 0 for those. */
    m->facts.cap_known = (strcmp(n->cap, "?") != 0);
    m->facts.size = (int64_t)strlen(n->value);
    snprintf(m->type, sizeof m->type, "%s",
             (m->kind == TIKU_KIND_FILE)      ? "tiku-file/data"
             : (m->kind == TIKU_KIND_DIRECTORY) ? "tiku-dir/store"
             : (m->kind == TIKU_KIND_VOLUME)    ? "tiku-dir/device"
             : n->is_dir ? "tiku-dir/namespace" : "tiku-node/value");

    if (n->id != 0u) {
        /* The device's own identity, stable across a rename: a renamed node
         * keeps its node_id. */
        m->node_id = n->id;
    } else {
        /* An older device has no identity column, so the id is a hash of
         * the path, and a node renamed there gets a new id. */
        for (p = n->path; *p != '\0'; p++) {
            h ^= (unsigned char)*p;
            h *= 1099511628211ULL;
        }
        m->node_id = h;
    }
    m->generation = n->generation;
}

/**
 * @brief What the device has recorded since the last ask, from its change
 *        ring at /sys/vfs/events.
 *
 * Reading the ring is one round trip whatever the size of the namespace.
 *
 * @return Changes written, 0 while the shell is leased, or -1 when the
 *         device has no ring.
 */
static int
do_changes(tiku_backend_t *b, tiku_model_change_t *out, int max,
           int *complete)
{
    tiku_impl_t *impl = b->impl;
    tiku_ns_change_t recs[32];
    unsigned dropped = 0u;
    int n, i, w = 0;

    if (shell_is_leased(b)) {
        /* The ring is not read while a client holds the shell: a command
         * now would consume the program's output. */
        return 0;
    }

    if (complete != NULL) {
        *complete = 1;
    }
    n = tiku_ns_events(impl->ns, recs,
                            (int)(sizeof recs / sizeof recs[0]), &dropped);
    if (n < 0) {
        return -1;              /* the device cannot tell: caller polls  */
    }
    /* The device's drop counter is cumulative since boot: an answer is
     * incomplete when the counter moved since the last ask. */
    if (dropped != impl->last_dropped) {
        impl->last_dropped = dropped;
        if (complete != NULL) {
            *complete = 0;
        }
    }
    /*
     * A created, removed or moved record changes the shape of the
     * namespace, so the manifest is read again: stat answers from it, and
     * a stale manifest still holds a deleted folder's path.
     */
    for (i = 0; i < n; i++) {
        if (strcmp(recs[i].op, "changed") != 0) {
            (void)tiku_ns_load(impl->ns);
            break;
        }
    }
    for (i = 0; i < n && w < max; i++) {
        out[w].id = (uint64_t)recs[i].id;
        /* A value change refreshes a row in place; any other op changes
         * the shape of the listing, which the caller lists again. */
        if (strcmp(recs[i].op, "created") == 0) {
            out[w].op = TIKU_CH_CREATED;
        } else if (strcmp(recs[i].op, "removed") == 0) {
            out[w].op = TIKU_CH_REMOVED;
        } else if (strcmp(recs[i].op, "moved") == 0) {
            out[w].op = TIKU_CH_MOVED;
        } else {
            out[w].op = TIKU_CH_CHANGED;
        }
        w++;
    }
    /* The read drained the ring: records past @p max are gone. */
    if (w < n && complete != NULL) {
        *complete = 0;
    }
    return w;
}

/** @brief The manifest's path for the node carrying @p id; -1 when no node
 *         has it. */
static int
do_path_of_id(tiku_backend_t *b, uint64_t id, char *out, size_t max)
{
    tiku_impl_t *impl = b->impl;
    const tiku_node_t *n = tiku_ns_find_id(impl->ns,
                                                     (unsigned)id);

    if (n == NULL) {
        return -1;
    }
    (void)snprintf(out, max, "%s", n->path);
    return 0;
}

/**
 * @brief Describe @p path: from the manifest, or by asking the device for
 *        a node the manifest does not hold.
 * @return 0, or -1 while the shell is leased or when the node is absent.
 */
static int
do_stat(tiku_backend_t *b, const char *path, tiku_model_t *out)
{
    if (shell_is_leased(b)) {
        return -1;
    }
    tiku_impl_t *impl = b->impl;
    const tiku_node_t *n = tiku_ns_find(impl->ns, path);

    if (n == NULL && path[0] == '/' && path[1] == '\0') {
        /*
         * The root is not in the manifest, and the device answers ENOENT
         * for it.  The router asks it of a mount while listing the folder
         * the mount sits in, so it is answered here: the device as a
         * volume, named by /sys/device/name or else by its key.
         */
        tiku_node_t root;

        memset(&root, 0, sizeof root);
        root.path[0] = '/';
        snprintf(root.name, sizeof root.name, "%s",
                 impl->ident.name[0] != '\0' ? impl->ident.name
                                              : impl->devid);
        snprintf(root.cap, sizeof root.cap, "-");
        snprintf(root.meta, sizeof root.meta, "-");
        root.is_dir = 1;
        from_node(b, &root, out);
        return 0;
    }
    if (n == NULL) {
        /* Not in the manifest, which is a snapshot: a node made since, such
         * as a /data file or a driver registered at run time, is read from
         * the device.  A node its parent lists but that cannot be read is
         * UNKNOWN; the view holds such an entry out of sight until a
         * listing describes it. */
        int rc = tiku_ns_read(impl->ns, path);

        memset(out, 0, sizeof *out);
        (void)snprintf(out->path, sizeof out->path, "%s", path);
        {
            const char *slash = strrchr(path, '/');

            (void)snprintf(out->name, sizeof out->name, "%s",
                           (slash != NULL) ? slash + 1 : path);
        }
        (void)snprintf(out->facts.meta, sizeof out->facts.meta, "-");
        (void)snprintf(out->facts.req_cap, sizeof out->facts.req_cap, "?");
        out->backend = b;
        if (rc >= 0) {
            out->kind = TIKU_KIND_DEVICE_NODE;
            out->facts.perm = TIKU_P_READ;
            (void)snprintf(out->type, sizeof out->type, "tiku-node/value");
            return 0;
        }
        {
            /* Not readable: if its parent lists it, it exists but cannot be
             * described now. */
            char names[64][64];
            char dir[TIKU_PATH_MAX];
            const char *slash = strrchr(path, '/');
            int k, nls;

            if (slash == NULL || slash == path) {
                (void)snprintf(dir, sizeof dir, "/");
            } else {
                (void)snprintf(dir, sizeof dir, "%.*s",
                               (int)(slash - path), path);
            }
            nls = tiku_ns_ls(impl->ns, dir, names, 64);
            for (k = 0; k < nls; k++) {
                if (slash != NULL && strcmp(names[k], slash + 1) == 0) {
                    out->kind = TIKU_KIND_UNKNOWN;
                    return 0;
                }
            }
        }
        return -1;
    }
    from_node(b, n, out);
    return 0;
}

/**
 * @brief List the children of @p path as the device has them now.
 * @return Entries listed, or -1 while the shell is leased or when the
 *         folder is not there.
 */
static int
do_list(tiku_backend_t *b, const char *path, tiku_entry_fn fn,
        void *ctx)
{
    if (shell_is_leased(b)) {
        return -1;
    }
    tiku_impl_t *impl = b->impl;
    const tiku_node_t *kids[256];
    int n, i, count;

    /*
     * The directory is listed on the device, because the manifest is a
     * snapshot.  The manifest supplies the typed detail (descriptor,
     * capability, identity) for the names that come back, and a name it
     * does not hold is still listed.
     */
    {
        char names[128][64];
        char kinds[128];
        int live = tiku_ns_ls_kinds(impl->ns, path, names, kinds, 128);

        count = 0;
        if (live > 0) {
            for (i = 0; i < live; i++) {
                tiku_model_t m;
                const tiku_node_t *known;
                char full[TIKU_PATH_MAX];

                if (strcmp(path, "/") == 0) {
                    (void)snprintf(full, sizeof full, "/%s", names[i]);
                } else {
                    (void)snprintf(full, sizeof full, "%s/%s", path,
                                   names[i]);
                }
                known = tiku_ns_find(impl->ns, full);
                if (known != NULL) {
                    from_node(b, known, &m);
                } else {
                    /* Not in the manifest: the entry carries what the
                     * listing said, and its capability is "?". */
                    memset(&m, 0, sizeof m);
                    (void)snprintf(m.path, sizeof m.path, "%s", full);
                    (void)snprintf(m.name, sizeof m.name, "%s", names[i]);
                    (void)snprintf(m.facts.meta, sizeof m.facts.meta, "-");
                    (void)snprintf(m.facts.req_cap, sizeof m.facts.req_cap,
                                   "?");
                    /* Its place decides its kind, as kind_of() does for a
                     * name the manifest holds. */
                    if (in_store(full)) {
                        m.kind = kinds[i] ? TIKU_KIND_DIRECTORY
                                          : TIKU_KIND_FILE;
                    } else {
                        m.kind = kinds[i] ? TIKU_KIND_VIRTUAL_DIR
                                          : TIKU_KIND_DEVICE_NODE;
                    }
                    /* The manifest states writability and does not hold
                     * this name, so the entry is read-only. */
                    m.facts.perm = TIKU_P_READ;
                    m.facts.cap_known = 0;
                    m.backend = b;
                    (void)snprintf(m.type, sizeof m.type, "%s",
                                   (m.kind == TIKU_KIND_FILE)
                                       ? "tiku-file/data"
                                   : (m.kind == TIKU_KIND_DIRECTORY)
                                       ? "tiku-dir/store"
                                   : kinds[i] ? "tiku-dir/namespace"
                                              : "tiku-node/value");
                }
                count++;
                if (fn != NULL && fn(&m, ctx) != 0) {
                    return count;
                }
            }
            return count;
        }
        /* The device listed nothing or could not be asked: the manifest's
         * children are listed, and a path the manifest does not hold
         * either returns -1. */
        n = tiku_ns_children(impl->ns, path, kids, 256);
        if (n <= 0 && tiku_ns_find(impl->ns, path) == NULL) {
            return -1;
        }
        for (i = 0; i < n; i++) {
            tiku_model_t m;
            from_node(b, kids[i], &m);
            count++;
            if (fn != NULL && fn(&m, ctx) != 0) {
                return count;
            }
        }
    }
    /* The manifest does not list a dynamic store's contents, so /data is
     * asked for with ls. */
    if (n == 0 && strcmp(path, "/data") == 0) {
        char names[64][64];
        int k = tiku_ns_ls(impl->ns, path, names, 64);

        for (i = 0; i < k; i++) {
            tiku_model_t m;
            memset(&m, 0, sizeof m);
            {   /* A name too long for the Model is skipped, not cut
                 * short. */
                size_t nl = strlen(names[i]);
                if (nl >= sizeof m.name - 8u) { continue; }
                memcpy(m.name, names[i], nl + 1u);
                memcpy(m.path, "/data/", 6u);
                memcpy(m.path + 6u, names[i], nl + 1u);
            }
            m.kind = TIKU_KIND_FILE;
            m.backend = b;
            m.facts.perm = TIKU_P_READ | TIKU_P_WRITE;
            m.facts.cap_known = 1;
            snprintf(m.facts.req_cap, sizeof m.facts.req_cap, "-");
            snprintf(m.facts.meta, sizeof m.facts.meta, "-");
            snprintf(m.type, sizeof m.type, "tiku-file/data");
            count++;
            if (fn != NULL && fn(&m, ctx) != 0) {
                break;
            }
        }
    }
    return count;
}

/** @brief Read @p path's value from the device into @p buf, unterminated.
 *  @return Bytes copied, at most @p max, or -1. */
static int
do_read(tiku_backend_t *b, const char *path, void *buf, size_t max)
{
    if (shell_is_leased(b)) {
        return -1;
    }
    tiku_impl_t *impl = b->impl;
    const tiku_node_t *n;
    size_t len;

    if (tiku_ns_find(impl->ns, path) == NULL) {
        /* A node the manifest does not hold, such as a file the board made
         * after it was mounted: its directory is completed from the device
         * first, as the forms under gui/ are at connect. */
        char dir[TIKU_PATH_MAX];
        char *slash;

        (void)snprintf(dir, sizeof dir, "%s", path);
        slash = strrchr(dir, '/');
        if (slash != NULL && slash != dir) {
            *slash = '\0';
            (void)tiku_ns_complete_dir(impl->ns, dir, 1);
        }
    }
    if (tiku_ns_read(impl->ns, path) != 0) {
        return -1;
    }
    n = tiku_ns_find(impl->ns, path);
    if (n == NULL || !n->value_valid) {
        return -1;
    }
    len = strlen(n->value);
    if (len > max) {
        len = max;
    }
    memcpy(buf, n->value, len);
    return (int)len;
}

/**
 * @brief Write @p len bytes to @p path on the device, as one value of at
 *        most TIKU_VAL_MAX - 1 bytes; a longer one is refused.
 *
 * A /data file is read back after the write, and a file that does not
 * hold exactly @p buf fails the write.
 * @return 0, or -1 with @p err filled.
 */
static int
do_write(tiku_backend_t *b, const char *path, const void *buf, size_t len,
         char *err, size_t errmax)
{
    if (shell_is_leased(b)) {
        if (err != NULL && errmax > 0u) {
            snprintf(err, errmax, "a program is running on the board");
        }
        return -1;
    }
    tiku_impl_t *impl = b->impl;
    char val[TIKU_VAL_MAX];
    int got;

    if (len > sizeof val - 1u) {
        if (err != NULL && errmax > 0u) {
            snprintf(err, errmax, "%zu bytes is too long for the board: "
                     "one write carries at most %zu", len, sizeof val - 1u);
        }
        return -1;
    }
    memcpy(val, buf, len);
    val[len] = '\0';
    /* The device's own refusal text is the error the user sees: it names the
     * reason (EACCES from the capability gate, ERANGE from a range). */
    if (tiku_ns_write(impl->ns, path, val, err, errmax) != 0) {
        return -1;
    }
    if (!in_store(path)) {
        return 0;
    }
    /* The board's shell keeps only as much of a line as its line buffer
     * holds, and a NUL ends the value early. */
    got = do_read(b, path, val, sizeof val);
    if (got != (int)len || memcmp(val, buf, len) != 0) {
        if (err != NULL && errmax > 0u) {
            if (got < 0) {
                snprintf(err, errmax, "the board did not confirm the "
                         "%zu bytes written", len);
            } else if (got != (int)len) {
                snprintf(err, errmax, "the board kept %d of %zu bytes",
                         got, len);
            } else {
                snprintf(err, errmax, "the board holds a different value "
                         "from the one written");
            }
        }
        return -1;
    }
    return 0;
}

/**
 * @brief Make the folder @p path inside /data with the board's shell
 *        command mkdir.
 *
 * A path outside /data, a board whose shell has no mkdir, and the shell's
 * own refusal fail with @p err filled.
 * @return 0, or -1.
 */
static int
do_mkdir(tiku_backend_t *b, const char *path, char *err, size_t errmax)
{
    tiku_impl_t *impl = b->impl;
    char where[TIKU_NS_PATH_MAX + 3];
    char cmd[TIKU_NS_PATH_MAX + 16];
    char buf[256];
    const char *why = NULL;

    if (shell_is_leased(b)) {
        why = "a program is running on the board";
    } else if (!in_store(path) || strcmp(path, "/data") == 0) {
        why = "a board makes folders only inside /data";
    } else if (tiku_ns_quote(path, where, sizeof where) != 0) {
        why = "the board's shell cannot carry this name";
    }
    if (why == NULL) {
        snprintf(cmd, sizeof cmd, "mkdir %s", where);
        if (tiku_session_cmd(impl->sess, cmd, buf, sizeof buf, 4000) < 0) {
            why = "the board did not answer";
        } else if (strstr(buf, "Unknown command") != NULL) {
            why = "this board's shell has no mkdir command";
        } else if (strstr(buf, "Usage:") != NULL) {
            why = "the link dropped the command";
        } else if (strstr(buf, "cannot create") != NULL ||
                   strstr(buf, "too long") != NULL) {
            why = buf;
        }
    }
    if (why != NULL) {
        if (err != NULL && errmax > 0u) {
            size_t n;

            snprintf(err, errmax, "%s", why);
            n = strlen(err);
            while (n > 0u && (err[n - 1] == '\n' || err[n - 1] == '\r')) {
                err[--n] = '\0';
            }
        }
        return -1;
    }
    return 0;
}

void
tiku_backend_tiku_set_write_timeout(tiku_backend_t *b, int ms)
{
    tiku_impl_t *impl = (b != NULL) ? b->impl : NULL;

    if (impl != NULL && impl->ns != NULL) {
        tiku_ns_set_write_timeout(impl->ns, ms);
    }
}

struct tiku_session *
tiku_backend_tiku_session(tiku_backend_t *b)
{
    tiku_impl_t *impl = (b != NULL) ? b->impl : NULL;

    return (impl != NULL) ? impl->sess : NULL;
}

int
tiku_backend_tiku_down(const tiku_backend_t *b)
{
    const tiku_impl_t *impl = (b != NULL) ? b->impl : NULL;

    return impl != NULL && impl->ns != NULL && tiku_ns_down(impl->ns);
}

/** @brief Service the namespace's notifications for up to @p timeout_ms.
 *  @return Nodes refreshed, or -1 on a link error. */
static int
do_pump(tiku_backend_t *b, int timeout_ms)
{
    tiku_impl_t *impl = b->impl;

    return tiku_ns_pump(impl->ns, timeout_ms);
}

/** @brief The device's sidecar state store, opened on first use under the
 *         device's key. */
static tiku_store_t *
tk_store(tiku_backend_t *b)
{
    tiku_impl_t *impl = b->impl;

    if (impl->store == NULL) {
        impl->store = tiku_store_sidecar_open(impl->devid);
    }
    return impl->store;
}

/** @brief Flush and free the state store, then the namespace, the session
 *         and the backend. */
static void
do_close(tiku_backend_t *b)
{
    tiku_impl_t *impl = b->impl;

    if (impl != NULL) {
        /* The store is flushed before the session closes: an unplug is
         * the device's unmount, and its arrangement is kept in the
         * sidecar. */
        if (impl->store != NULL) {
            (void)tiku_store_flush(impl->store);
            tiku_store_free(impl->store);
        }
        tiku_ns_free(impl->ns);
        tiku_session_free(impl->sess);
        free(impl);
    }
    free(b);
}

/*---------------------------------------------------------------------------*/
/* THE HAND                                                                  */
/*---------------------------------------------------------------------------*/

#define HAND(b) (&((tiku_impl_t *)(b)->impl)->hand)

/** @brief do_changes() with the hand held. */
static int
tk_changes(tiku_backend_t *b, tiku_model_change_t *out, int max,
           int *complete)
{
    int rc;

    pthread_mutex_lock(HAND(b));
    rc = do_changes(b, out, max, complete);
    pthread_mutex_unlock(HAND(b));
    return rc;
}

/** @brief do_path_of_id() with the hand held. */
static int
tk_path_of_id(tiku_backend_t *b, uint64_t id, char *out, size_t max)
{
    int rc;

    pthread_mutex_lock(HAND(b));
    rc = do_path_of_id(b, id, out, max);
    pthread_mutex_unlock(HAND(b));
    return rc;
}

/** @brief do_stat() with the hand held. */
static int
tk_stat(tiku_backend_t *b, const char *path, tiku_model_t *out)
{
    int rc;

    pthread_mutex_lock(HAND(b));
    rc = do_stat(b, path, out);
    pthread_mutex_unlock(HAND(b));
    return rc;
}

/** @brief do_list() with the hand held. */
static int
tk_list(tiku_backend_t *b, const char *path, tiku_entry_fn fn, void *ctx)
{
    int rc;

    pthread_mutex_lock(HAND(b));
    rc = do_list(b, path, fn, ctx);
    pthread_mutex_unlock(HAND(b));
    return rc;
}

/** @brief do_read() with the hand held. */
static int
tk_read(tiku_backend_t *b, const char *path, void *buf, size_t max)
{
    int rc;

    pthread_mutex_lock(HAND(b));
    rc = do_read(b, path, buf, max);
    pthread_mutex_unlock(HAND(b));
    return rc;
}

/** @brief do_write() with the hand held. */
static int
tk_write(tiku_backend_t *b, const char *path, const void *buf, size_t len,
         char *err, size_t errmax)
{
    int rc;

    pthread_mutex_lock(HAND(b));
    rc = do_write(b, path, buf, len, err, errmax);
    pthread_mutex_unlock(HAND(b));
    return rc;
}

/** @brief do_mkdir() with the hand held. */
static int
tk_mkdir(tiku_backend_t *b, const char *path, char *err, size_t errmax)
{
    int rc;

    pthread_mutex_lock(HAND(b));
    rc = do_mkdir(b, path, err, errmax);
    pthread_mutex_unlock(HAND(b));
    return rc;
}

/** @brief do_pump(), or nothing while a worker holds the hand. */
static int
tk_pump(tiku_backend_t *b, int timeout_ms)
{
    int rc;

    /* A pump does not wait for the hand: while a worker holds it, the pump
     * returns 0. */
    if (pthread_mutex_trylock(HAND(b)) != 0) {
        return 0;
    }
    rc = do_pump(b, timeout_ms);
    pthread_mutex_unlock(HAND(b));
    return rc;
}

/** @brief Wait for whoever holds the hand, then close the backend. */
static void
tk_close(tiku_backend_t *b)
{
    pthread_mutex_t *hand = HAND(b);

    pthread_mutex_lock(hand);
    pthread_mutex_unlock(hand);
    (void)pthread_mutex_destroy(hand);
    do_close(b);
}

static const tiku_backend_ops_t TIKU_OPS = {
    /* No setperm: a device node's capability bits are declared by the
     * device, and a NULL setperm shows the permissions grid read-only
     * (AW-078). */
    "tiku", tk_stat, tk_list, tk_read, tk_write, tk_pump, tk_changes,
    tk_path_of_id, NULL, tk_store, tk_close, tk_mkdir
};

int
tiku_backend_tiku_lease(tiku_backend_t *b,
                        void (*on_text)(const unsigned char *bytes,
                                        size_t n, void *ctx),
                        void *ctx)
{
    tiku_impl_t *impl = (b != NULL && b->ops == &TIKU_OPS) ? b->impl : NULL;

    if (impl == NULL || impl->sess == NULL || impl->leased || on_text == NULL) {
        return -1;
    }
    impl->leased = 1;
    tiku_session_on_text(impl->sess, on_text, ctx);
    return 0;
}

int
tiku_backend_tiku_lease_send(tiku_backend_t *b, const void *bytes, size_t n)
{
    tiku_impl_t *impl = (b != NULL && b->ops == &TIKU_OPS) ? b->impl : NULL;

    if (impl == NULL || !impl->leased) {
        return -1;
    }
    return tiku_session_send_raw(impl->sess, bytes, n);
}

void
tiku_backend_tiku_unlease(tiku_backend_t *b)
{
    tiku_impl_t *impl = (b != NULL && b->ops == &TIKU_OPS) ? b->impl : NULL;

    if (impl == NULL || !impl->leased) {
        return;
    }
    tiku_session_on_text(impl->sess, NULL, NULL);
    impl->leased = 0;
    /* The client's tail is drained and dropped before the sync: a prompt
     * in that tail would answer the sync and leave every later command one
     * prompt out of step. */
    tiku_session_drain(impl->sess, 150);
    /* A short sync: the line is quiet after the drain, and this can run on
     * the drawing loop. */
    (void)tiku_session_sync(impl->sess, 500);
}

int
tiku_backend_tiku_leased(const tiku_backend_t *b)
{
    return shell_is_leased(b);
}

tiku_backend_t *
tiku_backend_tiku_open(const char *port, int baud)
{
    return tiku_backend_tiku_open_within(port, baud, 4000, 1);
}

static tiku_backend_t *open_tx_within(tiku_tx_t *tx, int sync_ms,
                                      int recover);

tiku_backend_t *
tiku_backend_tiku_open_within(const char *port, int baud, int sync_ms,
                              int recover)
{
    tiku_tx_t *tx;
    char scan[8][256];

    if (port == NULL) {
        if (tiku_tx_scan_serial(scan, 8) <= 0) {
            return NULL;
        }
        port = scan[0];
    }
    tx = tiku_tx_open_serial(port, baud);
    if (tx == NULL) {
        return NULL;
    }
    return open_tx_within(tx, sync_ms, recover);
}

int
tiku_backend_tiku_port(const tiku_backend_t *b, char *out, size_t max)
{
    const tiku_impl_t *impl;
    const char *name;
    const char *at;
    size_t n;

    if (b == NULL || out == NULL || max == 0u || b->ops != &TIKU_OPS) {
        return 0;
    }
    out[0] = '\0';
    impl = b->impl;
    name = tiku_session_name(impl->sess);
    /* A serial transport names itself "<path> @<baud>"; anything else is
     * not a port and has no path to give. */
    at = strstr(name, " @");
    if (name[0] != '/' || at == NULL) {
        return 0;
    }
    n = (size_t)(at - name);
    if (n >= max) {
        n = max - 1u;
    }
    memcpy(out, name, n);
    out[n] = '\0';
    return (int)n;
}

/** @brief The first line of what @p s says to "cat @p path", into @p out;
 *         empty when the board cannot read it. */
static void
ident_cat(tiku_session_t *s, const char *path, char *out, size_t max)
{
    char cmd[64], buf[TIKU_VAL_MAX + 1];
    int n;
    size_t i = 0;

    out[0] = '\0';
    snprintf(cmd, sizeof cmd, "cat %s", path);
    n = tiku_session_cmd(s, cmd, buf, sizeof buf, 1500);
    if (n <= 0 || n >= TIKU_VAL_MAX || strstr(buf, "cannot read") != NULL) {
        return;
    }
    while (buf[i] != '\0' && buf[i] != '\r' && buf[i] != '\n' &&
           i + 1u < max) {
        out[i] = buf[i];
        i++;
    }
    out[i] = '\0';
}

int
tiku_backend_tiku_identify(const char *port, int baud, int sync_ms,
                           tiku_identity_t *out)
{
    tiku_tx_t *tx;
    tiku_session_t *s;
    int rc = -1;

    if (out == NULL) {
        return -1;
    }
    memset(out, 0, sizeof *out);
    tx = (port != NULL) ? tiku_tx_open_serial(port, baud) : NULL;
    if (tx == NULL) {
        return -1;
    }
    s = tiku_session_new(tx);
    if (s == NULL) {
        tiku_tx_close(tx);
        return -1;
    }
    if (tiku_session_sync(s, sync_ms) == 0) {
        ident_cat(s, "/sys/device/uid", out->uid, sizeof out->uid);
        ident_cat(s, "/sys/device/id", out->id, sizeof out->id);
        ident_cat(s, "/sys/device/mcu", out->mcu, sizeof out->mcu);
        ident_cat(s, "/sys/device/name", out->name, sizeof out->name);
        rc = 0;
    }
    tiku_session_free(s);       /* closes the port */
    return rc;
}

tiku_backend_t *
tiku_backend_tiku_open_uid(const char *uid, int baud)
{
    char scan[16][256];
    int found, i;

    if (uid == NULL || uid[0] == '\0') {
        return NULL;
    }
    /* Each port is asked who it is; only the board with the uid has its
     * namespace read. */
    found = tiku_tx_scan_serial(scan, 16);
    for (i = 0; i < found; i++) {
        tiku_identity_t id;

        if (tiku_backend_tiku_identify(scan[i], baud, 800, &id) == 0 &&
            strcmp(id.uid, uid) == 0) {
            return tiku_backend_tiku_open(scan[i], baud);
        }
    }
    return NULL;
}

tiku_backend_t *
tiku_backend_tiku_open_tx(tiku_tx_t *tx)
{
    return open_tx_within(tx, 4000, 1);
}

/**
 * @brief Open a device backend over @p tx: find the shell's prompt within
 *        @p sync_ms, read the manifest and the identity nodes.
 *
 * With @p recover set, a port that does not answer is sent a break and BYE
 * and asked once more.  @p tx is closed on failure.
 */
static tiku_backend_t *
open_tx_within(tiku_tx_t *tx, int sync_ms, int recover)
{
    tiku_backend_t *b;
    tiku_impl_t *impl;

    if (tx == NULL) {
        return NULL;
    }
    b = calloc(1, sizeof *b);
    impl = calloc(1, sizeof *impl);
    if (b == NULL || impl == NULL) {
        tiku_tx_close(tx);
        free(b);
        free(impl);
        return NULL;
    }
    {
        pthread_mutexattr_t attr;

        (void)pthread_mutexattr_init(&attr);
        (void)pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
        (void)pthread_mutex_init(&impl->hand, &attr);
        (void)pthread_mutexattr_destroy(&attr);
    }
            impl->sess = tiku_session_new(tx);
    {
        int answered = (impl->sess != NULL &&
                        tiku_session_sync(impl->sess, sync_ms) == 0);

        if (impl->sess != NULL && !answered && !recover) {
            /* A probe that got no answer writes nothing more to the port,
             * which may belong to something else. */
            tiku_session_free(impl->sess);
            impl->sess = NULL;
        } else if (impl->sess != NULL && !answered) {
        /* No shell prompt.  A board whose last client died mid-program is
         * still inside its interpreter, which answers ok>; a break and BYE
         * bring it back to the shell. */
        (void)tiku_session_send_raw(impl->sess, "\x03", 1u);
        tiku_session_drain(impl->sess, 150);
        (void)tiku_session_send_raw(impl->sess, "BYE\n", 4u);
        tiku_session_drain(impl->sess, 300);
                if (tiku_session_sync(impl->sess, 2000) != 0) {
            tiku_session_free(impl->sess);
            impl->sess = NULL;
        }
        }
    }
    if (impl->sess == NULL) {
        free(impl);
        free(b);
        return NULL;
    }
    impl->ns = tiku_ns_new(impl->sess);
    if (impl->ns == NULL || tiku_ns_load(impl->ns) < 0) {
        tiku_ns_free(impl->ns);
        tiku_session_free(impl->sess);
        free(impl);
        free(b);
        return NULL;
    }
    /* The manifest does not list what a dynamic directory holds, such as
     * the forms under gui/, so those are asked for; a manifest cut short
     * by the device's buffer is completed whole. */
    if (tiku_ns_truncated(impl->ns)) {
        (void)tiku_ns_complete(impl->ns, 4);
    } else {
        (void)tiku_ns_complete_dynamic(impl->ns, 2);
    }
    read_ident_field(impl->ns, "/sys/device/uid",  impl->ident.uid,
                     sizeof impl->ident.uid);
    read_ident_field(impl->ns, "/sys/device/id",   impl->ident.id,
                     sizeof impl->ident.id);
    read_ident_field(impl->ns, "/sys/device/mcu",  impl->ident.mcu,
                     sizeof impl->ident.mcu);
    read_ident_field(impl->ns, "/sys/device/name", impl->ident.name,
                     sizeof impl->ident.name);
    (void)tiku_ident_key(&impl->ident, impl->devid, sizeof impl->devid);
    snprintf(b->devid, sizeof b->devid, "%s", impl->devid);
    b->ops = &TIKU_OPS;
    b->impl = impl;
    return b;
}

const tiku_identity_t *
tiku_backend_tiku_identity(const tiku_backend_t *b)
{
    const tiku_impl_t *impl;

    if (b == NULL || b->ops != &TIKU_OPS) {
        return NULL;
    }
    impl = b->impl;
    return &impl->ident;
}
