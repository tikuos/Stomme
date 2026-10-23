/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_desktop_bar.c - the Deskbar as a responder: every press, move,
 * release, key and tick the bar answers, and the damage each one leaves.
 *
 * The bar's own state is desktop_bar_t; beyond it, the responder uses the
 * Deskbar itself, the drag a program's row arms, and the desktop's pick
 * dispatch.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_trk_desktop_private.h"

/** @brief The bar's frame, as a damage. */
static tiku_rect_t
bar_frame(desktop_t *app)
{
    return tiku_trk_deskbar_frame(app->deskbar);
}

/** @brief Whether @p x, @p y is inside the bar's frame. */
static int
in_frame(desktop_t *app, int x, int y)
{
    tiku_rect_t bf = bar_frame(app);

    return x >= bf.x && x < bf.x + bf.w && y >= bf.y && y < bf.y + bf.h;
}

/** @brief The bar answers a press on itself, or anywhere while its menu
 *         is out. */
static int
bar_hit(void *self, int x, int y)
{
    desktop_t *app = self;

    return tiku_trk_deskbar_menu_open(app->deskbar) ||
           tiku_trk_deskbar_hit(app->deskbar, x, y);
}

/** @brief On a press on a program's row: arm a drag that carries the
 *         program, and keep its path in bar.launch, which a release with
 *         no drag starts. */
static void
arm_program(desktop_t *app, const tiku_trk_db_pick_t *pick,
            const tiku_event_t *ev)
{
    const tiku_trk_app_t *program = desktop_app_at(app, pick->path);
    char parent[TIKU_PATH_MAX];
    tiku_model_t pm;

    parent_path(pick->path, parent, sizeof parent);
    tiku_trk_drag_arm(&app->drag, ev->x, ev->y, ev->button, 0u, 1, NULL,
                      parent);
    tiku_trk_drag_set_source_virtual(&app->drag, 1);
    (void)tiku_trk_drag_add_at(&app->drag, pick->path,
        (app->backend->ops->stat(app->backend, pick->path, &pm) == 0)
            ? pm.type : "", ev->x, ev->y, 1);
    tiku_trk_drag_name_at(&app->drag, 0, program->name);
    tiku_trk_drag_target_reset(&app->drag_target);
    snprintf(app->bar.launch, sizeof app->bar.launch, "%s", pick->path);
}

/**
 * @brief A press: the resize strip, a Cmd press that may re-dock, a click
 *        on a row or a menu, or the wheel over the list.
 */
static int
bar_press(void *self, const tiku_event_t *ev, tiku_rect_t *damage)
{
    desktop_t *app = self;
    desktop_bar_t *bar = &app->bar;
    tiku_trk_db_pick_t pick;
    int secondary = (ev->button == 3u);
    int dclicks = 1;

    *damage = tiku_trk_damage_none();
    if (ev->button == 1u && ev->modifiers == 0u) {
        /* The resize strip: four pixels along the bar's inner edge;
         * dragging it sets the bar's width (DB-050). */
        tiku_rect_t bf = bar_frame(app);
        tiku_trk_db_dock_t dk = tiku_trk_deskbar_dock(app->deskbar);
        int inner_ok = (dk == TIKU_TRK_DB_RIGHT_EXPANDED &&
                        ev->x >= bf.x && ev->x < bf.x + 4) ||
                       (dk == TIKU_TRK_DB_LEFT_EXPANDED &&
                        ev->x >= bf.x + bf.w - 4 && ev->x < bf.x + bf.w);

        if (inner_ok && ev->y >= bf.y && ev->y < bf.y + bf.h) {
            bar->resize = 1;
            bar->resize_x = ev->x;
            bar->resize_w = tiku_trk_deskbar_width(app->deskbar);
            return 1;
        }
    }
    if (ev->button == 1u && (ev->modifiers & TIKU_MOD_CMD) != 0u &&
        in_frame(app, ev->x, ev->y)) {
        /* A Cmd press waits: if it moves past the drag slop, it re-docks
         * the bar (DB-049); released before that, it is a chord the rows
         * answer (DB-019/021). */
        bar->dock_armed = 1;
        bar->dock_x = ev->x;
        bar->dock_y = ev->y;
        bar->dock_mods = ev->modifiers;
        bar->dock_button = ev->button;
        return 1;
    }
    /* The bar's own double-click: same spot, inside the interval, and
     * not refreshed when it fires, so a triple click cannot re-trigger
     * (DB-019). */
    if (ev->button == 1u && tiku_trk_now() - bar->click_at < 500000 &&
        ev->x - bar->click_x <= 4 && bar->click_x - ev->x <= 4 &&
        ev->y - bar->click_y <= 4 && bar->click_y - ev->y <= 4) {
        dclicks = 2;
        bar->click_at = 0;
    } else if (ev->button == 1u) {
        bar->click_at = tiku_trk_now();
        bar->click_x = ev->x;
        bar->click_y = ev->y;
    }
    memset(&pick, 0, sizeof pick);
    if (tiku_trk_deskbar_click(app->deskbar, ev->x, ev->y, ev->modifiers,
                               (int)ev->button, dclicks, &pick)) {
        if (ev->button == 1u && !secondary) {
            bar->held = 1;
            bar->press_at = tiku_trk_now();
            bar->press_x = ev->x;
            bar->press_y = ev->y;
        }
        if (pick.what == TIKU_TRK_DB_LAUNCH_APP && ev->button == 1u &&
            !secondary && desktop_app_at(app, pick.path) != NULL) {
            /* A program's row can be picked up as well as chosen: the
             * release starts it only if no drag began. */
            arm_program(app, &pick, ev);
        } else {
            deskbar_pick_dispatch(app, &pick, ev->x, ev->y);
        }
        *damage = tiku_trk_damage_all();
        return 1;
    }
    if ((ev->button == 4u || ev->button == 5u) &&
        in_frame(app, ev->x, ev->y)) {
        /* The wheel over the bar scrolls its list; with a modifier it
         * moves three rows a step, the wheel and the arrows alike
         * (DB-034). */
        int wstep = ((ev->modifiers & (TIKU_MOD_SHIFT | TIKU_MOD_CMD |
                                       TIKU_MOD_OPTION)) != 0u) ? 3 : 1;

        if (tiku_trk_deskbar_scroll(app->deskbar,
                                    (ev->button == 4u) ? -wstep : wstep)) {
            *damage = bar_frame(app);
        }
        return 1;
    }
    return 0;
}

