/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_circuit_geometry.c - circular arcs kept exact in a scene, as a
 * centre, a radius and two angles; the viewport tessellates them at its
 * own tolerance when it draws them.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_circuit_private.h"

#include <math.h>

/* Radians by which a sweep may miss a whole turn and still be one. */
#define TURN_SLACK 1e-9

tiku_circuit_err_t
circuit_arc(tiku_circuit_t *d, const char *name, const char *value,
            double x, double y, double r, double start, double sweep,
            double width, uint32_t colour, int fill)
{
    if (!isfinite(x) || !isfinite(y) || !isfinite(r) || r <= 0 ||
        r > CIRCUIT_COORD_MAX || !isfinite(start) || !isfinite(sweep) ||
        fabs(sweep) > 2 * M_PI + TURN_SLACK ||
        fabs(x) + r > CIRCUIT_COORD_MAX || fabs(y) + r > CIRCUIT_COORD_MAX) {
        return TIKU_CIRCUIT_BAD;
    }
    if (fill && fabs(fabs(sweep) - 2 * M_PI) > TURN_SLACK) {
        return TIKU_CIRCUIT_UNSUPPORTED;
    }
    circuit_point_t corners[] = {{x - r, y - r}, {x + r, y + r}};
    tiku_circuit_err_t e = circuit_add(d, name, value, corners, 2, width,
                                       colour, 0, fill);

    if (e) {
        return e;
    }
    circuit_shape_t *s = &d->shapes[d->count - 1];

    s->kind = CIRCUIT_ARC;
    s->cx = x;
    s->cy = y;
    s->r = r;
    s->start = fmod(start, 2 * M_PI);
    s->sweep = sweep;
    s->closed = fabs(fabs(sweep) - 2 * M_PI) < TURN_SLACK;
    return TIKU_CIRCUIT_OK;
}
