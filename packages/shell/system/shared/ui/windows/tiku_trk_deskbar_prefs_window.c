/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_deskbar_prefs_window.c - the Deskbar preferences window: its
 * rows, the dock monitor and the three buttons.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_layout.h"
#include "tiku_strings.h"
#include "tiku_text_layout_paint.h"
#include "tiku_trk_deskbar_prefs_window.h"
#include "tiku_trk_gfx_dep.h"
#include "tiku_ui.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum {
    ROW_SORT = 0, ROW_TRACKER, ROW_EXPANDER, ROW_EXPAND_NEW,
    ROW_HIDE_NAMES, ROW_ICON, ROW_FONT, ROW_DOCS, ROW_FOLDERS, ROW_APPS,
    ROW_ALWAYS_TOP, ROW_AUTO_RAISE, ROW_AUTO_HIDE, ROW_COUNT
};

/* The catalogue key of each row's label, in ROW order; the text is looked
 * up when the row is drawn. */
static const char *const label_keys[ROW_COUNT] = {
    "prefs.deskbar.sort_apps", "prefs.deskbar.tracker_first",
    "prefs.deskbar.show_expander", "prefs.deskbar.expand_new",
    "prefs.deskbar.hide_app_names", "label.icon_size",
    "prefs.deskbar.font_size", "label.recent_documents",
    "label.recent_folders", "label.recent_applications",
    "prefs.deskbar.always_on_top", "prefs.deskbar.auto_raise",
    "prefs.deskbar.auto_hide"
};

