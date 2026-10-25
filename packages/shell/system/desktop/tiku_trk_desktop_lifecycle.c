/*
 * Desktop shell and file manager.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_desktop_lifecycle.c - booting and tearing down the desktop: the
 * command line, the boot stages shown on the boot screen, the -shot set-ups,
 * the closing screen and teardown, and the answers to the questions the
 * status overlay asks.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_shell_product.h"
#include "tiku_icons_tiku.h"

#if TIKU_SHELL_BOARD_SERVICES
#include "tiku_link_tcp.h"

#endif
#include "tiku_trail.h"
#include "tiku_trk_desktop_private.h"
#include "tiku_resource_endpoint.h"
#include <strings.h>

/** @brief Apply committed shared preferences to live desktop views and sound. */
static void settings_changed(void *context)
{
    desktop_t *app = context;
    app->desktop_prefs_window.model.value.reduce_motion =
        tiku_trk_prefs_get(prefs_model(app), "ReduceMotion") != 0;
    tiku_trk_soundprefs_apply(prefs_model(app));
    sync_folder_preferences(app);
    desktop_damage_all(app);
}

/** @brief Apply committed Deskbar and font values to the existing native owners. */
static void record_settings_changed(void *context)
{
    desktop_t *app = context;
    tiku_trk_deskbar_apply_settings(app->deskbar, &app->deskbar_prefs_window.model.value);
    tiku_trk_fontprefs_apply(&app->fonts_window.model);
    desktop_damage_all(app);
}

/** @brief Apply accepted native Desktop preferences through its existing view. */
static void desktop_settings_changed(void *context)
{
    desktop_t *app = context;
    tiku_trk_folder_window_t *folder = tiku_trk_desktop_view_folder(&app->desktop_view);
    if (folder && folder->view) {
        int size = app->desktop_prefs_window.model.value.icon_size;
        if (tiku_trk_poseview_icon_size(folder->view) != size) tiku_trk_poseview_set_icon_size(folder->view, size);
        tiku_trk_folder_window_forget(folder);
        tiku_trk_desktop_settings_feedback(app->desktop_settings,folder->bg_drawn ? &folder->bg_applied : NULL,tiku_trk_poseview_icon_size(folder->view));
    }
    desktop_damage_all(app);
}

/** @brief Enqueue the Desktop window's current native proposals. */
static int desktop_settings_submit(void *context)
{
    return tiku_trk_desktop_settings_submit(((desktop_t *)context)->desktop_settings);
}

/** @brief Report native Desktop persistence feedback to its preferences window. */
static const char *desktop_settings_status(void *context)
{
    return tiku_trk_desktop_settings_status(((desktop_t *)context)->desktop_settings);
}

/** @brief Redraw preferences after the committed association cache changes. */
static void catalog_settings_changed(void *context)
{
    desktop_t *app = context;
    int i;
    for (i = 0; i < TIKU_TRK_INFO_WINDOW_MAX; i++)
        if (app->info_windows.window[i] && app->info_windows.window[i]->open)
            info_opens_with_fill(app, &app->info_windows.model[i]);
    desktop_damage_all(app);
}

/** @brief Read pending or failed record persistence feedback for native preferences. */
static const char *record_save_status(void *context)
{
    return tiku_trk_record_settings_status(((desktop_t *)context)->record_settings);
}

/** @brief Submit the current Deskbar GUI proposal through its record owner. */
static int record_bar_submit(void *context)
{
    return tiku_trk_record_settings_submit(((desktop_t *)context)->record_settings, 0);
}

/** @brief Submit the current Fonts GUI proposal through its record owner. */
static int record_font_submit(void *context)
{
    return tiku_trk_record_settings_submit(((desktop_t *)context)->record_settings, 1);
}

/** @brief A script's resize: the same path a window manager's takes. */
static int
script_resize(void *ctx, int width, int height)
{
    return resize_desktop((desktop_t *)ctx, width, height) >= 0;
}

/** @brief The services table's app_of: the installed application at
 *         @p path, through a link too. */
static const struct tiku_trk_app *
services_app_of(void *ctx, const char *path)
{
    return desktop_app_at(ctx, path);
}

/** @brief The services table's app_named: the installed application whose
 *         name or manifest identity is @p name, matched without regard to
 *         case, as Deskbar rows are named. */
static const struct tiku_trk_app *
services_app_named(void *ctx, const char *name)
{
    desktop_t *app = ctx;

    for (int i = 0; name != NULL && name[0] != '\0' && i < app->apps.count;
         i++) {
        const tiku_trk_app_t *a = &app->apps.app[i];

        if (strcasecmp(a->name, name) == 0 || strcasecmp(a->id, name) == 0) {
            return a;
        }
    }
    return NULL;
}

/** @brief The services table's art_for: the art an application's pose is
 *         drawn with. */
static const char *
services_art_for(void *ctx, const char *path)
{
    return desktop_app_art(ctx, path);
}

/** @brief Put @p text on the status line and repaint. */
static void
services_status(void *ctx, const char *text)
{
    desktop_t *app = ctx;

    snprintf(app->status, sizeof app->status, "%s", text);
    desktop_damage_all(app);
}

/** @brief Damage what a view says it changed. */
static void
services_damage(void *ctx, tiku_rect_t r)
{
    desktop_take_damage(ctx, r);
}

/**
 * @brief Wait out what the boot screen's current line is still owed of its
 *        minimum time.
 *
 * Called at the start of the next stage, so a stage's own work counts
 * toward its time: a stage that takes 300 ms shows for 300 ms, and one
 * that takes 2 ms is held to the minimum.
 *
 * @note The last line has no next stage; the caller settles it.
 */
static void
splash_settle(desktop_t *app)
{
    int64_t owed;

    if (!app->splash.live) {
        return;
    }
    owed = tiku_trk_splash_owed(&app->splash, tiku_trk_now());
    if (owed > 0) {
        struct timespec ts;

        ts.tv_sec = (time_t)(owed / 1000000);
        ts.tv_nsec = (long)(owed % 1000000) * 1000L;
        (void)nanosleep(&ts, NULL);
    }
}

/** @brief Show @p what as the boot or closing screen's line, after the
 *         line before has had its time. */
static void
splash_stage(desktop_t *app, const char *what)
{
    if (!app->splash.live) {
        return;
    }
    splash_settle(app);
    tiku_trk_splash_say(&app->splash, what, tiku_trk_now());
    tiku_trk_splash_draw(&app->splash, app->surf,
                         (tiku_rect_t){ 0, 0, app->surf->w, app->surf->h },
                         tiku_trk_now());
    if (app->host != NULL) {
        (void)tiku_host_present(app->host, app->surf);
    }
}

/**
 * @brief Begin the shutdown: play its sound and bring the closing screen
 *        over the desktop, on which teardown names each piece it puts away.
 *        A run with no splash (scripted, or conducted) stops at once.
 */
void
begin_shutdown(desktop_t *app)
{
    tiku_sound_play(TIKU_SOUND_SHUTDOWN);
    tiku_trk_splash_closing(&app->splash, tiku_trk_now());
    if (tiku_trk_splash_closed(&app->splash, tiku_trk_now())) {
        app->running = 0;
        return;
    }
    app->closing = 1;
    desktop_damage_all(app);
}


/**
 * @brief Carry out the answer to an alert, by its tag: a file operation's
 *        conflict or error, shut down, replace on save, open anyway, Get
 *        Info for a broken link, empty the Trash, or the delete question.
 */
