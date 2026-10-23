/*
 * Desktop shell and file manager.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_desktop_surfaces.c - the overlays as responders: the force-quit
 * picker, the alert and progress overlay, the popup menus with the
 * navigator inside them, the global menu bar's strip, the spring
 * navigator's tracking and the Info window's floating field pop-up; and
 * the order of the responder list.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_shell_product.h"
#include "tiku_trk_desktop_private.h"

/* the force-quit picker: modal while it picks */

/** @brief Every point is the picker's while it picks. */
static int
forcequit_hit(void *self, int x, int y)
{
    (void)x; (void)y;
    return desktop_forcequit_picking(self);
}

/** @brief Take every event while picking, and at any time Cmd-Option-K,
 *         which opens the Force Quit window. */
static int
forcequit_take(void *self, const tiku_event_t *ev, tiku_rect_t *damage)
{
    desktop_t *app = self;

    *damage = tiku_trk_damage_none();
    if (!desktop_forcequit_input(app, ev)) {
        return 0;
    }
    if (ev->type == TIKU_EVENT_KEY_DOWN) {
        /* A key taken here (Cmd-Option-K, or any key while picking)
         * closes every open menu, so none takes the clicks meant for the
         * Force Quit window. */
        tiku_trk_deskbar_close_menu(app->deskbar);
        app->popup = app->popup_sub = NULL;
        app->popup_fl = app->bar_fl = NULL;
        app->popup_path[0] = '\0';
        app->nav_on = 0;
        app->bar_open = app->gbar_open = -1;
        app->gbar.open = app->menus.bar.open = -1;
    }
    *damage = tiku_trk_damage_all();
    return 1;
}

/* the alert and the progress panel: over everything when they show */

/** @brief Every point: an alert or the progress window, when it shows, is
 *         modal over the whole screen. */
static int
status_hit(void *self, int x, int y)
{
    (void)self; (void)x; (void)y;
    return 1;
}

/** @brief A press an alert or a progress control consumes. */
static int
status_press(void *self, const tiku_event_t *ev, tiku_rect_t *damage)
{
    desktop_t *app = self;

    *damage = tiku_trk_damage_none();
    return tiku_trk_status_overlay_pointer(&app->status_overlay, ev->x,
                                           ev->y);
}

/** @brief The alert's button shows pressed under the pointer and raised
 *         off it. */
static int
status_move(void *self, const tiku_event_t *ev, tiku_rect_t *damage)
{
    desktop_t *app = self;

    *damage = tiku_trk_damage_none();
    return tiku_trk_status_overlay_pointer_move(&app->status_overlay, ev->x,
                                                ev->y);
}

/** @brief On the alert's button the release answers; elsewhere it is
 *         taken back. */
static int
status_release(void *self, const tiku_event_t *ev, tiku_rect_t *damage)
{
    desktop_t *app = self;

    *damage = tiku_trk_damage_none();
    if (tiku_trk_status_overlay_pointer_up(&app->status_overlay, ev->x,
                                           ev->y)) {
        *damage = tiku_trk_damage_all();
        return 1;
    }
    return 0;
}

/** @brief A key while a modal alert owns the keyboard. */
static int
status_key(void *self, const tiku_event_t *ev, tiku_rect_t *damage)
{
    desktop_t *app = self;

    *damage = tiku_trk_damage_none();
    if (ev->type != TIKU_EVENT_KEY_DOWN) {
        return 0;
    }
    if (tiku_trk_status_overlay_key(&app->status_overlay, ev->key)) {
        *damage = tiku_trk_damage_all();
        return 1;
    }
    return 0;
}

/* the popup menu, with the navigator inside it */

/** @brief A menu that is showing takes the next press, wherever it is. */
static int
popup_hit(void *self, int x, int y)
{
    desktop_t *app = self;

    (void)x; (void)y;
    return app->popup != NULL;
}

/**
 * @brief A click inside a navigator level chooses its row (CW-056).
 * @return 1 when the click was on a row of a level.
 */
