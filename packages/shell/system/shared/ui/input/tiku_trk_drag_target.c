/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_drag_target.c - the target under a drag: hit-testing the folder
 * and row, auto-scroll at a window's edge, and the spring hold.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_trk_drag_target.h"

/* Edge auto-scroll: the band that scrolls, in pixels; the depth past
 * which it steps by 3; the hold before it starts.  COUNT_HEIGHT is the
 * item-count strip at the bottom of a window.  SPRING_DELAY_US is not
 * read: the spring delay is TIKU_TRK_SPRING_DELAY_US. */
#define AUTOSCROLL_BAND 6
#define AUTOSCROLL_FAST 30
#define AUTOSCROLL_DELAY_US 600000
#define COUNT_HEIGHT 16
#define SPRING_DELAY_US 700000

void
tiku_trk_drag_target_init(tiku_trk_drag_target_t *controller,
    tiku_workspace_t *workspace, tiku_trk_deskbar_t *deskbar,
    tiku_trk_desktop_view_t *desktop_view, tiku_trk_drag_t *drag,
    tiku_trk_folder_window_t *folders, int *folder_count, int row_h,
    int *dirty, tiku_trk_drag_open_fn open, void *open_context)
{
    *controller = (tiku_trk_drag_target_t){
        .workspace = workspace,
        .deskbar = deskbar,
        .desktop_view = desktop_view,
        .drag = drag,
        .folders = folders,
        .folder_count = folder_count,
        .row_h = row_h,
        .dirty = dirty,
        .open = open,
        .open_context = open_context,
        .row = -1
    };
    tiku_trk_springmenu_reset(&controller->spring);
}

/** @brief The folder window shown in @p window, or NULL. */
static tiku_trk_folder_window_t *
folder_for_window(tiku_trk_drag_target_t *controller,
                  tiku_window_t *window)
{
    int i;

    for (i = 0; window != NULL && i < *controller->folder_count; i++) {
        if (controller->folders[i].win == window) {
            return &controller->folders[i];
        }
    }
    return NULL;
}

int
tiku_trk_drag_target_motion(tiku_trk_drag_target_t *controller,
                            int x, int y, int64_t now)
{
    tiku_window_t *window = NULL;
    tiku_trk_folder_window_t *folder;
    int row = -1;
    int team = -1;
    int changed = 0;
    int inside_spring = tiku_trk_springmenu_inside(&controller->spring, x, y);

    (void)tiku_workspace_hit(controller->workspace, x, y, &window);
    folder = window != NULL ? folder_for_window(controller, window)
        : tiku_trk_desktop_view_folder(controller->desktop_view);
    if (folder != NULL && folder->view == NULL) {
        folder = NULL;
    }
    if (folder != NULL) {
        row = tiku_trk_folder_window_row_at(folder, x, y);
        if (row >= 0) {
            tiku_trk_pose_t *pose = tiku_trk_poseview_at(folder->view, row);

            if (pose == NULL || !tiku_trk_drag_can_drop(controller->drag,
                                                        &pose->model)) {
                row = -1;
            }
        }
    }
    /* Within the spring's margin and over no droppable row, the target is
     * kept, so the hover clock does not restart on the way to a submenu
     * item (PVS-054). */
    if (inside_spring && controller->folder != NULL && row < 0) {
        folder = controller->folder;
        row = controller->row;
    }
    tiku_trk_deskbar_set_dragging(controller->deskbar, 1);
    (void)tiku_trk_deskbar_drop_at(controller->deskbar, x, y, &team);
    if (team != tiku_trk_deskbar_drop_team(controller->deskbar)) {
        tiku_trk_deskbar_set_drop_team(controller->deskbar, team);
        changed = 1;
    }
    if (folder != controller->folder || row != controller->row) {
        controller->folder = folder;
        controller->row = row;
        controller->hover_at = now;
        controller->sprung = 0;
        tiku_trk_springmenu_reset(&controller->spring);
        controller->spring_held = 0;
        if (folder != NULL && row >= 0) {
            tiku_trk_pose_t *pose = tiku_trk_poseview_at(folder->view, row);
            int px = 0, py = 0, pw = 1, ph = 1;
            tiku_rect_t content =
                tiku_trk_folder_window_content(folder);

            if (pose != NULL) {
                int ox = 0, oy = 0;

                /* The pose's rect is in view coordinates; the spring area
                 * is on screen, as tiku_trk_folder_window_row_at() maps a
                 * point: past the 2-pixel inset, less the scroll origin. */
                tiku_trk_poseview_pose_rect(folder->view, pose, &px, &py,
                                            &pw, &ph);
                tiku_trk_poseview_origin(folder->view, &ox, &oy);
                tiku_trk_springmenu_enter(&controller->spring,
                    content.x + 2 + px - ox, content.y + 2 + py - oy,
                    pw, ph, now);
            }
        }
        changed = 1;
    }
    if (folder != NULL && row < 0 &&
        window != tiku_workspace_focused(controller->workspace)) {
        tiku_workspace_activate(controller->workspace, window);
        changed = 1;
    }
    if (changed) {
        *controller->dirty = 1;
    }
    return changed;
}

