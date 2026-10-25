/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_deskbar.h - the Deskbar: the bar, its menus, its drops and its
 * tray.
 *
 * Three sections: the leaf button that opens the leaf menu, the
 * running-application list (each application's windows nested under it when
 * expanded), and the tray of replicants with the clock.  Behaviour is cited
 * to the DB-* rows of docs/contracts/tracker.md.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_DESKBAR_H_
#define TIKU_TRK_DESKBAR_H_

#include "tiku_attr.h"
#include "tiku_model.h"
#include "tiku_state.h"
#include "tiku_trk_gfx_dep.h"
#include <time.h>

#define TIKU_TRK_DB_TEAMS_MAX   8       /**< applications in the list     */
#define TIKU_TRK_DB_WINS_MAX   16       /**< windows under one entry      */
#define TIKU_TRK_DB_TRAY_MAX    8       /**< replicants in the tray       */
#define TIKU_TRK_DB_TRAYICONS_MAX 8     /**< applications' own tray icons */
#define TIKU_TRK_DB_LABEL_MAX  48       /**< a name, a title, a row label */
#define TIKU_TRK_DB_MAX_REPLICANT_H 12  /**< tallest replicant, in px     */

/** @brief Where the bar is docked. */
typedef enum {
    TIKU_TRK_DB_RIGHT_EXPANDED = 0,   /* the default                   */
    TIKU_TRK_DB_LEFT_EXPANDED,
    TIKU_TRK_DB_RIGHT_MINI,           /* leaf, running-apps button, tray */
    TIKU_TRK_DB_LEFT_MINI,
    /* Along the top or bottom edge: the bar spans the screen, one row
     * deep, and the application buttons share its width (DB-033). */
    TIKU_TRK_DB_TOP,
    TIKU_TRK_DB_BOTTOM,
    TIKU_TRK_DB_DOCKS               /* how many there are              */
} tiku_trk_db_dock_t;

/** @brief A window's state, which its icon distinguishes (DB-026). */
typedef enum {
    TIKU_TRK_DBW_SHOWN = 0,
    TIKU_TRK_DBW_MINIMIZED,
} tiku_trk_dbwin_state_t;

/** @brief One window under an application. */
typedef struct {
    /* The window's title, its application's name stripped. */
    char                   title[TIKU_TRK_DB_LABEL_MAX];
    tiku_trk_dbwin_state_t state;
    int                    current;      /* the application's front window */
    void                  *tag;          /* the owner's window identity    */
} tiku_trk_dbwin_t;

/** @brief One running application. */
typedef struct {
    char             name[TIKU_TRK_DB_LABEL_MAX];
    int              expanded;           /* windows shown underneath       */
    int              active;             /* owns the focused window        */
    tiku_trk_dbwin_t wins[TIKU_TRK_DB_WINS_MAX];
    int              win_count;
    /* The types this application can open; a drop of any other type is
     * refused unless forced (DB-035). */
    char             type[8][TIKU_TYPE_MAX];
    int              ntype;
    int              takes_anything;     /* opens any type: Tracker     */
    /* The processes behind the entry, one row however many there are;
     * tiku_trk_deskbar_prune_teams() removes a roster entry when its
     * last one ends (DB-015). */
    long             pids[16];
    int              pid_count;
    char             command[160];       /* its image, for a new instance */
    int              shell;              /* the shell's entry: never pruned */
    char             icon[64];           /* its own art, or "": generic */
} tiku_trk_team_t;

/** @brief A tray item: a namespace node rendered as label + value. */
typedef struct {
    int             id;             /* the item's numeric id (DB-038)     */
    uint64_t        node_id;        /* the node's identity, for moves     */
    char            label[16];
    char            path[TIKU_PATH_MAX];
    char            value[24];      /* raw, as the node reports it        */
    tiku_desc_t desc;           /* resolved once, at subscribe time   */
    int             desc_known;     /* desc was parsed                */
    int             valid;          /* value has been read            */
    int             height;         /* asked for, in px (DB-041)      */
} tiku_trk_replicant_t;

/**
 * @brief An application's own tray item: a small icon, a few characters of
 *        text after it, and a tip.  The owner is the shell's identifier for
 *        the application, a session's generation; a click on the item is
 *        picked with that owner.
 */
typedef struct {
    uint64_t        owner;          /* who set it; a click is picked for it */
    char            id[16];         /* the owner's name for it            */
    char            icon[48];       /* a name from the desk's icon set    */
    char            text[8];        /* beside the icon, or ""             */
    char            tip[TIKU_TRK_DB_LABEL_MAX]; /* shown on hover         */
} tiku_trk_trayicon_t;

#define TIKU_TRK_DB_LEAF_MAX 32         /**< rows of the leaf menu        */
#define TIKU_TRK_DB_SUB_MAX  50         /**< rows of its open submenu     */

/** @brief The kind of a leaf-menu row, which decides what a click does. */
typedef enum {
    TIKU_TRK_DB_ITEM_ROW,       /* chosen: it acts by its command       */
    TIKU_TRK_DB_ITEM_SEP,
    TIKU_TRK_DB_ITEM_DISABLED,  /* shown dimmed, never chosen           */
    TIKU_TRK_DB_ITEM_EXPAND     /* opens the submenu its expand names   */
} tiku_trk_db_item_kind_t;

