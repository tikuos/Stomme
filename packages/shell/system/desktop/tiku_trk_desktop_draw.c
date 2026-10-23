/*
 * Desktop shell and file manager.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_desktop_draw.c - drawing a frame: the backdrop, the windows, the
 * Deskbar, the menus and the overlays, and presenting it to the display;
 * the damage that asks for a frame, the screen resize and the font change
 * that reshape it, and the background work done each turn.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_shell_product.h"
#include "tiku_state.h"
#include "tiku_trail.h"
#include "tiku_trk_desktop_private.h"
#include <sys/stat.h>
#include <time.h>

/* Used before they are defined. */
void desktop_draw_splash(desktop_t *app);
void desktop_draw_overlays(desktop_t *app);
void desktop_draw_popup(desktop_t *app);
void desktop_draw_bars(desktop_t *app);
void desktop_draw_drag(desktop_t *app);
void desktop_draw_strip_drag(desktop_t *app);
/* End of the forward declarations. */

/** @brief The preferences' backdrop colour, taken as the desktop's. */
void
set_backdrop_color(void *context, const tiku_trk_color_t *color)
{
    desktop_t *app = context;

    if (app != NULL && color != NULL) {
        app->backdrop_color = TIKU_RGB(color->r, color->g, color->b);
    }
}

/**
 * @brief The clock's periodic task: repaint when the Deskbar clock's text
 *        changes (DB-044).
 * @return 0, so the task stays scheduled.
 */
int
tick_clock(void *ctx)
{
    desktop_t *app = ctx;
    char stamp[64];

    if (tiku_trk_deskbar_clock_stamp(app->deskbar, stamp,
                                     sizeof stamp) &&
        strcmp(stamp, app->clock_stamp) != 0) {
        /* The first stamp seen is recorded without a repaint: the
         * settings and a test fixture's frozen clock arrive after the
         * loop starts. */
        if (app->clock_stamp[0] != '\0') {
            desktop_damage_all(app);
        }
        snprintf(app->clock_stamp, sizeof app->clock_stamp, "%s", stamp);
    }
    return 0;
}

/**
 * @brief Present the frame on the display, only its damaged region after a
 *        bounded frame.  With no host nothing is presented: the surface is
 *        the output, and -shot writes it.
 */
void
blit(desktop_t *app)
{
    if (app->host == NULL) {
        return;
    }
    (void)tiku_host_present_in(app->host, app->surf,
                               app->frame_partial ? &app->frame_damage : NULL);
}

/**
 * @brief Resize the surface and tell every window controller the new
 *        screen size.  @return 1 when the size changed, 0 when it did
 *        not or the request was empty, -1 when the surface refused.
 */
int
resize_desktop(desktop_t *app, int width, int height)
{
    if (app == NULL || width <= 0 || height <= 0 ||
        (width == app->surf->w && height == app->surf->h)) {
        return 0;
    }
    if (tiku_surface_resize(app->surf, width, height,
                                 app->backdrop_color) != 0) {
        return -1;
    }
    app->status_overlay.screen_w = width;
    app->status_overlay.screen_h = height;
    app->find_window.screen_w = width;
    app->find_window.screen_h = height;
    app->select_window.screen_w = width;
    app->select_window.screen_h = height;
    app->prefs_window.screen_w = width;
    app->prefs_window.screen_h = height;
    app->deskbar_prefs_window.screen_w = width;
    app->deskbar_prefs_window.screen_h = height;
    app->desktop_prefs_window.screen_w = width;
    app->desktop_prefs_window.screen_h = height;
    app->fonts_window.screen_w = width;
    app->fonts_window.screen_h = height;
    app->sounds_window.screen_w = width;
    app->sounds_window.screen_h = height;
    app->open_with_window.screen_w = width;
    app->open_with_window.screen_h = height;
    app->switcher_window.screen_w = width;
    app->switcher_window.screen_h = height;
    app->file_panel.screen_w = width;
    app->file_panel.screen_h = height;
    app->folder_render.screen_w = width;
    app->folder_render.screen_h = height;
    app->folder_lifecycle.screen_w = width;
    app->folder_lifecycle.screen_h = height;
    (void)tiku_trk_desktop_view_resize(&app->desktop_view, width, height);
    app->drop_controller.screen_w = width;
    app->drop_controller.screen_h = height;
    return 1;
}

