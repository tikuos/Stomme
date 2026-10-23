/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_drag.h - the drag gesture: arming, payload and drop validity.
 *
 * The part between the press and the drop: when a press becomes a drag,
 * what the drag is carrying, and whether the thing under the pointer will
 * take it.  Where the bytes then go is tiku_fs's business.  It also tells a
 * double-click from two clicks.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_DRAG_H_
#define TIKU_TRK_DRAG_H_

#include "tiku_model.h"
#include <stddef.h>
#include <stdint.h>

/** @brief The two presses of a double-click are closer than this on each
 *         axis, in pixels. */
#define TIKU_TRK_DOUBLE_CLICK_SLOP 6

/** @brief The double-click interval used by both Tracker shells. */
#define TIKU_TRK_DOUBLE_CLICK_US 400000

/** @brief Flag: the click extends the selection, so it cannot also open. */
#define TIKU_TRK_CLICK_EXTEND  0x1u

/** @brief Flag: Control+primary, a secondary click that never opens. */
#define TIKU_TRK_CLICK_CONTROL 0x2u

/** @brief The first half of a possible double-click. */
typedef struct {
    const void *pose;               /* the pose itself, by address         */
    int         x, y;
    unsigned    button;
    int64_t     at;
} tiku_trk_click_t;

/**
 * @brief Record one pose press and say whether it completes a double-click.
 *
 * Only an unmodified primary click is eligible.  The pose and the button
 * must match, the point must be within TIKU_TRK_DOUBLE_CLICK_SLOP and the
 * time within TIKU_TRK_DOUBLE_CLICK_US.  A completed pair is forgotten, so
 * the third press of a triple-click cannot open the pose again.
 *
 * @param flags TIKU_TRK_CLICK_EXTEND and TIKU_TRK_CLICK_CONTROL.
 * @return 1 when the press completes a double-click.
 */
int tiku_trk_click_press(tiku_trk_click_t *click, const void *pose,
                         int x, int y, unsigned button, unsigned flags,
                         int64_t now);

/** @brief How many items one drag can carry. */
#define TIKU_TRK_DRAG_MAX 64

/**
 * @brief How far the pointer must move before a press becomes a drag.
 *
 * Tracker's radiusThreshold, in pixels, compared squared so the test needs
 * no square root.  A press that moves less stays a click.
 */
#define TIKU_TRK_DRAG_SLOP 4

/**
 * @brief The modifier bits a drop reads.
 *
 * Read at the drop, not at the press, so Option-dropping into the same
 * folder duplicates and Option-pressing does nothing.
 */
#define TIKU_TRK_DRAG_MOD_COPY 0x1u    /* Option: copy even within a volume */
#define TIKU_TRK_DRAG_MOD_LINK 0x2u    /* Control: make a link instead      */

/** @brief What a drop should do. */
typedef enum {
    TIKU_TRK_DROP_NOTHING = 0,
    TIKU_TRK_DROP_MOVE,
    TIKU_TRK_DROP_COPY,
    TIKU_TRK_DROP_LINK
} tiku_trk_drop_t;

/** @brief How many distinct types one drag's cache holds. */
#define TIKU_TRK_DRAG_TYPES 16

/** @brief How far apart stacked icons land. */
#define TIKU_TRK_DRAG_STACK 20
/** @brief The grid a list drag of foreign references lands in: columns, and
 *         each cell's width and height. */
#define TIKU_TRK_DRAG_FOREIGN_COLS 4
#define TIKU_TRK_DRAG_FOREIGN_CELL_W 96
#define TIKU_TRK_DRAG_FOREIGN_CELL_H 24
/** @brief The largest typed payload a drag carries, in bytes. */
#define TIKU_TRK_DRAG_PAYLOAD_MAX 4096

/** @brief One drag in flight. */
typedef struct {
    int      armed;                 /* a press armed it; stays set once
                                       active                            */
    int      active;                /* past the slop: a real drag        */
    int      x0, y0;                /* where the press was               */
    int      x, y;                  /* where the pointer is now          */
    unsigned buttons;               /* what was held at the press        */
    unsigned mods;                  /* modifiers at the press            */
    int      count;
    char     path[TIKU_TRK_DRAG_MAX][TIKU_PATH_MAX];
    /* What a link made of the item is called, when it is not the item's
     * own leaf: an application dragged from a menu is named as the menu
     * named it.  Empty for the leaf. */
    char     name[TIKU_TRK_DRAG_MAX][TIKU_NAME_MAX];
    char     src_dir[TIKU_PATH_MAX];
    int      src_virtual;          /* source is a merged/namespace view */
    void    *src_win;               /* opaque owner token                */
    /* Where each item sat when the drag began, so a drop from an icon view
     * keeps their arrangement (PVS-045). */
    int      ox[TIKU_TRK_DRAG_MAX], oy[TIKU_TRK_DRAG_MAX];
    int      from_list;             /* the source view was a list        */
    int      foreign_refs;          /* foreign refs or a typed payload   */
    char     payload_type[TIKU_TYPE_MAX];
    unsigned char payload[TIKU_TRK_DRAG_PAYLOAD_MAX];
    size_t   payload_size;
    /* The distinct types the drag carries, gathered as items are added and
     * sealed by the first tiku_trk_drag_types() call, so re-entering a view
     * with the same drag does not rebuild them (PVS-053). */
    char     type[TIKU_TRK_DRAG_TYPES][TIKU_TYPE_MAX];
    int      ntype;
    int      types_built;
} tiku_trk_drag_t;

