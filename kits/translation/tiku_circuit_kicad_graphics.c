/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_circuit_kicad_graphics.c - KiCad's graphic primitives with their
 * strokes and fills: polylines, wires, buses, rectangles, circles and
 * three-point arcs kept exact; and the connection marks: junctions,
 * no-connect crosses and bus entries.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_circuit_kicad_private.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define KC_JUNCTION  0.9144     /* KiCad's default junction diameter, mm */
#define KC_NOCONNECT 0.635      /* half a no-connect cross's size, mm    */
#define RECT_CORNERS 4          /* points of a rectangle                 */
#define COLLINEAR    1e-12      /* twice the area of a straight "arc"    */
#define FILL_OUTLINE 1          /* filled in the stroke's colour         */
#define FILL_PAPER   2          /* filled with the background colour     */

/** @brief Read the coordinate pair @p n, put through the matrix @p m.
 *  @return 1, or 0 with the failure noted */
static int
point(circuit_kc_t *k, size_t n, const double m[6], circuit_point_t *out)
{
    double x, y;

    if (!kc_number(k, n, 0, &x) || !kc_number(k, n, 1, &y) ||
        !kc_fields(k, n, "", "", 2, "")) {
        return 0;
    }
    if (fabs(x) > CIRCUIT_COORD_MAX || fabs(y) > CIRCUIT_COORD_MAX) {
        return kc_fail(k, n, TIKU_CIRCUIT_BAD,
                       "Coordinate outside supported range");
    }
    *out = kc_transform(m, x, y);
    return 1;
}

/** @brief Read @p n's stroke and fill: a width, KiCad's default for 0; a
 *         solid line; its colour into @p colour, if it gives one; the
 *         fill's kind.  Dashes and other fills are refused. */
static int
style(circuit_kc_t *k, size_t n, double *width, int *fill, uint32_t *colour)
{
    *width = KC_STROKE;
    *fill = 0;
    size_t stroke = kc_child(k, n, "stroke"), f = kc_child(k, n, "fill");

    if (stroke != SX_NONE) {
        if (!kc_fields(k, stroke, "width type color", "", 0, "")) {
            return 0;
        }
        size_t w = kc_child(k, stroke, "width");

        if (w != SX_NONE && !kc_number(k, w, 0, width)) {
            return 0;
        }
        if (*width < 0 || *width > CIRCUIT_WIDTH_MAX) {
            return kc_fail(k, w, TIKU_CIRCUIT_BAD, "Invalid stroke width");
        }
        if (!*width) {
            *width = KC_STROKE;
        }
        const char *type = kc_arg(k, kc_child(k, stroke, "type"), 0);

        if (type && strcmp(type, "default") && strcmp(type, "solid")) {
            return kc_fail(k, stroke, TIKU_CIRCUIT_UNSUPPORTED,
                           "Dashed schematic strokes are not supported yet");
        }
        size_t c = kc_child(k, stroke, "color");

        if (c != SX_NONE && !kc_colour(k, c, colour)) {
            return 0;
        }
    }
    if (f != SX_NONE) {
        if (!kc_fields(k, f, "type", "", 0, "")) {
            return 0;
        }
        const char *type = kc_arg(k, kc_child(k, f, "type"), 0);

        if (!type) {
            return kc_fail(k, f, TIKU_CIRCUIT_BAD, "Fill requires a type");
        }
        if (!strcmp(type, "outline")) {
            *fill = FILL_OUTLINE;
        } else if (!strcmp(type, "background")) {
            *fill = FILL_PAPER;
        } else if (strcmp(type, "none")) {
            return kc_fail(k, f, TIKU_CIRCUIT_UNSUPPORTED,
                           "Unsupported schematic fill");
        }
    }
    return 1;
}

