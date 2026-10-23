/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_info.h - the Get Info window: everything one entry is.
 *
 * Three tabs over one node.  Information is what the entry is: size, dates,
 * kind, where it lives, what a link points at, read through the same
 * formatters the columns use, so the two cannot say the same fact
 * differently.  Permissions is what it allows.  Attributes is what it
 * carries.
 *
 * A folder's size cannot be read off the folder, so it is walked in the
 * background and the field says "calculating" while the walk runs.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_INFO_H_
#define TIKU_TRK_INFO_H_

#include "tiku_model.h"
#include "tiku_state.h"

/** @brief The three tabs, in the order they are shown. */
typedef enum {
    TIKU_TRK_INFO_TAB_INFO = 0,
    TIKU_TRK_INFO_TAB_PERMISSIONS,
    TIKU_TRK_INFO_TAB_ATTRIBUTES,
    TIKU_TRK_INFO_TABS
} tiku_trk_info_tab_t;

/** @brief The most rows the Information tab lists, and the room for one
 *         row's or one attribute's value text. */
#define TIKU_TRK_INFO_FIELDS   8
#define TIKU_TRK_INFO_VAL_MAX  96

/** @brief Which field a row shows; the ones commented here take a click. */
typedef enum {
    TIKU_TRK_INFO_F_NONE = 0,
    TIKU_TRK_INFO_F_SIZE,       /* clicking recalculates a folder's       */
    TIKU_TRK_INFO_F_CAPACITY,
    TIKU_TRK_INFO_F_CREATED,
    TIKU_TRK_INFO_F_MODIFIED,
    TIKU_TRK_INFO_F_KIND,
    TIKU_TRK_INFO_F_LOCATION,   /* clicking opens the parent, selected    */
    TIKU_TRK_INFO_F_LINK,       /* clicking follows it, or re-points it   */
    TIKU_TRK_INFO_F_DIMENSIONS,
    TIKU_TRK_INFO_F_FILESYSTEM,
    TIKU_TRK_INFO_F_OPENS_WITH  /* clicking walks the candidates (AW-071) */
} tiku_trk_info_field_t;

/** @brief How far a folder-size walk has got. */
typedef enum {
    TIKU_TRK_INFO_CALC_IDLE = 0,
    TIKU_TRK_INFO_CALC_RUNNING,
    TIKU_TRK_INFO_CALC_DONE
} tiku_trk_info_calc_t;

/** @brief How many directories a size walk can hold waiting to be read. */
#define TIKU_TRK_INFO_CALC_QUEUE 64

/** @brief How many attributes the Attributes tab will list. */
#define TIKU_TRK_INFO_ATTRS 24

/** @brief One listed attribute. */
typedef struct {
    char name[40];
    char value[TIKU_TRK_INFO_VAL_MAX];
    char type[16];              /* the human type name                    */
} tiku_trk_info_attr_t;

/** @brief One info window's state. */
typedef struct {
    int                  used;
    char                 path[TIKU_PATH_MAX];
    tiku_model_t     model;
    /* The size walk: the directories waiting to be read, so a step can
     * return to the caller between any two of them and the walk can be
     * stopped part-way. */
    tiku_trk_info_calc_t calc;
    char                 queue[TIKU_TRK_INFO_CALC_QUEUE][TIKU_PATH_MAX];
    int                  nqueue;
    int64_t              calc_bytes;
    int                  calc_files, calc_dirs;
    int                  calc_overflow;   /* the queue ran out of room    */
    /* When the facts were last re-read, for the TIKU_TRK_INFO_REFRESH_US
     * throttle. */
    int64_t              last_refresh;
    tiku_trk_info_attr_t attr[TIKU_TRK_INFO_ATTRS];
    int                  nattr;
    /* The permissions the grid shows.  A toggle the store refuses leaves
     * them unchanged. */
    unsigned             perm;
    int                  perm_editable;
    /* "Opens with": the resolved handler's name, filled by the shell, which
     * owns the registry (AW-071).  Empty hides the row. */
    char                 opens_with[48];
} tiku_trk_info_t;

/**
 * @brief Fill @p in from @p path, and start a container's size walk.
 *
 * @return 0 when the entry is there.
 * @note The window is one per node: a caller holding several of these finds
 *       an existing one by path before opening another on the same entry
 *       (AW-065).
 */
int tiku_trk_info_open(tiku_trk_info_t *in, tiku_backend_t *b,
                       const char *path);

/** @brief The window's title: the entry's name and "info". */
void tiku_trk_info_title(const tiku_trk_info_t *in, char *out, size_t max);

/** @brief The tab's name, for drawing and for hit-testing. */
const char *tiku_trk_info_tab_name(tiku_trk_info_tab_t tab);

/**
 * @brief One row of the Information tab.
 *
 * The rows shown depend on the entry's kind: a volume reports capacity
 * where a file reports size, a symlink adds where it points, and a measured
 * image adds its dimensions.
 *
 * @param label Receives the field's name.
 * @param value Receives its text, formatted as the columns format it.
 * @return the field, or TIKU_TRK_INFO_F_NONE past the last row.
 */