static int
nav_press(desktop_t *app, const tiku_event_t *ev)
{
    int px2 = app->popup_frame.x + app->popup_frame.w - 4;
    int py2 = app->popup_frame.y + 4;
    int k, took = 0;

    for (k = 0; k < app->nav.depth && !took; k++) {
        tiku_rect_t r;
        int at;

        tiku_trk_nav_place(&app->nav, k, px2, py2, app->surf->w,
                           app->surf->h, &r.x, &r.y, &r.w, &r.h);
        at = tiku_trk_nav_item_at(&app->nav, k, r.x, r.y, ev->x, ev->y);
        if (at >= 0) {
            /* Levels deeper than the one clicked close, so only the
             * clicked branch stays open. */
            while (app->nav.depth > k + 1) {
                (void)tiku_trk_nav_leave(&app->nav);
            }
            app->nav.level[k].sel = at;
            if (app->nav.level[k].item[at].is_container) {
                (void)tiku_trk_nav_enter(&app->nav, at);
            }
            took = 1;
        }
        px2 = r.x + r.w - 4;
        py2 = r.y + 2 + ((app->nav.level[k].sel >= 0)
                             ? app->nav.level[k].sel : 0) * TIKU_TRK_NAV_ROW;
    }
    return took;
}

/**
 * @brief Carry out command @p pc from a menu raised on one path, the Info
 *        window's icon: recalculate a size, re-point a link, or a file
 *        action on the path.
 */
static void
popup_path_pick(desktop_t *app, int pc)
{
    int islot = -1, fi;

    for (fi = 0; fi < TIKU_TRK_INFO_WINDOW_MAX; fi++) {
        tiku_trk_info_t *im = &app->info_windows.model[fi];

        if (im->used && strcmp(im->path, app->popup_path) == 0) {
            islot = fi;
            break;
        }
    }
    if (pc == TIKU_TRK_CMD_RECALC_SIZE && islot >= 0) {
        tiku_trk_info_calc_start(&app->info_windows.model[islot]);
    } else if (pc == TIKU_TRK_CMD_SET_LINK_TARGET && islot >= 0) {
        /* The file panel, as the broken-link flow opens it (AW-074). */
        folder_t *ffl2 = folder_of(app, tiku_workspace_focused(app->desk));
        folder_t *start = (ffl2 != NULL) ? ffl2 : &app->folders[0];

        snprintf(app->info_relink_path, sizeof app->info_relink_path, "%s",
                 app->info_windows.model[islot].path);
        tiku_trk_panel_window_open_any(&app->file_panel, start,
                                       TIKU_TRK_CMD_GET_INFO, start->path);
    } else {
        (void)tiku_trk_folder_file_actions_invoke_path(&app->file_actions,
                                                       app->popup_path, pc);
    }
}

/** @brief A click while a menu shows: choosing a row, or dismissing. */
static int
popup_press(void *self, const tiku_event_t *ev, tiku_rect_t *damage)
{
    desktop_t *app = self;
    tiku_window_t *fw;
    folder_t *ffl;
    int pick = -1;

    *damage = tiku_trk_damage_all();
    if (app->nav_on && nav_press(app, ev)) {
        return 1;
    }
#if TIKU_SHELL_BOARD_SERVICES

    if (app->popup == &app->devtraymenu) {
        /* A second press on the cell the menu dropped from opens the
         * board's folder.  The cell is outside the menu, so the press
         * closes the menu below. */
        (void)desktop_devtray_again(app, ev, tiku_trk_now());
    }
#endif

    fw = tiku_workspace_focused(app->desk);
    ffl = (app->bar_fl != NULL) ? app->bar_fl
        : (app->popup_fl != NULL) ? app->popup_fl : folder_of(app, fw);
    if (app->popup_sub != NULL) {
        pick = tiku_menu_item_at(app->popup_sub, app->popup_sub_frame,
                                 ev->x, ev->y);
        /* Only an enabled row acts; a disabled row is drawn grey and is
         * not highlighted.  pick stays set for a disabled row, so the
         * click dismisses the menus and does not reach the parent. */
        if (pick >= 0 && app->popup_sub->item[pick].enabled) {
            if (app->popup_path[0] != '\0') {
                (void)tiku_trk_folder_file_actions_invoke_path(
                    &app->file_actions, app->popup_path,
                    app->popup_sub->item[pick].command);
            } else {
                global_or_invoke(app, ffl,
                                 app->popup_sub->item[pick].command);
            }
        }
    }
    if (pick < 0) {
        pick = tiku_menu_item_at(app->popup, app->popup_frame, ev->x, ev->y);
        if (pick >= 0 && app->popup->item[pick].enabled) {
            if (app->popup->item[pick].submenu != NULL) {
                /* Opening a submenu is not a choice; the menu stays up. */
                app->popup_sub = app->popup->item[pick].submenu;
                app->popup_sub_frame = tiku_menu_place_sub(app->popup, pick,
                    app->popup_frame,
                    (tiku_rect_t){ 0, 0, app->surf->w, app->surf->h });
                return 1;
            }
            if (app->popup_path[0] != '\0') {
                popup_path_pick(app, app->popup->item[pick].command);
            } else {
                global_or_invoke(app, ffl, app->popup->item[pick].command);
            }
        }
    }
    app->popup_path[0] = '\0';
    app->popup_fl = NULL;
    app->popup = NULL;
    app->popup_sub = NULL;
    app->bar_fl = NULL;
    app->bar_open = -1;
    app->gbar_open = -1;
    app->gbar.open = -1;
    app->menus.bar.open = -1;
    return 1;
}

