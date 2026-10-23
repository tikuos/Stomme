/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_office_gdi_shape.c - GDI's shapes as paths: rectangles, round
 * ones, ellipses and their arcs, chords and pies, polylines, polygons
 * and Béziers, PolyDraw's mixture, and GradientFill's boxes.
 *
 * Curves are made in logical units and mapped with their points, so a
 * turned or sheared world turns and shears them too.  An arc goes the
 * way SetArcDirection says as it is seen, whichever way the mapping
 * turns the logical y axis.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_office_gdi.h"

#include <math.h>
#include <string.h>

#define PI 3.14159265358979323846

/** @brief A figure begun at the current position, unless one is open. */
static void
from_here(gdi_t *g)
{
    if (!g->fig || g->npt == 0) {
        gdi_move(g, g->dc.x, g->dc.y);
    }
}

void
gdi_rect(gdi_t *g, double l, double t, double r, double b)
{
    gdi_move(g, l, t);
    gdi_line(g, r, t);
    gdi_line(g, r, b);
    gdi_line(g, l, b);
    gdi_close(g);
    gdi_paint(g, GDI_FILL | GDI_STROKE);
}

/** @brief The ellipse centred (@p cx, @p cy), radii @p rx, @p ry, from
 *         angle @p t0 through @p sweep radians, as Béziers of a quarter
 *         turn at most.
 *  @note The path is already at the arc's start. */
static void
arc_path(gdi_t *g, double cx, double cy, double rx, double ry, double t0,
         double sweep)
{
    int i, n = (int)ceil(fabs(sweep) / (PI / 2) - 1e-9);
    double d, k, a, b, p[6];

    n = n < 1 ? 1 : n;
    d = sweep / n;
    k = 4.0 / 3.0 * tan(d / 4);
    for (i = 0; i < n; i++) {
        a = t0 + d * i;
        b = a + d;
        p[0] = cx + rx * (cos(a) - k * sin(a));
        p[1] = cy + ry * (sin(a) + k * cos(a));
        p[2] = cx + rx * (cos(b) + k * sin(b));
        p[3] = cy + ry * (sin(b) - k * cos(b));
        p[4] = cx + rx * cos(b);
        p[5] = cy + ry * sin(b);
        gdi_curve(g, p);
    }
}

void
gdi_ellipse(gdi_t *g, double l, double t, double r, double b)
{
    double rx = fabs(r - l) / 2, ry = fabs(b - t) / 2;
    double cx = (l + r) / 2, cy = (t + b) / 2;

    gdi_move(g, cx + rx, cy);
    arc_path(g, cx, cy, rx, ry, 0, 2 * PI);
    gdi_close(g);
    gdi_paint(g, GDI_FILL | GDI_STROKE);
}

void
gdi_round(gdi_t *g, const double *box, double w, double h)
{
    double l = fmin(box[0], box[2]), r = fmax(box[0], box[2]);
    double t = fmin(box[1], box[3]), b = fmax(box[1], box[3]);
    double rx = fmin(fabs(w) / 2, (r - l) / 2);
    double ry = fmin(fabs(h) / 2, (b - t) / 2);

    gdi_move(g, r - rx, t);
    arc_path(g, r - rx, t + ry, rx, ry, -PI / 2, PI / 2);
    gdi_line(g, r, b - ry);
    arc_path(g, r - rx, b - ry, rx, ry, 0, PI / 2);
    gdi_line(g, l + rx, b);
    arc_path(g, l + rx, b - ry, rx, ry, PI / 2, PI / 2);
    gdi_line(g, l, t + ry);
    arc_path(g, l + rx, t + ry, rx, ry, PI, PI / 2);
    gdi_close(g);
    gdi_paint(g, GDI_FILL | GDI_STROKE);
}

/** @brief Whether the mapping keeps the logical axes turning the way the
 *         box's do (x right, y down): its determinant is positive. */
static int
kept(gdi_t *g)
{
    double x0, y0, x1, y1, x2, y2;

    gdi_map(g, 0, 0, &x0, &y0);
    gdi_map(g, 1, 0, &x1, &y1);
    gdi_map(g, 0, 1, &x2, &y2);
    return (x1 - x0) * (y2 - y0) - (y1 - y0) * (x2 - x0) >= 0;
}

