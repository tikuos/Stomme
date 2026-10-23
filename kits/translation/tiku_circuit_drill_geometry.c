/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_circuit_drill_geometry.c - holes, slots and routes drawn as the
 * swept shape of a round tool, with the details each one carries.
 * Source Y points up, scene Y down.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_circuit_drill_private.h"

#include <math.h>
#include <stdio.h>

/* The drill display colour, ARGB. */
#define DRILL_INK 0xff285e88

int
drill_emit(circuit_drill_t *d, int kind, double x, double y, double radius)
{
    double diameter = d->tools[d->tool].diameter;
    const char *what = kind == DRILL_HOLE ? "hole"
                     : kind == DRILL_SLOT ? "slot"
                     : kind == DRILL_STRAIGHT ? "straight route"
                     : "circular route";
    char name[64], value[96];

    snprintf(name, sizeof name, "T%02d %s %zu", d->tool, what,
             d->doc->count + 1);
    snprintf(value, sizeof value, "%.8g mm %s", diameter, what);
    tiku_circuit_err_t e;
    const uint32_t ink = DRILL_INK;

    if (kind == DRILL_HOLE
        || ((kind == DRILL_STRAIGHT || kind == DRILL_SLOT)
            && x == d->x && y == d->y)) {
        /* A hole, or a route or slot that ends where it starts. */
        e = circuit_arc(d->doc, name, value, x, -y, diameter / 2, 0,
                        2 * acos(-1), 0, ink, 1);
    } else if (kind == DRILL_STRAIGHT || kind == DRILL_SLOT) {
        circuit_point_t points[] = {{d->x, -d->y}, {x, -y}};

        e = circuit_add(d->doc, name, value, points, 2, diameter, ink, 0, 0);
    } else {
        /* The centre lies on the chord's bisector, on the side the
         * direction gives, for a sweep of at most 180 degrees. */
        double dx = x - d->x, dy = y - d->y, chord = hypot(dx, dy);

        if (radius <= 0 || !chord || chord > 2 * radius) {
            return drill_fail(d, TIKU_CIRCUIT_BAD,
                              "Circular route needs distinct endpoints and a "
                              "radius at least half the chord");
        }
        double h = sqrt(fmax(0, radius * radius - chord * chord / 4));
        double side = kind == DRILL_COUNTERCLOCKWISE ? 1 : -1;
        double cx = (x + d->x) / 2 - side * dy / chord * h;
        double cy = (y + d->y) / 2 + side * dx / chord * h;
        double start = atan2(cy - d->y, d->x - cx);
        double sweep = -side * 2 * asin(chord / (2 * radius));

        e = circuit_arc(d->doc, name, value, cx, -cy, radius, start, sweep,
                        diameter, ink, 0);
    }
    if (e) {
        return drill_fail(d, e, "Cannot construct drill geometry");
    }
    char file[DRILL_ATTRIBUTE_TEXT], object[DRILL_ATTRIBUTE_TEXT];
    char details[CIRCUIT_DETAILS_MAX + 1], route[160] = {0};

    if (kind != DRILL_HOLE) {
        snprintf(route, sizeof route,
                 "Route start: X %.8g, Y %.8g mm (source axes)\n",
                 d->x, d->y);
    }
    if (kind == DRILL_CLOCKWISE || kind == DRILL_COUNTERCLOCKWISE) {
        size_t used = 0;

        while (route[used]) {
            used++;
        }
        snprintf(route + used, sizeof route - used, "Radius: %.8g mm; %s\n",
                 radius,
                 kind == DRILL_CLOCKWISE ? "clockwise" : "counterclockwise");
    }
    if (!drill_attributes(d, d->file, file)
        || !drill_attributes(d, d->object, object)) {
        return 0;
    }
    int n = snprintf(details, sizeof details,
                     "Tool: T%02d\n"
                     "Geometry: %s\n"
                     "Diameter: %.8g mm\n"
                     "%s: X %.8g, Y %.8g mm (source axes)\n"
                     "%s"
                     "Source units: %s\n"
                     "Source line: %u\n"
                     "File attributes:\n"
                     "%sTool attributes:\n"
                     "%sObject attributes:\n"
                     "%s",
                     d->tool, what, diameter,
                     kind == DRILL_HOLE ? "Hole centre" : "Route end", x, y,
                     route, d->unit == 1 ? "mm" : "inch", d->line, file,
                     d->tools[d->tool].attributes, object);

    if (n < 0 || (size_t)n >= sizeof details) {
        return drill_fail(d, TIKU_CIRCUIT_LIMIT,
                          "Combined drill details exceed 4096 bytes");
    }
    e = circuit_details(d->doc, d->doc->count - 1, details);
    return e ? drill_fail(d, e, "Cannot store drill details") : 1;
}