/** @brief The hot row follows the pointer in the level it is over. */
static int
popup_move(void *self, const tiku_event_t *ev, tiku_rect_t *damage)
{
    desktop_t *app = self;
    int moved = 0;

    *damage = tiku_trk_damage_none();
    if (app->popup == NULL) {
        return 0;
    }
    moved |= tiku_menu_track(app->popup, app->popup_frame, ev->x, ev->y);
    if (app->popup_sub != NULL) {
        moved |= tiku_menu_track(app->popup_sub, app->popup_sub_frame,
                                 ev->x, ev->y);
    }
    if (moved) {
        *damage = (app->popup_sub != NULL)
            ? tiku_rect_union(app->popup_frame, app->popup_sub_frame)
            : app->popup_frame;
    }
    return 0;
}

/**
 * @brief A menu that is open rewrites itself as Shift goes down and up:
 *        its items change label and command together while Shift is held,
 *        on both key edges, since releasing Shift puts the labels back.
 *        Every other key is the menu's while it shows.
 */
static int
popup_key(void *self, const tiku_event_t *ev, tiku_rect_t *damage)
{
    desktop_t *app = self;
    unsigned mk = ev->key;

    *damage = tiku_trk_damage_none();
    if (app->popup == NULL) {
        return 0;
    }
    if (mk == TIKU_KEY_SHIFT || mk == TIKU_KEY_CMD) {
        folder_t *ffl = folder_of(app, tiku_workspace_focused(app->desk));
        unsigned m2 = 0;
        tiku_trk_sc_ctx_t c2;

        /* A key-up carries the modifiers from before the release, so
         * the Shift bit is cleared here. */
        if (ev->modifiers & TIKU_MOD_SHIFT) {
            m2 |= TIKU_TRK_MOD_SHIFT;
        }
        if (ev->type == TIKU_EVENT_KEY_UP && mk == TIKU_KEY_SHIFT) {
            m2 &= ~(unsigned)TIKU_TRK_MOD_SHIFT;
        }
        if (ev->type == TIKU_EVENT_KEY_DOWN && mk == TIKU_KEY_SHIFT) {
            m2 |= TIKU_TRK_MOD_SHIFT;
        }
        c2 = menu_ctx(app, ffl, m2);
        tiku_trk_sc_update(app->popup, &c2);
        if (app->popup_sub != NULL) {
            tiku_trk_sc_update(app->popup_sub, &c2);
        }
        *damage = tiku_trk_damage_all();
    }
    return 1;
}

/* the global menu bar: one strip at the top of the screen */

/** @brief Whether the strip takes a point at @p y: the bar is on, no menu
 *         shows, and @p y is within the strip. */
static int
gbar_hit(void *self, int x, int y)
{
    desktop_t *app = self;

    (void)x;
    return app->gbar_on == 1 && app->popup == NULL &&
           y < app->menus.bar.height;
}

/**
 * @brief A primary press on the strip: the camera at its right sets a
 *        screenshot pending, taken from the next frame drawn so that the
 *        press is not in the picture; a device tray cell drops its menu; a
 *        title opens its menu, built from what the focused window
 *        published.
 */