/**
 * @brief Keep the row height in step with the plain font: when the font's
 *        height changes, set the new row height in every controller that
 *        keeps a copy, and repaint.
 * @note Called at the start of every frame, so a font changed from any
 *       window is followed.
 */
static void
follow_font(desktop_t *app)
{
    int want = tiku_font_plain()->height + 5;

    if (want == app->row_h) {
        return;
    }
    app->row_h = want;
    app->folder_render.row_h = want;
    app->band_controller.row_h = want;
    app->drag_target.row_h = want;
    app->file_panel.row_h = want;
    app->script_adapter.row_h = want;
    /*
     * The open views are not set here: each folder sets its view's row
     * height from folder_render when it draws, after this runs.
     */
    desktop_damage_all(app);
}

/** @brief Ask for a full frame. */
void
desktop_damage_all(desktop_t *app)
{
    app->dirty = 1;
}

/** @brief Accumulate known damage without weakening a full invalidation. */
void desktop_damage(desktop_t *app, tiku_rect_t rect)
{
    if (rect.w <= 0 || rect.h <= 0) return;
    /* dirty == 1 asks for a full frame; bounded damage never turns it
     * into a partial one. */
    if (app->dirty && app->dirty != 2) return;
    /* A window's shadow reaches past its frame: what a move of it
     * dirties is that much more on every side. */
    rect.x -= TIKU_WINDOW_SHADOW; rect.y -= TIKU_WINDOW_SHADOW;
    rect.w += 2 * TIKU_WINDOW_SHADOW; rect.h += 2 * TIKU_WINDOW_SHADOW;
    app->damage = app->dirty ? tiku_rect_union(app->damage, rect) : rect;
    app->dirty = 2;
}

/**
 * @brief Draw a frame: only the damaged region after bounded damage, else
 *        the whole screen, from the backdrop up to the force-quit banner;
 *        and record what the frame cost.
 */
void
draw_all(desktop_t *app)
{
    follow_font(app);
    int partial = app->dirty == 2;
    int64_t started = tiku_trk_now();
    /* The request is cleared before drawing, so an animation that asks
     * for the next frame while this one is drawn keeps its request. */
    app->dirty = 0;
    tiku_clip_damage(app->surf, partial ? &app->damage : NULL);
    app->frame_partial = partial;
    app->frame_damage = app->surf->clip;
    tiku_workspace_cache_mode(app->desk,
        partial ? TIKU_CACHE_REUSE : TIKU_CACHE_REFRESH);
    app->frame_pixels += (uint64_t)app->surf->clip.w*app->surf->clip.h;
    if (partial) app->partial_frames++;
    else app->full_frames++;
    /* The strip tracker's owner, refreshed per frame so only the window a
     * gesture is running in draws the slide and the guide (TS-063). */
    app->folder_render.strip_owner =
        (tiku_trk_titleview_tracking(&app->tstrip) != TIKU_TRK_TRACK_NONE)
            ? app->tstrip_fl : NULL;

    tiku_fill(app->surf,
                   (tiku_rect_t){ 0, 0, app->surf->w, app->surf->h },
                   app->backdrop_color);
    desktop_publish_teams(app);    /* a row per application */
    /* Animations (a window rising as it opens) get the clock only with
     * a display and Reduce motion off; with no host the frames stay
     * still for a script to read. */
    int motion = app->host != NULL &&
        !tiku_trk_prefs_get(prefs_model(app), "ReduceMotion");
    tiku_workspace_set_now(app->desk, motion ? started : 0);
    tiku_trk_deskbar_set_now(app->deskbar, motion ? started : 0);
    if (!tiku_trk_deskbar_raised(app->deskbar)) {
        /* The bar not raised is drawn under the windows, which may
         * cover it; the drawing order is the z-order (DB-052). */
        tiku_trk_deskbar_draw(app->deskbar, app->surf);
        tiku_workspace_draw(app->desk);
    } else {
        tiku_workspace_draw(app->desk);
        tiku_trk_deskbar_draw(app->deskbar, app->surf);
    }
    if (tiku_workspace_animating(app->desk)) {
        /* The next frame damages only where the animation moves. */
        desktop_damage(app, tiku_workspace_motion(app->desk));
    }
    /* A Deskbar menu still fading in asks for its own rectangle again. */
    desktop_damage(app, tiku_trk_deskbar_motion_damage(app->deskbar));
    tiku_trk_responders_paint(&app->responders, app->surf);
    desktop_draw_strip_drag(app);
    desktop_draw_drag(app);
    desktop_draw_bars(app);
    desktop_draw_popup(app);
    desktop_draw_overlays(app);
    desktop_draw_splash(app);
    desktop_forcequit_overlay(app);
    tiku_clip_damage(app->surf, NULL);
    int64_t elapsed = tiku_trk_now()-started;
    app->draw_us += elapsed;
    app->frame_samples[app->frame_sample_count++ % 128] = elapsed;
    if (elapsed > 16667) app->slow_frames++;
}

