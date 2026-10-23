/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_deskbar_private.h - what the tiku_trk_deskbar files share: the
 * includes, the metrics, the bar's state and the functions one file defines
 * for another.  Outside this directory, only tests include it.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_DESKBAR_PRIVATE_H_
#define TIKU_TRK_DESKBAR_PRIVATE_H_

/* split: public system/deskbar/tiku_trk_deskbar.h */

/* split: tag db */

#include "tiku_iconpaint.h"
#include "tiku_logo.h"
#include "tiku_motion.h"
#include "tiku_mount.h"
#include "tiku_strings.h"
#include "tiku_trk_deskbar.h"
#include "tiku_trk_deskbar_prefs.h"
#include "tiku_trk_input.h"
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

/* Metrics from Haiku's Deskbar source, named beside them.  Rows do not
 * tile: each is drawn one pixel short and the seam between them is left
 * unpainted, so the pitch is the row plus SEAM. */
#define BAR_W        134     /* gMinimumWindowWidth at the 12 px face      */
/*
 * The leaf row's least height (db_leaf_row()).  The mark inside the leaf
 * is 10 px shorter than the row, so it is never under 24 px, and
 * tiku_logo_paint_with() always draws it in wireframe.
 */
#define LEAF_MIN      34
#define HLEAF_W 120     /* the leaf button, horizontal (DB-033) */
#define TEAM_MIN      20     /* kMinTeamItemHeight; pitch 21               */
#define WIN_MIN       19     /* pitch 20                                   */
#define SEAM           1     /* kGutter: the unpainted row between items   */
#define WIN_INDENT    22
#define TRAY_ROW      18     /* kGutter + fMaxReplicantHeight + kGutter    */

/** @brief Whether a menu entry accepts activation in the current drag state. */
int db_leaf_enabled(const tiku_trk_deskbar_t *db,
                    const tiku_trk_db_item_t *item);
#define CLOCK_MIN     20
#define PAD            4
#define EXPANDER_W    13     /* ExpanderBounds()                           */
#define EXPANDER_ARM   5
#define EXPANDER_PEN   2
#define TRAY_PAD       3     /* fTrayPadding                               */
#define CLOCK_MARGIN  12     /* fClockMargin = fTrayPadding * 4            */
#define SWITCH_ROW    16     /* the theme switch's own strip of tray        */
#define ICON_ROW      20     /* a row of applications' tray icons           */
#define ICON_GAP       6     /* between two of them                         */
#define SWITCH_W      26     /* the track */
#define SWITCH_H      12


