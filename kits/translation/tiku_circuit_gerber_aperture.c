/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_circuit_gerber_aperture.c - aperture definitions, and the flashes
 * and tracks drawn with them.  Standard apertures are read here; an AD
 * naming a macro is handed to the macro reader.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_circuit_gerber_private.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/* A D code's digits, and a standard aperture's modifiers. */
#define APERTURE_DIGITS 3
#define APERTURE_VALUES 4

int
gerber_aperture(circuit_gerber_t *g, const char *s)
{
    unsigned id = 0, digits = 0;

    while (*s >= '0' && *s <= '9') {
        if (++digits > APERTURE_DIGITS) {
            return gerber_fail(g, TIKU_CIRCUIT_LIMIT,
                               "Aperture IDs must be 10 to 255");
        }
        id = id * 10 + (*s++ - '0');
    }
    if (id < GERBER_APERTURE_FIRST || id >= GERBER_APERTURES || !g->unit
        || g->apertures[id].kind) {
        return gerber_fail(g, TIKU_CIRCUIT_BAD,
                           "Aperture requires units and a unique ID from 10 "
                           "to 255");
    }
    char kind = *s;

    if (!kind || !strchr("CROP", kind) || s[1] != ',') {
        return gerber_macro_aperture(g, &g->apertures[id], s);
    }
    s += 2;
    double p[APERTURE_VALUES] = {0};
    int n = 0;

    do {
        if (n == APERTURE_VALUES) {
            return gerber_fail(g, TIKU_CIRCUIT_UNSUPPORTED,
                               "Too many aperture modifiers");
        }
        if (!gerber_number(g, &s, &p[n++], 0)) {
            return 0;
        }
        if (*s != 'X') {
            break;
        }
        s++;
    } while (1);
    if (*s) {
        return gerber_fail(g, TIKU_CIRCUIT_BAD, "Invalid aperture modifier");
    }
    if ((kind == 'C' && n > 2) || ((kind == 'R' || kind == 'O')
                                   && (n < 2 || n > 3))
        || (kind == 'P' && n < 2)) {
        return gerber_fail(g, TIKU_CIRCUIT_UNSUPPORTED,
                           "Unsupported aperture modifier count");
    }
    gerber_aperture_t *a = &g->apertures[id];

    a->kind = kind;
    a->w = p[0] * g->unit;
    a->h = (kind == 'R' || kind == 'O') ? p[1] * g->unit : a->w;
    a->hole = (kind == 'C' ? p[1] : kind == 'P' ? p[3] : p[2]) * g->unit;
    if (kind == 'P') {
        if (p[1] < GERBER_POLYGON_MIN || p[1] > GERBER_POLYGON_MAX
            || p[1] != floor(p[1])) {
            return gerber_fail(g, TIKU_CIRCUIT_BAD,
                               "Polygon aperture needs 3 to 12 vertices");
        }
        a->vertices = (int)p[1];
        a->rotation = p[2];
    }
    double inside = fmin(a->w, a->h);

    if (kind == 'P') {
        inside *= cos(M_PI / a->vertices);  /* the inscribed circle */
    }
    /* A zero size, without a hole, is allowed: KiCad selects C,0 before
     * its regions. */
    if (a->w < 0 || a->h < 0 || a->w > GERBER_APERTURE_MAX
        || a->h > GERBER_APERTURE_MAX || a->hole < 0
        || (a->hole && a->hole >= inside)) {
        return gerber_fail(g, TIKU_CIRCUIT_UNSUPPORTED,
                           "Aperture needs dimensions of at most 1000 mm "
                           "and a hole strictly inside its outline");
    }
    return gerber_attributes(g, g->tool, a->attributes);
}

/** @brief Whether @p a covers nothing: a zero size, or a macro whose
 *         primitives cover nothing.  Its flashes and draws make no
 *         object. */
static int
empty(const gerber_aperture_t *a)
{
    return a->kind == 'M' ? !a->macro->count : !a->w || !a->h;
}

/** @brief Flash aperture @p a at (@p x, @p y), source axes, as a new
 *         object called @p name.  @return 0, or the scene's error. */
