/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_fonts_window.c - the Fonts preferences window: the family list,
 * the size stepper, a sample of the face, and Defaults, Revert and Done,
 * laid out as grouped boxes over the shared dialog controls.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_font.h"
#include "tiku_list_paint.h"
#include "tiku_preferences_layout.h"
#include "tiku_strings.h"
#include "tiku_text_layout_paint.h"
#include "tiku_trk_deskbar_prefs.h"
#include "tiku_trk_fonts_window.h"
#include "tiku_ui.h"
#include <stdio.h>
#include <string.h>

enum { FAMILY = 1, MINUS, PLUS, DEFAULTS, REVERT, DONE };
typedef struct {
    tiku_rect_t family, list, size, minus, number, plus, preview, paper;
    tiku_rect_t help, defaults, revert, done;
} font_layout_t;

/**
 * @brief The panel's body height, from the face alone; the family list
 *        scrolls.
 */
static int body_height(void)
{ return 300 + 7 * tiku_font_plain()->height; }

/**
 * @brief Where every control sits in the content @p c, read by the drawing
 *        and the input alike at every face size.
 */
static font_layout_t layout(tiku_rect_t c)
{
    font_layout_t l;
    int fh = tiku_font_plain()->height, bh = tiku_ui_button_h();
    int top = c.y + fh + 34, left = 216;
    int bottom = c.y + body_height() - bh - 18;
    l.family = (tiku_rect_t){c.x + 18, top, left, bottom - top - 72};
    l.list = tiku_preferences_group_body(l.family);
    l.size = (tiku_rect_t){c.x + left + 34, top, c.w - left - 52, bh + fh + 42};
    tiku_rect_t inner = tiku_preferences_group_body(l.size);
    l.minus = (tiku_rect_t){inner.x, inner.y, 42, bh};
    l.plus = (tiku_rect_t){inner.x + inner.w - 42, inner.y, 42, bh};
    l.number = (tiku_rect_t){l.minus.x + 50, inner.y, inner.w - 100, bh};
    l.preview = (tiku_rect_t){l.size.x, l.size.y + l.size.h + 16,
        l.size.w, l.family.y + l.family.h - l.size.y - l.size.h - 16};
    l.paper = tiku_preferences_group_body(l.preview);
    l.help = (tiku_rect_t){c.x + 18, l.family.y + l.family.h + 14,
        c.w - 36, 2 * (fh + 3)};
    tiku_preferences_action_t leading = {tiku_str("button.defaults"), 112};
    tiku_preferences_action_t trailing[] = {
        {tiku_str("button.revert"), 100}, {tiku_str("prefs.fonts.done"), 88}
    };
    tiku_rect_t buttons[2];
    tiku_preferences_actions((tiku_rect_t){c.x + 18, bottom, c.w - 36, bh},
        &leading, trailing, 2, 12, &l.defaults, buttons);
    l.revert = buttons[0]; l.done = buttons[1];
    return l;
}

/** @brief The frame's height: the body, the title tab and the border. */
static int window_height(void)
{ return body_height() + tiku_window_tab_h() + TIKU_WINDOW_BORDER; }

/**
 * @brief Fit the window's height to the face, keeping its top-left corner
 *        in place.
 */
static void refit(tiku_trk_fonts_window_t *p)
{
    if (p->window && p->window->open &&
        p->window->frame.h != window_height()) {
        p->window->frame.h = window_height();
        if (p->dirty) *p->dirty = 1;
    }
}

/**
 * @brief Give the dialog its controls at their layout rectangles, each
 *        enabled when it can act; the drawing and the hit testing both read
 *        them.
 */
