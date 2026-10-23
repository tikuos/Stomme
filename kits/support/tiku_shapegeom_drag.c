/*
 * KintsugiOS v0.01 kits -- the foundation KintsugiDesktop and KintsugiTracker.
 *
 * Authors: Ambuj Varshney <ambuj@kinstugi-os.org>
 *
 * tiku_shapegeom_drag.c - the adjust values a dragged handle stands for:
 * each value it moves searched within its range, nearest the point.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_shapegeom_private.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define SAMPLES 64                /* drag search: first, coarse pass    */
#define GOLDEN 0.6180339887498949 /* the golden section          */
#define REFINES                                                                \
    48 /* drag search: golden-section steps,                                   \
          enough to narrow a range of 2^32 to                                  \
          under a whole adjust unit          */

/** @brief A drag search: the shape, the handle, the point it is dragged
 *         to, and the adjust values the search moves, appended after the
 *         shape's own so that they win. */
typedef struct {
    const tiku_shapegeom_def_t *def;
    tiku_shapegeom_adjust_t *adjust; /* the shape's, then the moved */
    size_t count;                    /* all of them                 */
    size_t at[2];                    /* each moved value's entry    */
    float w, h, x, y;
    size_t index;
    tiku_shapegeom_opt_t options;
    tiku_shapegeom_handle_kind_t kind;
} drag_t;

/** @brief How far the handle lands from the point with the moved value
 *         @p k at @p v: along its axis for an xy handle, straight-line
 *         for a polar one.  *@p status takes a failed evaluation. */
static double
miss(drag_t *d, int k, double v, tiku_shapegeom_status_t *status)
{
    tiku_shapegeom_outline_t o;
    double dx, dy;

    d->adjust[d->at[k]].value = v;
    *status = tiku_shapegeom_eval(d->def, d->adjust, d->count, d->w, d->h,
                                  &d->options, &o);
    if (*status != TIKU_SHAPEGEOM_OK) {
        return HUGE_VAL;
    }
    dx = (double)o.handles[d->index].x - d->x;
    dy = (double)o.handles[d->index].y - d->y;
    tiku_shapegeom_outline_free(&o);
    if (d->kind == TIKU_SHAPEGEOM_HANDLE_XY) {
        return fabs(k == 0 ? dx : dy);
    }
    return sqrt(dx * dx + dy * dy);
}

/**
 * @brief The value of moved value @p k within [@p lo, @p hi] nearest the
 *        point: the range sampled, then the best sample's neighbourhood
 *        narrowed by golden sections; rounded to a whole number inside
 *        the range when the range holds one.
 */
static tiku_shapegeom_status_t
search(drag_t *d, int k, double lo, double hi, double *best)
{
    tiku_shapegeom_status_t st = TIKU_SHAPEGEOM_OK;
    double bv = *best, bm = miss(d, k, bv, &st), a, b, c, e, fc, fe;
    int i;

    for (i = 0; i < SAMPLES && st == TIKU_SHAPEGEOM_OK; i++) {
        double v = lo + (hi - lo) * i / (SAMPLES - 1), m = miss(d, k, v, &st);

        if (m < bm) {
            bm = m;
            bv = v;
        }
    }
    if (st != TIKU_SHAPEGEOM_OK) {
        return st; /* a sample's failure stands */
    }
    a = fmax(lo, bv - (hi - lo) / (SAMPLES - 1));
    b = fmin(hi, bv + (hi - lo) / (SAMPLES - 1));
    c = b - (b - a) * GOLDEN;
    e = a + (b - a) * GOLDEN;
    fc = miss(d, k, c, &st);
    fe = miss(d, k, e, &st);
    for (i = 0; i < REFINES && st == TIKU_SHAPEGEOM_OK; i++) {
        if (fc < fe) { /* the minimum is left of e */
            b = e;
            e = c;
            fe = fc;
            c = b - (b - a) * GOLDEN;
            fc = miss(d, k, c, &st);
        } else {
            a = c;
            c = e;
            fc = fe;
            e = a + (b - a) * GOLDEN;
            fe = miss(d, k, e, &st);
        }
    }
    if (st == TIKU_SHAPEGEOM_OK && fmin(fc, fe) < bm) {
        bv = fc < fe ? c : e;
    }
    if (ceil(lo) <= floor(hi)) { /* a whole number in range: round */
        bv = floor(bv + 0.5);
        bv = bv < ceil(lo) ? ceil(lo) : bv > floor(hi) ? floor(hi) : bv;
    }
    *best = bv;
    d->adjust[d->at[k]].value = bv;
    return st;
}

/** @brief The drag search over a handle evaluated as @p hd: each value
 *         it moves, twice round, the other held. */
static tiku_shapegeom_status_t
drag_run(drag_t *d, const tiku_shapegeom_handle_t *hd, double values[2])
{
    tiku_shapegeom_status_t st = TIKU_SHAPEGEOM_OK;
    int round, k;

    for (k = 0; k < 2; k++) {
        values[k] = hd->value[k];
        if (hd->ref[k][0]) {
            d->at[k] = d->count++;
            d->adjust[d->at[k]].name = hd->ref[k];
            d->adjust[d->at[k]].value = hd->value[k];
        }
    }
    d->kind = hd->kind;
    /* a handle that moves two values goes round twice, as each search
     * holds the other value where the last one left it */
    for (round = 0; round < (hd->ref[0][0] && hd->ref[1][0] ? 2 : 1) &&
                    st == TIKU_SHAPEGEOM_OK;
         round++) {
        for (k = 0; k < 2 && st == TIKU_SHAPEGEOM_OK; k++) {
            if (hd->ref[k][0]) {
                st = search(d, k, hd->min[k], hd->max[k], &values[k]);
            }
        }
    }
    return st;
}

tiku_shapegeom_status_t
tiku_shapegeom_drag(const tiku_shapegeom_def_t *def,
                    const tiku_shapegeom_adjust_t *adjust, size_t adjust_count,
                    float width, float height, size_t index, float x, float y,
                    const tiku_shapegeom_opt_t *options, double values[2])
{
    tiku_shapegeom_outline_t o;
    tiku_shapegeom_status_t st;
    drag_t d;

    if (values == NULL || !isfinite(x) || !isfinite(y)) {
        return TIKU_SHAPEGEOM_INVALID;
    }
    memset(&d, 0, sizeof d);
    d.options.flags =
        (options ? options->flags : 0) | TIKU_SHAPEGEOM_GEOM_NO_PATHS;
    st = tiku_shapegeom_eval(def, adjust, adjust_count, width, height,
                             &d.options, &o);
    if (st != TIKU_SHAPEGEOM_OK) {
        return st;
    }
    if (index >= o.handle_count) {
        tiku_shapegeom_outline_free(&o);
        return TIKU_SHAPEGEOM_INVALID;
    }
    d.adjust = calloc(adjust_count + 2, sizeof *d.adjust);
    if (d.adjust == NULL) {
        tiku_shapegeom_outline_free(&o);
        return TIKU_SHAPEGEOM_NO_MEMORY;
    }
    if (adjust_count > 0) {
        memcpy(d.adjust, adjust, adjust_count * sizeof *adjust);
    }
    d.def = def;
    d.count = adjust_count;
    d.w = width;
    d.h = height;
    d.x = x;
    d.y = y;
    d.index = index;
    st = drag_run(&d, &o.handles[index], values);
    tiku_shapegeom_outline_free(&o);
    free(d.adjust);
    return st;
}