/**
 * @brief Take the screenshot asked for, if one is: the desk, compressed on a
 *        worker into Desktop/Screenshots.
 */
void
desktop_screenshot_tick(desktop_t *app)
{
    if (!app->shot_pending) {
        return;
    }
    if (app->shot_pressed) {
        app->shot_pressed = 0;
        desktop_damage_all(app);
        return;
    }
    app->shot_pending = 0;
    app->shot_pressed = 0;
    desktop_shot_desk(app);
    desktop_damage_all(app);
}

/**
 * @brief Draw every open navigation level, outermost first, each beside the
 *        row that opened it, so the path back stays on screen (CW-045).
 */
static void
draw_nav_levels(desktop_t *app, int px, int py)
{
    {
        const tiku_font_t *nf = tiku_font_plain();
        int k;

        for (k = 0; k < app->nav.depth; k++) {
            tiku_trk_nav_level_t *lv = &app->nav.level[k];
            tiku_rect_t r;
            int i;

            tiku_trk_nav_place(&app->nav, k, px, py, app->surf->w,
                               app->surf->h,
                               &r.x, &r.y, &r.w, &r.h);
            tiku_fill(app->surf, r, TIKU_C_PANEL);
            tiku_bevel(app->surf, r,
                tiku_tint(TIKU_C_PANEL, TIKU_LIGHTEN_2),
                tiku_tint(TIKU_C_PANEL, TIKU_DARKEN_2));
            if (lv->n == 0) {
                tiku_text(app->surf, nf, r.x + 8,
                               r.y + 2 + nf->ascent, TIKU_TRK_NAV_EMPTY,
                               tiku_dim(TIKU_C_TEXT, TIKU_C_PANEL));
            }
            for (i = 0; i < lv->n; i++) {
                int ty = r.y + 2 + i * TIKU_TRK_NAV_ROW;
                tiku_rect_t row2;

                row2.x = r.x + 1; row2.y = ty; row2.w = r.w - 2;
                row2.h = TIKU_TRK_NAV_ROW;
                if (i == lv->sel) {
                    tiku_fill(app->surf, row2, TIKU_C_SELECT);
                }
                tiku_text(app->surf, nf, r.x + 8, ty + nf->ascent + 2,
                               lv->item[i].name,
                               (i == lv->sel) ? TIKU_C_SELTEXT
                               : (lv->item[i].enabled ? TIKU_C_TEXT
                                  : tiku_dim(TIKU_C_TEXT,
                                             TIKU_C_PANEL)));
                if (lv->item[i].is_container) {
                    /* A container row gets a submenu arrow. */
                    tiku_ui_submenu_arrow(app->surf,
                        (tiku_rect_t){ r.x, row2.y, r.w, row2.h },
                        (i == lv->sel) ? TIKU_C_SELTEXT : TIKU_C_TEXT);
                }
            }
            /* The next level starts beside this level's chosen row. */
            px = r.x + r.w - 4;
            py = r.y + 2 + ((lv->sel >= 0) ? lv->sel : 0) *
                 TIKU_TRK_NAV_ROW;
        }
    }
}