/** @brief What an expanding row opens: the builder of its submenu. */
typedef enum {
    TIKU_TRK_DB_EXPAND_NONE,
    TIKU_TRK_DB_EXPAND_DIR,      /* a folder's entries, from its path    */
    TIKU_TRK_DB_EXPAND_RECENT,   /* the recent list its `which` names    */
    TIKU_TRK_DB_EXPAND_APPS,     /* the installed programs of a group    */
    TIKU_TRK_DB_EXPAND_SETTINGS  /* the preferences windows              */
} tiku_trk_db_expand_t;


/** @brief Which recent list a RECENT row shows. */
typedef enum {
    TIKU_TRK_DB_RECENT_DOCS = 0,
    TIKU_TRK_DB_RECENT_FOLDERS,
    TIKU_TRK_DB_RECENT_APPS,
    TIKU_TRK_DB_RECENT_KINDS
} tiku_trk_db_recent_t;

/** @brief How many entries one recent list shows. */
#define TIKU_TRK_DB_RECENT_MAX 50

/** @brief One row of the leaf menu or of its open submenu. */
typedef struct {
    char                    label[TIKU_TRK_DB_LABEL_MAX];
    char                    path[TIKU_PATH_MAX];
    tiku_trk_db_item_kind_t kind;
    int                     command; /* ROW: the tiku_trk_db_action_t   */
    tiku_trk_db_expand_t    expand;  /* EXPAND: what it opens           */
    int                     which;   /* RECENT: which list; APPS: group */
    /* The name of this row's icon, given by whoever added the row.  The
     * bar only stores it; the painter set with
     * tiku_trk_deskbar_set_menu_icon_painter() draws it. */
    char                    icon[24];
} tiku_trk_db_item_t;

/** @brief The Deskbar; its fields are private to system/deskbar/bar. */
typedef struct tiku_trk_deskbar tiku_trk_deskbar_t;
/** @brief The Deskbar's settings, as tiku_trk_deskbar_prefs.h defines them. */
typedef struct tiku_trk_deskbar_settings tiku_trk_deskbar_settings_t;

/**
 * @brief Where the folder whose contents the leaf menu shows lives (DB-003).
 *
 * The menu reads @p user when it is a folder, else @p fallback, the
 * system copy.
 */
void tiku_trk_deskbar_set_menu_dir(tiku_trk_deskbar_t *db, const char *user,
                                   const char *fallback);

/**
 * @brief The painter of a menu row's icon, supplied by the shell (DB-002).
 *
 * The bar knows each row's kind and the name of its art; the shell paints
 * it.  @return nonzero when something was painted.
 */
typedef int (*tiku_trk_db_icon_fn)(void *context, tiku_surface_t *s,
                                   const tiku_trk_db_item_t *item,
                                   int x, int y, int size);
/**
 * @brief Give the bar the painter that draws a menu row's icon, with the
 *        context it wants handed back.
 */
void tiku_trk_deskbar_set_menu_icon_painter(tiku_trk_deskbar_t *db,
                                            tiku_trk_db_icon_fn fn,
                                            void *context);

/** @brief The folder the menu is actually reading, or "" (DB-065). */
const char *tiku_trk_deskbar_menu_dir(const tiku_trk_deskbar_t *db);
/** @brief The user's editable folder, even while the fallback is shown. */
const char *tiku_trk_deskbar_user_menu_dir(const tiku_trk_deskbar_t *db);

/**
 * @brief Hand the bar one of the recent lists (DB-002, DB-074).
 *
 * The lists belong to the shell, which shares them with the Open panel;
 * the bar copies at most TIKU_TRK_DB_RECENT_MAX paths.  A list with nothing
 * in it is not shown.
 */
void tiku_trk_deskbar_set_recent(tiku_trk_deskbar_t *db,
                                 tiku_trk_db_recent_t which,
                                 const char paths[][TIKU_PATH_MAX],
                                 int n);

/** @brief How many rows the leaf menu has. */
int tiku_trk_deskbar_leaf_count(const tiku_trk_deskbar_t *db);
/** @brief Row @p i of the leaf menu, or NULL when there is no such row. */
const tiku_trk_db_item_t *tiku_trk_deskbar_leaf_item(
    const tiku_trk_deskbar_t *db, int i);

/** @brief Whether @p d docks the bar down a side edge. */
int tiku_trk_deskbar_vertical(tiku_trk_db_dock_t d);

/** @brief Whether @p d is a collapsed mode: the leaf, one button that opens
 *         the running-apps menu, and the tray with the switch and the
 *         clock. */
int tiku_trk_deskbar_mini(tiku_trk_db_dock_t d);

/**
 * @brief The dock a point on the screen asks for (DB-048).
 *
 * The nearest screen edge decides: the top or bottom edge gives the
 * horizontal bar there; down a side edge the top third gives the expanded
 * bar and the rest the mini one.  A point more than a quarter of the
 * screen's longer side from every edge keeps the dock in @p now.
 */
tiku_trk_db_dock_t tiku_trk_deskbar_dock_at(int x, int y, int sw, int sh,
                                            tiku_trk_db_dock_t now);

