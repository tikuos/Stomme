/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_fs_tree.c - a tree copied, moved or removed.  A copy or a move into
 * a folder that is already there merges into it.  Contract rows FS-020 and
 * FS-029.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_fs_private.h"

/**
 * @brief Remove @p path and everything under it.
 *
 * Best effort: a child that will not go does not stop the rest.  A
 * symbolic link is removed itself; what it points at is not entered.
 *
 * @return 0 when the top-level item went.
 */
int
fop_remove_tree(const char *path)
{
    struct stat st;
    DIR *d;
    struct dirent *e;

    if (lstat(path, &st) != 0) {
        return -1;
    }
    if (!S_ISDIR(st.st_mode)) {
        return (unlink(path) == 0) ? 0 : -1;
    }
    d = opendir(path);
    while (d != NULL && (e = readdir(d)) != NULL) {
        char child[TIKU_PATH_MAX];

        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) {
            continue;
        }
        if (fop_join(child, sizeof child, path, e->d_name) == 0) {
            (void)fop_remove_tree(child);
        }
    }
    if (d != NULL) {
        (void)closedir(d);
    }
    return (rmdir(path) == 0) ? 0 : -1;
}

/**
 * @brief Copy @p src to @p dstpath, merging into a folder already there.
 *
 * Replacing a folder merges: the destination folder is kept, its children
 * with no counterpart in the source stay, same-named files are replaced and
 * same-named folders merge by the same rule.
 *
 * @param dstpath The full destination path, whose name a collision answer
 *                may have chosen.
 * @return 0, -1 with @p err filled, or -2 when cancelled.
 */
int
fop_copy_tree(tiku_fs_t *fs, const char *src, const char *dstpath, char *err,
          size_t errmax)
{
    struct stat st;
    dev_t sdev;
    DIR *d;
    struct dirent *e;
    {
        /* Between two stores the local filesystem calls below do not
         * apply; fop_copy_across() copies through the backends. */
        char sl[TIKU_PATH_MAX], dl[TIKU_PATH_MAX];
        tiku_backend_t *sb = fop_store_of(fs, src, sl, sizeof sl);
        tiku_backend_t *db = fop_store_of(fs, dstpath, dl, sizeof dl);

        if (sb != NULL && db != NULL && sb != db) {
            return fop_copy_across(fs, sb, sl, db, dl, src, err, errmax);
        }
    }

    /* lstat(), so a link is seen as a link (FS-032). */
    if (lstat(src, &st) != 0) {
        snprintf(err, errmax, "cannot read '%.60s'", fop_leaf_of(src));
        return -1;
    }
    if (S_ISLNK(st.st_mode)) {
        /* A link is copied as a link, not as what it points at
         * (FS-032). */
        char target[TIKU_PATH_MAX];
        ssize_t n = readlink(src, target, sizeof target - 1u);

        if (n < 0) {
            snprintf(err, errmax, "cannot read the link '%.50s'",
                     fop_leaf_of(src));
            return -1;
        }
        target[n] = '\0';
        (void)remove(dstpath);
        if (symlink(target, dstpath) != 0) {
            snprintf(err, errmax, "cannot create the link '%.50s' (%.40s)",
                     fop_leaf_of(dstpath), strerror(errno));
            return -1;
        }
        /* Owner and times, but no attributes. */
        fop_carry_over(src, dstpath, 1);
        return 0;
    }
    if (!S_ISDIR(st.st_mode)) {
        int rc = fop_copy_bytes(fs, src, dstpath, err, errmax);

        if (rc == 0) {
            fop_carry_over(src, dstpath, 0);
        }
        return rc;
    }
    /* The source's device, kept before st is reused for the destination:
     * the mount-point test below compares each child against it. */
    sdev = st.st_dev;
    if (stat(dstpath, &st) == 0) {
        if (!S_ISDIR(st.st_mode)) {
            snprintf(err, errmax,
                     "cannot replace the file '%.50s' with a folder",
                     fop_leaf_of(dstpath));
            return -1;
        }
        /* A folder is there: merge into it, its own attributes left as
         * they are. */
    } else if (mkdir(dstpath, 0755) != 0) {
        snprintf(err, errmax, "cannot create '%.60s' (%.40s)",
                 fop_leaf_of(dstpath), strerror(errno));
        return -1;
    } else {
        fop_carry_over(src, dstpath, 0);
    }
    d = opendir(src);
    if (d == NULL) {
        snprintf(err, errmax, "cannot read '%.60s'", fop_leaf_of(src));
        return -1;
    }
    while ((e = readdir(d)) != NULL) {
        char child[TIKU_PATH_MAX];

        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) {
            continue;
        }
        {
            char cdst[TIKU_PATH_MAX];

            if (fop_join(child, sizeof child, src, e->d_name) != 0 ||
                fop_join(cdst, sizeof cdst, dstpath, e->d_name) != 0) {
                continue;
            }
            /* A failure on one child does not stop the rest. */
            {   /* A directory on another filesystem is a mount point and
                 * is not copied, and an entry that is neither file,
                 * folder nor link, such as a device node, is left out
                 * (FS-036). */
                struct stat cs;

                if (lstat(child, &cs) == 0) {
                    if (S_ISDIR(cs.st_mode) && cs.st_dev != sdev) {
                        continue;
                    }
                    if (!S_ISDIR(cs.st_mode) && !S_ISREG(cs.st_mode) &&
                        !S_ISLNK(cs.st_mode)) {
                        continue;
                    }
                }
            }
            if (fop_copy_tree(fs, child, cdst, err, errmax) == -2) {
                /* Cancelled: the folders made and the files written stay,
                 * and only the file being written is removed (FS-034). */
                (void)closedir(d);
                return -2;
            }
        }
    }
    (void)closedir(d);
    return 0;
}

