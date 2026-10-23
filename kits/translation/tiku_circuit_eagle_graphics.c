/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_circuit_eagle_graphics.c - EAGLE drawing elements: wires, circles,
 * rectangles, polygons, text, labels and holes, transformed once from the
 * source's Y-up axes into the shared scene.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_circuit_eagle_private.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define EAGLE_POLYGON_MAX 1024      /* a polygon's vertices              */
#define EAGLE_TEXT_MAX    4096      /* one text element's bytes          */
#define EAGLE_TEXT_SIZE   1.27      /* text size when none is given, mm  */
#define EAGLE_TEXT_LIMIT  1000      /* the largest text size, mm         */
#define JUNCTION_RADIUS   .5        /* a junction dot's radius, mm       */

/** @brief Record a failed scene construction @p error.
 *  @return 1 for none, else 0 with the failure recorded. */
static int
result(circuit_eagle_t *e, tiku_circuit_err_t error)
{
    return error ? eagle_fail(e, error,
                              "Cannot construct EAGLE graphic "
                              "(geometry/font/resource limit)")
                 : 1;
}

/** @brief Required numeric attribute @p key of @p n. */
static double
number(circuit_eagle_t *e, eagle_node_t *n, const char *key)
{
    return eagle_number(e, n, key, 0, 1);
}

/** @brief A wire from (@p x, @p y) to (@p xx, @p yy), straight or bent
 *         through @p curve degrees, @p width wide; symbol-local source mm.
 *  @return 1, or 0 with the failure recorded. */
static int
line(circuit_eagle_t *e, double x, double y, double xx, double yy,
     double width, double curve)
{
    circuit_point_t p[] = {eagle_point(e, x, y), eagle_point(e, xx, yy)};

    if (e->error) {
        return 0;
    }
    if (!curve) {
        return result(e, circuit_add(e->doc, e->name, e->value, p, 2, width,
                                     EAGLE_INK_WIRE, 0, 0));
    }
    double dx = xx - x, dy = yy - y, theta = curve * M_PI / 180;
    double length = hypot(dx, dy);

    if (!length || fabs(curve) >= 360) {
        return eagle_fail(e, TIKU_CIRCUIT_BAD, "Invalid curved EAGLE wire");
    }
    /* The centre is on the chord's bisector, where the chord subtends
     * the curve's angle. */
    double cx = (x + xx) / 2 - dy / (2 * tan(theta / 2));
    double cy = (y + yy) / 2 + dx / (2 * tan(theta / 2));
    circuit_point_t center = eagle_point(e, cx, cy);
    double r = hypot(x - cx, y - cy);
    double start = atan2(p[0].y - center.y, p[0].x - center.x);
    double det = e->m[0] * e->m[3] - e->m[1] * e->m[2];

    /* Negating Y reverses the sweep; a mirroring transform reverses it
     * back. */
    return result(e, circuit_arc(e->doc, e->name, e->value, center.x,
                                 center.y, r, start,
                                 det > 0 ? -theta : theta, width,
                                 EAGLE_INK_WIRE, 0));
}

int
eagle_text(circuit_eagle_t *e, eagle_node_t *n, const char *text)
{
    double x = number(e, n, "x"), y = number(e, n, "y");
    double size = eagle_number(e, n, "size", EAGLE_TEXT_SIZE, 0), angle;
    int mirror, spin;

    if (!eagle_rotation(e, tiku_xml_attr(n, "rot"), &angle, &mirror, &spin)
        || e->error) {
        return 0;
    }
    if (size <= 0 || size > EAGLE_TEXT_LIMIT) {
        return eagle_fail(e, TIKU_CIRCUIT_BAD, "Invalid EAGLE text size");
    }
    if (!text || !*text) {
        return 1;
    }
    const char *align = eagle_attr(n, "align", "bottom-left");
    const char *aligns[] = {"bottom-left", "bottom-center", "bottom-right",
                            "center-left", "center", "center-right",
                            "top-left", "top-center", "top-right"};
    int a = 0, count = (int)(sizeof aligns / sizeof *aligns);

    while (a < count && strcmp(align, aligns[a])) {
        a++;
    }
    if (a == count) {
        return eagle_fail(e, TIKU_CIRCUIT_UNSUPPORTED,
                          "Unknown EAGLE text alignment");
    }
    double rad = angle * M_PI / 180, sign = mirror ? -1 : 1;
    double ax = sign * cos(rad), ay = sign * sin(rad);
    double bx = -sin(rad), by = cos(rad);
    /* Outline glyph coordinates point down, unlike EAGLE's source axes. */
    double m[] = {size * (e->m[0] * ax + e->m[2] * ay),
                  -size * (e->m[1] * ax + e->m[3] * ay),
                  -size * (e->m[0] * bx + e->m[2] * by),
                  size * (e->m[1] * bx + e->m[3] * by), 0, 0};
    circuit_point_t origin = eagle_point(e, x, y);

    m[4] = origin.x;
    m[5] = origin.y;
    /* aligns[] runs left to right, then bottom to top. */
    int justify = a % 3 - 1, vertical = a / 3 == 0 ? 1 : a / 3 == 1 ? 0 : -1;

    /* Schematic mirroring changes the anchor side, not glyph handedness. */
    if (m[0] * m[3] - m[1] * m[2] < 0) {
        m[0] = -m[0];
        m[1] = -m[1];
        justify = -justify;
    }
    /* Unspun text stays readable; it keeps its anchor and reverses its
     * alignment. */
    if (!spin && (m[0] < -1e-9 || (fabs(m[0]) < 1e-9 && m[1] > 0))) {
        for (int i = 0; i < 4; i++) {
            m[i] = -m[i];
        }
        justify = -justify;
        vertical = -vertical;
    }
    return result(e, circuit_text(e->doc, e->name, e->value, text, m, justify,
                                  vertical, 0, EAGLE_INK_FRAME));
}