/**
 * @brief How many programs a shell may offer in the leaf menu, in one
 *        table shared by every group.
 *
 * No fewer than the launch registry's TIKU_TRK_APP_MAX, which
 * tiku_trk_desktop_apps.c asserts.  One group's submenu shows at most
 * TIKU_TRK_DB_SUB_MAX of them.
 */
#define TIKU_TRK_DB_APPS_MAX 64

#define TIKU_TRK_DB_MIN_W 134           /**< narrowest the bar is set     */
#define TIKU_TRK_DB_MAX_W 320           /**< widest the bar is set        */

/**
 * @brief Set the bar's width down a side edge (DB-050).
 *
 * Clamped to TIKU_TRK_DB_MIN_W .. TIKU_TRK_DB_MAX_W.
 *
 * @return the width actually taken, or 0 for no bar.
 */
int tiku_trk_deskbar_set_width(tiku_trk_deskbar_t *db, int w);
/** @brief The bar's width, never under TIKU_TRK_DB_MIN_W. */
int tiku_trk_deskbar_width(const tiku_trk_deskbar_t *db);

/** @brief How wide the bar leaves itself when hidden (DB-051). */
#define TIKU_TRK_DB_SLIVER 2

/** @brief Whether the bar hides until the pointer reaches its edge
 *         (DB-051). */
void tiku_trk_deskbar_set_autohide(tiku_trk_deskbar_t *db, int on);
/**
 * @brief Whether the bar rises over windows when the pointer reaches it;
 *        turning it on takes the bar off always-on-top (DB-052, DB-073).
 */
void tiku_trk_deskbar_set_autoraise(tiku_trk_deskbar_t *db, int on);

/**
 * @brief Tell the bar where the pointer is (DB-051, DB-052).
 *
 * With auto-hide it decides whether the bar shows itself, and with
 * auto-raise whether it comes forward.
 *
 * @return 1 when the pointer came onto the bar or left it.
 */
int tiku_trk_deskbar_pointer(tiku_trk_deskbar_t *db, int x, int y);
/**
 * @brief The screen point of the first open-menu row whose label contains
 *        @p label.
 *
 * The submenu is searched first, so a label in both menus resolves to the
 * submenu's row.  Zero when no menu is open or no row matches.
 */
int tiku_trk_deskbar_menu_point(const tiku_trk_deskbar_t *db,
                                const char *label, int *x, int *y);

/**
 * @brief The open leaf menu's row labels, or with @p sub its open
 *        submenu's, as "a|b|".  @return how many rows; 0 when it is closed.
 */
int tiku_trk_deskbar_menu_labels(const tiku_trk_deskbar_t *db, int sub,
                                 char *out, size_t cap);

/**
 * @brief The label of the row under the pointer in whichever of the
 *        bar's menus is open: the submenu's before the leaf menu's, else
 *        the window, clock or running-applications menu's.
 *
 * @return 1 with @p out filled, 0 with @p out empty when no row is hot.
 */
int tiku_trk_deskbar_hot_label(const tiku_trk_deskbar_t *db, char *out,
                               size_t cap);

/** @brief The tooltip being shown, or "" for none. */
const char *tiku_trk_deskbar_tip(const tiku_trk_deskbar_t *db);

/** @brief The open leaf menu's bottom edge on the screen, or -1. */
int tiku_trk_deskbar_menu_bottom(const tiku_trk_deskbar_t *db);

/**
 * @brief Whether (@p x, @p y) is within 20 pixels of the open leaf menu or
 *        its open submenu: the margin the pointer may cross without the
 *        menu closing (DB-036).
 */
int tiku_trk_deskbar_menu_keeps_open(const tiku_trk_deskbar_t *db, int x, int y);
/**
 * @brief Whether a quit shortcut closes the open preferences window: 1 only
 *        with @p shortcut_quit and @p prefs_open.  The bar itself is never
 *        closed or quit (DB-053).
 */
int tiku_trk_deskbar_quit_policy(int prefs_open, int shortcut_quit);

/** @brief Whether the bar is currently showing more than its sliver. */
int tiku_trk_deskbar_shown(const tiku_trk_deskbar_t *db);

/**
 * @brief Scroll the application list (DB-034).
 *
 * The list scrolls only while it overflows the room above the tray, when
 * the arrows show; otherwise a step changes nothing.
 *
 * @param step Rows, positive downwards.
 * @return 1 when the first shown row changed.
 */
int tiku_trk_deskbar_scroll(tiku_trk_deskbar_t *db, int step);

/** @brief The first application row shown. */
int tiku_trk_deskbar_scroll_at(const tiku_trk_deskbar_t *db);
/** @brief Whether the application list overflows the bar. */
int tiku_trk_deskbar_scrollable(const tiku_trk_deskbar_t *db);

/** @brief Whether the mini dock's running-apps menu is up (DB-037). */
int tiku_trk_deskbar_apps_menu_open(const tiku_trk_deskbar_t *db);

/** @brief The horizontal strip's current button width (DB-033). */
int tiku_trk_deskbar_hbutton_width(const tiku_trk_deskbar_t *db);

/** @brief Whether each scroll arrow should show (DB-034). */
void tiku_trk_deskbar_arrows(const tiku_trk_deskbar_t *db, int *up,
                             int *down);