/**
 * @brief Move @p src to @p dstpath, merging into a folder already there
 *        (FS-029).
 *
 * On one filesystem the move is a rename; across filesystems, or from local
 * files to another store, it is a copy and then a delete.  A same-named
 * folder at the destination is merged into, and the emptied source folders
 * are removed.
 *
 * @return 0, -1 with @p err filled, or -2 when the copy of a move across
 *         filesystems or stores is cancelled.
 */
int
fop_move_tree(tiku_fs_t *fs, const char *src, const char *dstpath, char *err,
          size_t errmax)
{
    struct stat sst, dst_st;
    DIR *d;
    struct dirent *e;
    {
        /* Between two stores a move is a copy and then a delete, and the
         * delete needs the local filesystem: the backend ops remove
         * nothing.  A move off a device is refused. */
        char sl[TIKU_PATH_MAX], dl[TIKU_PATH_MAX];
        tiku_backend_t *sb = fop_store_of(fs, src, sl, sizeof sl);
        tiku_backend_t *db = fop_store_of(fs, dstpath, dl, sizeof dl);

        if (sb != NULL && db != NULL && sb != db) {
            int rc;

            if (sb->devid[0] != '\0') {
                snprintf(err, errmax,
                         "'%.40s' can be copied off that store but not "
                         "moved: nothing in the backend contract removes "
                         "anything", fop_leaf_of(src));
                return -1;
            }
            rc = fop_copy_across(fs, sb, sl, db, dl, src, err, errmax);
            if (rc != 0) {
                return rc;
            }
            return fop_remove_tree(sl);
        }
    }

    if (stat(src, &sst) != 0) {
        snprintf(err, errmax, "cannot read '%.60s'", fop_leaf_of(src));
        return -1;
    }
    if (stat(dstpath, &dst_st) != 0) {
        /* Nothing in the way: one rename moves the whole tree. */
        if (rename(src, dstpath) == 0) {
            return 0;
        }
        if (errno != EXDEV) {
            snprintf(err, errmax, "cannot move '%.60s' (%.40s)",
                     fop_leaf_of(src), strerror(errno));
            return -1;
        }
        /* Across a filesystem boundary a move is a copy and then a delete
         * (FS-030).  A cancelled copy returns -2 and the source stays. */
        {
            int rc = fop_copy_tree(fs, src, dstpath, err, errmax);

            if (rc != 0) {
                return rc;
            }
        }
        return fop_remove_tree(src);
    }
    if (!S_ISDIR(sst.st_mode) || !S_ISDIR(dst_st.st_mode)) {
        /* A file over a file: the refusal gate has already allowed this,
         * so the destination goes and the source takes its place. */
        (void)remove(dstpath);
        if (rename(src, dstpath) != 0) {
            snprintf(err, errmax, "cannot move '%.60s' (%.40s)",
                     fop_leaf_of(src), strerror(errno));
            return -1;
        }
        return 0;
    }
    d = opendir(src);
    if (d == NULL) {
        snprintf(err, errmax, "cannot read '%.60s'", fop_leaf_of(src));
        return -1;
    }
    while ((e = readdir(d)) != NULL) {
        char child[TIKU_PATH_MAX];

        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) {
            continue;
        }
        {
            char cdst[TIKU_PATH_MAX];

            if (fop_join(child, sizeof child, src, e->d_name) == 0 &&
                fop_join(cdst, sizeof cdst, dstpath, e->d_name) == 0) {
                (void)fop_move_tree(fs, child, cdst, err, errmax);
            }
        }
    }
    (void)closedir(d);
    /* The emptied source folder goes; one with something left in it
     * stays. */
    (void)rmdir(src);
    return 0;
}