/** @brief Clear @p d: no drag in flight. */
void tiku_trk_drag_reset(tiku_trk_drag_t *d);

/**
 * @brief Forget any earlier drag, then arm on a press over a pose.
 *
 * Armed, not started: the drag begins only if the pointer then moves past
 * TIKU_TRK_DRAG_SLOP, so a click is not a one-pixel drag (PVS-032).
 *
 * @param clicks The press's click count.  Only a single click arms, so the
 *               second press of a double-click does not.
 */
void tiku_trk_drag_arm(tiku_trk_drag_t *d, int x, int y, unsigned buttons,
                       unsigned mods, int clicks, void *src_win,
                       const char *src_dir);

/** @brief Mark a drag that originated in a virtual directory. */
void tiku_trk_drag_set_source_virtual(tiku_trk_drag_t *d, int virtual_);
/** @brief Mark a payload as foreign refs (FS-077/PVS-045). */
void tiku_trk_drag_set_foreign_refs(tiku_trk_drag_t *d, int foreign);

/**
 * @brief Attach a small typed non-file payload to an in-process drag, and
 *        mark it as foreign references.
 *
 * @return 0, or -1 when it is larger than TIKU_TRK_DRAG_PAYLOAD_MAX.
 */
int tiku_trk_drag_set_payload(tiku_trk_drag_t *d, const char *type,
                              const void *data, size_t size);

/**
 * @brief Add one item to what the drag carries (PVS-033).
 *
 * @return 0, or -1 when the drag is full or @p path is empty.
 */
int tiku_trk_drag_add(tiku_trk_drag_t *d, const char *path);

/**
 * @brief Add one item, remembering where it sat and what type it is.
 *
 * @param from_list Non-zero when the source view was a list, which has no
 *                  positions to preserve.
 * @return 0, or -1 as tiku_trk_drag_add() does.
 */
int tiku_trk_drag_add_at(tiku_trk_drag_t *d, const char *path,
                         const char *type, int x, int y, int from_list);

/** @brief Name what a link of item @p i is called; NULL or "" for its leaf. */
void tiku_trk_drag_name_at(tiku_trk_drag_t *d, int i, const char *name);

/**
 * @brief Where item @p i should land for a drop at @p dx, @p dy.
 *
 * From an icon view the arrangement is preserved: the whole selection moves
 * by one delta and keeps its shape.  From a list there are no positions to
 * preserve, so the items stack in a column at the drop point, or fill a
 * TIKU_TRK_DRAG_FOREIGN_COLS grid when the drag carries foreign references
 * (PVL-027, PVS-045).
 */
void tiku_trk_drag_landing(const tiku_trk_drag_t *d, int i, int dx, int dy,
                           int *x, int *y);

/**
 * @brief The distinct types this drag carries, built once (PVS-053).
 *
 * @return how many.
 */
int tiku_trk_drag_types(tiku_trk_drag_t *d);

/**
 * @brief Report pointer movement.
 *
 * An armed drag starts once the pointer is TIKU_TRK_DRAG_SLOP from the
 * press and the drag carries at least one item.
 *
 * @return 1 on the move that starts the drag, 0 otherwise.
 */
int tiku_trk_drag_moved(tiku_trk_drag_t *d, int x, int y);

/** @brief Whether a real drag is in flight. */
int tiku_trk_drag_active(const tiku_trk_drag_t *d);

/** @brief Whether @p path is one of the items being dragged (PVS-042). */
int tiku_trk_drag_carries(const tiku_trk_drag_t *d, const char *path);

/**
 * @brief Whether @p over can be a drop target for this drag (MA-016).
 *
 * A container that accepts new entries takes items, and an executable file
 * takes documents to open; nothing else is a target.  A pose that is itself
 * being dragged is never one, so a drop onto the selection behaves as a
 * drop on the window behind it.
 */
int tiku_trk_drag_can_drop(const tiku_trk_drag_t *d,
                           const tiku_model_t *over);

/**
 * @brief What a drop on @p dst_dir means.
 *
 * The same-volume rule as a paste, plus the modifiers: Option forces a
 * copy even within one volume, and Control asks for a link.  A drag from a
 * virtual folder links unless Option is held (FS-078).  A drop back into
 * the folder the items came from does nothing unless Option or Control is
 * held.
 */
tiku_trk_drop_t tiku_trk_drag_action(const tiku_trk_drag_t *d,
                                     const char *dst_dir, unsigned mods,
                                     int same_volume);

#endif /* TIKU_TRK_DRAG_H_ */
