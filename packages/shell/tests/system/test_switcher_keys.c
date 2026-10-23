/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_switcher_keys.c - the desk's keys while the switcher is up.
 *
 * Three applications are on the Deskbar.  Cmd+Tab brings the switcher up
 * on the second, and each further Tab steps the selection once.  The
 * desk's key responder is called as the event loop calls it.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "../../system/desktop/tiku_trk_desktop_keys.c"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails;

/** @brief One result line; a failure counts. */
static void
check(int ok, const char *what)
{
    printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) {
        fails++;
    }
}

/** @brief Main's file is not linked; nothing here has folders to sync. */
void sync_folder_preferences(desktop_t *app) { (void)app; }

/** @brief Press Tab with @p modifiers through the desk's key responder. */
static void
press_tab(desktop_t *app, unsigned modifiers)
{
    tiku_event_t ev;
    tiku_rect_t damage;

    memset(&ev, 0, sizeof ev);
    ev.type = TIKU_EVENT_KEY_DOWN;
    ev.key = TIKU_KEY_TAB;
    ev.modifiers = modifiers;
    (void)keys_key(app, &ev, &damage);
}

/** @brief Whether the switcher's selection is the Deskbar's entry @p i. */
static int
selects(desktop_t *app, int i)
{
    const char *sel = tiku_trk_switcher_window_selected(&app->switcher_window);
    const char *want = tiku_trk_deskbar_team_name(app->deskbar, i);

    printf("  (selected %s, entry %d is %s)\n", sel ? sel : "(none)", i,
           want ? want : "(none)");
    return sel != NULL && want != NULL && strcmp(sel, want) == 0;
}

int
main(void)
{
    desktop_t *app = calloc(1, sizeof *app);

    if (app == NULL) {
        return 1;
    }
    app->surf = tiku_surface_new(640, 480, TIKU_C_PANEL);
    app->desk = (app->surf != NULL) ? tiku_workspace_new(app->surf) : NULL;
    app->deskbar = tiku_trk_deskbar_new(NULL);
    if (app->desk == NULL || app->deskbar == NULL) {
        printf("  [FAIL] a desk and a Deskbar\n");
        return 1;
    }
    (void)tiku_trk_deskbar_add_team(app->deskbar, "Alpha");
    (void)tiku_trk_deskbar_add_team(app->deskbar, "Beta");
    (void)tiku_trk_deskbar_add_team(app->deskbar, "Gamma");
    tiku_trk_folder_lifecycle_init(&app->folder_lifecycle, app->folders,
        TIKU_WINDOW_MAX, &app->folder_count, &app->cascade_x,
        &app->cascade_y, app->desk, NULL, NULL, prefs_model(app),
        &app->folder_render, app->status, sizeof app->status, &app->dirty,
        640, 480);
    tiku_trk_switcher_window_init(&app->switcher_window, app->desk,
        app->deskbar, 640, 480, &app->dirty, app->status,
        sizeof app->status);

    printf("Tab while the switcher is up (D-048)\n");
    check(tiku_trk_deskbar_team_count(app->deskbar) == 3,
          "three applications are on the Deskbar");
    press_tab(app, TIKU_MOD_CMD);
    check(tiku_trk_switcher_window_shown(&app->switcher_window) &&
          selects(app, 1), "Cmd+Tab shows the switcher on the second");
    press_tab(app, TIKU_MOD_CMD);
    check(selects(app, 2), "a second Tab with Cmd held selects the third");
    press_tab(app, TIKU_MOD_CMD | TIKU_MOD_SHIFT);
    check(selects(app, 1), "and Shift+Tab steps back one");

    tiku_trk_switcher_window_close(&app->switcher_window, 0);
    printf("%s\n", fails ? "FAILURES" : "all passed");
    return fails ? 1 : 0;
}