static void
status_answer(void *context, const tiku_alert_t *alert)
{
    desktop_t *app = context;

    if (alert->tag == TIKU_TRK_ALERT_TAG_CONFLICT) {
        if (alert->chosen == alert->cancel) {
            tiku_fs_cancel(app->fs);
        } else {
            tiku_fs_resolve(app->fs,
                                tiku_trk_alert_conflict_answer(alert));
        }
    } else if (alert->tag == TIKU_TRK_ALERT_TAG_ERROR) {
        tiku_fs_resolve_error(app->fs,
                                  alert->chosen != alert->cancel);
    } else if (alert->tag == TIKU_TRK_ALERT_TAG_SHUTDOWN) {
        if (alert->chosen != alert->cancel) {
            begin_shutdown(app);
        }
    } else if (alert->tag == TIKU_TRK_ALERT_TAG_SAVE_REPLACE) {
        if (alert->chosen != alert->cancel) {
            tiku_trk_panel_window_confirmed(&app->file_panel);
        }
    } else if (alert->tag == TIKU_TRK_ALERT_TAG_OPEN_ANYWAY) {
        if (alert->chosen != alert->cancel) {
            tiku_trk_open_with_window_confirmed(&app->open_with_window);
        }
    } else if (alert->tag == TIKU_TRK_ALERT_TAG_BROKEN_OPEN) {
        if (alert->chosen != alert->cancel &&
            app->broken_open_path[0] != '\0') {
            {
                int islot = tiku_trk_info_windows_open(&app->info_windows,
                                                app->broken_open_path);

                if (islot >= 0) {
                    info_opens_with_fill(app,
                        &app->info_windows.model[islot]);
                }
            }
        }
    } else if (alert->tag == TIKU_TRK_ALERT_TAG_EMPTY_TRASH) {
        if (alert->chosen != alert->cancel) {
            tiku_trk_folder_file_actions_answer_empty(&app->file_actions);
        }
    } else if (alert->tag == TIKU_TRK_ALERT_TAG_DELETE) {
        folder_t *fl = folder_of(app, tiku_workspace_focused(app->desk));

        if (fl == NULL) {
            fl = desktop_folder(app);
        }
        {
            /* The buttons, in order: Cancel, Move to Trash, Delete
             * (FS-046).  The alert's opener sets Move to Trash as the
             * default, so Return answers Move to Trash. */
            static const tiku_trk_delete_answer_t by_button[] = {
                TIKU_TRK_DELETE_ANSWER_CANCEL,
                TIKU_TRK_DELETE_ANSWER_TRASH,
                TIKU_TRK_DELETE_ANSWER_DELETE,
            };
            int chosen = (alert->chosen >= 0 && alert->chosen < 3)
                             ? alert->chosen : 0;

            tiku_trk_folder_file_actions_answer_delete(&app->file_actions,
                fl, by_button[chosen]);
        }
    }
}

/** @brief The progress window's Stop: cancel the file operation at its next
 *         safe point. */
static void
status_stop(void *context)
{
    desktop_t *app = context;

    tiku_fs_cancel(app->fs);
}

/**
 * @brief What the command line asked for, into @p o.
 *
 * @return -1 to carry on; otherwise the exit code the line called for,
 *         as -version and -reset-settings do their work here and stop.
 */
int
desktop_parse_args(int argc, char **argv, desktop_options_t *o)
{
    int i;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-version") == 0 ||
            strcmp(argv[i], "--version") == 0) {
            /* The build's date and time, which tell copies of the
             * program apart. */
            printf(TIKU_SHELL_VERSION_FORMAT, __DATE__, __TIME__);
            return 0;
        }
        if (strcmp(argv[i], "-reset-settings") == 0 ||
            strcmp(argv[i], "--reset-settings") == 0) {
            /* Factory settings without opening a window: the settings
             * file and each device's .state file are renamed with a
             * .factory-reset suffix. */
            const char *home = getenv("HOME");
            char from[TIKU_PATH_MAX], to[TIKU_PATH_MAX];

            if (home == NULL || home[0] == '\0') {
                fprintf(stderr, "reset-settings: no HOME\n");
                return 1;
            }
            snprintf(from, sizeof from, TIKU_SHELL_SETTINGS_PATH, home);
            snprintf(to, sizeof to, "%s.factory-reset", from);
            if (rename(from, to) == 0) {
                printf("moved %s -> %s\n", from, to);
            }
            {
                char dir[TIKU_PATH_MAX];
                DIR *d;
                struct dirent *e;

                snprintf(dir, sizeof dir,
                         TIKU_SHELL_DEVICE_STATE_PATH, home);
                d = opendir(dir);
                while (d != NULL && (e = readdir(d)) != NULL) {
                    size_t len = strlen(e->d_name);

                    if (len > 6u &&
                        strcmp(e->d_name + len - 6u, ".state") == 0) {
                        snprintf(from, sizeof from, "%s/%s", dir,
                                 e->d_name);
                        snprintf(to, sizeof to, "%s.factory-reset", from);
                        if (rename(from, to) == 0) {
                            printf("moved %s -> %s\n", from, to);
                        }
                    }
                }
                if (d != NULL) {
                    (void)closedir(d);
                }
            }
            printf("settings are factory defaults on the next start\n");
            return 0;
        }
#if TIKU_SHELL_BOARD_SERVICES

        if (strcmp(argv[i], "-d") == 0)                      { o->use_device = 1; }
        /*
         * -devices: boards are mounted under /devices, each under its own
         * name, with the local files at the root; -d puts one board at
         * the root.  With -devices and no -p, mount_devices() only lists
         * the ports for the Devices menu.
         */
        else if (strcmp(argv[i], "-devices") == 0) { o->want_devices = 1; }
        /*
         * -listen [port]: take in boards that dial in over TCP, on
         * TIKU_LINK_TCP_PORT unless a port is given.  Off by default.
         */
        else if (strcmp(argv[i], "-listen") == 0) {
            o->listen_port = TIKU_LINK_TCP_PORT;
            if (i + 1 < argc && argv[i + 1][0] >= '0' &&
                argv[i + 1][0] <= '9') {
                o->listen_port = atoi(argv[++i]);
            }
        }
        else if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) { o->port = argv[++i]; }
        /*
         * -mount: where the board -p names is mounted, in place of
         * /devices/<name>, so a script written for /devices/board1 runs
         * against whatever board is on the cable.
         */
        else if (strcmp(argv[i], "-mount") == 0 && i + 1 < argc) { o->mount_at = argv[++i]; o->want_devices = 1; }
        else if (strcmp(argv[i], "-b") == 0 && i + 1 < argc) { o->baud = atoi(argv[++i]);
#else

        if (strcmp(argv[i], "-d") == 0 ||
            strcmp(argv[i], "-devices") == 0 ||
            strcmp(argv[i], "-listen") == 0 ||
            strcmp(argv[i], "-p") == 0 ||
            strcmp(argv[i], "-mount") == 0 ||
            strcmp(argv[i], "-b") == 0) {
            fprintf(stderr, "KintsugiOS: unsupported option %s\n", argv[i]);
            return 2;
#endif
 }
        else if (strcmp(argv[i], "-shot") == 0 && i + 1 < argc) { o->shot = argv[++i]; }
        else if (strcmp(argv[i], "-shotmenu") == 0) { o->shot_menu = 1; }
        else if (strcmp(argv[i], "-script") == 0 && i + 1 < argc) { o->script_path = argv[++i]; }
                else if (strcmp(argv[i], "-mute") == 0) { o->mute = 1; }
        else if (strcmp(argv[i], "-fullscreen") == 0) { o->fullscreen = 1; }
        else if (strcmp(argv[i], "-after") == 0 && i + 1 < argc) {
            o->after = argv[++i];
        }
        else if (strcmp(argv[i], "-conduct") == 0 && i + 1 < argc) { o->conduct_path = argv[++i]; }
        else if (strcmp(argv[i], "-conduct-tty") == 0 && i + 1 < argc) { o->conduct_tty = argv[++i]; }
        else if (strcmp(argv[i], "-conduct-token") == 0 && i + 1 < argc) { o->conduct_token = argv[++i]; }
        else if (strcmp(argv[i], "-conduct-query-token") == 0 && i + 1 < argc) { o->conduct_query_token = argv[++i]; }
        else if (strcmp(argv[i], "-shotedit") == 0 && i + 1 < argc) {
            o->shot_edit = argv[++i];
        }
        else if (strcmp(argv[i], "-shotfind") == 0 && i + 1 < argc) {
            o->shot_find = argv[++i];
        }
        else if (strcmp(argv[i], "-shotpanel") == 0) { o->shot_panel = 1; }
#if TIKU_SHELL_BOARD_SERVICES

        else if (strcmp(argv[i], "-shotlive") == 0) { o->shot_live = 1; }
#endif

        else if (strcmp(argv[i], "-safe") == 0) { o->safe_mode = 1; }
        else if (strcmp(argv[i], "-shotshift") == 0) { o->shot_shift = 1; }
        else if (strcmp(argv[i], "-shotzoom") == 0) { o->shot_zoom = 1;
#if TIKU_SHELL_BOARD_SERVICES
 }
        else if (strcmp(argv[i], "-shotcascade") == 0) { o->shot_cascade = 1; }
        else if (strcmp(argv[i], "-shotnotify") == 0) { o->shot_notify = 1;
#endif
 }
        else if (strcmp(argv[i], "-shotselect") == 0) { o->shot_select = 1; }
        else if (argv[i][0] != '-' && o->nopen < (int)(sizeof o->openme /
                                                    sizeof o->openme[0])) {
            /* Every path argument opens its own window (AW-100), up to
             * TIKU_WINDOW_MAX of them; later ones are dropped. */
            o->openme[o->nopen++] = argv[i];
        }
        else if (strcmp(argv[i], "-seconds") == 0 && i + 1 < argc) {
            o->deadline = tiku_trk_now() + (int64_t)atoi(argv[++i]) * 1000000;
        }
    }