/** @brief The Deskbar's state, which tiku_trk_deskbar.h declares opaque. */
struct tiku_trk_deskbar {
    tiku_backend_t  *backend;   /**< the namespace the leaf menu lists */
    tiku_store_t     *store;    /**< where the tray persists, or NULL  */
    tiku_trk_db_dock_t   dock;  /**< which edge, expanded or mini      */
    /** The running applications, one row each; order[] holds indices
     *  into teams[] in the order the rows are shown. */
    tiku_trk_team_t      teams[TIKU_TRK_DB_TEAMS_MAX];
    int                  order[TIKU_TRK_DB_TEAMS_MAX];  /**< teams[] indices */
    int                  dragging;  /**< a drag is passing over the bar */
    int                  drop_team; /**< teams[] index under the drag, or -1 */
    int                  team_count;    /**< teams[] in use              */
    tiku_trk_replicant_t tray[TIKU_TRK_DB_TRAY_MAX];  /**< the tray rows */
    int                  tray_count;    /**< tray[] in use               */
    int                  next_replicant;   /**< the last id handed out
                                            *   (DB-038)                  */
    int                  width;     /**< down a side edge, user-set    */
    /** Which of the three tables is in force: 0 light, 1 dark, 2 no hue.
     *  The bar only draws it; the shell owns the setting. */
    int                  theme;
    /** The applications the leaf menu offers, as installed. */
    struct {
        char label[TIKU_TRK_DB_LABEL_MAX];  /**< the row's text        */
        char command[TIKU_PATH_MAX];        /**< what launching it runs */
        char icon[24];          /**< the icon set's name for it */
        int  group;             /**< which leaf row lists it */
    }                    app[TIKU_TRK_DB_APPS_MAX];
    int                  app_count; /**< app[] in use                    */
    int                  top_inset; /**< the global bar owns this much   */
    tiku_trk_deskbar_settings_t settings;   /**< the preferences in force */
    int                  autohide;  /**< hidden until the pointer reaches
                                     *   its screen edge                */
    int                  autoraise; /**< raised over windows on hover    */
    int                  pointer_on;  /**< the pointer is over the bar    */
    int                  pointer_x;   /**< last pointer x; -1 when away   */
    int                  pointer_y;   /**< last pointer y; -1 when away   */
    int                  leaf_hover;  /**< pointer over the TikuOS button */
    int                  scroll;      /**< first application row shown    */
    int                  overflow;    /**< the list ran past the tray     */
    int                  menu_open;     /**< the leaf menu is showing     */
    int64_t              motion_now;    /**< frame time, us; 0: no fades  */
    int                  menu_seen;     /**< the menu's fade has started  */
    int                  sub_seen;      /**< leaf row whose submenu fade
                                         *   has started, or -1           */
    tiku_motion_t        menu_motion;   /**< the leaf menu's fade         */
    tiku_motion_t        sub_motion;    /**< the submenu's fade           */
    tiku_rect_t          motion_damage; /**< where a fade still owes a
                                         *   frame, or empty              */
    int                  calendar_open; /**< the calendar popup is showing */
    int                  clock_menu_open;   /**< the clock's menu is showing */
    int                  calendar_year;     /**< the year the calendar shows */
    int                  calendar_month;    /**< its month, 1 to 12        */
    time_t               fixed_now;     /**< frozen clock for fixtures, or 0 */
    /* The hot row of each little menu, following the pointer; -1 when the
     * pointer is elsewhere. */
    int                  leaf_hot;      /**< into leaf[]                  */
    int                  sub_hot;       /**< into sub[]                   */
    int                  sub_pending;   /**< leaf awaiting hover dwell, -1 */
    tiku_rect_t          hover_damage;  /**< what the last hover changed  */
    int64_t              sub_entered;   /**< dwell start; -1 once tried   */
    tiku_trk_db_icon_fn  menu_icon;     /**< from the shell, or NULL      */
    void                *menu_icon_ctx; /**< handed back to menu_icon     */
    int                  win_hot;       /**< the window menu's row        */
    int                  clock_hot;     /**< the clock menu's row         */
    int                  calendar_day;  /**< the day picked, 1-based      */
    int                  win_menu_team;    /**< team whose window menu is
                                            *   open, or -1              */
    /** The team whose expander press is still held, or -1: the arrow
     *  turns down at once but the state commits only on release over the
     *  same strip (DB-022). */
    int                  expander_press;
    /** The tooltip: the full name of the row under the pointer when its
     *  label is cut or names are hidden (DB-032), a tray icon's tip, or
     *  the clock's long date (DB-044). */
    char                 tip[TIKU_TRK_DB_LABEL_MAX];
    int                  tip_y;            /**< the tip's top, or -1   */
    /** The applications' own tray icons, in the order they arrived. */
    tiku_trk_trayicon_t  trayicons[TIKU_TRK_DB_TRAYICONS_MAX];
    int                  trayicon_count;   /**< trayicons[] in use       */
    int                  trayicon_hot;     /**< under the pointer, or -1 */
    /* The horizontal strip's layout, computed at draw time so the click
     * and the drawing read the same numbers (DB-033). */
    int                  hbutton_w;        /**< one team button's width  */
    int                  hleft;            /**< first button's left edge */
    int                  hoverflow;        /**< the strip ran out of room */
    /* Mini mode's second icon: the running-apps popup (DB-037). */
    int                  apps_menu_open;   /**< the popup is showing     */
    int                  apps_hot;         /**< its row under the pointer */
    int                  screen_h;         /**< the surface's, at last draw */
    /** The leaf menu's rows, rebuilt from the namespace each time it
     *  opens, so a file put in the Deskbar folder is in the next menu
     *  (DB-005). */
    tiku_trk_db_item_t   leaf[TIKU_TRK_DB_LEAF_MAX];
    int                  leaf_count;       /**< leaf[] in use            */
    int                  sub_of;           /**< leaf row whose submenu is
                                            *   open, or -1              */
    tiku_trk_db_item_t   sub[TIKU_TRK_DB_SUB_MAX];     /**< that submenu */
    int                  sub_count;        /**< sub[] in use             */
    char                 menu_dir[TIKU_PATH_MAX];  /**< the user's folder */
    /** The system's Deskbar folder, read when menu_dir is not a folder. */
    char                 menu_dir_fallback[TIKU_PATH_MAX];
    /** Recent paths per kind, newest first (DB-074). */
    char                 recent[TIKU_TRK_DB_RECENT_KINDS]
                               [TIKU_TRK_DB_RECENT_MAX][TIKU_PATH_MAX];
    int                  recent_count[TIKU_TRK_DB_RECENT_KINDS]; /**< in use */
    tiku_rect_t     frame;      /**< the bar on the screen, the sliver
                                 *   while hidden                        */
};

