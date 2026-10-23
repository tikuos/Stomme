/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_desktop_drop.c - handling a drop: on an application pose, on the
 * file panel, on an Info window's icon, or anywhere else through the drop
 * controller.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_trk_desktop_private.h"

/** @brief A drop on an application pose launches it with the dropped paths
 *         (PVS-044). */
int
drop_launch(void *context, const struct tiku_trk_app *application,
            const char *const *paths, int count)
{
    desktop_t *app = context;

    return launch_paths(app, (const tiku_trk_app_t *)application, paths,
                        count);
}

/**
 * @brief Handle a drop anywhere on the file panel (AW-059).
 *
 * One dropped item navigates the panel to it; any other count is ignored.
 *
 * @return nonzero when the drop was on the panel, handled or ignored.
 */
int
drop_on_panel(desktop_t *app, int x, int y)
{
    tiku_window_t *w = NULL;

    (void)tiku_workspace_hit(app->desk, x, y, &w);
    if (w == NULL || w != tiku_trk_panel_window_handle(&app->file_panel)) {
        return 0;
    }
    if (app->drag.count == 1) {
        tiku_trk_panel_window_drop(&app->file_panel, app->drag.path[0]);
    }
    desktop_damage_all(app);
    return 1;
}

/**
 * @brief The Info window icon under the pointer that would take a drop.
 *
 * The icon stands for the window's own item, so it is a drop target
 * exactly when that item is a container the store can write (AW-075).
 * @return the slot, or -1.
 */
int
info_icon_drop_slot(desktop_t *app, int x, int y)
{
    tiku_window_t *w = NULL;
    tiku_trk_info_t *info;
    tiku_model_t m;
    int slot;

    (void)tiku_workspace_hit(app->desk, x, y, &w);
    slot = tiku_trk_info_windows_slot(&app->info_windows, w);
    if (slot < 0 ||
        tiku_trk_info_windows_hit(&app->info_windows, slot, x, y, NULL) !=
            TIKU_TRK_INFO_HIT_ICON) {
        return -1;
    }
    info = tiku_trk_info_windows_model(&app->info_windows, slot);
    if (app->backend->ops->stat(app->backend, info->path, &m) != 0 ||
        !tiku_model_is_mutable_container(&m)) {
        return -1;
    }
    return slot;
}

/**
 * @brief Move, copy or link the dragged items into the folder an Info
 *        window's icon stands for.  Option asks for a copy, Cmd for a link
 *        (relative with Shift); otherwise the drag's own rule decides.
 * @return 1 when the pointer was over such an icon, 0 otherwise.
 */
int
drop_on_info_icon(desktop_t *app, int x, int y, unsigned modifiers)
{
    const char *paths[TIKU_TRK_DRAG_MAX];
    tiku_trk_info_t *info;
    unsigned drag_modifiers = 0u;
    tiku_trk_drop_t action;
    int slot = info_icon_drop_slot(app, x, y);
    int same_store, i, count = 0;

    if (slot < 0 || app->drag.count == 0) {
        return 0;
    }
    info = tiku_trk_info_windows_model(&app->info_windows, slot);
    if ((modifiers & TIKU_MOD_OPTION) != 0u) {
        drag_modifiers |= TIKU_TRK_DRAG_MOD_COPY;
    }
    if ((modifiers & TIKU_MOD_CMD) != 0u) {
        drag_modifiers |= TIKU_TRK_DRAG_MOD_LINK;
    }
    same_store = tiku_fs_drop_action(app->fs, app->drag.path[0],
                                         info->path, 0) == TIKU_OP_MOVE;
    action = tiku_trk_drag_action(&app->drag, info->path, drag_modifiers,
                                  same_store);
    if (action == TIKU_TRK_DROP_NOTHING) {
        return 1;               /* over the icon, but nothing to do */
    }
    for (i = 0; i < app->drag.count; i++) {
        paths[count++] = app->drag.path[i];
    }
    snprintf(app->prog_dest, sizeof app->prog_dest, "%s", info->path);
    switch (action) {
    case TIKU_TRK_DROP_COPY:
        (void)tiku_fs_copy(app->fs, paths, count, info->path,
                               TIKU_CONFLICT_ASK);
        break;
    case TIKU_TRK_DROP_LINK:
        (void)tiku_fs_link(app->fs, paths, count, info->path,
                               (modifiers & TIKU_MOD_SHIFT) != 0u);
        break;
    default:
        (void)tiku_fs_move(app->fs, paths, count, info->path,
                               TIKU_CONFLICT_ASK);
        break;
    }
    snprintf(app->status, sizeof app->status,
             "dropped %d item%s on %s", count, count == 1 ? "" : "s",
             info->model.name);
    return 1;
}

/**
 * @brief Perform the drop under the pointer, through the drop controller.
 *
 * The destination is the highlighted pose when there is one, else the
 * folder of the window under the pointer.  A dragged pose is never a
 * target, so a drop on one goes into that window's folder (PVS-042).
 */
void
do_drop(desktop_t *app, int x, int y, unsigned state, unsigned button)
{
    tiku_trk_drop_controller_execute(&app->drop_controller, x, y, state,
                                     button);
}
