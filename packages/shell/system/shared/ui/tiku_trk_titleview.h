/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_titleview.h - the column title strip, and what a drag on it means.
 *
 * One tracking state serves every column gesture: a press picks a resize or
 * a drag, and the moves and the release decide whether it was a click (a
 * sort), a reorder, a resize or a removal.  The decisions are thresholds: a
 * click deadline taken at the press, a one-pixel dead zone, a ten-pixel
 * vertical hysteresis for removal, and a swap condition that keeps two
 * columns from trading places on every move.  Nothing here draws; the
 * TIKU_TRK_TT_* flags tell the caller what changed.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_TITLEVIEW_H_
#define TIKU_TRK_TITLEVIEW_H_

#include "tiku_trk_poseview.h"
#include <stdint.h>

/** @brief Half-width of the band around a column's right edge. */
#define TIKU_TRK_EDGE_SIZE 6

/** @brief How far out of the strip a drag must go to tear a column out. */
#define TIKU_TRK_REMOVE_MARGIN 10

/** @brief What the tracker is doing. */
typedef enum {
    TIKU_TRK_TRACK_NONE = 0,
    TIKU_TRK_TRACK_DRAG,
    TIKU_TRK_TRACK_RESIZE
} tiku_trk_track_t;

/** @brief What the caller has to do about the last event. */
#define TIKU_TRK_TT_NOTHING   0x00u
#define TIKU_TRK_TT_REDRAW    0x01u   /* the strip or the columns changed */
#define TIKU_TRK_TT_ACTIVATE  0x02u   /* the click only woke the window   */
#define TIKU_TRK_TT_MENU      0x04u   /* the attributes menu was asked for */
#define TIKU_TRK_TT_SORTED    0x08u   /* the sort keys moved              */
#define TIKU_TRK_TT_LINE      0x10u   /* the guide line is on, at line_x  */
#define TIKU_TRK_TT_NO_LINE   0x20u   /* and now it is off                */
#define TIKU_TRK_TT_CURSOR    0x40u   /* the pointer is over a resize edge */

/** @brief The strip's tracking state. */
typedef struct tiku_trk_titleview {
    tiku_trk_poseview_t *view;        /* whose columns these are         */
    int              height;          /* the strip's height              */
    tiku_trk_track_t track;           /* what the press is doing: none,  */
                                      /* a resize or a drag              */
    int              field;           /* the column being tracked        */
    int              first_x, first_y;    /* where the press landed      */
    int64_t          past_click;      /* the absolute click deadline     */
    int              has_moved;       /* the pointer left the press point, */
                                      /* so a release is not a click     */
    int              grab;            /* resize: right edge minus mouse  */
    int              drag_base;       /* drag: the raw x at press        */
    int              last_x;          /* where the pointer is now        */
    int              line_x;          /* guide line position, or -1      */
    /* A column dragged out of the strip is removed from the view, and kept
     * here so that a drag back in restores it. */
    int              removed;         /* a column is out of the strip    */
    int              removed_field, removed_width, removed_align;
    /* The previous divider click, for the double-click auto-fit. */
    int              prev_field;      /* the divider it landed on, or -1 */
    int64_t          prev_click_at;   /* when, in microseconds; 0 never  */
} tiku_trk_titleview_t;

/** @brief A strip over @p v, @p height tall, tracking nothing yet. */
void tiku_trk_titleview_init(tiku_trk_titleview_t *t,
                             tiku_trk_poseview_t *v, int height);

/**
 * @brief The title rectangle of @p field, in content coordinates.
 *
 * Wider than the column: it starts TIKU_TRK_COL_LEFT_MARGIN before the data
 * (the gap the sort mark lives in) and ends TIKU_TRK_COL_RIGHT_MARGIN past
 * it.  Hit tests use these bounds, not the column's width.
 *
 * @return 1 when @p field has a column.
 */
int tiku_trk_titleview_bounds(const tiku_trk_titleview_t *t, int field,
                              int *left, int *right);

/** @brief The first title containing @p x, or -1. */
int tiku_trk_titleview_title_at(const tiku_trk_titleview_t *t, int x);

/**
 * @brief The first title whose resize edge contains @p x, or -1.
 *
 * The band reaches TIKU_TRK_EDGE_SIZE either side of a title's right edge,
 * into the next title: a press just inside a column's left edge resizes the
 * column before it.
 */
int tiku_trk_titleview_edge_at(const tiku_trk_titleview_t *t, int x);

/**
 * @brief A press on the strip.
 *
 * @param buttons 1 primary, 2 secondary, 3 tertiary.
 * @param active  Whether the window is active; an inactive one only wakes.
 * @return TIKU_TRK_TT_* flags.
 */
unsigned tiku_trk_titleview_mouse_down(tiku_trk_titleview_t *t, int x, int y,
                                       int buttons, int active, int64_t now);

/**
 * @brief Motion, tracking or not.  @return TIKU_TRK_TT_* flags; with nothing
 *        tracked, TIKU_TRK_TT_CURSOR over a resize edge.
 */
unsigned tiku_trk_titleview_mouse_moved(tiku_trk_titleview_t *t, int x, int y,
                                        int64_t now);

/**
 * @brief Release: the click-or-drag decision, and what it means.
 *
 * @param shift Whether Shift is held at the release, which is when the sort
 *              reads it.
 */
unsigned tiku_trk_titleview_mouse_up(tiku_trk_titleview_t *t, int x, int y,
                                     int shift, int64_t now);

/** @brief Whether a gesture is in progress, and which. */
tiku_trk_track_t tiku_trk_titleview_tracking(const tiku_trk_titleview_t *t);

/**
 * @brief The column being dragged, for the pressed and outlined look.
 *
 * @param offset Receives how far right of its own place the outline sits.
 */
int tiku_trk_titleview_dragged(const tiku_trk_titleview_t *t, int *offset);

#endif /* TIKU_TRK_TITLEVIEW_H_ */
