/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_repaint.h - the damage a view owes: rectangles to paint, a
 * scroll, and at most one slide per batch.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_REPAINT_H_
#define TIKU_TRK_REPAINT_H_

#include <stdint.h>

/** @brief A rectangle to paint, in the view's own coordinates. */
typedef struct {
    int32_t x, y, w, h;
} tiku_trk_repaint_rect_t;

/** @brief How many rectangles a batch records; more merge into the first. */
#define TIKU_TRK_REPAINT_MAX 32

/** @brief One batch of damage. */
typedef struct {
    tiku_trk_repaint_rect_t rect[TIKU_TRK_REPAINT_MAX];
    int count;
    int scroll_delta;   /* rows inserted or removed above the view, in px */
    /* Pixels that only moved: everything from slide_y down moves by
     * slide_delta, and the caller blits it. */
    int slide_y;
    int slide_delta;
} tiku_trk_repaint_t;

/** @brief Nothing to paint. */
void tiku_trk_repaint_init(tiku_trk_repaint_t *r);
/**
 * @brief Paint @p rect: merged into a recorded rectangle it touches, else
 * recorded on its own; past the table's end it folds into the first.
 */
void tiku_trk_repaint_add(tiku_trk_repaint_t *r, tiku_trk_repaint_rect_t rect);
/**
 * @brief A row appeared at @p row_y, in the view's own coordinates.
 *
 * Above the view that is a scroll; inside it, a slide plus the one new row.
 */
void tiku_trk_repaint_insert(tiku_trk_repaint_t *r, tiku_trk_repaint_rect_t visible,
                             int row_h, int row_y);
/**
 * @brief A row of @p row_h at @p row_y went away.
 *
 * Above the view that is a scroll; inside it, the rows below slide up and
 * the bottom row's worth of pixels is painted.
 */
void tiku_trk_repaint_remove(tiku_trk_repaint_t *r, tiku_trk_repaint_rect_t visible,
                             int row_h, int row_y);
/**
 * @brief The band grew or shrank from @p old_rect to @p new_rect.
 *
 * Only the strips one has and the other has not are painted; a change of x
 * or width adds the overlapping height as well.
 */
void tiku_trk_repaint_band(tiku_trk_repaint_t *r, tiku_trk_repaint_rect_t old_rect,
                           tiku_trk_repaint_rect_t new_rect);
/** @brief Take up to @p max rectangles out, oldest first.  @return how many. */
int tiku_trk_repaint_take(tiku_trk_repaint_t *r, tiku_trk_repaint_rect_t *out,
                          int max);

/**
 * @brief Everything from @p y down has moved by @p delta pixels.
 *
 * A row inserted or removed inside the view leaves the rows below it
 * unchanged in every way but where they sit, so they are blitted.  Only one
 * slide fits in a batch: a second one gives up and repaints @p visible.
 */
void tiku_trk_repaint_slide(tiku_trk_repaint_t *r, int y, int delta,
                            tiku_trk_repaint_rect_t visible);

/** @brief Take the pending slide: @return its delta, with its top in @p y,
 *         or 0 when there is none. */
int tiku_trk_repaint_take_slide(tiku_trk_repaint_t *r, int *y);

#endif
