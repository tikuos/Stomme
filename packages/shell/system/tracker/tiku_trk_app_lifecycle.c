/*
 * Desktop shell and file manager.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_app_lifecycle.c - the command line, the boot up to the first
 * frame, and the teardown.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_shell_product.h"
#include "tiku_trk_app_private.h"

/**
 * @brief Close the window and the surface, let file work under way finish,
 *        report the script's verdict, and free the rest.
 * @return the script's verdict, or 0 when no script ran.
 */
int
browser_teardown(app_t *app, browser_loop_t *lp)
{
    int rc = 0;

    browser_image_detach(app);
    tiku_sound_free();
    tiku_host_close(app->host);
    tiku_surface_free(app->surf);
    /* Quitting waits up to five seconds for file work already under way
     * (AW-009). */
    browser_drain_fs(app->fs);
    if (lp->sc != NULL) {
        rc = tiku_trk_script_report(lp->sc, "tracker");
        tiku_trk_script_free(lp->sc);
    }
    tiku_fs_free(app->fs);
    tiku_trk_poseview_free(app->view);
    tiku_backend_close(app->backend);
    return rc;
}

/**
 * @brief Everything between the command line and the first frame.
 * @return -1 when every step succeeded; otherwise the exit code of the
 *         step that failed.
 */
