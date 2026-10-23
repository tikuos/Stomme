/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_deskbar_press.c - what a click on the bar, or on one of its
 * popups, does.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_trk_deskbar_private.h"

/* Used before they are defined. */
int db_click_list(tiku_trk_deskbar_t *db, int x, int y, unsigned mods,
                  int button, int clicks, tiku_trk_db_pick_t *pick,
                  int secondary, int *ry);
int db_click_apps_menu(tiku_trk_deskbar_t *db, int x, int y, unsigned mods,
                       int button, int clicks, tiku_trk_db_pick_t *pick);
int db_click_leaf_menu(tiku_trk_deskbar_t *db, int x, int y, unsigned mods,
                       int button, int clicks, tiku_trk_db_pick_t *pick,
                       const tiku_font_t *f);
int db_click_win_menu(tiku_trk_deskbar_t *db, int x, int y, unsigned mods,
                      int button, int clicks, tiku_trk_db_pick_t *pick,
                      const tiku_font_t *f);
int db_click_clock_menu(tiku_trk_deskbar_t *db, int x, int y, unsigned mods,
                        int button, int clicks, tiku_trk_db_pick_t *pick);
int db_click_calendar(tiku_trk_deskbar_t *db, int x, int y, unsigned mods,
                      int button, int clicks, tiku_trk_db_pick_t *pick);
/* End of the forward declarations. */

