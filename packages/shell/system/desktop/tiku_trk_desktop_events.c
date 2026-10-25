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
}

/** @brief A button went down: the surface under the pointer answers. */
void
desktop_pointer_down(desktop_t *app, const tiku_event_t *ev, desktop_loop_t *lp)
{
    (void)lp;
    deliver(app, ev);
}

/**
 * @brief The pointer moved: record where it is, choose the cursor's shape
 *        for what is under it, hand the move to the responders, and show
 *        or hide a device tray cell's tip.
 */
void
desktop_pointer_move(desktop_t *app, const tiku_event_t *ev, desktop_loop_t *lp)
{
    int was_x = app->ptr_x, was_y = app->ptr_y;

    (void)lp;
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
}

/** @brief A button came up: the holder of the capture answers first. */
void
desktop_pointer_up(desktop_t *app, const tiku_event_t *ev, desktop_loop_t *lp)
{
    (void)lp;
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
            if (app->dirty) {
                int64_t frame_due = app->last_present+16667-tiku_trk_now();
                if (frame_due < nap) nap = frame_due;
            }
            if (app->cq_head == app->cq_tail)
                tiku_host_wait(app->host, nap);
        }
    }
}
