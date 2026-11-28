/*
 * Desktop shell and file manager.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_desktop_events.c - what the desktop does with an event:
 * committed text, delivered to the focused window; a button down, the
 * pointer moving, a button up and a key, each handed to the responders; and
 * the work of a loop turn before the events are read and after.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_shell_product.h"
#include "tiku_trk_desktop_private.h"
#include "tiku_work.h"


/**
 * @brief Deliver committed text to the focused window, past the menus and
 *        the key chords: an embedded or remote application, the device
 *        preferences, or the desktop's own fields in turn.  Nothing is
 *        delivered while an alert or a menu is open.  Text that is not
 *        valid, or that a field refuses, puts a refusal on the status line;
 *        text no field takes is dropped.
 */
void desktop_text_input(desktop_t *app, const tiku_event_t *ev)
{
    tiku_window_t *w = tiku_workspace_focused(app->desk);
    struct embed_win *ew = embed_win_of_window(app, w);
    if (alert_model(app)->open || app->popup) {
        tiku_text_input_reset(&app->legacy_commit);
        return;
    }
    if (ew != NULL) {
        if (embed_deliver(ew, ev)) embed_stop(ew->slot);
    } else if (tiku_remote_owner(&app->remote, w) != NULL) {
        tiku_remote_event(&app->remote, w, ev);
#if TIKU_SHELL_BOARD_SERVICES

    } else if (w && w == desktop_deviceprefs_handle(app)) {
        (void)tiku_window_send(w, ev);
#endif

    } else {
        if (ev->button & TIKU_TEXT_BEGIN) app->legacy_target = w;
        if (app->legacy_target != w) tiku_text_input_reset(&app->legacy_commit);
        int state = tiku_text_input_event(&app->legacy_commit, ev);
        int result = state == TIKU_TEXT_REJECTED ? -1 : 0;
        if (state == TIKU_TEXT_COMPLETE) {
            const char *text = app->legacy_commit.text;
            result = tiku_trk_info_windows_text(&app->info_windows, text);
            if (!result)
                result = tiku_trk_panel_window_text(&app->file_panel, text);
            if (!result)
                result = tiku_trk_find_window_text(&app->find_window, text);
            if (!result)
                result = tiku_trk_select_window_text(&app->select_window, text);
            tiku_trk_described_t *panel = panel_of_window(app, w);
            if (!result && panel && tiku_form_editing(&panel->form) >= 0)
                result = tiku_form_text(&panel->form, text) ? 1 : -1;
            if (!result && app->finding) {
                result = tiku_text_input_append(app->find_buf,
                    sizeof app->find_buf, text) ? 1 : -1;
                app->find_len = (int)strlen(app->find_buf);
            }
            folder_t *fl = folder_of(app, w);
            if (!result && (!w || fl))
                result = tiku_trk_folder_edit_text(&app->folder_edit, fl, text);
        }
        if (result < 0)
            snprintf(app->status, sizeof app->status,
                     "Text input is invalid or does not fit this field.");
    }
    desktop_damage_all(app);
}

/** @brief Hand one event to the responders and damage what they changed. */
static void
deliver(desktop_t *app, const tiku_event_t *ev)
{
    tiku_rect_t taken;

    (void)tiku_trk_responders_deliver(&app->responders, ev, &taken);
    desktop_take_damage(app, taken);
#if TIKU_SHELL_DAMAGE_CHECK
    app->damage_check_input = 1;
    app->damage_check_event = *ev;
#endif
}

/* the touch calibration page */

void
desktop_calibrate_target(const desktop_t *app, int i, int *x, int *y)
{
    int w = app->surf->w, h = app->surf->h;
    int col = i % 3, row = i / 3;

    *x = (col == 0) ? w / 8 : (col == 1) ? w / 2 : w - w / 8;
    *y = (row == 0) ? h / 8 : (row == 1) ? h / 2 : h - h / 8;
}

tiku_rect_t
desktop_calibrate_cancel_box(const desktop_t *app)
{
    return (tiku_rect_t){ app->surf->w / 2 - 70, app->surf->h * 70 / 100,
                          140, 44 };
}

tiku_rect_t
desktop_calibrate_keep_box(const desktop_t *app)
{
    return (tiku_rect_t){ app->surf->w / 2 - 120, app->surf->h * 52 / 100,
                          240, 90 };
}

