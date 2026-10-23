/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_circuit_gerber_region.c - G36/G37 regions.  Each contour is closed
 * explicitly, its curves arrive as chords, and it is validated, cut-ins to
 * holes included, before it becomes a filled object.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_circuit_gerber_private.h"

#include <math.h>
#include <stdio.h>

int
gerber_region(circuit_gerber_t *g, int op, double x, double y)
{
    if (!g->region) {
        return gerber_fail(g, TIKU_CIRCUIT_BAD, "G37 without G36");
    }
    if (op) {
        if (op == 3 || (op == 1 && (g->mode != 1 || !g->count))) {
            return gerber_fail(g, TIKU_CIRCUIT_UNSUPPORTED,
                               "Region requires an initial move and linear "
                               "contour segments");
        }
        if (op == 2 && g->count) {
            /* A move ends the open contour and starts the next one. */
            if (!gerber_region(g, 0, 0, 0)) {
                return 0;
            }
            g->region = 1;
            g->count = 0;
        }
        if (g->count >= GERBER_REGION_POINTS) {
            return gerber_fail(g, TIKU_CIRCUIT_LIMIT,
                               "Region exceeds 1024 edges");
        }
        circuit_point_t p = {x, -y};

        if (g->count && p.x == g->contour[g->count - 1].x
            && p.y == g->contour[g->count - 1].y) {
            return 1;   /* KiCad exports can repeat an endpoint between arcs */
        }
        g->contour[g->count++] = p;
        return 1;
    }
    size_t n = g->count;

    if (n < 4 || g->contour[0].x != g->contour[n - 1].x
        || g->contour[0].y != g->contour[n - 1].y) {
        return gerber_fail(g, TIKU_CIRCUIT_BAD,
                           "Region contour must be explicitly closed with at "
                           "least three edges");
    }
    n--;
    tiku_circuit_err_t valid = circuit_contour(g->contour, n, g->options);

    if (valid) {
        return gerber_fail(g, valid,
                           "Invalid region contour, cut-in topology, or "
                           "contour budget");
    }
    char name[64], attrs[GERBER_ATTRIBUTE_TEXT];

    snprintf(name, sizeof name, "Region %zu", g->doc->count + 1);
    tiku_circuit_err_t e = circuit_add(g->doc, name, "Region contour",
                                       g->contour, n, 0, GERBER_INK, 1, 1);

    if (e) {
        return gerber_fail(g, e, "Cannot construct Gerber region");
    }
    if (!gerber_attributes(g, g->tool, attrs)) {
        return 0;
    }
    g->region = 0;
    return gerber_details(g, "Geometry: region contour (validated cut-ins; "
                             "curves tessellated for display)", attrs);
}