/** @brief Whether the point lies within @p r. */
static int
inside(tiku_rect_t r, int x, int y)
{
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

/** @brief A push button in the toolkit's look, greyed when not @p enabled. */
static void
button(tiku_surface_t *surface, tiku_rect_t rect,
       const char *label, int enabled, int down)
{
    tiku_ui_button(surface, rect, label,
                        (enabled ? 0u : TIKU_S_DISABLED) |
                        (down ? TIKU_S_PRESSED : 0u));
}

/** @brief Whether the row's setting applies at this dock and these settings. */
static int
row_enabled(const tiku_trk_deskbar_prefs_window_t *controller, int row)
{
    if (row == ROW_EXPANDER || row == ROW_EXPAND_NEW) {
        return tiku_trk_deskbar_prefs_expanders_enabled(&controller->model,
                   tiku_trk_deskbar_dock(controller->deskbar));
    }
    if (row == ROW_AUTO_RAISE) {
        return tiku_trk_deskbar_prefs_auto_raise_enabled(&controller->model);
    }
    return 1;
}

/** @brief The on/off setting a row toggles, or NULL for a stepper row. */
static int *
row_value(tiku_trk_deskbar_settings_t *s, int row)
{
    switch (row) {
    case ROW_SORT:        return &s->sort_apps;
    case ROW_TRACKER:     return &s->tracker_first;
    case ROW_EXPANDER:    return &s->show_expander;
    case ROW_EXPAND_NEW:  return &s->expand_new_apps;
    case ROW_HIDE_NAMES:  return &s->hide_app_names;
    case ROW_ALWAYS_TOP:  return &s->always_on_top;
    case ROW_AUTO_RAISE:  return &s->auto_raise;
    case ROW_AUTO_HIDE:   return &s->auto_hide;
    default:              return NULL;
    }
}

/** @brief Normalise, apply to the bar, save, and ask for a redraw. */
static void
apply(tiku_trk_deskbar_prefs_window_t *controller)
{
    if (controller->submit_save) {
        (void)controller->submit_save(controller->save_context);
        if (controller->dirty) *controller->dirty = 1;
        return;
    }
    tiku_trk_deskbar_settings_normalize(&controller->model.value);
    tiku_trk_deskbar_apply_settings(controller->deskbar,
                                    &controller->model.value);
    (void)tiku_trk_deskbar_prefs_save(&controller->model,
                                      controller->store);
    (void)tiku_store_flush(controller->store);
    if (controller->dirty != NULL) { *controller->dirty = 1; }
}

/**
 * @brief The stepper's layout: part 0 the minus, part 1 the number,
 *        part 2 the plus.  The drawing and the press both read it.
 */
static void
stepper_row(tiku_layout_t *l)
{
    int w = tiku_ui_step_w();

    tiku_layout_init(l, 1, 4, 0);
    (void)tiku_layout_add(l, w, 0, 0);      /* minus  */
    (void)tiku_layout_add(l, 0, 1, 40);     /* the number it changes */
    (void)tiku_layout_add(l, w, 0, 0);      /* plus   */
}

/** @brief The run the stepper is laid along, in the row at @p y. */
static tiku_rect_t
stepper_rect(tiku_rect_t content, int y)
{
    tiku_rect_t r;
    int h = tiku_ui_row_h();

    r.x = content.x + 320;
    r.y = y - 2;
    r.w = 140;
    r.h = h;
    return r;
}

/** @brief Draw the rows, the dock monitor, and the three buttons. */
static void
draw(tiku_window_t *window, tiku_surface_t *surface,
     tiku_rect_t content, void *context)
{
    tiku_trk_deskbar_prefs_window_t *controller = context;
    tiku_trk_deskbar_settings_t *s = &controller->model.value;
    const tiku_font_t *font = tiku_font_plain();
    int row;

    (void)window;
    tiku_fill(surface, content, TIKU_C_PANEL);
    for (row = 0; row < ROW_COUNT; row++) {
        int y = content.y + 12 + row * 29;
        int enabled = row_enabled(controller, row);
        int *value = row_value(s, row);
        char number[24];

        if (value != NULL) {
            tiku_rect_t box = { content.x + 18, y, 13, 13 };

            tiku_fill(surface, box, TIKU_C_DOC);
            tiku_frame(surface, box, TIKU_C_TEXT);
            if (*value) {
                tiku_text(surface, font, box.x + 2, box.y + 11, "x",
                               TIKU_C_TEXT);
            }
        }
        tiku_text(surface, font, content.x + 40, y + 12,
            tiku_str(label_keys[row]),
            enabled ? TIKU_C_TEXT
                    : tiku_tint(TIKU_C_PANEL, 1.45f));
        if (row == ROW_ICON) {
            snprintf(number, sizeof number, "%d px", s->icon_size);
        } else if (row == ROW_FONT) {
            snprintf(number, sizeof number, "%d px", s->font_size);
        } else if (row >= ROW_DOCS && row <= ROW_APPS) {
            int which = row - ROW_DOCS;
            tiku_rect_t box = { content.x + 18, y, 13, 13 };

            tiku_fill(surface, box, TIKU_C_DOC);
            tiku_frame(surface, box, TIKU_C_TEXT);
            if (s->recent_enabled[which]) {
                tiku_text(surface, font, box.x + 2, box.y + 11, "x",
                               TIKU_C_TEXT);
            }
            snprintf(number, sizeof number, "%d",
                     s->recent_count[which]);
        } else {
            number[0] = '\0';
        }
        if (number[0] != '\0') {
            /* The two arrows and the number between them, placed by the
             * layout the press reads. */
            tiku_layout_t step;
            tiku_rect_t run = stepper_rect(content, y);
            tiku_rect_t minus, mid, plus;

            stepper_row(&step);
            minus = tiku_layout_slot(&step, run, 0);
            mid = tiku_layout_slot(&step, run, 1);
            plus = tiku_layout_slot(&step, run, 2);
            tiku_ui_button(surface, minus, "-",
                                enabled ? 0u : TIKU_S_DISABLED);
            tiku_ui_button(surface, plus, "+",
                                enabled ? 0u : TIKU_S_DISABLED);
            tiku_text(surface, font, mid.x + 4, y + 12, number,
                           enabled ? TIKU_C_TEXT
                                   : tiku_dim(TIKU_C_TEXT, TIKU_C_PANEL));
        }
    }
    {
        tiku_rect_t monitor = { content.x + 292, content.y + 392,
                                     116, 82 };
        tiku_trk_db_dock_t dock = tiku_trk_deskbar_dock(controller->deskbar);
        char current[32];

        tiku_fill(surface, monitor, TIKU_C_DOC);
        tiku_frame(surface, monitor, TIKU_C_TEXT);
        snprintf(current, sizeof current, tiku_str("prefs.deskbar.dock_n"),
                 (int)dock);
        tiku_text(surface, font, monitor.x + 29, monitor.y + 45,
                       current, TIKU_C_TEXT);
        tiku_text(surface, font, content.x + 18, content.y + 432,
                       tiku_str("prefs.deskbar.dock_location"),
                       TIKU_C_TEXT);
    }
    if (controller->save_status) {
        const char *status = controller->save_status(controller->save_context);
        if (status && *status) tiku_text_line_draw(surface,
            (tiku_rect_t){content.x + 18, content.y + 518, content.w - 36, 30},
            TIKU_MEASURE_PLAIN, status, TIKU_C_TEXT);
    }
    button(surface, (tiku_rect_t){ content.x + 18, content.y + 488,
           112, 26 }, tiku_str("button.defaults"),
           tiku_trk_deskbar_prefs_defaultable(&controller->model),
           tiku_press_held(&controller->press, 1));
    button(surface, (tiku_rect_t){ content.x + 140, content.y + 488,
           100, 26 }, tiku_str("button.revert"),
           tiku_trk_deskbar_prefs_revertable(&controller->model),
           tiku_press_held(&controller->press, 2));
    button(surface, (tiku_rect_t){ content.x + 278, content.y + 488,
           142, 26 }, tiku_str("prefs.deskbar.edit_in_tracker"), 1,
           tiku_press_held(&controller->press, 3));
}

/**
 * @brief A press on a row toggles or steps its setting, and on the monitor
 *        moves the dock; a button acts on a release over it.
 * @return 1 when taken.
 */
static int
event(tiku_window_t *window, const tiku_event_t *event,
      void *context)
{
    tiku_trk_deskbar_prefs_window_t *controller = context;
    tiku_rect_t content = tiku_window_content(window);
    int x = event->x - content.x, y = event->y - content.y;
    int row = (y - 12) / 29;

    {
        /* The three buttons along the bottom show pressed while held and
         * act on a release over them; the rows act on the press. */
        int under = (inside((tiku_rect_t){ 18, 488, 112, 26 }, x, y) &&
                     tiku_trk_deskbar_prefs_defaultable(&controller->model)) ? 1
                  : (inside((tiku_rect_t){ 140, 488, 100, 26 }, x, y) &&
                     tiku_trk_deskbar_prefs_revertable(&controller->model)) ? 2
                  : inside((tiku_rect_t){ 278, 488, 142, 26 }, x, y) ? 3
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
            if (id == 1) {
                tiku_trk_deskbar_prefs_defaults(&controller->model);
                apply(controller);
            } else if (id == 2) {
                tiku_trk_deskbar_prefs_revert(&controller->model);
                apply(controller);
            } else if (id == 3 && controller->open_path != NULL &&
                       controller->menu_dir[0] != '\0') {
                controller->open_path(controller->open_context,
                                      controller->menu_dir);
            }
            return 1;
        }
        if (event->type == TIKU_EVENT_POINTER_DOWN && under != 0) {
            (void)tiku_press_arm(&controller->press, under);
            if (controller->dirty != NULL) { *controller->dirty = 1; }
            return 1;
        }
    }
    if (event->type != TIKU_EVENT_POINTER_DOWN ||
        !inside(content, event->x, event->y)) {
        return 0;
    }
    if (row >= 0 && row < ROW_COUNT && y >= 12 && y < 389) {
        tiku_trk_deskbar_settings_t *s = &controller->model.value;
        int *value = row_value(s, row);

        if (!row_enabled(controller, row)) { return 1; }
        if (row == ROW_ICON || row == ROW_FONT ||
            (row >= ROW_DOCS && row <= ROW_APPS)) {
            /*
             * Part 0 of the stepper is the minus and part 2 the plus; a
             * press elsewhere in the row is not a step.  In a recent-items
             * row, a press left of x 300 toggles the row's box.
             */
            tiku_layout_t step;
            tiku_rect_t run = stepper_rect((tiku_rect_t){ 0, 0, 0, 0 },
                                           12 + row * 29);
            int part;

            stepper_row(&step);
            part = tiku_layout_at(&step, run, x, y);
            if (row == ROW_ICON) {
                if (part == 0) { s->icon_size -= 8; }
                if (part == 2) { s->icon_size += 8; }
            } else if (row == ROW_FONT) {
                if (part == 0) { s->font_size -= 2; }
                if (part == 2) { s->font_size += 2; }
            } else {
                int which = row - ROW_DOCS;

                if (x < 300) {
                    s->recent_enabled[which] = !s->recent_enabled[which];
                } else if (part == 0) {
                    s->recent_count[which] -= 1;
                } else if (part == 2) {
                    s->recent_count[which] += 1;
                }
            }
        } else if (value != NULL) {
            *value = !*value;
        }
        apply(controller);
        return 1;
    }
    if (inside((tiku_rect_t){ 292, 392, 116, 82 }, x, y)) {
        tiku_trk_db_dock_t dock = tiku_trk_deskbar_prefs_dock_at(
            x - 292, y - 392, 116, 82,
            tiku_trk_deskbar_dock(controller->deskbar));

        tiku_trk_deskbar_set_dock(controller->deskbar, dock);
        if (controller->dirty != NULL) { *controller->dirty = 1; }
        return 1;
    }
    return 0;
}