static void controls(tiku_trk_fonts_window_t *p, font_layout_t l)
{
    tiku_dialog_begin(&p->dialog, DONE, DONE);
    tiku_dialog_add(&p->dialog, FAMILY, l.list, TIKU_DIALOG_INPUT,
                    p->offer.count > 0);
    tiku_dialog_add(&p->dialog, MINUS, l.minus, TIKU_DIALOG_BUTTON,
        p->model.value.size > TIKU_TRK_FONT_MIN);
    tiku_dialog_add(&p->dialog, PLUS, l.plus, TIKU_DIALOG_BUTTON,
        p->model.value.size < TIKU_TRK_FONT_MAX);
    tiku_dialog_add(&p->dialog, DEFAULTS, l.defaults, TIKU_DIALOG_BUTTON,
        tiku_trk_fontprefs_defaultable(&p->model));
    tiku_dialog_add(&p->dialog, REVERT, l.revert, TIKU_DIALOG_BUTTON,
        tiku_trk_fontprefs_revertable(&p->model));
    tiku_dialog_add(&p->dialog, DONE, l.done, TIKU_DIALOG_BUTTON, 1);
    tiku_dialog_end(&p->dialog);
}

/**
 * @brief Save the font preferences, apply them through the Deskbar's
 *        settings, put a dropped-in family in force, and refit the window.
 */
static void
apply(tiku_trk_fonts_window_t *controller)
{
    if (controller->submit_save) {
        (void)controller->submit_save(controller->save_context);
        if (controller->dirty) *controller->dirty = 1;
        return;
    }
    tiku_trk_deskbar_prefs_t record;

    (void)tiku_trk_fontprefs_save(&controller->model, controller->store);
    (void)tiku_store_flush(controller->store);
    /* Applied through the Deskbar, which holds the settings everything
     * else is applied from; a stale copy there would put the old face
     * back on its next change. */
    tiku_trk_deskbar_prefs_init(&record);
    if (controller->store != NULL) {
        (void)tiku_trk_deskbar_prefs_load(&record, controller->store);
    } else {
        record.value.font_family = controller->model.value.family;
        record.value.font_size = controller->model.value.size;
    }
    tiku_trk_deskbar_apply_settings(controller->deskbar, &record.value);
    /* Last, because the Deskbar's apply puts a baked family in force and
     * a dropped-in family must override it. */
    tiku_trk_fontprefs_apply(&controller->model);
    refit(controller);
    if (controller->dirty != NULL) {
        *controller->dirty = 1;
    }
}

/** @brief The name of the family on @p row, for the list; "" past the end. */
static const char *family_name(void *ctx, int row)
{
    tiku_trk_fonts_window_t *p = ctx;
    return row >= 0 && row < p->offer.count ? p->offer.name[row] : "";
}

/** @brief Draw the buttons, each disabled, focused or pressed as the
 *         dialog says. */
static void buttons_draw(tiku_trk_fonts_window_t *p, tiku_surface_t *s)
{
    const char *labels[] = {"", "", "-", "+", tiku_str("button.defaults"),
        tiku_str("button.revert"), tiku_str("prefs.fonts.done")};
    for (int i = 0; i < p->dialog.count; i++) {
        const tiku_dialog_control_t *c = &p->dialog.control[i];
        if (c->id == FAMILY) continue;
        unsigned flags = c->enabled ? 0 : TIKU_S_DISABLED;
        if (tiku_dialog_focused(&p->dialog, c->id)) flags |= TIKU_S_FOCUS;
        if (tiku_dialog_pressed(&p->dialog, c->id)) flags |= TIKU_S_PRESSED;
        tiku_ui_button(s, c->rect, labels[c->id], flags);
    }
}

/**
 * @brief Draw the panel: the family list, the size stepper, a sample of the
 *        face, the help text and the buttons, refitting the window first.
 */
