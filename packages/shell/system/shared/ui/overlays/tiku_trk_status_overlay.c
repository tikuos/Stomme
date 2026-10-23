/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_status_overlay.c - the alert and the file-job progress panel
 * drawn over everything else, and the pointer and keys they take while
 * they are up.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_trk_status_overlay.h"
#include "tiku_trk_task.h"
#include "tiku_ui.h"
#include <stdio.h>
#include <string.h>

/* The progress panel's geometry, in pixels. */
#define PROGRESS_WIDTH  380
#define PROGRESS_MARGIN 24
#define PROGRESS_ROW_H  48
#define PROGRESS_TOP    22

/** @brief Ask for a redraw, when there is a flag to raise. */
static void
mark_dirty(tiku_trk_status_overlay_t *controller)
{
    if (controller->dirty != NULL) {
        *controller->dirty = 1;
    }
}

/** @brief Where the alert sits: centred, a third of the way down. */
static tiku_rect_t
alert_frame(const tiku_trk_status_overlay_t *controller)
{
    int w, h;

    tiku_alert_size(&w, &h);
    return (tiku_rect_t){
        (controller->screen_w - w) / 2,
        (controller->screen_h - h) / 3, w, h
    };
}

/** @brief Where the progress panel sits: the bottom right corner. */
static tiku_rect_t
progress_frame(const tiku_trk_status_overlay_t *controller)
{
    int height = PROGRESS_TOP + TIKU_TRK_PROGRESS_ROWS * PROGRESS_ROW_H;

    return (tiku_rect_t){
        controller->screen_w - PROGRESS_WIDTH - PROGRESS_MARGIN,
        controller->screen_h - height - PROGRESS_MARGIN,
        PROGRESS_WIDTH, height
    };
}

/** @brief The alert, when one is open. */
static void
draw_alert(tiku_trk_status_overlay_t *controller)
{
    if (!controller->alert.open) {
        return;
    }
    tiku_alert_draw(&controller->alert, controller->surface,
                    alert_frame(controller));
}

