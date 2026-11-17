/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * panel.c - slider and press models with cancellation and damaged repaint.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "panel.h"
#include "tiku_slider_paint.h"
#include "tiku_font.h"
#include "tiku_ui.h"

static const tiku_rect_t slider_box = {24, 104, 500, 28};
static const tiku_rect_t reset_box = {24, 180, 160, 40};
static const tiku_rect_t record_box = {24, 250, 300, 26};

/** @brief Test a point against the fixed control rectangle. */
static int inside(tiku_rect_t rectangle, int x, int y)
{
    return x >= rectangle.x && y >= rectangle.y &&
        x < rectangle.x + rectangle.w && y < rectangle.y + rectangle.h;
}

/** @brief Union bounded control damage into the pending repaint. */
static void damage(tiku_portable_panel_t *panel, tiku_rect_t rectangle)
{
    panel->dirty = tiku_rect_union(panel->dirty, rectangle);
}

int tiku_portable_panel_init(tiku_portable_panel_t *panel, tiku_allocator_t allocator)
{
    if (!panel) return 0;
    *panel = (tiku_portable_panel_t){0};
    panel->frame = tiku_framebuffer_create(allocator, 800, 480, 0);
    if (!panel->frame) return 0;
    panel->events = tiku_event_queue_create(allocator, 16);
    if (!panel->events) {
        (void)tiku_framebuffer_destroy(&panel->frame);
        return 0;
    }
    tiku_slider_init(&panel->slider, 0, 100, 25, 1);
    panel->dirty = (tiku_rect_t){0, 0, 800, 480};
    return 1;
}

size_t tiku_portable_panel_dispatch(tiku_portable_panel_t *panel, size_t budget)
{
    size_t count;
    if (!panel || !panel->frame || !panel->events) return 0;
    for (count = 0; count < budget; ++count) {
        tiku_input_event_t event;
        tiku_queue_result_t result = tiku_event_queue_take(panel->events, &event);
        int hit;
        if (result == TIKU_QUEUE_EMPTY) break;
        if (result == TIKU_QUEUE_CANCEL) {
            tiku_slider_release(&panel->slider);
            (void)tiku_press_release(&panel->press, 0);
            damage(panel, (tiku_rect_t){24, 104, 500, 172});
            continue;
        }
        hit = inside(reset_box, event.x, event.y) ? 1 :
              inside(record_box, event.x, event.y) ? 2 : 0;
        if (event.type == TIKU_EVENT_POINTER_DOWN) {
            if (inside(slider_box, event.x, event.y)) {
                (void)tiku_slider_press(&panel->slider, slider_box, event.x);
                damage(panel, slider_box);
            } else if (tiku_press_arm(&panel->press, hit)) {
                damage(panel, hit == 1 ? reset_box : record_box);
            }
        } else if (event.type == TIKU_EVENT_POINTER_MOVE) {
            if (panel->slider.tracking) {
                (void)tiku_slider_drag(&panel->slider, slider_box, event.x);
                damage(panel, slider_box);
            }
            if (tiku_press_track(&panel->press, hit)) {
                damage(panel, (tiku_rect_t){24, 180, 300, 96});
            }
        } else if (event.type == TIKU_EVENT_POINTER_UP) {
            int action = tiku_press_release(&panel->press, hit);
            tiku_slider_release(&panel->slider);
            if (action == 1) (void)tiku_slider_set(&panel->slider, 25);
            if (action == 2) panel->recording = !panel->recording;
            damage(panel, (tiku_rect_t){24, 104, 500, 172});
        }
    }
    return count;
}

tiku_present_result_t tiku_portable_panel_render(tiku_portable_panel_t *panel,
                                                  tiku_presenter_t presenter)
{
    tiku_surface_t *surface;
    tiku_present_result_t result;
    if (!panel || !panel->frame) return TIKU_PRESENT_ERROR;
    if (!panel->dirty.w || !panel->dirty.h) return TIKU_PRESENT_OK;
    surface = tiku_framebuffer_begin(panel->frame, &panel->dirty);
    if (!surface) return TIKU_PRESENT_BUSY;
    tiku_ui_panel(surface, (tiku_rect_t){0, 0, 800, 480});
    tiku_text(surface, tiku_font_bold(), 24, 40, "Stomme portable panel", TIKU_C_TEXT);
    tiku_text(surface, tiku_font_plain(), 24, 78, "Level", TIKU_C_TEXT);
    tiku_slider_draw(&panel->slider, surface, slider_box);
    tiku_ui_button(surface, reset_box, "Reset level",
                   tiku_press_held(&panel->press, 1) ? TIKU_S_PRESSED : 0);
    tiku_ui_checkbox(surface, record_box, "Record audio",
                     (panel->recording ? TIKU_S_ON : 0) |
                     (tiku_press_held(&panel->press, 2) ? TIKU_S_PRESSED : 0));
    tiku_text(surface, tiku_font_plain(), 24, 340,
              "800 x 480 | local rendering | bounded memory", TIKU_C_TEXT);
    result = tiku_framebuffer_present(panel->frame, presenter);
    if (result == TIKU_PRESENT_OK) panel->dirty = (tiku_rect_t){0};
    else (void)tiku_framebuffer_abandon(panel->frame);
    return result;
}

int tiku_portable_panel_stop(tiku_portable_panel_t *panel)
{
    if (!panel) return 1;
    if (!tiku_framebuffer_destroy(&panel->frame)) return 0;
    tiku_event_queue_close(panel->events);
    return tiku_event_queue_destroy(&panel->events);
}
