/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_app_rename.c - renaming in place, in the inline editor.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_trk_app_private.h"

/**
 * @brief Open the inline editor on the active pose's name.  A device node
 *        or namespace folder is refused with a note in the status line:
 *        the device namespace cannot be renamed.
 */
void
browser_begin_rename(app_t *a)
{
    tiku_trk_pose_t *p = tiku_trk_poseview_active(a->view);

    if (p == NULL || a->fs == NULL) {
        return;
    }
    /* A device node or namespace folder is refused here, before the
     * editor opens. */
    if (p->model.kind == TIKU_KIND_DEVICE_NODE ||
        p->model.kind == TIKU_KIND_VIRTUAL_DIR) {
        snprintf(a->status, sizeof a->status,
                 "'%s' is part of the device namespace and cannot be renamed",
                 p->model.name);
        a->dirty = 1;
        return;
    }
    a->editing = 1;
    tiku_text_input_reset(&a->commit);
    tiku_host_cancel_composition(a->host);
    a->edit_row = browser_pose_index(a, p);
    tiku_field_start(&a->ed, p->model.name);
    /* Characters the store refuses are kept out of the field. */
    tiku_field_deny(&a->ed, tiku_fs_name_deny());
    snprintf(a->edit_path, sizeof a->edit_path, "%s", p->model.path);
    a->status[0] = '\0';
    a->dirty = 1;
}

/** @brief Begin the rename the slow-second-click timer armed on @p row. */
void
browser_begin_rename_row(app_t *a, int row)
{
    tiku_trk_pose_t *p = tiku_trk_poseview_at(a->view, row);

    if (p == NULL) {
        return;
    }
    /* The engine armed it on a row and the editor works on the active
     * pose, so that row is made the active pose first. */
    tiku_trk_poseview_click(a->view, row, 0u);
    browser_begin_rename(a);
}

/** @brief Commit the inline edit with @p commit, else abandon it; a name
 *         the store refuses keeps the editor open, its text selected. */
void
browser_end_rename(app_t *a, int commit)
{
    char err[200];

    if (!a->editing) {
        return;
    }
    {   /* MA-050: an unchanged name is not sent to the store, which
         * refuses it as a name that already exists. */
        const char *slash = strrchr(a->edit_path, '/');
        const char *was = (slash != NULL) ? slash + 1 : a->edit_path;

        if (commit && strcmp(was, a->ed.text) == 0) {
            commit = 0;
        }
    }
    if (commit && a->ed.len > 0 &&
        tiku_fs_rename(a->fs, a->edit_path, a->ed.text, err,
                           sizeof err) != 0) {
        /* A rejected name keeps the editor open, with the reason in the
         * status line and the text selected whole for retyping (NeXT UIG
         * ch7). */
        snprintf(a->status, sizeof a->status, "%s", err);
        a->ed.active = 1;               /* Return had already closed it */
        a->ed.sel_a = 0;
        a->ed.sel_b = a->ed.len;
        a->ed.caret = a->ed.len;
        a->dirty = 1;
        return;
    }
    if (commit && a->ed.len > 0) {
        char renamed[TIKU_PATH_MAX];
        char parent[TIKU_PATH_MAX];
        char *slash;
        int row;

        a->status[0] = '\0';
        browser_after_op(a);
        /* The rename re-sorted the row; it is selected and scrolled into
         * view (NeXT UIG ch3). */
        snprintf(parent, sizeof parent, "%s", a->edit_path);
        slash = strrchr(parent, '/');
        if (slash != NULL) {
            *slash = '\0';
            snprintf(renamed, sizeof renamed, "%s/%s", parent, a->ed.text);
            row = browser_index_of_path(a, renamed);
            if (row >= 0) {
                tiku_trk_poseview_click(a->view, row, 0u);
                tiku_trk_poseview_scroll_into_view(a->view, row,
                                                   browser_visible_rows(a));
            }
        }
    }
    a->editing = 0;
    tiku_field_stop(&a->ed);
    a->dirty = 1;
}
