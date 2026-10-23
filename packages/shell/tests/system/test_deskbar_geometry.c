/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_deskbar_geometry.c - where the Deskbar's list, tray, popups and
 * hidden sliver go on each dock, and what a click or a drop there finds.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <string.h>

#include "../../system/deskbar/bar/tiku_trk_deskbar_private.h"

static int fails;
void db_draw_tip(tiku_trk_deskbar_t *db, tiku_surface_t *s);
void db_draw_calendar(tiku_trk_deskbar_t *, tiku_surface_t *,
                      const tiku_font_t *, const tiku_font_t *, tiku_rgb_t);
void db_draw_leaf_menu(tiku_trk_deskbar_t *, tiku_surface_t *, const tiku_font_t *);

static void
check(int cond, const char *row, const char *what)
{
    printf("  [%s] %-6s %s\n", cond ? "PASS" : "FAIL", row, what);
    if (!cond) { fails++; }
}

/** @brief A bar on @p dock with @p teams applications, drawn once. */
static tiku_trk_deskbar_t *
fixture(tiku_surface_t *s, tiku_trk_db_dock_t dock, int teams)
{
    tiku_trk_deskbar_t *db = tiku_trk_deskbar_new(NULL);
    char name[16];

    tiku_trk_deskbar_set_dock(db, dock);
    for (int i = 0; i < teams; i++) {
        snprintf(name, sizeof name, "app%d", i);
        (void)tiku_trk_deskbar_add_team(db, name);
    }
    tiku_trk_deskbar_draw(db, s);
    return db;
}

/** @brief The height of one application row. */
static int
team_h(const tiku_trk_deskbar_t *db)
{
    return (db->settings.icon_size + 4 > db_team_row())
               ? db->settings.icon_size + 4 : db_team_row();
}

/** @brief A primary click at @p x, @p y.  @return what it picked. */
static tiku_trk_db_pick_t
click(tiku_trk_deskbar_t *db, int x, int y, unsigned mods, int button)
{
    tiku_trk_db_pick_t pick;

    memset(&pick, 0, sizeof pick);
    (void)tiku_trk_deskbar_click(db, x, y, mods, button, 1, &pick);
    return pick;
}

/** @brief D-062: a scrolled side dock draws, clicks and drops from the
 *         first row shown, and draws nothing past the list's bottom. */
static void
scrolled_list(void)
{
    tiku_surface_t *s = tiku_surface_new(400, 150, TIKU_C_BACKDROP);
    tiku_trk_deskbar_t *db = fixture(s, TIKU_TRK_DB_RIGHT_EXPANDED, 8);
    tiku_rect_t f;
    tiku_trk_db_pick_t pick;
    int ly, lb, which = -1, painted = 0;

    printf("a scrolled side dock (D-062)\n");
    check(tiku_trk_deskbar_scrollable(db) &&
          tiku_trk_deskbar_scroll(db, 2) == 1, "D-062",
          "eight applications overflow a 150 px screen and scroll");
    tiku_trk_deskbar_draw(db, s);
    f = tiku_trk_deskbar_frame(db);
    ly = db_list_y(db);
    lb = db_tray_top(db) - PAD;
    pick = click(db, f.x + 20, ly + team_h(db) / 2, TIKU_TRK_MOD_CMD, 1);
    check(pick.what == TIKU_TRK_DB_SHOW_ALL && pick.team == db->order[2],
          "D-062", "a click on the top row picks the third application");
    check(tiku_trk_deskbar_drop_at(db, f.x + 20, ly + team_h(db) / 2,
                                   &which) == TIKU_TRK_DB_DROP_TEAM &&
          which == db->order[2], "D-062",
          "and a drop there is handed to the third application");
    for (int y = lb; y < f.y + f.h - PAD; y++) {
        painted += tiku_peek(s, f.x + 3, y) != TIKU_C_PANEL;
    }
    check(painted == 0, "D-062",
          "no application row is painted below the list's bottom");
    tiku_trk_deskbar_free(db);
    tiku_surface_free(s);
}

