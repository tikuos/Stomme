/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_folder_edit.h - inline editing in folder windows: renaming a
 * pose, and editing an attribute's value in place.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_FOLDER_EDIT_H_
#define TIKU_TRK_FOLDER_EDIT_H_

#include "tiku_attr.h"
#include "tiku_field.h"
#include "tiku_fs.h"
#include "tiku_trk_folder_window.h"
#include <stddef.h>

/** @brief State and dependencies for both kinds of inline folder editing. */
typedef struct tiku_trk_folder_edit {
    tiku_backend_t *backend;    /**< where values are written */
    tiku_fs_t *fs;              /**< where renames happen */
    char *status;               /**< the shell's status line */
    size_t status_size;
    int *dirty;                 /**< set when a redraw is owed */
    int editing;                /**< a value edit is open */
    tiku_field_t value_edit;
    tiku_trk_folder_window_t *value_folder; /**< owner of the value editor */
    char edit_path[TIKU_PATH_MAX];
    tiku_desc_t edit_desc;      /**< the attribute being edited */
    int renaming;               /**< a rename is open */
    tiku_field_t rename_edit;
    char rename_path[TIKU_PATH_MAX];
    tiku_trk_folder_window_t *rename_folder; /**< owner of the rename */
    int rename_row;             /**< the pose being renamed, or -1 */
} tiku_trk_folder_edit_t;

/**
 * @brief Bind the editor to its backend and filesystem, and to the shell's
 *        status line and dirty flag.
 */
void tiku_trk_folder_edit_init(tiku_trk_folder_edit_t *controller,
    tiku_backend_t *backend, tiku_fs_t *fs, char *status,
    size_t status_size, int *dirty);
/**
 * @brief Start editing the active pose's value in place; a value that
 *        cannot be written is refused in the status line.
 */
void tiku_trk_folder_edit_begin_value(tiku_trk_folder_edit_t *controller,
    tiku_trk_folder_window_t *folder);
/**
 * @brief End the value edit and write the text, canonicalised, to the
 *        node; a value that fails validation or the write is reported.
 */
void tiku_trk_folder_edit_commit_value(tiku_trk_folder_edit_t *controller,
    tiku_trk_folder_window_t *folder);
/** @brief End the value edit without writing. */
void tiku_trk_folder_edit_cancel_value(tiku_trk_folder_edit_t *controller);
/**
 * @brief Start renaming the pose at @p row in place; a device node or a
 *        namespace folder is refused, and a read-only volume's name is
 *        offered to copy only.
 */
void tiku_trk_folder_edit_begin_rename(tiku_trk_folder_edit_t *controller,
    tiku_trk_folder_window_t *folder, int row);
/**
 * @brief End the rename, applying the new name when @p commit; a name the
 *        filesystem rejects keeps the editor open with the text selected.
 */
void tiku_trk_folder_edit_finish_rename(tiku_trk_folder_edit_t *controller,
    int commit);
/**
 * @brief A key while an editor is open: text editing, Cmd+X, C and V,
 *        Tab and Shift-Tab to the next editable thing, and Cmd+N, which
 *        commits the edit first.
 * @return 1 when taken; 0 for Cmd+N and for a key aimed at a window the
 *         editor is not in, which the caller then handles.
 */
int tiku_trk_folder_edit_key(tiku_trk_folder_edit_t *controller,
    tiku_trk_folder_window_t *folder, unsigned key, unsigned modifiers,
    char character);

/**
 * @brief Insert committed text into the open editor of @p folder.
 * @return 0 when it has none open, 1 handled, -1 rejected.
 */
int tiku_trk_folder_edit_text(tiku_trk_folder_edit_t *controller,
    tiku_trk_folder_window_t *folder, const char *text);

#endif /* TIKU_TRK_FOLDER_EDIT_H_ */