static void draw(tiku_window_t *window, tiku_surface_t *s,
                 tiku_rect_t content, void *ctx)
{
    tiku_trk_fonts_window_t *p = ctx;
    refit(p); content = tiku_window_content(window);
    font_layout_t l = layout(content);
    controls(p, l);
    tiku_fill(s, content, TIKU_C_PANEL);
    tiku_text_line_draw(s, (tiku_rect_t){content.x + 18, content.y + 8,
        content.w - 36, tiku_font_plain()->height + 12},
        TIKU_MEASURE_PLAIN, tiku_str("prefs.fonts.subtitle"), TIKU_C_TEXT);
    tiku_ui_group(s, l.family, tiku_str("prefs.fonts.family"));
    tiku_ui_group(s, l.size, tiku_str("prefs.fonts.size"));
    tiku_ui_group(s, l.preview, tiku_str("prefs.fonts.preview"));
    tiku_fill(s, l.list, TIKU_C_DOC);
    tiku_frame(s, l.list, tiku_tint(TIKU_C_PANEL, TIKU_DARKEN_2));
    tiku_list_set_count(&p->families, p->offer.count);
    tiku_list_draw(&p->families, s, l.list, family_name, p);
    if (tiku_dialog_focused(&p->dialog, FAMILY))
        tiku_frame(s, l.list, TIKU_C_SELECT);
    char number[24];
    snprintf(number, sizeof number, "%d px", p->model.value.size);
    tiku_text_centered(s, tiku_font_plain(), l.number, number, TIKU_C_TEXT);
    tiku_fill(s, l.paper, TIKU_C_DOC);
    tiku_frame(s, l.paper, tiku_tint(TIKU_C_PANEL, TIKU_DARKEN_2));
    tiku_rect_t title = {l.paper.x + 12, l.paper.y + 8, l.paper.w - 24,
                         tiku_font_bold()->height + 10};
    tiku_text_line_draw(s, title, TIKU_MEASURE_BOLD,
        tiku_str("prefs.fonts.sample_title"), TIKU_C_TEXT);
    tiku_text_block_draw(s, (tiku_rect_t){title.x, title.y + title.h + 8,
        title.w, l.paper.h - title.h - 28}, TIKU_MEASURE_PLAIN,
        tiku_str("prefs.fonts.sample_body"), TIKU_C_TEXT, 3);
    const char *status = p->save_status ? p->save_status(p->save_context) : "";
    tiku_text_block_draw(s, l.help, TIKU_MEASURE_PLAIN,
                         status && *status ? status
                                           : tiku_str("prefs.fonts.help"),
                         TIKU_C_TEXT, 3);
    buttons_draw(p, s);
}

/**
 * @brief Carry out a button: Done closes the window; the size steps,
 *        Defaults and Revert are applied, and the list then selects and
 *        reveals the chosen family.
 */
static void activate(tiku_trk_fonts_window_t *p, int command)
{
    int changed = 0;
    if (command == DONE) { tiku_trk_fonts_window_close(p); return; }
    if (command == MINUS || command == PLUS)
        changed = tiku_trk_fontprefs_step_size(&p->model, command == PLUS);
    if (command == DEFAULTS) {
        tiku_trk_fontprefs_defaults(&p->model); changed = 1;
    }
    if (command == REVERT) {
        tiku_trk_fontprefs_revert(&p->model); changed = 1;
    }
    if (changed) apply(p);
    tiku_list_select_only(&p->families,
        tiku_trk_fontprefs_chosen(&p->model, &p->offer));
    tiku_list_reveal(&p->families,
        layout(tiku_window_content(p->window)).list);
}

/**
 * @brief A key moves the focused family list, a button acts on a release
 *        over it, a press on the list picks a family, and the wheel scrolls
 *        the list.
 * @return 1 when taken.
 */