/**
 * @brief Tell the remote applications which window has focus (none while
 *        an alert is open), and let only the focused folder window paint
 *        its selection; the others keep theirs unseen, unless inactive
 *        selections are set to show (PVS-023).
 */
void
desktop_focus_paint(desktop_t *app)
{
    int i;

    /* Run once per turn, so a change of focus is followed whatever made
     * it. */
    {
        tiku_window_t *fw = tiku_workspace_focused(app->desk);

        remote_focus(app, app->status_overlay.alert.open ? NULL : fw);
        for (i = 0; i < app->folder_count; i++) {
            folder_t *fl = &app->folders[i];

            if (fl->win == NULL || !fl->win->open) {
                continue;
            }
            tiku_trk_poseview_window_activated(fl->view,
                                               fl->win == fw);
        }
    }
}

/**
 * @brief One step of the background work, each turn: the activation sound,
 *        the sound and panel pumps, boards dialling in, the channels, a step
 *        of a folder-size walk, the next thumbnail, a slice of each live
 *        query, and the band's and the drag's timers.
 */
void
desktop_idle_work(desktop_t *app)
{
    tiku_window_t *front = tiku_workspace_focused(app->desk);
    int windows = tiku_workspace_count(app->desk);

    /* A window coming to the front plays the activation sound, unless it
     * came there by opening, which played its own. */
    if (front != NULL && front != app->sound_focused &&
        windows <= app->sound_windows) {
        tiku_sound_play(TIKU_SOUND_WINDOW_ACTIVATE);
    }
    app->sound_focused = front;
    app->sound_windows = windows;
    tiku_sound_pump();
    panels_pump(app);
#if TIKU_SHELL_BOARD_SERVICES

    boards_accept(app);
    channels_tend(app);
#endif

    /* One directory of one folder-size walk per turn, so a Get Info
     * window on a deep tree fills in while the desktop keeps answering. */
    (void)tiku_trk_info_windows_calc_step(&app->info_windows);
    /* One picture at a time, decoded on a worker: the window keeps
     * answering while a folder of images fills in (IV-031). */
    (void)desktop_wire_thumb(app);
    {
        /* A live query window's walk, sixteen directories per turn, so
         * the window fills as it goes (Q-045). */
        int k;

        for (k = 0; k < app->folder_count; k++) {
            if (tiku_trk_folder_lifecycle_step_results(
                    &app->folder_lifecycle, &app->folders[k], 16)) {
                desktop_damage_all(app);
            }
        }
    }
    (void)tiku_trk_band_controller_pulse(&app->band_controller,
                                         tiku_trk_now());
    (void)tiku_trk_drag_target_pulse(&app->drag_target,
                                     tiku_trk_now());

}

/**
 * @brief Draw the boot or the closing screen over the finished frame, and
 *        ask for the next frame while it shows.
 */
void
desktop_draw_splash(desktop_t *app)
{
    /* Drawn over the complete frame, so the fade uncovers a finished
     * picture. */
    if (tiku_trk_splash_visible(&app->splash, tiku_trk_now())) {
        tiku_trk_splash_draw(&app->splash, app->surf,
                             (tiku_rect_t){ 0, 0, app->surf->w, app->surf->h },
                             tiku_trk_now());
        desktop_damage_all(app);         /* still showing: next frame */
    }
}

/**
 * @brief Draw the overlays: the opened icon's outline, the dragged query
 *        icon, the navigation levels, and the alert and progress overlay.
 */
