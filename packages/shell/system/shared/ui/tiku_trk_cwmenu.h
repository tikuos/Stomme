/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_cwmenu.h - a folder window's menus.
 *
 * File and Window always, Attributes only in list view, and the context
 * menus chosen by what the pointer is over.  Action items are added through
 * the action table, so every menu agrees on what is enabled.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_CWMENU_H_
#define TIKU_TRK_CWMENU_H_

#include "tiku_menu.h"
#include "tiku_model.h"
#include "tiku_trk_shortcuts.h"

/* One command per partition in the Mount submenu (CW-034). */
#define TIKU_TRK_CMD_MOUNT_PART_BASE 1600
/* The save panel's own errand: write the Find panel's query as a file. */
#define TIKU_TRK_CMD_SAVE_QUERY      1700
/* The file panel picking a folder to add to a search's scope. */
#define TIKU_TRK_CMD_ADD_SCOPE       1701
/* A file pick an application asked for; the shell keeps who asked in its
 * pending_pick. */
#define TIKU_TRK_CMD_APP_PICK        1702
/* The Info window icon's menu: recalculate a folder's size. */
#define TIKU_TRK_CMD_RECALC_SIZE     1707
/* The Info window icon's menu: choose a broken link's new target. */
#define TIKU_TRK_CMD_SET_LINK_TARGET 1703
/* The Sounds window asked for a file from anywhere for one moment. */
#define TIKU_TRK_CMD_CHOOSE_SOUND    1704
/* TikuReader's file picker. */
#define TIKU_TRK_CMD_READER_OPEN     1705
/* The image viewer's file picker. */
#define TIKU_TRK_CMD_IMAGE_OPEN      1706
/* The Move to / Copy to / Create link pickers: 16 destinations each
 * (CW-035).  (command - BASE) / 16 says which picker, % 16 which entry. */
#define TIKU_TRK_CMD_DEST_BASE       1720
#define TIKU_TRK_CMD_DEST_MAX        16
/* How many partitions the Mount submenu lists. */
#define TIKU_TRK_MOUNT_PART_MAX 32

/** @brief A folder window's menu bar, its menus and the context menus. */
typedef struct {
    tiku_menubar_t bar;          /* File, Window, and Attributes in list  */
    tiku_menu_t    file;
    tiku_menu_t    new_menu;     /* New folder and discovered templates */
    tiku_menu_t    templates;    /* root of the template hierarchy       */
    tiku_menu_t    window;
    tiku_menu_t    attributes;   /* in the bar only in list view         */
    tiku_menu_t    arrange;      /* submenu of Window                   */
    tiku_menu_t    icon_size;    /* submenu of Window                   */
    tiku_menu_t    addons;      /* add-on inventory                      */
    /* Mount >: every partition, mounted ones marked, plus Mount all and
     * Settings… (CW-034).  The names are kept beside the menu, so a picked
     * command is answered from the scan the menu was built from. */
    tiku_menu_t    mount_menu;
    char               mount_part[TIKU_TRK_MOUNT_PART_MAX][32];
    int                mount_parts;  /* mount_part[] in use               */
    /* The context menus, one per target, so which one appeared is known
     * afterwards. */
    tiku_menu_t    pose_ctx;
    tiku_menu_t    window_ctx;
    tiku_menu_t    volume_ctx;
    tiku_menu_t    trash_ctx;
    tiku_menu_t    drop_ctx;
    tiku_menu_t    view_ctx;    /* the gutter's view-mode popup     */
    /* The Info window icon's own copy of its context menu: the shared
     * pose_ctx is rebuilt by every folder draw, and the icon's extra items
     * (AW-076) are kept across frames. */
    tiku_menu_t    info_ctx;
    /* The applications that can open what is selected, as a submenu of the
     * "Open with" item.  Built by the shell from the registry; the File menu
     * and the pose menu show this same submenu. */
    tiku_menu_t    open_with;
    /* The Move to, Copy to and Create link as pickers: Current folder,
     * Recent folders, Desktop, Home and the volumes, built by the shell
     * from its recent folders and mounts (CW-035, CW-058). */
    tiku_menu_t    picker[3];
    char               picker_path[3][TIKU_TRK_CMD_DEST_MAX][TIKU_PATH_MAX];
    int                picker_paths[3];
    /* One submenu per type the folder holds, each offering the columns
     * that type has to show (CW-026).  Filled by the shell from the
     * listing. */
    tiku_menu_t    per_type[4];
    char               per_type_icon[4][32]; /* the type's art (IV-056)   */
    char               per_type_name[4][TIKU_TYPE_MAX]; /* submenu titles */
    int                per_type_count;   /* per_type[] in use           */
    int                attributes_shown; /* Attributes is in the bar    */
    char               templates_root[TIKU_PATH_MAX]; /* ~/Templates    */
    struct {
        tiku_menu_t menu;
        char            path[TIKU_PATH_MAX];
    } template_node[64];             /* a submenu per template folder   */
    int                template_node_count;
    struct {
        int  command;
        int  add_submenu;
        char path[TIKU_PATH_MAX];
    } template_entry[128];           /* what each template command means */
    int                template_entry_count;
} tiku_trk_cwmenu_t;