int
tiku_trk_deskbar_click(tiku_trk_deskbar_t *db, int x, int y, unsigned mods,
                       int button, int clicks, tiku_trk_db_pick_t *pick)
{
    int r;
    int secondary = (button == 3);
    const tiku_font_t *f = tiku_font_plain();
    int ry;

    if (db == NULL || pick == NULL) {
        return 0;
    }
    memset(pick, 0, sizeof *pick);

    /* An open popup owns the next click wherever it lands. */
    if ((r = db_click_calendar(db, x, y, mods, button, clicks, pick)) >= 0) {
        return r;
    }
    if ((r = db_click_clock_menu(db, x, y, mods, button, clicks, pick)) >= 0) {
        return r;
    }
    if ((r = db_click_win_menu(db, x, y, mods, button, clicks, pick, f)) >= 0) {
        return r;
    }
    if ((r = db_click_leaf_menu(db, x, y, mods, button, clicks, pick,
        f)) >= 0) {
        return r;
    }
    if ((r = db_click_apps_menu(db, x, y, mods, button, clicks, pick)) >= 0) {
        return r;
    }
    if (!tiku_trk_deskbar_hit(db, x, y)) {
        return 0;
    }
    {
        /* An application's tray icon: a primary click is picked for its
         * owner, in every dock; another button does nothing. */
        int ti = db_trayicon_at(db, x, y);

        if (ti >= 0) {
            if (button == 1) {
                pick->what = TIKU_TRK_DB_TRAY_ICON;
                pick->owner = db->trayicons[ti].owner;
                snprintf(pick->path, sizeof pick->path, "%s",
                         db->trayicons[ti].id);
            }
            return 1;
        }
    }
    if (!tiku_trk_deskbar_vertical(db->dock)) {
        /* The horizontal strip: the leaf keeps its own width, and the
         * rest of the bar is application buttons (DB-033). */
        if (x < db->frame.x + 2 + HLEAF_W) {
            tiku_trk_deskbar_open_menu(db);
            return 1;
        }
        if (x >= db->frame.x + db->frame.w - SWITCH_W - 10) {
            /* The strip's far end is the theme switch: the whole
             * reserved end answers. */
            pick->what = TIKU_TRK_DB_TOGGLE_DARK;
            return 1;
        }
        if (!tiku_trk_deskbar_mini(db->dock) && db->hbutton_w > 0) {
            int astep = ((mods & (TIKU_TRK_MOD_SHIFT | TIKU_TRK_MOD_CMD |
                                  TIKU_MOD_OPTION)) != 0u) ? 3 : 1;
            int idx;

            if (db->hoverflow && x < db->hleft) {
                (void)tiku_trk_deskbar_scroll(db, -astep);
                return 1;
            }
            idx = (db->hoverflow ? db->scroll : 0) +
                  (x - db->hleft) / db->hbutton_w;
            if (idx >= 0 && idx < db->team_count &&
                x >= db->hleft &&
                x < db->hleft + (idx + 1 -
                    (db->hoverflow ? db->scroll : 0)) * db->hbutton_w) {
                db_team_action(db, db->order[idx], mods, button, clicks,
                            pick);
                return 1;
            }
            if (db->hoverflow) {
                (void)tiku_trk_deskbar_scroll(db, astep);
                return 1;
            }
        }
        return 1;
    }
    if (y < db->frame.y + db_leaf_row() + 2) {
        tiku_trk_deskbar_open_menu(db);
        return 1;
    }
    if (tiku_trk_deskbar_mini(db->dock) &&
        y < db->frame.y + db_leaf_row() + 4 + db_team_row()) {
        /* The second icon: the running-apps menu (DB-037). */
        db->apps_menu_open = 1;
        db_reset_menu_hots(db);
        return 1;
    }
    {
        /* The theme switch's strip, checked before anything below it so
         * the mini dock reaches it too; a primary click anywhere on the
         * row answers. */
        tiku_rect_t sw = tiku_trk_deskbar_switch_rect(db);

        if (!secondary && y >= sw.y - (SWITCH_ROW - SWITCH_H) / 2 &&
            y < sw.y + sw.h + (SWITCH_ROW - SWITCH_H) / 2) {
            pick->what = TIKU_TRK_DB_TOGGLE_DARK;
            return 1;
        }
    }
    if (tiku_trk_deskbar_mini(db->dock)) {
        return 1;
    }

    /* The scroll arrows first: while the list overflows, its top and
     * bottom edges step it, three rows at a time with a modifier held
     * (DB-034). */
    if (db->overflow) {
        int astep = ((mods & (TIKU_TRK_MOD_SHIFT | TIKU_TRK_MOD_CMD |
                              TIKU_MOD_OPTION)) != 0u) ? 3 : 1;
        int ly = db_list_y(db);
        int lb = db_list_bottom(db);

        if (y >= ly && y < ly + 8) {
            (void)tiku_trk_deskbar_scroll(db, -astep);
            return 1;
        }
        if (y >= lb - 8 && y < lb) {
            (void)tiku_trk_deskbar_scroll(db, astep);
            return 1;
        }
    }

    if ((r = db_click_list(db, x, y, mods, button, clicks, pick, secondary,
        &ry)) >= 0) {
        return r;
    }
    /* The clock: a secondary click opens its menu and a primary one the
     * calendar (DB-043, DB-070); a secondary click on the tray row above
     * the switch strip opens the menu too. */
    if (db->settings.show_clock && y >= db->frame.y + db->frame.h -
        db_clock_row() - PAD) {
        if (secondary) {
            db->clock_menu_open = 1;
        db_reset_menu_hots(db);
        } else {
            time_t now = (db->fixed_now != 0) ? db->fixed_now : time(NULL);
            struct tm tmv;

            (void)localtime_r(&now, &tmv);
            db->calendar_year = 1900 + tmv.tm_year;
            db->calendar_month = 1 + tmv.tm_mon;
            db->calendar_day = tmv.tm_mday;
            db->calendar_open = 1;
        }
    } else if (secondary &&
               y >= db_tray_top(db) + db_tray_h(db) - TRAY_ROW &&
               y < db_tray_top(db) + db_tray_h(db)) {
        db->clock_menu_open = 1;
        db_reset_menu_hots(db);
    }
    return 1;
}

void
tiku_trk_deskbar_calendar_date(const tiku_trk_deskbar_t *db, int *year,
                               int *month, int *day)
{
    if (year != NULL) { *year = db != NULL ? db->calendar_year : 0; }
    if (month != NULL) { *month = db != NULL ? db->calendar_month : 0; }
    if (day != NULL) { *day = db != NULL ? db->calendar_day : 0; }
}

/**
 * @brief A click on the application list: the expander, a team row or a
 *        window row.
 * @return -1 when the click missed every row, else 1.
 */