/** @brief The frame the session remembers, or the default placement. */
static tiku_rect_t
saved_frame(tiku_trk_deskbar_prefs_window_t *controller)
{
    int32_t position[2];
    int found;

    found = tiku_state_read(controller->store, TIKU_KEY_SESSION,
            TIKU_ATTR_DESKBAR_PREFS_FRAME, position, sizeof position) ==
            (int)sizeof position;
    return tiku_trk_deskbar_prefs_frame(found,
        found ? position[0] : 0, found ? position[1] : 0,
        controller->screen_w, controller->screen_h,
        TIKU_TRK_DB_PREFS_W, TIKU_TRK_DB_PREFS_H);
}

void
tiku_trk_deskbar_prefs_window_init(
    tiku_trk_deskbar_prefs_window_t *controller,
    tiku_workspace_t *workspace, tiku_trk_deskbar_t *deskbar,
    tiku_store_t *store, int screen_w, int screen_h, const char *menu_dir,
    int *dirty, tiku_trk_deskbar_prefs_open_fn open_path, void *open_context)
{
    memset(controller, 0, sizeof *controller);
    controller->workspace = workspace;
    controller->deskbar = deskbar;
    controller->store = store;
    controller->screen_w = screen_w;
    controller->screen_h = screen_h;
    controller->dirty = dirty;
    controller->open_path = open_path;
    controller->open_context = open_context;
    snprintf(controller->menu_dir, sizeof controller->menu_dir, "%s",
             menu_dir != NULL ? menu_dir : "");
    tiku_trk_deskbar_prefs_init(&controller->model);
    (void)tiku_trk_deskbar_prefs_load(&controller->model, store);
    tiku_trk_deskbar_apply_settings(deskbar, &controller->model.value);
}

