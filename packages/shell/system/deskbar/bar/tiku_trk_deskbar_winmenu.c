/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_deskbar_winmenu.c - the window menu, what a click on a team row
 * means, and where the calendar opens.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_trk_deskbar_private.h"

/**
 * @brief The window menu's rows for @p team, with their enabled states.
 *
 * Hide all is refused when every window is already minimized and Show all
 * when none is; an application with no windows gets 'No windows' and,
 * except for Tracker's own entry, 'Force Quit…' (DB-028).
 * @return how many rows.
 */
int
db_win_menu_items(const tiku_trk_deskbar_t *db, int team, const char **items,
               int *enabled)
{
    const tiku_trk_team_t *t = &db->teams[team];
    int j, mini = 0;

    if (t->win_count == 0) {
        items[0] = "No windows";
        enabled[0] = 0;
        if (t->takes_anything) {
            return 1;
        }
        items[1] = "Force Quit…";
        enabled[1] = 1;
        return 2;
    }
    for (j = 0; j < t->win_count; j++) {
        if (t->wins[j].state == TIKU_TRK_DBW_MINIMIZED) {
            mini++;
        }
    }
    items[0] = tiku_str("menu.hide_all");
    enabled[0] = (mini < t->win_count);
    items[1] = tiku_str("menu.show_all");
    enabled[1] = (mini > 0);
    items[2] = "-";
    enabled[2] = 0;
    items[3] = tiku_str("menu.close_all");
    enabled[3] = 1;
    return 4;
}

/**
 * @brief The left edge of a popup @p w wide that opens @p off px left of a
 *        side-docked bar's left edge.  Beside a bar docked left it opens on
 *        the bar's right side, mirrored; it never starts left of x 0.
 */
static int
popup_x(const tiku_trk_deskbar_t *db, int off, int w)
{
    int x = (db->dock == TIKU_TRK_DB_LEFT_EXPANDED ||
             db->dock == TIKU_TRK_DB_LEFT_MINI)
                ? db->frame.x + db->frame.w + off - w
                : db->frame.x - off;

    return (x < 0) ? 0 : x;
}

/** @brief Where the window menu sits: beside the bar, whichever way it is
 *         docked. */
tiku_rect_t
db_win_menu_rect(const tiku_trk_deskbar_t *db, int rows, int rowh)
{
    tiku_rect_t m;

    m.w = 128;
    m.h = rows * rowh + 4;
    if (tiku_trk_deskbar_vertical(db->dock)) {
        m.x = popup_x(db, 130, m.w);
        m.y = db_list_y(db) + db_team_row();
    } else {
        m.x = db->frame.x + HLEAF_W + 8;
        m.y = (db->dock == TIKU_TRK_DB_TOP)
                  ? db->frame.y + db->frame.h + 2
                  : db->frame.y - m.h - 2;
    }
    return m;
}

/** @brief What a click on an application row asks for: a secondary click
 *         opens its window menu; a chord or a double click makes a pick. */
void
db_team_action(tiku_trk_deskbar_t *db, int team, unsigned mods, int button,
            int clicks, tiku_trk_db_pick_t *pick)
{
    if (button == 3) {
        db->win_menu_team = team;
        db->win_hot = -1;
        return;
    }
    if ((mods & TIKU_TRK_MOD_CMD) != 0u &&
        (mods & TIKU_MOD_OPTION) != 0u &&
        (mods & TIKU_TRK_MOD_SHIFT) != 0u) {
        pick->what = TIKU_TRK_DB_KILL_TEAM;
        pick->team = team;
    } else if (button == 2) {
        if ((mods & TIKU_TRK_MOD_SHIFT) == 0u) {
            pick->what = TIKU_TRK_DB_LAUNCH_TEAM;
            pick->team = team;
        }
    } else if ((mods & TIKU_TRK_MOD_CMD) != 0u &&
               (mods & TIKU_TRK_MOD_SHIFT) != 0u) {
        pick->what = TIKU_TRK_DB_HIDE_ALL;
        pick->team = team;
    } else if ((mods & TIKU_TRK_MOD_CMD) != 0u) {
        pick->what = TIKU_TRK_DB_SHOW_ALL;
        pick->team = team;
    } else if (clicks > 1) {
        pick->what = TIKU_TRK_DB_ACTIVATE_TEAM;
        pick->team = team;
    }
}

/**
 * @brief Where the calendar opens: 4 px below the clock, or flipped
 *        above it when that would fall past the screen bottom (DB-070).
 */
tiku_rect_t
db_calendar_rect(const tiku_trk_deskbar_t *db)
{
    tiku_rect_t c = { 0, 0, 186, 148 };

    c.x = popup_x(db, 190, c.w);
    c.y = db->frame.y + db->frame.h + 4;
    if (db->screen_h > 0 && c.y + c.h > db->screen_h) {
        c.y = db->frame.y + db->frame.h - db_clock_row() - PAD - 4 - c.h;
    }
    return c;
}

/** @brief Where the clock's four-row menu opens: beside the bar, its
 *         bottom row level with the clock. */
tiku_rect_t
db_clock_menu_rect(const tiku_trk_deskbar_t *db)
{
    tiku_rect_t m = { 0, db->frame.y + db->frame.h - db_clock_row() - 32,
                      170, 4 * 28 };

    m.x = popup_x(db, 130, m.w);
    return m;
}

/** @brief Clear every menu's hot row and the pending submenu dwell: no row
 *         is hot until the pointer moves onto it (DB-004). */
void
db_reset_menu_hots(tiku_trk_deskbar_t *db)
{
    db->leaf_hot = -1;
    db->sub_hot = -1;
    db->sub_pending = -1;
    db->sub_entered = 0;
    db->win_hot = -1;
    db->clock_hot = -1;
    db->apps_hot = -1;
}