#if TIKU_SHELL_BOARD_SERVICES

    /* -p without -devices puts that board at the root, as -d does; with
     * -devices it is mounted under /devices and the root stays local.
     * Either way the port is opened once. */
    if (o->port != NULL && !o->want_devices) {
        o->use_device = 1;
    }
#endif

    return -1;
}

/**
 * @brief The namespace the desktop holds and the window it draws in: one
 *        backend at the root, the devices beside it, the surface and the
 *        host.
 * @return -1 to go on, or the exit code.
 */
static int
boot_namespace(desktop_t *app, const desktop_options_t *o, desktop_loop_t *lp)
{
    (void)lp;
    {
#if TIKU_SHELL_BOARD_SERVICES

        /*
         * The desktop always holds a mount table, even with one backend
         * in it: a device that arrives later is one more entry, and
         * every view asks the table.
         */
        tiku_backend_t *root = o->use_device
                                   ? tiku_backend_tiku_open(o->port, o->baud)
                                   : tiku_backend_posix_open();
#else

        /*
         * The desktop always holds a mount table, even with one backend
         * in it: a device that arrives later is one more entry, and
         * every view asks the table.
         */
        tiku_backend_t *root = tiku_backend_posix_open();
#endif


        if (root == NULL) {
            fprintf(stderr, "tracker: no backend\n");
            return 1;
        }
        app->backend = tiku_mount_open();
        if (app->backend == NULL ||
            tiku_mount_add(app->backend, "/", root, 1) != 0) {
            fprintf(stderr, "tracker: no namespace\n");
            return 1;
        }
    }
    {
        const char *directory=getenv("STOMME_RESOURCE_DIRECTORY");
        if(directory && *directory){
            const char *mount=getenv("STOMME_RESOURCE_MOUNT");
            const char *fixture=getenv("STOMME_RESOURCE_TEST_PROVIDER");
            if(desktop_lifecycle_vfs_restarting(app)&&tiku_resource_endpoint_recover(directory)) {
                fprintf(stderr,"resource service: endpoint recovery refused\n");return 1;
            }
            app->resource_session=tiku_trk_resource_session_open(app->backend,directory,
                mount && *mount?mount:"/.stomme",TIKU_SHELL_PRODUCT_NAME,fixture && !strcmp(fixture,"1"));
            if(!app->resource_session){fprintf(stderr,"resource service: startup refused\n");return 1;}
        }
    }
    /* From here a folder under /devices is listed by a worker. */
    desktop_wire_install(app);
#if TIKU_SHELL_BOARD_SERVICES

    if (o->want_devices) {
        (void)mount_devices(app, o->port, o->baud, o->mount_at);
    }
    if (o->listen_port > 0 && !boards_listen(app, o->listen_port)) {
        fprintf(stderr, "tracker: nothing listening on %d\n", o->listen_port);
    }
#endif

    {
        /*
         * TIKU_MOUNT=/path mounts the local files again at /path: a
         * second mount for tests, so the routing between mounts runs on
         * a host with one filesystem and no board.
         */
        const char *mirror = getenv("TIKU_MOUNT");

        if (mirror != NULL && mirror[0] == '/' && mirror[1] != '\0') {
            /*
             * A posix backend of its own, so the two mounts are two
             * stores to anything that asks whether they are the same one,
             * as a copy between mounts does.
             */
            tiku_backend_t *second = tiku_backend_posix_open();

            if (second == NULL ||
                tiku_mount_add(app->backend, mirror, second, 1) != 0) {
                tiku_backend_close(second);
            }
        }
    }
    app->fs = tiku_fs_new(app->backend);
    (void)tiku_fs_prepare_trash(app->fs);
    app->backdrop_color = TIKU_C_BACKDROP;
    app->surf = tiku_surface_new(DESK_W, DESK_H, app->backdrop_color);
    /*
     * The window opens before the boot's work, so the boot screen shows in
     * it while that work runs.  A scripted or conducted run opens no
     * window.
     */
    if (o->script_path == NULL && o->conduct_path == NULL && o->conduct_tty == NULL) {
        app->host = tiku_host_open(DESK_W, DESK_H, TIKU_SHELL_DESKTOP_TITLE);
        if (app->host == NULL) {
            fprintf(stderr, "tracker: no display\n");
            return 1;
        }
        /* Full screen is asked for before the first frame, so the window
         * is never seen growing; the new size arrives as a resize. */
        if (o->fullscreen) {
            tiku_host_set_fullscreen(app->host, 1);
        }
        if (tiku_host_scale(app->host) > 1) {
            (void)tiku_surface_rescale(app->surf,
                tiku_host_scale(app->host), app->backdrop_color);
        }
    }
    return -1;
}

/**
 * @brief The boot screen goes up; the sound, the workspace, the Deskbar, the
 *        responders, the menus, the status overlay, the Find window, the
 *        installed applications, the remote listener and its channels, the
 *        loop's periodic tasks and the session store are made.
 * @return -1 to go on.
 */
