/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_state.c - the three state stores behind one interface.
 *
 * The xattr store writes through to the file at once.  The sidecar and
 * memory stores hold records in a list, and the sidecar writes them to its
 * file on flush and on free: a magic string, then length-prefixed records
 * whose values are binary.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_state.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#if defined(__linux__) || defined(__APPLE__)
#include <sys/xattr.h>
#define TIKU_HAVE_XATTR 1
#endif

#include <stdint.h>

#define STATE_NODE_MAX 256
/* The longest attribute name a record keeps, with its terminator; the
 * xattr store's "user." prefix has room of its own. */
#define STATE_ATTR_MAX 64
#define STATE_VAL_MAX  4096
#define SIDECAR_MAGIC  "TRKSTATE1"

enum store_kind { STORE_XATTR, STORE_SIDECAR, STORE_MEMORY };

typedef struct state_rec {
    char              node[STATE_NODE_MAX];
    char              attr[STATE_ATTR_MAX];
    unsigned char    *val;
    size_t            len;
    struct state_rec *next;
} state_rec_t;

struct tiku_store {
    enum store_kind kind;
    state_rec_t    *recs;
    char            path[512];      /* sidecar file */
    int             dirty;
    int             writable;
};

/*---------------------------------------------------------------------------*/
/* record list (sidecar + memory)                                            */
/*---------------------------------------------------------------------------*/

/** @brief The record for @p attr of @p node, or NULL. */
static state_rec_t *
rec_find(tiku_store_t *s, const char *node, const char *attr)
{
    state_rec_t *r;

    for (r = s->recs; r != NULL; r = r->next) {
        if (strcmp(r->node, node) == 0 && strcmp(r->attr, attr) == 0) {
            return r;
        }
    }
    return NULL;
}

/** @brief Set @p attr of @p node to a copy of @p len bytes, adding the
 *         record when it is new.  @return 0, or -1 when refused. */
static int
rec_set(tiku_store_t *s, const char *node, const char *attr,
        const void *buf, size_t len)
{
    state_rec_t *r = rec_find(s, node, attr);
    unsigned char *copy;

    if (len > STATE_VAL_MAX) {
        return -1;
    }
    /* A name too long for a record is refused: a shortened name would not
     * match its lookup, and every write would add another record. */
    if (strlen(node) >= sizeof r->node || strlen(attr) >= sizeof r->attr) {
        return -1;
    }
    copy = malloc(len ? len : 1u);
    if (copy == NULL) {
        return -1;
    }
    memcpy(copy, buf, len);
    if (r == NULL) {
        r = calloc(1, sizeof *r);
        if (r == NULL) {
            free(copy);
            return -1;
        }
        snprintf(r->node, sizeof r->node, "%s", node);
        snprintf(r->attr, sizeof r->attr, "%s", attr);
        r->next = s->recs;
        s->recs = r;
    } else {
        free(r->val);
    }
    r->val = copy;
    r->len = len;
    s->dirty = 1;
    return 0;
}

/*---------------------------------------------------------------------------*/
/* sidecar file                                                              */
/*---------------------------------------------------------------------------*/

/** @brief $XDG_DATA_HOME/tracker/devices (else ~/.local/share/...),
 *         created if needed. */
static int
sidecar_dir(char *out, size_t max)
{
    const char *xdg = getenv("XDG_DATA_HOME");
    const char *home = getenv("HOME");
    char base[400];

    if (xdg != NULL && xdg[0] != '\0') {
        snprintf(base, sizeof base, "%s", xdg);
    } else if (home != NULL && home[0] != '\0') {
        snprintf(base, sizeof base, "%s/.local/share", home);
    } else {
        return -1;
    }
    snprintf(out, max, "%s/tracker/devices", base);
    {
        char part[512];
        char *p;
        snprintf(part, sizeof part, "%s", out);
        for (p = part + 1; *p != '\0'; p++) {
            if (*p == '/') {
                *p = '\0';
                (void)mkdir(part, 0700);
                *p = '/';
            }
        }
        (void)mkdir(part, 0700);
    }
    return 0;
}

/** @brief Read the sidecar file into @p s; a missing file is a device seen
 *         for the first time.  @return 0, or -1 when it is not a sidecar. */