tiku_trk_info_field_t tiku_trk_info_field(const tiku_trk_info_t *in,
                                          tiku_store_t *store, int i,
                                          const char **label, char *value,
                                          size_t max);

/*---------------------------------------------------------------------------*/
/* The folder-size walk (AW-068)                                             */
/*---------------------------------------------------------------------------*/

/**
 * @brief Begin the folder-size walk from the node's own path; a node that
 *        is not a container is left alone.
 */
void tiku_trk_info_calc_start(tiku_trk_info_t *in);

/**
 * @brief Read one queued directory.
 *
 * @return 1 when there is more to do.
 * @note A caller drives it from an idle tick, one directory a call, so the
 *       window keeps answering the mouse.
 */
int tiku_trk_info_calc_step(tiku_trk_info_t *in, tiku_backend_t *b);

/** @brief Abandon the walk; the size row shows no partial total. */
void tiku_trk_info_calc_stop(tiku_trk_info_t *in);

/*---------------------------------------------------------------------------*/
/* Liveness (AW-069, AW-077)                                                 */
/*---------------------------------------------------------------------------*/

/** @brief How often a volume's free space and a file's size are re-read. */
#define TIKU_TRK_INFO_REFRESH_US 1000000

/**
 * @brief Re-read the facts if it is time to.
 *
 * Volumes, the root and plain files only: their numbers change without a
 * notice, because free space belongs to no node and a file's size is not
 * reported until it is closed.
 *
 * @return 1 when the size, free space or modification time changed.
 */
int tiku_trk_info_refresh(tiku_trk_info_t *in, tiku_backend_t *b,
                          int64_t now);

/**
 * @brief The entry, or a folder above it, moved from @p from to @p to; the
 *        window follows it (AW-077).
 *
 * @return 1 when the window re-pointed.
 */
int tiku_trk_info_moved(tiku_trk_info_t *in, tiku_backend_t *b,
                        const char *from, const char *to);

/**
 * @brief Look for the entry again after it stopped being where it was.
 *
 * By node identity, in the folder it was in: a rename changes only the
 * name, so the entry there with the same identity is the same entry.  A
 * move elsewhere is not found this way and reads as gone.
 *
 * @return 1 when it was found and the window re-pointed.
 */
int tiku_trk_info_refind(tiku_trk_info_t *in, tiku_backend_t *b);

/** @brief Whether removing @p removed took the entry, itself or a folder
 *         above it, so the window must close (AW-077). */
int tiku_trk_info_gone(const tiku_trk_info_t *in, const char *removed);

/*---------------------------------------------------------------------------*/
/* Permissions (AW-078)                                                      */
/*---------------------------------------------------------------------------*/

/** @brief The three columns of the grid. */
#define TIKU_TRK_INFO_PERM_READ  0
#define TIKU_TRK_INFO_PERM_WRITE 1
#define TIKU_TRK_INFO_PERM_EXEC  2

/**
 * @brief Toggle one box and apply it at once.
 *
 * There is no OK button, so no change is ever pending.  A store that
 * refuses says so through @p err, and the box keeps its old state.
 *
 * @return 0 when applied.
 */
int tiku_trk_info_perm_toggle(tiku_trk_info_t *in, tiku_backend_t *b,
                              int which, char *err, size_t errmax);

/** @brief Whether the box for @p which is ticked. */
int tiku_trk_info_perm(const tiku_trk_info_t *in, int which);

/*---------------------------------------------------------------------------*/
/* Attributes (AW-079)                                                       */
/*---------------------------------------------------------------------------*/

/**
 * @brief Fill the attribute list from the store.
 *
 * @return how many rows the tab will show.
 */
int tiku_trk_info_attrs(tiku_trk_info_t *in, tiku_store_t *store);

/*---------------------------------------------------------------------------*/
/* Where the clickable fields lead (AW-072, AW-074)                          */
/*---------------------------------------------------------------------------*/

/**
 * @brief The place to open for a node: its parent in @p dir and its own
 *        name in @p select.
 *
 * @return 1 when there is a parent to open; 0 with both empty for an unused
 *         slot or the root.
 */
int tiku_trk_info_location(const tiku_trk_info_t *in, char *dir, size_t dmax,
                           char *select, size_t smax);

/**
 * @brief The place to open for "Link to": the target's folder in @p dir
 *        and its name in @p select.
 *
 * @return 1 when the target resolves; 0 when the link is broken.
 */
int tiku_trk_info_link_target(const tiku_trk_info_t *in,
                              tiku_backend_t *b, char *dir, size_t dmax,
                              char *select, size_t smax);

/**
 * @brief Point a link at @p target under the same name.
 *
 * The replacement is put in place atomically, and the link's state is not
 * copied: a path-keyed store keeps it under the link's path, and the xattr
 * store keeps state on the file a link resolves to, so the new target
 * shows its own (AW-074).
 *
 * @return 0, or -1 with @p err filled.
 */
int tiku_trk_info_relink(tiku_trk_info_t *in, tiku_backend_t *b,
                         const char *target, char *err, size_t errmax);

#endif /* TIKU_TRK_INFO_H_ */
