/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_fs_queue.c - jobs started: the selection queued, less what the
 * rules refuse, once the volumes and the free space allow the job.  Also
 * the space a copy needs, whether a path is in the Trash, and which moves
 * must be confirmed.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_fs_private.h"

/**
 * @brief Whether a Trash sits directly inside @p path: <path>/.Trash
 *        exists.
 */
static int
contains_trash(tiku_fs_t *fs, const char *path)
{
    char probe[TIKU_PATH_MAX];
    size_t n;

    if (path == NULL || path[0] == '\0') {
        return 0;
    }
    n = strlen(path);
    while (n > 1u && path[n - 1u] == '/') {
        n--;                            /* a trailing slash is not a level */
    }
    if (n + sizeof "/.Trash" > sizeof probe) {
        return 0;
    }
    memcpy(probe, path, n);
    probe[n] = '\0';
    return fop_exists(fs, probe, ".Trash");
}

/**
 * @brief Queue @p paths for @p op into @p dst, leaving out what the rules
 *        refuse, and check what the volumes allow.
 *
 * Requests above FS_QUEUE_MAX are rejected before queuing any path.
 *
 * @return 0 when the job starts, or -1 when the engine is busy or a volume
 *         refuses the whole job.
 */
static int
start(tiku_fs_t *fs, tiku_op_t op, const char *const *paths, int n,
      const char *dst, tiku_conflict_t policy)
{
    int i;

    if (fs == NULL || tiku_fs_busy(fs)) {
        return -1;
    }
    if (n > FS_QUEUE_MAX) {
        snprintf(fs->prog.error, sizeof fs->prog.error,
                 "Too many paths in one operation (maximum %d)", FS_QUEUE_MAX);
        return -1;
    }
    fs->prog.error[0] = '\0';
    fs->prog.skipped = 0;
    fs->qcount = 0;
    for (i = 0; i < n; i++) {
        if ((op == TIKU_OP_COPY || op == TIKU_OP_MOVE) &&
            dst != NULL) {
            /* Every copy and move passes this refusal check (FS-023,
             * FS-024, FS-025). */
            char why[200];

            if (tiku_fs_check_move(fs, paths[i], dst, why,
                                       sizeof why) != TIKU_REFUSE_NONE) {
                snprintf(fs->prog.error, sizeof fs->prog.error, "%s", why);
                fs->prog.skipped++;
                continue;
            }
        }
        /* A trashing drops the Trash, anything in it and anything that
         * holds it, without a message (FS-043).  Other operations keep
         * them, so a folder that holds a Trash can still be copied. */
        if (op == TIKU_OP_TRASH &&
            (fop_under_trash(paths[i]) ||
             contains_trash(fs, paths[i]))) {
            continue;
        }
        if ((op == TIKU_OP_TRASH || op == TIKU_OP_DELETE) &&
            !fop_on_device(fs, paths[i])) {
            /* A whole volume is refused, with a message: trashed as a
             * directory, it would be moved into its own Trash. */
            tiku_volumes_t vs;

            if (tiku_volumes_scan(&vs) > 0 &&
                tiku_volume_is_root(&vs, paths[i])) {
                snprintf(fs->prog.error, sizeof fs->prog.error,
                         "\"%.40s\" is a disk, not an item: it can be "
                         "unmounted but not moved, copied or deleted.",
                         fop_leaf_of(paths[i]));
                fs->prog.skipped++;
                continue;
            }
        }
        fs->qexpanded[fs->qcount] = 0;
        snprintf(fs->queue[fs->qcount++], TIKU_PATH_MAX, "%s", paths[i]);
    }
    fs->qcursor = 0;
    fs->policy = policy;
    snprintf(fs->dst, sizeof fs->dst, "%s", dst ? dst : "");
    {   /* Progress is cleared except for the refusal recorded while the
         * queue was built, and the skip count: they say why items are
         * missing. */
        char kept[sizeof fs->prog.error];
        int skipped = fs->prog.skipped;

        snprintf(kept, sizeof kept, "%s", fs->prog.error);
        memset(&fs->prog, 0, sizeof fs->prog);
        snprintf(fs->prog.error, sizeof fs->prog.error, "%s", kept);
        fs->prog.skipped = skipped;
    }
    {
        /* Counted before the job starts: the collision question's form
         * depends on how many there are (FS-017). */
        int k, collisions = 0;

        for (k = 0; dst != NULL && k < fs->qcount; k++) {
            if (fop_exists(fs, dst, fop_leaf_of(fs->queue[k]))) {
                collisions++;
            }
        }
        fs->prog.collisions = collisions;
    }
    {   /* What the volumes allow, before a byte moves: a destination
         * that takes no write refuses the job (FS-012); a move off a
         * read-only volume becomes a copy, which is how a file comes off
         * a disc (FS-013); and trashing or deleting an item on a
         * read-only volume is refused with the volume named (FS-045). */
        tiku_volumes_t vs;
        char why[200];
        int k;

        /* The volume rules apply to local filesystems, decided by the
         * destination, or by the first item when there is none. */
        if (!fop_on_device(fs, (dst != NULL) ? dst : paths[0]) &&
            tiku_volumes_scan(&vs) > 0) {
            if (dst != NULL &&
                !tiku_volume_may_write(&vs, dst, 1, why, sizeof why)) {
                snprintf(fs->prog.error, sizeof fs->prog.error,
                         "You can't move or copy items to read-only "
                         "volumes.");
                fs->prog.error_ask = 1;
                fs->prog.error_can_continue = 0;
                fs->qcursor = fs->qcount;
                return -1;
            }
            if (op == TIKU_OP_MOVE) {
                for (k = 0; k < fs->qcount; k++) {
                    if (!tiku_volume_may_write(&vs, fs->queue[k], 1,
                                                   why, sizeof why)) {
                        op = TIKU_OP_COPY;
                        break;
                    }
                }
            } else if (op == TIKU_OP_TRASH || op == TIKU_OP_DELETE) {
                for (k = 0; k < fs->qcount; k++) {
                    if (!tiku_volume_may_write(&vs, fs->queue[k], 1,
                                                   why, sizeof why)) {
                        snprintf(fs->prog.error, sizeof fs->prog.error,
                                 "%s", why);
                        fs->prog.error_ask = 1;
                        fs->prog.error_can_continue = 0;
                        fs->qcursor = fs->qcount;
                        return -1;
                    }
                }
            }
        }
    }
    if (op == TIKU_OP_COPY && dst != NULL &&
        !tiku_fs_in_trash(fs, dst) && !fop_on_device(fs, dst)) {
        /* The space needed is summed before a byte moves, and a copy that
         * would not fit does not start.  Skipped for the Trash, where the
         * copy becomes a move, and for a device, whose free space is not
         * known here (FS-015). */
        const char *q[FS_QUEUE_MAX];
        int k;
        uint64_t total;

        for (k = 0; k < fs->qcount; k++) {
            q[k] = fs->queue[k];
        }
        total = tiku_fs_items_size(q, fs->qcount);
        if (!tiku_fs_fits(total, tiku_fs_free_bytes(dst))) {
            snprintf(fs->prog.error, sizeof fs->prog.error,
                     "There is not enough free space on the destination "
                     "volume.");
            fs->prog.error_ask = 1;
            fs->prog.error_can_continue = 0;
            fs->qcursor = fs->qcount;
            return -1;
        }
    }
    if (op == TIKU_OP_COPY && dst != NULL &&
        tiku_fs_in_trash(fs, dst)) {
        /* A copy into the Trash is a move (FS-014). */
        op = TIKU_OP_MOVE;
    }
    fs->prog.op = op;
    fs->prog.total = fs->qcount;
    snprintf(fs->prog.to, sizeof fs->prog.to, "%s", dst ? dst : "");
    fs->cancelling = 0;             /* a new job is not the cancelled one */
    fop_undo_reset(fs, op);
    return 0;
}