static int
sidecar_load(tiku_store_t *s)
{
    FILE *f = fopen(s->path, "rb");
    char magic[16];

    if (f == NULL) {
        return 0;                      /* first sight of this device */
    }
    if (fread(magic, 1, strlen(SIDECAR_MAGIC), f) != strlen(SIDECAR_MAGIC) ||
        memcmp(magic, SIDECAR_MAGIC, strlen(SIDECAR_MAGIC)) != 0) {
        (void)fclose(f);
        return -1;
    }
    for (;;) {
        uint32_t nl, al, vl;
        char node[STATE_NODE_MAX], attr[STATE_ATTR_MAX];
        unsigned char val[STATE_VAL_MAX];

        if (fread(&nl, sizeof nl, 1, f) != 1) {
            break;
        }
        if (fread(&al, sizeof al, 1, f) != 1 ||
            fread(&vl, sizeof vl, 1, f) != 1) {
            break;
        }
        if (nl >= sizeof node || al >= sizeof attr || vl > sizeof val) {
            break;                     /* corrupt: stop, keep what is read */
        }
        if (fread(node, 1, nl, f) != nl || fread(attr, 1, al, f) != al ||
            fread(val, 1, vl, f) != vl) {
            break;
        }
        node[nl] = '\0';
        attr[al] = '\0';
        (void)rec_set(s, node, attr, val, vl);
    }
    (void)fclose(f);
    s->dirty = 0;
    return 0;
}

/** @brief Write every record to "<path>.new" and rename it over the
 *         sidecar.  @return 0, or -1. */
static int
sidecar_save(tiku_store_t *s)
{
    char tmp[540];
    FILE *f;
    state_rec_t *r;

    snprintf(tmp, sizeof tmp, "%s.new", s->path);
    f = fopen(tmp, "wb");
    if (f == NULL) {
        return -1;
    }
    if (fwrite(SIDECAR_MAGIC, 1, strlen(SIDECAR_MAGIC), f)
        != strlen(SIDECAR_MAGIC)) {
        (void)fclose(f);
        return -1;
    }
    for (r = s->recs; r != NULL; r = r->next) {
        uint32_t nl = (uint32_t)strlen(r->node);
        uint32_t al = (uint32_t)strlen(r->attr);
        uint32_t vl = (uint32_t)r->len;

        if (fwrite(&nl, sizeof nl, 1, f) != 1 ||
            fwrite(&al, sizeof al, 1, f) != 1 ||
            fwrite(&vl, sizeof vl, 1, f) != 1 ||
            fwrite(r->node, 1, nl, f) != nl ||
            fwrite(r->attr, 1, al, f) != al ||
            (vl > 0u && fwrite(r->val, 1, vl, f) != vl)) {
            (void)fclose(f);
            (void)unlink(tmp);
            return -1;
        }
    }
    if (fclose(f) != 0) {
        (void)unlink(tmp);
        return -1;
    }
    /* The new file is renamed over the old one, so a crash mid-write
     * leaves the previous file whole. */
    if (rename(tmp, s->path) != 0) {
        (void)unlink(tmp);
        return -1;
    }
    s->dirty = 0;
    return 0;
}

/*---------------------------------------------------------------------------*/
/* public                                                                    */
/*---------------------------------------------------------------------------*/

/** @brief A new, empty, writable store of @p kind. */
static tiku_store_t *
store_new(enum store_kind kind)
{
    tiku_store_t *s = calloc(1, sizeof *s);

    if (s != NULL) {
        s->kind = kind;
        s->writable = 1;
    }
    return s;
}

tiku_store_t *
tiku_store_xattr_open(void)
{
    return store_new(STORE_XATTR);
}

tiku_store_t *
tiku_store_memory_open(void)
{
    return store_new(STORE_MEMORY);
}

tiku_store_t *
tiku_store_sidecar_open(const char *devid)
{
    tiku_store_t *s;
    char dir[512];

    if (devid == NULL || devid[0] == '\0') {
        return NULL;
    }
    s = store_new(STORE_SIDECAR);
    if (s == NULL) {
        return NULL;
    }
    if (sidecar_dir(dir, sizeof dir) != 0) {
        s->writable = 0;               /* nowhere to persist          */
        return s;
    }
    /* A path too long is refused, not cut short: two devices' cut paths
     * could name one file. */
    if (strlen(dir) + strlen(devid) + 8u >= sizeof s->path) {
        s->writable = 0;
        return s;
    }
    /* Assembled by hand: the length is checked above, and snprintf here
     * draws a truncation warning from the compiler. */
    {
        size_t dl = strlen(dir), il = strlen(devid);
        memcpy(s->path, dir, dl);
        s->path[dl] = '/';
        memcpy(s->path + dl + 1u, devid, il);
        memcpy(s->path + dl + 1u + il, ".state", 7u);
    }
    (void)sidecar_load(s);
    return s;
}