static int
gbar_press(void *self, const tiku_event_t *ev, tiku_rect_t *damage)
{
    desktop_t *app = self;
    tiku_rect_t strip;
    int bi;

    *damage = tiku_trk_damage_none();
    if (ev->button != 1u) {
        return 0;
    }
    if (ev->x >= app->shot_btn.x &&
        ev->x < app->shot_btn.x + app->shot_btn.w) {
        app->shot_pending = 1;
        app->shot_pressed = 1;
        *damage = tiku_trk_damage_all();
        return 1;
    }
#if TIKU_SHELL_BOARD_SERVICES

    if (desktop_devtray_at(app, ev->x, ev->y) >= 0) {
        commit_rename(app, 1);
        (void)desktop_devtray_press(app, ev, tiku_trk_now());
        *damage = tiku_trk_damage_all();
        return 1;
    }
#endif

    strip = global_bar_rect(app);
    bi = tiku_menubar_title_at(&app->gbar, strip, ev->x, ev->y);
    if (bi < 0) {
        return 0;
    }
    commit_rename(app, 1);
    app->gbar_open = bi;
    app->gbar.open = bi;
    app->popup = app->gbar.menu[bi];
    app->popup_sub = NULL;
    app->popup_frame = tiku_menubar_panel(&app->gbar, strip,
        (tiku_rect_t){ 0, 0, app->surf->w, app->surf->h });
    app->nav_on = 0;
    *damage = tiku_trk_damage_all();
    return 1;
#if TIKU_SHELL_BOARD_SERVICES

}

/** @brief The strip's clock: the device tray follows the boards and ends
 *         the red of a failure whose time is up. */
static int
gbar_tick(void *self, int64_t now, tiku_rect_t *damage)
{
    return desktop_devtray_tick(self, now, damage);
#endif

}

/* the spring navigator, tracked while a drag moves over it */

/** @brief A drag moving over the spring navigator walks its levels. */
static int
spring_move(void *self, const tiku_event_t *ev, tiku_rect_t *damage)
{
    desktop_t *app = self;

    *damage = tiku_trk_damage_none();
    if (app->spring_nav_on &&
        (tiku_trk_drag_moved(&app->drag, ev->x, ev->y) ||
         tiku_trk_drag_active(&app->drag))) {
        spring_nav_track(app, ev->x, ev->y);
    }
    return 0;
}

/* the Info window's field pop-up: a floating box with a frame of its own */

/**
 * @brief The box the hovered field's full text floats in, when the text
 *        is wider than its row; @return 0 for none.
 */
static int
info_hover_box(desktop_t *app, int slot, int field, tiku_rect_t *box,
               char *value, size_t max)
{
    tiku_trk_info_t *info;
    const char *label = "";
    const tiku_font_t *font = tiku_font_plain();
    int fx, fy, fw, fh, tw;

    if (slot < 0 || field < 0) {
        return 0;
    }
    info = &app->info_windows.model[slot];
    if (tiku_trk_info_field(info, app->store, field, &label, value, max) ==
            TIKU_TRK_INFO_F_NONE ||
        !tiku_trk_info_windows_field_rect(&app->info_windows, slot, field,
                                          &fx, &fy, &fw, &fh)) {
        return 0;
    }
    tw = tiku_text_width(font, value);
    if (tw <= fw) {
        return 0;               /* the text fits: no pop-up */
    }
    *box = (tiku_rect_t){ fx - 4, fy - 2, tw + 12, font->height + 6 };
    if (box->x + box->w > app->surf->w) {
        box->x = app->surf->w - box->w;
    }
    if (box->x < 0) {
        box->x = 0;
    }
    return 1;
}

/** @brief The pop-up's box as damage; no damage when no pop-up shows. */
static tiku_rect_t
info_hover_damage_now(desktop_t *app)
{
    tiku_rect_t box;
    char value[TIKU_TRK_INFO_VAL_MAX];

    if (info_hover_box(app, app->info_windows.hover_slot,
                       app->info_windows.hover_field, &box, value,
                       sizeof value)) {
        return box;
    }
    return tiku_trk_damage_none();
}

/** @brief The pointer over a truncated field of the focused Info window
 *         opens the pop-up at once (AW-073); a drag closes it. */
static int
info_hover_move(void *self, const tiku_event_t *ev, tiku_rect_t *damage)
{
    desktop_t *app = self;
    tiku_window_t *hw = NULL;
    int hslot = -1, hfield = -1, hwhich = -1;

    *damage = tiku_trk_damage_none();
    if (!tiku_trk_drag_active(&app->drag)) {
        (void)tiku_workspace_hit(app->desk, ev->x, ev->y, &hw);
        hslot = tiku_trk_info_windows_slot(&app->info_windows, hw);
        if (hslot >= 0 && hw == tiku_workspace_focused(app->desk) &&
            tiku_trk_info_windows_hit(&app->info_windows, hslot, ev->x,
                                      ev->y, &hwhich) ==
                TIKU_TRK_INFO_HIT_FIELD) {
            hfield = hwhich;
        } else {
            hslot = -1;
        }
    }
    if (hslot != app->info_windows.hover_slot ||
        hfield != app->info_windows.hover_field) {
        /* The old box and the new one are damaged: the pop-up can
         * extend past its window's frame. */
        *damage = info_hover_damage_now(app);
        app->info_windows.hover_slot = hslot;
        app->info_windows.hover_field = hfield;
        *damage = tiku_trk_damage_union(*damage, info_hover_damage_now(app));
    }
    return 0;
}

