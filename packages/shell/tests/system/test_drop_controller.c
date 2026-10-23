/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_drop_controller.c - a drop on a file in the open Deskbar menu, and
 * on an application row of the Deskbar.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "tiku_trk_drop_controller.h"
#include "tiku_trk_deskbar_prefs.h"
#include "tiku_trk_launch.h"

static int fails;

static void
check(int cond, const char *row, const char *what)
{
    printf("  [%s] %-9s %s\n", cond ? "PASS" : "FAIL", row, what);
    if (!cond) { fails++; }
}

/** The registered applications the fake shell knows: one by its path, one
 *  by its name. */
static tiku_trk_app_t editor;
static tiku_trk_app_t alpha_app;
static int launches;
static int launched_count;
static const tiku_trk_app_t *launched_app;
static char launched_path[TIKU_PATH_MAX];
static char status_line[256];

static const tiku_trk_app_t *
fake_app_of(void *ctx, const char *path)
{
    (void)ctx;
    return strcmp(path, editor.path) == 0 ? &editor : NULL;
}

static const tiku_trk_app_t *
fake_app_named(void *ctx, const char *name)
{
    (void)ctx;
    return strcmp(name, alpha_app.name) == 0 ? &alpha_app : NULL;
}

static void
fake_status(void *ctx, const char *text)
{
    (void)ctx;
    snprintf(status_line, sizeof status_line, "%s", text);
}

static int
fake_launch(void *ctx, const struct tiku_trk_app *application,
            const char *const *paths, int count)
{
    (void)ctx;
    launches++;
    launched_count = count;
    launched_app = application;
    snprintf(launched_path, sizeof launched_path, "%s",
             count > 0 ? paths[0] : "");
    return 1;
}

/** @brief The y of team @p team's row at @p x, or -1. */
static int
team_row_y(tiku_trk_deskbar_t *db, int x, int team)
{
    tiku_rect_t f = tiku_trk_deskbar_frame(db);
    int which, y;

    for (y = f.y; y < f.y + f.h; y++) {
        if (tiku_trk_deskbar_drop_at(db, x, y, &which) ==
                TIKU_TRK_DB_DROP_TEAM && which == team) {
            return y;
        }
    }
    return -1;
}

/** @brief The y of a point at @p x on the bar itself, off every row, or
 *         -1. */
static int
bar_y(tiku_trk_deskbar_t *db, int x)
{
    tiku_rect_t f = tiku_trk_deskbar_frame(db);
    int which, y;

    for (y = f.y; y < f.y + f.h; y++) {
        if (tiku_trk_deskbar_drop_at(db, x, y, &which) ==
                TIKU_TRK_DB_DROP_BAR) {
            return y;
        }
    }
    return -1;
}

/** @brief The y of the menu row whose path is @p want, or -1. */
static int
menu_row_y(tiku_trk_deskbar_t *db, int x, const char *want)
{
    char into[TIKU_PATH_MAX];
    int y;

    for (y = 0; y < 600; y++) {
        if (tiku_trk_deskbar_menu_drop_at(db, x, y, into, sizeof into) ==
                TIKU_TRK_DB_DROP_MENU_FILE &&
            strcmp(into, want) == 0) {
            return y;
        }
    }
    return -1;
}