static int event(tiku_window_t *window, const tiku_event_t *ev, void *ctx)
{
    tiku_trk_fonts_window_t *p = ctx;
    font_layout_t l = layout(tiku_window_content(window));
    controls(p, l);
    /* Keys with Cmd or Option, Tab, Escape and Return do not reach the
     * list: Cmd-A would pick the first family and redraw the desktop in
     * it.  The dialog gives Return and Escape to Done. */
    if (p->dialog.focus == FAMILY && ev->type == TIKU_EVENT_KEY_DOWN &&
        !(ev->modifiers & (TIKU_MOD_CMD | TIKU_MOD_OPTION)) &&
        ev->key != TIKU_KEY_TAB && ev->key != TIKU_KEY_ESCAPE &&
        ev->key != TIKU_KEY_RETURN) {
        tiku_list_key(&p->families, ev->key, ev->modifiers, l.list);
        if (tiku_trk_fontprefs_choose(&p->model, &p->offer,
                                      p->families.cursor)) apply(p);
        if (p->dirty) *p->dirty = 1;
        return 1;
    }
    tiku_dialog_result_t result = tiku_dialog_event(&p->dialog, ev);
    if (result.activated != TIKU_DIALOG_NONE) activate(p, result.activated);
    if (ev->type == TIKU_EVENT_POINTER_DOWN && ev->button == 1) {
        int row = tiku_list_at(&p->families, l.list, ev->x, ev->y);
        if (row >= 0) {
            tiku_list_select_only(&p->families, row);
            if (tiku_trk_fontprefs_choose(&p->model, &p->offer, row)) apply(p);
            result.taken = 1;
        }
    }
    if (ev->type == TIKU_EVENT_WHEEL) {
        tiku_list_scroll_to(&p->families, p->families.top + ev->dy * 3, l.list);
        result.taken = 1;
    }
    if (result.taken && p->dirty) *p->dirty = 1;
    return result.taken;
}

void
tiku_trk_fonts_window_init(tiku_trk_fonts_window_t *controller,
                           tiku_workspace_t *workspace,
                           tiku_trk_deskbar_t *deskbar,
                           tiku_store_t *store, int screen_w,
                           int screen_h, int *dirty)
{
    memset(controller, 0, sizeof *controller);
    controller->workspace = workspace;
    controller->deskbar = deskbar;
    controller->store = store;
    controller->screen_w = screen_w;
    controller->screen_h = screen_h;
    controller->dirty = dirty;
    tiku_trk_fontprefs_init(&controller->model);
    tiku_dialog_init(&controller->dialog);
    tiku_list_init(&controller->families, 1);
}

void
tiku_trk_fonts_window_close(tiku_trk_fonts_window_t *controller)
{
    if (controller->window != NULL && controller->window->open) {
        tiku_workspace_close(controller->workspace, controller->window);
    }
    controller->window = NULL;
    tiku_dialog_init(&controller->dialog);
    if (controller->dirty != NULL) {
        *controller->dirty = 1;
    }
}

void
tiku_trk_fonts_window_destroy(tiku_trk_fonts_window_t *controller)
{
    tiku_trk_fonts_window_close(controller);
}

void
tiku_trk_fonts_window_open(tiku_trk_fonts_window_t *controller)
{
    tiku_rect_t frame;

    if (controller->window != NULL && controller->window->open) {
        tiku_workspace_activate(controller->workspace,
                                     controller->window);
        return;
    }
    /* Read afresh: the Deskbar's preferences also change the size, and a
     * stale model would put the old size back. */
    (void)tiku_trk_fontprefs_load(&controller->model, controller->store);
    tiku_trk_fontprefs_mark_opened(&controller->model);
    /* The fonts folder is read on every open, so a face dropped in while
     * the window was shut is offered. */
    (void)tiku_trk_fontprefs_offer(&controller->offer);

    tiku_dialog_init(&controller->dialog);
    tiku_list_init(&controller->families, 1);
    tiku_list_set_count(&controller->families, controller->offer.count);
    tiku_list_select_only(&controller->families,
        tiku_trk_fontprefs_chosen(&controller->model, &controller->offer));
    frame.w = TIKU_TRK_FONTS_W;
    frame.h = window_height();
    frame.x = (controller->screen_w - frame.w) / 2;
    frame.y = (controller->screen_h - frame.h) / 3;
    controller->window = tiku_workspace_open(controller->workspace,
        tiku_str("title.fonts"), frame, draw, controller, NULL);
    if (controller->window != NULL) {
        tiku_window_set_size_controls(controller->window, 0, 0);
        tiku_window_set_handlers(controller->window, event, NULL, NULL);
        tiku_list_reveal(&controller->families,
            layout(tiku_window_content(controller->window)).list);
    }
    if (controller->dirty != NULL) {
        *controller->dirty = 1;
    }
}

tiku_window_t *
tiku_trk_fonts_window_handle(tiku_trk_fonts_window_t *controller)
{
    return controller->window;
}