void
desktop_draw_overlays(desktop_t *app)
{
    if (app->activation_motion.active) {
        /* Turning Reduce motion on while it runs passes a zero time, which
         * retires it on this frame. */
        int enabled = app->host != NULL &&
                      !tiku_trk_prefs_get(prefs_model(app), "ReduceMotion");
        float progress = tiku_motion_progress(&app->activation_motion,
                                              enabled ? tiku_trk_now() : 0);

        tiku_motion_outline(app->surf, app->activation_rect, progress);
        if (app->activation_motion.active) {
            desktop_damage(app,
                tiku_motion_outline_bounds(app->activation_rect));
        }
    }
    if (app->qdrag_on) {
        /* The dragged query icon, under the pointer (Q-034). */
        (void)tiku_icon_paint(app->surf, "query", app->ptr_x - 16,
                                  app->ptr_y - 16, 32);
    }
    if (app->nav_on && app->popup != NULL) {
        draw_nav_levels(app, app->popup_frame.x + app->popup_frame.w - 4,
                        app->popup_frame.y + 4);
    } else if (app->spring_nav_on) {
        /* The spring's levels start beside the sprung pose (PVS-051). */
        draw_nav_levels(app, app->spring_nav_x, app->spring_nav_y);
    }
    tiku_trk_status_overlay_draw(&app->status_overlay);
}

#define MENU_FADE_US TIKU_MOTION_MENU_US

/**
 * @brief One menu, fading in where it stands, so its rows take clicks
 *        from the first frame.
 *
 * With no display, or with Reduce motion on, the menu is drawn complete
 * at once.
 */
static void
draw_popup_menu(desktop_t *app, tiku_menu_t *menu, tiku_rect_t frame,
                int64_t since)
{
    int64_t age = tiku_trk_now() - since;
    tiku_rect_t reach = { frame.x - 12, frame.y - 12,
                          frame.w + 24, frame.h + 24 };
    tiku_rect_t captured = {0};
    tiku_surface_t *under;
    float t;

    if (app->host == NULL || age < 0 || age >= MENU_FADE_US ||
        tiku_trk_prefs_get(prefs_model(app), "ReduceMotion")) {
        tiku_menu_draw(menu, app->surf, frame);
        return;
    }
    t = (float)age / (float)MENU_FADE_US;
    under = tiku_motion_fade_begin(app->surf, reach, t, &captured);
    tiku_menu_draw(menu, app->surf, frame);
    tiku_motion_fade_end(app->surf, under, captured, t);
    desktop_damage(app, reach);
}

/**
 * @brief Draw the open menu and its open submenu, each fading in, over the
 *        bars.
 */
void
desktop_draw_popup(desktop_t *app)
{
    if (app->popup != NULL) {
        if (app->popup != app->popup_seen) {
            app->popup_seen = app->popup;
            app->popup_since = tiku_trk_now();
        }
        /* The menus are rebuilt on every draw, which clears the hot row,
         * so it is set again from the pointer here. */
        (void)tiku_menu_track(app->popup, app->popup_frame,
                                  app->ptr_x, app->ptr_y);
        draw_popup_menu(app, app->popup, app->popup_frame,
                        app->popup_since);
        if (app->popup_sub != NULL) {
            if (app->popup_sub != app->popup_sub_seen) {
                app->popup_sub_seen = app->popup_sub;
                app->popup_sub_since = tiku_trk_now();
            }
            (void)tiku_menu_track(app->popup_sub,
                                      app->popup_sub_frame, app->ptr_x,
                                      app->ptr_y);
            draw_popup_menu(app, app->popup_sub, app->popup_sub_frame,
                            app->popup_sub_since);
        } else {
            app->popup_sub_seen = NULL;
        }
    } else {
        app->popup_seen = NULL;
        app->popup_sub_seen = NULL;
    }
}

/**
 * @brief Rebuild the shared menu model for the folder that owns the open
 *        menu, and draw the global bar from the focused folder's published
 *        menus.
 */