static int
boot_deskbar(desktop_t *app, const desktop_options_t *o, desktop_loop_t *lp)
{
    (void)lp;
    /* A scripted, conducted or -mute run plays no sound; the sound kit
     * still counts the moments. */
    tiku_sound_init((o->script_path != NULL || o->conduct_path != NULL ||
                     o->conduct_tty != NULL || o->mute)
                        ? NULL : tiku_sound_host_backend());
    (void)tiku_sound_load_scores();
    /* The boot screen shows with a display, or when TIKU_SPLASH is set, as
     * the boot-screen tests set it. */
    tiku_trk_splash_begin(&app->splash,
        app->host != NULL || getenv("TIKU_SPLASH") != NULL, tiku_trk_now());
    tiku_sound_play(TIKU_SOUND_STARTUP);
    splash_stage(app, "storage");
    app->row_h = tiku_font_plain()->height + 5;
    app->desk = tiku_workspace_new(app->surf);
    app->deskbar = tiku_trk_deskbar_new(app->backend);
    desktop_responders_attach(app);
    app->team = tiku_trk_deskbar_add_team(app->deskbar,
                                           TIKU_SHELL_TRACKER_NAME);
    splash_stage(app, "the Deskbar");
    /* The Deskbar's rows are the desktop and its applications, not the
     * machine's processes. */
    tiku_trk_cwmenu_init(&app->menus);
    tiku_trk_status_overlay_init(&app->status_overlay, app->surf,
        DESK_W, DESK_H, &app->dirty, status_answer, status_stop, app);
    tiku_trk_find_window_init(&app->find_window, app->desk, DESK_W, DESK_H,
        &app->dirty, app->status, sizeof app->status, panel_search, app);
    /* TikuOS's own icons are registered before the person's icon folder
     * is loaded, so an icon there replaces TikuOS's of the same name. */
    (void)tiku_icons_tiku_register();
    prepare_places(app);
    tiku_trk_launch_clear(&app->apps);
    offer_installed(app, o->argv0);
    /*
     * The Interface kit carries icon commands in display lists without
     * decoding them; this installs the HVIF player that draws them.
     */
    tiku_dl_set_icon_painter(tiku_icon_stream_play);
    if (tiku_remote_listen(&app->remote) != 0) {
        /* Without the socket no application can connect; the desktop
         * runs on. */
        app->remote.fd = -1;
    }
#if TIKU_SHELL_BOARD_SERVICES

    channels_attach(app);
#endif

    if (!o->safe_mode && getenv("TIKU_EMBED_DEMO") != NULL) {
        /* TIKU_EMBED_DEMO starts the framework demo, through the usual
         * launcher. */
        (void)desktop_launch_app(app, "framework-demo", NULL);
    }
    app->loop = tiku_trk_loop_new();
    /* Once a second, so the displayed minute can never go stale while
     * nothing else dirties the surface (DB-044). */
    (void)tiku_trk_run_periodic(app->loop, tick_clock, app, NULL, 1000000,
                                0);
    /* Every 75 ms, one tick for all hosted applications; a tick that
     * changes nothing draws nothing. */
    (void)tiku_trk_run_periodic(app->loop, tick_embeds, app, NULL,
                                75000, 0);
    if (o->script_path == NULL && o->conduct_path == NULL &&
        o->conduct_tty == NULL) {
        /* Empty: the Deskbar's rows need no scan of the machine's
         * processes. */
    }
    /* The store is open before the Open With window keeps it. */
    app->store = tiku_trk_session_open_store(app->backend);
    tiku_trk_open_with_window_init(&app->open_with_window, app->desk,
        DESK_W, DESK_H, &app->dirty, open_with_window_launch, app,
        alert_model(app), app->store);
    tiku_trk_select_window_init(&app->select_window, app->desk,
        DESK_W, DESK_H, &app->dirty, apply_select_model, app);
    tiku_trk_clip_init(&app->clip);
    return -1;
}

/**
 * @brief Settings and the windows that edit them, the help folder, and the
 *        chosen fonts.
 * @return -1 to go on.
 */
static int
boot_settings(desktop_t *app, const desktop_options_t *o, desktop_loop_t *lp)
{
    (void)o;
    (void)lp;
    splash_stage(app, "settings");
    tiku_trk_deskbar_set_store(app->deskbar, app->store);
    (void)tiku_trk_deskbar_load_replicants(app->deskbar);
    tiku_trk_session_controller_init(&app->session, app->store, app->deskbar,
        &app->desktop_view, &app->folder_lifecycle);
    tiku_trk_find_window_set_store(&app->find_window, app->store);
    (void)tiku_trk_poseview_install_query_defaults(app->backend, app->store);
    tiku_trk_session_restore_deskbar(&app->session);
    {
        /* The Deskbar menu's folders: the user's, then the shipped one
         * (DB-002, DB-003). */
        const char *home = getenv("HOME");
        char user[TIKU_PATH_MAX];

        snprintf(user, sizeof user, "%s/.config/deskbar",
                 (home != NULL && home[0] != '\0') ? home : "");
        tiku_trk_deskbar_set_menu_dir(app->deskbar, user,
                                      TIKU_SHELL_DESKBAR_PATH);
    }
    tiku_trk_deskbar_prefs_window_init(&app->deskbar_prefs_window, app->desk,
        app->deskbar, app->store, DESK_W, DESK_H,
        tiku_trk_deskbar_user_menu_dir(app->deskbar), &app->dirty,
        deskbar_edit_path, app);
    /* The backend's own state store, which holds a node's properties
     * and the Desktop's backdrop picture; not the session store. */
    tiku_trk_desktop_prefs_window_init(&app->desktop_prefs_window, app->desk,
        &app->desktop_view,
        (app->backend != NULL && app->backend->ops->state_store != NULL)
            ? app->backend->ops->state_store(app->backend) : NULL,
        DESK_W, DESK_H, &app->dirty);
    if (app->desktop_prefs_window.store && app->desktop_prefs_window.node[0]) {
        tiku_trk_folder_window_t *folder = tiku_trk_desktop_view_folder(&app->desktop_view);
        if (folder && folder->view) app->desktop_prefs_window.model.value.icon_size = tiku_trk_poseview_icon_size(folder->view);
        app->desktop_settings = tiku_trk_desktop_settings_open(app->desktop_prefs_window.store,
            app->desktop_prefs_window.node, &app->desktop_prefs_window.model, desktop_settings_changed, app);
        if (!app->desktop_settings) return 1;
        app->desktop_prefs_window.save_context = app;
        app->desktop_prefs_window.submit_save = desktop_settings_submit;
        app->desktop_prefs_window.save_status = desktop_settings_status;
    }
    tiku_trk_fonts_window_init(&app->fonts_window, app->desk, app->deskbar,
                               app->store, DESK_W, DESK_H, &app->dirty);
    app->record_settings = tiku_trk_record_settings_open(app->store,
        &app->deskbar_prefs_window.model, &app->fonts_window.model,
        record_settings_changed, app);
    if (!app->record_settings) return 1;
    app->deskbar_prefs_window.save_context = app;
    app->deskbar_prefs_window.submit_save = record_bar_submit;
    app->deskbar_prefs_window.save_status = record_save_status;
    app->fonts_window.save_context = app;
    app->fonts_window.submit_save = record_font_submit;
    app->fonts_window.save_status = record_save_status;
    {
        if (!find_help_dir(o->argv0, app->help_root, sizeof app->help_root))
            app->help_root[0] = 0;
    }
    {
        /* Applied after the Deskbar's settings, so a face in the fonts
         * folder overrides the built-in family they name, from startup
         * on. */
        tiku_trk_fontprefs_t chosen;

        (void)tiku_trk_fontprefs_load(&chosen, app->store);
        tiku_trk_fontprefs_apply(&chosen);
    }
    return -1;
}

/**
 * @brief The types database, and the applications: TikuTracker itself and
 *        the installed registry.
 * @return -1 to go on.
 */