/**
 * @brief Whether the bar should be drawn above the windows.
 *
 * With Always on top it always is; otherwise only while one of its popups
 * is open, or, with Auto-raise on, while the pointer is on the bar
 * (DB-052, DB-072).
 */
int tiku_trk_deskbar_raised(const tiku_trk_deskbar_t *db);


/** @brief What a click on the Deskbar asked for. */
typedef enum {
    TIKU_TRK_DB_NONE = 0,
    TIKU_TRK_DB_ACTIVATE_WINDOW,  /* `tag` names the window            */
    TIKU_TRK_DB_MINIMIZE_WINDOW,
    TIKU_TRK_DB_CLOSE_WINDOW,
    TIKU_TRK_DB_OPEN_PATH,
    TIKU_TRK_DB_SHOW_ABOUT,
    TIKU_TRK_DB_FIND,
    TIKU_TRK_DB_PREFERENCES,
    TIKU_TRK_DB_TRACKER_PREFERENCES,
    TIKU_TRK_DB_DESKTOP_PREFERENCES,
    TIKU_TRK_DB_FONT_PREFERENCES,
    TIKU_TRK_DB_SOUND_PREFERENCES,
    TIKU_TRK_DB_DEVICE_PREFERENCES,
    TIKU_TRK_DB_STARTUP_PREFERENCES,
    TIKU_TRK_DB_ASSOC_PREFERENCES,
    TIKU_TRK_DB_OPEN_PANEL,     /* `path`: a board form's panel node */
    TIKU_TRK_DB_HIDE_ALL,
    TIKU_TRK_DB_SHOW_ALL,
    TIKU_TRK_DB_CLOSE_ALL,
    TIKU_TRK_DB_QUIT,
    TIKU_TRK_DB_CLOCK_HIDE,
    TIKU_TRK_DB_CLOCK_SHOW,
    TIKU_TRK_DB_CLOCK_SECONDS,
    TIKU_TRK_DB_CLOCK_DAY,
    TIKU_TRK_DB_CLOCK_ZONE,
    TIKU_TRK_DB_ACTIVATE_TEAM,    /* double-click: the application  */
    TIKU_TRK_DB_LAUNCH_TEAM,      /* middle-click: a new instance   */
    TIKU_TRK_DB_KILL_TEAM,        /* kill it (DB-020)               */
    TIKU_TRK_DB_QUIT_TEAM,        /* ask it to quit (DB-028)        */
    TIKU_TRK_DB_TOGGLE_DARK,      /* the tray's theme switch        */
    TIKU_TRK_DB_LAUNCH_APP,       /* `path` is the program to start */
    TIKU_TRK_DB_HELP,             /* the desktop's help pages       */
    TIKU_TRK_DB_TOGGLE_REPLICANTS, /* the bar flips it; the shell saves */
    TIKU_TRK_DB_FORCE_QUIT,       /* the Force Quit window          */
    TIKU_TRK_DB_TRAY_ICON,        /* an application's own tray item:
                                     `owner` set it, `path` is its id */
} tiku_trk_db_action_t;

/** @brief What a click on the bar asked for, and what it names. */
typedef struct {
    tiku_trk_db_action_t what;
    void                *tag;           /* the window, for a window action */
    int                  team;          /* an entry's index, not its row */
    char                 path[TIKU_PATH_MAX];
    uint64_t             owner;         /* a tray item's owner         */
} tiku_trk_db_pick_t;

/** @brief A new bar over @p backend, docked right and expanded, or NULL. */
tiku_trk_deskbar_t *tiku_trk_deskbar_new(tiku_backend_t *backend);

/**
 * @brief Freeze the clock at @p at; 0 returns it to the wall.
 *
 * A frozen clock shows @p at in every frame; scripted runs freeze it.
 */
void tiku_trk_deskbar_set_clock(tiku_trk_deskbar_t *db, time_t at);

/**
 * @brief The clock's date, time and zone as one string, which changes when
 *        the shown time turns over (DB-044).
 *
 * @return Nonzero while the clock is shown; otherwise @p out is empty.
 */
int tiku_trk_deskbar_clock_stamp(const tiku_trk_deskbar_t *db, char *out,
                                 size_t max);

/**
 * @brief Follow the pointer: the TikuOS button's hover, each open menu's
 *        hot row, the tray icon under the pointer and the tooltip.
 *
 * @return Nonzero when a highlight moved.
 */
int tiku_trk_deskbar_motion(tiku_trk_deskbar_t *db, int x, int y);

/**
 * @brief What tiku_trk_deskbar_motion() has changed on screen since the
 *        last call.
 *
 * The union of the button, menu, tray icon and tooltip rectangles whose
 * highlight changed.  The call clears it; empty when nothing moved.
 */
tiku_rect_t tiku_trk_deskbar_hover_area(tiku_trk_deskbar_t *db);

/** @brief The hover dwell before a submenu opens, in microseconds. */
#define TIKU_TRK_DB_SUBMENU_DELAY_US 500000
/**
 * @brief Advance the submenu hover by the clock, even while the pointer
 *        rests: a leaf row hovered for TIKU_TRK_DB_SUBMENU_DELAY_US opens
 *        its submenu, or closes the open one when it has none.
 *
 * Hover never chooses a command, and leaving a row cancels its pending
 * opening.
 *
 * @param now Monotonic time, in microseconds.
 * @return Nonzero only when a submenu opened, switched or closed.
 */
