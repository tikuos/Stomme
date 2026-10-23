/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_fs_trash.c - the Trash: one per writable volume, made where it is
 * missing; where a trashed item came from, and putting it back; emptying
 * every Trash; and what the Delete key asks.  Contract rows FS-038, FS-039,
 * FS-040, FS-044, FS-047 and FS-048.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_fs_private.h"

/** @brief Create @p path and every missing folder above it. */
int
fop_make_path(const char *path)
{
    char buf[TIKU_PATH_MAX];
    char *p;

    snprintf(buf, sizeof buf, "%s", path);
    for (p = buf + 1; *p != '\0'; p++) {
        if (*p != '/') {
            continue;
        }
        *p = '\0';
        (void)mkdir(buf, 0755);
        *p = '/';
    }
    return mkdir(buf, 0755);
}

/**
 * @brief Whether the store can hold a Trash, answered for its root: a
 *        device has none, as its nodes are not files (FS-044).
 *
 * Whether one item can be trashed is answered by tiku_fs_trash_dir().
 */
int
fop_fs_has_trash(const tiku_fs_t *fs)
{
    return (fs != NULL && fs->backend != NULL && !fop_on_device(fs, "/"));
}

/**
 * @brief The root of the filesystem @p path lives on.
 *
 * Walked up until the device changes.  The Trash is on the item's own
 * filesystem, so trashing is a rename.
 */
static void
volume_root(const char *path, char *out, size_t max)
{
    char cur[TIKU_PATH_MAX], parent[TIKU_PATH_MAX];
    struct stat st, pst;

    snprintf(out, max, "/");
    if (path == NULL || path[0] == '\0') {
        return;
    }
    snprintf(cur, sizeof cur, "%s", path);
    /* The item itself may be gone, as when a trashed item is put back:
     * the walk starts at the nearest ancestor that exists. */
    while (stat(cur, &st) != 0) {
        char *slash = strrchr(cur, '/');

        if (slash == NULL || slash == cur) {
            return;                     /* nothing above it exists       */
        }
        *slash = '\0';
    }
    for (;;) {
        char *slash = strrchr(cur, '/');

        if (slash == NULL) {
            break;
        }
        if (slash == cur) {
            snprintf(parent, sizeof parent, "/");
        } else {
            snprintf(parent, sizeof parent, "%.*s", (int)(slash - cur), cur);
        }
        if (stat(parent, &pst) != 0 || pst.st_dev != st.st_dev) {
            snprintf(out, max, "%s", cur);   /* the parent is elsewhere   */
            return;
        }
        if (strcmp(parent, "/") == 0) {
            snprintf(out, max, "/");
            return;
        }
        snprintf(cur, sizeof cur, "%s", parent);
    }
}

/** @brief Make @p dir/.Trash if it is not there.  @return 0 on success. */
static int
trash_under(const char *dir, char *out, size_t max)
{
    struct stat st;
    int n;

    /* One slash between, so "/" gives "/.Trash", not "//.Trash". */
    n = snprintf(out, max, "%s%s%s", dir,
                 (dir[0] != '\0' && dir[strlen(dir) - 1u] == '/') ? "" : "/",
                 TIKU_TRASH_NAME);
    if (n < 0 || (size_t)n >= max) {
        return -1;
    }
    if (stat(out, &st) == 0) {
        return S_ISDIR(st.st_mode) ? 0 : -1;
    }
    return (mkdir(out, 0755) == 0) ? 0 : -1;
}

/** @brief tiku_fs_trash_dirs(): the Trash of every writable volume in the
 *         mount table. */
int
fop_fs_trash_dirs(tiku_fs_t *fs, char out[][TIKU_PATH_MAX],
                       int max)
{
    tiku_volumes_t vs;

    if (fs == NULL || out == NULL || max <= 0 ||
        !tiku_fs_has_trash(fs)) {
        return 0;
    }
    if (fop_on_device(fs, "/") || tiku_volumes_scan(&vs) <= 0) {
        /* With no volume table, the one Trash is the home folder's. */
        if (tiku_fs_trash_dir(fs, NULL, out[0], TIKU_PATH_MAX) ==
                0) {
            return 1;
        }
        return 0;
    }
    return tiku_fs_trash_dirs_of(fs,
               (const struct tiku_volumes *)&vs, out, max);
}

/** @brief Make every writable volume's Trash.  @return How many there
 *         are. */