static int
boot_fonts(desktop_t *app, const desktop_options_t *o, desktop_loop_t *lp)
{
    (void)o;
    (void)lp;
    splash_stage(app, "fonts");
    {
        /* The types database, then the applications registry, read
         * from ~/.config/tracker/apps or else the shipped list. */
        const char *home = getenv("HOME");
        char reg[TIKU_PATH_MAX];

        {
            char tdb[TIKU_PATH_MAX];

            snprintf(tdb, sizeof tdb, TIKU_SHELL_TYPE_SETTINGS_PATH,
                     (home != NULL && home[0] != '\0') ? home : "");
            (void)tiku_trk_types_install(&app->types, tdb);
            tiku_trk_find_window_set_types(&app->find_window, &app->types);
            /* The icon cache asks the types database for a type's icon
             * (IV-011). */
            tiku_trk_nodeicon_set_types(&app->nicons, &app->types);
        }
        {
            /* TikuTracker is in the list, marked as this desktop:
             * choosing it opens folders here (AW-035). */
            int self = tiku_trk_launch_add(&app->apps,
                                           TIKU_SHELL_TRACKER_NAME, "",
                                           "inode/directory");
            if (self >= 0) {
                app->apps.app[self].is_tracker = 1;
            }
        }
        snprintf(reg, sizeof reg, TIKU_SHELL_APP_SETTINGS_PATH,
                 (home != NULL && home[0] != '\0') ? home : "");
        if (tiku_trk_launch_load(&app->apps, reg) < 0) {
            (void)tiku_trk_launch_load(&app->apps,
                                       TIKU_SHELL_INSTALLED_APPS_PATH);
        }
    }
    return -1;
}

/**
 * @brief The launcher, the folder menus, the icon painters, the add-ons,
 *        the thumbnail queue and the node-icon cache.
 * @return -1 to go on.
 */
static int
boot_applications(desktop_t *app, const desktop_options_t *o, desktop_loop_t *lp)
{
    (void)o;
    (void)lp;
    splash_stage(app, "applications");
    tiku_trk_launch_controller_init(&app->launcher, &app->apps, app->store,
        app->status, sizeof app->status, launch_open_path, app);
    app->launcher.open_document = desktop_document_open;
    tiku_trk_folder_menu_init(&app->folder_menu, &app->menus, &app->clip,
        app->backend, app->fs, &app->types, &app->launcher, &app->bar_fl,
        &app->bar_open);
    tiku_menu_set_icon_painter(paint_menu_icon);
    tiku_trk_deskbar_set_menu_icon_painter(app->deskbar, deskbar_menu_icon,
                                           app);
    desktop_addons_register(app);
    {
        const char *home = getenv("HOME");

        if (home != NULL && home[0] != '\0') {
            snprintf(app->addons_dir, sizeof app->addons_dir,
                     TIKU_SHELL_ADDONS_PATH, home);
            snprintf(app->addons_keys, sizeof app->addons_keys,
                     TIKU_SHELL_ADDON_SHORTCUTS_PATH, home);
        }
        sync_addons(app, 1);
    }
    tiku_thumb_init(&app->thumbs);
    tiku_trk_nodeicon_init(&app->nicons);
    return -1;
}

/**
 * @brief The windows, panels and controllers: Info windows, the file panel,
 *        the preferences and sounds windows, the current device and the
 *        look for boards, the file and edit controllers, the folder
 *        renderer and lifecycle, the Desktop view, the drag target, the
 *        script adapter and the drop controller.
 * @return -1 to go on.
 */
static int
boot_icons(desktop_t *app, const desktop_options_t *o, desktop_loop_t *lp)
{
    (void)o;
    (void)lp;
    splash_stage(app, "icons");
    tiku_volwatch_init(&app->volwatch);
    app->info_relink_path[0] = '\0';
    tiku_trk_info_windows_init(&app->info_windows, app->desk, app->backend,
        app->store, app->fs, alert_model(app), &app->dirty);
    tiku_trk_panel_window_init(&app->file_panel, app->desk, app->backend,
        app->store, &app->nicons, app->row_h, DESK_W, DESK_H, &app->dirty,
        fpanel_commit, app);
    tiku_trk_panel_window_set_alert(&app->file_panel, alert_model(app));
    app->find_window.save_as = find_save_as;
    app->find_window.save_as_context = app;
    app->find_window.select_folder = find_select_folder;
    {
        const char *home = getenv("HOME");
        char path[TIKU_PATH_MAX] = "";

        if (home != NULL && home[0] != '\0') {
            snprintf(path, sizeof path, TIKU_SHELL_SETTINGS_PATH, home);
        }
        tiku_trk_prefs_window_init(&app->prefs_window, app->desk,
            DESK_W, DESK_H, path, &app->dirty,
            &alert_model(app)->open);
        if (path[0]) {
            char types_path[TIKU_PATH_MAX];
            if (snprintf(types_path, sizeof types_path, TIKU_SHELL_TYPE_SETTINGS_PATH, home) >= (int)sizeof types_path) return 1;
            app->catalog_settings = tiku_trk_catalog_settings_open_with_file(app->store, &app->apps, &app->types, types_path, catalog_settings_changed, app);
            if (!app->catalog_settings) return 1;
            tiku_trk_open_with_window_bind_settings(&app->open_with_window, app->catalog_settings);
            app->settings = tiku_trk_settings_open_with_desktop(prefs_model(app), path,
                tiku_trk_resource_session_registry(app->resource_session), app->record_settings, app->catalog_settings, app->desktop_settings,
                settings_changed, app);
            if (!app->settings) {
                fprintf(stderr, "settings controller: startup refused\n");
                return 1;
            }
        }
        tiku_trk_desktop_prefs_window_bind_settings(&app->desktop_prefs_window,
                                                    prefs_model(app), path);
        tiku_trk_sounds_window_init(&app->sounds_window, app->desk,
            prefs_model(app), path, DESK_W, DESK_H, &app->dirty,
            sound_choose, app);
        /* The chime has played by now on the kit's own defaults; from
         * here the settings file says which moments sound. */
        tiku_trk_soundprefs_apply(prefs_model(app));
#if TIKU_SHELL_BOARD_SERVICES

        /* The device current last time: its name and port are restored,
         * and that port alone is connected again here. */
        {
            tiku_trk_prefs_t *prefs = prefs_model(app);
            const char *dev = tiku_trk_prefs_text(prefs, "CurrentDevice");
            const char *dport =
                tiku_trk_prefs_text(prefs, "CurrentDevicePort");

            snprintf(app->current_device, sizeof app->current_device,
                     "%s", (dev != NULL) ? dev : "");
            snprintf(app->current_device_port,
                     sizeof app->current_device_port, "%s",
                     (dport != NULL) ? dport : "");
            if (app->current_device_port[0] != '\0') {
                app->look.going = 1;
                app->look.gen++;
                app->look.pending = 0;
                app->look.session = -1;
                app->look.slot = NULL;
                /* The probe device_connect() posts, counted in
                 * look.pending. */
                if (desktop_wire_probe(app, DESKTOP_ASK_CONNECT,
                                       app->current_device_port,
                                       (app->board_baud > 0)
                                           ? app->board_baud : 115200,
                                       0, NULL, 1) == 0) {
                    app->look.pending++;
                }
                desktop_wire_wait(app, &app->look.pending, 5000L);
                app->look.going = 0;
            }
            /* A look at every port, on workers, names the boards for the
             * Devices menu ("nrf54l15 (ttyACM3)").  Its answers are handled
             * once the loop runs; a connected port is left alone. */
            desktop_board_look_begin(app, -1, NULL);
        }
#endif

    }
    tiku_trk_folder_file_actions_init(&app->file_actions, app->fs,
        app->backend, &app->clip,
        &app->drop_controller.pending, app->drop_controller.destination,
        alert_model(app), open_preferences, app, app->status,
        sizeof app->status, file_actions_close_under, app);
    tiku_trk_folder_edit_init(&app->folder_edit, app->backend, app->fs,
        app->status, sizeof app->status, &app->dirty);
    tiku_trk_folder_menu_set_editor(&app->folder_menu, &app->folder_edit);
    tiku_trk_band_controller_init(&app->band_controller, app->row_h,
                                  &app->dirty);
    tiku_trk_switcher_window_init(&app->switcher_window, app->desk,
        app->deskbar, DESK_W, DESK_H, &app->dirty, app->status,
        sizeof app->status);
    app->services = (tiku_trk_services_t){
        .ctx = app, .app_of = services_app_of,
        .app_named = services_app_named, .art_for = services_art_for,
        .status = services_status, .damage = services_damage
    };
    app->folder_render = (tiku_trk_folder_render_t){
        .workspace = app->desk,
        .menus = &app->menus,
        .prefs = prefs_model(app),
        .clip = &app->clip,
        .backend = app->backend,
        .icons = &app->nicons,
        .store = app->store,
        .thumbs = &app->thumbs,
        .row_h = app->row_h,
        .screen_w = DESK_W,
        .screen_h = DESK_H,
        .status = app->status,
        .count_text = app->count_text,
        .count_text_size = sizeof app->count_text,
        .finding = &app->finding,
        .find_text = app->find_buf,
        .edit = &app->folder_edit,
        .band = &app->band_controller,
        .drop = &app->drag_target,
        .press = &app->press,
        .services = &app->services,
        .prepare_menu = tiku_trk_folder_menu_prepare,
        .strip = &app->tstrip,
        .strip_owner = NULL,
        .prepare_context = &app->folder_menu
    };
    tiku_trk_folder_lifecycle_init(&app->folder_lifecycle, app->folders,
        TIKU_WINDOW_MAX, &app->folder_count, &app->cascade_x,
        &app->cascade_y, app->desk, app->backend, app->store,
        prefs_model(app), &app->folder_render, app->status,
        sizeof app->status, &app->dirty, DESK_W, DESK_H);

    app->desk_row = -1;
#if TIKU_SHELL_BOARD_SERVICES

    /* Zero is a real session; -1 is nobody. */
    app->board_run_session = -1;
#endif

    (void)tiku_trk_desktop_view_init(&app->desktop_view, app->backend, app->fs,
        prefs_model(app), app->deskbar, &app->folder_render, DESK_W, DESK_H);
    tiku_trk_drag_target_init(&app->drag_target, app->desk, app->deskbar,
        &app->desktop_view, &app->drag, app->folders, &app->folder_count,
        app->row_h, &app->dirty, spring_open_nav, app);
    tiku_trk_script_adapter_init(&app->script_adapter, app->desk, app->surf,
        &app->folder_lifecycle, &app->desktop_view, app->deskbar,
        alert_model(app), &app->find_window, &app->file_panel,
        &app->folder_edit, &app->popup, &app->popup_sub, &app->popup_frame,
        &app->popup_sub_frame, app->status, app->count_text, app->row_h);
    /* After the init, which writes the whole adapter. */
    app->script_adapter.redraw_diff = script_redraw_diff;
    app->script_adapter.redraw_ctx = app;
    app->script_adapter.resize = script_resize;
    app->script_adapter.resize_ctx = app;
    app->script_adapter.say = conduct_text;
    app->script_adapter.say_ctx = app;
    tiku_trk_drop_controller_init(&app->drop_controller, app->deskbar,
        app->backend, app->fs, &app->desktop_view, &app->menus,
        alert_model(app), &app->drag, &app->drag_target.folder,
        &app->drag_target.row,
        &app->popup, &app->popup_sub, &app->popup_frame, app->prog_dest,
        sizeof app->prog_dest, &app->services,
        DESK_W, DESK_H, set_backdrop_color, app);
    tiku_trk_drop_controller_set_launcher(&app->drop_controller, drop_launch,
                                          app);
    return -1;
}