int
db_click_list(tiku_trk_deskbar_t *db, int x, int y, unsigned mods, int button,
              int clicks, tiku_trk_db_pick_t *pick, int secondary, int *ry)
{
    int i;
    int j;

    /* A press on the expander arms it, and a press elsewhere on a team row
     * goes to db_team_action().  A window row raises its window; with Cmd
     * it minimises it, with Cmd+Option it hides the application, and a
     * secondary or Shift+middle click closes it. */
    (*ry) = db_list_y(db);
    for (i = db_list_first(db); i < db->team_count; i++) {
        int team = db->order[i];
        tiku_trk_team_t *t = &db->teams[team];
        int team_h = (db->settings.icon_size + 4 > db_team_row())
                         ? db->settings.icon_size + 4 : db_team_row();

        if ((*ry) + team_h > db_list_bottom(db)) {
            break;              /* below the list's bottom: not shown */
        }

        /* The seam belongs to neither row; a click on it is a miss, which
         * is why hit-testing uses the row height and stepping uses the
         * pitch. */
        if (y >= (*ry) && y < (*ry) + team_h) {
            if (db->settings.show_expander &&
                x > db->frame.x + db->frame.w - EXPANDER_W - 6) {
                /* Armed: the arrow turns down at once, and the state
                 * flips on a release over the same strip (DB-022). */
                db->expander_press = team;
            } else {
                /* Shared with the horizontal strip: Cmd+Option+Shift
                 * kills, a middle click launches another instance, Cmd
                 * shows the windows, Cmd+Shift minimises them, a double
                 * click activates and a secondary click opens the window
                 * menu (DB-019, DB-020, DB-021). */
                db_team_action(db, team, mods, button, clicks, pick);
            }
            return 1;
        }
        (*ry) += team_h + SEAM;
        if (!t->expanded) {
            continue;
        }
        for (j = 0; j < t->win_count; j++) {
            if ((*ry) + db_win_row() > db_list_bottom(db)) {
                break;
            }
            if (y >= (*ry) && y < (*ry) + db_win_row()) {
                pick->tag = t->wins[j].tag;
                if (secondary || (button == 2 &&
                                  (mods & TIKU_TRK_MOD_SHIFT) != 0u)) {
                    /* Shift+middle on a window entry closes that window,
                     * the same request the secondary click makes
                     * (DB-021). */
                    pick->what = TIKU_TRK_DB_CLOSE_WINDOW;
                } else if ((mods & TIKU_TRK_MOD_CMD) != 0u &&
                           (mods & TIKU_MOD_OPTION) != 0u) {
                    /* With Option, the gesture that minimises one window
                     * hides the whole application (CW-014). */
                    pick->what = TIKU_TRK_DB_HIDE_ALL;
                } else if (mods & TIKU_TRK_MOD_CMD) {
                    pick->what = TIKU_TRK_DB_MINIMIZE_WINDOW;
                } else {
                    pick->what = TIKU_TRK_DB_ACTIVATE_WINDOW;
                }
                return 1;
            }
            (*ry) += db_win_row() + SEAM;
        }
    }
    return -1;
}

/**
 * @brief A click while the mini dock's running-apps menu is open: a row
 *        activates its application, and any click closes the menu.
 * @return -1 when the menu is closed, else 1.
 */
int
db_click_apps_menu(tiku_trk_deskbar_t *db, int x, int y, unsigned mods,
                   int button, int clicks, tiku_trk_db_pick_t *pick)
{
    (void)mods;
    (void)button;
    (void)clicks;
    if (db->apps_menu_open) {
        const tiku_font_t *fm = tiku_font_plain();
        int rowh = fm->height + 6;
        int n = (db->team_count > 0) ? db->team_count : 1;
        tiku_rect_t ab = { db->frame.x + 2,
                                db->frame.y + 2 + db_leaf_row() + 2,
                                db->frame.w - 4, db_team_row() };
        tiku_rect_t am;
        int idx;

        am.w = 176;
        am.h = n * rowh + 4;
        am.y = ab.y;
        am.x = (db->frame.x > 0) ? db->frame.x - am.w - 2
                                 : db->frame.x + db->frame.w + 2;
        idx = (y - am.y - 2) / rowh;
        db->apps_menu_open = 0;
        if (x >= am.x && x < am.x + am.w && y >= am.y &&
            y < am.y + am.h && idx >= 0 && idx < db->team_count) {
            /* Choosing an application activates it (DB-037); the empty
             * menu's one row cannot be chosen. */
            pick->what = TIKU_TRK_DB_ACTIVATE_TEAM;
            pick->team = db->order[idx];
        }
        return 1;
    }
    return -1;
}

/**
 * @brief A click while the leaf menu is open: a row of the submenu or of
 *        the menu is chosen or expanded, and a click anywhere else closes
 *        the menu.
 * @return -1 when the menu is closed, else 1.
 */