/** @brief The command range an attribute column is toggled with. */
#define TIKU_TRK_CMD_COL_ATTR_BASE 800

/** @brief The command range a candidate application is picked with. */
#define TIKU_TRK_CMD_OPEN_WITH_BASE 900
/** @brief The command range of the template rows. */
#define TIKU_TRK_CMD_TEMPLATE_BASE 1200
/** @brief The command range of the "Add new submenu" rows. */
#define TIKU_TRK_CMD_ADD_TEMPLATE_SUBMENU_BASE 1400

/** @brief Empty menus, with the Templates folder under the home. */
void tiku_trk_cwmenu_init(tiku_trk_cwmenu_t *cw);

/** @brief The template a template command stands for, or NULL. */
const char *tiku_trk_cwmenu_template_path(const tiku_trk_cwmenu_t *cw,
                                           int command);

/** @brief The folder an "Add new submenu" command adds to, or NULL. */
const char *tiku_trk_cwmenu_template_add_path(const tiku_trk_cwmenu_t *cw,
                                               int command);

/** @brief The user's template directory, or NULL when none is configured. */
const char *tiku_trk_cwmenu_templates_root(const tiku_trk_cwmenu_t *cw);

/**
 * @brief Rebuild every menu against @p c.
 *
 * Called when a menu is about to open and whenever the modifiers change while
 * one is open: labels and enabled states are decided now, and the Templates
 * folder and the partitions are scanned again.
 */
void tiku_trk_cwmenu_rebuild(tiku_trk_cwmenu_t *cw,
                             const tiku_trk_sc_ctx_t *c);

/**
 * @brief Which context menu belongs to what was clicked, rebuilt first.
 *
 * @param m NULL when the click was on empty window background.
 */
tiku_menu_t *tiku_trk_cwmenu_context(tiku_trk_cwmenu_t *cw,
                                         const tiku_model_t *m,
                                         const tiku_trk_sc_ctx_t *c);

/** @brief The menu offered when items are dropped with the second button. */
tiku_menu_t *tiku_trk_cwmenu_drop(tiku_trk_cwmenu_t *cw,
                                      int dst_read_only, int src_read_only,
                                      unsigned mods);

/** @brief The Find icon's drop menu: query, template, cancel (Q-034). */
tiku_menu_t *tiku_trk_cwmenu_query_drop(tiku_trk_cwmenu_t *cw,
                                            int dst_read_only);

/**
 * @brief The view-mode popup the scroller gutter offers (NeXT UIG ch7),
 *        with the mode in force marked.
 */
tiku_menu_t *tiku_trk_cwmenu_view_popup(tiku_trk_cwmenu_t *cw,
                                            const tiku_trk_sc_ctx_t *c);

#endif /* TIKU_TRK_CWMENU_H_ */
