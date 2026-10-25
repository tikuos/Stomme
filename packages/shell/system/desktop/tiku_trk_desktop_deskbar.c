/*
 * Desktop shell and file manager.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_desktop_deskbar.c - the Deskbar's picks: what each row of its
 * menus does, from opening a window to shutting the desktop down; and the
 * icons its rows and the menus paint.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_shell_product.h"
#include "tiku_sys.h"
#include "tiku_trk_desktop_private.h"

/* Used before they are defined. */
int deskbar_pick_clock(desktop_t *app, tiku_trk_db_pick_t *pick);
static void team_activate(desktop_t *app, int team);
static void team_launch(desktop_t *app, int team);
static void team_close(desktop_t *app, int team);
int deskbar_pick_team(desktop_t *app, tiku_trk_db_pick_t *pick, int x, int y);
int deskbar_pick_system(desktop_t *app, tiku_trk_db_pick_t *pick);
/* End of the forward declarations. */

/** @brief Open the Tracker preferences window. */
void
open_preferences(void *context)
{
    desktop_t *app = context;

    tiku_trk_prefs_window_open(&app->prefs_window);
}

/**
 * @brief Paint the icon a Deskbar row named when it was built (DB-002).
 * @return nonzero when an icon was painted.
 */
int
deskbar_menu_icon(void *context, tiku_surface_t *s,
                  const tiku_trk_db_item_t *item, int x, int y, int size)
{
    (void)context;
    if ((item->kind != TIKU_TRK_DB_ITEM_ROW &&
         item->kind != TIKU_TRK_DB_ITEM_EXPAND) || item->icon[0] == '\0') {
        return 0;               /* a separator, a note, or a row with none */
    }
    return tiku_icon_paint(s, item->icon, x, y, size);
}

/**
 * @brief Paint a menu item's icon, dimmed for a disabled item (IV-054).
 * @return nonzero when an icon was painted.
 */
int
paint_menu_icon(tiku_surface_t *s, const char *icon, int x, int y,
                int size, int dimmed)
{
    if (icon != NULL && strcmp(icon, MARK_ICON) == 0) {
        /* The system mark is drawn, not looked up, and never dimmed:
         * the rows it marks are never disabled. */
        tiku_logo_paint(s, (tiku_rect_t){ x, y, size, size }, 0u);
        return 1;
    }
    return dimmed
        ? tiku_icon_paint_dim(s, icon, x, y, size, 0.55f,
                                  TIKU_C_PANEL)
        : tiku_icon_paint(s, icon, x, y, size);
}

/** @brief Bring application @p team to the front: its windows back on the
 *         desk, the topmost of them focused (DB-019). */
static void
team_activate(desktop_t *app, int team)
{
    tiku_window_t *top = NULL;
    int k, m = tiku_workspace_count(app->desk);

    for (k = 0; k < m; k++) {
        tiku_window_t *ww = tiku_workspace_at(app->desk, k);

        if (desktop_team_has(app, team, ww)) {
            ww->minimized = 0;
            top = ww;
        }
    }
    if (top != NULL) {
        tiku_workspace_activate(app->desk, top);
    }
}

/** @brief Start another instance of application @p team from the
 *         installed applications (DB-021). */
static void
team_launch(desktop_t *app, int team)
{
    const char *name = tiku_trk_deskbar_team_app(app->deskbar, team);

    for (int i = 0; name != NULL && i < app->apps.count; i++) {
        if (strcasecmp(app->apps.app[i].name, name) == 0) {
            (void)tiku_trk_launch_run(&app->apps.app[i], NULL, 0);
            return;
        }
    }
}

/**
 * @brief Close every window of team @p team: an application's windows and
 *        the desktop's folder windows through close_window(), as their own
 *        Close does; the desktop's other windows directly on the workspace.
 */
static void
team_close(desktop_t *app, int team)
{
    int shell = !desktop_team_is_app(app, team);

    for (int k = tiku_workspace_count(app->desk) - 1; k >= 0; k--) {
        tiku_window_t *ww = tiku_workspace_at(app->desk, k);

        if (!desktop_team_has(app, team, ww)) {
            continue;
        }
        if (shell && folder_of(app, ww) == NULL) {
            tiku_workspace_close(app->desk, ww);
        } else {
            close_window(app, ww);
        }
    }
}