int
db_click_leaf_menu(tiku_trk_deskbar_t *db, int x, int y, unsigned mods,
                   int button, int clicks, tiku_trk_db_pick_t *pick,
                   const tiku_font_t *f)
{
    (void)mods;
    (void)button;
    (void)clicks;
    if (db->menu_open) {
        int rowh = f->height + 6;
        tiku_rect_t m = db_leaf_menu_rect(db, rowh, db->leaf_count);
        int idx = (y - m.y - 2) / rowh;

        if (db->sub_of >= 0 && db->sub_count > 0) {
            tiku_rect_t sm = db_sub_menu_rect(db, m, rowh);
            int si = (y - sm.y - 2) / rowh;

            if (x >= sm.x && x < sm.x + sm.w && y >= sm.y &&
                y < sm.y + sm.h && si >= 0 && si < db->sub_count) {
                const tiku_trk_db_item_t *it = &db->sub[si];

                if (it->command == TIKU_TRK_DB_TOGGLE_REPLICANTS) {
                    /* The toggle leaves the menu open, the row
                     * relabelled; the pick tells the shell to save it. */
                    int of = db->sub_of;

                    db->settings.show_replicants =
                        !db->settings.show_replicants;
                    pick->what = TIKU_TRK_DB_TOGGLE_REPLICANTS;
                    db_open_sub(db, of);
                    return 1;
                }
                if (it->kind == TIKU_TRK_DB_ITEM_ROW) {
                    pick->what = it->command;
                    snprintf(pick->path, sizeof pick->path, "%s", it->path);
                } else if (it->kind == TIKU_TRK_DB_ITEM_EXPAND &&
                           it->expand == TIKU_TRK_DB_EXPAND_DIR) {
                    /* A folder in the submenu opens: a submenu has no
                     * submenu of its own. */
                    pick->what = TIKU_TRK_DB_OPEN_PATH;
                    snprintf(pick->path, sizeof pick->path, "%s", it->path);
                }
                db->menu_open = 0;
                db->sub_of = -1;
                return 1;
            }
        }
        if (x >= m.x && x < m.x + m.w && idx >= 0 && idx < db->leaf_count) {
            const tiku_trk_db_item_t *it = &db->leaf[idx];

            if (!db_leaf_enabled(db, it)) return 1;
            switch (it->kind) {
            case TIKU_TRK_DB_ITEM_EXPAND:
                db_open_sub(db, idx);
                return 1;
            case TIKU_TRK_DB_ITEM_DISABLED:
            case TIKU_TRK_DB_ITEM_SEP:
                return 1;       /* cannot be chosen */
            default:
                break;
            }
            if (it->command == TIKU_TRK_DB_TOGGLE_REPLICANTS) {
                db->settings.show_replicants =
                    !db->settings.show_replicants;
                pick->what = TIKU_TRK_DB_TOGGLE_REPLICANTS;
                db_build_leaf(db);
                return 1;
            }
            pick->what = it->command;
            snprintf(pick->path, sizeof pick->path, "%s", it->path);
        }
        db->menu_open = 0;
        db->sub_of = -1;
        return 1;
    }
    return -1;
}

/**
 * @brief A click while an application's window menu is open: an enabled
 *        row picks its action, and any click closes the menu.
 * @return -1 when the menu is closed, else 1.
 */
int
db_click_win_menu(tiku_trk_deskbar_t *db, int x, int y, unsigned mods,
                  int button, int clicks, tiku_trk_db_pick_t *pick,
                  const tiku_font_t *f)
{
    (void)mods;
    (void)button;
    (void)clicks;
    if (db->win_menu_team >= 0) {
        int rowh = f->height + 6;
        const char *items[4];
        int enabled[4];
        int n = db_win_menu_items(db, db->win_menu_team, items, enabled);
        tiku_rect_t m = db_win_menu_rect(db, n, rowh);
        int idx = (y - m.y - 2) / rowh;
        int team = db->win_menu_team;

        db->win_menu_team = -1;
        if (x >= m.x && x < m.x + m.w && y >= m.y && y < m.y + m.h &&
            idx >= 0 && idx < n && enabled[idx]) {
            if (db->teams[team].win_count == 0) {
                /* The empty-application variant: its one enabled row,
                 * Force Quit…, picks TIKU_TRK_DB_QUIT_TEAM (DB-028). */
                if (idx == 1) {
                    pick->what = TIKU_TRK_DB_QUIT_TEAM;
                    pick->team = team;
                }
            }
            else if (idx == 0) { pick->what = TIKU_TRK_DB_HIDE_ALL; }
            else if (idx == 1) { pick->what = TIKU_TRK_DB_SHOW_ALL; }
            else if (idx == 3) { pick->what = TIKU_TRK_DB_CLOSE_ALL; }
        }
        return 1;
    }
    return -1;
}

