/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_fs_drop.c - what a drop does: which store serves a path, whether a
 * device does, and which volume the path is on.  A drop within one volume
 * moves; a drop across volumes, or one that asks for a copy, copies.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_fs_private.h"

/**
 * @brief Which backend serves @p path, and the path it knows it by.
 *
 * The local filesystem calls of the tiku_fs files are made only for a path
 * a local store serves.
 */
tiku_backend_t *
fop_store_of(const tiku_fs_t *fs, const char *path, char *local, size_t max)
{
    return tiku_backend_serving(fs->backend, path, local, max);
}

/**
 * @brief Whether a device serves @p path.
 *
 * Asked per path: a namespace carries its root's identity, which with local
 * files at the root says nothing about a path under /devices/<board>.
 */
int
fop_on_device(const tiku_fs_t *fs, const char *path)
{
    tiku_backend_t *on = fop_store_of(fs, path, NULL, 0);

    return on != NULL && on->devid[0] != '\0';
}

/**
 * @brief The volume @p path is on, as st_dev (its parent's when @p path
 *        does not exist); 0 when neither can be stat'ed.
 */
static unsigned long
volume_of(const char *path)
{
    struct stat st;

    if (stat(path, &st) != 0) {
        char dir[TIKU_PATH_MAX];
        fop_parent_of(path, dir, sizeof dir);
        if (stat(dir, &st) != 0) {
            return 0;
        }
    }
    return (unsigned long)st.st_dev;
}

/** @brief TIKU_OP_MOVE when @p src and @p dst_dir are on one volume and no
 *         copy was asked for, else TIKU_OP_COPY. */
tiku_op_t
fop_fs_drop_action(tiku_fs_t *fs, const char *src,
                        const char *dst_dir, int force_copy)
{
    unsigned long a, b;

    if (force_copy) {
        return TIKU_OP_COPY;
    }
    if (fop_on_device(fs, src) || fop_on_device(fs, dst_dir)) {
        /* With a device on either side, a drop moves only within the
         * store that serves both paths. */
        return (fop_store_of(fs, src, NULL, 0) ==
                fop_store_of(fs, dst_dir, NULL, 0)) ? TIKU_OP_MOVE
                                                    : TIKU_OP_COPY;
    }
    a = volume_of(src);
    b = volume_of(dst_dir);
    /* Same volume: a move is a rename and costs nothing.  Different volume:
     * the bytes have to travel, so it is a copy and the original stays. */
    return (a != 0 && a == b) ? TIKU_OP_MOVE : TIKU_OP_COPY;
}
