/*
 * Desktop shell and file manager.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_desktop_windows.c - the workspace as a responder: every window's
 * frame (its tab, its close and zoom boxes, its resize corner), the windows
 * that answer their own content (preferences, panels, embedded and remote
 * applications, the Find window), their keys, and the wheel.
 *
 * A folder window's content belongs to the folders responder, and an Info
 * window's parts to the Info responder.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_shell_product.h"
#include "tiku_trk_desktop_private.h"
#include <limits.h>

/** @brief Send an embedded or remote application's window a resize event
 *         carrying its content size. */
static void
resize_deliver(desktop_t *app, tiku_window_t *w)
{
    tiku_rect_t rc = tiku_window_content(w);
    struct embed_win *ew = embed_win_of_window(app, w);
    tiku_event_t rev;

    memset(&rev, 0, sizeof rev);
    rev.type = TIKU_EVENT_RESIZE;
    rev.width = rc.w;
    rev.height = rc.h;
    if (ew != NULL) {
        (void)embed_deliver(ew, &rev);
    } else if (tiku_remote_owner(&app->remote, w) != NULL) {
        tiku_remote_event(&app->remote, w, &rev);
    }
}

/**
 * @brief Act on a close or zoom box released over the box it was pressed in.
 *
 * Zoom fits a folder window to its content, and toggles an application's
 * window between the desk's area and its frame before.  Close closes the
 * window; with Cmd, every folder window and the Find window; with Option,
 * a folder window and every window on a folder inside it.
 */
static void
tab_box_act(desktop_t *app, tiku_window_t *w, int hit, unsigned mods)
{
    if (hit == TIKU_HIT_ZOOM) {
        folder_t *fl = folder_of(app, w);

        if (fl != NULL) {
            tiku_sound_play(TIKU_SOUND_WINDOW_ZOOM);
            (void)tiku_workspace_zoom(app->desk, w,
                                     tiku_trk_folder_window_fit_width(fl),
                                     tiku_trk_folder_window_fit_height(fl));
        } else if (w && (tiku_remote_owner(&app->remote, w) != NULL ||
                         embed_win_of_window(app, w) != NULL)) {
            /* A program's window: the whole desk, or back to where it
             * was.  The program hears the new size either way. */
            tiku_trk_rect_t area =
                tiku_trk_desktop_view_area(&app->desktop_view);

            if (area.y + area.h > app->surf->h)
                area.h = app->surf->h - area.y;
            if (tiku_trk_deskbar_dock(app->deskbar) == TIKU_TRK_DB_BOTTOM) {
                tiku_rect_t bar = tiku_trk_deskbar_frame(app->deskbar);
                area.h = bar.y - area.y - 4;
            }
            if (w->zoom_valid) {
                w->frame = w->saved_zoom;
                w->zoom_valid = 0;
            } else {
                w->saved_zoom = w->frame;
                w->zoom_valid = 1;
                w->frame = (tiku_rect_t){area.x, area.y, area.w, area.h};
            }
            resize_deliver(app, w);
            tiku_sound_play(TIKU_SOUND_WINDOW_ZOOM);
        }
        return;
    }
    if (mods & TIKU_MOD_CMD) {
        int k;

        for (k = 0; k < app->folder_count; k++) {
            if (app->folders[k].win != NULL &&
                app->folders[k].win->open) {
                close_window(app, app->folders[k].win);
            }
        }
        if (tiku_trk_find_window_handle(&app->find_window) != NULL) {
            tiku_trk_find_window_close(&app->find_window);
        }
        return;
    }
    if (mods & TIKU_MOD_OPTION) {
        folder_t *cf = folder_of(app, w);

        if (cf != NULL) {
            close_windows_under(app, cf->path);
            return;
        }
    }
    close_window(app, w);
}

