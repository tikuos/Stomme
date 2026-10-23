/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_circuit_contour.c - a filled contour checked before it is drawn:
 * simple loops, joined only by axis-aligned cut-in pairs that run exactly
 * back over themselves.  The bridges are taken out to recover the loops,
 * whose nesting and winding are then checked.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_circuit_private.h"

#include <math.h>
#include <stdlib.h>

#define CONTOUR_POINTS_MAX 5000 /* the checks are quadratic in the points */

/** @brief Which way a contour's cut-in bridges run. */
enum { BRIDGE_NONE, BRIDGE_VERTICAL, BRIDGE_HORIZONTAL };

/** @brief Whether @p a and @p b are the same point. */
static int
same(circuit_point_t a, circuit_point_t b)
{
    return a.x == b.x && a.y == b.y;
}

/** @brief Twice the signed area of the triangle @p a, @p b, @p c. */
static double
cross(circuit_point_t a, circuit_point_t b, circuit_point_t c)
{
    return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}

/** @brief Whether @p p lies on the segment from @p a to @p b. */
static int
between(circuit_point_t a, circuit_point_t b, circuit_point_t p)
{
    return !cross(a, b, p) && p.x >= fmin(a.x, b.x) &&
           p.x <= fmax(a.x, b.x) && p.y >= fmin(a.y, b.y) &&
           p.y <= fmax(a.y, b.y);
}

/** @brief Even-odd containment of @p q in the loops of @p p other than
 *         loop @p exclude, in one pass over the edges. */
static int
inside(const circuit_point_t *p, const int *loop, size_t n, int exclude,
       circuit_point_t q)
{
    int in = 0;

    for (size_t i = 0; i < n; i++) {
        if (loop[i] && loop[i] != exclude) {
            circuit_point_t a = p[i], b = p[(i + 1) % n];

            if ((a.y > q.y) != (b.y > q.y) &&
                q.x < a.x + (b.x - a.x) * (q.y - a.y) / (b.y - a.y)) {
                in = !in;
            }
        }
    }
    return in;
}

tiku_circuit_err_t
circuit_contour(const circuit_point_t *p, size_t n,
                const tiku_circuit_import_t *opt)
{
    if (!p || n < 3) {
        return TIKU_CIRCUIT_BAD;
    }
    if (n > CONTOUR_POINTS_MAX) {
        return TIKU_CIRCUIT_LIMIT;
    }
    int *bridge = calloc(n, sizeof *bridge), *loop = calloc(n, sizeof *loop);
    double *area = calloc(n + 1, sizeof *area);
    size_t *first = calloc(n + 1, sizeof *first);
    tiku_circuit_err_t e = TIKU_CIRCUIT_BAD;

    if (!bridge || !loop || !area || !first) {
        e = TIKU_CIRCUIT_NOMEM;
        goto done;
    }
    int direction = BRIDGE_NONE;

    for (size_t i = 0; i < n; i++) {
        if (opt && opt->cancel && opt->cancel(opt->ctx)) {
            e = TIKU_CIRCUIT_CANCELLED;
            goto done;
        }
        circuit_point_t a = p[i], b = p[(i + 1) % n];

        if (same(a, b)) {
            goto done;
        }
        for (size_t j = i + 1; j < n; j++) {
            if (same(a, p[(j + 1) % n]) && same(b, p[j])) {
                int axis = a.x == b.x ? BRIDGE_VERTICAL
                           : a.y == b.y ? BRIDGE_HORIZONTAL : BRIDGE_NONE;

                if (!axis || (direction && axis != direction) ||
                    bridge[i] || bridge[j]) {
                    goto done;
                }
                direction = axis;
                bridge[i] = bridge[j] = 1;
            }
        }
    }
    for (size_t i = 0; i < n; i++) {
        if (opt && opt->cancel && opt->cancel(opt->ctx)) {
            e = TIKU_CIRCUIT_CANCELLED;
            goto done;
        }
        circuit_point_t a = p[i], b = p[(i + 1) % n];

        for (size_t j = i + 1; j < n; j++) {
            circuit_point_t c = p[j], d = p[(j + 1) % n];

            if (bridge[i] && bridge[j] && same(a, d) && same(b, c)) {
                continue;
            }
            double ca = cross(a, b, c), da = cross(a, b, d);
            double ac = cross(c, d, a), bc = cross(c, d, b);

            if (ca * da < 0 && ac * bc < 0) {
                goto done;
            }
            if ((!same(c, a) && !same(c, b) && between(a, b, c)) ||
                (!same(d, a) && !same(d, b) && between(a, b, d)) ||
                (!same(a, c) && !same(a, d) && between(c, d, a)) ||
                (!same(b, c) && !same(b, d) && between(c, d, b))) {
                goto done;
            }
            if (!ca && !da && same(a, c) && same(b, d)) {
                goto done;
            }
        }
    }
    int loops = 0;

    for (size_t i = 0; i < n; i++) {
        if (!bridge[i] && !loop[i]) {
            int id = ++loops;

            first[id] = i;
            size_t at = i, steps = 0;

            do {
                if (opt && opt->cancel && opt->cancel(opt->ctx)) {
                    e = TIKU_CIRCUIT_CANCELLED;
                    goto done;
                }
                if (loop[at] || ++steps > n) {
                    goto done;
                }
                loop[at] = id;
                circuit_point_t a = p[at], b = p[(at + 1) % n];

                area[id] += a.x * b.y - a.y * b.x;
                size_t next = SIZE_MAX;
                int incoming = 0;

                for (size_t j = 0; j < n; j++) {
                    if (!bridge[j]) {
                        if (same(p[j], b)) {
                            if (next != SIZE_MAX) {
                                goto done;
                            }
                            next = j;
                        }
                        if (same(p[(j + 1) % n], b)) {
                            incoming++;
                        }
                    }
                }
                if (next == SIZE_MAX || incoming != 1) {
                    goto done;
                }
                at = next;
            } while (at != i);
            if (steps < 3 || !area[id]) {
                goto done;
            }
        }
    }
    if (!loops) {
        goto done;
    }
    int outer_sign = 0;

    for (int i = 1; i <= loops; i++) {
        if (opt && opt->cancel && opt->cancel(opt->ctx)) {
            e = TIKU_CIRCUIT_CANCELLED;
            goto done;
        }
        int depth = inside(p, loop, n, i, p[first[i]]);
        int sign = area[i] > 0 ? 1 : -1;

        if (depth & 1) {
            sign = -sign;
        }
        if (outer_sign && sign != outer_sign) {
            goto done;
        }
        outer_sign = sign;
    }
    for (size_t i = 0; i < n; i++) {
        if (bridge[i]) {
            circuit_point_t q = {(p[i].x + p[(i + 1) % n].x) / 2,
                                 (p[i].y + p[(i + 1) % n].y) / 2};
            int depth = inside(p, loop, n, 0, q);

            if (!(depth & 1)) {
                goto done;
            }
        }
    }
    e = TIKU_CIRCUIT_OK;
done:
    free(bridge);
    free(loop);
    free(area);
    free(first);
    return e;
}