/** @brief Whether (@p x, @p y) lies in @p r. */
static int
calibrate_in(tiku_rect_t r, int x, int y)
{
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

/** @brief Leave the page with one closing line, or at once with none. */
static void
calibrate_end(desktop_t *app, const char *note)
{
    desktop_calibrate_t *c = &app->calibrate;

    c->note = note;
    c->phase = (note != NULL) ? DESKTOP_CALIBRATE_NOTE : DESKTOP_CALIBRATE_OFF;
    c->until = tiku_trk_now() + DESKTOP_CALIBRATE_NOTE_US;
    desktop_damage_all(app);
}

void
desktop_calibrate_begin(desktop_t *app)
{
    desktop_calibrate_t *c = &app->calibrate;

    if (c->map.fit == NULL || c->phase != DESKTOP_CALIBRATE_OFF) {
        return;
    }
    /* The lens does not open over the page, and one left open would lie
     * over it. */
    app->lens.shown = app->lens.held = app->lens.picking = 0;
    c->phase = DESKTOP_CALIBRATE_ASK;
    c->step = 0;
    c->note = NULL;
    desktop_damage_all(app);
}

/**
 * @brief The taps are in: fit a line on each axis and hand the product the
 *        positions it reports at the screen's edges.  A fit that fails, or
 *        one the product refuses, changes nothing.
 */
static void
calibrate_fit(desktop_t *app)
{
    desktop_calibrate_t *c = &app->calibrate;
    int tx[DESKTOP_CALIBRATE_TAPS], ty[DESKTOP_CALIBRATE_TAPS];
    int x0, x1, y0, y1, i;

    for (i = 0; i < DESKTOP_CALIBRATE_TAPS; i++) {
        desktop_calibrate_target(app, i, &tx[i], &ty[i]);
    }
    if (tiku_trk_pointer_fit(tx, c->x, DESKTOP_CALIBRATE_TAPS,
                             app->surf->w - 1, &x0, &x1) != 0 ||
        tiku_trk_pointer_fit(ty, c->y, DESKTOP_CALIBRATE_TAPS,
                             app->surf->h - 1, &y0, &y1) != 0 ||
        c->map.fit(x0, x1, y0, y1) != 0) {
        calibrate_end(app, "The taps did not fit a calibration. "
                           "Nothing was changed.");
        return;
    }
    c->phase = DESKTOP_CALIBRATE_CONFIRM;
    c->until = tiku_trk_now() + DESKTOP_CALIBRATE_KEEP_US;
    c->left = -1;
    desktop_damage_all(app);
}

/** @brief The page takes every press while it shows. */
static int
calibrate_hit(void *self, int x, int y)
{
    desktop_t *app = self;

    (void)x; (void)y;
    return app->calibrate.phase != DESKTOP_CALIBRATE_OFF;
}

/**
 * @brief A press on the page: a tap for the target asked for, Cancel, Keep,
 *        or the end of a closing line.
 */
static int
calibrate_press(void *self, const tiku_event_t *ev, tiku_rect_t *damage)
{
    desktop_t *app = self;
    desktop_calibrate_t *c = &app->calibrate;

    *damage = tiku_trk_damage_none();
    switch (c->phase) {
    case DESKTOP_CALIBRATE_ASK:
        if (calibrate_in(desktop_calibrate_cancel_box(app), ev->x, ev->y)) {
            calibrate_end(app, NULL);
            break;
        }
        c->x[c->step] = ev->x;
        c->y[c->step] = ev->y;
        c->step++;
        if (c->step == DESKTOP_CALIBRATE_TAPS) {
            calibrate_fit(app);
        } else {
            desktop_damage_all(app);
        }
        break;
    case DESKTOP_CALIBRATE_CONFIRM:
        /* Only the Keep box answers: a tap elsewhere may be the new map
         * landing wrong, and the wait ends by itself. */
        if (calibrate_in(desktop_calibrate_keep_box(app), ev->x, ev->y)) {
            calibrate_end(app, (c->map.keep() == 0)
                ? "The touch calibration was saved."
                : "The calibration is in use but could not be saved.");
        }
        break;
    case DESKTOP_CALIBRATE_NOTE:
        calibrate_end(app, NULL);
        break;
    default:
        return 0;
    }
    return 1;
}

/** @brief Moves and releases over the page go nowhere. */
static int
calibrate_swallow(void *self, const tiku_event_t *ev, tiku_rect_t *damage)
{
    desktop_t *app = self;

    (void)ev;
    *damage = tiku_trk_damage_none();
    return app->calibrate.phase != DESKTOP_CALIBRATE_OFF;
}

/** @brief Escape leaves the page, putting the stored map back when a
 *         fitted one waits; the page takes every other key too. */
static int
calibrate_key(void *self, const tiku_event_t *ev, tiku_rect_t *damage)
{
    desktop_t *app = self;
    desktop_calibrate_t *c = &app->calibrate;

    *damage = tiku_trk_damage_none();
    if (c->phase == DESKTOP_CALIBRATE_OFF) {
        return 0;
    }
    if (ev->type == TIKU_EVENT_KEY_DOWN && ev->key == TIKU_KEY_ESCAPE) {
        if (c->phase == DESKTOP_CALIBRATE_CONFIRM) {
            c->map.revert();
        }
        calibrate_end(app, NULL);
    }
    return 1;
}

/** @brief The clock: a fitted map not kept in time gives way to the stored
 *         one, the seconds left are shown again as they change, and a
 *         closing line ends. */
static int
calibrate_tick(void *self, int64_t now, tiku_rect_t *damage)
{
    desktop_t *app = self;
    desktop_calibrate_t *c = &app->calibrate;

    *damage = tiku_trk_damage_none();
    if (c->phase == DESKTOP_CALIBRATE_CONFIRM) {
        int left = (int)((c->until - now + 999999) / 1000000);

        if (now >= c->until) {
            c->map.revert();
            calibrate_end(app, "Not kept: the earlier calibration is back.");
            return 1;
        }
        if (left != c->left) {
            c->left = left;
            desktop_damage_all(app);
            return 1;
        }
    } else if (c->phase == DESKTOP_CALIBRATE_NOTE && now >= c->until) {
        calibrate_end(app, NULL);
        return 1;
    }
    return 0;
}

void
desktop_calibrate_attach(desktop_t *app)
{
    app->calibrate_responder = (tiku_trk_responder_t){
        .name = "calibrate", .self = app, .hit = calibrate_hit,
        .press = calibrate_press, .move = calibrate_swallow,
        .release = calibrate_swallow, .key = calibrate_key,
        .tick = calibrate_tick
    };
    (void)tiku_trk_responders_push(&app->responders,
                                   &app->calibrate_responder);
}

/** @brief Whether (@p x, @p y) lies in @p r. */
static int
lens_in(tiku_rect_t r, int x, int y)
{
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

/** @brief @p v held to @p lo .. @p hi, or @p lo when the range is empty. */
static int
lens_clamp(int v, int lo, int hi)
{
    return (v > hi) ? ((hi > lo) ? hi : lo) : ((v < lo) ? lo : v);
}

/**
 * @brief Whether a press at (@p x, @p y) opens the lens.
 * @param menu Receives the menu the press is on, or an empty rectangle.
 * @note While a menu shows, or a popup of the Deskbar, only a press on it
 *       does: its rows are the small controls then, and a press off it
 *       closes it as without the lens.  Otherwise a press does that is on
 *       or near a window's close or zoom box (under it, or at one of the
 *       points up to DESKTOP_LENS_NEAR pixels across and down from it, in
 *       steps of half that), on the menu bar along the top of the screen,
 *       on the visible Deskbar outside its leaf button and theme switch,
 *       or, in the folder window
 *       under the press, on its own menu bar or an end of its scroll bar.
 */
static int
lens_wanted(desktop_t *app, int x, int y, tiku_rect_t *menu)
{
    tiku_window_t *w = NULL;
    tiku_hit_t under;
    int dx, dy;

    *menu = (tiku_rect_t){ 0, 0, 0, 0 };
    if (app->popup != NULL) {
        if (app->popup_sub != NULL && lens_in(app->popup_sub_frame, x, y)) {
            *menu = app->popup_sub_frame;
        } else if (lens_in(app->popup_frame, x, y)) {
            *menu = app->popup_frame;
        }
        return menu->w > 0;
    }
    if (app->deskbar != NULL && tiku_trk_deskbar_menu_open(app->deskbar)) {
        return tiku_trk_deskbar_popup_at(app->deskbar, x, y, menu);
    }
    under = tiku_workspace_hit(app->desk, x, y, &w);
    if (app->deskbar != NULL &&
        lens_in(tiku_trk_deskbar_frame(app->deskbar), x, y) &&
        (under == TIKU_HIT_NONE || tiku_trk_deskbar_raised(app->deskbar))) {
        return !lens_in(tiku_trk_deskbar_leaf_button_rect(app->deskbar), x, y) &&
               !lens_in(tiku_inset(tiku_trk_deskbar_switch_rect(app->deskbar), -2), x, y);
    }
    for (dy = -DESKTOP_LENS_NEAR; dy <= DESKTOP_LENS_NEAR;
         dy += DESKTOP_LENS_NEAR / 2) {
        for (dx = -DESKTOP_LENS_NEAR; dx <= DESKTOP_LENS_NEAR;
             dx += DESKTOP_LENS_NEAR / 2) {
            tiku_window_t *near = NULL;
            tiku_hit_t hit = tiku_workspace_hit(app->desk, x + dx, y + dy,
                                                &near);

            if (hit == TIKU_HIT_CLOSE || hit == TIKU_HIT_ZOOM) {
                return 1;
            }
        }
    }
    if (lens_in(global_bar_rect(app), x, y)) {
        return 1;
    }
    if (under == TIKU_HIT_CONTENT && w != NULL) {
        folder_t *fl = folder_of(app, w);

        if (fl != NULL) {
            tiku_rect_t sb = tiku_trk_folder_window_scrollbar(fl);

            if (lens_in(tiku_trk_folder_window_bar(fl), x, y)) {
                return 1;
            }
            if (lens_in(sb, x, y) &&
                (y < sb.y + sb.w || y >= sb.y + sb.h - sb.w)) {
                return 1;
            }
        }
    }
    return 0;
}

/** @brief Whether (@p x, @p y) is within @p reach pixels of (@p ax, @p ay)
 *         on both axes. */
static int
lens_within(int x, int y, int ax, int ay, int reach)
{
    return x - ax <= reach && ax - x <= reach &&
           y - ay <= reach && ay - y <= reach;
}

/** @brief The part of the lens that shows the magnified area. */
static tiku_rect_t
lens_view(const desktop_lens_t *l)
{
    return tiku_inset(l->box, 2);
}

/**
 * @brief Aim the pick at the screen point under (@p x, @p y) in the lens's
 *        view.
 * @return 1 when (@p x, @p y) is in the view, else 0 and the aim stays.
 */
static int
lens_aim(desktop_t *app, int x, int y)
{
    desktop_lens_t *l = &app->lens;
    tiku_rect_t in = lens_view(l);

    if (!lens_in(in, x, y)) {
        return 0;
    }
    l->ax = l->area.x + (x - in.x) / l->scale;
    l->ay = l->area.y + (y - in.y) / l->scale;
    return 1;
}

/** @brief Take the lens off the screen. */
static void
lens_close(desktop_t *app)
{
    desktop_lens_t *l = &app->lens;

    if (l->shown) {
        desktop_damage(app, l->box);
    }
    l->shown = l->picking = 0;
}

/**
 * @brief A press while the touch lens is used and the calibration page is
 *        not showing.
 * @return 1 when the lens took the press, which then reaches no surface.
 * @note With the lens open: a press in its view is a pick; a press
 *       within DESKTOP_LENS_AGAIN pixels of the one that opened the lens,
 *       a second tap on the control, changes nothing; any other closes
 *       the lens.  With the lens closed: a press on the place the lens
 *       had, within DESKTOP_LENS_GUARD_US of its click, is dropped, so a
 *       second tap on the lens reaches nothing under it; a press
 *       lens_wanted() names opens the lens, with the area around the press
 *       as the frame has it, magnified above the finger, or below where
 *       there is no room.  On a menu the lens magnifies less and the
 *       area starts at the menu's left edge, where its rows' names begin.
 */
static int
lens_open(desktop_t *app, const tiku_event_t *ev)
{
    desktop_lens_t *l = &app->lens;
    tiku_surface_t *s = app->surf;
    int bw = DESKTOP_LENS_VIEW_W + 4;
    int bh = DESKTOP_LENS_VIEW_H + 4;
    tiku_rect_t menu;
    int i, j, aw, ah;

    if (!l->used || l->passing || s == NULL || alert_model(app)->open ||
        app->calibrate.phase != DESKTOP_CALIBRATE_OFF) {
        return 0;
    }
    if (l->shown) {
        l->held = 1;
        l->opening = 0;
        l->press = *ev;
        l->picking = lens_aim(app, ev->x, ev->y);
        if (!l->picking &&
            !lens_within(ev->x, ev->y, l->ox, l->oy, DESKTOP_LENS_AGAIN)) {
            lens_close(app);
        }
        return 1;
    }
    if (l->clicked_at != 0 &&
        ev->time_us - l->clicked_at < DESKTOP_LENS_GUARD_US &&
        lens_in(l->box, ev->x, ev->y)) {
        l->held = 1;
        l->opening = l->picking = 0;
        return 1;
    }
    if (!lens_wanted(app, ev->x, ev->y, &menu)) {
        return 0;
    }
    l->scale = (menu.w > 0) ? DESKTOP_LENS_MENU_SCALE : DESKTOP_LENS_SCALE;
    aw = DESKTOP_LENS_VIEW_W / l->scale;
    ah = DESKTOP_LENS_VIEW_H / l->scale;
    l->area = (tiku_rect_t){
        lens_clamp((menu.w > 0) ? menu.x : ev->x - aw / 2, 0, s->w - aw),
        lens_clamp(ev->y - ah / 2, 0, s->h - ah), aw, ah };
    for (j = 0; j < ah; j++) {
        for (i = 0; i < aw; i++) {
            l->px[j * aw + i] = tiku_peek(s, l->area.x + i, l->area.y + j);
        }
    }
    l->box = (tiku_rect_t){ lens_clamp(ev->x - bw / 2, 0, s->w - bw),
                            ev->y - 28 - bh, bw, bh };
    if (l->box.y < 0) {
        l->box.y = lens_clamp(ev->y + 28, 0, s->h - bh);
    }
    l->held = l->shown = l->opening = 1;
    l->picking = 0;
    l->clicked_at = 0;
    l->ox = l->ax = ev->x;
    l->oy = l->ay = ev->y;
    l->press = *ev;
    desktop_damage(app, l->box);
    return 1;
}

/**
 * @brief A move during a press the lens took.
 * @return 1 when the lens took the move.
 * @note The press that opened the lens, once it has travelled more than
 *       DESKTOP_LENS_SLOP pixels, is a drag: the lens closes, the press
 *       goes to the surfaces as it came, and this move and the rest of the
 *       drag follow it there.  A pick's aim follows the finger in the
 *       view, and leaving the view drops the pick.
 */
static int
lens_move(desktop_t *app, const tiku_event_t *ev, desktop_loop_t *lp)
{
    desktop_lens_t *l = &app->lens;

    if (!l->held || l->passing) {
        return 0;
    }
    if (l->opening) {
        tiku_event_t press = l->press;

        if (lens_within(ev->x, ev->y, l->ox, l->oy, DESKTOP_LENS_SLOP)) {
            return 1;
        }
        l->held = l->opening = 0;
        lens_close(app);
        l->passing = 1;
        desktop_pointer_down(app, &press, lp);
        l->passing = 0;
        return 0;
    }
    if (l->picking && !lens_aim(app, ev->x, ev->y)) {
        l->picking = 0;
    }
    return 1;
}

/**
 * @brief The release of a press the lens took: after a pick, a click at
 *        its aim and the lens closes; after the press that opened the
 *        lens, it stays open for the pick.
 * @return 1 when the lens took the release.
 */
static int
lens_release(desktop_t *app, const tiku_event_t *ev, desktop_loop_t *lp)
{
    desktop_lens_t *l = &app->lens;
    tiku_event_t down, up;

    if (!l->held || l->passing) {
        return 0;
    }
    l->held = l->opening = 0;
    if (!l->picking) {
        return 1;
    }
    lens_close(app);
    l->clicked_at = ev->time_us != 0 ? ev->time_us : 1;
    down = l->press;
    up = *ev;
    down.x = up.x = app->ptr_x = l->ax;
    down.y = up.y = app->ptr_y = l->ay;
    down.time_us = up.time_us;
    l->passing = 1;
    desktop_pointer_down(app, &down, lp);
    desktop_pointer_up(app, &up, lp);
    l->passing = 0;
    return 1;
}

/** @brief A button went down: the touch lens takes a press on a small
 *         control, every press while it is open and one on its place
 *         just after its click; else the surface under the pointer
 *         answers. */
void
desktop_pointer_down(desktop_t *app, const tiku_event_t *ev, desktop_loop_t *lp)
{
    (void)lp;
    if (lens_open(app, ev)) {
        return;
    }
    deliver(app, ev);
}

/**
 * @brief The pointer moved: record where it is, choose the cursor's shape
 *        for what is under it, hand the move to the responders, show or
 *        hide a device tray cell's tip, and damage where a dragged icon was
 *        and is.  During a press the touch lens took, the lens answers
 *        instead, until that press proves a drag.
 */
void
desktop_pointer_move(desktop_t *app, const tiku_event_t *ev, desktop_loop_t *lp)
{
    int was_x = app->ptr_x, was_y = app->ptr_y;
    /* The drag image as drawn before the responders move the drag. */
    int dragging = tiku_trk_drag_active(&app->drag);
    tiku_rect_t drag_was = desktop_drag_box(app->drag.x, app->drag.y);

    if (lens_move(app, ev, lp)) {
        return;
    }
    app->ptr_x = ev->x;
    app->ptr_y = ev->y;
    desktop_pointer_says(app, ev->x, ev->y);
    deliver(app, ev);
#if TIKU_SHELL_BOARD_SERVICES

    desktop_devtray_hover(app, was_x, was_y);
#endif

    if (app->qdrag_on) {
        /* The dragged query icon: damage its old and new boxes only. */
        desktop_damage(app, tiku_rect_union(
            (tiku_rect_t){ was_x - 16, was_y - 16, 32, 32 },
            (tiku_rect_t){ ev->x - 16, ev->y - 16, 32, 32 }));
    }
    if (tiku_trk_drag_active(&app->drag)) {
        /* The drag image follows the drag: its old and new boxes, or only
         * the new one when this move started the drag. */
        tiku_rect_t drag_now = desktop_drag_box(app->drag.x, app->drag.y);

        if (!dragging) {
            desktop_damage(app, drag_now);
        } else if (drag_now.x != drag_was.x || drag_now.y != drag_was.y) {
            desktop_damage(app, tiku_rect_union(drag_was, drag_now));
        }
    }
}

/** @brief A button came up: the touch lens answers for a press it took,
 *         else the holder of the capture answers first. */
void
desktop_pointer_up(desktop_t *app, const tiku_event_t *ev, desktop_loop_t *lp)
{
    if (lens_release(app, ev, lp)) {
        return;
    }
    deliver(app, ev);
    /* A resize may have ended: choose the pointer's shape again. */
    desktop_pointer_says(app, ev->x, ev->y);
}

/**
 * @brief A key went down or up: the surfaces answer in their order, the
 *        topmost first.
 */
void
desktop_key_down(desktop_t *app, const tiku_event_t *ev, desktop_loop_t *lp)
{
    (void)lp;
    deliver(app, ev);
}

/*
 * The most a scripted turn waits, in microseconds, for a child the desktop
 * started to connect; the wait ends when it connects.
 */
#define SCRIPT_CHILD_SLICE_US 20000

/**
 * @brief What a turn does before it reads events: wait for a scripted
 * child to connect, write a report SIGUSR2 asked for, collect finished
 * worker jobs, reload the settings file and run the scripting transport's
 * commands when they change, poll the remote sessions and the conductor
 * channel, bring the sessions' windows, clipboard, board names and file
 * picks up to date, apply the settings to the windows, and reload the
 * add-ons when their files change.
 */
void desktop_turn_begin(desktop_t *app, const desktop_options_t *o,
                        desktop_loop_t *lp)
{
    (void)o;
    /*
     * A script's wait counts turns of this loop, and a child the desktop
     * started advances in another process.  While a script waits and
     * fewer sessions have connected than children are running, the turn
     * waits up to SCRIPT_CHILD_SLICE_US for a connection.  A connected
     * session counts whether or not it has opened a window.  Nothing is
     * read here: the poll below takes what arrived.
     */
    if (lp != NULL && lp->sc != NULL && tiku_trk_script_waiting(lp->sc) &&
        tiku_trk_launch_running() > tiku_remote_sessions(&app->remote)) {
        (void)tiku_remote_wait(&app->remote, SCRIPT_CHILD_SLICE_US);
    }
    desktop_report_poll(app);
    /* Finished worker jobs are collected before the events are read, so
     * this turn's events are handled with their results applied. */
    (void)tiku_work_collect();
    desktop_save_theme(app);
    tiku_trk_catalog_settings_poll(app->catalog_settings);
    {
        struct timespec now;
        if (!clock_gettime(CLOCK_MONOTONIC, &now)) {
            desktop_resources_poll(app, (uint64_t)now.tv_sec * 1000 +
                                            (uint64_t)now.tv_nsec / 1000000);
            tiku_trk_desktop_settings_poll(app->desktop_settings,
                (uint64_t)now.tv_sec * 1000 + (uint64_t)now.tv_nsec / 1000000);
            tiku_trk_record_settings_poll(app->record_settings,
                (uint64_t)now.tv_sec * 1000 + (uint64_t)now.tv_nsec / 1000000);
            tiku_trk_settings_poll(app->settings,
                (uint64_t)now.tv_sec * 1000 + (uint64_t)now.tv_nsec / 1000000);
        }
    }
    {   /* The scripting transport (PVK-052): when
         * ~/.config/tracker/command changes, its lines run as
         * scripting commands.  The file found at the first turn is the
         * baseline and does not run. */
        static int64_t command_stamp;
        static int command_primed;
        struct stat cst;
        char cpath[TIKU_PATH_MAX];
        const char *chome = getenv("HOME");

        snprintf(cpath, sizeof cpath, TIKU_SHELL_COMMAND_PATH,
                 (chome != NULL && chome[0] != '\0') ? chome : "");
        if (stat(cpath, &cst) == 0) {
            /* Compared to the nanosecond, so two commands written
             * within one second both run. */
            int64_t stamp = (int64_t)cst.st_mtim.tv_sec * 1000000000 +
                            cst.st_mtim.tv_nsec;

            if (!command_primed) {
                command_stamp = stamp;
            } else if (stamp != command_stamp) {
                command_stamp = stamp;
                script_command_file(app, cpath);
            }
        }
        command_primed = 1;
    }
    if (tiku_remote_poll(&app->remote)) {
        desktop_damage_all(app);
    }
    if (app->conduct_on) {
        (void)tiku_conduct_poll(&app->conduct);
    }
    remote_reconcile(app);
    remote_hear(app);
    clip_mirror(app);
#if TIKU_SHELL_BOARD_SERVICES

    /* Tell the sessions the board names and the current device when
     * they change or a session joins. */
    desktop_board_names_tell(app);
#endif

    pick_sweep(app);
    remote_sweep(app);
    tiku_trk_window_resources_poll(app->window_resources);
    {
        struct timespec now;
        if (!clock_gettime(CLOCK_MONOTONIC, &now))
            desktop_lifecycle_vfs_poll(app,
                                       (uint64_t)now.tv_sec * 1000 +
                                           (uint64_t)now.tv_nsec / 1000000);
        if (!clock_gettime(CLOCK_MONOTONIC, &now))
            desktop_host_vfs_poll(app, (uint64_t)now.tv_sec * 1000 +
                                           (uint64_t)now.tv_nsec / 1000000);
    }
    sync_folder_preferences(app);
    sync_addons(app, 0);
}

/**
 * @brief What a turn does after the events: timers, hovers, tracking, the
 * hints and thumbnails, the -shot frame, the frame itself, the deadlines,
 * and the nap until the next task is due.
 */
void
desktop_turn_end(desktop_t *app, const desktop_options_t *o, desktop_loop_t *lp)
{
    tiku_window_t *focused = tiku_workspace_focused(app->desk);
    if (app->input_focus != focused || alert_model(app)->open) {
        tiku_host_cancel_composition(app->host);
#if TIKU_SHELL_REMOTE_SERVICES

        tiku_host_text_rect(app->host, (tiku_rect_t){0, 0, 1, 1});
#endif

        tiku_text_input_reset(&app->legacy_commit);
        app->input_focus = focused;
    }
    tiku_event_t deactivate = {.type = TIKU_EVENT_DEACTIVATED};
    if (app->prefs_track &&
        (app->prefs_track != focused || alert_model(app)->open)) {
        (void)tiku_window_send(app->prefs_track, &deactivate);
        app->prefs_track = NULL;
#if TIKU_SHELL_BOARD_SERVICES

    }
    if (desktop_deviceprefs_popup(app) &&
        (focused != desktop_deviceprefs_handle(app) ||
         alert_model(app)->open)) {
        (void)tiku_window_send(desktop_deviceprefs_handle(app), &deactivate);
#endif

    }
    desktop_spring_tick(app);
    {
        tiku_rect_t ticked;

        (void)tiku_trk_responders_tick(&app->responders, tiku_trk_now(),
                                       &ticked);
        desktop_take_damage(app, ticked);
    }
    desktop_idle_work(app);
    desktop_press_hold(app);
    desktop_rename_pulse(app);
    desktop_focus_paint(app);

    desktop_device_pump(app);
    desktop_sweep(app, lp);
    desktop_notice_pump(app, lp);
    int dirty_before_tick = app->dirty;
    desktop_builtins_turn(app);
    int builtin_frame = !dirty_before_tick && app->dirty;
    desktop_keep_window_tabs_visible(app);
    if (o->shot != NULL && lp->shot_at > 0 && tiku_trk_now() >= lp->shot_at) {
        desktop_damage_all(app);
    }
        if (app->shot_pending) {
        /* Redraw without the menu, then capture. */
        desktop_damage_all(app);
    }
    if (app->dirty && (lp->sc ||
        tiku_trk_now()-app->last_present >= 16667)) {
        draw_all(app);
        int64_t before_present = tiku_trk_now();
        blit(app);
        app->present_us += tiku_trk_now()-before_present;
        /* Frame start, not frame end: drawing time is inside the budget. */
        app->last_present = before_present -
            app->frame_samples[(app->frame_sample_count-1) % 128];
        if (o->shot != NULL && lp->shot_at > 0 &&
            tiku_trk_now() >= lp->shot_at) {
            (void)tiku_surface_png(app->surf, o->shot);
            printf("wrote %s\n", o->shot);
            fflush(stdout);
            lp->shot_at = 0;
        }
        desktop_screenshot_tick(app);
    }
#if TIKU_SHELL_DAMAGE_CHECK
    if (app->damage_check_input && !app->dirty) {
        desktop_damage_check_idle(app);
    }
    if (!app->dirty) {
        app->damage_check_input = 0;
    }
#endif
    desktop_progress_tick(app);
    if (o->deadline > 0 && tiku_trk_now() > o->deadline) {
        app->running = 0;
    }
    if (app->closing &&
        tiku_trk_splash_closed(&app->splash, tiku_trk_now())) {
        /* The closing screen is complete: the loop ends, and the
         * teardown shows its stages on that screen. */
        app->running = 0;
    }
    /* Under a script the loop still sleeps between turns, so timed work
     * (the notice poll, an armed rename, the progress bar) advances. */
    if (app->loop != NULL) {
        /* A task that changes the screen damages it itself; running a
         * task does not repaint. */
        (void)tiku_trk_loop_pulse(app->loop);
        if (app->sbar_fl != NULL && app->sbar_fl->view != NULL) {
            /* A held arrow keeps stepping (NeXT UIG ch3). */
            int rep = tiku_scroll_arrow_repeat(&app->sbar_fl->sbar);

            if (rep != 0) {
                tiku_trk_poseview_scroll_to(app->sbar_fl->view,
                    tiku_trk_poseview_scroll(app->sbar_fl->view) +
                        rep * tiku_scroll_line(&app->sbar_fl->sbar));
                desktop_damage_all(app);
            }
        }
    }
    {
        /* Sleep until the next task is due or for 10 ms (20 ms under a
         * script), whichever is sooner (AW-086); without a script, also
         * no later than the next frame is due, and not while injected
         * events wait. */
        int64_t nap = (lp->sc != NULL) ? 20000 : 10000;
        int64_t due = (app->loop != NULL)
                          ? tiku_trk_loop_next_due(app->loop) : -1;

        if (due >= 0 && due < nap) {
            nap = (due < 1000) ? 1000 : due;
        }
        if (lp->sc) usleep((useconds_t)nap);
        else {
            /* A built-in's next animation tick runs on the next turn.
             * Count the time spent presenting its frame toward that deadline. */
            if (builtin_frame || app->dirty) {
                int64_t frame_due = app->last_present+16667-tiku_trk_now();
                if (frame_due < nap) nap = frame_due;
            }
            if (nap > 0 && app->cq_head == app->cq_tail)
                tiku_host_wait(app->host, nap);
        }
    }
}
