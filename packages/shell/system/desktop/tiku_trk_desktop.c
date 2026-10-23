/*
 * Desktop shell and file manager.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_desktop.c - the desktop's entry point and event loop, and the
 * settings pushed to its windows.
 *
 * One host window holds the desktop: a backdrop, folder windows with title
 * tabs, and the Deskbar with its tray.  A folder has one window: opening
 * it again brings that window forward.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_shell_product.h"
#include "tiku_report.h"
#include "tiku_trk_desktop_private.h"
#include "tiku_watch.h"
#include "tiku_trk_session_controller.h"

/**
 * @brief Apply the settings to the running desktop: thumbnails, the global
 *        menu bar, the theme, the file panel's top, the Disks window, the
 *        title paths and the switches every folder view carries.  A change
 *        to a window's shape or palette forgets its pixels and repaints.
 */
void
sync_folder_preferences(desktop_t *app)
{
    tiku_trk_prefs_t *prefs = prefs_model(app);
    int hide = tiku_trk_prefs_get(prefs, "HideDotFiles") != 0;
    int folders_first = tiku_trk_prefs_get(prefs, "SortFolderNamesFirst") != 0;
    int filter = tiku_trk_prefs_get(prefs, "TypeAheadFiltering") != 0;
    int full_path = tiku_trk_prefs_get(prefs, "ShowFullPathInTitleBar") != 0;
    int i;

    /* The thumbnail queue's switch follows the preference (TS-053). */
    app->thumbs.enabled = tiku_trk_prefs_get(prefs,
                              "GenerateImageThumbnails") != 0;
    {
        /* One bar at the top of the screen, or one per window: a change
         * forgets every window's pixels and moves the desk's poses below
         * the strip. */
        int gon = tiku_trk_prefs_get(prefs, "GlobalMenuBar") != 0;

        if (gon != app->gbar_on) {
            app->gbar_on = gon;
            app->folder_render.global_bar = gon;
            tiku_trk_desktop_view_set_top_inset(&app->desktop_view,
                gon ? app->menus.bar.height : 0);
            tiku_trk_deskbar_set_top_inset(app->deskbar,
                gon ? app->menus.bar.height : 0);
            for (i = 0; i < app->folder_count; i++) {
                tiku_trk_folder_window_forget(&app->folders[i]);
            }
            app->gbar_open = -1;
            desktop_damage_all(app);
        }
    }
    {
        /* The theme swaps the colour table as soon as the setting
         * changes.  A change forgets every window's pixels, painted from
         * the old table, and repaints once. */
        int mode = tiku_trk_prefs_get(prefs, "DarkMode");

        if (mode < 0 || mode > 2) {
            mode = 0;               /* out of range: the default theme */
        }
        /* The Deskbar's switch shows the setting, whatever changed it. */
        tiku_trk_deskbar_set_theme(app->deskbar, mode);
        if (mode != app->theme_mode) {
            tiku_trk_folder_window_t *desk =
                tiku_trk_desktop_view_folder(&app->desktop_view);

            app->theme_mode = mode;
            tiku_theme_set(mode == 1 ? tiku_theme_dark()
                         : mode == 2 ? tiku_theme_mono() : NULL);
            for (i = 0; i < app->folder_count; i++) {
                tiku_trk_folder_window_forget(&app->folders[i]);
            }
            if (desk != NULL) {
                tiku_trk_folder_window_forget(desk);
            }
            desktop_damage_all(app);
        }
    }
    /* With DesktopFilePanelRoot on, the file panel's top is the Desktop
     * (AW-061); read every turn, so the next Up follows a change. */
    app->file_panel.model.desktop_is_root =
        tiku_trk_prefs_get(prefs, "DesktopFilePanelRoot") != 0;

    {   /* With ShowDisksIcon and MountVolumesOntoDesktop both off, a
         * window on the root closes, as the Disks icon goes (PVN-058). */
        int disks = tiku_trk_prefs_get(prefs, "ShowDisksIcon") != 0 ||
                    tiku_trk_prefs_get(prefs,
                                       "MountVolumesOntoDesktop") != 0;

        if (!disks) {
            for (i = app->folder_count - 1; i >= 0; i--) {
                folder_t *folder = &app->folders[i];

                if (folder->win != NULL && folder->win->open &&
                    strcmp(folder->path, "/") == 0) {
                    close_window(app, folder->win);
                    desktop_damage_all(app);
                }
            }
        }
    }

    if (full_path != app->applied_full_path) {
        /* Every open window is re-titled when ShowFullPathInTitleBar
         * changes (TS-047). */
        app->applied_full_path = full_path;
        for (i = 0; i < app->folder_count; i++) {
            folder_t *folder = &app->folders[i];

            if (folder->view != NULL && folder->win != NULL) {
                tiku_trk_folder_title_for(prefs, folder->path,
                                          folder->win->title,
                                          sizeof folder->win->title);
            }
        }
        desktop_damage_all(app);
    }

    /* Every turn, so a window opened or switched to list view since takes
     * TypeAheadFiltering too; a view that already matches is left alone
     * (TS-052). */
    for (i = 0; i < app->folder_count; i++) {
        folder_t *folder = &app->folders[i];

        if (folder->view == NULL ||
            tiku_trk_poseview_mode(folder->view) != TIKU_TRK_MODE_LIST) {
            continue;
        }
        if (filter != tiku_trk_poseview_is_filtering(folder->view)) {
            tiku_trk_poseview_set_filtering(folder->view, filter);
            desktop_damage_all(app);
        }
    }
    if (hide == app->applied_hide_dotfiles &&
        folders_first == app->applied_folders_first) {
        return;
    }
    app->applied_hide_dotfiles = hide;
    app->applied_folders_first = folders_first;
    for (i = 0; i < app->folder_count; i++) {
        folder_t *folder = &app->folders[i];

        if (folder->view == NULL) {
            continue;
        }
        tiku_trk_poseview_set_hide_dotfiles(folder->view, hide);
        tiku_trk_poseview_set_folders_first(folder->view, folders_first);
    }
    desktop_damage_all(app);
}