int
fop_fs_prepare_trash(tiku_fs_t *fs)
{
    char dirs[TIKU_VOLUME_MAX][TIKU_PATH_MAX];

    if (fs == NULL) {
        return 0;
    }
    /* Called at startup, not from tiku_fs_new(): making an engine creates
     * no directories. */
    return tiku_fs_trash_dirs(fs, dirs, TIKU_VOLUME_MAX);
}

/** @brief The Trash of each writable volume in @p v, each listed once. */
int
fop_fs_trash_dirs_of(tiku_fs_t *fs, const struct tiku_volumes *v,
                          char out[][TIKU_PATH_MAX], int max)
{
    const tiku_volumes_t *vsp = (const tiku_volumes_t *)v;
    int i, n = 0;

    if (fs == NULL || vsp == NULL || out == NULL || max <= 0) {
        return 0;
    }
    for (i = 0; i < vsp->n && n < max; i++) {
        char here[TIKU_PATH_MAX];
        int k, dupe = 0;

        if (vsp->v[i].read_only) {
            /* A read-only volume has no Trash; asking would try to make
             * one. */
            continue;
        }
        if (tiku_fs_trash_dir(fs, vsp->v[i].mount, here,
                                  sizeof here) != 0) {
            continue;
        }
        for (k = 0; k < n; k++) {
            if (strcmp(out[k], here) == 0) {
                dupe = 1;       /* two mounts resolving to one Trash */
                break;
            }
        }
        if (!dupe) {
            snprintf(out[n++], TIKU_PATH_MAX, "%s", here);
        }
    }
    return n;
}

/** @brief The Trash for @p path, or for the home folder when @p path is
 *         NULL, made when absent; see tiku_fs_trash_dir().  @return 0, or
 *         -1. */
int
fop_fs_trash_dir(tiku_fs_t *fs, const char *path, char *out,
                      size_t max)
{
    char root[TIKU_PATH_MAX];
    const char *home = getenv("HOME");
    struct stat rs, hs;

    if (fs == NULL || out == NULL || !tiku_fs_has_trash(fs) ||
        (path != NULL && fop_on_device(fs, path))) {
        /* Asked per item: with a board mounted beside the disk, the disk
         * has a Trash and the board has none. */
        return -1;
    }
    /* Resolved from the item's own filesystem, so trashing is a
     * rename. */
    volume_root((path != NULL) ? path : home, root, sizeof root);
    /* The home folder's Trash first, whenever home is on that filesystem:
     * a volume root can be writable by everyone (a sticky /tmp), and a
     * Trash made there would belong to whoever trashed first.  Home is per
     * user and on the same filesystem, so the move stays a rename. */
    if (home != NULL && home[0] != '\0' && stat(home, &hs) == 0 &&
        stat(root, &rs) == 0 && hs.st_dev == rs.st_dev &&
        trash_under(home, out, max) == 0) {
        return 0;
    }
    /* A volume the home does not live on keeps its Trash at its root. */
    return (trash_under(root, out, max) == 0) ? 0 : -1;
}

/** @brief Where the trashed item at @p path came from, into @p out.
 *  @return 1, or 0 when nothing was recorded. */
int
fop_fs_original_path(tiku_fs_t *fs, const char *path, char *out,
                          size_t max)
{
    tiku_store_t *store;
    int n;

    if (fs == NULL || path == NULL || out == NULL || max == 0u ||
        fs->backend->ops->state_store == NULL) {
        return 0;
    }
    store = fs->backend->ops->state_store(fs->backend);
    if (store == NULL) {
        return 0;
    }
    n = tiku_state_read(store, path, TIKU_ATTR_ORIGINAL, out,
                            max - 1u);
    if (n <= 0) {
        out[0] = '\0';
        return 0;
    }
    out[n] = '\0';
    return 1;
}

/** @brief Remember where @p trashed came from, under its trashed name. */
void
fop_record_origin(tiku_fs_t *fs, const char *trashed, const char *origin)
{
    tiku_store_t *store;

    if (fs->backend->ops->state_store == NULL) {
        return;
    }
    store = fs->backend->ops->state_store(fs->backend);
    if (store == NULL || !tiku_store_writable(store)) {
        return;
    }
    /* Recorded under the path the item now has, in the Trash. */
    (void)tiku_state_write(store, trashed, TIKU_ATTR_ORIGINAL,
                               origin, strlen(origin));
    (void)tiku_store_flush(store);
}

