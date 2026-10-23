/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_circuit_gerber_repeat.c - SR step-and-repeat blocks, expanded when
 * they close: Y then X, in the original order, so dark and clear
 * operations keep their polarity order.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_circuit_gerber_private.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/* Columns times rows in one block. */
#define REPEAT_COPIES_MAX 4096

int
gerber_repeat(circuit_gerber_t *g, const char *s)
{
    if (!g->unit || g->region) {
        return gerber_fail(g, TIKU_CIRCUIT_BAD,
                           "Repeat requires units and cannot interrupt a "
                           "region");
    }
    if (*s) {
        if (g->repeat_depth == GERBER_REPEAT_DEPTH) {
            return gerber_fail(g, TIKU_CIRCUIT_LIMIT,
                               "Repeat nesting exceeds eight levels");
        }
        double p[4];
        const char keys[] = "XYIJ";

        for (int i = 0; i < 4; i++) {
            if (*s++ != keys[i] || !gerber_number(g, &s, &p[i], 0)) {
                return gerber_fail(g, TIKU_CIRCUIT_BAD,
                                   "Invalid repeat parameters");
            }
        }
        if (*s || p[0] < 1 || p[1] < 1 || p[0] != floor(p[0])
            || p[1] != floor(p[1]) || p[2] < 0 || p[3] < 0) {
            return gerber_fail(g, TIKU_CIRCUIT_BAD,
                               "Repeat counts must be positive integers, "
                               "distances nonnegative");
        }
        if (p[0] * p[1] > REPEAT_COPIES_MAX) {
            return gerber_fail(g, TIKU_CIRCUIT_LIMIT,
                               "Repeat count exceeds 4096");
        }
        g->repeats[g->repeat_depth++] = (gerber_repeat_t){
            g->doc->count, (unsigned)p[0], (unsigned)p[1], p[2] * g->unit,
            p[3] * g->unit
        };
        return 1;
    }
    if (!g->repeat_depth) {
        return gerber_fail(g, TIKU_CIRCUIT_BAD,
                           "Repeat close without an open block");
    }
    gerber_repeat_t r = g->repeats[--g->repeat_depth];
    size_t end = g->doc->count, n = end - r.begin;

    if (n && r.x * r.y - 1 > (CIRCUIT_OBJECTS_MAX - end) / n) {
        return gerber_fail(g, TIKU_CIRCUIT_LIMIT,
                           "Expanded repeat exceeds scene object limit");
    }
    /* Copy (0, 0) is the block itself. */
    for (unsigned x = 0; x < r.x; x++) {
        for (unsigned y = 0; y < r.y; y++) {
            if (x || y) {
                for (size_t i = r.begin; i < end; i++) {
                    if (g->options && g->options->cancel
                        && g->options->cancel(g->options->ctx)) {
                        return gerber_fail(g, TIKU_CIRCUIT_CANCELLED,
                                           "Repeat expansion cancelled");
                    }
                    tiku_circuit_err_t e;

                    e = circuit_clone(g->doc, &g->doc->shapes[i], x * r.dx,
                                      -(y * r.dy));
                    if (e) {
                        return gerber_fail(g, e,
                                           "Cannot expand repeat geometry");
                    }
                    size_t at = g->doc->count - 1;
                    const char *old = tiku_circuit_details(g->doc, at);
                    char detail[CIRCUIT_DETAILS_MAX + 1];
                    int len = snprintf(detail, sizeof detail,
                                       "%s\nRepeat displacement: X %.8g, "
                                       "Y %.8g mm (source axes)",
                                       old, x * r.dx, y * r.dy);

                    if (len < 0 || (size_t)len >= sizeof detail) {
                        return gerber_fail(g, TIKU_CIRCUIT_LIMIT,
                                           "Repeat metadata exceeds limit");
                    }
                    e = circuit_details(g->doc, at, detail);
                    if (e) {
                        return gerber_fail(g, e,
                                           "Cannot retain repeat metadata");
                    }
                }
            }
        }
    }
    g->have_point = 0;
    g->operation = 0;
    return 1;
}