tiku_resource_error_t
desktop_window_resource_perform(void *opaque, tiku_window_t *window,
    tiku_trk_window_action_t action, int a, int b)
{
    desktop_t *app=opaque; struct embed_win *embedded=embed_win_of_window(app,window);
    int remote=tiku_remote_owner(&app->remote,window)!=NULL;
    if (action==TIKU_TRK_WINDOW_CLOSE) {
        tiku_event_t event={.type=TIKU_EVENT_KEY_DOWN,.key='w',.modifiers=TIKU_MOD_CMD};
        if (remote) return tiku_remote_request_close(&app->remote,window)?TIKU_RESOURCE_OK:TIKU_RESOURCE_BUSY;
        if (embedded) { (void)embed_deliver(embedded,&event); return TIKU_RESOURCE_OK; }
        uint64_t generation=window->generation;
        if (window->close_requested && !window->close_requested(window,window->context)) return TIKU_RESOURCE_OK;
        if (window->open && window->generation==generation) close_window(app,window);
    } else if (action==TIKU_TRK_WINDOW_FOCUS) {
        tiku_workspace_restore(app->desk,window); tiku_workspace_activate(app->desk,window);
    } else if (action==TIKU_TRK_WINDOW_MINIMIZE) {
        tiku_workspace_minimize(app->desk,window,tiku_trk_deskbar_frame(app->deskbar));
        if (tiku_workspace_focused(app->desk)==window) tiku_workspace_deactivate(app->desk,window);
    } else if (action==TIKU_TRK_WINDOW_RESTORE) {
        if (window->zoom_valid) { window->frame=window->saved_zoom;window->zoom_valid=0;resize_deliver(app,window); }
        tiku_workspace_restore(app->desk,window); tiku_workspace_activate(app->desk,window);
    } else if (action==TIKU_TRK_WINDOW_MAXIMIZE) {
        if (!window->zoom_valid) tab_box_act(app,window,TIKU_HIT_ZOOM,0);
    } else {
        int64_t dx=(int64_t)a-(action==TIKU_TRK_WINDOW_MOVE?window->frame.x:window->frame.w);
        int64_t dy=(int64_t)b-(action==TIKU_TRK_WINDOW_MOVE?window->frame.y:window->frame.h);
        if (dx<INT_MIN || dx>INT_MAX || dy<INT_MIN || dy>INT_MAX) return TIKU_RESOURCE_VALUE;
        tiku_workspace_begin_track(app->desk,window,action==TIKU_TRK_WINDOW_MOVE?TIKU_HIT_TAB:TIKU_HIT_RESIZE,0,0);
        (void)tiku_workspace_track(app->desk,(int)dx,(int)dy); tiku_workspace_end_track(app->desk);
        window->zoom_valid=0;
        if (action==TIKU_TRK_WINDOW_RESIZE) resize_deliver(app,window);
    }
    desktop_damage_all(app); return TIKU_RESOURCE_OK;
}

/** @brief Whether the pointer is over the box that was pressed. */
static int
tab_box_under(desktop_t *app, int x, int y)
{
    tiku_window_t *hw = NULL;
    int hit = tiku_workspace_hit(app->desk, x, y, &hw);

    return hw == app->box_press && hit == app->box_hit;
}

/**
 * @brief A pressed box is drawn raised while the pointer is off it and
 *        pressed while the pointer is over it.
 * @return whether a box is pressed at all.
 */
static int
tab_box_move(desktop_t *app, const tiku_event_t *ev, tiku_rect_t *damage)
{
    int on;

    if (app->box_press == NULL) {
        return 0;
    }
    on = tab_box_under(app, ev->x, ev->y);
    if ((app->box_press->pressed != 0) != on) {
        app->box_press->pressed = on ? app->box_hit : 0;
        *damage = tiku_trk_damage_all();
    }
    return 1;
}

/**
 * @brief End a box press: the box acts only when released over itself.
 * @return whether a box was pressed.
 */
static int
tab_box_release(desktop_t *app, const tiku_event_t *ev, tiku_rect_t *damage)
{
    tiku_window_t *w = app->box_press;
    int hit = app->box_hit, on;

    if (w == NULL) {
        return 0;
    }
    on = tab_box_under(app, ev->x, ev->y);
    w->pressed = 0;
    app->box_press = NULL;
    *damage = tiku_trk_damage_all();
    if (on && w->open) {
        tab_box_act(app, w, hit, app->box_mods);
    }
    return 1;
}

/**
 * @brief Offer a press to open window @p w, wherever the press is; while
 *        an alert is up nothing is offered.  A primary press the window
 *        takes is tracked to it until the release.
 * @return 1 when the window's handler took the press.
 */
