/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_folder_edit.c - inline editing in folder windows: renaming a
 * pose, and editing an attribute's value in place.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_trk_folder_edit.h"
#include <stdio.h>
#include <string.h>

void
tiku_trk_folder_edit_init(tiku_trk_folder_edit_t *controller,
    tiku_backend_t *backend, tiku_fs_t *fs, char *status,
    size_t status_size, int *dirty)
{
    *controller = (tiku_trk_folder_edit_t){
        .backend = backend,
        .fs = fs,
        .status = status,
        .status_size = status_size,
        .dirty = dirty,
        .rename_row = -1
    };
}

void
tiku_trk_folder_edit_begin_value(tiku_trk_folder_edit_t *controller,
                                 tiku_trk_folder_window_t *folder)
{
    tiku_trk_pose_t *pose;

    if (controller == NULL || folder == NULL) {
        return;
    }
    pose = tiku_trk_poseview_active(folder->view);
    if (pose == NULL) {
        return;
    }
    if (!tiku_attr_editable(&pose->model, &pose->desc)) {
        snprintf(controller->status, controller->status_size,
                 "%.40s cannot be written", pose->model.name);
        *controller->dirty = 1;
        return;
    }
    controller->editing = 1;
    controller->value_folder = folder;
    controller->edit_desc = pose->desc;
    snprintf(controller->edit_path, sizeof controller->edit_path, "%s",
             pose->model.path);
    tiku_field_start(&controller->value_edit,
                        pose->value_valid ? pose->value : "");
    controller->status[0] = '\0';
    *controller->dirty = 1;
}

void
tiku_trk_folder_edit_commit_value(tiku_trk_folder_edit_t *controller,
                                  tiku_trk_folder_window_t *folder)
{
    char error[128];
    char canonical[64];
    const char *slash;

    if (controller == NULL || !controller->editing) {
        return;
    }
    controller->editing = 0;
    tiku_field_stop(&controller->value_edit);
    if (!tiku_attr_validate(&controller->edit_desc,
                                controller->value_edit.text, error,
                                sizeof error)) {
        snprintf(controller->status, controller->status_size, "%s", error);
        *controller->dirty = 1;
        return;
    }
    (void)tiku_attr_canonical(&controller->edit_desc,
        controller->value_edit.text, canonical, sizeof canonical);
    if (controller->backend->ops->write(controller->backend,
            controller->edit_path, canonical, strlen(canonical), error,
            sizeof error) != 0) {
        snprintf(controller->status, controller->status_size, "%s", error);
    } else {
        slash = strrchr(controller->edit_path, '/');
        snprintf(controller->status, controller->status_size,
                 "%.100s = %.64s",
                 slash != NULL ? slash + 1 : controller->edit_path,
                 canonical);
        if (folder != NULL) {
            (void)tiku_trk_poseview_refresh_values(folder->view, 0, 64);
        }
    }
    *controller->dirty = 1;
}

void
tiku_trk_folder_edit_cancel_value(tiku_trk_folder_edit_t *controller)
{
    if (controller == NULL) {
        return;
    }
    controller->editing = 0;
    tiku_field_stop(&controller->value_edit);
    controller->status[0] = '\0';
    *controller->dirty = 1;
}

void
tiku_trk_folder_edit_begin_rename(tiku_trk_folder_edit_t *controller,
                                  tiku_trk_folder_window_t *folder, int row)
{
    tiku_trk_pose_t *pose;

    if (controller == NULL || folder == NULL || controller->fs == NULL) {
        return;
    }
    pose = tiku_trk_poseview_at(folder->view, row);
    if (pose == NULL) {
        return;
    }
    if (pose->model.kind == TIKU_KIND_DEVICE_NODE ||
        pose->model.kind == TIKU_KIND_VIRTUAL_DIR) {
        snprintf(controller->status, controller->status_size,
                 "'%.40s' is part of the device namespace and cannot be "
                 "renamed", pose->model.name);
        *controller->dirty = 1;
        return;
    }
    controller->renaming = 1;
    controller->rename_folder = folder;
    controller->rename_row = row;
    tiku_field_start(&controller->rename_edit, pose->model.name);
    tiku_field_deny(&controller->rename_edit, tiku_fs_name_deny());
    /* On a read-only volume the name opens as selectable text that cannot
     * be edited, for copying (MA-048). */
    tiku_field_set_editable(&controller->rename_edit,
        (pose->model.facts.vol_flags & TIKU_VOL_READ_ONLY) == 0u);
    snprintf(controller->rename_path, sizeof controller->rename_path, "%s",
             pose->model.path);
    controller->status[0] = '\0';
    *controller->dirty = 1;
}