/**
 * @brief A move: the hover on a row, the resize and the re-dock in
 *        flight, and where the pointer is for auto-hide.
 * @return 0: the rest of the desk sees the move too.
 */
static int
bar_move(void *self, const tiku_event_t *ev, tiku_rect_t *damage)
{
    desktop_t *app = self;
    desktop_bar_t *bar = &app->bar;

    *damage = tiku_trk_damage_none();
    if (tiku_trk_deskbar_motion(app->deskbar, ev->x, ev->y)) {
        *damage = tiku_trk_damage_union(*damage,
            tiku_trk_deskbar_hover_area(app->deskbar));
    }
    if (bar->held && (ev->x - bar->press_x > 4 || bar->press_x - ev->x > 4 ||
                      ev->y - bar->press_y > 4 || bar->press_y - ev->y > 4)) {
        bar->held = 0;          /* moved: no long press */
    }
    if (bar->resize) {
        int dw = (tiku_trk_deskbar_dock(app->deskbar) ==
                  TIKU_TRK_DB_RIGHT_EXPANDED)
                     ? bar->resize_x - ev->x : ev->x - bar->resize_x;

        if (tiku_trk_deskbar_set_width(app->deskbar, bar->resize_w + dw) > 0) {
            *damage = tiku_trk_damage_all();
        }
    }
    if (bar->dock_armed &&
        ((ev->x - bar->dock_x) * (ev->x - bar->dock_x) +
         (ev->y - bar->dock_y) * (ev->y - bar->dock_y) >
         TIKU_TRK_DRAG_SLOP * TIKU_TRK_DRAG_SLOP)) {
        bar->dock_armed = 0;
        bar->dock_drag = 1;
    }
    if (bar->dock_drag) {
        /* While a Cmd drag re-docks, the dock follows the pointer's edge
         * of the screen; in the middle the bar keeps its current dock
         * (DB-049). */
        tiku_trk_db_dock_t want = tiku_trk_deskbar_dock_at(ev->x, ev->y,
            app->surf->w, app->surf->h, tiku_trk_deskbar_dock(app->deskbar));

        if (want != tiku_trk_deskbar_dock(app->deskbar)) {
            tiku_trk_deskbar_set_dock(app->deskbar, want);
            *damage = tiku_trk_damage_all();
        }
    }
    if (tiku_trk_deskbar_pointer(app->deskbar, ev->x, ev->y)) {
        /* Auto-hide and auto-raise follow the pointer (DB-051,
         * DB-052). */
        *damage = tiku_trk_damage_all();
    }
    return 0;
}

/**
 * @brief A release: a program's row let go where it was pressed starts
 *        it; a Cmd chord that never moved is answered; the resize and the
 *        re-dock end; an expander press commits.
 */