static int
offer_press(desktop_t *app, tiku_window_t *w, const tiku_event_t *ev)
{
    tiku_event_t window_event = { 0 };

    if (w == NULL || !w->open || alert_model(app)->open) {
        return 0;
    }
    window_event.type = TIKU_EVENT_POINTER_DOWN;
    window_event.time_us = tiku_trk_now();
    window_event.x = ev->x;
    window_event.y = ev->y;
    window_event.button = ev->button;
    if (!tiku_window_send(w, &window_event)) {
        return 0;
    }
    if (ev->button == 1) {
        app->prefs_track = w;
    }
    return 1;
}

/**
 * @brief The wheel over a folder scrolls it, three rows in a list and three
 *        row heights in an icon mode.  Over the fonts window, or an
 *        embedded or remote application's window, it is sent as a wheel
 *        event, in window coordinates for an application.
 * @return 1.
 */
static int
wheel(desktop_t *app, const tiku_event_t *ev, tiku_rect_t *damage)
{
    tiku_window_t *ww = NULL;
    folder_t *wfl;

    (void)tiku_workspace_hit(app->desk, ev->x, ev->y, &ww);
    wfl = folder_of(app, ww);
    if (wfl != NULL) {
        int step = (ev->button == 4u) ? -1 : 1;

        if (tiku_trk_poseview_mode(wfl->view) != TIKU_TRK_MODE_LIST) {
            int ox = 0, oy = 0;

            tiku_trk_poseview_origin(wfl->view, &ox, &oy);
            tiku_trk_poseview_set_origin(wfl->view, ox,
                                         oy + step * 3 * app->row_h);
        } else {
            tiku_trk_poseview_scroll_to(wfl->view,
                tiku_trk_poseview_scroll(wfl->view) + step * 3);
        }
        *damage = tiku_trk_damage_all();
    } else if (ww != NULL) {
        struct embed_win *ew = embed_win_of_window(app, ww);
        tiku_rect_t rc = tiku_window_content(ww);
        tiku_event_t we;

        memset(&we, 0, sizeof we);
        we.type = TIKU_EVENT_WHEEL;
        we.x = ev->x - rc.x;
        we.y = ev->y - rc.y;
        we.dy = (ev->button == 4u) ? -3 : 3;
        we.modifiers = ev->modifiers;
        we.time_us = tiku_trk_now();
        if (ww == tiku_trk_fonts_window_handle(&app->fonts_window)) {
            we.x = ev->x;
            we.y = ev->y;
            (void)tiku_window_send(ww, &we);
            *damage = tiku_trk_damage_all();
        } else if (ew != NULL) {
            if (embed_deliver(ew, &we)) {
                embed_stop(ew->slot);
            }
            *damage = tiku_trk_damage_all();
        } else if (tiku_remote_owner(&app->remote, ww) != NULL) {
            tiku_remote_event(&app->remote, ww, &we);
            *damage = tiku_trk_damage_all();
        }
    }
    return 1;
}

/**
 * @brief Every point: an open preferences window is asked first wherever a
 *        press is, and a press no window takes is returned untaken.
 */
static int
windows_hit(void *self, int x, int y)
{
    (void)self; (void)x; (void)y;
    return 1;
}

/**
 * @brief A press: the device preferences popup, an open preferences
 *        window, then the window under the point: its frame, or its
 *        content when the window answers for itself.  A folder's content
 *        is left for the folders responder.
 */