int
browser_boot(app_t *app, browser_options_t *o, browser_loop_t *lp)
{
    tiku_sound_init((o->script_path != NULL || o->mute)
                        ? NULL : tiku_sound_host_backend());
    (void)tiku_sound_load_scores();
    if (o->use_device) {
        app->backend = tiku_backend_tiku_open(o->port, o->baud);
        if (app->backend == NULL) {
            fprintf(stderr, "tracker: no device answered\n");
            return 1;
        }
        if (o->start == NULL) { o->start = "/"; }
    } else {
        app->backend = tiku_backend_posix_open();
        if (o->start == NULL) { o->start = getenv("HOME"); }
        if (o->start == NULL) { o->start = "/"; }
    }

    tiku_menu_set_icon_painter(browser_paint_menu_icon);
    app->loop = tiku_trk_loop_new();
    tiku_scroll_init(&app->sb, 1);
    tiku_scroll_init(&app->hsb, 20);
    app->view = tiku_trk_poseview_new(app->backend, o->start);
    if (app->view != NULL) {
        (void)tiku_trk_poseview_restore_state(app->view);
    }
    if (app->view == NULL) {
        fprintf(stderr, "tracker: cannot open %s\n", o->start);
        return 1;
    }
    tiku_trk_navigator_init(&app->nav, tiku_trk_poseview_path(app->view));
    app->fs = tiku_fs_new(app->backend);
    (void)tiku_fs_prepare_trash(app->fs);
    tiku_trk_clip_init(&app->clip);
    app->surf = tiku_surface_new(WIN_W, WIN_H, TIKU_C_PANEL);
    tiku_trk_status_overlay_init(&app->status_overlay, app->surf, WIN_W, WIN_H,
                                 &app->dirty, browser_app_alert_answer,
                                 browser_app_alert_stop, app);
    /* The command layer asks its questions on the overlay's alert, as in
     * the desktop shell (FS-046; rule 6 of docs/contracts/tracker.md). */
    tiku_trk_folder_file_actions_init(&app->file_actions, app->fs, app->backend,
        &app->clip, NULL, NULL,
        tiku_trk_status_overlay_alert(&app->status_overlay), NULL, NULL,
        app->status, sizeof app->status, NULL, NULL);
    /* The row height follows the interface face (IV-013). */
    app->row_h = tiku_font_plain()->height + 5;
    {
        const char *home = getenv("HOME");

        if (home != NULL && home[0] != '\0') {
            snprintf(app->prefs_path, sizeof app->prefs_path,
                     TIKU_SHELL_SETTINGS_PATH, home);
        }
        browser_sync_navigator_preference(app, 1);
    }

    if (o->script_path != NULL) {
        /* Displayless: the shell draws into the same surface, so a
         * scripted run exercises the real draw path (see
         * tiku_trk_script.h). */
        lp->sc = tiku_trk_script_open(o->script_path);
        if (lp->sc == NULL) {
            return 2;
        }
    } else {
        app->host = tiku_host_open(WIN_W, WIN_H, TIKU_SHELL_TRACKER_NAME);
        if (app->host == NULL) {
            fprintf(stderr, "tracker: no display\n");
            return 1;
        }
        if (tiku_host_scale(app->host) > 1) {
            (void)tiku_surface_rescale(app->surf,
                tiku_host_scale(app->host), TIKU_C_PANEL);
        }
    }
    /* The window measures labels for the view: a pose's rectangle grows
     * to fit a long name (PVL-065), and column fitting measures with
     * it. */
    tiku_trk_poseview_set_measure(app->view, browser_measure_text,
                                  (void *)tiku_font_plain());
    tiku_trk_poseview_set_font_size(app->view,
                                    tiku_font_plain()->height);
    /* The face metrics the icon-mode cells are derived from. */
    tiku_trk_poseview_set_font_metrics(app->view, tiku_font_plain()->height,
                                       tiku_font_plain()->ascent);
    (void)tiku_trk_poseview_restore_columns(app->view);
    tiku_trk_titleview_init(&app->tv, app->view, TITLE_H);
    browser_settle_layout(app);
    browser_settle_writability(app);
    browser_retitle(app);
    if (getenv("TRK_DEMO_OPS") != NULL) {
        /* TRK_DEMO_OPS: everything selected and duplicated, for a
         * capture of an operation. */
        char paths[64][TIKU_PATH_MAX];
        const char *sel[64];
        int n;

        tiku_trk_poseview_select_all(app->view);
        n = browser_selection_paths(app, paths, sel, 64);
        if (n > 0) {
            (void)tiku_fs_duplicate(app->fs, sel, n);
            snprintf(app->status, sizeof app->status, "duplicating %d items", n);
        }
    }
    if (getenv("TRK_DEMO_ICON") != NULL) {
        /* TRK_DEMO_ICON: icon mode, auto-placed, with the first two icons
         * moved and the second selected. */
        tiku_rect_t lr = browser_list_rect(app);
        tiku_scroll_detach(&app->sb);
        tiku_trk_poseview_set_mode(app->view, TIKU_TRK_MODE_ICON);
        tiku_scroll_attach(&app->sb);
        (void)tiku_trk_poseview_place_unplaced(app->view, lr.w - 4);
        {
            tiku_trk_pose_t *p0 = tiku_trk_poseview_at(app->view, 0);
            tiku_trk_pose_t *p1 = tiku_trk_poseview_at(app->view, 1);
            if (p0 != NULL) { tiku_trk_poseview_move_pose(app->view, p0, 300, 180, 1); }
            if (p1 != NULL) { tiku_trk_poseview_move_pose(app->view, p1, 300, 260, 1); p1->selected = 1; }
        }
        (void)tiku_trk_poseview_save_state(app->view);
    }
    if (getenv("TRK_DEMO_MINI") != NULL) {
        tiku_rect_t lr = browser_list_rect(app);

        tiku_scroll_detach(&app->sb);
        tiku_trk_poseview_set_mode(app->view, TIKU_TRK_MODE_MINI);
        tiku_scroll_attach(&app->sb);
        (void)tiku_trk_poseview_place_unplaced(app->view, lr.w - 4);
        tiku_trk_poseview_click(app->view, 2, 0);
    }
    if (getenv("TRK_DEMO_COLS") != NULL) {
        /* TRK_DEMO_COLS: a resize of the Name column in progress, with
         * its guide line and pressed title. */
        int r = 0;

        (void)tiku_trk_titleview_bounds(&app->tv, TIKU_TRK_COL_NAME, NULL, &r);
        (void)tiku_trk_titleview_mouse_down(&app->tv, r, 9, 1, 1,
                                            tiku_trk_now());
        (void)tiku_trk_titleview_mouse_moved(&app->tv, r - 90, 9,
                                             tiku_trk_now() +
                                             2 * TIKU_TRK_CLICK_SPEED_US);
    }
    if (getenv("TRK_DEMO_FILTER") != NULL) {
        /* TRK_DEMO_FILTER: two filter terms, 'e' and 's', and the first
         * matching row selected. */
        tiku_trk_poseview_set_filtering(app->view, 1);
        tiku_trk_poseview_type_ahead(app->view, 'e');
        (void)tiku_trk_poseview_filter_term(app->view);
        tiku_trk_poseview_type_ahead(app->view, 's');
        tiku_trk_poseview_click(app->view, 0, 0);
    }
    if (getenv("TRK_DEMO_RENAME") != NULL) {
        /* TRK_DEMO_RENAME: the inline editor open on the second row. */
        tiku_trk_poseview_click(app->view, 1, 0);
        browser_begin_rename(app);
    }
    if (getenv("TRK_DEMO_SELECT") != NULL) {
        /* TRK_DEMO_SELECT: rows 16 to 18 selected by a click and a
         * Shift-click. */
        tiku_trk_poseview_click(app->view, 16, 0);
        tiku_trk_poseview_click(app->view, 18, TIKU_TRK_MOD_SHIFT);
    }
    app->running = 1;
    app->dirty = 1;
    app->active = 1;
    if (o->shot != NULL) {
        /* The -shot frame is due 1.5 s after boot, which gives the first
         * values time to arrive. */
        lp->shot_at_us = tiku_trk_now() + 1500000;
    }
    return -1;
}

/**
 * @brief Fill @p o from the command line: -d, -p port, -b baud, -shot
 *        file, -script file, -mute, -seconds n, and the folder to open.
 * @return -1, to carry on: no option ends the run here.
 */
int
browser_parse_args(int argc, char **argv, browser_options_t *o)
{
    int i;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-d") == 0)                      { o->use_device = 1; }
        else if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) { o->port = argv[++i]; o->use_device = 1; }
        else if (strcmp(argv[i], "-b") == 0 && i + 1 < argc) { o->baud = atoi(argv[++i]); }
        else if (strcmp(argv[i], "-shot") == 0 && i + 1 < argc) { o->shot = argv[++i]; }
        else if (strcmp(argv[i], "-script") == 0 && i + 1 < argc) { o->script_path = argv[++i]; }
        else if (strcmp(argv[i], "-mute") == 0) { o->mute = 1; }
        else if (strcmp(argv[i], "-seconds") == 0 && i + 1 < argc) { o->deadline_us = tiku_trk_now() + (int64_t)atoi(argv[++i]) * 1000000; }
        else                                                 { o->start = argv[i]; }
    }
    return -1;
}
