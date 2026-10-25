/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_repaint.c - recording a view's damage and merging it.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_trk_repaint.h"
#include <string.h>

/** @brief The larger of two ints. */
static int max_i(int a, int b) { return a > b ? a : b; }
/** @brief The smaller of two ints. */
static int min_i(int a, int b) { return a < b ? a : b; }

/** @brief The smallest rectangle holding both. */
static tiku_trk_repaint_rect_t
union_rect(tiku_trk_repaint_rect_t a, tiku_trk_repaint_rect_t b)
{
    int ax = min_i(a.x, b.x), ay = min_i(a.y, b.y);
    int ar = max_i(a.x + a.w, b.x + b.w);
    int ab = max_i(a.y + a.h, b.y + b.h);
    return (tiku_trk_repaint_rect_t){ ax, ay, ar - ax, ab - ay };
}

void
tiku_trk_repaint_init(tiku_trk_repaint_t *r)
{
    if (r != NULL) memset(r, 0, sizeof *r);
}

void
tiku_trk_repaint_add(tiku_trk_repaint_t *r, tiku_trk_repaint_rect_t rect)
{
    int i;
    if (r == NULL || rect.w <= 0 || rect.h <= 0) return;
    for (i = 0; i < r->count; i++) {
        tiku_trk_repaint_rect_t old = r->rect[i];
        int touch = !(old.x + old.w < rect.x || rect.x + rect.w < old.x ||
                       old.y + old.h < rect.y || rect.y + rect.h < old.y);
        if (touch) {
            r->rect[i] = union_rect(old, rect);
            return;
        }
    }
    if (r->count < TIKU_TRK_REPAINT_MAX) r->rect[r->count++] = rect;
    else r->rect[0] = union_rect(r->rect[0], rect);
}

void
tiku_trk_repaint_insert(tiku_trk_repaint_t *r, tiku_trk_repaint_rect_t visible,
                        int row_h, int row_y)
{
    if (r == NULL || row_h <= 0) {
        return;
    }
    if (row_y < visible.y) {
        r->scroll_delta += row_h;      /* above the view: it scrolls      */
        return;
    }
    if (row_y >= visible.y + visible.h) {
        return;                        /* below it: nothing on screen     */
    }
    /* The rows under it only change place, so they slide, and the row
     * that appeared is the only one painted. */
    tiku_trk_repaint_slide(r, row_y, row_h, visible);
    tiku_trk_repaint_add(r, (tiku_trk_repaint_rect_t){ visible.x, row_y,
                                                       visible.w, row_h });
}

void
tiku_trk_repaint_remove(tiku_trk_repaint_t *r, tiku_trk_repaint_rect_t visible,
                        int row_h, int row_y)
{
    if (r == NULL || row_h <= 0) {
        return;
    }
    if (row_y < visible.y) {
        r->scroll_delta -= row_h;
        return;
    }
    if (row_y >= visible.y + visible.h) {
        return;
    }
    tiku_trk_repaint_slide(r, row_y + row_h, -row_h, visible);
    /* Moving the rows up leaves the bottom row's worth of pixels holding
     * what was there before the move. */
    tiku_trk_repaint_add(r, (tiku_trk_repaint_rect_t){
        visible.x, visible.y + visible.h - row_h, visible.w, row_h });
}

/** @brief Paint the strip from @p y0 down to @p y1, if it has any height. */
static void
add_strip(tiku_trk_repaint_t *r, int x, int y0, int w, int y1)
{
    if (y1 > y0) {
        tiku_trk_repaint_add(r, (tiku_trk_repaint_rect_t){ x, y0, w,
                                                           y1 - y0 });
    }
}

void
tiku_trk_repaint_band(tiku_trk_repaint_t *r, tiku_trk_repaint_rect_t old_rect,
                      tiku_trk_repaint_rect_t new_rect)
{
    tiku_trk_repaint_rect_t all;
    int old_b, new_b;

    if (r == NULL) {
        return;
    }
    /* What changed: the strips one rectangle has and the other does not. */
    all = union_rect(old_rect, new_rect);
    old_b = old_rect.y + old_rect.h;
    new_b = new_rect.y + new_rect.h;
    if (old_rect.y < new_rect.y) {
        add_strip(r, all.x, old_rect.y, all.w, new_rect.y);
    } else {
        add_strip(r, all.x, new_rect.y, all.w, old_rect.y);
    }
    if (old_b < new_b) {
        add_strip(r, all.x, old_b, all.w, new_b);
    } else {
        add_strip(r, all.x, new_b, all.w, old_b);
    }
    if (old_rect.x != new_rect.x ||
        old_rect.w != new_rect.w) {
        /* Sideways growth changes the whole overlapping height, and a
         * band that only widened has no top or bottom strip to report. */
        int y0 = (old_rect.y > new_rect.y) ? old_rect.y : new_rect.y;
        int y1 = (old_b < new_b) ? old_b : new_b;

        add_strip(r, all.x, y0, all.w, y1);
    }
}

void
tiku_trk_repaint_slide(tiku_trk_repaint_t *r, int y, int delta,
                       tiku_trk_repaint_rect_t visible)
{
    int i;

    if (r == NULL || delta == 0) {
        return;
    }
    if (r->slide_delta != 0) {
        /* One blit to a frame: the rectangles already recorded cannot
         * follow a second slide, so the batch paints the whole view. */
        r->slide_delta = 0;
        r->slide_y = 0;
        tiku_trk_repaint_add(r, visible);
        return;
    }
    /* A rectangle recorded before the slide names pixels the blit is about
     * to move, and the place that needs painting moves with them. */
    for (i = 0; i < r->count; i++) {
        if (r->rect[i].y >= y) {
            r->rect[i].y += delta;
        }
    }
    r->slide_y = y;
    r->slide_delta = delta;
}

int
tiku_trk_repaint_take_slide(tiku_trk_repaint_t *r, int *y)
{
    int delta;

    if (r == NULL || r->slide_delta == 0) {
        return 0;
    }
    delta = r->slide_delta;
    if (y != NULL) {
        *y = r->slide_y;
    }
    r->slide_delta = 0;
    r->slide_y = 0;
    return delta;
}

int tiku_trk_repaint_take(tiku_trk_repaint_t *r, tiku_trk_repaint_rect_t *out,
                          int max)
{
    int n, i;
    if (r == NULL || out == NULL || max <= 0) return 0;
    n = r->count < max ? r->count : max;
    for (i = 0; i < n; i++) out[i] = r->rect[i];
    if (n < r->count) memmove(r->rect, r->rect + n,
                              (size_t)(r->count - n) * sizeof r->rect[0]);
    r->count -= n;
    return n;
}
