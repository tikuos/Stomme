/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_fs_create.c - new items: a new folder, or a copy of a template,
 * under the first free name and recorded for undo.  Contract row FS-079.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_fs_private.h"

/** @brief The first free "New folder" name in @p dir; see
 *         tiku_fs_new_folder_name(). */
void
fop_fs_new_folder_name(tiku_fs_t *fs, const char *dir, char *out,
                            size_t max)
{
    long n;

    if (out == NULL || max == 0) {
        return;
    }
    snprintf(out, max, "New folder");
    if (fs == NULL || dir == NULL || !fop_exists(fs, dir, out)) {
        return;
    }
    for (n = 2; n < 10000; n++) {
        /* Past nine there is no space: "New folder 9" is followed by
         * "New folder10", the names Haiku Tracker gives. */
        if (n <= 9) {
            snprintf(out, max, "New folder %ld", n);
        } else {
            snprintf(out, max, "New folder%ld", n);
        }
        if (!fop_exists(fs, dir, out)) {
            return;
        }
    }
}

/** @brief Make a new folder in @p dir through the backend that serves it,
 *         recorded for undo. */
int
fop_fs_new_folder(tiku_fs_t *fs, const char *dir, char *out,
                       size_t max, char *err, size_t errmax)
{
    char name[TIKU_PATH_MAX], path[TIKU_PATH_MAX], why[160];

    if (fs == NULL || dir == NULL) {
        return -1;
    }
    tiku_fs_new_folder_name(fs, dir, name, sizeof name);
    why[0] = '\0';
    if (fop_join(path, sizeof path, dir, name) != 0 ||
        tiku_backend_mkdir(fs->backend, path, why, sizeof why) != 0) {
        if (err != NULL) {
            snprintf(err, errmax, "Sorry, could not create a new folder%s%s.",
                     why[0] != '\0' ? ": " : "", why);
        }
        return -1;
    }
    fop_undo_reset(fs, TIKU_OP_NEW_FOLDER);
    fop_undo_push(fs, path, NULL, 1);
    if (out != NULL) {
        snprintf(out, max, "%s", path);
    }
    return 0;
}

/** @brief Copy a template into @p dst_dir under a fresh "New ..." name; see
 *         tiku_fs_new_from_template(). */
int
fop_fs_new_from_template(tiku_fs_t *fs, const char *template_path,
                              const char *dst_dir, char *out, size_t max,
                              char *err, size_t errmax)
{
    struct stat st;
    char name[TIKU_NAME_MAX];
    char candidate[TIKU_NAME_MAX];
    char dst[TIKU_PATH_MAX];
    const char *base;
    int n;

    if (err != NULL && errmax > 0) {
        err[0] = '\0';
    }
    if (fs == NULL || template_path == NULL || dst_dir == NULL ||
        out == NULL || max == 0) {
        if (err != NULL) {
            snprintf(err, errmax, "template destination is unavailable");
        }
        return -1;
    }
    /* Templates are local files; a device destination is refused. */
    if (fs->backend == NULL || fop_on_device(fs, dst_dir) ||
        lstat(template_path, &st) != 0 ||
        (!S_ISREG(st.st_mode) && !S_ISDIR(st.st_mode) &&
         !S_ISLNK(st.st_mode))) {
        if (err != NULL) {
            snprintf(err, errmax, "could not read template");
        }
        return -1;
    }
    base = fop_leaf_of(template_path);
    if (base[0] == '\0' || strlen(base) >= sizeof name - 5u) {
        if (err != NULL) {
            snprintf(err, errmax, "template name is too long");
        }
        return -1;
    }
    snprintf(name, sizeof name, "New %s", base);
    snprintf(candidate, sizeof candidate, "%s", name);
    if (fop_exists(fs, dst_dir, candidate)) {
        for (n = 2; n < 10000; n++) {
            snprintf(candidate, sizeof candidate, "New %s %d", base, n);
            if (!fop_exists(fs, dst_dir, candidate)) {
                break;
            }
        }
        if (n >= 10000) {
            if (err != NULL) {
                snprintf(err, errmax, "could not find a free template name");
            }
            return -1;
        }
    }
    if (fop_join(dst, sizeof dst, dst_dir, candidate) != 0) {
        if (err != NULL) {
            snprintf(err, errmax, "template destination path is too long");
        }
        return -1;
    }
    if (fop_copy_tree(fs, template_path, dst, err, errmax) != 0) {
        return -1;
    }
    fop_copy_pose_info(fs, template_path, dst);
    fop_undo_reset(fs, TIKU_OP_COPY);
    fop_undo_push(fs, dst, NULL, 1);
    snprintf(out, max, "%s", dst);
    return 0;
}
