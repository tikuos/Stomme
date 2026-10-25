/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_prefs_window.c - the Tracker preferences window: the list of
 * pages, the settings of the page shown, and Defaults and Revert.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_strings.h"
#include "tiku_trk_gfx_dep.h"
#include "tiku_trk_prefs_window.h"
#include "tiku_ui.h"
#include <stdio.h>
#include <string.h>

/**
 * @brief Draw the page list, the settings of the page shown, and the
 *        Defaults and Revert buttons.
 */
static void
draw(tiku_window_t *window, tiku_surface_t *surface,
     tiku_rect_t content, void *context)
{
    tiku_trk_prefs_window_t *controller = context;
    const tiku_font_t *font = tiku_font_plain();
    int index[32], count, i, x, y, width, height;

    (void)window;
    tiku_fill(surface, content, TIKU_C_PANEL);
    for (i = 0; i < TIKU_TRK_PAGES; i++) {
        tiku_rect_t rect;
        int selected = (i == (int)controller->page);

        tiku_trk_prefs_page_rect(i, &x, &y, &width, &height);
        rect.x = content.x + x;
        rect.y = content.y + y;
        rect.w = width;
        rect.h = height;
        tiku_fill(surface, rect, TIKU_C_PANEL);
        if (selected) {
            tiku_fill_round(surface, rect, 3, TIKU_CORNER_ALL, TIKU_C_SELECT);
        }
        {
            const char *name =
                tiku_trk_prefs_page_name((tiku_trk_page_t)i);
            unsigned ink = selected ? TIKU_C_SELTEXT
                                    : TIKU_C_TEXT;

            tiku_text(surface, font, rect.x + 6, rect.y + 15, name,
                           ink);
            if (tiku_trk_prefs_revertable(&controller->model,
                                          (tiku_trk_page_t)i)) {
                /* A page with changes to revert is drawn bold, by
                 * drawing its name again one pixel over (TS-060). */
                tiku_text(surface, font, rect.x + 7, rect.y + 15,
                               name, ink);
            }
        }
    }
    count = tiku_trk_prefs_page_controls(&controller->model,
                                          controller->page, index, 32);
    for (i = 0; i < count; i++) {
        tiku_trk_pref_t *pref = &controller->model.pref[index[i]];
        tiku_rect_t box;

        tiku_trk_prefs_control_rect(i, &x, &y, &width, &height);
        box.x = content.x + x;
        box.y = content.y + y + 4;
        box.w = 12;
        box.h = 12;
        int on = tiku_trk_prefs_control_enabled(&controller->model, pref);
        unsigned ink = on ? TIKU_C_TEXT
                          : tiku_dim(TIKU_C_TEXT, TIKU_C_PANEL);

        if (pref->type == TIKU_TRK_PREF_BOOL) {
            /* An on/off setting is drawn as the kit's checkbox. */
            tiku_rect_t row = { content.x + x, content.y + y,
                                     width, height };

            tiku_ui_checkbox(surface, row, pref->name,
                (unsigned)(pref->value ? TIKU_S_ON : 0u)
                | (unsigned)(on ? 0u : TIKU_S_DISABLED));
            continue;
        } else if (pref->type == TIKU_TRK_PREF_COLOUR) {
            /* The swatch is the control: it shows the colour, and a
             * click moves to the next one. */
            tiku_fill(surface, box,
                           (tiku_rgb_t)pref->value & 0xffffffu);
            tiku_frame(surface, box, TIKU_C_TEXT);
        } else {
            /*
             * A setting with a few positions is a track with the knob in
             * the seat its value names; a click moves the knob to the
             * next seat.
             */
            int seats = tiku_trk_prefs_seats(pref->name);
            int at = (seats > 1) ? (int)pref->value % seats : 0;
            tiku_rect_t knob = box;

            if (at < 0) {
                at = 0;
            }
            tiku_fill(surface, box, tiku_tint(TIKU_C_PANEL, 1.10f));
            tiku_frame(surface, box, ink);
            knob.w = (seats > 1) ? box.w / seats : box.w;
            knob.x = box.x + at * knob.w;
            tiku_fill(surface, knob, TIKU_C_PANEL);
            tiku_frame(surface, knob, ink);
        }
        tiku_text(surface, font, content.x + x + 18,
                       content.y + y + 14, pref->name, ink);
    }
    for (i = 0; i < 2; i++) {
        tiku_rect_t button;
        int enabled = i ? tiku_trk_prefs_any_revertable(&controller->model)
                        : tiku_trk_prefs_any_defaultable(&controller->model);

        tiku_trk_prefs_button_rect(i, &x, &y, &width, &height);
        button.x = content.x + x;
        button.y = content.y + y;
        button.w = width;
        button.h = height;
        tiku_ui_button(
            surface, button,
            i ? tiku_str("button.revert") : tiku_str("button.defaults"),
            (enabled ? 0u : TIKU_S_DISABLED) |
                (tiku_press_held(&controller->press, i + 1) ? TIKU_S_PRESSED
                                                            : 0u));
    }
}

/**
 * @brief A press picks a page or changes a setting, and Defaults and
 *        Revert act on a release over them; each change is saved at once.
 * @return 1 when taken.
 */