/** @brief The progress panel and its rows, when it is open. */
static void
draw_progress(tiku_trk_status_overlay_t *controller)
{
    tiku_rect_t frame = progress_frame(controller);
    const tiku_font_t *font = tiku_font_plain();
    int i, visible_row = 0;

    if (!controller->progress.open) {
        return;
    }
    tiku_fill(controller->surface, frame, TIKU_C_PANEL);
    tiku_frame(controller->surface, frame,
        tiku_tint(TIKU_C_PANEL, 1.40f));
    tiku_bevel(controller->surface, tiku_inset(frame, 1),
        tiku_tint(TIKU_C_PANEL, TIKU_LIGHTEN_MAX),
        tiku_tint(TIKU_C_PANEL, TIKU_DARKEN_2));
    tiku_bevel(controller->surface, tiku_inset(frame, 2),
        tiku_tint(TIKU_C_PANEL, TIKU_LIGHTEN_1),
        tiku_tint(TIKU_C_PANEL, TIKU_DARKEN_1));
    for (i = 0; i < TIKU_TRK_PROGRESS_ROWS; i++) {
        tiku_trk_progress_row_t *progress = &controller->progress.row[i];
        char line[160];
        const char *leaf;
        tiku_rect_t bar;
        int y;

        if (!progress->used) {
            continue;
        }
        y = frame.y + 14 + visible_row * PROGRESS_ROW_H;
        leaf = tiku_trk_progress_row_text(&controller->progress, i);
        leaf = strrchr(leaf, '/');
        leaf = leaf != NULL ? leaf + 1
                            : tiku_trk_progress_row_text(
                                  &controller->progress, i);
        snprintf(line, sizeof line, "%s%.40s",
                 progress->paused ? "" : progress->caption, leaf);
        tiku_text(controller->surface, font, frame.x + 10, y, line,
                       TIKU_C_TEXT);
        if (tiku_trk_progress_show_speed(
                tiku_trk_now() - controller->progress_opened)) {
            int64_t speed = tiku_trk_progress_speed(&controller->progress, i);

            if (speed > 0) {
                snprintf(line, sizeof line, "%lld KB/s",
                         (long long)(speed / 1024));
            } else {
                snprintf(line, sizeof line, "%d of %d", progress->done,
                         progress->total);
            }
        } else {
            int64_t eta = tiku_trk_progress_eta(&controller->progress, i);

            if (eta >= 0) {
                snprintf(line, sizeof line, "%lld s left", (long long)eta);
            } else {
                snprintf(line, sizeof line, "%d of %d", progress->done,
                         progress->total);
            }
        }
        tiku_text(controller->surface, font, frame.x + frame.w - 70,
                       y, line, TIKU_C_TEXT);
        /* The destination folder, under the name (FS-069). */
        if (progress->dest[0] != '\0') {
            const char *to = strrchr(progress->dest, '/');

            snprintf(line, sizeof line, "To: %.60s",
                     (to != NULL && to[1] != '\0') ? to + 1
                                                   : progress->dest);
            tiku_text(controller->surface, font, frame.x + 10, y + 16,
                           line, TIKU_C_TEXT);
        }
        bar = (tiku_rect_t){ frame.x + 10, y + 34,
                                  frame.w - 20, 8 };
        tiku_ui_gauge(controller->surface, bar,
                      (progress->total > 0)
                          ? (float)progress->done / (float)progress->total
                          : 0.0f);
        {
            int action;

            for (action = TIKU_TRK_PROGRESS_HIT_PAUSE;
                 action <= TIKU_TRK_PROGRESS_HIT_STOP; action++) {
                tiku_rect_t button;
                int x, by, width, height;

                tiku_trk_progress_button_rect(visible_row, action, frame.w,
                                               &x, &by, &width, &height);
                button = (tiku_rect_t){ frame.x + x, frame.y + by,
                                             width, height };
                tiku_fill(controller->surface, button,
                               TIKU_C_PANEL);
                tiku_bevel(controller->surface, button,
                    tiku_tint(TIKU_C_PANEL,
                                   TIKU_LIGHTEN_2),
                    tiku_tint(TIKU_C_PANEL,
                                   TIKU_DARKEN_2));
                tiku_text(controller->surface, font, button.x + 6,
                    button.y + 13,
                    action == TIKU_TRK_PROGRESS_HIT_STOP ? "x"
                        : (progress->paused ? ">" : "|"),
                    TIKU_C_TEXT);
            }
        }
        visible_row++;
    }
}

/**
 * @brief Answer the alert with button @p chosen: the answer callback gets
 *        the alert, then the alert is reset.
 */
static void
answer(tiku_trk_status_overlay_t *controller, int chosen)
{
    controller->alert.chosen = chosen;
    if (controller->answer != NULL) {
        controller->answer(controller->callback_context,
                           &controller->alert);
    }
    tiku_alert_reset(&controller->alert);
    mark_dirty(controller);
}

void
tiku_trk_status_overlay_init(tiku_trk_status_overlay_t *controller,
    tiku_surface_t *surface, int screen_w, int screen_h, int *dirty,
    tiku_trk_status_alert_fn answer_callback,
    tiku_trk_status_stop_fn stop_callback, void *callback_context)
{
    memset(controller, 0, sizeof *controller);
    controller->surface = surface;
    controller->screen_w = screen_w;
    controller->screen_h = screen_h;
    controller->dirty = dirty;
    controller->answer = answer_callback;
    controller->stop = stop_callback;
    controller->callback_context = callback_context;
    tiku_alert_reset(&controller->alert);
    tiku_trk_progress_reset(&controller->progress);
}

tiku_alert_t *
tiku_trk_status_overlay_alert(tiku_trk_status_overlay_t *controller)
{
    return &controller->alert;
}

tiku_trk_progress_win_t *
tiku_trk_status_overlay_progress(tiku_trk_status_overlay_t *controller)
{
    return &controller->progress;
}