static tiku_circuit_err_t
flash(circuit_gerber_t *g, gerber_aperture_t *a, const char *name,
      double x, double y)
{
    tiku_circuit_err_t e;

    if (a->kind == 'M') {
        return circuit_group(g->doc, name, "Macro pad", a->macro, x, -y);
    }
    if (a->kind == 'C') {
        e = circuit_arc(g->doc, name, "Circular pad", x, -y, a->w / 2, 0,
                        2 * M_PI, 0, GERBER_INK, 1);
    } else if (a->kind == 'O') {
        double dx = fmax(0, a->w - a->h) / 2, dy = fmax(0, a->h - a->w) / 2;
        circuit_point_t p[] = {{x - dx, -y - dy}, {x + dx, -y + dy}};

        e = circuit_add(g->doc, name, "Obround pad", p, 2, fmin(a->w, a->h),
                        GERBER_INK, 1, 0);
        if (!e) {
            circuit_shape_t *s = &g->doc->shapes[g->doc->count - 1];

            s->kind = CIRCUIT_OBROUND;
            s->fill = 1;
            s->r = fmin(a->w, a->h) / 2;
        }
    } else {
        circuit_point_t p[GERBER_POLYGON_MAX];
        size_t count;

        if (a->kind == 'R') {
            count = 4;
            p[0] = (circuit_point_t){x - a->w / 2, -y - a->h / 2};
            p[1] = (circuit_point_t){x + a->w / 2, -y - a->h / 2};
            p[2] = (circuit_point_t){x + a->w / 2, -y + a->h / 2};
            p[3] = (circuit_point_t){x - a->w / 2, -y + a->h / 2};
        } else {
            count = (size_t)a->vertices;
            for (size_t i = 0; i < count; i++) {
                double angle = a->rotation * M_PI / 180
                               + 2 * M_PI * i / count;

                p[i] = (circuit_point_t){x + a->w / 2 * cos(angle),
                                         -y - a->w / 2 * sin(angle)};
            }
        }
        e = circuit_add(g->doc, name,
                        a->kind == 'R' ? "Rectangular pad" : "Polygon pad",
                        p, count, 0, GERBER_INK, 1, 1);
    }
    if (!e) {
        circuit_shape_t *s = &g->doc->shapes[g->doc->count - 1];

        s->cx = x;
        s->cy = -y;
        s->hole = a->hole;
    }
    return e;
}

int
gerber_draw(circuit_gerber_t *g, int op, double x, double y, double i,
            double j)
{
    gerber_aperture_t *a = &g->apertures[g->aperture];
    double arc[6];

    if (!a->kind) {
        return gerber_fail(g, TIKU_CIRCUIT_BAD,
                           "Draw or flash without a selected aperture");
    }
    if (op != 3) {
        if (a->kind != 'C' || a->hole) {
            return gerber_fail(g, TIKU_CIRCUIT_UNSUPPORTED,
                               "Tracks require a circular aperture without "
                               "a hole");
        }
        if (g->mode != 1 && !gerber_arc(g, x, y, i, j, arc)) {
            return 0;
        }
    }
    if (empty(a)) {
        return 1;
    }
    char name[64], description[320];

    snprintf(name, sizeof name, "D%d %s %zu", g->aperture,
             op == 3 ? "pad" : "track", g->doc->count + 1);
    tiku_circuit_err_t e;

    if (op == 3) {
        e = flash(g, a, name, x, y);
    } else if (g->mode == 1) {
        circuit_point_t points[] = {{g->x, -g->y}, {x, -y}};

        if (x == g->x && y == g->y) {
            e = flash(g, a, name, x, y);        /* a zero-length draw */
        } else {
            e = circuit_add(g->doc, name, "Straight track", points, 2, a->w,
                            GERBER_INK, 0, 0);
        }
    } else {
        e = circuit_arc(g->doc, name, "Circular track", arc[0], arc[1],
                        fmax(arc[2], arc[3]), arc[4], arc[5], a->w,
                        GERBER_INK, 0);
        if (!e) {
            circuit_shape_t *s = &g->doc->shapes[g->doc->count - 1];

            s->r = arc[2];
            s->end_r = arc[3];
        }
    }
    if (e) {
        return gerber_fail(g, e, "Cannot construct Gerber geometry");
    }
    if (a->kind == 'M') {
        snprintf(description, sizeof description,
                 "Aperture: D%d (macro %.127s)\n"
                 "Bounds size: %.8g x %.8g mm\n"
                 "Geometry: isolated macro flash; %zu primitives\n"
                 "Position: X %.8g, Y %.8g mm (source axes)",
                 g->aperture, a->macro_name, a->w, a->h, a->macro->count,
                 x, y);
    } else {
        snprintf(description, sizeof description,
                 "Aperture: D%d (%c)\n"
                 "Size: %.8g x %.8g mm\n"
                 "Transparent hole: %.8g mm\n"
                 "Geometry: %s\n"
                 "Position/end: X %.8g, Y %.8g mm (source axes)",
                 g->aperture, a->kind, a->w, a->h, a->hole,
                 op == 3 ? "flash" : "track", x, y);
    }
    return gerber_details(g, description, a->attributes);
}