int
kc_colour(circuit_kc_t *k, size_t n, uint32_t *colour)
{
    double r, g, b, a;

    if (!kc_number(k, n, 0, &r) || !kc_number(k, n, 1, &g) ||
        !kc_number(k, n, 2, &b) || !kc_number(k, n, 3, &a) ||
        !kc_fields(k, n, "", "", 4, "")) {
        return 0;
    }
    if (!r && !g && !b && !a) {
        return 1;
    }
    if (r < 0 || g < 0 || b < 0 || r > 255 || g > 255 || b > 255 ||
        a != 1) {
        return kc_fail(k, n, TIKU_CIRCUIT_UNSUPPORTED,
                       "Only opaque custom colours are supported");
    }
    *colour = CIRCUIT_OPAQUE | ((uint32_t)r << 16) | ((uint32_t)g << 8) |
              (uint32_t)b;
    return 1;
}

/** @brief Append a path's fill, if any, then its stroke over it, so the
 *         boundary keeps its whole width.  @return 1 or 0 */
static int
polygon(circuit_kc_t *k, size_t n, const char *name, const char *value,
        circuit_point_t *p, size_t count, double width, uint32_t colour,
        int closed, int fill)
{
    if (fill && !kc_result(k, n, circuit_add(k->doc, name, value, p, count,
                                             0, fill == FILL_PAPER
                                                ? CIRCUIT_PAPER : colour,
                                             1, 1))) {
        return 0;
    }
    return kc_result(k, n, circuit_add(k->doc, name, value, p, count, width,
                                       colour, closed || fill, 0));
}

int
kc_graphic(circuit_kc_t *k, size_t n, const double m[6], const char *name,
           const char *value, uint32_t colour)
{
    const char *head = circuit_sx_head(&k->sx, n);
    double width;
    int fill;
    const char *fields = !strcmp(head, "rectangle")
                         ? "start end stroke fill uuid"
                         : !strcmp(head, "circle")
                         ? "center radius stroke fill uuid"
                         : !strcmp(head, "arc")
                         ? "start mid end stroke fill uuid"
                         : "pts stroke fill uuid";

    if (!kc_fields(k, n, fields, "", 0, "") ||
        !style(k, n, &width, &fill, &colour)) {
        return 0;
    }
    if (!strcmp(head, "polyline") || !strcmp(head, "wire") ||
        !strcmp(head, "bus")) {
        size_t pts = kc_child(k, n, "pts"), count = 0;

        if (!kc_fields(k, pts, "xy", "xy", 0, "")) {
            return 0;
        }
        for (size_t i = pts + 2; i < k->sx.node[pts].end;
             i = k->sx.node[i].end) {
            count++;
        }
        if (count < 2 || count > CIRCUIT_POINTS_MAX) {
            return kc_fail(k, pts, TIKU_CIRCUIT_LIMIT,
                           "Invalid polyline point count");
        }
        circuit_point_t *p = malloc(count * sizeof *p);

        if (!p) {
            return kc_fail(k, n, TIKU_CIRCUIT_NOMEM,
                           "Cannot allocate polyline");
        }
        size_t j = 0;

        for (size_t i = pts + 2; i < k->sx.node[pts].end && !k->error;
             i = k->sx.node[i].end) {
            if (!point(k, i, m, &p[j++])) {
                break;
            }
        }
        int ok = !k->error && polygon(k, n, name, value, p, count, width,
                                      colour, 0, fill);

        free(p);
        return ok;
    }
    if (!strcmp(head, "rectangle")) {
        circuit_point_t a, b;

        if (!point(k, kc_child(k, n, "start"), KC_IDENTITY, &a) ||
            !point(k, kc_child(k, n, "end"), KC_IDENTITY, &b)) {
            return 0;
        }
        circuit_point_t p[RECT_CORNERS] = {kc_transform(m, a.x, a.y),
                                           kc_transform(m, b.x, a.y),
                                           kc_transform(m, b.x, b.y),
                                           kc_transform(m, a.x, b.y)};

        return polygon(k, n, name, value, p, RECT_CORNERS, width, colour, 1,
                       fill);
    }
    circuit_point_t center;
    double radius = 0, start = 0, sweep = 2 * M_PI;

    if (!strcmp(head, "circle")) {
        if (!point(k, kc_child(k, n, "center"), m, &center) ||
            !kc_number(k, kc_child(k, n, "radius"), 0, &radius)) {
            return 0;
        }
    } else if (!strcmp(head, "arc")) {
        circuit_point_t a, b, c;

        if (!point(k, kc_child(k, n, "start"), m, &a) ||
            !point(k, kc_child(k, n, "mid"), m, &b) ||
            !point(k, kc_child(k, n, "end"), m, &c)) {
            return 0;
        }
        double bx = b.x - a.x, by = b.y - a.y;
        double cx = c.x - a.x, cy = c.y - a.y;
        double cross = 2 * (bx * cy - by * cx), bb = bx * bx + by * by;
        double cc = cx * cx + cy * cy;

        if (fabs(cross) < COLLINEAR) {
            return kc_fail(k, n, TIKU_CIRCUIT_BAD, "Collinear arc points");
        }
        center = (circuit_point_t){a.x + (bb * cy - cc * by) / cross,
                                   a.y + (bx * cc - cx * bb) / cross};
        radius = hypot(a.x - center.x, a.y - center.y);
        start = atan2(a.y - center.y, a.x - center.x);
        sweep = fmod(atan2(c.y - center.y, c.x - center.x) - start +
                     2 * M_PI, 2 * M_PI);
        double mid = fmod(atan2(b.y - center.y, b.x - center.x) - start +
                          2 * M_PI, 2 * M_PI);

        if (mid > sweep) {
            sweep -= 2 * M_PI;
        }
    } else {
        return kc_fail(k, n, TIKU_CIRCUIT_UNSUPPORTED,
                       "Unsupported graphic primitive");
    }
    if (fill && !kc_result(k, n, circuit_arc(k->doc, name, value, center.x,
                                             center.y, radius, start, sweep,
                                             0, fill == FILL_PAPER
                                                ? CIRCUIT_PAPER : colour,
                                             1))) {
        return 0;
    }
    return kc_result(k, n, circuit_arc(k->doc, name, value, center.x,
                                       center.y, radius, start, sweep, width,
                                       colour, 0));
}