/** @brief One directory walk into a menu: where its rows go and how many
 *         it may add. */
struct menu_walk {
    tiku_trk_deskbar_t *db;     /**< whose menu is being built          */
    int                 n;      /**< rows written into into[] so far    */
    tiku_trk_db_item_t *into;   /**< the rows, or NULL: the bar's leaf  */
    int                 max;    /**< how many rows the walk may add     */
};

/** @brief What relay_menu_icon() hands to the shell's painter. */
struct menu_icon_relay {
    tiku_trk_deskbar_t       *db;       /**< holds the painter and its ctx */
    const tiku_trk_db_item_t *items;    /**< the menu's rows, by index     */
};

/**
 * @brief The TikuOS button's rectangle: where it is painted, where the
 *        pointer hovers it, and what a change of hover repaints.
 */
tiku_rect_t db_leaf_button_rect(const tiku_trk_deskbar_t *db);

/* Shared across the family: defined in one file, used in another. */

/* tiku_trk_deskbar.c */
int db_clock_row(void);
int db_leaf_row(void);
int db_team_row(void);
int db_win_row(void);

/* tiku_trk_deskbar_draw.c */
tiku_rect_t db_leaf_menu_rect(const tiku_trk_deskbar_t *db, int rowh, int rows);
tiku_rect_t db_sub_menu_rect(const tiku_trk_deskbar_t *db, tiku_rect_t m,
                             int rowh);
void db_open_sub(tiku_trk_deskbar_t *db, int at);

/* tiku_trk_deskbar_layout.c */
int db_list_rows(const tiku_trk_deskbar_t *db);
int db_list_y(const tiku_trk_deskbar_t *db);
int db_tray_y(const tiku_trk_deskbar_t *db);
int db_tray_rows(const tiku_trk_deskbar_t *db);
int db_tray_h(const tiku_trk_deskbar_t *db);
int db_tray_top(const tiku_trk_deskbar_t *db);
int db_list_bottom(const tiku_trk_deskbar_t *db);
int db_list_first(const tiku_trk_deskbar_t *db);
int db_trayicons_w(const tiku_trk_deskbar_t *db);
int db_hclock_w(const tiku_trk_deskbar_t *db);
int db_trayicon_at(const tiku_trk_deskbar_t *db, int x, int y);
void db_draw_switch(const tiku_trk_deskbar_t *db, tiku_surface_t *s);

/* tiku_trk_deskbar_leaf.c */
const char *db_leaf_name(const char *path);
int db_calendar_days(int year, int month);
int db_menu_entry(const tiku_model_t *m, void *ctx);
void db_build_leaf(tiku_trk_deskbar_t *db);
void db_fill_entry(tiku_trk_db_item_t *it, const tiku_model_t *m);
void db_calendar_shift(tiku_trk_deskbar_t *db, int months, int years);

/* tiku_trk_deskbar_winmenu.c */
int db_win_menu_items(const tiku_trk_deskbar_t *db, int team,
                      const char **items, int *enabled);
tiku_rect_t db_calendar_rect(const tiku_trk_deskbar_t *db);
tiku_rect_t db_clock_menu_rect(const tiku_trk_deskbar_t *db);
tiku_rect_t db_win_menu_rect(const tiku_trk_deskbar_t *db, int rows, int rowh);
void db_reset_menu_hots(tiku_trk_deskbar_t *db);
void db_team_action(tiku_trk_deskbar_t *db, int team, unsigned mods, int button,
                    int clicks, tiku_trk_db_pick_t *pick);

/* End of the shared declarations. */

#endif /* TIKU_TRK_DESKBAR_PRIVATE_H_ */
