/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_fs_refusals.c - what a move or a copy refuses before it starts: the
 * Trash, a whole volume, a folder into itself, a folder replaced by one of
 * its sub-folders, and a file put over a folder or a folder over a file.
 * Contract rows FS-023, FS-024 and FS-025.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_fs_private.h"

/** @brief Whether @p dir is @p path or contains it, at a component
 *         boundary. */
static int
path_contains(const char *dir, const char *path)
{
    size_t n;

    if (dir == NULL || path == NULL) {
        return 0;
    }
    n = strlen(dir);
    while (n > 1u && dir[n - 1u] == '/') {
        n--;
    }
    if (strncmp(dir, path, n) != 0) {
        return 0;
    }
    /* The same path counts as contained; a longer one only when the next
     * character is a separator, or "/a/b" would swallow "/a/bc". */
    return path[n] == '\0' || path[n] == '/';
}

/** @brief Whether @p path is a directory. */
int
fop_is_dir(const char *path)
{
    struct stat st;

    return (stat(path, &st) == 0) && S_ISDIR(st.st_mode);
}

/** @brief The refusal, if any, for moving or copying @p src into
 *         @p dst_dir; @p msg receives the sentence to show. */
tiku_refusal_t
fop_fs_check_move(tiku_fs_t *fs, const char *src, const char *dst_dir,
                       char *msg, size_t max)
{
    char dstpath[TIKU_PATH_MAX];

    if (msg != NULL && max > 0u) {
        msg[0] = '\0';
    }
    if (fs == NULL || src == NULL || dst_dir == NULL) {
        return TIKU_REFUSE_NONE;
    }
    /* The Trash is not an item to be moved about; it is where items go. */
    if (strstr(src, "/" TIKU_TRASH_NAME) != NULL &&
        strcmp(fop_leaf_of(src), TIKU_TRASH_NAME) == 0) {
        if (msg != NULL) {
            snprintf(msg, max, "You can't move or copy the trash.");
        }
        return TIKU_REFUSE_TRASH;
    }
    /*
     * A whole volume is not an item: a mount point moved off its device
     * gets EXDEV, and the fallback would copy every byte of the volume and
     * then delete the source.
     */
    if (!fop_on_device(fs, src)) {
        tiku_volumes_t vs;

        if (tiku_volumes_scan(&vs) > 0 &&
            tiku_volume_is_root(&vs, src)) {
            if (msg != NULL) {
                snprintf(msg, max,
                         "\"%.40s\" is a disk, not an item: it can be "
                         "unmounted but not moved, copied or deleted.",
                         fop_leaf_of(src));
            }
            return TIKU_REFUSE_VOLUME;
        }
    }
    /* A folder into itself, or into anything underneath it. */
    if (fop_is_dir(src) && path_contains(src, dst_dir)) {
        if (msg != NULL) {
            snprintf(msg, max,
                     "You can't move a folder into itself or any of its own "
                     "sub-folders.");
        }
        return TIKU_REFUSE_INTO_SELF;
    }
    if (fop_join(dstpath, sizeof dstpath, dst_dir, fop_leaf_of(src)) != 0) {
        return TIKU_REFUSE_NONE;
    }
    {
        struct stat dst_st;

        if (stat(dstpath, &dst_st) != 0) {
            return TIKU_REFUSE_NONE;    /* no collision, no question  */
        }
        /* The second refusal is the first one inverted: here the thing
         * being overwritten is an ancestor of the thing overwriting it. */
        if (S_ISDIR(dst_st.st_mode) && path_contains(dstpath, src)) {
            if (msg != NULL) {
                snprintf(msg, max,
                         "You can't replace a folder with one of its "
                         "sub-folders.");
            }
            return TIKU_REFUSE_BY_CHILD;
        }
        /* A kind mismatch is refused outright. */
        if (S_ISDIR(dst_st.st_mode) != (fop_is_dir(src) ? 1 : 0)) {
            if (msg != NULL) {
                snprintf(msg, max, fop_is_dir(src)
                         ? "You cannot replace a file with a folder or a "
                           "symbolic link."
                         : "You cannot replace a folder or a symbolic link "
                           "with a file.");
            }
            return TIKU_REFUSE_KIND;
        }
    }
    return TIKU_REFUSE_NONE;
}