void
gdi_arc(gdi_t *g, const double *box, const double *p, const double *q,
        int kind)
{
    double cx = (box[0] + box[2]) / 2, cy = (box[1] + box[3]) / 2;
    double rx = fabs(box[2] - box[0]) / 2, ry = fabs(box[3] - box[1]) / 2;
    double t0, t1, sweep;
    int up;

    if (rx <= 0 || ry <= 0) {
        return;
    }
    t0 = atan2((p[1] - cy) / ry, (p[0] - cx) / rx);
    t1 = atan2((q[1] - cy) / ry, (q[0] - cx) / rx);
    up = (g->dc.arcdir == 2) == kept(g);    /* angles grow as it goes */
    sweep = t1 - t0;
    if (up && sweep <= 0) {
        sweep += 2 * PI;
    } else if (!up && sweep >= 0) {
        sweep -= 2 * PI;
    }
    if (kind == 1) {                /* ArcTo: a line to its start first */
        from_here(g);
        gdi_line(g, cx + rx * cos(t0), cy + ry * sin(t0));
    } else if (kind == 3) {         /* a pie: from the centre */
        gdi_move(g, cx, cy);
        gdi_line(g, cx + rx * cos(t0), cy + ry * sin(t0));
    } else {
        gdi_move(g, cx + rx * cos(t0), cy + ry * sin(t0));
    }
    arc_path(g, cx, cy, rx, ry, t0, sweep);
    if (kind == 1) {
        g->dc.x = cx + rx * cos(t1);
        g->dc.y = cy + ry * sin(t1);
    }
    if (kind >= 2) {
        gdi_close(g);
    }
    gdi_paint(g, kind >= 2 ? GDI_FILL | GDI_STROKE : GDI_STROKE);
}

void
gdi_angle_arc(gdi_t *g, double x, double y, double r, double from,
              double sweep)
{
    double t0 = -from * PI / 180, t = -sweep * PI / 180;

    r = fabs(r);
    from_here(g);
    gdi_line(g, x + r * cos(t0), y + r * sin(t0));
    if (r > 0 && t != 0) {
        arc_path(g, x, y, r, r, t0, fmax(-4 * PI, fmin(4 * PI, t)));
    }
    g->dc.x = x + r * cos(t0 + t);
    g->dc.y = y + r * sin(t0 + t);
    gdi_paint(g, GDI_STROKE);
}

void
gdi_poly(gdi_t *g, const double *p, int n, int flags, int to)
{
    int i;

    if (n <= 0) {
        return;
    }
    if (to) {
        from_here(g);
    } else {
        gdi_move(g, p[0], p[1]);
    }
    for (i = to ? 0 : 1; i < n; i++) {
        gdi_line(g, p[2 * i], p[2 * i + 1]);
    }
    if (to) {
        g->dc.x = p[2 * n - 2];
        g->dc.y = p[2 * n - 1];
    }
    if (flags & GDI_SHUT) {
        gdi_close(g);
    }
    if (!(flags & GDI_MORE)) {
        gdi_paint(g, flags & GDI_SHUT ? GDI_FILL | GDI_STROKE : GDI_STROKE);
    }
}

void
gdi_beziers(gdi_t *g, const double *p, int n, int to)
{
    int i = 0;

    if (n <= 0) {
        return;
    }
    if (to) {
        from_here(g);
    } else {
        gdi_move(g, p[0], p[1]);
        i = 1;
    }
    for (; i + 3 <= n; i += 3) {
        gdi_curve(g, p + 2 * i);
    }
    if (to && i > 0) {
        g->dc.x = p[2 * i - 2];
        g->dc.y = p[2 * i - 1];
    }
    gdi_paint(g, GDI_STROKE);
}

void
gdi_polydraw(gdi_t *g, const double *p, const unsigned char *types, int n)
{
    int i, kind;

    for (i = 0; i < n; i++) {
        kind = types[i] & ~1;
        if (kind == 6) {            /* PT_MOVETO */
            gdi_move(g, p[2 * i], p[2 * i + 1]);
        } else if (kind == 4 && i + 2 < n) {    /* PT_BEZIERTO */
            from_here(g);
            gdi_curve(g, p + 2 * i);
            i += 2;
        } else {                    /* PT_LINETO */
            from_here(g);
            gdi_line(g, p[2 * i], p[2 * i + 1]);
        }
        g->dc.x = p[2 * i];
        g->dc.y = p[2 * i + 1];
        if (types[i] & 1) {         /* PT_CLOSEFIGURE */
            gdi_close(g);
        }
    }
    gdi_paint(g, GDI_STROKE);
}

void
gdi_gradient(gdi_t *g, const double *r, uint32_t c0, uint32_t c1,
             int vertical)
{
    tiku_pdf_gradient_t gr;
    tiku_pdf_stop_t s[2];
    double my = (r[1] + r[3]) / 2, mx = (r[0] + r[2]) / 2;

    memset(&gr, 0, sizeof gr);
    s[0].at = 0;
    s[0].argb = c0;
    s[1].at = 1;
    s[1].argb = c1;
    gr.stop = s;
    gr.nstop = 2;
    gdi_map(g, vertical ? mx : r[0], vertical ? r[1] : my, &gr.x0, &gr.y0);
    gdi_map(g, vertical ? mx : r[2], vertical ? r[3] : my, &gr.x1, &gr.y1);
    gdi_move(g, r[0], r[1]);
    gdi_line(g, r[2], r[1]);
    gdi_line(g, r[2], r[3]);
    gdi_line(g, r[0], r[3]);
    gdi_close(g);
    gdi_paint_gradient(g, &gr);
}