/** @brief Start a copy job: start() for TIKU_OP_COPY. */
int
fop_fs_copy(tiku_fs_t *fs, const char *const *paths, int n,
                 const char *dst_dir, tiku_conflict_t policy)
{
    return start(fs, TIKU_OP_COPY, paths, n, dst_dir, policy);
}

/** @brief Start a move job: start() for TIKU_OP_MOVE. */
int
fop_fs_move(tiku_fs_t *fs, const char *const *paths, int n,
                 const char *dst_dir, tiku_conflict_t policy)
{
    return start(fs, TIKU_OP_MOVE, paths, n, dst_dir, policy);
}

/** @brief Start a duplicate job, every collision renamed. */
int
fop_fs_duplicate(tiku_fs_t *fs, const char *const *paths, int n)
{
    return start(fs, TIKU_OP_DUPLICATE, paths, n, NULL,
                 TIKU_CONFLICT_RENAME);
}

/** @brief Start a trashing job. */
int
fop_fs_trash(tiku_fs_t *fs, const char *const *paths, int n)
{
    return start(fs, TIKU_OP_TRASH, paths, n, NULL,
                 TIKU_CONFLICT_SKIP);
}

uint64_t
tiku_fs_free_bytes(const char *path)
{
    struct statvfs v;

    if (path == NULL || statvfs(path, &v) != 0) {
        return 0u;
    }
    return (uint64_t)v.f_bavail * (uint64_t)v.f_frsize;
}

