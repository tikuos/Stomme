/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_circuit_clone.c - shapes copied and grouped: a step-and-repeat
 * copy of an object, and an aperture macro's flash as one object over a
 * retained, flat scene of its own coverage.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_circuit_private.h"

#include <math.h>
#include <stdlib.h>

tiku_circuit_err_t
circuit_group(tiku_circuit_t *d, const char *name, const char *value,
              tiku_circuit_t *child, double x, double y)
{
    if (!d || !child || d == child || child->font || child->layers ||
        !isfinite(x) || !isfinite(y)) {
        return TIKU_CIRCUIT_BAD;
    }
    /* A group's scene holds no groups, so drawing one recurses no further
     * and no cycle can form. */
    for (size_t i = 0; i < child->count; i++) {
        if (child->shapes[i].group) {
            return TIKU_CIRCUIT_BAD;
        }
    }
    if (child->bytes > CIRCUIT_BYTES_MAX - d->bytes ||
        child->points > CIRCUIT_POINTS_MAX - d->points) {
        return TIKU_CIRCUIT_LIMIT;
    }
    tiku_circuit_box_t b = child->bounds;
    circuit_point_t p[] = {{b.x0 + x, b.y0 + y}, {b.x1 + x, b.y1 + y}};
    d->bytes += child->bytes;
    d->points += child->points;
    tiku_circuit_err_t e = circuit_add(d, name, value, p, 2, 0,
                                       CIRCUIT_COPPER, 1, 1);

    if (e) {
        d->bytes -= child->bytes;
        d->points -= child->points;
        return e;
    }
    circuit_shape_t *s = &d->shapes[d->count - 1];

    s->kind = CIRCUIT_GROUP;
    s->group = tiku_circuit_retain(child);
    s->cx = x;
    s->cy = y;
    return TIKU_CIRCUIT_OK;
}

tiku_circuit_err_t
circuit_clone(tiku_circuit_t *d, const circuit_shape_t *source, double x,
              double y)
{
    if (!d || !source || source->kind == CIRCUIT_TEXT) {
        return TIKU_CIRCUIT_BAD;
    }
    /* The source may belong to d, whose shapes can move while one is
     * appended: work from a copy. */
    circuit_shape_t s = *source;

    if (s.group) {
        tiku_circuit_err_t e = circuit_group(d, s.info.name, s.info.value,
                                             s.group, s.cx + x, s.cy + y);

        if (e) {
            return e;
        }
    } else {
        if (s.count > CIRCUIT_POINTS_MAX) {
            return TIKU_CIRCUIT_LIMIT;
        }
        circuit_point_t *p = malloc(s.count * sizeof *p);

        if (!p) {
            return TIKU_CIRCUIT_NOMEM;
        }
        for (size_t i = 0; i < s.count; i++) {
            p[i] = (circuit_point_t){s.points[i].x + x, s.points[i].y + y};
        }
        tiku_circuit_err_t e = circuit_add(d, s.info.name, s.info.value, p,
                                           s.count, s.width, s.colour,
                                           s.closed, s.fill);

        free(p);
        if (e) {
            return e;
        }
        circuit_shape_t *to = &d->shapes[d->count - 1];

        to->kind = s.kind;
        to->cx = s.cx + x;
        to->cy = s.cy + y;
        to->r = s.r;
        to->end_r = s.end_r;
        to->start = s.start;
        to->sweep = s.sweep;
        to->hole = s.hole;
        to->info.bounds = (tiku_circuit_box_t){s.info.bounds.x0 + x,
                                               s.info.bounds.y0 + y,
                                               s.info.bounds.x1 + x,
                                               s.info.bounds.y1 + y};
        if (fabs(to->info.bounds.x0) > CIRCUIT_COORD_MAX ||
            fabs(to->info.bounds.y0) > CIRCUIT_COORD_MAX ||
            fabs(to->info.bounds.x1) > CIRCUIT_COORD_MAX ||
            fabs(to->info.bounds.y1) > CIRCUIT_COORD_MAX) {
            return TIKU_CIRCUIT_LIMIT;
        }
        circuit_bounds_add(d, to->info.bounds);
    }
    d->shapes[d->count - 1].clear = s.clear;
    return s.details ? circuit_details(d, d->count - 1, s.details)
                     : TIKU_CIRCUIT_OK;
}