/**
 * @brief A click while the clock menu is open: a row picks its setting,
 *        and any click closes the menu.
 * @return -1 when the menu is closed, else 1.
 */
int
db_click_clock_menu(tiku_trk_deskbar_t *db, int x, int y, unsigned mods,
                    int button, int clicks, tiku_trk_db_pick_t *pick)
{
    (void)mods;
    (void)button;
    (void)clicks;
    if (db->clock_menu_open) {
        tiku_rect_t m = db_clock_menu_rect(db);
        int inside = x >= m.x && x < m.x + m.w && y >= m.y && y < m.y + m.h;
        int item = inside ? (y - m.y) / 28 : -1;
        db->clock_menu_open = 0;
        if (inside && item == 0) {
            pick->what = db->settings.show_clock ? TIKU_TRK_DB_CLOCK_HIDE
                                                  : TIKU_TRK_DB_CLOCK_SHOW;
        } else if (inside && item == 1) {
            pick->what = TIKU_TRK_DB_CLOCK_SECONDS;
        } else if (inside && item == 2) {
            pick->what = TIKU_TRK_DB_CLOCK_DAY;
        } else if (inside && item == 3) {
            pick->what = TIKU_TRK_DB_CLOCK_ZONE;
        }
        return 1;
    }
    return -1;
}

/**
 * @brief A click while the calendar is open: on the clock, a stepper or a
 *        day it stays open; anywhere else closes it.
 * @return -1 when the calendar is closed, else 1.
 */
int
db_click_calendar(tiku_trk_deskbar_t *db, int x, int y, unsigned mods,
                  int button, int clicks, tiku_trk_db_pick_t *pick)
{
    (void)mods;
    (void)button;
    (void)clicks;
    (void)pick;
    if (db->calendar_open) {
        /* A click on the clock keeps the calendar open (DB-045). */
        if (db->settings.show_clock &&
            x >= db->frame.x && x < db->frame.x + db->frame.w &&
            y >= db->frame.y + db->frame.h - db_clock_row() - PAD) {
            return 1;
        }
        {
            tiku_rect_t c = db_calendar_rect(db);
            int col, rowi, first, day;
            struct tm tmv;
            time_t shown = 0;

            memset(&tmv, 0, sizeof tmv);
            tmv.tm_year = db->calendar_year - 1900;
            tmv.tm_mon = db->calendar_month - 1;
            tmv.tm_mday = 1;
            tmv.tm_isdst = -1;
            shown = mktime(&tmv);
            (void)localtime_r(&shown, &tmv);
            first = tmv.tm_wday;
            if (x >= c.x + 4 && x < c.x + 48 && y >= c.y + 2 &&
                y < c.y + 20) {
                db_calendar_shift(db, -1, 0);
                return 1;
            }
            if (x >= c.x + 48 && x < c.x + 92 && y >= c.y + 2 &&
                y < c.y + 20) {
                db_calendar_shift(db, 1, 0);
                return 1;
            }
            if (x >= c.x + 94 && x < c.x + 138 && y >= c.y + 2 &&
                y < c.y + 20) {
                db_calendar_shift(db, 0, -1);
                return 1;
            }
            if (x >= c.x + 138 && x < c.x + 182 && y >= c.y + 2 &&
                y < c.y + 20) {
                db_calendar_shift(db, 0, 1);
                return 1;
            }
            if (x >= c.x + 6 && x < c.x + 181 && y >= c.y + 22 &&
                y < c.y + 142) {
                col = (x - c.x - 6) / 25;
                rowi = (y - c.y - 22) / 20;
                day = rowi * 7 + col - first + 1;
                if (day >= 1 && day <= db_calendar_days(db->calendar_year,
                                                     db->calendar_month)) {
                    db->calendar_day = day;
                    return 1;
                }
            }
        }
        db->calendar_open = 0;
        return 1;
    }
    return -1;
}
