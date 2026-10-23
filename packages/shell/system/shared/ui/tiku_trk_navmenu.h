/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_navmenu.h - navigation menus: a menu that walks a folder tree,
 * one level per open folder.
 *
 * A level is listed a chunk at a time and afresh every time it opens; it
 * hides dot files when the window hides them, and the Trash.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_NAVMENU_H_
#define TIKU_TRK_NAVMENU_H_

#include "tiku_model.h"

/** @brief How many entries one level holds. */
#define TIKU_TRK_NAV_MAX 256

/** @brief How many levels deep a walk may go. */
#define TIKU_TRK_NAV_DEPTH 8

/** @brief How many entries one fill step adds. */
#define TIKU_TRK_NAV_CHUNK 20
/** @brief A time budget per fill step; the fill does not read it. */
#define TIKU_TRK_NAV_SLICE_US 200000

/** @brief One entry in a level. */
typedef struct {
    char name[TIKU_NAME_MAX];       /* what the row shows                */
    char path[TIKU_PATH_MAX];       /* the entry it stands for           */
    int  is_container;              /* opens a submenu of its own        */
    int  enabled;                   /* the row can be chosen             */
} tiku_trk_nav_item_t;

/** @brief One open level of the walk. */
typedef struct {
    char                path[TIKU_PATH_MAX];    /* the folder listed     */
    tiku_trk_nav_item_t item[TIKU_TRK_NAV_MAX]; /* its entries, sorted   */
    int                 n;          /* item[] in use                     */
    int                 sel;        /* highlighted row, or -1            */
    int                 done;       /* the fill has reached the end      */
    void               *walk;       /* not used                          */
} tiku_trk_nav_level_t;

/** @brief A walk: a stack of levels, the last one innermost. */
typedef struct {
    tiku_trk_nav_level_t level[TIKU_TRK_NAV_DEPTH];
    int                  depth;         /* level[] in use                */
    tiku_backend_t  *backend;           /* where the folders are listed  */
    int                  hide_dotfiles; /* hide ".name" entries          */
    int                  folders_first; /* folders sort ahead; on        */
} tiku_trk_nav_t;

/**
 * @brief Begin a walk at @p path, with its level empty until
 *        tiku_trk_nav_fill() lists it.
 */
void tiku_trk_nav_open(tiku_trk_nav_t *nav, tiku_backend_t *b,
                       const char *path);

/**
 * @brief Add up to TIKU_TRK_NAV_CHUNK more entries to level @p level, and
 *        sort it (CW-055).
 *
 * The root of a local backend lists the volumes instead, in one step.
 *
 * @return 1 while more may remain, 0 once the level is complete.
 */
int tiku_trk_nav_fill(tiku_trk_nav_t *nav, int level);

/** @brief Fill a level completely, a chunk at a time. */
void tiku_trk_nav_fill_all(tiku_trk_nav_t *nav, int level);

/**
 * @brief Descend into the entry at @p i of the innermost level.
 *
 * Only a container opens, up to TIKU_TRK_NAV_DEPTH levels.  The new level
 * starts empty, so tiku_trk_nav_fill() lists the folder afresh (CW-055).
 *
 * @return 1 when a level was opened.
 */
int tiku_trk_nav_enter(tiku_trk_nav_t *nav, int i);

/** @brief Close the innermost level.  @return 1 when one was closed. */
int tiku_trk_nav_leave(tiku_trk_nav_t *nav);

/** @brief What the arrow keys do (CW-056). */
typedef enum {
    TIKU_TRK_NAV_UP = 0,
    TIKU_TRK_NAV_DOWN,
    TIKU_TRK_NAV_IN,
    TIKU_TRK_NAV_OUT
} tiku_trk_nav_key_t;

/**
 * @brief Move the highlight up or down, without wrapping, or open or close
 *        a level.  @return 1 when something changed.
 */
int tiku_trk_nav_move(tiku_trk_nav_t *nav, tiku_trk_nav_key_t k);

/** @brief The innermost level, or NULL. */
tiku_trk_nav_level_t *tiku_trk_nav_top(tiku_trk_nav_t *nav);

/** @brief What choosing the highlighted row would open, or NULL. */
const char *tiku_trk_nav_chosen(tiku_trk_nav_t *nav);

/**
 * @brief Whether a folder menu belongs on a context menu for @p m.
 *
 * Only a container gets one (CW-045).
 */
int tiku_trk_nav_wanted(const tiku_model_t *m);

/**
 * @brief Where a level sits, given where its parent item is.
 *
 * At @p px, @p py, 190 pixels wide; a level that would run off the right
 * edge opens to the left of @p px instead, and it is kept on screen
 * vertically.
 */
void tiku_trk_nav_place(const tiku_trk_nav_t *nav, int level, int px,
                        int py, int screen_w, int screen_h,
                        int *x, int *y, int *w, int *h);

struct tiku_trk_apps;
/**
 * @brief Enable or grey every item of every level for a spring-loaded drag
 *        (PVS-052): a folder unless its link is broken, an entry that does
 *        not resolve never, and a file only when it is an application in
 *        @p apps that takes one of @p types.
 */
void tiku_trk_nav_spring_states(tiku_trk_nav_t *nav,
                                const struct tiku_trk_apps *apps,
                                const char *const *types, int ntypes);

/** @brief The item at @p x, @p y in @p level, framed at @p fx, @p fy, or
 *         -1. */
int tiku_trk_nav_item_at(const tiku_trk_nav_t *nav, int level, int fx,
                         int fy, int x, int y);

/** @brief One row's height, in pixels. */
#define TIKU_TRK_NAV_ROW 18

/** @brief The text an empty level shows. */
#define TIKU_TRK_NAV_EMPTY "Empty folder"

#endif /* TIKU_TRK_NAVMENU_H_ */