/**
 * @brief With a board at the root, its Deskbar replicants and Find places;
 *        then the startup applications, the last session's folders, and
 *        the default Find.
 * @return -1 to go on.
 */
static int
boot_session(desktop_t *app, const desktop_options_t *o, desktop_loop_t *lp)
{
    int restore, fallback;
    (void)lp;
#if TIKU_SHELL_BOARD_SERVICES

    if (o->use_device) {
        /* The board's uptime, boot count and free NVM, as Deskbar
         * replicants. */
        (void)tiku_trk_deskbar_add_replicant(app->deskbar, "uptime",
                                             "/sys/uptime");
        (void)tiku_trk_deskbar_add_replicant(app->deskbar, "boots",
                                             "/sys/boot_count");
        (void)tiku_trk_deskbar_add_replicant(app->deskbar, "free",
                                             "/sys/mem/nvmfree");
        /* The Find panel's places are the board's top-level folders. */
        {
            tiku_model_t roots[TIKU_TRK_FIND_PLACES_MAX];
            int nr = 0, k;

            nr = list_children(app->backend, "/", roots,
                               TIKU_TRK_FIND_PLACES_MAX);
            for (k = 0; k < nr; k++) {
                if (tiku_model_is_container(&roots[k])) {
                    (void)tiku_trk_find_add_place_identity(
                        find_model(app), roots[k].name, roots[k].facts.dev,
                        app->backend->devid[0] != '\0' ? app->backend->devid
                                                     : roots[k].name);
                }
            }
        }
    }
#endif

    desktop_startup_run(app, o->safe_mode, &restore, &fallback);
    app->resource_safe_mode = o->safe_mode;
    app->resource_boot_restore = !o->safe_mode && restore;
    app->resource_boot_fallback = fallback;
#if TIKU_SHELL_BOARD_SERVICES

    tiku_trk_session_restore_folders(&app->session, o->openme, o->nopen,
        o->safe_mode || !restore, o->use_device, fallback);
#else

    tiku_trk_session_restore_folders(&app->session, o->openme, o->nopen,
        o->safe_mode || !restore, 0, fallback);
#endif


    (void)tiku_trk_find_open_default(find_model(app), (int64_t)time(NULL));
    return -1;
}

/**
 * @brief The conductor channel and the script, for a desktop driven from
 *        outside.
 * @return -1 to go on, or the exit code.
 */
static int
boot_drivers(desktop_t *app, const desktop_options_t *o, desktop_loop_t *lp)
{
    if (o->conduct_tty != NULL) {
        int line = tiku_conduct_open_tty(o->conduct_tty, 0);

        if (line < 0 ||
            tiku_conduct_adopt(&app->conduct, line, app,
                                    conduct_inject, conduct_pixel,
                                    conduct_text) != 0) {
            fprintf(stderr, "tracker: cannot speak the conductor channel "
                            "over %s\n", o->conduct_tty);
            return 3;
        }
        app->conduct_on = 1;
    }
    if (o->conduct_path != NULL) {
        if (tiku_conduct_listen(&app->conduct, o->conduct_path, app,
                                     conduct_inject, conduct_pixel,
                                     conduct_text) != 0) {
            fprintf(stderr, "tracker: cannot offer the conductor channel "
                            "on %s\n", o->conduct_path);
            return 3;
        }
        app->conduct_on = 1;
    }
    if (app->conduct_on &&
        (o->conduct_token != NULL || o->conduct_query_token != NULL)) {
        tiku_conduct_require(&app->conduct, o->conduct_token,
                                  o->conduct_query_token);
    }
    if (app->conduct_on) {
        /* The Deskbar clock is frozen at the scripts' fixture time, 7 Aug
         * 2026 16:26, so the clock's pixels do not change during a run. */
        {
            struct tm fixed;

            memset(&fixed, 0, sizeof fixed);
            fixed.tm_year = 2026 - 1900;
            fixed.tm_mon = 7;
            fixed.tm_mday = 7;
            fixed.tm_hour = 16;
            fixed.tm_min = 26;
            fixed.tm_isdst = -1;
            tiku_trk_deskbar_set_clock(app->deskbar, mktime(&fixed));
        }
    }
    if (o->script_path != NULL) {
        /* A scripted run has no window but draws into the same surface,
         * through the same draw path (see tiku_trk_script.h). */
        lp->sc = tiku_trk_script_open(o->script_path);
        /* A scripted wait is a number of turns: count the autoscroll hold
         * in pulses so the same script passes under any load (PVS-050). */
        tiku_trk_band_controller_set_script_delay(&app->band_controller, 30);
        tiku_trk_drag_target_set_script_delay(&app->drag_target, 30);
        {
            /* The fixture's clock, frozen at 7 Aug 2026 16:26, so the
             * clock's pixels do not change during a run. */
            struct tm fixed;

            memset(&fixed, 0, sizeof fixed);
            fixed.tm_year = 2026 - 1900;
            fixed.tm_mon = 7;
            fixed.tm_mday = 7;
            fixed.tm_hour = 16;
            fixed.tm_min = 26;
            fixed.tm_isdst = -1;
            tiku_trk_deskbar_set_clock(app->deskbar, mktime(&fixed));
        }
        if (lp->sc == NULL) {
            return 2;
        }
    } else if (app->conduct_on) {
        /* Conducted and displayless: the surface is the output, and the
         * driver reads it through the channel. */
        app->host = NULL;
    }
    /* Otherwise the window has been open since boot_namespace(). */
    return -1;
}

