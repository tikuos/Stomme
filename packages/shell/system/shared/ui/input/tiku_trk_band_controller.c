/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_band_controller.c - the rubber band: arming, sweeping, and
 * scrolling while held at a window's edge.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_trk_band_controller.h"
#include "tiku_trk_drag.h"

#define AUTOSCROLL_BAND 6
#define AUTOSCROLL_FAST 30
#define AUTOSCROLL_DELAY_US 600000
#define COUNT_HEIGHT 16

void
tiku_trk_band_controller_init(tiku_trk_band_controller_t *controller,
                              int row_h, int *dirty)
{
    *controller = (tiku_trk_band_controller_t){
        .row_h = row_h,
        .dirty = dirty
    };
}

/** @brief Select what the band covers, in the view's own coordinates. */
static void
select_band(tiku_trk_band_controller_t *controller)
{
    tiku_rect_t content;
    int origin_x = 0;
    int origin_y = 0;

    if (controller == NULL || controller->folder == NULL) {
        return;
    }
    content = tiku_trk_folder_window_content(controller->folder);
    tiku_trk_poseview_origin(controller->folder->view, &origin_x, &origin_y);
    (void)tiku_trk_poseview_select_rect(controller->folder->view,
        controller->x0 - (content.x + 2) + origin_x,
        controller->y0 - (content.y + 2) + origin_y,
        controller->x1 - (content.x + 2) + origin_x,
        controller->y1 - (content.y + 2) + origin_y,
        controller->modifiers);
}

void
tiku_trk_band_controller_arm(tiku_trk_band_controller_t *controller,
    tiku_trk_folder_window_t *folder, int x, int y, unsigned modifiers)
{
    controller->armed_folder = folder;
    controller->folder = NULL;
    controller->x0 = controller->x1 = x;
    controller->y0 = controller->y1 = y;
    controller->modifiers = modifiers;
    controller->scroll_at = 0;
    controller->held_pulses = 0;
}

int
tiku_trk_band_controller_motion(tiku_trk_band_controller_t *controller,
                                int x, int y, unsigned modifiers)
{
    if (controller->armed_folder != NULL &&
        (x - controller->x0 > TIKU_TRK_DRAG_SLOP ||
         controller->x0 - x > TIKU_TRK_DRAG_SLOP ||
         y - controller->y0 > TIKU_TRK_DRAG_SLOP ||
         controller->y0 - y > TIKU_TRK_DRAG_SLOP)) {
        tiku_trk_poseview_set_band(controller->armed_folder->view, 1);
        controller->folder = controller->armed_folder;
        controller->armed_folder = NULL;
    }
    if (controller->folder == NULL) {
        return 0;
    }
    controller->x1 = x;
    controller->y1 = y;
    controller->modifiers = modifiers;
    select_band(controller);
    *controller->dirty = 1;
    return 1;
}

int
tiku_trk_band_controller_finish(tiku_trk_band_controller_t *controller)
{
    int active = controller->folder != NULL;

    controller->armed_folder = NULL;
    controller->scroll_at = 0;
    controller->held_pulses = 0;
    if (controller->folder != NULL) {
        tiku_trk_poseview_set_band(controller->folder->view, 0);
        (void)tiku_trk_poseview_save_state(controller->folder->view);
        controller->folder = NULL;
        *controller->dirty = 1;
    }
    return active;
}

void
tiku_trk_band_controller_set_script_delay(
    tiku_trk_band_controller_t *controller, int pulses)
{
    controller->script_delay_pulses = pulses;
}

int
tiku_trk_band_controller_pulse(tiku_trk_band_controller_t *controller,
                               int64_t now)
{
    tiku_rect_t content;
    int top;
    int bottom;
    int overshoot = 0;
    int distance;
    int step;

    if (controller->folder == NULL) {
        return 0;
    }
    content = tiku_trk_folder_window_content(controller->folder);
    top = content.y + 2;
    bottom = content.y + content.h - COUNT_HEIGHT;
    if (controller->y1 < top + AUTOSCROLL_BAND) {
        overshoot = -((top + AUTOSCROLL_BAND) - controller->y1);
    } else if (controller->y1 > bottom - AUTOSCROLL_BAND) {
        overshoot = controller->y1 - (bottom - AUTOSCROLL_BAND);
    }
    if (overshoot == 0) {
        controller->scroll_at = 0;
        controller->held_pulses = 0;
        return 0;
    }
    if (controller->script_delay_pulses > 0) {
        /* Scripted: the delay is a number of turns, so a script passes the
         * same on a loaded machine and an idle one. */
        if (controller->held_pulses < controller->script_delay_pulses) {
            controller->held_pulses++;
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
    distance = overshoot < 0 ? -overshoot : overshoot;
    step = distance > AUTOSCROLL_FAST ? 3 : 1;
    if (tiku_trk_poseview_mode(controller->folder->view) ==
            TIKU_TRK_MODE_LIST) {
        tiku_trk_poseview_scroll_to(controller->folder->view,
            tiku_trk_poseview_scroll(controller->folder->view) +
                (overshoot < 0 ? -step : step));
    } else {
        int origin_x = 0;
        int origin_y = 0;

        tiku_trk_poseview_origin(controller->folder->view, &origin_x,
                                 &origin_y);
        tiku_trk_poseview_set_origin(controller->folder->view, origin_x,
            origin_y + (overshoot < 0 ? -step : step) * controller->row_h);
    }
    select_band(controller);
    *controller->dirty = 1;
    return 1;
}