void
tiku_trk_folder_edit_finish_rename(tiku_trk_folder_edit_t *controller,
                                   int commit)
{
    char error[160];
    const char *slash;
    const char *old_name;

    if (controller == NULL || !controller->renaming) {
        return;
    }
    slash = strrchr(controller->rename_path, '/');
    old_name = slash != NULL ? slash + 1 : controller->rename_path;
    if (commit && strcmp(old_name, controller->rename_edit.text) == 0) {
        commit = 0;
    }
    if (commit && controller->rename_edit.len > 0 &&
        tiku_fs_rename(controller->fs, controller->rename_path,
            controller->rename_edit.text, error, sizeof error) != 0) {
        /* A rejected name keeps the editor open with its whole text
         * selected (NeXT UIG ch7). */
        snprintf(controller->status, controller->status_size, "%s", error);
        controller->rename_edit.active = 1; /* Return deactivated it     */
        controller->rename_edit.sel_a = 0;
        controller->rename_edit.sel_b = controller->rename_edit.len;
        controller->rename_edit.caret = controller->rename_edit.len;
        *controller->dirty = 1;
        return;
    }
    if (commit && controller->rename_edit.len > 0) {
        controller->status[0] = '\0';
    }
    controller->renaming = 0;
    tiku_field_stop(&controller->rename_edit);
    *controller->dirty = 1;
}

/** @brief The field action a key means; anything else is a character. */
static tiku_field_key_t
edit_key(unsigned key)
{
    switch (key) {
    case TIKU_KEY_RETURN:    return TIKU_FIELD_COMMIT;
    case TIKU_KEY_ESCAPE:    return TIKU_FIELD_CANCEL;
    case TIKU_KEY_BACKSPACE: return TIKU_FIELD_BACKSPACE;
    case TIKU_KEY_DELETE:    return TIKU_FIELD_DELETE;
    case TIKU_KEY_LEFT:      return TIKU_FIELD_LEFT;
    case TIKU_KEY_RIGHT:     return TIKU_FIELD_RIGHT;
    case TIKU_KEY_HOME:      return TIKU_FIELD_HOME;
    case TIKU_KEY_END:       return TIKU_FIELD_END;
    default:                      return TIKU_FIELD_CHAR;
    }
}

/**
 * @brief The next row after @p row, or before it when @p backwards,
 *        wrapping, whose name can be edited: device nodes and namespace
 *        folders are skipped.
 * @return the row, or -1 for none.
 */
static int
next_rename_row(tiku_trk_folder_window_t *folder, int row, int backwards)
{
    int n, step, i;

    if (folder == NULL || folder->view == NULL) {
        return -1;
    }
    n = tiku_trk_poseview_count(folder->view);
    step = backwards ? -1 : 1;
    for (i = 1; i <= n; i++) {
        int at = (row + step * i) % n;
        tiku_trk_pose_t *pose;

        if (at < 0) {
            at += n;
        }
        pose = tiku_trk_poseview_at(folder->view, at);
        if (pose != NULL && pose->model.kind != TIKU_KIND_DEVICE_NODE &&
            pose->model.kind != TIKU_KIND_VIRTUAL_DIR) {
            return at;
        }
    }
    return -1;
}

