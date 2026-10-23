/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_circuit_gerber_primitive.c - the standard and historical aperture
 * macro primitives.  Their parameters are in file units about the macro's
 * origin; the macro's scene gets them in mm with Y negated.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_circuit_gerber_macro.h"

#include <math.h>

#define OUTLINE_MAX        1024     /* an outline's vertices             */
#define MOIRE_RINGS_MAX    128      /* a moire's rings                   */
#define THERMAL_PIECES_MAX 500      /* chords in one thermal arc         */
#define THERMAL_POINTS     1024     /* one thermal quarter's outline     */
#define THERMAL_STEP_MIN   1e-6     /* the smallest chord angle, radians */

/* Primitive codes; 2, 6 and 22 are historical. */
enum {
    PRIM_CIRCLE = 1, PRIM_LINE = 2, PRIM_OUTLINE = 4, PRIM_POLYGON = 5,
    PRIM_MOIRE = 6, PRIM_THERMAL = 7, PRIM_VECTOR = 20, PRIM_CENTRE = 21,
    PRIM_LOWER_LEFT = 22
};

/** @brief (@p x, @p y), file units, turned @p a degrees about the macro
 *         origin, as scene mm (@p unit mm per file unit) with Y negated. */
static circuit_point_t
rotate(double x, double y, double a, double unit)
{
    a *= M_PI / 180;
    return (circuit_point_t){unit * (x * cos(a) - y * sin(a)),
                             -unit * (x * sin(a) + y * cos(a))};
}

/** @brief The @p n points @p p, file units, turned @p angle degrees, as a
 *         filled polygon in the macro scene @p d; @p clear makes it take
 *         coverage away.  @return 0, or the scene's error. */
static tiku_circuit_err_t
polygon(circuit_gerber_t *g, tiku_circuit_t *d, circuit_point_t *p,
        size_t n, double angle, int clear)
{
    for (size_t i = 0; i < n; i++) {
        p[i] = rotate(p[i].x, p[i].y, angle, g->unit);
    }
    tiku_circuit_err_t e = circuit_add(d, "Macro primitive", "Filled polygon",
                                       p, n, 0, GERBER_INK, 1, 1);

    if (!e) {
        d->shapes[d->count - 1].clear = clear;
    }
    return e;
}

/** @brief A disc of @p diameter with a centred @p hole, centred on
 *         (@p x, @p y) turned @p angle degrees; file units.  A zero
 *         diameter adds nothing.  @return 0, BAD, or the scene's error. */
static tiku_circuit_err_t
circle(circuit_gerber_t *g, tiku_circuit_t *d, double x, double y,
       double diameter, double hole, double angle, int clear)
{
    if (diameter == 0) {
        return TIKU_CIRCUIT_OK;
    }
    if (diameter < 0 || hole < 0 || hole >= diameter) {
        return TIKU_CIRCUIT_BAD;
    }
    circuit_point_t p = rotate(x, y, angle, g->unit);
    tiku_circuit_err_t e = circuit_arc(d, "Macro primitive", "Circle", p.x,
                                       p.y, diameter * g->unit / 2, 0,
                                       2 * M_PI, 0, GERBER_INK, 1);

    if (!e) {
        d->shapes[d->count - 1].clear = clear;
        d->shapes[d->count - 1].hole = hole * g->unit;
    }
    return e;
}

/** @brief A @p w by @p h rectangle centred on (@p x, @p y), turned
 *         @p angle degrees about the macro origin; file units.  A zero
 *         side adds nothing.  @return 0, BAD, or the scene's error. */
static tiku_circuit_err_t
rectangle(circuit_gerber_t *g, tiku_circuit_t *d, double x, double y,
          double w, double h, double angle, int clear)
{
    if (w < 0 || h < 0) {
        return TIKU_CIRCUIT_BAD;
    }
    if (!w || !h) {
        return TIKU_CIRCUIT_OK;
    }
    circuit_point_t p[] = {{x - w / 2, y - h / 2}, {x + w / 2, y - h / 2},
                           {x + w / 2, y + h / 2}, {x - w / 2, y + h / 2}};

    return polygon(g, d, p, 4, angle, clear);
}