static int
windows_press(void *self, const tiku_event_t *ev, tiku_rect_t *damage)
{
    desktop_t *app = self;
    tiku_window_t *w = NULL;
    tiku_hit_t hit;

    *damage = tiku_trk_damage_none();
#if TIKU_SHELL_BOARD_SERVICES

    if (!alert_model(app)->open && desktop_deviceprefs_popup(app)) {
        tiku_window_t *pw = desktop_deviceprefs_handle(app);
        tiku_event_t pe = *ev;

        if (ev->button == 1) app->prefs_track = pw;
        if (ev->button == 4 || ev->button == 5) {
            pe.type = TIKU_EVENT_WHEEL;
            pe.dy = ev->button == 4 ? -1 : 1;
        }
        (void)tiku_window_send(pw, &pe);
        *damage = tiku_trk_damage_all();
        return 1;
    }
#endif

    if (offer_press(app, tiku_trk_deskbar_prefs_window_handle(
                        &app->deskbar_prefs_window), ev) ||
        offer_press(app, tiku_trk_desktop_prefs_window_handle(
                        &app->desktop_prefs_window), ev)) {
        return 1;
    }
    {
        tiku_window_t *sounds =
            tiku_trk_sounds_window_handle(&app->sounds_window);
        tiku_window_t *under = NULL;

        (void)tiku_workspace_hit(app->desk, ev->x, ev->y, &under);
        if (under == sounds && offer_press(app, sounds, ev)) {
            return 1;
        }
    }
    if (offer_press(app, tiku_trk_prefs_window_handle(&app->prefs_window),
                    ev)) {
        return 1;
    }
    if (ev->button == 4u || ev->button == 5u) {
        return wheel(app, ev, damage);
    }
    hit = tiku_workspace_hit(app->desk, ev->x, ev->y, &w);
    if (tiku_trk_find_window_popup_open(&app->find_window)) {
        w = tiku_trk_find_window_handle(&app->find_window);
        hit = TIKU_HIT_CONTENT;
    }
    if (w == NULL) {
#if TIKU_SHELL_NOTE_TABS

        tiku_window_t *focused = tiku_workspace_focused(app->desk);
        if (ev->button == 1 && focused && focused->auto_hide_tab) {
            tiku_workspace_deactivate(app->desk, focused);
            *damage = tiku_trk_damage_all();
        }
#endif

        return 0;               /* the desk itself is the folders' */
    }
    if (hit != TIKU_HIT_CONTENT) {
        if (app->folder_edit.renaming &&
            app->folder_edit.rename_folder != NULL &&
            app->folder_edit.rename_folder->win != NULL &&
            app->folder_edit.rename_folder->win != w) {
            commit_rename(app, 1);
        }
        tiku_workspace_activate(app->desk, w);
        *damage = tiku_trk_damage_all();
        if (hit == TIKU_HIT_ZOOM || hit == TIKU_HIT_CLOSE) {
            w->pressed = hit;
            app->box_press = w;
            app->box_hit = hit;
            app->box_mods = ev->modifiers;
        } else if (hit == TIKU_HIT_TAB || hit == TIKU_HIT_RESIZE) {
            app->resize_track = (hit == TIKU_HIT_RESIZE) ? w : NULL;
            tiku_workspace_begin_track(app->desk, w, hit, ev->x, ev->y);
        }
        return 1;
    }
#if TIKU_SHELL_BOARD_SERVICES

    if (w == desktop_deviceprefs_handle(app) ||
        w == desktop_startup_handle(app) ||
        w == desktop_associations_handle(app) ||
        w == tiku_trk_open_with_window_handle(&app->open_with_window) ||
        w == desktop_forcequit_handle(app) ||
        w == tiku_trk_fonts_window_handle(&app->fonts_window)) {
#else

    if (w == desktop_startup_handle(app) ||
        w == desktop_associations_handle(app) ||
        w == tiku_trk_open_with_window_handle(&app->open_with_window) ||
        w == desktop_forcequit_handle(app) ||
        w == tiku_trk_fonts_window_handle(&app->fonts_window)) {
#endif

        tiku_workspace_activate(app->desk, w);
        if (ev->button == 1) app->prefs_track = w;
        (void)tiku_window_send(w, ev);
        *damage = tiku_trk_damage_all();
        return 1;
    }
    if (w == tiku_trk_select_window_handle(&app->select_window) ||
        w == tiku_trk_panel_window_handle(&app->file_panel)) {
        if (ev->button == 1) app->prefs_track = w;
        (void)tiku_window_send(w, ev);
        *damage = tiku_trk_damage_all();
        return 1;
    }
    if (panel_of_window(app, w) != NULL) {
        tiku_workspace_activate(app->desk, w);
        (void)tiku_window_send(w, ev);
        *damage = tiku_trk_damage_all();
        return 1;
    }
    if (embed_win_of_window(app, w) != NULL) {
        struct embed_win *ew = embed_win_of_window(app, w);
        tiku_event_t rev = *ev;
        tiku_rect_t rc = tiku_window_content(w);

        tiku_workspace_activate(app->desk, w);
        rev.x -= rc.x;
        rev.y -= rc.y;
        if (embed_deliver(ew, &rev)) {
            embed_stop(ew->slot);
        } else {
            app->embed_press = ew;
        }
        *damage = tiku_trk_damage_all();
        return 1;
    }
    if (tiku_remote_owner(&app->remote, w) != NULL) {
        tiku_event_t rev = *ev;
        tiku_rect_t rc = tiku_window_content(w);

        tiku_workspace_activate(app->desk, w);
        rev.x -= rc.x;
        rev.y -= rc.y;
        tiku_remote_event(&app->remote, w, &rev);
        app->remote_press = w;
        *damage = tiku_trk_damage_all();
        return 1;
    }
    if (w == tiku_trk_find_window_handle(&app->find_window)) {
        if (ev->button == 1u &&
            tiku_trk_find_window_icon_at(&app->find_window, ev->x, ev->y)) {
            app->qdrag_on = 1;  /* the query's icon, picked up (Q-034) */
        } else {
            if (ev->button == 1) app->prefs_track = w;
            (void)tiku_window_send(w, ev);
        }
        *damage = tiku_trk_damage_all();
        return 1;
    }
    return 0;                   /* a folder's content, or nobody's */
}

/**
 * @brief A move: the window a press is tracked to gets it and nobody
 *        else; otherwise an application's window under the pointer is
 *        sent the move, and a tab or corner being dragged moves or resizes
 *        its window.
 */
static int
windows_move(void *self, const tiku_event_t *ev, tiku_rect_t *damage)
{
    desktop_t *app = self;

    *damage = tiku_trk_damage_none();
    if (tab_box_move(app, ev, damage)) {
        return 1;
    }
    if (app->prefs_track != NULL && app->prefs_track->open) {
        tiku_event_t pe = *ev;

        if (alert_model(app)->open) pe.type = TIKU_EVENT_DEACTIVATED;
        (void)tiku_window_send(app->prefs_track, &pe);
        if (alert_model(app)->open) app->prefs_track = NULL;
#if TIKU_SHELL_BOARD_SERVICES

        *damage = tiku_trk_damage_all();
        return 1;
    }
    if (!alert_model(app)->open && desktop_deviceprefs_popup(app)) {
        (void)tiku_window_send(desktop_deviceprefs_handle(app), ev);
#endif

        *damage = tiku_trk_damage_all();
        return 1;
    }
    if (tiku_trk_sounds_window_tracking(&app->sounds_window)) {
        /* The volume knob is held: every move goes to it. */
        (void)tiku_window_send(
            tiku_trk_sounds_window_handle(&app->sounds_window), ev);
        *damage = tiku_trk_damage_all();
        return 1;
    }
    if (panel_tracking(app) != NULL) {
        (void)tiku_window_send(panel_tracking(app)->window, ev);
        return 1;
    }
    if (app->embed_press != NULL && app->embed_press->window != NULL) {
        struct embed_win *ew = app->embed_press;
        tiku_event_t rev = *ev;
        tiku_rect_t rc = tiku_window_content(ew->window);

        rev.x -= rc.x;
        rev.y -= rc.y;
        if (embed_deliver(ew, &rev)) {
            embed_stop(ew->slot);
        }
        *damage = tiku_trk_damage_all();
        return 1;
    }
    if (app->remote_press != NULL &&
        tiku_remote_owner(&app->remote, app->remote_press) != NULL) {
        tiku_event_t rev = *ev;
        tiku_rect_t rc = tiku_window_content(app->remote_press);

        rev.x -= rc.x;
        rev.y -= rc.y;
        tiku_remote_event(&app->remote, app->remote_press, &rev);
        *damage = tiku_trk_damage_all();
        return 1;
    }
    if (!tiku_trk_drag_active(&app->drag)) {
        tiku_window_t *hw = NULL;

        if (tiku_workspace_hit(app->desk, ev->x, ev->y, &hw) ==
                TIKU_HIT_CONTENT && hw != NULL) {
            struct embed_win *hew = embed_win_of_window(app, hw);
            tiku_event_t rev = *ev;
            tiku_rect_t rc = tiku_window_content(hw);

            rev.x -= rc.x;
            rev.y -= rc.y;
            if (hew != NULL) {
                if (embed_deliver(hew, &rev)) {
                    embed_stop(hew->slot);
                }
            } else if (tiku_remote_owner(&app->remote, hw) != NULL) {
                tiku_remote_event(&app->remote, hw, &rev);
            }
        }
    }
    if (tiku_workspace_tracking(app->desk)) {
        tiku_window_t *moving = tiku_workspace_moving(app->desk);
        tiku_rect_t before = moving ? moving->frame : (tiku_rect_t){0};

        if (tiku_workspace_track(app->desk, ev->x, ev->y)) {
            if (app->resize_track != NULL) {
                /* The frame changed size: the window is told now, so its
                 * page follows the corner. */
                resize_deliver(app, app->resize_track);
            }
            *damage = moving ? tiku_rect_union(before, moving->frame)
                             : tiku_trk_damage_all();
        }
    }
    return 0;
}

/**
 * @brief A release: a pressed box, the tracked window and the picked-up
 *        query icon each end what their press began; otherwise a tab or
 *        corner being dragged is released, and a resized window is told
 *        its new size.
 */
static int
windows_release(void *self, const tiku_event_t *ev, tiku_rect_t *damage)
{
    desktop_t *app = self;

    *damage = tiku_trk_damage_none();
    if (tab_box_release(app, ev, damage)) {
        return 1;
    }
    if (app->prefs_track != NULL) {
        tiku_window_t *pw = app->prefs_track;
        tiku_event_t pe = *ev;

        if (alert_model(app)->open) pe.type = TIKU_EVENT_DEACTIVATED;
        if (ev->button == 1) app->prefs_track = NULL;
        if (pw->open) (void)tiku_window_send(pw, &pe);
        *damage = tiku_trk_damage_all();
        return 1;
    }
    if (tiku_trk_sounds_window_tracking(&app->sounds_window)) {
        (void)tiku_window_send(
            tiku_trk_sounds_window_handle(&app->sounds_window), ev);
        *damage = tiku_trk_damage_all();
        return 1;
    }
    if (panel_tracking(app) != NULL) {
        (void)tiku_window_send(panel_tracking(app)->window, ev);
        return 1;
    }
    if (app->embed_press != NULL) {
        struct embed_win *ew = app->embed_press;

        app->embed_press = NULL;
        if (ew->slot->running && ew->window != NULL) {
            tiku_event_t rev = *ev;
            tiku_rect_t rc = tiku_window_content(ew->window);

            rev.x -= rc.x;
            rev.y -= rc.y;
            if (embed_deliver(ew, &rev)) {
                embed_stop(ew->slot);
            }
        }
        *damage = tiku_trk_damage_all();
        return 1;
    }
    if (app->remote_press != NULL) {
        if (tiku_remote_owner(&app->remote, app->remote_press) != NULL) {
            tiku_event_t rev = *ev;
            tiku_rect_t rc = tiku_window_content(app->remote_press);

            rev.x -= rc.x;
            rev.y -= rc.y;
            tiku_remote_event(&app->remote, app->remote_press, &rev);
        }
        app->remote_press = NULL;
        *damage = tiku_trk_damage_all();
        return 1;
    }
    if (app->qdrag_on) {
        /* The query's icon let go over a folder: a menu of what to do
         * with it there (Q-034). */
        tiku_window_t *tw = NULL;
        folder_t *tfl;

        (void)tiku_workspace_hit(app->desk, ev->x, ev->y, &tw);
        tfl = (tw != NULL) ? folder_of(app, tw) : desktop_folder(app);
        app->qdrag_on = 0;
        if (tfl != NULL && tfl->view != NULL &&
            (tw == NULL ||
             tw != tiku_trk_find_window_handle(&app->find_window))) {
            tiku_model_t dm;
            int ro = 1;

            if (app->backend->ops->stat(app->backend, tfl->path, &dm) == 0) {
                ro = !tiku_model_is_mutable_container(&dm);
            }
            snprintf(app->qdrop_dest, sizeof app->qdrop_dest, "%s",
                     tfl->path);
            app->popup = tiku_trk_cwmenu_query_drop(&app->menus, ro);
            app->popup_fl = tfl;
            app->popup_sub = NULL;
            app->popup_frame = tiku_menu_place(app->popup, ev->x, ev->y,
                (tiku_rect_t){ 0, 0, app->surf->w, app->surf->h });
        }
        *damage = tiku_trk_damage_all();
        return 1;
    }
    tiku_workspace_end_track(app->desk);
    if (app->resize_track != NULL) {
        resize_deliver(app, app->resize_track);
        app->resize_track = NULL;
        *damage = tiku_trk_damage_all();
    }
    return 0;
}

/**
 * @brief The focused window's keys: a panel's or a search's field, an
 *        application's or a remote window, the inline editor, and Cmd-Q
 *        closing a settings window or the desk.
 */
static int
windows_key(void *self, const tiku_event_t *ev, tiku_rect_t *damage)
{
    desktop_t *app = self;
    unsigned key = ev->key;
    tiku_window_t *w = tiku_workspace_focused(app->desk);
    folder_t *fl = folder_of(app, w);

    *damage = tiku_trk_damage_none();
    if (ev->type != TIKU_EVENT_KEY_DOWN) {
        return 0;
    }
    if (panel_key(app, key, key_char(ev))) {
        return 1;
    }
    if (tiku_trk_panel_window_key(&app->file_panel, key,
                                  ev->modifiers,
                                  key_char(ev))) {
        if (tiku_trk_panel_window_refused(&app->file_panel)) {
            snprintf(app->status, sizeof app->status,
                     "The clipboard does not fit this name.");
            desktop_damage_all(app);
        }
        return 1;
    }
    if (tiku_workspace_focused(app->desk) ==
            tiku_trk_find_window_handle(&app->find_window) &&
        app->find_window.model.focus ==
            TIKU_TRK_FIND_FOCUS_NAME &&
        key == ' ' && (ev->modifiers & TIKU_MOD_CMD)) {
        /* Cmd-Space completes the name against the first
         * scoped place, or home when the search is everywhere
         * (NeXT UIG ch3). */
        tiku_trk_find_t *fm = &app->find_window.model;
        char dir[TIKU_PATH_MAX];
        char full[TIKU_NAME_MAX];
        const char *home = getenv("HOME");
        int pi, chosen = -1;

        for (pi = 0; pi < fm->place_count; pi++) {
            if (fm->place_mask & (1u << pi)) {
                chosen = pi;
                break;
            }
        }
        if (chosen >= 0) {
            snprintf(dir, sizeof dir, "/%s", fm->place[chosen]);
        } else {
            snprintf(dir, sizeof dir, "%s",
                     (home != NULL && home[0] != '\0') ? home
                                                        : "/");
        }
        if (tiku_trk_panel_complete(app->backend, dir,
                fm->name_text, full, sizeof full) > 0) {
            snprintf(fm->name_text, sizeof fm->name_text,
                     "%s", full);
        }
        desktop_damage_all(app);
        return 1;
    }
    {
        tiku_window_t *rfw =
            tiku_workspace_focused(app->desk);

        if (embed_win_of_window(app, rfw) != NULL) {
            struct embed_win *ew =
                embed_win_of_window(app, rfw);
            tiku_event_t rev = *ev;

            /* Cmd-W closes the focused window before the
             * application sees the key, so no application can
             * keep its window open from the keyboard. */
            if (key == 'w' &&
                (ev->modifiers & TIKU_MOD_CMD)) {
                embed_drop_window(ew, 1);
                desktop_damage_all(app);
                return 1;
            }
            if (embed_deliver(ew, &rev)) {
                embed_stop(ew->slot);
            }
            desktop_damage_all(app);
            return 1;
        }
        if (tiku_remote_owner(&app->remote, rfw) != NULL) {
            tiku_event_t rev = *ev;

            tiku_remote_event(&app->remote, rfw, &rev);
            desktop_damage_all(app);
            return 1;
        }
    }
    if (tiku_trk_find_window_key(&app->find_window, key,
                                 key_char(ev))) {
        return 1;
    }
    if (app->finding) {
        if (key == TIKU_KEY_RETURN) {
            app->finding = 0;
            run_find(app, fl);
        } else if (key == TIKU_KEY_ESCAPE) {
            app->finding = 0;
            app->status[0] = '\0';
            desktop_damage_all(app);
        } else if (key == TIKU_KEY_BACKSPACE) {
            if (app->find_len > 0) {
                tiku_text_input_backspace(app->find_buf, sizeof app->find_buf);
                app->find_len = (int)strlen(app->find_buf);
            }
            desktop_damage_all(app);
        } else {
            char typed = key_char(ev);

            if (typed >= 0x20 && typed < 0x7F &&
                app->find_len < (int)sizeof app->find_buf - 1) {
                app->find_buf[app->find_len++] = typed;
                app->find_buf[app->find_len] = '\0';
                desktop_damage_all(app);
            }
        }
        return 1;
    }
    if (tiku_trk_select_window_key(&app->select_window, key,
                                   ev->modifiers,
                                   key_char(ev))) {
        return 1;
    }
    if (tiku_trk_open_with_window_key(&app->open_with_window,
                                      key, ev->modifiers)) {
        desktop_damage_all(app);
        return 1;
    }
    if (tiku_trk_folder_edit_key(&app->folder_edit, fl, key,
            ev->modifiers, key_char(ev))) {
        return 1;
    }
    if (key == 'q' && (ev->modifiers & TIKU_MOD_CMD) &&
        tiku_workspace_focused(app->desk) ==
            tiku_trk_prefs_window_handle(&app->prefs_window)) {
        tiku_trk_prefs_window_close(&app->prefs_window);
        desktop_damage_all(app);
        return 1;
    }
    if (key == 'q' && (ev->modifiers & TIKU_MOD_CMD) &&
        tiku_workspace_focused(app->desk) ==
            tiku_trk_deskbar_prefs_window_handle(
                &app->deskbar_prefs_window)) {
        /* Cmd-Q in the Deskbar preferences closes them; the
         * desktop keeps running (DB-053). */
        tiku_trk_deskbar_prefs_window_close(
            &app->deskbar_prefs_window);
        desktop_damage_all(app);
        return 1;
    }
    if (key == 'q' && (ev->modifiers & TIKU_MOD_CMD) &&
        desktop_startup_handle(app) != NULL &&
        tiku_workspace_focused(app->desk) == desktop_startup_handle(app)) {
        /* Cmd-Q in the startup preferences closes them and drops the
         * draft; the desktop keeps running (DB-053). */
        desktop_startup_close(app);
        return 1;
    }
    if (key == 'q' && (ev->modifiers & TIKU_MOD_CMD) &&
        desktop_forcequit_handle(app) != NULL &&
        tiku_workspace_focused(app->desk) == desktop_forcequit_handle(app)) {
        desktop_forcequit_close(app);
        return 1;
    }
    if (key == 'q' && (ev->modifiers & TIKU_MOD_CMD) &&
        desktop_associations_handle(app) != NULL &&
        tiku_workspace_focused(app->desk) == desktop_associations_handle(app)) {
        desktop_associations_close(app);
        return 1;
    }
#if TIKU_SHELL_BOARD_SERVICES

    if (key == 'q' && (ev->modifiers & TIKU_MOD_CMD) &&
        desktop_deviceprefs_handle(app) != NULL &&
        tiku_workspace_focused(app->desk) == desktop_deviceprefs_handle(app)) {
        desktop_deviceprefs_close(app);
        return 1;
    }
#endif

    if (key == 'q' && (ev->modifiers & TIKU_MOD_CMD)) {
        begin_shutdown(app);
        return 1;
    }
    return 0;
}

/**
 * @brief Put the workspace's frames, and the windows that answer their own
 *        content, on the responder list.
 */
void
desktop_windows_attach(desktop_t *app)
{
    app->windows_responder = (tiku_trk_responder_t){
        .name = "windows", .self = app, .hit = windows_hit,
        .press = windows_press, .move = windows_move,
        .release = windows_release, .key = windows_key
    };
    (void)tiku_trk_responders_push(&app->responders, &app->windows_responder);
}