static int
event(tiku_window_t *window, const tiku_event_t *event,
      void *context)
{
    tiku_trk_prefs_window_t *controller = context;
    tiku_rect_t content;
    tiku_trk_pref_hit_t hit;
    int which = -1;

    if (controller->blocked != NULL && *controller->blocked) {
        return 0;
    }
    content = tiku_window_content(window);
    hit = tiku_trk_prefs_hit(&controller->model, controller->page,
                             event->x - content.x, event->y - content.y,
                             &which);
    /* The two buttons show pressed while held and act on a release over
     * them; everything else in the window acts on the press. */
    {
        int under = (hit == TIKU_TRK_PREF_HIT_DEFAULTS &&
                     tiku_trk_prefs_any_defaultable(&controller->model)) ? 1
                  : (hit == TIKU_TRK_PREF_HIT_REVERT &&
                     tiku_trk_prefs_any_revertable(&controller->model)) ? 2
                  : 0;

        if (event->type == TIKU_EVENT_POINTER_MOVE) {
            if (tiku_press_track(&controller->press, under)) {
                if (controller->dirty != NULL) { *controller->dirty = 1; }
            }
            return controller->press.id != 0;
        }
        if (event->type == TIKU_EVENT_POINTER_UP) {
            int id = tiku_press_release(&controller->press, under);

            if (controller->dirty != NULL) { *controller->dirty = 1; }
            if (id == 0) { return 1; }
            hit = (id == 1) ? TIKU_TRK_PREF_HIT_DEFAULTS
                            : TIKU_TRK_PREF_HIT_REVERT;
        } else if (event->type == TIKU_EVENT_POINTER_DOWN && under != 0) {
            (void)tiku_press_arm(&controller->press, under);
            if (controller->dirty != NULL) { *controller->dirty = 1; }
            return 1;
        } else if (event->type != TIKU_EVENT_POINTER_DOWN) {
            return 0;
        }
    }
    if (hit == TIKU_TRK_PREF_HIT_NONE) {
        return 0;
    }
    if (hit == TIKU_TRK_PREF_HIT_PAGE) {
        controller->page = (tiku_trk_page_t)which;
    } else if (hit == TIKU_TRK_PREF_HIT_SETTING) {
        tiku_trk_pref_t *pref = &controller->model.pref[which];

        if (!tiku_trk_prefs_control_enabled(&controller->model, pref)) {
            return 1;               /* greyed: the click is refused */
        }
        if (pref->type == TIKU_TRK_PREF_BOOL) {
            tiku_trk_prefs_set(&controller->model, pref->name,
                               !pref->value);
        } else if (pref->type == TIKU_TRK_PREF_COLOUR) {
            pref->value = tiku_trk_prefs_colour_next(pref->value);
        } else {
            int seats = tiku_trk_prefs_seats(pref->name);

            tiku_trk_prefs_set(&controller->model, pref->name,
                               (seats > 1)
                                   ? (pref->value + 1) % seats
                                   : pref->value);
        }
    } else {
        int page;

        for (page = 0; page < TIKU_TRK_PAGES; page++) {
            if (hit == TIKU_TRK_PREF_HIT_DEFAULTS) {
                tiku_trk_prefs_defaults(&controller->model,
                                        (tiku_trk_page_t)page);
            } else {
                tiku_trk_prefs_revert(&controller->model,
                                      (tiku_trk_page_t)page);
            }
        }
    }
    if (controller->path[0] != '\0') {
        (void)tiku_trk_prefs_submit(&controller->model, controller->path);
    }
    if (controller->dirty != NULL) {
        *controller->dirty = 1;
    }
    return 1;
}

void
tiku_trk_prefs_window_init(tiku_trk_prefs_window_t *controller,
    tiku_workspace_t *workspace, int screen_w, int screen_h,
    const char *settings_path, int *dirty, const int *blocked)
{
    memset(controller, 0, sizeof *controller);
    controller->workspace = workspace;
    controller->screen_w = screen_w;
    controller->screen_h = screen_h;
    controller->dirty = dirty;
    controller->blocked = blocked;
    controller->page = TIKU_TRK_PAGE_DESKTOP;
    tiku_trk_prefs_init(&controller->model);
    if (settings_path != NULL && settings_path[0] != '\0') {
        snprintf(controller->path, sizeof controller->path, "%s",
                 settings_path);
        (void)tiku_trk_prefs_load(&controller->model, controller->path);
    }
}

void
tiku_trk_prefs_window_destroy(tiku_trk_prefs_window_t *controller)
{
    if (controller->window != NULL && controller->window->open) {
        tiku_workspace_close(controller->workspace, controller->window);
    }
    controller->window = NULL;
    tiku_trk_prefs_free(&controller->model);
}

tiku_trk_prefs_t *
tiku_trk_prefs_window_model(tiku_trk_prefs_window_t *controller)
{
    return &controller->model;
}

tiku_window_t *
tiku_trk_prefs_window_handle(tiku_trk_prefs_window_t *controller)
{
    return controller->window;
}

void
tiku_trk_prefs_window_open(tiku_trk_prefs_window_t *controller)
{
    tiku_rect_t frame;

    if (controller->window != NULL && controller->window->open) {
        tiku_workspace_activate(controller->workspace,
                                     controller->window);
        return;
    }
    frame.x = (controller->screen_w - TIKU_TRK_PREF_W) / 2;
    frame.y = (controller->screen_h - TIKU_TRK_PREF_H) / 3;
    frame.w = TIKU_TRK_PREF_W;
    frame.h = TIKU_TRK_PREF_H + 26;
    tiku_trk_prefs_mark_opened(&controller->model);
    controller->window = tiku_workspace_open(controller->workspace,
        tiku_str("title.tracker_prefs"), frame, draw, controller, NULL);
    if (controller->window != NULL) {
        tiku_window_set_handlers(controller->window, event, NULL, NULL);
    }
}

void
tiku_trk_prefs_window_close(tiku_trk_prefs_window_t *controller)
{
    if (controller == NULL || controller->window == NULL ||
        !controller->window->open) {
        return;
    }
    tiku_workspace_close(controller->workspace, controller->window);
    controller->window = NULL;
}