int
main(void)
{
    tiku_backend_t *b = tiku_backend_posix_open();
    tiku_trk_deskbar_t *db = tiku_trk_deskbar_new(b);
    tiku_trk_services_t services = { 0 };
    tiku_trk_drop_controller_t controller;
    tiku_trk_folder_window_t *beneath = NULL;
    tiku_trk_drag_t drag;
    tiku_menu_t *popup = NULL;
    tiku_menu_t *popup_sub = NULL;
    tiku_rect_t popup_frame = { 0, 0, 0, 0 };
    char progress[TIKU_PATH_MAX];
    char dir[] = "/tmp/trk_dropctl_XXXXXX";
    char plain[TIKU_PATH_MAX];
    int drop_row = -1;
    int x;
    int y;
    FILE *f;
    tiku_rect_t frame;

    if (db == NULL || mkdtemp(dir) == NULL) {
        return 1;
    }
    snprintf(editor.name, sizeof editor.name, "Editor");
    snprintf(editor.path, sizeof editor.path, "%s/Editor", dir);
    editor.takes_anything = 1;
    snprintf(plain, sizeof plain, "%s/notes", dir);
    if ((f = fopen(editor.path, "w")) == NULL) { return 1; }
    (void)fclose(f);
    if ((f = fopen(plain, "w")) == NULL) { return 1; }
    (void)fclose(f);
    {   /* The frame is set by a draw. */
        tiku_surface_t *sfc = tiku_surface_new(1000, 700, TIKU_C_BACKDROP);

        tiku_trk_deskbar_draw(db, sfc);
        tiku_surface_free(sfc);
    }
    frame = tiku_trk_deskbar_frame(db);
    x = frame.x - 20;
    tiku_trk_deskbar_set_menu_dir(db, "/tmp/trk_dropctl_missing", dir);
    tiku_trk_deskbar_open_menu(db);

    services.app_of = fake_app_of;
    services.status = fake_status;
    memset(&drag, 0, sizeof drag);
    drag.count = 1;
    snprintf(drag.path[0], sizeof drag.path[0], "%s", plain);
    snprintf(drag.src_dir, sizeof drag.src_dir, "%s", dir);
    tiku_trk_drop_controller_init(&controller, db, b, NULL, NULL, NULL,
        NULL, &drag, &beneath, &drop_row, &popup, &popup_sub, &popup_frame,
        progress, sizeof progress, &services, 1000, 700, NULL, NULL);
    tiku_trk_drop_controller_set_launcher(&controller, fake_launch, NULL);

    printf("a drop on a file in the open Deskbar menu (D-005)\n");
    y = menu_row_y(db, x, editor.path);
    check(y >= 0, "D-005", "the application is a file row of the menu");
    tiku_trk_drop_controller_execute(&controller, x, y, 0u, 1u);
    check(launches == 1 && launched_count == 1 &&
          strcmp(launched_path, plain) == 0, "D-005",
          "an application row launches with the dropped document");

    y = menu_row_y(db, x, plain);
    status_line[0] = '\0';
    launches = 0;
    snprintf(drag.path[0], sizeof drag.path[0], "%s", editor.path);
    tiku_trk_drop_controller_execute(&controller, x, y, 0u, 1u);
    check(y >= 0 && launches == 0 &&
          strstr(status_line, "not a registered application") != NULL,
          "D-005",
          "a row that is not an application refuses the drop on the "
          "status line");

    printf("\na drop on an application row of a sorted list (D-193)\n");
    {
        /* Zulu is added first and Alpha second, so in name order the
         * first row shows the second entry.  Writer is known only by the
         * image its process was started from. */
        tiku_trk_deskbar_t *sorted = tiku_trk_deskbar_new(b);
        tiku_trk_deskbar_settings_t settings;
        tiku_surface_t *sfc = tiku_surface_new(1000, 700, TIKU_C_BACKDROP);
        int zulu, alpha, writer, row_y, ry, which = -1;

        snprintf(alpha_app.name, sizeof alpha_app.name, "Alpha");
        zulu = tiku_trk_deskbar_add_team(sorted, "Zulu");
        alpha = tiku_trk_deskbar_add_team(sorted, "Alpha");
        writer = tiku_trk_deskbar_add_team_pid(sorted, "Writer", 4242L,
                                               editor.path, NULL);
        tiku_trk_deskbar_settings_default(&settings);
        settings.sort_apps = 1;
        tiku_trk_deskbar_apply_settings(sorted, &settings);
        tiku_trk_deskbar_draw(sorted, sfc);
        tiku_surface_free(sfc);
        frame = tiku_trk_deskbar_frame(sorted);
        x = frame.x + 10;
        row_y = -1;
        for (ry = frame.y; ry < frame.y + frame.h && row_y < 0; ry++) {
            if (tiku_trk_deskbar_drop_at(sorted, x, ry, &which) ==
                TIKU_TRK_DB_DROP_TEAM) {
                row_y = ry;
            }
        }
        check(row_y >= 0 && which == alpha && alpha != 0 &&
              strcmp(tiku_trk_deskbar_team_name(sorted, 0), "Alpha") == 0,
              "D-193", "the first row shows Alpha, the second entry added");

        services.app_named = fake_app_named;
        tiku_trk_drop_controller_init(&controller, sorted, b, NULL, NULL,
            NULL, NULL, &drag, &beneath, &drop_row, &popup, &popup_sub,
            &popup_frame, progress, sizeof progress, &services, 1000, 700,
            NULL, NULL);
        tiku_trk_drop_controller_set_launcher(&controller, fake_launch, NULL);
        drag.count = 1;
        snprintf(drag.path[0], sizeof drag.path[0], "%s", plain);
        status_line[0] = '\0';
        launches = 0;
        tiku_trk_drop_controller_execute(&controller, x, row_y, 0u, 1u);
        check(strcmp(status_line, "Alpha cannot open that") == 0 &&
              launches == 0, "D-193",
              "a refused drop names the application under the pointer");

        printf("\na drop on an application row launches it (D-024)\n");
        launches = 0;
        launched_app = NULL;
        tiku_trk_drop_controller_execute(&controller, x, row_y,
                                         TIKU_MOD_CMD, 1u);
        check(launches == 1 && launched_app == &alpha_app &&
              launched_count == 1 && strcmp(launched_path, plain) == 0,
              "D-024",
              "a row found by its name launches with the dropped file");

        launches = 0;
        launched_app = NULL;
        row_y = team_row_y(sorted, x, writer);
        tiku_trk_drop_controller_execute(&controller, x, row_y,
                                         TIKU_MOD_CMD, 1u);
        check(row_y >= 0 && launches == 1 && launched_app == &editor &&
              launched_count == 1 && strcmp(launched_path, plain) == 0,
              "D-024",
              "a row found by the image it was started from does the same");

        launches = 0;
        status_line[0] = '\0';
        row_y = team_row_y(sorted, x, zulu);
        tiku_trk_drop_controller_execute(&controller, x, row_y,
                                         TIKU_MOD_CMD, 1u);
        check(row_y >= 0 && launches == 0 &&
              strcmp(status_line, "Zulu is not an installed application")
                  == 0, "D-024",
              "a row with no installed application refuses, by its name");

        printf("\na drop on the bar launches the applications in it "
               "(D-024)\n");
        drag.count = 3;
        snprintf(drag.path[0], sizeof drag.path[0], "%s", editor.path);
        snprintf(drag.path[1], sizeof drag.path[1], "%s", plain);
        snprintf(drag.path[2], sizeof drag.path[2], "%s", dir);
        launches = 0;
        launched_app = NULL;
        status_line[0] = '\0';
        row_y = bar_y(sorted, x);
        tiku_trk_drop_controller_execute(&controller, x, row_y, 0u, 1u);
        check(row_y >= 0 && launches == 1 && launched_app == &editor &&
              launched_count == 0, "D-024",
              "an application launches; the folder is passed over");
        check(strcmp(status_line,
                     "launching 1 application; 1 item is not one") == 0,
              "D-024", "and the document is said not to be launched");

        drag.count = 1;
        snprintf(drag.path[0], sizeof drag.path[0], "%s", plain);
        launches = 0;
        status_line[0] = '\0';
        tiku_trk_drop_controller_execute(&controller, x, row_y, 0u, 1u);
        check(launches == 0 &&
              strcmp(status_line,
                     "nothing launched: 1 item is not an application")
                  == 0, "D-024",
              "a drop with no application in it launches nothing");
        tiku_trk_deskbar_free(sorted);
    }

    tiku_trk_deskbar_free(db);
    tiku_backend_close(b);
    (void)unlink(editor.path);
    (void)unlink(plain);
    (void)rmdir(dir);
    printf("\n%s\n", fails ? "FAILURES" : "Deskbar menu drops hold");
    return fails ? 1 : 0;
}