uint64_t
tiku_fs_items_size(const char *const *paths, int n)
{
    uint64_t total = 0u;
    int i;

    for (i = 0; i < n; i++) {
        struct stat st;
        DIR *d;

        if (paths[i] == NULL || lstat(paths[i], &st) != 0) {
            continue;
        }
        if (!S_ISDIR(st.st_mode)) {
            /* A file is charged its size and one block: its st_blksize
             * clamped to [1024, 8192], or 2048 when the filesystem gives
             * none. */
            unsigned long blk = (st.st_blksize > 0)
                                    ? (unsigned long)st.st_blksize : 2048ul;

            if (blk < 1024ul) { blk = 1024ul; }
            if (blk > 8192ul) { blk = 8192ul; }
            total += (uint64_t)st.st_size + (uint64_t)blk;
            continue;
        }
        total += 1024u;             /* one block for the directory itself */
        d = opendir(paths[i]);
        if (d != NULL) {
            struct dirent *e;

            while ((e = readdir(d)) != NULL) {
                char child[TIKU_PATH_MAX];
                const char *one[1];

                if (strcmp(e->d_name, ".") == 0 ||
                    strcmp(e->d_name, "..") == 0) {
                    continue;
                }
                if (fop_join(child, sizeof child, paths[i], e->d_name) != 0) {
                    continue;
                }
                one[0] = child;
                total += tiku_fs_items_size(one, 1);
            }
            (void)closedir(d);
        }
    }
    return total;
}

int
tiku_fs_fits(uint64_t total, uint64_t free_bytes)
{
    /* Four kilobytes of headroom; landing exactly on the free space
     * counts as not fitting. */
    if (free_bytes == 0u) {
        return 1;                   /* unknown: not a reason to refuse    */
    }
    return !((total + 4u * 1024u) >= free_bytes);
}

/** @brief Whether @p path is @p dir or something under it. */
static int
under(const char *path, const char *dir)
{
    size_t n;

    if (path == NULL || dir == NULL || dir[0] == '\0') {
        return 0;
    }
    n = strlen(dir);
    while (n > 1u && dir[n - 1u] == '/') {
        n--;
    }
    return (strncmp(path, dir, n) == 0 &&
            (path[n] == '\0' || path[n] == '/'));
}

tiku_confirm_t
tiku_fs_confirm_move(const char *path)
{
    /* The system directories: a move out of one needs Shift held to
     * confirm. */
    static const char *system_dirs[] = {
        "/bin", "/boot", "/etc", "/lib", "/lib64", "/sbin", "/usr", "/var"
    };
    const char *home = getenv("HOME");
    size_t i;

    if (path == NULL || path[0] == '\0') {
        return TIKU_CONFIRM_NONE;
    }
    for (i = 0; i < sizeof system_dirs / sizeof system_dirs[0]; i++) {
        if (under(path, system_dirs[i])) {
            return TIKU_CONFIRM_OVERRIDE;
        }
    }
    if (home != NULL && home[0] != '\0') {
        char cfg[TIKU_PATH_MAX];
        const char *xdg = getenv("XDG_CONFIG_HOME");

        /* Home itself only, by exact match: a file inside home moves
         * without a question. */
        if (strcmp(path, home) == 0) {
            return TIKU_CONFIRM_OVERRIDE;
        }
        if (xdg != NULL && xdg[0] != '\0' && under(path, xdg)) {
            return TIKU_CONFIRM_ASK;
        }
        if (snprintf(cfg, sizeof cfg, "%s/.config", home) > 0 &&
            under(path, cfg)) {
            return TIKU_CONFIRM_ASK;
        }
    }
    return TIKU_CONFIRM_NONE;
}

/** @brief Whether @p path is its filesystem's Trash or inside it. */
int
fop_fs_in_trash(tiku_fs_t *fs, const char *path)
{
    char trash[TIKU_PATH_MAX];
    size_t n;

    if (fs == NULL || path == NULL ||
        tiku_fs_trash_dir(fs, path, trash, sizeof trash) != 0) {
        return 0;
    }
    n = strlen(trash);
    /* A prefix test on a whole component, so a sibling named ".Trashcan"
     * is not mistaken for something inside the Trash. */
    return (strncmp(path, trash, n) == 0 &&
            (path[n] == '\0' || path[n] == '/'));
}

/** @brief Start a job linking @p paths into @p dst. */
int
fop_fs_link(tiku_fs_t *fs, const char *const *paths, int n,
                 const char *dst, int relative)
{
    if (fs == NULL) {
        return -1;
    }
    fs->link_relative = relative;
    fs->link_as[0] = '\0';
    return start(fs, TIKU_OP_LINK, paths, n, dst,
                 TIKU_CONFLICT_ASK);
}

/** @brief Start a job making one link to @p path in @p dst, named
 *         @p name. */
int
fop_fs_link_as(tiku_fs_t *fs, const char *path, const char *dst,
           const char *name, int relative)
{
    const char *paths[1] = { path };

    if (fs == NULL || name == NULL || name[0] == '\0') {
        return -1;
    }
    fs->link_relative = relative;
    snprintf(fs->link_as, sizeof fs->link_as, "%s", name);
    return start(fs, TIKU_OP_LINK, paths, 1, dst, TIKU_CONFLICT_ASK);
}

/** @brief Start a deletion job. */
int
fop_fs_delete(tiku_fs_t *fs, const char *const *paths, int n)
{
    return start(fs, TIKU_OP_DELETE, paths, n, NULL,
                 TIKU_CONFLICT_ASK);
}