int tiku_trk_deskbar_menu_tick(tiku_trk_deskbar_t *db, int64_t now);

/**
 * @brief A release: an armed expander press commits here (DB-022).
 *
 * @return nonzero when a row expanded or collapsed.
 */
int tiku_trk_deskbar_release(tiku_trk_deskbar_t *db, int x, int y);

/** @brief Whether entry @p team shows its windows inline. */
int tiku_trk_deskbar_team_expanded(const tiku_trk_deskbar_t *db, int team);

/** @brief Attach the durable store used for tray subscriptions. */
void tiku_trk_deskbar_set_store(tiku_trk_deskbar_t *db,
                                tiku_store_t *store);
/** @brief Load saved tray paths, pruning entries that no longer resolve. */
int tiku_trk_deskbar_load_replicants(tiku_trk_deskbar_t *db);
/** @brief Remove a tray subscription whose source disappeared. */
int tiku_trk_deskbar_remove_replicant(tiku_trk_deskbar_t *db,
                                      const char *path);
/** @brief Update a tray source after the backing node was moved. */
int tiku_trk_deskbar_repoint_replicant(tiku_trk_deskbar_t *db,
                                       const char *old_path,
                                       const char *new_path);

/** @brief Free the bar. */
void tiku_trk_deskbar_free(tiku_trk_deskbar_t *db);

/** @brief Dock the bar at @p dock; a value outside the docks is ignored. */
void tiku_trk_deskbar_set_dock(tiku_trk_deskbar_t *db,
                               tiku_trk_db_dock_t dock);
/** @brief Where the bar is docked; right and expanded for no bar. */
tiku_trk_db_dock_t tiku_trk_deskbar_dock(const tiku_trk_deskbar_t *db);

/**
 * @brief Take @p settings as the bar's own and apply them live: auto-hide,
 *        auto-raise, the expander, the order of the list and the
 *        interface's font (DB-061, DB-073).
 */
void tiku_trk_deskbar_apply_settings(tiku_trk_deskbar_t *db,
    const tiku_trk_deskbar_settings_t *settings);

/**
 * @brief Register an application in the list as the shell's own entry,
 *        matched without regard to case (DB-014, DB-015).
 *
 * An entry registered here stays until tiku_trk_deskbar_remove_team(); the
 * roster's entries come and go through tiku_trk_deskbar_add_team_pid() and
 * tiku_trk_deskbar_prune_teams().
 *
 * @return Its index, or -1.
 */
int tiku_trk_deskbar_add_team(tiku_trk_deskbar_t *db, const char *name);

/**
 * @brief Add process @p pid to the entry named @p name, making the entry
 *        when there is none (DB-015).
 *
 * @p command becomes the entry's command when it has none.
 *
 * @return the team index, or -1 when the list is full; @p changed is set
 *         nonzero when a row appeared or the pid was new.
 */
int tiku_trk_deskbar_add_team_pid(tiku_trk_deskbar_t *db, const char *name,
                                  long pid, const char *command,
                                  int *changed);

/**
 * @brief Drop instances no longer in @p live; a team whose last one went
 *        disappears (DB-015).  Shell-seeded teams stay.
 *
 * @return nonzero when anything changed.
 */
int tiku_trk_deskbar_prune_teams(tiku_trk_deskbar_t *db, const long *live,
                                 int n);

/** @brief Remove entry @p team now, shell-seeded or not. */
void tiku_trk_deskbar_remove_team(tiku_trk_deskbar_t *db, int team);

/** @brief The entry named @p name, matched without regard to case as
 *         add_team matches it, without adding one.  @return it, or -1. */
int tiku_trk_deskbar_find_team(const tiku_trk_deskbar_t *db,
                               const char *name);

/** @brief Set the icon entry @p team is drawn with: a name from the desk's
 *         icon set, or NULL or "" for the generic one. */
void tiku_trk_deskbar_set_team_icon(tiku_trk_deskbar_t *db, int team,
                                    const char *icon);

/** @brief The name of entry @p team, by the index add_team returned or a
 *         pick carries (team_name() takes a place in the display order).
 *  @return it, or NULL for no such entry. */
const char *tiku_trk_deskbar_team_app(const tiku_trk_deskbar_t *db,
                                      int team);

/** @brief Entry @p team's windows as last published, into @p wins, and
 *         whether it holds the focused one, into @p active (either may be
 *         NULL).  @return how many, 0 for no such entry. */
int tiku_trk_deskbar_team_windows(const tiku_trk_deskbar_t *db, int team,
                                  const tiku_trk_dbwin_t **wins, int *active);

/** @brief The icon entry @p team is drawn with: its own, else "folder"
 *         for the TikuTracker entry and "application" for any other. */
const char *tiku_trk_deskbar_team_icon(const tiku_trk_deskbar_t *db,
                                       int team);

/** @brief The pids behind entry @p team.  @return how many were written. */
int tiku_trk_deskbar_team_pids(const tiku_trk_deskbar_t *db, int team,
                               long *out, int max);

