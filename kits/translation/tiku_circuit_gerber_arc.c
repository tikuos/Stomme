/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_circuit_gerber_arc.c - G02/G03 arcs: checked against the source's
 * rounding, kept analytic for tracks and tessellated inside regions.
 * Rounded coordinates can give slightly different start and end radii.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_circuit_gerber_private.h"

#include <math.h>

/* An arc's start and end radii may differ by two source coordinate
 * quanta, up to this many mm. */
#define ARC_ROUNDING_MAX .01

int
gerber_arc(circuit_gerber_t *g, double x, double y, double i, double j,
           double arc[6])
{
    if (!g->quadrants || (g->mode != 2 && g->mode != 3)) {
        return gerber_fail(g, TIKU_CIRCUIT_BAD,
                           "Circular interpolation requires G75 and "
                           "G02/G03");
    }
    double cx = g->x + i, cy = g->y + j, r = hypot(i, j);
    double end = hypot(x - cx, y - cy);
    double tolerance = fmax(1e-8 * fmax(1, r),
                            fmin(ARC_ROUNDING_MAX,
                                 2 * g->unit / pow(10, g->decimals)));

    if (!r || !end || fabs(r - end) > tolerance) {
        return gerber_fail(g, TIKU_CIRCUIT_UNSUPPORTED,
                           "Arc radius mismatch exceeds supported "
                           "coordinate-rounding tolerance");
    }
    double start = atan2(g->y - cy, g->x - cx), finish = atan2(y - cy, x - cx);
    double sweep = g->mode == 2 ? start - finish : finish - start;

    sweep = fmod(sweep + 2 * M_PI, 2 * M_PI);
    if (!sweep) {
        if (x != g->x || y != g->y) {
            return gerber_fail(g, TIKU_CIRCUIT_BAD,
                               "Radial-only arc is not a full circle");
        }
        sweep = 2 * M_PI;
    }
    arc[0] = cx;
    arc[1] = -cy;
    arc[2] = r;
    arc[3] = end;
    arc[4] = -start;
    arc[5] = g->mode == 2 ? sweep : -sweep;
    return 1;
}

int
gerber_region_arc(circuit_gerber_t *g, double x, double y, double i,
                  double j)
{
    if (!g->count) {
        return gerber_fail(g, TIKU_CIRCUIT_BAD,
                           "Region arc without a contour start");
    }
    double a[6];

    if (!gerber_arc(g, x, y, i, j, a)) {
        return 0;
    }
    double radius = fmax(a[2], a[3]);
    double step = radius <= GERBER_CHORD_ERROR
                  ? M_PI / 8 : 2 * acos(1 - GERBER_CHORD_ERROR / radius);
    double pieces = ceil(fabs(a[5]) / step);

    if (!isfinite(pieces) || pieces > GERBER_REGION_POINTS - g->count) {
        return gerber_fail(g, TIKU_CIRCUIT_LIMIT,
                           "Curved region exceeds 1024 edges");
    }
    size_t n = (size_t)fmax(2, pieces);

    if (n > GERBER_REGION_POINTS - g->count) {
        return gerber_fail(g, TIKU_CIRCUIT_LIMIT,
                           "Region curve budget exceeded");
    }
    for (size_t k = 1; k <= n; k++) {
        double t = (double)k / n, r = a[2] + (a[3] - a[2]) * t;
        double angle = a[4] + a[5] * t;

        /* The last point is the declared end, not the computed one. */
        g->contour[g->count++] = k == n
            ? (circuit_point_t){x, -y}
            : (circuit_point_t){a[0] + r * cos(angle),
                                a[1] + r * sin(angle)};
    }
    return 1;
}