/** @brief Whether the @p n points @p p lie on one line, so a polygon
 *         through them has no area; fewer than three count as such.
 *  @note Consecutive points must differ. */
static int
flat(const circuit_point_t *p, size_t n)
{
    for (size_t i = 2; i < n; i++) {
        if ((p[1].x - p[0].x) * (p[i].y - p[0].y)
            != (p[1].y - p[0].y) * (p[i].x - p[0].x)) {
            return 0;
        }
    }
    return 1;
}

int
gerber_macro_primitive(circuit_gerber_t *g, tiku_circuit_t *d, int code,
                       const double *p, size_t n)
{
    tiku_circuit_err_t e = TIKU_CIRCUIT_BAD;
    int clear = 0;

    /* All but the moire and the thermal open with their exposure. */
    if (code != PRIM_MOIRE && code != PRIM_THERMAL) {
        if (!n || (p[0] != 0 && p[0] != 1)) {
            goto fail;
        }
        clear = p[0] == 0;
    }
    if (code == PRIM_CIRCLE && (n == 4 || n == 5)) {
        /* Exposure, diameter, centre, and an optional rotation. */
        e = circle(g, d, p[2], p[3], p[1], 0, n == 5 ? p[4] : 0, clear);
    } else if ((code == PRIM_VECTOR || code == PRIM_LINE) && n == 7) {
        /* Exposure, width, start, end, rotation. */
        double dx = p[4] - p[2], dy = p[5] - p[3], length = hypot(dx, dy);

        if (p[1] < 0) {
            goto fail;
        }
        if (!length || !p[1]) {
            e = TIKU_CIRCUIT_OK;    /* no length or no width covers nothing */
        } else {
            double x = -dy * p[1] / (2 * length);
            double y = dx * p[1] / (2 * length);
            circuit_point_t points[] = {
                {p[2] + x, p[3] + y}, {p[4] + x, p[5] + y},
                {p[4] - x, p[5] - y}, {p[2] - x, p[3] - y}
            };

            e = polygon(g, d, points, 4, p[6], clear);
        }
    } else if ((code == PRIM_CENTRE || code == PRIM_LOWER_LEFT) && n == 6) {
        /* Exposure, width, height, then the centre (21) or the lower left
         * corner (22), and the rotation. */
        e = rectangle(g, d, p[3] + (code == PRIM_LOWER_LEFT ? p[1] / 2 : 0),
                      p[4] + (code == PRIM_LOWER_LEFT ? p[2] / 2 : 0), p[1],
                      p[2], p[5], clear);
    } else if (code == PRIM_OUTLINE && n >= 11 && p[1] >= 3
               && p[1] <= OUTLINE_MAX && p[1] == floor(p[1])
               && n == 5 + 2 * (size_t)p[1]) {
        /* Exposure, vertex count, the start point, each vertex (the last
         * back at the start), rotation. */
        size_t count = (size_t)p[1], kept = 0;
        circuit_point_t points[OUTLINE_MAX + 1];

        /* A point equal to the one before adds no edge; KiCad repeats
         * the start as the last vertex and again as the closing point. */
        for (size_t i = 0; i <= count; i++) {
            circuit_point_t q = {p[2 + 2 * i], p[3 + 2 * i]};

            if (!kept || q.x != points[kept - 1].x
                || q.y != points[kept - 1].y) {
                points[kept++] = q;
            }
        }
        if (points[0].x != points[kept - 1].x
            || points[0].y != points[kept - 1].y) {
            goto fail;
        }
        count = kept - 1;
        if (flat(points, count)) {
            e = TIKU_CIRCUIT_OK;    /* an outline with no area */
        } else {
            e = circuit_contour(points, count, g->options);
            if (!e) {
                e = polygon(g, d, points, count, p[n - 1], clear);
            }
        }
    } else if (code == PRIM_POLYGON && n == 6 && p[1] >= GERBER_POLYGON_MIN
               && p[1] <= GERBER_POLYGON_MAX && p[1] == floor(p[1])
               && p[4] >= 0) {
        /* Exposure, vertex count, centre, diameter, rotation. */
        circuit_point_t points[GERBER_POLYGON_MAX];
        size_t count = (size_t)p[1];

        for (size_t i = 0; i < count; i++) {
            points[i] = (circuit_point_t){
                p[2] + p[4] / 2 * cos(2 * M_PI * i / count),
                p[3] + p[4] / 2 * sin(2 * M_PI * i / count)
            };
        }
        e = p[4] ? polygon(g, d, points, count, p[5], clear)
                 : TIKU_CIRCUIT_OK;
    } else if (code == PRIM_MOIRE && n == 9 && p[2] > 0 && p[3] > 0
               && p[4] >= 0 && p[5] >= 1 && p[5] <= MOIRE_RINGS_MAX
               && p[5] == floor(p[5]) && p[6] >= 0 && p[7] >= 0) {
        /* Centre, outer diameter, ring thickness, gap, ring count, cross
         * thickness and length, rotation. */
        e = TIKU_CIRCUIT_OK;
        for (int i = 0; i < (int)p[5] && !e; i++) {
            double diameter = p[2] - 2 * i * (p[3] + p[4]);

            if (diameter <= 0) {
                break;
            }
            e = circle(g, d, p[0], p[1], diameter,
                       fmax(0, diameter - 2 * p[3]), p[8], 0);
        }
        if (!e) {
            e = rectangle(g, d, p[0], p[1], p[7], p[6], p[8], 0);
        }
        if (!e) {
            e = rectangle(g, d, p[0], p[1], p[6], p[7], p[8], 0);
        }
    } else if (code == PRIM_THERMAL && n == 6 && p[2] > p[3] && p[3] >= 0
               && p[4] >= 0 && p[4] * sqrt(2) < p[2]) {
        /* Centre, outer and inner diameter, gap, rotation. */
        double outer = p[2] / 2, inner = p[3] / 2, gap = p[4] / 2;

        if (!gap) {
            e = circle(g, d, p[0], p[1], p[2], p[3], p[5], 0);
        } else {
            /* Each quarter between the gaps: its outer arc, then its
             * inner arc back, or the corner where the gaps meet. */
            double start = asin(gap / outer), stop = M_PI / 2 - start;
            double step = 2 * acos(fmax(-1, 1 - GERBER_CHORD_ERROR
                                                / (outer * g->unit)));
            size_t pieces = (size_t)ceil((stop - start)
                                         / fmax(step, THERMAL_STEP_MIN));

            /* A thermal that needs more chords gets THERMAL_PIECES_MAX;
             * each then strays from its arc by at most 1.3e-6 of the
             * radius. */
            if (pieces > THERMAL_PIECES_MAX) {
                pieces = THERMAL_PIECES_MAX;
            }
            e = TIKU_CIRCUIT_OK;
            for (int q = 0; q < 4 && !e; q++) {
                circuit_point_t points[THERMAL_POINTS];
                size_t used = 0;

                for (size_t i = 0; i <= pieces; i++) {
                    double a = start + (stop - start) * i / pieces;

                    points[used++] = (circuit_point_t){outer * cos(a),
                                                       outer * sin(a)};
                }
                if (inner > gap * sqrt(2)) {
                    double a = asin(gap / inner), b = M_PI / 2 - a;

                    for (size_t i = 0; i <= pieces; i++) {
                        double angle = b + (a - b) * i / pieces;

                        points[used++] = (circuit_point_t){
                            inner * cos(angle), inner * sin(angle)
                        };
                    }
                } else {
                    points[used++] = (circuit_point_t){gap, gap};
                }
                for (size_t i = 0; i < used; i++) {
                    double x = points[i].x, y = points[i].y;
                    double a = q * M_PI / 2;

                    points[i] = (circuit_point_t){
                        p[0] + x * cos(a) - y * sin(a),
                        p[1] + x * sin(a) + y * cos(a)
                    };
                }
                e = polygon(g, d, points, used, p[5], 0);
            }
        }
    }
fail:
    return e ? gerber_fail(g, e, "Invalid, unsupported or over-budget macro "
                                 "primitive")
             : 1;
}