/**
 * @brief Carry out what a Deskbar click asked for: a tray icon's click goes
 *        to its owner, then the system, team and clock rows.
 * @note Called on a press; on the release of a Cmd press that never
 *       moved, since a Cmd press may begin a re-dock drag (DB-049,
 *       DB-019, DB-021); and on the release of a program's row that was
 *       not dragged.
 */
void
deskbar_pick_dispatch(desktop_t *app, tiku_trk_db_pick_t *pick, int x, int y)
{
    if (pick->what == TIKU_TRK_DB_TRAY_ICON) {     /* told to its owner */
        desktop_tray_hit(app, pick->owner, pick->path);
        return;
    }
    if (deskbar_pick_system(app, pick)) {
        return;
    }
    if (deskbar_pick_team(app, pick, x, y)) {
        return;
    }
    (void)deskbar_pick_clock(app, pick);
}

/**
 * @brief Apply a clock pick (show, hide, seconds, day, zone) to the Deskbar
 *        settings, and save them.
 * @return 1 for a clock pick.
 */
int
deskbar_pick_clock(desktop_t *app, tiku_trk_db_pick_t *pick)
{
    switch (pick->what) {

    case TIKU_TRK_DB_CLOCK_HIDE:
    case TIKU_TRK_DB_CLOCK_SHOW:
        app->deskbar_prefs_window.model.value.show_clock =
            (pick->what == TIKU_TRK_DB_CLOCK_SHOW);
        tiku_trk_deskbar_settings_normalize(
            &app->deskbar_prefs_window.model.value);
        (void)tiku_trk_record_settings_submit(app->record_settings, 0);
        break;
    case TIKU_TRK_DB_CLOCK_SECONDS:
    case TIKU_TRK_DB_CLOCK_DAY:
    case TIKU_TRK_DB_CLOCK_ZONE:
        if (pick->what == TIKU_TRK_DB_CLOCK_SECONDS) {
            app->deskbar_prefs_window.model.value.show_seconds =
                !app->deskbar_prefs_window.model.value.show_seconds;
        } else if (pick->what == TIKU_TRK_DB_CLOCK_DAY) {
            app->deskbar_prefs_window.model.value.show_day_of_week =
                !app->deskbar_prefs_window.model.value.show_day_of_week;
        } else {
            app->deskbar_prefs_window.model.value.show_timezone =
                !app->deskbar_prefs_window.model.value.show_timezone;
        }
        tiku_trk_deskbar_settings_normalize(
            &app->deskbar_prefs_window.model.value);
        (void)tiku_trk_record_settings_submit(app->record_settings, 0);
        break;
    default:
        return 0;
    }
    return 1;
}

/**
 * @brief A pick on a team or one of its windows: activate, minimise, close,
 *        launch, quit, kill, hide all and show all.
 * @return 1 for a pick of this group.
 */