/** @brief D-063: a drop on a window row belongs to its application. */
static void
window_row_drop(void)
{
    tiku_surface_t *s = tiku_surface_new(1000, 680, TIKU_C_BACKDROP);
    tiku_trk_deskbar_t *db = fixture(s, TIKU_TRK_DB_RIGHT_EXPANDED, 2);
    tiku_trk_dbwin_t wins[2];
    tiku_rect_t f;
    int y, row_y = -1, which = -1;

    printf("\na drop on a window row (D-063)\n");
    memset(wins, 0, sizeof wins);
    snprintf(wins[0].title, sizeof wins[0].title, "%s", "one");
    wins[0].tag = (void *)1;
    snprintf(wins[1].title, sizeof wins[1].title, "%s", "two");
    wins[1].tag = (void *)2;
    tiku_trk_deskbar_set_windows(db, 0, wins, 2, 0);
    db->teams[0].expanded = 1;
    tiku_trk_deskbar_draw(db, s);
    f = tiku_trk_deskbar_frame(db);
    for (y = f.y; y < f.y + f.h && row_y < 0; y++) {
        tiku_trk_db_pick_t pick = click(db, f.x + 20, y,
                                        TIKU_TRK_MOD_CMD, 1);

        if (pick.what == TIKU_TRK_DB_MINIMIZE_WINDOW &&
            pick.tag == (void *)2) {
            row_y = y;
        }
    }
    check(row_y >= 0, "D-063", "the second window's row was found");
    check(tiku_trk_deskbar_drop_at(db, f.x + 20, row_y, &which) ==
          TIKU_TRK_DB_DROP_TEAM && which == 0, "D-063",
          "a drop on it is handed to the application that owns it");
    tiku_trk_deskbar_free(db);
    tiku_surface_free(s);
}

/** @brief D-064: on a left dock the window menu, the calendar and the
 *         clock menu open right of the bar. */
static void
left_dock_popups(void)
{
    tiku_surface_t *s = tiku_surface_new(1000, 680, TIKU_C_BACKDROP);
    tiku_trk_deskbar_t *db = fixture(s, TIKU_TRK_DB_LEFT_EXPANDED, 2);
    tiku_rect_t f = tiku_trk_deskbar_frame(db);
    tiku_rect_t r;
    tiku_trk_db_pick_t pick;
    int rowh = tiku_font_plain()->height + 6;
    int clock_y = f.y + f.h - 4;
    int menu_y = f.y + f.h - db_clock_row() - 32;

    printf("\npopups beside a bar docked left (D-064)\n");
    (void)click(db, f.x + 20, db_list_y(db) + team_h(db) / 2, 0u, 3);
    r = db_win_menu_rect(db, 4, rowh);
    check(db->win_menu_team >= 0 && r.x >= f.x + f.w && r.x + r.w <= 1000,
          "D-064", "the window menu opens right of the bar, on screen");
    tiku_trk_deskbar_close_menu(db);
    (void)click(db, f.x + 4, clock_y, 0u, 1);
    r = db_calendar_rect(db);
    check(tiku_trk_deskbar_calendar_open(db) && r.x >= f.x + f.w &&
          r.x + r.w <= 1000, "D-064",
          "the calendar opens right of the bar, on screen");
    tiku_trk_deskbar_close_calendar(db);
    (void)click(db, f.x + 4, clock_y, 0u, 3);
    pick = click(db, f.x + f.w + 100, menu_y + 10, 0u, 1);
    check(pick.what == TIKU_TRK_DB_CLOCK_HIDE, "D-064",
          "the clock menu's first row answers right of the bar");
    tiku_trk_deskbar_free(db);
    tiku_surface_free(s);
}

