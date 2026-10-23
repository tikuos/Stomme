/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_folder_actions.c - the commands a folder view carries out
 * itself, dispatched by command.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_trk_folder_actions.h"
#include "tiku_trk_shortcuts.h"

/** @brief Sort by @p field and lay the icons out again to the width. */
static void
sort_and_layout(tiku_trk_folder_window_t *folder, int field)
{
    tiku_trk_poseview_sort_by(folder->view, field);
    tiku_trk_poseview_cleanup(folder->view,
                              tiku_trk_folder_window_content(folder).w);
}

/** @brief Icon mode, at @p size. */
static void
set_icon_size(tiku_trk_folder_window_t *folder, int size)
{
    tiku_trk_poseview_set_mode(folder->view, TIKU_TRK_MODE_ICON);
    tiku_trk_poseview_set_icon_size(folder->view, size);
}

int
tiku_trk_folder_actions_invoke(tiku_workspace_t *workspace,
    tiku_trk_folder_window_t *folder, int command)
{
    if (folder == NULL || folder->view == NULL) {
        return 0;
    }
    switch (command) {
    case TIKU_TRK_CMD_SELECT_ALL:
        tiku_trk_poseview_select_all(folder->view);
        return 1;
    case TIKU_TRK_CMD_LIST_VIEW:
        tiku_trk_poseview_set_mode(folder->view, TIKU_TRK_MODE_LIST);
        return 1;
    case TIKU_TRK_CMD_ICON_VIEW:
        tiku_trk_poseview_set_mode(folder->view, TIKU_TRK_MODE_ICON);
        return 1;
    case TIKU_TRK_CMD_MINI_ICON_VIEW:
        tiku_trk_poseview_set_mode(folder->view, TIKU_TRK_MODE_MINI);
        return 1;
    case TIKU_TRK_CMD_ICON_SIZE_32:
        set_icon_size(folder, 32);
        return 1;
    case TIKU_TRK_CMD_ICON_SIZE_40:
        set_icon_size(folder, 40);
        return 1;
    case TIKU_TRK_CMD_ICON_SIZE_48:
        set_icon_size(folder, 48);
        return 1;
    case TIKU_TRK_CMD_ICON_SIZE_64:
        set_icon_size(folder, 64);
        return 1;
    case TIKU_TRK_CMD_ICON_SIZE_96:
        set_icon_size(folder, 96);
        return 1;
    case TIKU_TRK_CMD_ICON_SIZE_128:
        set_icon_size(folder, 128);
        return 1;
    case TIKU_TRK_CMD_SORT_BY_NAME:
        sort_and_layout(folder, TIKU_TRK_COL_NAME);
        return 1;
    case TIKU_TRK_CMD_SORT_BY_KIND:
        sort_and_layout(folder, TIKU_TRK_COL_KIND);
        return 1;
    case TIKU_TRK_CMD_SORT_BY_SIZE:
        sort_and_layout(folder, TIKU_TRK_COL_SIZE);
        return 1;
    case TIKU_TRK_CMD_SORT_BY_VALUE:
        sort_and_layout(folder, TIKU_TRK_COL_VALUE);
        return 1;
    case TIKU_TRK_CMD_SORT_BY_CAP:
        sort_and_layout(folder, TIKU_TRK_COL_CAP);
        return 1;
    case TIKU_TRK_CMD_COL_KIND:
        (void)tiku_trk_poseview_toggle_column(folder->view,
                                               TIKU_TRK_COL_KIND);
        return 1;
    case TIKU_TRK_CMD_COL_VALUE:
        (void)tiku_trk_poseview_toggle_column(folder->view,
                                               TIKU_TRK_COL_VALUE);
        return 1;
    case TIKU_TRK_CMD_COL_SIZE:
        (void)tiku_trk_poseview_toggle_column(folder->view,
                                               TIKU_TRK_COL_SIZE);
        return 1;
    case TIKU_TRK_CMD_COL_DATE:
        (void)tiku_trk_poseview_toggle_column(folder->view,
                                               TIKU_TRK_COL_DATE);
        return 1;
    case TIKU_TRK_CMD_COL_PATH:
        (void)tiku_trk_poseview_toggle_column(folder->view,
                                               TIKU_TRK_COL_PATH);
        return 1;
    case TIKU_TRK_CMD_COL_PERM:
        (void)tiku_trk_poseview_toggle_column(folder->view,
                                               TIKU_TRK_COL_PERM);
        return 1;
    case TIKU_TRK_CMD_COL_ORIGINAL:
        (void)tiku_trk_poseview_toggle_column(folder->view,
                                               TIKU_TRK_COL_ORIGINAL);
        return 1;
    case TIKU_TRK_CMD_COL_CAP:
        (void)tiku_trk_poseview_toggle_column(folder->view,
                                               TIKU_TRK_COL_CAP);
        return 1;
    case TIKU_TRK_CMD_REVERSE_ORDER:
        sort_and_layout(folder,
                        tiku_trk_poseview_sort_field(folder->view));
        return 1;
    case TIKU_TRK_CMD_CLEANUP:
        (void)tiku_trk_poseview_snap_to_grid(folder->view);
        return 1;
    case TIKU_TRK_CMD_CLEANUP_ALL:
        tiku_trk_poseview_cleanup(folder->view,
                                  tiku_trk_folder_window_content(folder).w);
        return 1;
    case TIKU_TRK_CMD_RESIZE_TO_FIT:
        if (workspace != NULL && folder->win != NULL) {
            (void)tiku_workspace_zoom(workspace, folder->win,
                tiku_trk_folder_window_fit_width(folder),
                tiku_trk_folder_window_fit_height(folder));
            /* Then back to the content's top-left, so the fitted window
             * shows all of it (PVL-059). */
            tiku_trk_poseview_scroll_to(folder->view, 0);
            tiku_trk_poseview_set_origin(folder->view, 0, 0);
        }
        return 1;
    default:
        return 0;
    }
}
