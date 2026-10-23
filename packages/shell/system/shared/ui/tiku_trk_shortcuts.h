/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_shortcuts.h - the action table: for every user action, its
 * label, the command it sends, its shortcut key, and whether it is enabled
 * now.
 *
 * Menus are built and updated through it, so the File menu and the context
 * menus agree on every action.  Eight actions rename themselves while
 * Shift is held, and each changes its command with its label.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_SHORTCUTS_H_
#define TIKU_TRK_SHORTCUTS_H_

#include "tiku_menu.h"

/** @brief The action commands: Tracker's actions, with this port's values. */
enum {
    TIKU_TRK_CMD_NONE = 0,
    TIKU_TRK_CMD_FIND,
    TIKU_TRK_CMD_NEW_FOLDER,
    TIKU_TRK_CMD_OPEN,
    /* Placed between Open and Open with, as in the original menus
     * (CW-033). */
    TIKU_TRK_CMD_EDIT_QUERY,
    TIKU_TRK_CMD_OPEN_WITH,
    TIKU_TRK_CMD_GET_INFO,
    TIKU_TRK_CMD_EDIT_NAME,
    TIKU_TRK_CMD_DUPLICATE,
    TIKU_TRK_CMD_MOVE_TO_TRASH,
    TIKU_TRK_CMD_DELETE,             /* what Move to Trash becomes on Shift */
    TIKU_TRK_CMD_RESTORE,
    TIKU_TRK_CMD_EMPTY_TRASH,
    TIKU_TRK_CMD_MOVE_TO,
    TIKU_TRK_CMD_COPY_TO,
    TIKU_TRK_CMD_CREATE_LINK,
    TIKU_TRK_CMD_CREATE_REL_LINK,    /* Shift                               */
    TIKU_TRK_CMD_CUT,
    TIKU_TRK_CMD_CUT_MORE,           /* Shift                               */
    TIKU_TRK_CMD_COPY,
    TIKU_TRK_CMD_COPY_MORE,          /* Shift                               */
    TIKU_TRK_CMD_PASTE,
    TIKU_TRK_CMD_PASTE_LINKS,        /* Shift                               */
    TIKU_TRK_CMD_COPY_LAYOUT,
    TIKU_TRK_CMD_PASTE_LAYOUT,
    TIKU_TRK_CMD_IDENTIFY,
    TIKU_TRK_CMD_ICON_VIEW,
    TIKU_TRK_CMD_MINI_ICON_VIEW,
    TIKU_TRK_CMD_LIST_VIEW,
    TIKU_TRK_CMD_RESIZE_TO_FIT,
    TIKU_TRK_CMD_ARRANGE_BY,
    TIKU_TRK_CMD_CLEANUP,
    TIKU_TRK_CMD_CLEANUP_ALL,        /* Shift                               */
    TIKU_TRK_CMD_REVERSE_ORDER,
    TIKU_TRK_CMD_SELECT_MATCHING,
    TIKU_TRK_CMD_SELECT_ALL,
    TIKU_TRK_CMD_INVERT_SELECTION,
    TIKU_TRK_CMD_OPEN_PARENT,
    TIKU_TRK_CMD_CLOSE,
    TIKU_TRK_CMD_CLOSE_ALL,          /* Shift                               */
    TIKU_TRK_CMD_CLOSE_ALL_WORKSPACE,
    TIKU_TRK_CMD_PREFERENCES,
    TIKU_TRK_CMD_EDIT_TEMPLATES = 1320,
    TIKU_TRK_CMD_SORT_BY_NAME = TIKU_TRK_CMD_PREFERENCES + 2,
    TIKU_TRK_CMD_SORT_BY_KIND,
    TIKU_TRK_CMD_SORT_BY_SIZE,
    TIKU_TRK_CMD_SORT_BY_VALUE,
    /* Every rung of the ladder Cmd-+/- walks, so the menu can reach the
     * same sizes the keyboard can (PVL-002). */
    TIKU_TRK_CMD_ICON_SIZE_32,
    TIKU_TRK_CMD_ICON_SIZE_40,
    TIKU_TRK_CMD_ICON_SIZE_48,
    TIKU_TRK_CMD_ICON_SIZE_64,
    TIKU_TRK_CMD_ICON_SIZE_96,
    TIKU_TRK_CMD_ICON_SIZE_128,
    TIKU_TRK_CMD_SORT_BY_CAP,
    TIKU_TRK_CMD_COL_NAME,
    TIKU_TRK_CMD_COL_KIND,
    TIKU_TRK_CMD_COL_SIZE,
    TIKU_TRK_CMD_COL_VALUE,
    TIKU_TRK_CMD_COL_CAP,
    TIKU_TRK_CMD_COL_DATE,
    TIKU_TRK_CMD_COL_PATH,
    TIKU_TRK_CMD_COL_PERM,
    TIKU_TRK_CMD_COL_ORIGINAL,
    TIKU_TRK_CMD_ADD_ONS,
    TIKU_TRK_CMD_MOUNT,
    TIKU_TRK_CMD_UNMOUNT,
    TIKU_TRK_CMD_MOUNT_ALL,
    TIKU_TRK_CMD_MOUNT_SETTINGS,
    TIKU_TRK_CMD_OPEN_WITH_WINDOW,
    /* Drop menu */
    TIKU_TRK_CMD_MOVE_HERE,
    TIKU_TRK_CMD_COPY_HERE,
    TIKU_TRK_CMD_LINK_HERE,
    TIKU_TRK_CMD_REL_LINK_HERE,      /* Shift                               */
    /* The global bar's system menu */
        TIKU_TRK_CMD_ABOUT,
    TIKU_TRK_CMD_APP_ABOUT,          /* the focused program's, hoisted */
    TIKU_TRK_CMD_APP_HELP,           /* the page the program named      */
    TIKU_TRK_CMD_SCREENSHOT,         /* save the screen to a PNG */
    TIKU_TRK_CMD_SHUTDOWN,
    /* The Devices menu on the global bar */
    TIKU_TRK_CMD_DEVICE_HOST,        /* be current on this machine */
    TIKU_TRK_CMD_DEVICE_SCAN,        /* re-list the bus */
    /* Query-icon drop menu (Q-034) */
    TIKU_TRK_CMD_CREATE_QUERY,
    TIKU_TRK_CMD_CREATE_QUERY_TPL,
    TIKU_TRK_CMD_CANCEL
};