/** @brief The command a new instance of @p team would run, or "". */
const char *tiku_trk_deskbar_team_command(const tiku_trk_deskbar_t *db,
                                          int team);

/** @brief Replace an application's window list, keeping its expanded
 *         state; the windows are kept in natural title order (DB-031). */
void tiku_trk_deskbar_set_windows(tiku_trk_deskbar_t *db, int team,
                                  const tiku_trk_dbwin_t *wins, int n,
                                  int active);

/**
 * @brief Set @p owner's tray item @p id: its @p icon, @p text beside it and
 *        @p tip, added at the end of the icons when new (DB-042).  An empty
 *        icon and text take it away instead.
 * @return 1 when the tray changed, 0 when nothing did, -1 when the tray is
 *         full or the item is malformed.
 */
int tiku_trk_deskbar_trayicon_set(tiku_trk_deskbar_t *db, uint64_t owner,
                                  const char *id, const char *icon,
                                  const char *text, const char *tip);

/** @brief Take @p owner's item @p id away.  @return 1 when it was there. */
int tiku_trk_deskbar_trayicon_remove(tiku_trk_deskbar_t *db, uint64_t owner,
                                     const char *id);

/** @brief Remove every item whose owner is not among the @p n in
 *         @p owners.  @return 1 when any was removed. */
int tiku_trk_deskbar_trayicon_keep(tiku_trk_deskbar_t *db,
                                   const uint64_t *owners, int n);

/** @brief The application icons in the tray.  @return how many, with
 *         item @p i into @p out when it is one of them (either optional). */
int tiku_trk_deskbar_trayicons(const tiku_trk_deskbar_t *db, int i,
                               tiku_trk_trayicon_t *out);

/** @brief Where tray item @p i is drawn and answers the pointer, as the
 *         bar was last laid out; empty for no such item. */
tiku_rect_t tiku_trk_deskbar_trayicon_rect(const tiku_trk_deskbar_t *db,
                                           int i);

/**
 * @brief Subscribe the tray to @p path under @p label (DB-038).
 *
 * @return the item's numeric id (positive), the existing id when the
 *         same path is already subscribed, or -1: the tray is full, or a
 *         different path already has that label (the duplicate-name
 *         refusal).
 */
int tiku_trk_deskbar_add_replicant(tiku_trk_deskbar_t *db, const char *label,
                                   const char *path);

/** @brief Tray index of the item with @p id, or -1. */
int tiku_trk_deskbar_replicant_by_id(const tiku_trk_deskbar_t *db, int id);

/** @brief Tray index of the item labelled @p name, or -1. */
int tiku_trk_deskbar_replicant_by_name(const tiku_trk_deskbar_t *db,
                                       const char *name);

/** @brief Unsubscribe the item with @p id.  @return nonzero if removed. */
int tiku_trk_deskbar_drop_replicant(tiku_trk_deskbar_t *db, int id);

/**
 * @brief Notice moved or deleted tray files (DB-040).
 *
 * A path that no longer resolves is looked up by node identity: found
 * elsewhere, the item is re-pointed and keeps its place; not found, it is
 * removed.  @return nonzero when anything changed.
 */
int tiku_trk_deskbar_sync_replicants(tiku_trk_deskbar_t *db);
/**
 * @brief Record the height the replicant at @p path asked for, within the
 *        tray's limit, and save the tray.
 * @return 1 when the path is in the tray, 0 otherwise or for a height out
 *         of range.
 */
int tiku_trk_deskbar_set_replicant_height(tiku_trk_deskbar_t *db,
                                          const char *path, int height);
/** @brief The height recorded for the replicant at @p path, or 0. */
int tiku_trk_deskbar_replicant_height(const tiku_trk_deskbar_t *db,
                                      const char *path);
/** @brief Show or hide the tray's replicants. */
void tiku_trk_deskbar_set_show_replicants(tiku_trk_deskbar_t *db, int show);
/** @brief Whether the tray shows its replicants. */
int tiku_trk_deskbar_show_replicants(const tiku_trk_deskbar_t *db);

/** @brief Re-read the tray's nodes; one that cannot be read is removed.
 *  @return how many values changed, a removal counting as one. */
int tiku_trk_deskbar_refresh(tiku_trk_deskbar_t *db);

/**
 * @brief As tiku_trk_deskbar_refresh(), skipping every replicant whose path
 *        begins with @p except; the caller reads those and hands each
 *        value to tiku_trk_deskbar_replicant_set().
 */
int tiku_trk_deskbar_refresh_except(tiku_trk_deskbar_t *db,
                                    const char *except);

/** @brief Copy one live replicant by tray index; return 0 or -1 out of range. */
int tiku_trk_deskbar_replicant_at(const tiku_trk_deskbar_t *, int, tiku_trk_replicant_t *);

/** @brief The paths of the replicants, into @p out.
 *  @return how many were written, at most @p max. */
int tiku_trk_deskbar_replicant_paths(const tiku_trk_deskbar_t *db,
                                     char out[][TIKU_PATH_MAX], int max);

/**
 * @brief Set the value of the replicant at @p path from a read the caller
 *        made; NULL, for a read that failed, removes the replicant.
 * @return nonzero when something shown changed.
 */
