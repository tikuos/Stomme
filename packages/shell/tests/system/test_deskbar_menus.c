/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_deskbar_menus.c - the Deskbar's popups and options: which popups
 * count as open, what closing them closes, a drop on a store's row, the
 * raise and expander settings, and the Desktop's top inset.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "tiku_mount.h"
#include "tiku_state.h"
#include "tiku_trk_deskbar.h"
#include "tiku_trk_deskbar_prefs.h"
#include "tiku_trk_desktop_view.h"
#include "tiku_trk_poseview.h"

static int fails;

static void
check(int cond, const char *row, const char *what)
{
    printf("  [%s] %-9s %s\n", cond ? "PASS" : "FAIL", row, what);
    if (!cond) { fails++; }
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

/** @brief How tall the tray's drop zone is at @p x, measured up from the
 *         frame's foot. */
static int
tray_zone_h(tiku_trk_deskbar_t *db, int x)
{
    tiku_rect_t f = tiku_trk_deskbar_frame(db);
    int which, y;

    for (y = f.y; y < f.y + f.h; y++) {
        if (tiku_trk_deskbar_drop_at(db, x, y, &which) ==
                TIKU_TRK_DB_DROP_TRAY) {
            return f.y + f.h - y;
        }
    }
    return 0;
}

/** @brief Click the leaf menu's row labelled @p label, or its submenu's.
 * @return 1 when the row was found. */
static int
click_menu_row(tiku_trk_deskbar_t *db, const char *label,
               tiku_trk_db_pick_t *pick)
{
    int x, y;

    memset(pick, 0, sizeof *pick);
    if (!tiku_trk_deskbar_menu_point(db, label, &x, &y)) {
        return 0;
    }
    (void)tiku_trk_deskbar_click(db, x, y, 0u, 1, 1, pick);
    return 1;
}

/** @brief A label measure: eight pixels a character. */
static int
measure(const char *text, void *ctx)
{
    (void)ctx;
    return (text != NULL) ? (int)strlen(text) * 8 : 0;
}

/** @brief The first placed pose of @p v and its rectangle's top. */
static tiku_trk_pose_t *
first_pose(tiku_trk_poseview_t *v, int *top)
{
    int i, n = tiku_trk_poseview_count(v);

    for (i = 0; i < n; i++) {
        tiku_trk_pose_t *p = tiku_trk_poseview_at(v, i);
        int x, y, w, h;

        if (p != NULL && p->placed) {
            tiku_trk_poseview_pose_rect(v, p, &x, &y, &w, &h);
            *top = y;
            return p;
        }
    }
    return NULL;
}

int
main(void)
{
    tiku_surface_t *s = tiku_surface_new(1000, 700, TIKU_C_BACKDROP);
    tiku_trk_deskbar_t *db = tiku_trk_deskbar_new(NULL);
    tiku_trk_db_pick_t pick;
    tiku_rect_t f;
    int team, y;

    if (s == NULL || db == NULL) { return 1; }
    team = tiku_trk_deskbar_add_team(db, "Editor");
    tiku_trk_deskbar_draw(db, s);
    f = tiku_trk_deskbar_frame(db);

    printf("the window menu and the running-apps menu are popups (D-067, "
           "D-068)\n");
    y = team_row_y(db, f.x + 10, team);
    memset(&pick, 0, sizeof pick);
    (void)tiku_trk_deskbar_click(db, f.x + 10, y, 0u, 3, 1, &pick);
    check(y >= 0 && tiku_trk_deskbar_menu_open(db), "D-067",
          "a secondary click on a row opens its window menu, and the bar "
          "says a popup is open");
    tiku_trk_deskbar_close_menu(db);
    check(!tiku_trk_deskbar_menu_open(db), "D-067",
          "closing every popup closes the window menu");
    tiku_trk_deskbar_set_dock(db, TIKU_TRK_DB_RIGHT_MINI);
    tiku_trk_deskbar_draw(db, s);
    f = tiku_trk_deskbar_frame(db);
    memset(&pick, 0, sizeof pick);
    (void)tiku_trk_deskbar_click(db, f.x + f.w / 2, f.y + 26 + 2 + 10, 0u,
                                 1, 1, &pick);
    check(tiku_trk_deskbar_apps_menu_open(db) &&
          tiku_trk_deskbar_menu_open(db), "D-067",
          "the mini dock's running-apps menu counts as an open popup");
    tiku_trk_deskbar_close_menu(db);
    check(!tiku_trk_deskbar_apps_menu_open(db) &&
          !tiku_trk_deskbar_menu_open(db), "D-068",
          "closing every popup closes the running-apps menu");
    tiku_trk_deskbar_set_dock(db, TIKU_TRK_DB_RIGHT_EXPANDED);
    tiku_trk_deskbar_draw(db, s);
    f = tiku_trk_deskbar_frame(db);

    printf("\nraising with Always on top off (D-202)\n");
    {
        tiku_trk_deskbar_settings_t settings;

        tiku_trk_deskbar_settings_default(&settings);
        settings.always_on_top = 0;
        settings.auto_raise = 0;
        tiku_trk_deskbar_apply_settings(db, &settings);
        (void)tiku_trk_deskbar_pointer(db, f.x + 2, f.y + 2);
        check(!tiku_trk_deskbar_raised(db), "D-202",
              "without Auto-raise the pointer on the bar does not raise it");
        y = team_row_y(db, f.x + 10, team);
        (void)tiku_trk_deskbar_click(db, f.x + 10, y, 0u, 3, 1, &pick);
        (void)tiku_trk_deskbar_pointer(db, 100, 300);
        check(tiku_trk_deskbar_raised(db), "D-202",
              "an open window menu raises the bar, so the menu is drawn "
              "over the windows");
        tiku_trk_deskbar_close_menu(db);
        settings.auto_raise = 1;
        tiku_trk_deskbar_apply_settings(db, &settings);
        check(!tiku_trk_deskbar_raised(db), "D-202",
              "with Auto-raise on and the pointer away it is not raised");
        (void)tiku_trk_deskbar_pointer(db, f.x + 2, f.y + 2);
        check(tiku_trk_deskbar_raised(db), "D-202",
              "and the pointer on the bar raises it");
        (void)tiku_trk_deskbar_pointer(db, 100, 300);
        tiku_trk_deskbar_settings_default(&settings);
        tiku_trk_deskbar_apply_settings(db, &settings);
    }

    printf("\na new row with the expander hidden (D-203)\n");
    {
        tiku_trk_deskbar_settings_t settings;
        int fresh;

        tiku_trk_deskbar_settings_default(&settings);
        settings.show_expander = 0;
        settings.expand_new_apps = 1;
        tiku_trk_deskbar_apply_settings(db, &settings);
        fresh = tiku_trk_deskbar_add_team(db, "Player");
        check(fresh >= 0 && !tiku_trk_deskbar_team_expanded(db, fresh),
              "D-203", "a row added with no expander starts folded");
        tiku_trk_deskbar_settings_default(&settings);
        tiku_trk_deskbar_apply_settings(db, &settings);
        fresh = tiku_trk_deskbar_add_team(db, "Viewer");
        check(fresh >= 0 && tiku_trk_deskbar_team_expanded(db, fresh),
              "D-203", "with the expander shown, Expand new applications "
              "still opens a new row");
    }
    tiku_trk_deskbar_free(db);

    printf("\nShow and Hide replicants (D-069)\n");
    {
        tiku_trk_deskbar_t *rdb = tiku_trk_deskbar_new(NULL);
        tiku_trk_deskbar_prefs_t prefs;
        tiku_store_t *store = tiku_store_memory_open();
        int32_t older[22];
        int shown_h, hidden_h, k;

        check(rdb != NULL && tiku_trk_deskbar_show_replicants(rdb),
              "D-069", "a new Deskbar shows its replicants");
        (void)tiku_trk_deskbar_add_replicant(rdb, "free", "/sys/mem/free");
        (void)tiku_trk_deskbar_add_replicant(rdb, "boots", "/sys/boots");
        tiku_trk_deskbar_draw(rdb, s);
        f = tiku_trk_deskbar_frame(rdb);
        shown_h = tray_zone_h(rdb, f.x + 10);
        tiku_trk_deskbar_open_menu(rdb);
        check(click_menu_row(rdb, "Settings", &pick) &&
              click_menu_row(rdb, "Hide replicants", &pick), "D-069",
              "the Settings submenu offers Hide replicants");
        check(pick.what == TIKU_TRK_DB_TOGGLE_REPLICANTS &&
              !tiku_trk_deskbar_show_replicants(rdb), "D-069",
              "choosing it hides them and tells the shell, to save");
        check(tiku_trk_deskbar_menu_point(rdb, "Show replicants", &k, &y),
              "D-069", "the menu stays open, the row now Show replicants");
        tiku_trk_deskbar_close_menu(rdb);
        tiku_trk_deskbar_draw(rdb, s);
        f = tiku_trk_deskbar_frame(rdb);
        hidden_h = tray_zone_h(rdb, f.x + 10);
        check(shown_h - hidden_h == 2 * 18, "D-069",
              "the tray and its drop zone shrink by the two replicant "
              "rows");
        tiku_trk_deskbar_set_show_replicants(rdb, 1);
        tiku_trk_deskbar_draw(rdb, s);
        check(tray_zone_h(rdb, f.x + 10) == shown_h, "D-069",
              "and Show brings the rows back");

        /* A record saved before the setting existed: version 5. */
        for (k = 0; k < 22; k++) { older[k] = 0; }
        older[0] = 5;
        older[6] = 16;
        older[20] = 12;
        (void)tiku_state_write(store, TIKU_KEY_SESSION,
                               TIKU_ATTR_DESKBAR_PREFS, older, sizeof older);
        tiku_trk_deskbar_set_show_replicants(rdb, 0);
        tiku_trk_deskbar_prefs_init(&prefs);
        check(tiku_trk_deskbar_prefs_load(&prefs, store) == 0, "D-069",
              "a version 5 record is read");
        tiku_trk_deskbar_apply_settings(rdb, &prefs.value);
        check(tiku_trk_deskbar_show_replicants(rdb), "D-069",
              "and a Deskbar given it shows its replicants");
        prefs.value.show_replicants = 0;
        check(tiku_trk_deskbar_prefs_defaultable(&prefs), "D-069",
              "hidden replicants give Defaults something to do");
        (void)tiku_trk_deskbar_prefs_save(&prefs, store);
        tiku_trk_deskbar_prefs_init(&prefs);
        (void)tiku_trk_deskbar_prefs_load(&prefs, store);
        tiku_trk_deskbar_apply_settings(rdb, &prefs.value);
        check(!prefs.value.show_replicants &&
              !tiku_trk_deskbar_show_replicants(rdb), "D-069",
              "the setting is saved and restored with the others");
        tiku_trk_deskbar_free(rdb);
        tiku_store_free(store);
    }

    printf("\na drop on a mounted store's row (D-070)\n");
    {
        tiku_backend_t *ns = tiku_mount_open();
        tiku_trk_deskbar_t *mdb;
        char into[TIKU_PATH_MAX];
        int found = 0;

        if (ns == NULL ||
            tiku_mount_add(ns, "/", tiku_backend_posix_open(), 1) != 0 ||
            tiku_mount_add(ns, "/data", tiku_backend_posix_open(), 1) != 0) {
            return 1;
        }
        mdb = tiku_trk_deskbar_new(ns);
        tiku_trk_deskbar_draw(mdb, s);
        f = tiku_trk_deskbar_frame(mdb);
        tiku_trk_deskbar_open_menu(mdb);
        for (y = 0; y < 600 && !found; y++) {
            if (tiku_trk_deskbar_menu_drop_at(mdb, f.x - 20, y, into,
                                              sizeof into) ==
                    TIKU_TRK_DB_DROP_MENU_DIR &&
                strcmp(into, "/data") == 0) {
                found = 1;
            }
        }
        check(found, "D-070",
              "the store's row, labelled \"data\", takes the drop as a "
              "folder");
        tiku_trk_deskbar_free(mdb);
        ns->ops->close(ns);
    }

    printf("\nthe Desktop's top inset (D-071)\n");
    {
        char dir[] = "/tmp/trk_dbmenus_XXXXXX";
        char cmd[128];
        tiku_backend_t *b = tiku_backend_posix_open();
        tiku_trk_desktop_view_t dv;
        tiku_trk_pose_t *p;
        int top = -1, lx, ly;

        if (mkdtemp(dir) == NULL) { return 1; }
        snprintf(cmd, sizeof cmd, "touch %s/alpha", dir);
        if (system(cmd) != 0) { return 1; }
        db = tiku_trk_deskbar_new(NULL);
        tiku_trk_deskbar_draw(db, s);
        memset(&dv, 0, sizeof dv);
        dv.backend = b;
        dv.deskbar = db;
        dv.screen_w = 1000;
        dv.screen_h = 700;
        dv.folder.view = tiku_trk_poseview_new(b, dir);
        tiku_trk_poseview_set_measure(dv.folder.view, measure, NULL);
        tiku_trk_poseview_set_font_metrics(dv.folder.view, 12, 9);
        (void)tiku_trk_poseview_reload(dv.folder.view);
        tiku_trk_poseview_set_mode(dv.folder.view, TIKU_TRK_MODE_ICON);
        tiku_trk_poseview_set_icon_size(dv.folder.view, 32);
        tiku_trk_poseview_set_viewport(dv.folder.view, 1000, 700);
        (void)tiku_trk_poseview_place_unplaced(dv.folder.view, 600);
        p = first_pose(dv.folder.view, &top);
        if (p == NULL) { return 1; }
        tiku_trk_poseview_pose_location(dv.folder.view, p, &lx, &ly);
        tiku_trk_poseview_move_pose(dv.folder.view, p, lx, ly - top + 2, 0);
        (void)first_pose(dv.folder.view, &top);
        check(top == 2, "D-071", "the pose starts at the top of the screen");
        tiku_trk_desktop_view_set_top_inset(&dv, 20);
        (void)first_pose(dv.folder.view, &top);
        check(top >= 20, "D-071",
              "a 20 px inset moves it out from under the strip");
        tiku_trk_desktop_view_set_top_inset(&dv, 0);
        (void)first_pose(dv.folder.view, &top);
        check(top < 20, "D-071", "and taking the inset away reclaims it");
        (void)tiku_trk_desktop_view_ensure_visible(&dv);
        dv.top_inset = 20;
        (void)tiku_trk_desktop_view_ensure_visible(&dv);
        (void)first_pose(dv.folder.view, &top);
        check(top >= 20, "D-071",
              "a pose under the inset is not in view: ensure_visible moves "
              "it below");
        tiku_trk_poseview_free(dv.folder.view);
        tiku_trk_deskbar_free(db);
        b->ops->close(b);
        snprintf(cmd, sizeof cmd, "rm -rf %s", dir);
        if (system(cmd) != 0) { /* a leftover directory harms nothing */ }
    }

    tiku_surface_free(s);
    printf("\n%s (%d failure%s)\n", fails ? "FAIL" : "PASS", fails,
           fails == 1 ? "" : "s");
    return fails ? 1 : 0;
}