/**
 * @brief The -shot flags: what a -shot run puts on screen before it writes
 *        its one frame.
 * @return -1 to go on.
 */
static int
boot_shots(desktop_t *app, const desktop_options_t *o, desktop_loop_t *lp)
{
    if (o->shot != NULL) {
        lp->shot_at = tiku_trk_now() + 2000000;
    }
    if (o->shot_select) {
        /* Scripted: the Select window over the front folder, with an
         * expression already in it, so a capture shows the whole control
         * set the way it comes up. */
        tiku_window_t *fw = tiku_workspace_focused(app->desk);
        folder_t *ffl = folder_of(app, fw);
        tiku_trk_select_window_open(&app->select_window, ffl, 520, 300);
        {
            const char *e = "gpio*";
            while (*e != '\0') {
                (void)tiku_field_key(&select_model(app)->expr,
                                        TIKU_FIELD_CHAR, 0, *e++);
            }
        }
    }
#if TIKU_SHELL_BOARD_SERVICES
    if (o->shot_live) {
        /* Search /dev for the nodes whose value is 1, write 1 to
         * /dev/led2, and let the results window's own refresh apply the
         * difference. */
        char err[128];
        open_panel(app);
        tiku_trk_find_set_mode(find_model(app), TIKU_QMODE_ATTRIBUTE);
        find_model(app)->row[0].field = TIKU_QF_VALUE;
        find_model(app)->row[0].op = TIKU_OP_IS;
        snprintf(find_model(app)->row[0].value, sizeof find_model(app)->row[0].value, "1");
        find_model(app)->everywhere = 0;
        {
            int k;

            for (k = 0; k < find_model(app)->place_count; k++) {
                if (strcmp(find_model(app)->place[k], "dev") == 0) {
                    find_model(app)->place_mask = 1u << k;
                }
            }
        }
        (void)panel_search(app, find_model(app));
        (void)app->backend->ops->write(app->backend, "/dev/led2", "1", 1, err,
                                      sizeof err);
        {
            int k;

            for (k = 0; k < app->folder_count; k++) {
                (void)refresh_results(app, &app->folders[k]);
            }
        }
        tiku_trk_find_window_close(&app->find_window);
    }
#endif

    if (o->shot_panel) {
        /* Set the panel up through its model: open it, choose attribute
         * mode, fill two rows joined by or, scope it to the first place,
         * and search. */
        open_panel(app);
        tiku_trk_find_set_mode(find_model(app), TIKU_QMODE_ATTRIBUTE);
        (void)tiku_trk_find_add_row(find_model(app));
        find_model(app)->row[0].field = TIKU_QF_UNIT;
        find_model(app)->row[0].op = TIKU_OP_IS;
        snprintf(find_model(app)->row[0].value, sizeof find_model(app)->row[0].value,
                 "bytes");
        find_model(app)->row[0].join = TIKU_JOIN_OR;
        find_model(app)->row[1].field = TIKU_QF_UNIT;
        find_model(app)->row[1].op = TIKU_OP_IS;
        snprintf(find_model(app)->row[1].value, sizeof find_model(app)->row[1].value, "s");
        find_model(app)->everywhere = 0;
        find_model(app)->place_mask = 1u;
        (void)panel_search(app, find_model(app));
        open_panel(app);
    }
    if (o->shot_find != NULL) {
        snprintf(app->find_buf, sizeof app->find_buf, "%.60s", o->shot_find);
        app->find_len = (int)strlen(app->find_buf);
        run_find(app, &app->folders[app->folder_count - 1]);
    }
    if (o->shot_edit != NULL) {
        /* Select the node named before the '=', start an edit with
         * begin_edit(), put the text after the '=' in the field, and
         * commit it with commit_edit(). */
        folder_t *fl = &app->folders[app->folder_count - 1];
        int i2, n2 = tiku_trk_poseview_count(fl->view);
        const char *eq = strchr(o->shot_edit, '=');
        char want[32];

        snprintf(want, sizeof want, "%.*s",
                 (int)(eq ? (size_t)(eq - o->shot_edit) : strlen(o->shot_edit)),
                 o->shot_edit);
        for (i2 = 0; i2 < n2; i2++) {
            tiku_trk_pose_t *p2 = tiku_trk_poseview_at(fl->view, i2);

            if (p2 != NULL && strcmp(p2->model.name, want) == 0) {
                tiku_trk_poseview_click(fl->view, i2, 0u);
                begin_edit(app, fl);
                if (eq != NULL && app->folder_edit.editing) {
                    tiku_field_start(&app->folder_edit.value_edit, eq + 1);
                    commit_edit(app, fl);
                }
                break;
            }
#if TIKU_SHELL_BOARD_SERVICES

        }
    }
    if (o->shot_notify) {
        /* Change a value under the view and let the poll derive the
         * notice; the -shot frame then shows what the notice handler
         * did. */
        folder_t *fl = &app->folders[app->folder_count - 1];
        char err[128];
        unsigned nf;

        (void)app->backend->ops->write(app->backend, "/dev/led0", "1", 1, err,
                                      sizeof err);
        {
            tiku_model_change_t ch[32];
            int nch = -1, complete = 1;

            if (app->backend->ops->changes != NULL) {
                nch = app->backend->ops->changes(app->backend, ch, 32,
                                                &complete);
            }
            nf = tiku_trk_poseview_poll(fl->view, tiku_trk_now(), ch, nch,
                                        complete);
        }
        (void)tiku_trk_poseview_refresh_values(fl->view, 0, 64);
        snprintf(app->status, sizeof app->status,
                 "poll: flags=%u, %d parked, %d rows", nf,
                 tiku_trk_poseview_parked(fl->view),
                 tiku_trk_poseview_count(fl->view));
    }
    if (o->shot_cascade) {
        static const char *dirs[] = { "/sys", "/sys/mem", "/sys/cpu",
                                      "/sys/timer", "/sys/clock",
                                      "/dev/gpio", "/dev/uart", "/dev/i2c",
                                      "/sys/watchdog", "/sys/power" };
        size_t k;

        for (k = 0; k < sizeof dirs / sizeof *dirs; k++) {
            (void)open_folder(app, dirs[k]);
#endif

        }
    }
    if (o->shot_zoom) {
        folder_t *fl = &app->folders[app->folder_count - 1];
        const tiku_trk_column_t *cols;
        int nc = tiku_trk_poseview_columns(fl->view, &cols);
        int wide = 0, k;

        for (k = 0; k < nc; k++) { wide += cols[k].width; }
        (void)tiku_workspace_zoom(app->desk, fl->win, wide + 8,
            tiku_trk_poseview_count(fl->view) * app->row_h + COUNT_H + 6);
    }
    if (o->shot_shift) {
        /* Open a pose context menu, then apply the modifier the way the key
         * handler does, so the shot shows the shifted faces. */
        folder_t *fl = &app->folders[app->folder_count - 1];
        tiku_trk_pose_t *p;
        tiku_trk_sc_ctx_t c;

        tiku_trk_poseview_click(fl->view, 7, 0u);
        p = tiku_trk_poseview_at(fl->view, 7);
        c = menu_ctx(app, fl, 0u);
        build_open_with(app, fl);
        build_per_type(app, fl);
        app->popup = tiku_trk_cwmenu_context(&app->menus,
                                            (p != NULL) ? &p->model : NULL,
                                            &c);
        app->popup_frame = tiku_menu_place(app->popup, 250, 150,
                              (tiku_rect_t){ 0, 0, app->surf->w,
                                                  app->surf->h });
        c = menu_ctx(app, fl, TIKU_TRK_MOD_SHIFT);
        tiku_trk_sc_update(app->popup, &c);
    }
    if (o->shot_menu) {
        /* Select a pose, build its context menu and place it, as a click
         * does. */
        folder_t *fl = &app->folders[app->folder_count - 1];
        tiku_trk_pose_t *p;
        tiku_trk_sc_ctx_t c;

        tiku_trk_poseview_click(fl->view, 1, 0u);
        p = tiku_trk_poseview_at(fl->view, 1);
        c = menu_ctx(app, fl, 0u);
        build_open_with(app, fl);
        build_per_type(app, fl);
        app->popup = tiku_trk_cwmenu_context(&app->menus,
                                            (p != NULL) ? &p->model : NULL,
                                            &c);
        app->popup_frame = tiku_menu_place(app->popup, 250, 160,
                              (tiku_rect_t){ 0, 0, app->surf->w,
                                                  app->surf->h });
    }
    return -1;
}