/** @brief The Delete key's question for @p path; see tiku_delete_q_t. */
tiku_delete_q_t
fop_fs_delete_question(const tiku_fs_t *fs, const char *path,
                            int shift_held)
{
    if (fs == NULL) {
        return TIKU_DELETE_STRAIGHT;
    }
    if (shift_held) {
        return TIKU_DELETE_STRAIGHT;    /* the user has decided       */
    }
    {
        /* Asked of this item's filesystem: with no Trash there, the item
         * is deleted outright (FS-044). */
        char trash[TIKU_PATH_MAX];

        if (tiku_fs_trash_dir((tiku_fs_t *)fs, path, trash,
                                  sizeof trash) != 0) {
            return TIKU_DELETE_STRAIGHT;
        }
    }
    if (path != NULL && fop_under_trash(path)) {
        /* Inside the Trash the item is deleted outright (FS-047). */
        return TIKU_DELETE_STRAIGHT;
    }
    return TIKU_DELETE_ASK;
}

/** @brief Put trashed items back where they came from; see
 *         tiku_fs_restore().  @return Number restored. */
int
fop_fs_restore(tiku_fs_t *fs, const char *const *paths, int n,
                    char *err, size_t errmax)
{
    int i, done = 0;

    if (fs == NULL || paths == NULL) {
        return 0;
    }
    if (err != NULL && errmax > 0u) {
        err[0] = '\0';
    }
    for (i = 0; i < n; i++) {
        char origin[TIKU_PATH_MAX];
        struct stat st;

        if (!tiku_fs_original_path(fs, paths[i], origin, sizeof origin)) {
            if (err != NULL) {
                snprintf(err, errmax,
                         "'%s' does not remember where it came from",
                         fop_leaf_of(paths[i]));
            }
            continue;
        }
        if (stat(origin, &st) == 0) {
            /* Something at the original path is not overwritten: the item
             * stays in the Trash (FS-040). */
            if (err != NULL) {
                snprintf(err, errmax,
                         "cannot restore '%s': something is there already",
                         fop_leaf_of(origin));
            }
            continue;
        }
        {   /* The folders it lived in may have gone since; make them. */
            char parent[TIKU_PATH_MAX];
            char *slash;

            snprintf(parent, sizeof parent, "%s", origin);
            slash = strrchr(parent, '/');
            if (slash != NULL && slash != parent) {
                *slash = '\0';
                if (stat(parent, &st) != 0) {
                    (void)fop_make_path(parent);
                }
            }
        }
        if (rename(paths[i], origin) != 0) {
            if (err != NULL) {
                snprintf(err, errmax, "cannot restore '%s' (%s)",
                         fop_leaf_of(paths[i]), strerror(errno));
            }
            continue;
        }
        fop_landed(fs, paths[i], origin);
        done++;
    }
    return done;
}

/** @brief Start deleting what every Trash holds.  @return Items queued. */
int
fop_fs_empty_trash(tiku_fs_t *fs)
{
    char dirs[TIKU_VOLUME_MAX][TIKU_PATH_MAX];
    char paths[FS_QUEUE_MAX][TIKU_PATH_MAX];
    const char *sel[FS_QUEUE_MAX];
    int n = 0;
    int nd, k;

    if (fs == NULL) {
        return 0;
    }
    /* Every volume's Trash at once, the set the union window lists
     * (FS-048, PVN-027); a read-only volume has none.  The Trash folders
     * stay: only what is in them is queued. */
    nd = tiku_fs_trash_dirs(fs, dirs, TIKU_VOLUME_MAX);
    for (k = 0; k < nd; k++) {
        DIR *d = opendir(dirs[k]);
        struct dirent *e;

        if (d == NULL) {
            continue;
        }
        while ((e = readdir(d)) != NULL && n < FS_QUEUE_MAX) {
            if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) {
                continue;
            }
            if ((int)snprintf(paths[n], sizeof paths[n], "%s/%s", dirs[k],
                              e->d_name) >= (int)sizeof paths[n]) {
                continue;               /* a name that cannot be addressed */
            }
            sel[n] = paths[n];
            n++;
        }
        (void)closedir(d);
    }
    if (n == 0) {
        return 0;
    }
    /* Deleted, not trashed: a trashing drops anything inside the Trash
     * (FS-043, FS-048). */
    (void)tiku_fs_delete(fs, sel, n);
    return n;
}