int tiku_trk_deskbar_replicant_set(tiku_trk_deskbar_t *db, const char *path,
                                   const char *value);

/** @brief How many applications the bar is tracking. */
int tiku_trk_deskbar_team_count(const tiku_trk_deskbar_t *db);

/** @brief The name of the application in row @p i of the list, in the
 *         order it is shown, or NULL. */
const char *tiku_trk_deskbar_team_name(const tiku_trk_deskbar_t *db, int i);

/**
 * @brief Strip the owning application's name from a window title.
 *
 * "TikuTracker: /sys" and "/sys - TikuTracker" both become "/sys", and
 * "TikuPDF - guide.pdf" becomes "guide.pdf".
 */
void tiku_trk_deskbar_strip_title(const char *app, const char *title,
                                  char *out, size_t max);

/** @brief The bar's rectangle as last laid out: its sliver while hidden. */
tiku_rect_t tiku_trk_deskbar_frame(const tiku_trk_deskbar_t *db);

/** @brief Draw the whole bar onto @p s, laying its frame out first. */
void tiku_trk_deskbar_draw(tiku_trk_deskbar_t *db, tiku_surface_t *s);

/** @brief Frame time in microseconds; zero disables animation. */
void tiku_trk_deskbar_set_now(tiku_trk_deskbar_t *db, int64_t now);
/** @brief Region needing another frame, or an empty rectangle when idle. */
tiku_rect_t tiku_trk_deskbar_motion_damage(const tiku_trk_deskbar_t *db);

/** @brief Whether the point is inside the bar's frame. */
int tiku_trk_deskbar_hit(const tiku_trk_deskbar_t *db, int x, int y);

/**
 * @brief Handle a click: an open popup takes it first, then the tray
 *        icons, the leaf, the theme switch, the scroll arrows, the
 *        application list and the clock.
 *
 * @param mods    TIKU_TRK_MOD_* held: on a window row, Cmd minimises the
 *                window.
 * @param button  1 primary, 2 middle, 3 secondary.
 * @param clicks  2 for a double click.
 * @param pick    Receives what the click asked for.
 * @return 1 when the click was consumed.
 */
int tiku_trk_deskbar_click(tiku_trk_deskbar_t *db, int x, int y,
                           unsigned mods, int button, int clicks,
                           tiku_trk_db_pick_t *pick);

/** @brief What sits under a drag hovering the Deskbar. */
typedef enum {
    TIKU_TRK_DB_DROP_NONE = 0,
    TIKU_TRK_DB_DROP_LEAF,      /* the logo: links go in the Deskbar folder */
    TIKU_TRK_DB_DROP_TEAM,      /* an application: hand the files to it     */
    TIKU_TRK_DB_DROP_MENU_DIR,  /* a folder in the menu: links go there     */
    TIKU_TRK_DB_DROP_MENU_FILE, /* a file in the menu: launch it with them  */
    TIKU_TRK_DB_DROP_BAR,       /* the strip itself: launch the files       */
    TIKU_TRK_DB_DROP_TRAY       /* the tray: subscribe the node (DB-038)    */
} tiku_trk_db_drop_t;

/**
 * @brief What a drag hovering @p x, @p y would drop onto.
 *
 * @param which Receives the team index for DROP_TEAM, or -1.
 */
tiku_trk_db_drop_t tiku_trk_deskbar_drop_at(const tiku_trk_deskbar_t *db,
                                            int x, int y, int *which);

/**
 * @brief Tell the bar whether a drag is in flight.
 *
 * While one is, tiku_trk_deskbar_item_enabled() reports the fixed leaf-menu
 * rows as disabled: they are not drop targets (DB-008).  Ending the drag
 * clears the drop highlight.
 */
void tiku_trk_deskbar_set_dragging(tiku_trk_deskbar_t *db, int dragging);

/**
 * @brief What a drag over the open leaf menu is on (DB-009).
 *
 * @param path Receives the entry's path for DROP_MENU_DIR and
 *             DROP_MENU_FILE.
 * @return DROP_MENU_DIR for a folder entry or an open-path row with the
 *         "folder" icon (a mounted store's row), DROP_MENU_FILE for any
 *         other open-path row, DROP_NONE for a fixed row or a point off
 *         the menu.
 */
tiku_trk_db_drop_t tiku_trk_deskbar_menu_drop_at(const tiku_trk_deskbar_t *db,
                                                 int x, int y, char *path,
                                                 size_t max);

/**
 * @brief Light the application a hovering drag would be handed to.
 *
 * Pass -1 for none.  The bar draws the highlight from this value
 * (DB-035).
 */
void tiku_trk_deskbar_set_drop_team(tiku_trk_deskbar_t *db, int i);

/** @brief Which application is lit, or -1. */
int tiku_trk_deskbar_drop_team(const tiku_trk_deskbar_t *db);

/** @brief Whether a leaf-menu row is enabled: a fixed row (@p fixed
 *         nonzero) not while a drag is in flight, any other always
 *         (DB-008). */
int tiku_trk_deskbar_item_enabled(const tiku_trk_deskbar_t *db, int fixed);