int
deskbar_pick_team(desktop_t *app, tiku_trk_db_pick_t *pick, int x, int y)
{
    tiku_window_t *t;

    switch (pick->what) {

    case TIKU_TRK_DB_ACTIVATE_WINDOW:
        /* Ignored during a drag: the drop takes the release, and a
         * window raised under the pointer becomes the drop target
         * (DB-030). */
        if (tiku_trk_drag_active(&app->drag)) {
            break;
        }
        t = tiku_workspace_find(app->desk, pick->tag);
        if (t != NULL) {
            /* Restored, rising as an opening window does when it was
             * minimized, and focused (DB-030). */
            tiku_workspace_restore(app->desk, t);
            tiku_workspace_activate(app->desk, t);
        }
        break;
    case TIKU_TRK_DB_MINIMIZE_WINDOW:
        if (tiku_trk_drag_active(&app->drag)) {
            break;
        }
        t = tiku_workspace_find(app->desk, pick->tag);
        if (t != NULL) {
            /* Minimized: a ghost of the window shrinks toward the
             * pointer. */
            if (!t->minimized) {
                tiku_workspace_minimize(app->desk, t,
                    (tiku_rect_t){ x - 8, y - 8, 16, 16 });
            }
            tiku_sound_play(TIKU_SOUND_WINDOW_MINIMIZE);
        }
        break;
    case TIKU_TRK_DB_CLOSE_WINDOW:
        t = tiku_workspace_find(app->desk, pick->tag);
        close_window(app, t);
        break;
    case TIKU_TRK_DB_ACTIVATE_TEAM:
        team_activate(app, pick->team);
        break;
    case TIKU_TRK_DB_LAUNCH_TEAM: {
        /* Middle click: another instance of the same
         * image (DB-021). */
        const char *cmd =
            tiku_trk_deskbar_team_command(app->deskbar,
                                          pick->team);

        if (cmd[0] == '\0') {      /* an application's row */
            team_launch(app, pick->team);
        } else {
            pid_t child = fork();

            if (child == 0) {
                execlp(cmd, cmd, (char *)NULL);
                _exit(127);
            }
            if (child > 0) {
                const char *leaf = strrchr(cmd, '/');
                tiku_trk_launch_note(child, leaf ? leaf + 1 : cmd);
            }
        }
        break;
    }
    case TIKU_TRK_DB_QUIT_TEAM:
    case TIKU_TRK_DB_KILL_TEAM:
        /* Quit on an application's row closes its windows, as its own
         * Close does; the application ends with its last window. */
        if (pick->what == TIKU_TRK_DB_QUIT_TEAM &&
            desktop_team_is_app(app, pick->team) &&
            tiku_trk_deskbar_team_windows(app->deskbar, pick->team,
                                           NULL, NULL) > 0) {
            team_close(app, pick->team);
            break;
        }
        /* Kill, and Quit on other rows, open the Force Quit picker: a
         * roster name can cover unrelated processes, the desktop among
         * them, and the picker confirms one authenticated process. */
        desktop_forcequit_open(app);
        break;
    case TIKU_TRK_DB_HIDE_ALL:
    case TIKU_TRK_DB_SHOW_ALL: {
        int k, m = tiku_workspace_count(app->desk);
        for (k = 0; k < m; k++) {
            tiku_window_t *ww =
                tiku_workspace_at(app->desk, k);
            if (desktop_team_has(app, pick->team, ww)) {
                ww->minimized =
                    (pick->what == TIKU_TRK_DB_HIDE_ALL);
            }
        }
        break;
    }
    case TIKU_TRK_DB_CLOSE_ALL:
        /* Close All closes the team's windows and quits nothing. */
        team_close(app, pick->team);
        break;
    default:
        return 0;
    }
    return 1;
}

/**
 * @brief A pick on the leaf menu's own rows: Find, the preferences, Help,
 *        Force Quit, an application, the theme, the replicants, About,
 *        Shut down, a folder and a described panel.
 * @return 1 for a pick of this group.
 */