int
kc_mark(circuit_kc_t *k, size_t n)
{
    const char *head = circuit_sx_head(&k->sx, n);
    double x, y, angle;

    if (!kc_fields(k, n, "at diameter color size stroke uuid", "", 0, "") ||
        !kc_at(k, n, &x, &y, &angle)) {
        return 0;
    }
    if (!strcmp(head, "junction")) {
        double diameter = KC_JUNCTION;
        size_t d = kc_child(k, n, "diameter");

        if (d != SX_NONE && !kc_number(k, d, 0, &diameter)) {
            return 0;
        }
        if (!diameter) {
            diameter = KC_JUNCTION;
        }
        uint32_t colour = KC_WIRE;
        size_t col = kc_child(k, n, "color");

        if (col != SX_NONE && !kc_colour(k, col, &colour)) {
            return 0;
        }
        return kc_result(k, n, circuit_arc(k->doc, "Junction", "", x, y,
                                           diameter / 2, 0, 2 * M_PI, 0,
                                           colour, 1));
    }
    if (!strcmp(head, "bus_entry")) {
        double dx, dy, width;
        int fill;
        uint32_t colour = KC_BLUE;
        size_t size = kc_child(k, n, "size");

        if (!kc_number(k, size, 0, &dx) || !kc_number(k, size, 1, &dy) ||
            !style(k, n, &width, &fill, &colour)) {
            return 0;
        }
        circuit_point_t p[] = {{x, y}, {x + dx, y + dy}};

        return kc_result(k, n, circuit_add(k->doc, "Bus entry", "", p, 2,
                                           width, colour, 0, 0));
    }
    for (int sign = -1; sign <= 1; sign += 2) {
        circuit_point_t p[] = {{x - KC_NOCONNECT, y - sign * KC_NOCONNECT},
                               {x + KC_NOCONNECT, y + sign * KC_NOCONNECT}};

        if (!kc_result(k, n, circuit_add(k->doc, "No connect", "", p, 2,
                                         KC_STROKE, KC_BLUE, 0, 0))) {
            return 0;
        }
    }
    return 1;
}