/**
 * @brief Whether team @p i can be handed files of these types (DB-035).
 *
 * Each of the @p ntypes dragged types must be one given to the team by
 * tiku_trk_deskbar_team_type(); a drag with no types returns 0.  @p force
 * (Ctrl held) accepts anything, as does Tracker's own entry.
 */
int tiku_trk_deskbar_team_accepts(const tiku_trk_deskbar_t *db, int i,
                                  const char *const *types, int ntypes,
                                  int force);

/** @brief Give team @p i a type it can open.
 *  @return 0, or -1 for no such team, no type, or eight types already. */
int tiku_trk_deskbar_team_type(tiku_trk_deskbar_t *db, int i,
                               const char *type);

/**
 * @brief Whether node @p m should be launched by a Deskbar drop (DB-066).
 *
 * @return 0 for a directory or NULL, which a drop skips without a message;
 *         1 for anything else.
 */
int tiku_trk_deskbar_launchable(const tiku_model_t *m);

/**
 * @brief Whether any popup of the bar is open: the leaf menu, the
 *        calendar, the clock menu, a window menu or the mini dock's
 *        running-apps menu.
 */
int tiku_trk_deskbar_menu_open(const tiku_trk_deskbar_t *db);
/** @brief Open the leaf menu, as the Menu key does (DB-011). */
void tiku_trk_deskbar_open_menu(tiku_trk_deskbar_t *db);

/**
 * @brief Close every popup of the bar: the leaf menu, the calendar, the
 *        clock menu, the window menu and the mini dock's running-apps menu.
 */
void tiku_trk_deskbar_close_menu(tiku_trk_deskbar_t *db);

/**
 * @brief Tell the bar which theme is in effect, for the tray's
 *        three-position switch.
 *
 * The bar only draws the state; moving the switch is a pick
 * (TIKU_TRK_DB_TOGGLE_DARK) that the shell answers by stepping the
 * DarkMode setting to its next value.
 *
 * @param which 0 light, 1 dark, 2 without hue.  Anything else reads as 0.
 */
void tiku_trk_deskbar_set_theme(tiku_trk_deskbar_t *db, int which);

/** @brief Reserve the top @p px of the screen (the global menu bar). */
void tiku_trk_deskbar_set_top_inset(tiku_trk_deskbar_t *db, int px);

/**
 * @brief Offer @p label under Utilities, starting @p command when
 *        chosen, drawn with the art named by @p icon.
 *
 * @return nonzero when the row was taken.
 */
int tiku_trk_deskbar_add_app(tiku_trk_deskbar_t *db, const char *label,
                             const char *command, const char *icon);

/**
 * @brief Which group of the leaf menu an installed program's row lives in.
 *
 * The leaf menu shows each group that has programs, in the order
 * Documents, Graphics & Design, Utilities, Development, Device Tools and
 * Demos; the values here do not follow that order.
 */
enum {
    TIKU_TRK_DB_GROUP_APPS = 0,
    TIKU_TRK_DB_GROUP_PROGRAMMING,
    TIKU_TRK_DB_GROUP_DEVICE_TOOLS,
    TIKU_TRK_DB_GROUP_DEMOS,
    TIKU_TRK_DB_GROUP_DOCUMENTS,
    TIKU_TRK_DB_GROUP_GRAPHICS,
    TIKU_TRK_DB_GROUP_COUNT,
    /* APPS and PROGRAMMING, by the names the leaf menu shows them under. */
    TIKU_TRK_DB_GROUP_UTILITIES = TIKU_TRK_DB_GROUP_APPS,
    TIKU_TRK_DB_GROUP_DEVELOPMENT = TIKU_TRK_DB_GROUP_PROGRAMMING
};

/**
 * @brief Offer @p label in leaf group @p group (TIKU_TRK_DB_GROUP_*),
 *        starting @p command when chosen, drawn with the art named by
 *        @p icon.
 *
 * Each group with programs is a row of the leaf menu, expanding to its
 * programs sorted by label.
 *
 * @return nonzero when the row was taken; 0 for a group out of range or a
 *         full table.
 */
int tiku_trk_deskbar_add_app_in(tiku_trk_deskbar_t *db, int group,
                                const char *label, const char *command,
                                const char *icon);

/** @brief Where the theme switch sits, as last laid out. */
tiku_rect_t tiku_trk_deskbar_switch_rect(const tiku_trk_deskbar_t *db);

/** @brief Whether the clock's calendar is showing. */
int tiku_trk_deskbar_calendar_open(const tiku_trk_deskbar_t *db);
/** @brief The calendar's shown year and month (1..12), and its chosen
 *         day. */
void tiku_trk_deskbar_calendar_date(const tiku_trk_deskbar_t *db,
                                    int *year, int *month, int *day);
/** @brief Whether the clock is shown. */
int tiku_trk_deskbar_clock_visible(const tiku_trk_deskbar_t *db);
/** @brief Show or hide the clock; hiding it closes the calendar too. */
void tiku_trk_deskbar_set_clock_visible(tiku_trk_deskbar_t *db, int visible);
/** @brief Close the calendar and the clock menu. */
void tiku_trk_deskbar_close_calendar(tiku_trk_deskbar_t *db);

#endif /* TIKU_TRK_DESKBAR_H_ */