/**
 * @brief Everything between the command line and the first frame.
 *
 * The namespace and the host window, then the boot screen's stages: the
 * workspace and the Deskbar, settings, fonts, applications, icons and the
 * session; then the drivers and the -shot set-ups.
 *
 * @return -1 once the desktop is up, else the exit code of the stage that
 *         failed.
 */

int
desktop_boot(desktop_t *app, const desktop_options_t *o, desktop_loop_t *lp)
{
    int code;

    code = boot_namespace(app, o, lp);
    if (code >= 0) {
        return code;
    }
    code = boot_deskbar(app, o, lp);
    if (code >= 0) {
        return code;
    }
    code = boot_settings(app, o, lp);
    if (code >= 0) {
        return code;
    }
    code = boot_fonts(app, o, lp);
    if (code >= 0) {
        return code;
    }
    code = boot_applications(app, o, lp);
    if (code >= 0) {
        return code;
    }
    code = boot_icons(app, o, lp);
    if (code >= 0) {
        return code;
    }
    code = boot_session(app, o, lp);
    if (code >= 0) {
        return code;
    }
    code = boot_drivers(app, o, lp);
    if (code >= 0) {
        return code;
    }
    code = boot_shots(app, o, lp);
    if (code >= 0) {
        return code;
    }

    app->running = 1;
    if (desktop_resources_open(app)) {
        fprintf(stderr,"desktop resource providers: startup refused\n"); return 1;
    }
    app->applied_hide_dotfiles = -1;
    app->applied_full_path = -1;
    app->applied_folders_first = -1;
    desktop_damage_all(app);
    /*
     * Everything is built: show the last stage, then let the boot screen
     * fade off the drawn desktop.
     */
    splash_stage(app, "the desktop");
    tiku_trk_splash_done(&app->splash, tiku_trk_now());
    if (o->after != NULL) {
        /* -after: the supervisor restarted the desktop after a failure;
         * an alert says so, and where the exit report is. */
        char says[256];

        snprintf(says, sizeof says,
                 TIKU_SHELL_RESTART_MESSAGE
                 "What happened is in %s", o->after);
        tiku_trail("started after %s", o->after);
        tiku_alert_open(alert_model(app), TIKU_ALERT_WARN, 0, says,
                        tiku_str("alert.btn.ok"));
        desktop_damage_all(app);
    }
    return -1;
}

/**
 * @brief Take the desktop down, naming each stage on the closing screen
 *        before its work: the session, the desktop's windows, the Deskbar,
 *        the applications and channels, and storage.
 * @return the script's result when one ran, else 0.
 */
int
desktop_teardown(desktop_t *app, desktop_loop_t *lp)
{
    int rc = 0;
    int i;

    if (getenv("TIKU_FRAME_STATS")) desktop_frame_report(app, stderr);
#if TIKU_SHELL_BOARD_SERVICES

    int vfs_drained = desktop_vfs_destroy(app);
#endif

    tiku_trk_open_with_window_destroy(&app->open_with_window);
    desktop_resources_close(app);
    tiku_trk_desktop_settings_close(app->desktop_settings);
    app->desktop_settings = NULL;
    tiku_trk_record_settings_close(app->record_settings);
    app->record_settings = NULL;
    tiku_trk_catalog_settings_close(app->catalog_settings);
    app->catalog_settings = NULL;
    tiku_sound_free();
    splash_stage(app, "the session");
    tiku_trk_session_controller_save(&app->session);
    splash_stage(app, "the desktop");
    for (i = 0; i < app->folder_count; i++) {
        tiku_trk_poseview_free(app->folders[i].view);
    }
    tiku_trk_desktop_view_destroy(&app->desktop_view);
    if (tiku_trk_switcher_window_shown(&app->switcher_window)) {
        tiku_trk_switcher_window_close(&app->switcher_window, 0);
    }
    tiku_trk_panel_window_destroy(&app->file_panel);
    tiku_trk_deskbar_prefs_window_destroy(&app->deskbar_prefs_window);
    tiku_trk_desktop_prefs_window_destroy(&app->desktop_prefs_window);
    tiku_trk_fonts_window_destroy(&app->fonts_window);
#if TIKU_SHELL_BOARD_SERVICES

    desktop_deviceprefs_destroy(app);
#endif

    desktop_startup_destroy(app);
    desktop_associations_destroy(app);
    desktop_forcequit_destroy(app);
    tiku_trk_sounds_window_destroy(&app->sounds_window);
    tiku_trk_settings_close(app->settings);
    app->settings = NULL;
    tiku_trk_prefs_window_destroy(&app->prefs_window);
    splash_stage(app, "the Deskbar");
    tiku_trk_deskbar_free(app->deskbar);
    splash_stage(app, "applications");
#if TIKU_SHELL_REMOTE_SERVICES

    tiku_print_rpc_close(&app->printing);
#else

    {
        int k;

        boards_unlisten(app);
        for (k = 0; k < TIKU_MOUNT_MAX; k++) {
            tiku_channel_close(app->channels[k]);
            app->channels[k] = NULL;
        }
    }
#endif

    tiku_remote_shutdown(&app->remote);
    tiku_workspace_free(app->desk);
    splash_stage(app, "storage");
    /* Quitting waits up to five seconds for a file operation under way
     * (AW-009), so a copy is not cut off mid-file. */
    desktop_wire_wait(app, &app->fs_job, 5000L);
    tiku_trk_session_drain_filesystem(app->fs);
    if (lp->sc != NULL) {
        rc = tiku_trk_script_report(lp->sc, "desktop");
        tiku_trk_script_free(lp->sc);
    }
    tiku_fs_free(app->fs);
#if TIKU_SHELL_BOARD_SERVICES

    /* A timed-out service worker owns its backend until process exit. */
    if (vfs_drained) {
        tiku_trk_resource_session_close(app->resource_session);
        tiku_backend_close(app->backend);
    }
#else

    tiku_trk_resource_session_close(app->resource_session);
    tiku_backend_close(app->backend);
#endif

    if (app->conduct_on) {
        tiku_conduct_shutdown(&app->conduct);
    }
    /* No stage follows the last line, so its minimum time is waited out
     * here. */
    splash_settle(app);
    tiku_host_close(app->host);
    tiku_surface_free(app->surf);
    return rc;
}