/*
 * The Devices menu's dynamic rows: one command per connected device
 * (PICK), one per available port (CONNECT), and one Disconnect per device
 * (DROP).  The bases are high and fixed, so the index added to them never
 * collides with the enum above or with a folder command.
 */
#define TIKU_TRK_CMD_DEVICE_PICK_BASE    0x2000
#define TIKU_TRK_CMD_DEVICE_CONNECT_BASE 0x2100
#define TIKU_TRK_CMD_DEVICE_DROP_BASE    0x2200

/* A device tray cell's menu on the global bar: each acts on the board the
 * menu dropped from. */
#define TIKU_TRK_CMD_DEVTRAY_OPEN       0x2300
#define TIKU_TRK_CMD_DEVTRAY_PREFS      0x2301
#define TIKU_TRK_CMD_DEVTRAY_DISCONNECT 0x2302
#define TIKU_TRK_CMD_DEVTRAY_RECLAIM    0x2303

/**
 * @brief The up arrow as a shortcut key: outside the printable range, so
 *        it cannot be taken for a letter.
 */
#define TIKU_TRK_KEY_ARROW_UP 0x01

/**
 * @brief What the enablement rules are asked about, one field per question
 *        Tracker's Update methods ask.
 */
typedef struct {
    int      selection;          /* items selected                          */
    int      selection_ro;       /* selection lives somewhere unwritable    */
    int      target_ro;          /* this folder cannot be written           */
    int      is_root;            /* the window shows the root               */
    int      is_trash;           /* the window shows the Trash              */
    int      in_trash;           /* the window shows a folder in the Trash  */
    int      is_query;           /* the window shows a query's results      */
    int      is_virtual;         /* a namespace directory: fixed shape      */
    int      is_desktop;         /* the desktop background, not a folder    */
    int      selection_is_volume;    /* the selection is a volume           */
    int      selection_is_boot;      /* the boot volume cannot be unmounted */
    int      can_edit_name;      /* the selection can be renamed            */
    int      can_open_parent;    /* the folder has a parent to open         */
    int      can_trash_or_dup;   /* selection can be trashed or duplicated  */
    int      trash_full;         /* the Trash holds something               */
    int      clipboard_has_refs; /* files are on the clipboard              */
    int      clipboard_has_layout;   /* a column layout has been copied     */
    int      text_focus;         /* a text field has focus                  */
    int      view_mode;          /* TIKU_TRK_MODE_*                         */
    int      icon_size;          /* in pixels                               */
    int      sort_field;         /* TIKU_TRK_COL_* the view sorts by        */
    int      reverse_sort;       /* the sort runs descending                */
    /* Which columns the view is showing, as a bit per TIKU_TRK_COL_*, so the
     * Attributes menu can mark the ones shown. */
    unsigned columns;
    unsigned mods;               /* live modifiers, for the Shift renames   */
} tiku_trk_sc_ctx_t;

/**
 * @brief The label an action shows right now: never NULL, and "" for a
 *        command the table does not know.
 */
const char *tiku_trk_sc_label(int command, unsigned mods);

/** @brief The command an action sends right now, given the modifiers. */
int tiku_trk_sc_command(int command, unsigned mods);

/** @brief Whether an action is available in @p c. */
int tiku_trk_sc_enabled(int command, const tiku_trk_sc_ctx_t *c);

/** @brief The shortcut key for an action, or 0. */
char tiku_trk_sc_key(int command);

/**
 * @brief The command a Cmd+letter chord asks for, or 0 (PVK-042).
 *
 * The shells' key fallback, over the keys o, i, e, g, u, n, x, c, v and k:
 * Shift picks the shifted commands, and Option turns Open into Open with.
 * Each letter but u (Unmount) is the one tiku_trk_sc_key() gives the
 * command.
 */
int tiku_trk_sc_command_for_key(unsigned key, unsigned mods);

/** @brief The modifier mask drawn with the shortcut, given live modifiers. */
unsigned tiku_trk_sc_mods(int command, unsigned mods);

/**
 * @brief Append an action to @p m, resolved against @p c.
 *
 * The label, command, shortcut and enabled state all come from this table.
 *
 * @return what tiku_menu_add() returns.
 */
int tiku_trk_sc_add(tiku_menu_t *m, int command,
                    const tiku_trk_sc_ctx_t *c);

/**
 * @brief Re-resolve every item of @p m against @p c.
 *
 * Called when a menu opens and whenever the modifiers change while it is
 * open.  Submenus are updated in turn; a row whose command the table does
 * not know keeps its own label and state, and marks are left alone.
 */
void tiku_trk_sc_update(tiku_menu_t *m, const tiku_trk_sc_ctx_t *c);

#endif /* TIKU_TRK_SHORTCUTS_H_ */