/** @brief D-065: on a bottom dock the leaf menu opens above the bar. */
static void
bottom_dock_menu(void)
{
    tiku_surface_t *s = tiku_surface_new(1000, 680, TIKU_C_BACKDROP);
    tiku_trk_deskbar_t *db = fixture(s, TIKU_TRK_DB_BOTTOM, 2);
    tiku_rect_t f = tiku_trk_deskbar_frame(db);
    tiku_rect_t m;
    int x, y;

    printf("\nthe leaf menu on a bottom dock (D-065)\n");
    tiku_trk_deskbar_open_menu(db);
    m = db_leaf_menu_rect(db, tiku_font_plain()->height + 6,
                          db->leaf_count);
    check(m.y >= 0 && m.y + m.h <= f.y + 2, "D-065",
          "the menu opens above the leaf button, on screen");
    check(tiku_trk_deskbar_menu_point(db, db->leaf[0].label, &x, &y) &&
          y < f.y, "D-065", "and its first row is above the bar");
    tiku_trk_deskbar_free(db);
    tiku_surface_free(s);
}

/** @brief D-066: a hidden top dock keeps its sliver below the global bar. */
static void
hidden_top_dock(void)
{
    tiku_surface_t *s = tiku_surface_new(1000, 680, TIKU_C_BACKDROP);
    tiku_trk_deskbar_t *db = fixture(s, TIKU_TRK_DB_TOP, 2);
    tiku_rect_t f;

    printf("\na hidden bar under the global menu bar (D-066)\n");
    tiku_trk_deskbar_set_top_inset(db, 20);
    tiku_trk_deskbar_set_autohide(db, 1);
    (void)tiku_trk_deskbar_pointer(db, 500, 300);
    tiku_trk_deskbar_draw(db, s);
    f = tiku_trk_deskbar_frame(db);
    check(f.y == 20 && f.h == TIKU_TRK_DB_SLIVER, "D-066",
          "a top dock's sliver sits at the top inset");
    tiku_trk_deskbar_set_dock(db, TIKU_TRK_DB_BOTTOM);
    tiku_trk_deskbar_draw(db, s);
    f = tiku_trk_deskbar_frame(db);
    check(f.y + f.h == 680 && f.h == TIKU_TRK_DB_SLIVER, "D-066",
          "a bottom dock's sliver sits on the bottom edge");
    tiku_trk_deskbar_free(db);
    tiku_surface_free(s);
}

/** @brief D-204, D-205: the tray's drop zone and its secondary click start
 *         at the tray's top, not at the switch strip. */
static void
tray_rows(void)
{
    tiku_surface_t *s = tiku_surface_new(1000, 680, TIKU_C_BACKDROP);
    tiku_trk_deskbar_t *db = fixture(s, TIKU_TRK_DB_RIGHT_EXPANDED, 2);
    tiku_rect_t f;
    int top, strip, which = -1;

    printf("\nthe tray above the switch strip (D-204, D-205)\n");
    (void)tiku_trk_deskbar_add_replicant(db, "one", "/sys/one");
    (void)tiku_trk_deskbar_add_replicant(db, "two", "/sys/two");
    tiku_trk_deskbar_draw(db, s);
    f = tiku_trk_deskbar_frame(db);
    top = db_tray_top(db);
    strip = top + db_tray_h(db);
    check(tiku_trk_deskbar_drop_at(db, f.x + 20, top + 4, &which) ==
          TIKU_TRK_DB_DROP_TRAY, "D-204",
          "a drop on the first replicant row is a subscription");
    (void)click(db, f.x + 20, strip - TRAY_ROW / 2, 0u, 3);
    check(db->clock_menu_open, "D-205",
          "a secondary click on the replicant row above the strip opens "
          "the clock menu");
    tiku_trk_deskbar_close_calendar(db);
    (void)click(db, f.x + 20, strip + SWITCH_ROW / 2, 0u, 3);
    check(!db->clock_menu_open, "D-205",
          "a secondary click on the switch strip does not");
    tiku_trk_deskbar_free(db);
    tiku_surface_free(s);
}

/** @brief D-206: the down arrow sits at the list's bottom, not on the
 *         tray's first row. */