void
desktop_draw_bars(desktop_t *app)
{
    /* Drawing another folder rebuilt the one shared menu model for that
     * folder: rebuild it for the folder that owns the popup, before it is
     * drawn or hit. */
    if (app->bar_fl == NULL && app->bar_open < 0 && app->popup != NULL &&
        app->popup_fl != NULL) {
        tiku_trk_sc_ctx_t c;

        build_open_with(app, app->popup_fl);
        build_per_type(app, app->popup_fl);
        c = menu_ctx(app, app->popup_fl, 0u);
        tiku_trk_cwmenu_rebuild(&app->menus, &c);
    }
    if (app->bar_fl != NULL && app->bar_open >= 0) {
        tiku_trk_sc_ctx_t c;

        build_open_with(app, app->bar_fl);
        build_per_type(app, app->bar_fl);
        c = menu_ctx(app, app->bar_fl, 0u);
        tiku_trk_cwmenu_rebuild(&app->menus, &c);
        app->menus.bar.open = app->bar_open;
        if (app->bar_open < app->menus.bar.count) {
            app->popup = app->menus.bar.menu[app->bar_open];
        }
    }
    if (app->gbar_on == 1) {
        /* The focused folder's menus are rebuilt and published, and the
         * strip draws only what was published: the same data an
         * application's window sends. */
        tiku_rect_t strip;
        tiku_window_t *fw = tiku_workspace_focused(app->desk);
        folder_t *ffl = folder_of(app, fw);

        if (ffl != NULL) {
            tiku_trk_sc_ctx_t gc;

            build_open_with(app, ffl);
            build_per_type(app, ffl);
            gc = menu_ctx(app, ffl, 0u);
            tiku_trk_cwmenu_rebuild(&app->menus, &gc);
            publish_folder_menus(app, ffl);
        }
        rebuild_global_bar(app);
        strip = global_bar_rect(app);
        tiku_fill(app->surf, strip, TIKU_C_PANEL);
        tiku_hline(app->surf, strip.x, strip.y + strip.h - 1,
                        strip.w, tiku_tint(TIKU_C_PANEL, 1.40f));
                tiku_menubar_draw(&app->gbar, app->surf, strip);
        global_bar_status_draw(app, strip);
        if (app->gbar_open >= 0 && app->gbar_open < app->gbar.count) {
            app->popup = app->gbar.menu[app->gbar_open];
            app->popup_frame = tiku_menubar_panel(&app->gbar, strip,
                (tiku_rect_t){ 0, 0, app->surf->w, app->surf->h });
        }
    }
}

/**
 * @brief Draw the drag in progress: up to four of the carried items' icons
 *        at the pointer, faded, and a plus badge when the drop would copy.
 */