static int
bar_release(void *self, const tiku_event_t *ev, tiku_rect_t *damage)
{
    desktop_t *app = self;
    desktop_bar_t *bar = &app->bar;

    *damage = tiku_trk_damage_none();
    if (bar->launch[0] != '\0') {
        if (!tiku_trk_drag_active(&app->drag)) {
            tiku_trk_db_pick_t pick;

            memset(&pick, 0, sizeof pick);
            pick.what = TIKU_TRK_DB_LAUNCH_APP;
            snprintf(pick.path, sizeof pick.path, "%s", bar->launch);
            bar->launch[0] = '\0';
            bar->held = 0;
            tiku_trk_drag_reset(&app->drag);
            deskbar_pick_dispatch(app, &pick, ev->x, ev->y);
            *damage = tiku_trk_damage_all();
            return 1;
        }
        bar->launch[0] = '\0';  /* carried off: the drop handles it */
    }
    bar->dock_drag = 0;
    bar->resize = 0;
    if (bar->dock_armed) {
        /* The Cmd press never moved: answer it as a chord
         * (DB-019/020/021). */
        tiku_trk_db_pick_t pick;

        bar->dock_armed = 0;
        memset(&pick, 0, sizeof pick);
        if (tiku_trk_deskbar_click(app->deskbar, bar->dock_x, bar->dock_y,
                                   bar->dock_mods, (int)bar->dock_button, 1,
                                   &pick)) {
            deskbar_pick_dispatch(app, &pick, bar->dock_x, bar->dock_y);
            *damage = tiku_trk_damage_all();
        }
    }
    bar->held = 0;
    if (tiku_trk_deskbar_release(app->deskbar, ev->x, ev->y)) {
        *damage = tiku_trk_damage_all();
    }
    return 0;
}

/** @brief Escape closes the calendar; the Menu key alone toggles the leaf
 *         menu. */
static int
bar_key(void *self, const tiku_event_t *ev, tiku_rect_t *damage)
{
    desktop_t *app = self;

    *damage = tiku_trk_damage_none();
    if (ev->key == TIKU_KEY_ESCAPE &&
        tiku_trk_deskbar_calendar_open(app->deskbar)) {
        /* Escape is consumed here: it closes the calendar and nothing
         * behind it. */
        tiku_trk_deskbar_close_calendar(app->deskbar);
        *damage = tiku_trk_damage_all();
        return 1;
    }
    if (ev->key == TIKU_KEY_MENU &&
        (ev->modifiers & (TIKU_MOD_CMD | TIKU_MOD_SHIFT | TIKU_MOD_OPTION))
            == 0u) {
        /* The Menu key opens and closes the leaf menu (DB-011). */
        if (tiku_trk_deskbar_menu_open(app->deskbar)) {
            tiku_trk_deskbar_close_menu(app->deskbar);
        } else {
            tiku_trk_deskbar_open_menu(app->deskbar);
        }
        *damage = tiku_trk_damage_all();
        return 1;
    }
    return 0;
}

/**
 * @brief The turn's clock: a primary button held still for two click
 *        intervals is the bar's long press, a secondary click without
 *        the release (PVS-017); a hovered row expands after its dwell.
 */
static int
bar_tick(void *self, int64_t now, tiku_rect_t *damage)
{
    desktop_t *app = self;
    desktop_bar_t *bar = &app->bar;
    int changed = 0;

    *damage = tiku_trk_damage_none();
    if (bar->held && now - bar->press_at > 2 * TIKU_TRK_CLICK_SPEED_US) {
        tiku_trk_db_pick_t pick;

        tiku_trk_deskbar_close_calendar(app->deskbar);
        memset(&pick, 0, sizeof pick);
        (void)tiku_trk_deskbar_click(app->deskbar, bar->press_x,
                                     bar->press_y, 0u, 3, 1, &pick);
        bar->held = 0;
        *damage = tiku_trk_damage_all();
        changed = 1;
    }
    if (tiku_trk_deskbar_menu_tick(app->deskbar, now)) {
        *damage = tiku_trk_damage_all();
        changed = 1;
    }
    return changed;
}

/** @brief Put the Deskbar on the responder list with a fresh state. */
void
desktop_bar_attach(desktop_t *app)
{
    memset(&app->bar, 0, sizeof app->bar);
    app->bar_responder = (tiku_trk_responder_t){
        .name = "deskbar", .self = app, .hit = bar_hit, .press = bar_press,
        .move = bar_move, .release = bar_release, .key = bar_key,
        .tick = bar_tick, .paint = NULL
    };
    (void)tiku_trk_responders_push(&app->responders, &app->bar_responder);
}

/** @brief Ask for a frame of what a responder said it changed. */
void
desktop_take_damage(desktop_t *app, tiku_rect_t damage)
{
    if (tiku_trk_damage_is_all(damage)) {
        desktop_damage_all(app);
    } else if (!tiku_trk_damage_is_none(damage)) {
        desktop_damage(app, damage);
    }
}