static void
down_arrow(void)
{
    tiku_surface_t *s = tiku_surface_new(400, 150, TIKU_C_BACKDROP);
    tiku_trk_deskbar_t *db = fixture(s, TIKU_TRK_DB_RIGHT_EXPANDED, 8);
    tiku_rect_t f;
    int top, lb, cx;

    printf("\nthe list's down arrow (D-206)\n");
    (void)tiku_trk_deskbar_add_replicant(db, "one", "/sys/one");
    tiku_trk_deskbar_draw(db, s);
    f = tiku_trk_deskbar_frame(db);
    top = db_tray_top(db);
    lb = top - PAD;
    cx = f.x + f.w / 2;
    check(tiku_trk_deskbar_scrollable(db) &&
          tiku_peek(s, cx, lb - 5) == TIKU_C_TEXT, "D-206",
          "the down arrow is drawn at the list's bottom");
    (void)click(db, cx, top + 10, 0u, 1);
    check(tiku_trk_deskbar_scroll_at(db) == 0, "D-206",
          "a click on the tray's first row does not scroll the list");
    (void)tiku_trk_deskbar_scroll(db, -5);
    (void)click(db, cx, lb - 4, 0u, 1);
    check(tiku_trk_deskbar_scroll_at(db) == 1, "D-206",
          "a click just above the list's bottom does");
    tiku_trk_deskbar_free(db);
    tiku_surface_free(s);
}

int
main(void)
{
    tiku_surface_t *s = tiku_surface_new(800, 600, TIKU_C_BACKDROP);
    tiku_trk_deskbar_t *db = fixture(s, TIKU_TRK_DB_LEFT_EXPANDED, 1);
    strcpy(db->tip, "Application name"); db->tip_y = 100;
    tiku_fill(s, (tiku_rect_t){0, 0, 800, 600}, TIKU_C_BACKDROP);
    db_draw_tip(db, s);
    tiku_rect_t f = db->frame;
    int outside = 0;
    for (int y = 100; y < 125; y++)
        for (int x = f.x + f.w + 2; x < f.x + f.w + 150; x++)
            outside += tiku_peek(s, x, y) != TIKU_C_BACKDROP;
    check(outside > 20, "D-211", "left-docked tooltip appears beside the bar");
    db->calendar_open = 1; db->calendar_year = 2026; db->calendar_month = 5;
    tiku_rect_t cal = db_calendar_rect(db);
    db_draw_calendar(db, s, tiku_font_plain(), tiku_font_bold(), TIKU_C_TEXT);
    check(tiku_peek(s, cal.x + 8, cal.y + 10) == TIKU_C_TEXT,
          "D-076", "calendar month stepper has a visible arrow");
    db->calendar_open = 0;
    db->menu_open = 1; db->dragging = 1;
    db->leaf_count = 1; db->leaf[0].kind = TIKU_TRK_DB_ITEM_ROW;
    db->leaf[0].command = TIKU_TRK_DB_FIND;
    strcpy(db->leaf[0].label, "Find");
    db->menu_seen = 1; db->motion_now = 100000000;
    db_draw_leaf_menu(db, s, tiku_font_plain());
    tiku_rect_t menu = db_leaf_menu_rect(db, tiku_font_plain()->height + 6, 1);
    int bright = 0;
    for (int y = menu.y + 3; y < menu.y + menu.h - 3; y++)
        for (int x = menu.x + 8; x < menu.x + menu.w - 8; x++)
            bright += tiku_peek(s, x, y) == TIKU_C_TEXT;
    check(bright == 0, "D-210", "fixed menu action is dimmed during a drag");
    tiku_trk_deskbar_free(db); tiku_surface_free(s);
    scrolled_list();
    window_row_drop();
    left_dock_popups();
    bottom_dock_menu();
    hidden_top_dock();
    tray_rows();
    down_arrow();
    printf("\n%s: %d failure(s)\n", fails ? "FAIL" : "PASS", fails);
    return fails ? 1 : 0;
}