/**
 * @brief Scroll the target view while the drag sits at its edge.
 *        @return 1 when it scrolled.
 */
static int
auto_scroll(tiku_trk_drag_target_t *controller, int64_t now)
{
    tiku_rect_t content;
    int top;
    int bottom;
    int left;
    int right;
    int overshoot = 0;
    int horizontal = 0;
    int distance;
    int step;

    if (!tiku_trk_drag_active(controller->drag) ||
        controller->folder == NULL) {
        return 0;
    }
    content = tiku_trk_folder_window_content(controller->folder);
    top = content.y + 2;
    bottom = content.y + content.h - COUNT_HEIGHT;
    left = content.x + 2;
    right = content.x + content.w - 2;
    if (controller->drag->y < top + AUTOSCROLL_BAND) {
        overshoot = -((top + AUTOSCROLL_BAND) - controller->drag->y);
    } else if (controller->drag->y > bottom - AUTOSCROLL_BAND) {
        overshoot = controller->drag->y - (bottom - AUTOSCROLL_BAND);
    }
    if (tiku_trk_poseview_mode(controller->folder->view) !=
            TIKU_TRK_MODE_LIST) {
        if (controller->drag->x < left + AUTOSCROLL_BAND) {
            horizontal = -((left + AUTOSCROLL_BAND) - controller->drag->x);
        } else if (controller->drag->x > right - AUTOSCROLL_BAND) {
            horizontal = controller->drag->x - (right - AUTOSCROLL_BAND);
        }
    }
    if (overshoot == 0 && horizontal == 0) {
        controller->scroll_at = 0;
        controller->scroll_held = 0;
        return 0;
    }
    if (controller->spring_script_pulses > 0) {
        /* Scripted: the hold is a number of turns, so a script's timing
         * does not depend on the machine's speed (PVS-050). */
        if (controller->scroll_held < controller->spring_script_pulses) {
            controller->scroll_held++;
            return 0;
        }
    } else {
        if (controller->scroll_at == 0) {
            controller->scroll_at = now;
            return 0;
        }
        if (now - controller->scroll_at <= AUTOSCROLL_DELAY_US) {
            return 0;
        }
    }
    distance = overshoot != 0 ? (overshoot < 0 ? -overshoot : overshoot)
                              : (horizontal < 0 ? -horizontal : horizontal);
    step = distance > AUTOSCROLL_FAST ? 3 : 1;
    if (tiku_trk_poseview_mode(controller->folder->view) !=
            TIKU_TRK_MODE_LIST) {
        int origin_x = 0;
        int origin_y = 0;

        tiku_trk_poseview_origin(controller->folder->view, &origin_x,
                                 &origin_y);
        if (horizontal != 0) {
            origin_x += (horizontal < 0 ? -step : step) * controller->row_h;
        }
        if (overshoot != 0) {
            origin_y += (overshoot < 0 ? -step : step) * controller->row_h;
        }
        tiku_trk_poseview_set_origin(controller->folder->view, origin_x,
                                     origin_y);
    } else {
        tiku_trk_poseview_scroll_to(controller->folder->view,
            tiku_trk_poseview_scroll(controller->folder->view) +
                (overshoot < 0 ? -step : step));
    }
    *controller->dirty = 1;
    return 1;
}

int
tiku_trk_drag_target_pulse(tiku_trk_drag_target_t *controller, int64_t now)
{
    int changed = auto_scroll(controller, now);

    int spring_fired = 0;

    if (controller->spring_script_pulses > 0) {
        /* Scripted: the hold is a number of turns (PVS-050). */
        if (tiku_trk_drag_active(controller->drag) &&
            controller->folder != NULL && controller->row >= 0 &&
            !controller->sprung && controller->spring.armed &&
            !controller->spring.open) {
            if (++controller->spring_held >=
                controller->spring_script_pulses) {
                controller->spring.open = 1;
                spring_fired = 1;
            }
        }
    } else {
        spring_fired = tiku_trk_drag_active(controller->drag) &&
                       controller->folder != NULL &&
                       controller->row >= 0 && !controller->sprung &&
                       tiku_trk_springmenu_pulse(&controller->spring, now);
    }
    if (spring_fired) {
        tiku_trk_pose_t *pose = tiku_trk_poseview_at(controller->folder->view,
                                                     controller->row);

        controller->sprung = 1;
        if (pose != NULL && controller->open != NULL) {
            controller->open(controller->open_context, pose->model.path);
        }
        *controller->dirty = 1;
        changed = 1;
    }
    return changed;
}

void
tiku_trk_drag_target_set_script_delay(tiku_trk_drag_target_t *controller,
                                      int pulses)
{
    controller->spring_script_pulses = pulses;
}

void
tiku_trk_drag_target_reset(tiku_trk_drag_target_t *controller)
{
    controller->folder = NULL;
    controller->row = -1;
    controller->scroll_held = 0;
    controller->hover_at = 0;
    controller->scroll_at = 0;
    controller->sprung = 0;
    controller->spring_held = 0;
    tiku_trk_springmenu_reset(&controller->spring);
    tiku_trk_deskbar_set_dragging(controller->deskbar, 0);
}