int
tiku_trk_folder_edit_key(tiku_trk_folder_edit_t *controller,
    tiku_trk_folder_window_t *folder, unsigned key, unsigned modifiers,
    char character)
{
    tiku_field_t *editor;
    tiku_field_key_t action;
    tiku_field_result_t result;
    int renaming;

    if (controller == NULL ||
        (!controller->renaming && !controller->editing)) {
        return 0;
    }
    /*
     * Only the window the editor is in takes its keys: the caller routes
     * keys by the focused window, and focus can move while an editor is
     * open (a click on another window's tab activates it without
     * committing).  The desktop backdrop's folder has no window and keeps
     * its editor.
     */
    if (controller->renaming && controller->rename_folder != NULL &&
        controller->rename_folder->win != NULL &&
        folder != controller->rename_folder) {
        return 0;               /* not this window's editor */
    }
    if (!controller->renaming && controller->editing &&
        controller->value_folder && controller->value_folder->win &&
        folder != controller->value_folder) return 0;
    if ((modifiers & TIKU_MOD_CMD) &&
        (key == 'x' || key == 'c' || key == 'v')) {
        tiku_field_key_t text_key = (key == 'x') ? TIKU_FIELD_CUT
                                  : (key == 'c') ? TIKU_FIELD_COPY
                                                 : TIKU_FIELD_PASTE;
        editor = controller->renaming ? &controller->rename_edit
                                      : &controller->value_edit;
        if (tiku_field_key(editor, text_key, 0, '\0') ==
            TIKU_FIELD_REJECTED && controller->status != NULL) {
            snprintf(controller->status, controller->status_size,
                     "The clipboard does not fit this name.");
        }
        *controller->dirty = 1;
        return 1;
    }
    if ((modifiers & TIKU_MOD_CMD) && key == 'n') {
        /* Cmd+N commits the open name or value, then returns 0 so the
         * shell's dispatcher runs the command. */
        if (controller->renaming) {
            tiku_trk_folder_edit_finish_rename(controller, 1);
        } else {
            tiku_trk_folder_edit_commit_value(controller, folder);
        }
        return 0;
    }
    if (controller->renaming && key == TIKU_KEY_TAB) {
        int backwards = (modifiers & TIKU_MOD_SHIFT) != 0u;
        int row = controller->rename_row;

        tiku_trk_folder_edit_finish_rename(controller, 1);
        if (!backwards && folder != NULL &&
            tiku_trk_poseview_has_column(folder->view,
                                         TIKU_TRK_COL_VALUE)) {
            /* Tab commits and moves to the next editable column of the
             * same pose (MA-047): the Value column, when the entry's value
             * can be written. */
            tiku_trk_pose_t *pose;

            tiku_trk_poseview_click(folder->view, row, 0u);
            pose = tiku_trk_poseview_active(folder->view);
            if (pose != NULL &&
                tiku_attr_editable(&pose->model, &pose->desc)) {
                tiku_trk_folder_edit_begin_value(controller, folder);
                *controller->dirty = 1;
                return 1;
            }
        }
        /* No editable column that way: the walk goes on to the next
         * row's name, or with Shift the previous row's. */
        row = next_rename_row(folder, row, backwards);
        if (row >= 0) {
            tiku_trk_poseview_click(folder->view, row, 0u);
            tiku_trk_folder_edit_begin_rename(controller, folder, row);
        }
        *controller->dirty = 1;
        return 1;
    }
    if (controller->editing && !controller->renaming &&
        key == TIKU_KEY_TAB && folder != NULL) {
        int backwards = (modifiers & TIKU_MOD_SHIFT) != 0u;
        tiku_trk_pose_t *pose = tiku_trk_poseview_active(folder->view);
        int row = (pose != NULL) ? pose->index : -1;

        tiku_trk_folder_edit_commit_value(controller, folder);
        if (backwards && row >= 0) {
            /* Shift-Tab goes back to the name column of the same pose. */
            tiku_trk_folder_edit_begin_rename(controller, folder, row);
        } else {
            int next = next_rename_row(folder, row, 0);

            if (next >= 0) {
                tiku_trk_poseview_click(folder->view, next, 0u);
                tiku_trk_folder_edit_begin_rename(controller, folder,
                                                  next);
            }
        }
        *controller->dirty = 1;
        return 1;
    }
    renaming = controller->renaming;
    editor = renaming ? &controller->rename_edit : &controller->value_edit;
    action = edit_key(key);
    if (action == TIKU_FIELD_CHAR && character == '\0') {
        return 1;
    }
    result = tiku_field_key(editor, action,
        (modifiers & TIKU_MOD_SHIFT) != 0u, character);
    if (result == TIKU_FIELD_COMMITTED) {
        if (renaming) {
            tiku_trk_folder_edit_finish_rename(controller, 1);
        } else {
            tiku_trk_folder_edit_commit_value(controller, folder);
        }
    } else if (result == TIKU_FIELD_CANCELLED) {
        if (renaming) {
            tiku_trk_folder_edit_finish_rename(controller, 0);
        } else {
            tiku_trk_folder_edit_cancel_value(controller);
        }
    }
    *controller->dirty = 1;
    return 1;
}

int
tiku_trk_folder_edit_text(tiku_trk_folder_edit_t *controller,
    tiku_trk_folder_window_t *folder, const char *text)
{
    if (!controller || (!controller->renaming && !controller->editing))
        return 0;
    tiku_trk_folder_window_t *owner = controller->renaming
        ? controller->rename_folder : controller->value_folder;
    if (!owner || (folder != owner && (owner->win || folder))) return 0;
    tiku_field_t *field = controller->renaming
        ? &controller->rename_edit : &controller->value_edit;
    int result = tiku_field_commit(field, text);
    *controller->dirty = 1;
    return result == TIKU_FIELD_REJECTED ? -1 : 1;
}