int
deskbar_pick_system(desktop_t *app, tiku_trk_db_pick_t *pick)
{
    switch (pick->what) {

    case TIKU_TRK_DB_FIND:
        /* Find opens the Find panel, as Cmd-F does. */
        open_panel(app);
        break;
    case TIKU_TRK_DB_PREFERENCES:
        tiku_trk_deskbar_prefs_window_open(
            &app->deskbar_prefs_window);
        break;
    case TIKU_TRK_DB_TRACKER_PREFERENCES:
        tiku_trk_prefs_window_open(&app->prefs_window);
        break;
    case TIKU_TRK_DB_FONT_PREFERENCES:
        tiku_trk_fonts_window_open(&app->fonts_window);
        break;
    case TIKU_TRK_DB_HELP:
        (void)desktop_help_open(app, NULL);
        break;
    case TIKU_TRK_DB_FORCE_QUIT:
        desktop_forcequit_open(app);
        break;
    case TIKU_TRK_DB_SOUND_PREFERENCES:
        tiku_trk_sounds_window_open(&app->sounds_window);
        break;
#if TIKU_SHELL_BOARD_SERVICES

    case TIKU_TRK_DB_DEVICE_PREFERENCES:
        desktop_deviceprefs_open(app);
        break;
#endif

    case TIKU_TRK_DB_STARTUP_PREFERENCES:
        desktop_startup_open(app);
        break;
    case TIKU_TRK_DB_ASSOC_PREFERENCES:
        desktop_associations_open(app);
        break;
    case TIKU_TRK_DB_DESKTOP_PREFERENCES:
        tiku_trk_desktop_prefs_window_open(&app->desktop_prefs_window);
        break;
    case TIKU_TRK_DB_LAUNCH_APP: {
        if (!strcmp(pick->path, "builtin:reader")) {
            (void)desktop_launch_app(app, "reader", NULL);
            break;
        }
        if (!strcmp(pick->path, "builtin:viewer")) {
            (void)desktop_launch_app(app, "image", NULL);
#if TIKU_SHELL_BOARD_SERVICES

            break;
        }
        if (!strcmp(pick->path, "builtin:monitor") ||
            !strcmp(pick->path, "builtin:vfs")) {
            (void)desktop_launch_app(app,
                !strcmp(pick->path, "builtin:vfs") ? "vfs" : "monitor", NULL);
#endif

            break;
        }
        /* A program row starts the program, which opens its own
         * windows through its session. */
        tiku_trk_app_t program;

        const char *args[1];
        size_t n = strlen(pick->path);
        int nargs = 0;

        const char *leaf = strrchr(pick->path, '/');

        memset(&program, 0, sizeof program);
        /* The program's name is the path's leaf: the name field is
         * short. */
        snprintf(program.name, sizeof program.name, "%s",
                 (leaf != NULL) ? leaf + 1 : pick->path);
        if (n > 3u && strcmp(pick->path + n - 3, ".so") == 0) {
            /* A loadable application (.so) runs in the runner, a
             * process of its own. */
            snprintf(program.cmd, sizeof program.cmd, "%s", app->runner);
            args[nargs++] = pick->path;
        } else {
            snprintf(program.cmd, sizeof program.cmd, "%s", pick->path);
        }
        if (tiku_trk_launch_run(&program, (nargs > 0) ? args : NULL,
                                nargs) != TIKU_TRK_LAUNCH_OK) {
            snprintf(app->status, sizeof app->status,
                     "could not start %s", pick->path);
            desktop_damage_all(app);
        }
        break;
    }
    case TIKU_TRK_DB_TOGGLE_DARK: {
        /* The switch is the DarkMode setting: step it, save it, and let
         * sync_folder_preferences() apply it, so the switch, the
         * preferences and the settings file agree. */
        tiku_trk_prefs_t *prefs = prefs_model(app);

        /* DarkMode has three values; each press steps to the next,
         * wrapping round. */
        tiku_trk_prefs_set(prefs, "DarkMode",
                           (tiku_trk_prefs_get(prefs, "DarkMode") + 1) % 3);
        (void)tiku_trk_prefs_submit(prefs, app->prefs_window.path);
        sync_folder_preferences(app);
        desktop_damage_all(app);
        break;
    }
    case TIKU_TRK_DB_TOGGLE_REPLICANTS:
        /* The bar has already flipped it; the setting is saved with the
         * rest of the Deskbar's. */
        app->deskbar_prefs_window.model.value.show_replicants =
            tiku_trk_deskbar_show_replicants(app->deskbar);
        (void)tiku_trk_record_settings_submit(app->record_settings, 0);
        tiku_trk_deskbar_apply_settings(app->deskbar,
            &app->deskbar_prefs_window.model.value);
        desktop_damage_all(app);
        break;
    case TIKU_TRK_DB_SHOW_ABOUT:
        (void)embed_open_or_raise(app, &tiku_trk_about_app);
        break;
    case TIKU_TRK_DB_QUIT:
        /* Shut down asks first (DB-006). */
        tiku_sound_play(TIKU_SOUND_BEEP);
        tiku_alert_open(alert_model(app),
            TIKU_ALERT_WARN,
            TIKU_TRK_ALERT_TAG_SHUTDOWN,
            "Shut down the desktop?",
            tiku_str("alert.btn.shut_down"));
        break;
    case TIKU_TRK_DB_OPEN_PATH:
        (void)open_folder(app, pick->path);
        break;
    case TIKU_TRK_DB_OPEN_PANEL:
        (void)open_panel_path(app, pick->path);
        break;
    default:
        return 0;
    }
    return 1;
}