void
tiku_trk_deskbar_prefs_window_close(
    tiku_trk_deskbar_prefs_window_t *controller)
{
    if (controller->window != NULL && controller->window->open) {
        int32_t position[2] = { controller->window->frame.x,
                                controller->window->frame.y };

        (void)tiku_state_write(controller->store, TIKU_KEY_SESSION,
            TIKU_ATTR_DESKBAR_PREFS_FRAME, position, sizeof position);
        (void)tiku_store_flush(controller->store);
        tiku_workspace_close(controller->workspace, controller->window);
    }
    controller->window = NULL;
    if (controller->dirty != NULL) { *controller->dirty = 1; }
}

void
tiku_trk_deskbar_prefs_window_destroy(
    tiku_trk_deskbar_prefs_window_t *controller)
{
    tiku_trk_deskbar_prefs_window_close(controller);
}

void
tiku_trk_deskbar_prefs_window_open(
    tiku_trk_deskbar_prefs_window_t *controller)
{
    if (controller->window != NULL && controller->window->open) {
        tiku_workspace_activate(controller->workspace,
                                     controller->window);
        return;
    }
    /* Read afresh: the Fonts window writes two of these fields, and a
     * stale model would write the old face back on the next change made
     * here. */
    (void)tiku_trk_deskbar_prefs_load(&controller->model, controller->store);
    tiku_trk_deskbar_prefs_mark_opened(&controller->model);
    controller->window = tiku_workspace_open(controller->workspace,
        tiku_str("title.deskbar_prefs"), saved_frame(controller), draw,
        controller,
        NULL);
    if (controller->window != NULL) {
        tiku_window_set_size_controls(controller->window, 0, 0);
        tiku_window_set_handlers(controller->window, event, NULL, NULL);
    }
    if (controller->dirty != NULL) { *controller->dirty = 1; }
}

tiku_window_t *
tiku_trk_deskbar_prefs_window_handle(
    tiku_trk_deskbar_prefs_window_t *controller)
{
    return controller->window;
}

int
tiku_trk_deskbar_prefs_window_key(
    tiku_trk_deskbar_prefs_window_t *controller, const char *key)
{
    tiku_trk_db_dock_t dock;

    if (controller == NULL || controller->window == NULL ||
        !controller->window->open) {
        return 0;
    }
    dock = tiku_trk_deskbar_dock(controller->deskbar);
    if (!tiku_trk_deskbar_prefs_dock_key(key, &dock)) { return 0; }
    tiku_trk_deskbar_set_dock(controller->deskbar, dock);
    if (controller->dirty != NULL) { *controller->dirty = 1; }
    return 1;
}