void
desktop_draw_drag(desktop_t *app)
{
    if (tiku_trk_drag_active(&app->drag)) {
        /* The icons are translucent, and fainter for more than three
         * items, so what is under them stays readable (IV-053). */
        int i, shown = (app->drag.count < 4) ? app->drag.count : 4;
        int alpha = (app->drag.count > 3) ? 120 : 168;

        for (i = 0; i < shown; i++) {
            tiku_rect_t box;
            char art[TIKU_NAME_MAX];
            tiku_model_t dm;

            if (app->backend->ops->stat(app->backend, app->drag.path[i],
                                        &dm) != 0) {
                continue;
            }
            {
                const char *app_art = desktop_app_art(app, app->drag.path[i]);

                if (app_art != NULL) {
                    snprintf(art, sizeof art, "%s", app_art);
                } else {
                    (void)tiku_trk_nodeicon_resolve(&app->nicons, app->store,
                                                    &dm, art, sizeof art);
                }
            }
            box.x = app->drag.x - 16 + i * 6;
            box.y = app->drag.y - 16 + i * 6;
            box.w = 32;
            box.h = 32;
            (void)tiku_icon_paint(app->surf, art, box.x, box.y, 32);
            /* The fade is a tint of the backdrop colour over the icon's
             * own pixels. */
            tiku_paint_tint(app->surf, box, app->backdrop_color,
                                255 - alpha);
            if (app->drag.count > 3) {
                /* For more than three items, each 4-pixel edge band is
                 * tinted once more, the corners twice, so the picture's
                 * edge fades (PVS-034). */
                tiku_rect_t edge = box;
                edge.w = 4;
                tiku_paint_tint(app->surf, edge, app->backdrop_color, 96);
                edge.x = box.x + box.w - 4;
                tiku_paint_tint(app->surf, edge, app->backdrop_color, 96);
                edge = box;
                edge.h = 4;
                tiku_paint_tint(app->surf, edge, app->backdrop_color, 96);
                edge.y = box.y + box.h - 4;
                tiku_paint_tint(app->surf, edge, app->backdrop_color, 96);
            }
        }
        /* When the drop under the pointer would copy (onto another
         * store, or with Option held), a plus badge is drawn on the
         * picture (PVS-036). */
        if (app->drag_target.folder != NULL) {
            char dest[TIKU_PATH_MAX];
            unsigned dmods = 0u;
            int same;

            if (app->drag_target.row >= 0) {
                tiku_trk_pose_t *p = tiku_trk_poseview_at(
                    app->drag_target.folder->view, app->drag_target.row);

                snprintf(dest, sizeof dest, "%s",
                         (p != NULL) ? p->model.path : "");
            } else {
                snprintf(dest, sizeof dest, "%s",
                         app->drag_target.folder->path);
            }
            if ((app->drag_live_mods & TIKU_MOD_OPTION) != 0u) {
                dmods |= TIKU_TRK_DRAG_MOD_COPY;
            }
            if ((app->drag_live_mods & TIKU_MOD_CMD) != 0u) {
                dmods |= TIKU_TRK_DRAG_MOD_LINK;
            }
            same = dest[0] != '\0' &&
                   tiku_fs_drop_action(app->fs, app->drag.path[0],
                                           dest, 0) == TIKU_OP_MOVE;
            if (dest[0] != '\0' &&
                tiku_trk_drag_action(&app->drag, dest, dmods, same) ==
                    TIKU_TRK_DROP_COPY) {
                tiku_rect_t badge = { app->drag.x + 10,
                                           app->drag.y + 10, 13, 13 };
                tiku_rect_t stroke;

                tiku_fill(app->surf, badge,
                               TIKU_RGB(255, 255, 190));
                tiku_frame(app->surf, badge, TIKU_C_TEXT);
                stroke = (tiku_rect_t){ badge.x + 3, badge.y + 6,
                                             7, 1 };
                tiku_fill(app->surf, stroke, TIKU_C_TEXT);
                stroke = (tiku_rect_t){ badge.x + 6, badge.y + 3,
                                             1, 7 };
                tiku_fill(app->surf, stroke, TIKU_C_TEXT);
            }
        }
    }
}

/**
 * @brief Draw a column being dragged off the title strip as a dotted
 *        outline at the pointer (TS-008).
 */
void
desktop_draw_strip_drag(desktop_t *app)
{
    if (tiku_trk_titleview_tracking(&app->tstrip) == TIKU_TRK_TRACK_DRAG &&
        app->tstrip.removed) {
        /* The outline uses the folder's column-strip height. */
        int wds = app->tstrip.removed_width;
        int x0 = app->strip_ptr_x - wds / 2;
        int y0 = app->strip_ptr_y - TIKU_TRK_TITLE_HEIGHT / 2;
        int d;

        for (d = 0; d < wds; d += 4) {
            tiku_rect_t dash = { x0 + d, y0, 2, 1 };

            tiku_fill(app->surf, dash, TIKU_RGB(0, 0, 0));
            dash.y = y0 + TIKU_TRK_TITLE_HEIGHT - 1;
            tiku_fill(app->surf, dash, TIKU_RGB(0, 0, 0));
        }
        for (d = 0; d < TIKU_TRK_TITLE_HEIGHT; d += 4) {
            tiku_rect_t dash = { x0, y0 + d, 1, 2 };

            tiku_fill(app->surf, dash, TIKU_RGB(0, 0, 0));
            dash.x = x0 + wds - 1;
            tiku_fill(app->surf, dash, TIKU_RGB(0, 0, 0));
        }
    }
}