/** @brief A release closes the pop-up; the next move may open it again. */
static int
info_hover_release(void *self, const tiku_event_t *ev, tiku_rect_t *damage)
{
    desktop_t *app = self;

    (void)ev;
    *damage = info_hover_damage_now(app);
    app->info_windows.hover_slot = -1;
    app->info_windows.hover_field = -1;
    return 0;
}

/** @brief The hovered field's full text in its floating box. */
static void
info_hover_paint(void *self, tiku_surface_t *s)
{
    desktop_t *app = self;
    const tiku_font_t *font = tiku_font_plain();
    char value[TIKU_TRK_INFO_VAL_MAX];
    tiku_rect_t box;

    if (!info_hover_box(app, app->info_windows.hover_slot,
                        app->info_windows.hover_field, &box, value,
                        sizeof value)) {
        return;
    }
    tiku_fill(s, box, TIKU_C_NOTE);
    tiku_frame(s, box, TIKU_C_TEXT);
    tiku_text(s, font, box.x + 5, box.y + 2 + font->ascent, value,
              TIKU_C_TEXT);
}

/**
 * @brief The text the Info field pop-up shows, into @p out, for a driver's
 *        question.
 * @return 1, or 0 when no pop-up shows.
 */
int
info_hover_text(desktop_t *app, char *out, size_t max)
{
    char value[TIKU_TRK_INFO_VAL_MAX];
    tiku_rect_t box;

    if (!info_hover_box(app, app->info_windows.hover_slot,
                        app->info_windows.hover_field, &box, value,
                        sizeof value)) {
        return 0;
    }
    snprintf(out, max, "%s", value);
    return 1;
}

/* the responder list */

/**
 * @brief Fill the responder list, bottom first: the Info field pop-up, the
 *        spring navigator, the folders' content, the desk's keys, the
 *        window frames, the Deskbar, the global strip, the Info windows'
 *        parts, F1 help, the menus, the alert and progress overlay, and the
 *        force-quit picker.
 */
void
desktop_responders_attach(desktop_t *app)
{
    tiku_trk_responders_init(&app->responders);
    app->info_hover_responder = (tiku_trk_responder_t){
        .name = "info-hover", .self = app, .move = info_hover_move,
        .release = info_hover_release, .paint = info_hover_paint
    };
    app->spring_responder = (tiku_trk_responder_t){
        .name = "spring", .self = app, .move = spring_move
    };
    app->gbar_responder = (tiku_trk_responder_t){
#if TIKU_SHELL_BOARD_SERVICES

        .name = "global-bar", .self = app, .hit = gbar_hit,
        .press = gbar_press, .tick = gbar_tick
    }
#else

        .name = "global-bar", .self = app, .hit = gbar_hit,
        .press = gbar_press
    }
#endif
;
    app->popup_responder = (tiku_trk_responder_t){
        .name = "popup", .self = app, .hit = popup_hit, .press = popup_press,
        .move = popup_move, .key = popup_key
    };
    app->status_responder = (tiku_trk_responder_t){
        .name = "status", .self = app, .hit = status_hit,
        .press = status_press, .move = status_move,
        .release = status_release, .key = status_key
    };
    app->forcequit_responder = (tiku_trk_responder_t){
        .name = "forcequit", .self = app, .hit = forcequit_hit,
        .press = forcequit_take, .move = forcequit_take,
        .release = forcequit_take, .key = forcequit_take
    };
    (void)tiku_trk_responders_push(&app->responders,
                                   &app->info_hover_responder);
    (void)tiku_trk_responders_push(&app->responders, &app->spring_responder);
    desktop_folders_attach(app);
    desktop_keys_attach(app);
    desktop_windows_attach(app);
    desktop_bar_attach(app);
    (void)tiku_trk_responders_push(&app->responders, &app->gbar_responder);
    desktop_infowin_attach(app);
    desktop_help_attach(app);
    (void)tiku_trk_responders_push(&app->responders, &app->popup_responder);
    (void)tiku_trk_responders_push(&app->responders, &app->status_responder);
    (void)tiku_trk_responders_push(&app->responders,
                                   &app->forcequit_responder);
}