void
tiku_trk_status_overlay_progress_start(tiku_trk_status_overlay_t *controller,
                                       int64_t now)
{
    if (controller->progress_opened == 0) {
        controller->progress_opened = now;
    }
}

void
tiku_trk_status_overlay_progress_finish(
    tiku_trk_status_overlay_t *controller)
{
    controller->progress_opened = 0;
}

int64_t
tiku_trk_status_overlay_progress_opened(
    const tiku_trk_status_overlay_t *controller)
{
    return controller->progress_opened;
}

void
tiku_trk_status_overlay_draw(tiku_trk_status_overlay_t *controller)
{
    draw_progress(controller);
    draw_alert(controller);
}

int
tiku_trk_status_overlay_pointer(tiku_trk_status_overlay_t *controller,
                                int x, int y)
{
    if (controller->alert.open) {
        tiku_rect_t frame = alert_frame(controller);

        /* A press arms the button under it; the release over it answers
         * (tiku_trk_status_overlay_pointer_up()). */
        (void)tiku_alert_press(&controller->alert, frame.w, frame.h,
                               x - frame.x, y - frame.y);
        mark_dirty(controller);
        return 1;
    }
    if (controller->progress.open) {
        tiku_rect_t frame = progress_frame(controller);
        int row = 0;
        tiku_trk_progress_hit_t hit = tiku_trk_progress_hit(
            &controller->progress, frame.w, x - frame.x, y - frame.y, &row);

        if (hit == TIKU_TRK_PROGRESS_HIT_PAUSE) {
            controller->progress.row[row].paused =
                !controller->progress.row[row].paused;
            mark_dirty(controller);
            return 1;
        }
        if (hit == TIKU_TRK_PROGRESS_HIT_STOP) {
            if (controller->stop != NULL) {
                controller->stop(controller->callback_context);
            }
            mark_dirty(controller);
            return 1;
        }
    }
    return 0;
}

int
tiku_trk_status_overlay_pointer_move(tiku_trk_status_overlay_t *controller,
                                     int x, int y)
{
    tiku_rect_t frame;

    if (!controller->alert.open) {
        return 0;
    }
    frame = alert_frame(controller);
    if (tiku_alert_track(&controller->alert, frame.w, frame.h,
                         x - frame.x, y - frame.y)) {
        mark_dirty(controller);
    }
    return 1;
}

int
tiku_trk_status_overlay_pointer_up(tiku_trk_status_overlay_t *controller,
                                   int x, int y)
{
    tiku_rect_t frame;
    int picked;

    if (!controller->alert.open) {
        return 0;
    }
    frame = alert_frame(controller);
    picked = tiku_alert_release(&controller->alert, frame.w, frame.h,
                                x - frame.x, y - frame.y);
    if (picked >= 0) {
        answer(controller, picked);
    }
    mark_dirty(controller);
    return 1;
}

int
tiku_trk_status_overlay_key(tiku_trk_status_overlay_t *controller,
                            unsigned key)
{
    if (!controller->alert.open) {
        return 0;
    }
    if (key == TIKU_KEY_ESCAPE && controller->alert.cancel >= 0) {
        answer(controller, controller->alert.cancel);
    } else if (key == TIKU_KEY_RETURN &&
               controller->alert.nbutton > 0) {
        answer(controller, controller->alert.dflt >= 0
                               ? controller->alert.dflt
                               : controller->alert.nbutton - 1);
    } else if (key >= 'a' && key <= 'z') {
        /* A lower-case letter answers with the first button whose label
         * starts with it, in either case (FS-046). */
        int i;

        for (i = 0; i < controller->alert.nbutton; i++) {
            unsigned first = (unsigned char)controller->alert.button[i][0];

            if (first >= 'A' && first <= 'Z') {
                first = first - 'A' + 'a';
            }
            if (first == key) {
                answer(controller, i);
                break;
            }
        }
    }
    mark_dirty(controller);
    return 1;
}