/** @brief Text @p s with a leading '>' variable replaced: NAME, PART,
 *         VALUE, GATE, or an attribute of the instance, part or
 *         schematic.  @return the text; "" after a failure. */
static const char *
substitute(circuit_eagle_t *e, const char *s)
{
    if (*s != '>') {
        return s;
    }
    if (!strcmp(s, ">NAME")) {
        return e->instance ? e->display_name : e->name;
    }
    if (!strcmp(s, ">PART")) {
        return e->name;
    }
    if (!strcmp(s, ">VALUE")) {
        return e->value;
    }
    if (!strcmp(s, ">GATE")) {
        return e->gate ? e->gate : "";
    }
    eagle_node_t *parents[] = {e->instance, e->part,
                               tiku_xml_child(e->schematic, "attributes")};

    for (size_t i = 0; i < 3; i++) {
        for (eagle_node_t *n = tiku_xml_child(parents[i], "attribute"); n;
             n = tiku_xml_next(n, "attribute")) {
            if (!strcmp(eagle_attr(n, "name", ""), s + 1)) {
                return eagle_attr(n, "value", "");
            }
        }
    }
    eagle_fail(e, TIKU_CIRCUIT_UNSUPPORTED, "Unresolved EAGLE text variable");
    return "";
}

int
eagle_graphic(circuit_eagle_t *e, eagle_node_t *n)
{
    if (!eagle_step(e)) {
        return 0;
    }
    size_t start = e->doc->count;
    const char *kind = n->name;

    if (!strcmp(kind, "wire")) {
        if (strcmp(eagle_attr(n, "style", "continuous"), "continuous")
            || strcmp(eagle_attr(n, "cap", "round"), "round")) {
            return eagle_fail(e, TIKU_CIRCUIT_UNSUPPORTED,
                              "Dashed or flat-ended EAGLE wire is not "
                              "supported");
        }
        double x = number(e, n, "x1"), y = number(e, n, "y1");
        double xx = number(e, n, "x2"), yy = number(e, n, "y2");
        double w = eagle_number(e, n, "width", 0, 1);
        double curve = eagle_number(e, n, "curve", 0, 0);

        if (!line(e, x, y, xx, yy, w ? w : EAGLE_LINE_WIDTH, curve)) {
            return 0;
        }
    } else if (!strcmp(kind, "circle") || !strcmp(kind, "junction")) {
        double x = number(e, n, "x"), y = number(e, n, "y");
        double r = !strcmp(kind, "junction") ? JUNCTION_RADIUS
                                             : number(e, n, "radius");
        double width = eagle_number(e, n, "width", 0, 0);
        circuit_point_t p = eagle_point(e, x, y);

        /* A zero width fills the circle. */
        if (e->error
            || !result(e, circuit_arc(e->doc, e->name, e->value, p.x, p.y, r,
                                      0, 2 * M_PI, width, EAGLE_INK_WIRE,
                                      !width))) {
            return 0;
        }
    } else if (!strcmp(kind, "rectangle")) {
        double x = number(e, n, "x1"), y = number(e, n, "y1");
        double xx = number(e, n, "x2"), yy = number(e, n, "y2"), angle;
        int mirror, spin;

        if (!eagle_rotation(e, tiku_xml_attr(n, "rot"), &angle, &mirror,
                            &spin)
            || e->error) {
            return 0;
        }
        double cx = (x + xx) / 2, cy = (y + yy) / 2, a = angle * M_PI / 180;
        circuit_point_t p[4];

        /* The corners, turned about the centre. */
        for (int i = 0; i < 4; i++) {
            double u = (i == 0 || i == 3 ? x : xx) - cx;
            double v = (i < 2 ? y : yy) - cy;

            if (mirror) {
                u = -u;
            }
            p[i] = eagle_point(e, cx + u * cos(a) - v * sin(a),
                               cy + u * sin(a) + v * cos(a));
        }
        if (!result(e, circuit_add(e->doc, e->name, e->value, p, 4, 0,
                                   EAGLE_INK_SYMBOL, 1, 1))) {
            return 0;
        }
    } else if (!strcmp(kind, "polygon")) {
        circuit_point_t points[EAGLE_POLYGON_MAX];
        size_t count = 0;
        double width = eagle_number(e, n, "width", 0, 1);

        if (width < 0 || strcmp(eagle_attr(n, "pour", "solid"), "solid")) {
            return eagle_fail(e, TIKU_CIRCUIT_UNSUPPORTED,
                              "Only solid EAGLE schematic polygons are "
                              "supported");
        }
        for (eagle_node_t *v = tiku_xml_child(n, NULL); v;
             v = tiku_xml_next(v, NULL)) {
            if (!tiku_xml_is(v, "vertex")) {
                return eagle_fail(e, TIKU_CIRCUIT_UNSUPPORTED,
                                  "Unknown polygon element");
            }
            if (count == EAGLE_POLYGON_MAX) {
                return eagle_fail(e, TIKU_CIRCUIT_LIMIT,
                                  "EAGLE polygon exceeds 1024 vertices");
            }
            if (eagle_number(e, v, "curve", 0, 0)) {
                return eagle_fail(e, TIKU_CIRCUIT_UNSUPPORTED,
                                  "Curved EAGLE polygon edge is not "
                                  "supported");
            }
            double x = number(e, v, "x"), y = number(e, v, "y");

            points[count++] = eagle_point(e, x, y);
        }
        if (count > 1 && points[0].x == points[count - 1].x
            && points[0].y == points[count - 1].y) {
            count--;                /* the first vertex again at the end */
        }
        if (e->error
            || !result(e, circuit_contour(points, count, e->options))) {
            return 0;
        }
        if (!result(e, circuit_add(e->doc, e->name, e->value, points, count,
                                   0, EAGLE_INK_SYMBOL, 1, 1))) {
            return 0;
        }
        if (width
            && !result(e, circuit_add(e->doc, e->name, e->value, points,
                                      count, width, EAGLE_INK_SYMBOL, 1,
                                      0))) {
            return 0;
        }
    } else if (!strcmp(kind, "text")) {
        char text[EAGLE_TEXT_MAX + 1];

        if (tiku_xml_text(n, text, sizeof text) >= sizeof text) {
            return eagle_fail(e, TIKU_CIRCUIT_LIMIT,
                              "EAGLE text exceeds limit");
        }
        if (!strcmp(text, ">SHEET")) {
            snprintf(text, sizeof text, "%u/%u", e->sheet_number,
                     e->sheet_total);
        } else if (!strcmp(text, ">DRAWING_NAME")) {
            const char *base = strrchr(e->path, '/');

            snprintf(text, sizeof text, "%s", base ? base + 1 : e->path);
        }
        /* A smashed instance's NAME, VALUE and attributes are drawn by
         * the instance, where it placed them. */
        if (e->instance && text[0] == '>'
            && !strcmp(eagle_attr(e->instance, "smashed", "no"), "yes")) {
            if (!strcmp(text, ">NAME") || !strcmp(text, ">VALUE")) {
                return 1;
            }
            for (eagle_node_t *a = tiku_xml_child(e->instance, "attribute");
                 a; a = tiku_xml_next(a, "attribute")) {
                if (!strcmp(eagle_attr(a, "name", ""), text + 1)) {
                    return 1;
                }
            }
        }
        const char *value = substitute(e, text);

        if (e->error || !eagle_text(e, n, value)) {
            return 0;
        }
    } else if (!strcmp(kind, "label")) {
        if (!e->net) {
            return eagle_fail(e, TIKU_CIRCUIT_BAD,
                              "Label outside a net or bus");
        }
        if (!strcmp(eagle_attr(n, "xref", "no"), "yes")) {
            return eagle_fail(e, TIKU_CIRCUIT_UNSUPPORTED,
                              "Generated EAGLE cross-reference labels are "
                              "not supported; use ordinary net labels");
        }
        if (!eagle_text(e, n, e->net)) {
            return 0;
        }
    } else if (!strcmp(kind, "pin")) {
        return eagle_pin(e, n);
    } else if (!strcmp(kind, "frame")) {
        if (!eagle_frame(e, n)) {
            return 0;
        }
    } else if (!strcmp(kind, "hole")) {
        double x = number(e, n, "x"), y = number(e, n, "y");
        double diameter = number(e, n, "drill");
        circuit_point_t p = eagle_point(e, x, y);

        if (e->error
            || !result(e, circuit_arc(e->doc, e->name, e->value, p.x, p.y,
                                      diameter / 2, 0, 2 * M_PI,
                                      EAGLE_LINE_WIDTH, EAGLE_INK_SYMBOL,
                                      0))) {
            return 0;
        }
    } else {
        return eagle_fail(e, TIKU_CIRCUIT_UNSUPPORTED,
                          "Unsupported EAGLE drawing element");
    }
    return eagle_metadata(e, start);
}