void
tiku_store_free(tiku_store_t *s)
{
    state_rec_t *r;

    if (s == NULL) {
        return;
    }
    if (s->kind == STORE_SIDECAR && s->dirty && s->writable) {
        (void)sidecar_save(s);
    }
    r = s->recs;
    while (r != NULL) {
        state_rec_t *next = r->next;
        free(r->val);
        free(r);
        r = next;
    }
    free(s);
}

int
tiku_store_writable(const tiku_store_t *s)
{
    return (s != NULL) ? s->writable : 0;
}

/** @brief Map an attribute name into the user xattr namespace. */
static void
xattr_name(const char *attr, char *out, size_t max)
{
    snprintf(out, max, "user.%s", attr);
}

#if defined(TIKU_HAVE_XATTR)
/**
 * @brief getxattr() on either host.
 *
 * Darwin's calls take a position and an options word that Linux's do not;
 * both are 0: the whole value, no resource fork, and a symlink is followed
 * to the node the pose belongs to.
 */
static ssize_t
state_get(const char *node, const char *name, void *buf, size_t max)
{
#if defined(__APPLE__)
    return getxattr(node, name, buf, max, 0u, 0);
#else
    return getxattr(node, name, buf, max);
#endif
}

/** @brief setxattr() on either host, following a symlink. */
static int
state_set(const char *node, const char *name, const void *buf, size_t len)
{
#if defined(__APPLE__)
    return setxattr(node, name, buf, len, 0u, 0);
#else
    return setxattr(node, name, buf, len, 0);
#endif
}

/** @brief listxattr() on either host, following a symlink. */
static ssize_t
state_list(const char *node, char *names, size_t max)
{
#if defined(__APPLE__)
    return listxattr(node, names, max, 0);
#else
    return listxattr(node, names, max);
#endif
}
#endif

int
tiku_state_read(tiku_store_t *s, const char *node, const char *attr,
                    void *buf, size_t max)
{
    if (s == NULL || node == NULL || attr == NULL || buf == NULL) {
        return -1;
    }
    if (s->kind == STORE_XATTR) {
#if defined(TIKU_HAVE_XATTR)
        char name[STATE_ATTR_MAX + 8];
        ssize_t n;

        xattr_name(attr, name, sizeof name);
        n = state_get(node, name, buf, max);
        return (n >= 0) ? (int)n : -1;
#else
        return -1;
#endif
    }
    {
        state_rec_t *r = rec_find(s, node, attr);
        size_t n;

        if (r == NULL) {
            return -1;
        }
        n = (r->len < max) ? r->len : max;
        memcpy(buf, r->val, n);
        return (int)n;
    }
}

int
tiku_state_write(tiku_store_t *s, const char *node, const char *attr,
                     const void *buf, size_t len)
{
    if (s == NULL || node == NULL || attr == NULL || buf == NULL) {
        return -1;
    }
    if (!s->writable) {
        return -1;                     /* the store cannot persist      */
    }
    if (s->kind == STORE_XATTR) {
#if defined(TIKU_HAVE_XATTR)
        char name[STATE_ATTR_MAX + 8];

        xattr_name(attr, name, sizeof name);
        return (state_set(node, name, buf, len) == 0) ? 0 : -1;
#else
        return -1;
#endif
    }
    return rec_set(s, node, attr, buf, len);
}

int
tiku_state_each(tiku_store_t *s, const char *node,
                    int (*fn)(const char *attr, const void *val, size_t len,
                              void *ctx), void *ctx)
{
    int n = 0;

    if (s == NULL || node == NULL || fn == NULL) {
        return 0;
    }
    if (s->kind == STORE_XATTR) {
#if defined(TIKU_HAVE_XATTR)
        char *names;
        ssize_t need_names = state_list(node, NULL, 0u);
        ssize_t len;
        ssize_t at;
        int bad = 0;

        if (need_names <= 0) {
            return 0;
        }
        names = malloc((size_t)need_names);
        if (names == NULL) {
            return -1;
        }
        len = state_list(node, names, (size_t)need_names);
        if (len < 0) {
            free(names);
            return -1;
        }
        for (at = 0; at < len; at += (ssize_t)strlen(names + at) + 1) {
            unsigned char *val;
            const char *nm = names + at;
            ssize_t need, got;
            int stop;

            /* Only the _trk/ attributes; other programs' attributes on the
             * file are skipped. */
            if (strncmp(nm, "user._trk/", 10) != 0) {
                continue;
            }
            /* A value that cannot be read, such as one removed or grown
             * since the listing, is skipped and the walk reports -1. */
            need = state_get(node, nm, NULL, 0u);
            if (need < 0) {
                bad = 1;
                continue;
            }
            val = malloc(need > 0 ? (size_t)need : 1u);
            if (val == NULL) {
                bad = 1;
                continue;
            }
            got = state_get(node, nm, val, (size_t)need);
            if (got < 0) {
                free(val);
                bad = 1;
                continue;
            }
            n++;
            stop = fn(nm + 5, val, (size_t)got, ctx);
            free(val);
            if (stop != 0) {
                break;
            }
        }
        free(names);
        return bad ? -1 : n;
#else
        return 0;
#endif
    }
    {
        state_rec_t *r;

        for (r = s->recs; r != NULL; r = r->next) {
            if (strcmp(r->node, node) != 0) {
                continue;
            }
            n++;
            if (fn(r->attr, r->val, r->len, ctx) != 0) {
                break;
            }
        }
    }
    return n;
}