/**
 * @brief The desktop's entry point: parse the command line, boot, run the
 *        event loop until the desktop stops, and tear it down.
 * @return the process's exit code.
 */
int
tracker_run(int argc, char **argv)
{
    desktop_t app;
    desktop_options_t opt;
    desktop_loop_t lp;
    int code;

    /* The interface strings load first: the language comes from the
     * environment, with the built-in English as the fallback. */
    (void)tiku_strings_init(NULL, NULL);
    memset(&opt, 0, sizeof opt);
#if TIKU_SHELL_BOARD_SERVICES

    opt.baud = 115200;
#endif

    opt.argv0 = argv[0];
    memset(&lp, 0, sizeof lp);
    lp.started_at = tiku_trk_now();
    memset(&app, 0, sizeof app);
    app.theme_mode = -1;      /* unapplied: the first sync decides */
    app.gbar_on = -1;
    app.gbar_open = -1;
    tiku_menubar_init(&app.gbar);
    (void)setlocale(LC_ALL, "");
    app.bar_open = -1;
    code = desktop_parse_args(argc, argv, &opt);
    if (code >= 0) {
        return code;
    }
    code = desktop_boot(&app, &opt, &lp);
    if (code >= 0) {
        if(app.fs != NULL)tiku_trk_session_drain_filesystem(app.fs);
        tiku_trk_resource_session_close(app.resource_session);
        return code;
    }

    /* A watchdog reports a turn of the loop that never ends; each turn
     * beats it. */
        tiku_watch_install(0);
    tiku_report_ask_install();
    while (app.running) {
        tiku_watch_beat();
        desktop_turn_begin(&app, &opt, &lp);
        if(tiku_trk_resource_session_pump(app.resource_session))app.running=0;
        int64_t event_started = tiku_trk_now();
        int event_count = 0;
        for (;;) {
            tiku_event_t ev;
            int have = 0;

            memset(&ev, 0, sizeof ev);
            if (lp.sc != NULL) {
                tiku_trk_script_adapter_result_t script_result =
                    tiku_trk_script_adapter_next(&app.script_adapter, lp.sc,
                                                 &ev);

                if (script_result == TIKU_TRK_SCRIPT_ADAPTER_STOP) {
                    app.running = 0;
                    break;
                }
                if (script_result == TIKU_TRK_SCRIPT_ADAPTER_SHOT) {
                    desktop_damage_all(&app);
                    draw_all(&app);
                    (void)tiku_surface_png(
                        app.surf, app.script_adapter.shot_path);
                }
                have = script_result == TIKU_TRK_SCRIPT_ADAPTER_EVENT;
            } else if (app.cq_head != app.cq_tail) {
                /* An event injected over the conductor channel, in the
                 * order it arrived, handled as a display event is. */
                ev = app.cq[app.cq_head];
                app.cq_head = (app.cq_head + 1) %
                              (int)(sizeof app.cq / sizeof app.cq[0]);
                have = 1;
            } else if (app.host != NULL && tiku_host_poll(app.host,
                                                               &ev)) {
                have = 1;
            }
            if (!have) {
                break;
            }
            if (app.closing) {
                /* While closing, events are still read, since a display
                 * whose events go unread stops sending them, and are then
                 * dropped. */
                continue;
            }
            if (ev.type != TIKU_EVENT_TEXT_INPUT)
                (void)tiku_text_input_event(&app.legacy_commit, &ev);
            if (ev.type == TIKU_EVENT_KEY_DOWN ||
                ev.type == TIKU_EVENT_TEXT_INPUT ||
                ev.type == TIKU_EVENT_POINTER_DOWN ||
                ev.type == TIKU_EVENT_POINTER_MOVE)
                tiku_trk_loop_note_activity(app.loop);
            switch (ev.type) {
#if TIKU_SHELL_REMOTE_SERVICES

            case TIKU_EVENT_PREEDIT:
            case TIKU_EVENT_TEXT_INPUT:
                desktop_text_input(&app, &ev);
#else

            case TIKU_EVENT_TEXT_INPUT:
                desktop_text_input(&app, &ev);
#endif

                break;
            case TIKU_EVENT_CLIPBOARD:
                clip_mirror(&app);
                if (ev.key != TIKU_CLIPBOARD_OK) {
                    snprintf(app.status, sizeof app.status, "%s",
                             tiku_clipboard_status_text((int)ev.key));
                    desktop_damage_all(&app);
                }
                break;
            case TIKU_EVENT_DEACTIVATED: {
                tiku_window_t *fw = tiku_workspace_focused(app.desk);
#if TIKU_SHELL_NOTE_TABS

                if (fw && fw->auto_hide_tab) {
                    tiku_workspace_deactivate(app.desk, fw);
                    desktop_damage_all(&app);
                }
#endif

                /* The Deskbar's hover ends when the display window loses
                 * focus. */
                if (tiku_trk_deskbar_motion(app.deskbar, -1, -1))
                    desktop_damage_all(&app);
                if (app.prefs_track && app.prefs_track != fw)
                    (void)tiku_window_send(app.prefs_track, &ev);
                app.prefs_track = NULL;
#if TIKU_SHELL_BOARD_SERVICES

                if (fw == desktop_deviceprefs_handle(&app) ||
                    fw == desktop_startup_handle(&app) ||
                    fw == desktop_associations_handle(&app) ||
                    fw == desktop_forcequit_handle(&app))
                    (void)tiku_window_send(fw, &ev);
#else

                if (fw == desktop_startup_handle(&app) ||
                    fw == desktop_associations_handle(&app) ||
                    fw == desktop_forcequit_handle(&app))
                    (void)tiku_window_send(fw, &ev);
#endif

                break;
            }
            case TIKU_EVENT_RESIZE:
                (void)resize_desktop(&app, ev.width, ev.height);
                desktop_damage_all(&app);
                break;
            case TIKU_EVENT_EXPOSE:
                desktop_damage_all(&app);
                break;
            case TIKU_EVENT_POINTER_DOWN:
                desktop_pointer_down(&app, &ev, &lp);
                break;
            case TIKU_EVENT_POINTER_MOVE:
                desktop_pointer_move(&app, &ev, &lp);
                break;
            case TIKU_EVENT_POINTER_UP:
                desktop_pointer_up(&app, &ev, &lp);
                break;
            case TIKU_EVENT_KEY_UP:
                if (tiku_trk_switcher_window_shown(&app.switcher_window) &&
                    ev.key == TIKU_KEY_CMD) {
                    /* Cmd released quickly switches to the previous
                     * application; held longer, the release chooses the
                     * selected entry (DB-054). */
                    (void)tiku_trk_switcher_window_release(
                        &app.switcher_window, tiku_trk_now());
                    tiku_trk_switcher_window_close(&app.switcher_window, 1);
                    break;
                }
                /* fall through */
            case TIKU_EVENT_KEY_DOWN:
                desktop_key_down(&app, &ev, &lp);
                break;
            case TIKU_EVENT_CLOSE:
                app.running = 0;
                break;
            default:
                break;
            }
            if (lp.sc != NULL) {
                break;              /* one instruction per turn */
            }
            /* At most 64 events or 2 ms per turn, so a stream of input
             * cannot starve the timers or the next frame. */
            if (++event_count >= 64 ||
                tiku_trk_now()-event_started >= 2000) break;
        }
        desktop_turn_end(&app, &opt, &lp);
    }

    return desktop_teardown(&app, &lp);
}

const tiku_app_descriptor_t tiku_trk_application = {
    .id = "org.tikuos.tracker",
    .name = TIKU_SHELL_TRACKER_NAME,
    .run = tracker_run
};