int
tiku_state_forget(tiku_store_t *s, const char *node)
{
    state_rec_t **p;
    int n = 0;

    if (s == NULL || node == NULL) {
        return -1;
    }
    p = &s->recs;
    while (*p != NULL) {
        if (strcmp((*p)->node, node) == 0) {
            state_rec_t *dead = *p;
            *p = dead->next;
            free(dead->val);
            free(dead);
            n++;
            s->dirty = 1;
        } else {
            p = &(*p)->next;
        }
    }
    return n;
}

int
tiku_store_flush(tiku_store_t *s)
{
    if (s == NULL) {
        return -1;
    }
    if (s->kind == STORE_SIDECAR && s->dirty && s->writable) {
        return sidecar_save(s);
    }
    return 0;
}

int
tiku_store_count(const tiku_store_t *s)
{
    const state_rec_t *r;
    int n = 0;

    for (r = (s != NULL) ? s->recs : NULL; r != NULL; r = r->next) {
        n++;
    }
    return n;
}

const char *
tiku_desktop_dir(char *out, size_t max)
{
    const char *set = getenv("TIKU_DESKTOP");
    const char *home = getenv("HOME");

    if (out == NULL || max == 0u) {
        return out;
    }
    if (set != NULL && set[0] != '\0') {
        snprintf(out, max, "%s", set);
        return out;
    }
    snprintf(out, max, "%s/.config/tracker/Desktop",
             (home != NULL && home[0] != '\0') ? home : "");
    return out;
}

const char *
tiku_fonts_dir(char *out, size_t max)
{
    const char *set = getenv("TIKU_FONTS");
    const char *home = getenv("HOME");

    if (out == NULL || max == 0u) {
        return out;
    }
    if (set != NULL && set[0] != '\0') {
        snprintf(out, max, "%s", set);
        return out;
    }
    snprintf(out, max, "%s/.config/tracker/fonts",
             (home != NULL && home[0] != '\0') ? home : "");
    return out;
}

const char *
tiku_sounds_dir(char *out, size_t max)
{
    const char *set = getenv("TIKU_SOUNDS");
    const char *home = getenv("HOME");

    if (out == NULL || max == 0u) {
        return out;
    }
    if (set != NULL && set[0] != '\0') {
        snprintf(out, max, "%s", set);
        return out;
    }
    snprintf(out, max, "%s/.config/tracker/sounds",
             (home != NULL && home[0] != '\0') ? home : "");
    return out;
}

/** @brief Create a directory, accepting an existing directory or its symlink. */
static int
make_parent(const char *path)
{
    struct stat st;

    if (mkdir(path, 0755) == 0) {
        return 0;
    }
    return errno == EEXIST && stat(path, &st) == 0 && S_ISDIR(st.st_mode)
             ? 0 : -1;
}

int
tiku_state_mkparents(const char *path)
{
    char dir[1024];
    char *slash;
    size_t i;

    if (path == NULL || path[0] == '\0' || strlen(path) >= sizeof dir) {
        return -1;
    }
    snprintf(dir, sizeof dir, "%s", path);
    slash = strrchr(dir, '/');
    if (slash == NULL || slash == dir) {
        return 0;               /* no directory part to make */
    }
    *slash = '\0';
    /* Each component in turn, because a fresh home has neither .config nor
     * what is under it. */
    for (i = 1u; dir[i] != '\0'; i++) {
        if (dir[i] == '/') {
            dir[i] = '\0';
            if (make_parent(dir) != 0) {
                return -1;
            }
            dir[i] = '/';
        }
    }
    return make_parent(dir);
}
