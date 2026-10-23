/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_circuit_artwork.c - the renderer's drawing helpers.  An aperture
 * hole is a contour of its own flash, through which earlier artwork
 * shows; a clear operation takes coverage away within its own layer.
 * Paths reach the rasteriser through a pen that clips them to the
 * viewport.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_circuit_artwork.h"

#include <math.h>
#include <string.h>

#define OUTLINE_PIECES_MIN 4    /* segments in each half of a small outline */

/** @brief Append the closed capsule of radius @p r whose ends are centred
 *         on (@p x + @p dx, @p y + @p dy) and (@p x - @p dx, @p y - @p dy):
 *         a circle when both are zero. */
static int
outline(circuit_clip_t *p, const tiku_circuit_view_t *v, double x,
        double y, double r, double dx, double dy)
{
    double radius = r * v->scale;
    double step = radius <= CIRCUIT_CURVE_ERROR
                  ? CIRCUIT_COARSE_STEP
                  : 2 * acos(1 - CIRCUIT_CURVE_ERROR / radius);
    double pieces = ceil(M_PI / step);

    if (!isfinite(pieces) || pieces >= CIRCUIT_POINTS_MAX / 2) {
        return TIKU_VECTOR_LIMIT;
    }
    size_t n = (size_t)fmax(OUTLINE_PIECES_MIN, pieces);
    double direction = atan2(dy, dx);

    for (int side = 0; side < 2; side++) {
        for (size_t j = 0; j <= n; j++) {
            double a = direction - M_PI / 2 + side * M_PI + M_PI * j / n;
            double sign = side ? -1 : 1;
            double px = (x + sign * dx + r * cos(a)) * v->scale + v->x;
            double py = (y + sign * dy + r * sin(a)) * v->scale + v->y;

            if (!side && !j) {
                circuit_clip_move(p, px, py);
            } else {
                circuit_clip_line(p, px, py);
            }
        }
    }
    circuit_clip_close(p);
    return p->path->failed;
}

int
circuit_artwork_outline(const circuit_shape_t *s,
                        const tiku_circuit_view_t *v, circuit_clip_t *p)
{
    int error = 0;

    if (s->kind == CIRCUIT_OBROUND) {
        circuit_clip_clear(p);
        double dx = (s->points[1].x - s->points[0].x) / 2;
        double dy = (s->points[1].y - s->points[0].y) / 2;

        error = outline(p, v, s->cx, s->cy, s->r, dx, dy);
    }
    if (!error && s->hole) {
        error = outline(p, v, s->cx, s->cy, s->hole / 2, 0, 0);
    }
    return error;
}

void
circuit_artwork_apply(unsigned char *coverage, int width,
                      const tiku_vector_mask_t *m, int clear)
{
    for (int y = 0; y < m->h; y++) {
        for (int x = 0; x < m->w; x++) {
            unsigned char *dst = &coverage[(size_t)(y + m->y0) * width +
                                           x + m->x0];
            unsigned a = m->a[(size_t)y * m->w + x];

            *dst = (unsigned char)(clear
                                   ? (*dst * (255 - a) + 127) / 255
                                   : a + (*dst * (255 - a) + 127) / 255);
        }
    }
}

void
circuit_artwork_colour(tiku_image_t *im, const unsigned char *coverage)
{
    const uint32_t ink = CIRCUIT_COPPER, background = CIRCUIT_PAPER;

    for (size_t i = 0; i < (size_t)im->w * im->h; i++) {
        unsigned a = coverage[i];
        uint32_t pixel = CIRCUIT_OPAQUE;

        for (int shift = 0; shift <= 16; shift += 8) {
            pixel |= ((((background >> shift) & 255) * (255 - a) +
                       ((ink >> shift) & 255) * a + 127) / 255) << shift;
        }
        im->px[i] = pixel;
    }
}

/** @brief Whether (@p x, @p y) is on the inner side of the pen's edge
 *         @p edge: 0 left, 1 right, 2 top, 3 bottom. */
static int
inner(const circuit_clip_t *c, int edge, double x, double y)
{
    return edge == 0 ? x >= c->x0 : edge == 1 ? x <= c->x1
           : edge == 2 ? y >= c->y0 : y <= c->y1;
}

/** @brief Where the segment from (@p ax, @p ay) to (@p bx, @p by), which
 *         crosses edge @p edge, meets it. */
static void
crossing(const circuit_clip_t *c, int edge, double ax, double ay,
         double bx, double by, double *x, double *y)
{
    if (edge < 2) {
        double e = edge ? c->x1 : c->x0;

        *x = e;
        *y = ay + (e - ax) / (bx - ax) * (by - ay);
    } else {
        double e = edge == 3 ? c->y1 : c->y0;

        *x = ax + (e - ay) / (by - ay) * (bx - ax);
        *y = e;
    }
}

static void stage_point(circuit_clip_t *c, int edge, double x, double y);

/** @brief Pass a point out of edge @p edge's stage: to the next stage,
 *         or after the last into the path. */
static void
emit(circuit_clip_t *c, int edge, double x, double y)
{
    if (edge < 3) {
        stage_point(c, edge + 1, x, y);
    } else if (!c->drawn++) {
        tiku_vector_path_moveto(c->path, x, y);
    } else {
        tiku_vector_path_lineto(c->path, x, y);
    }
}

/** @brief Clip one polygon vertex against edge @p edge (Sutherland and
 *         Hodgman), one edge at a time, as the vertices arrive. */
static void
stage_point(circuit_clip_t *c, int edge, double x, double y)
{
    circuit_clip_stage_t *st = &c->stage[edge];
    int in = inner(c, edge, x, y);

    if (!st->started) {
        st->started = 1;
        st->fx = x;
        st->fy = y;
    } else if (inner(c, edge, st->px, st->py) != in) {
        double cx, cy;

        crossing(c, edge, st->px, st->py, x, y, &cx, &cy);
        emit(c, edge, cx, cy);
    }
    if (in) {
        emit(c, edge, x, y);
    }
    st->px = x;
    st->py = y;
}

/** @brief Close the polygon at edge @p edge's stage and every stage after
 *         it, then close the path's contour. */
static void
stage_close(circuit_clip_t *c, int edge)
{
    circuit_clip_stage_t *st = &c->stage[edge];

    if (st->started && inner(c, edge, st->px, st->py) !=
                       inner(c, edge, st->fx, st->fy)) {
        double cx, cy;

        crossing(c, edge, st->px, st->py, st->fx, st->fy, &cx, &cy);
        emit(c, edge, cx, cy);
    }
    st->started = 0;
    if (edge < 3) {
        stage_close(c, edge + 1);
    } else if (c->drawn) {
        tiku_vector_path_close(c->path);
        c->drawn = 0;
    }
}

/** @brief The part of the segment from (@p ax, @p ay) to (@p bx, @p by)
 *         inside the rectangle (Liang and Barsky), as the parameters
 *         @p t0 to @p t1 along it.  @return 1, or 0 when none is */
static int
segment(const circuit_clip_t *c, double ax, double ay, double bx,
        double by, double *t0, double *t1)
{
    double dx = bx - ax, dy = by - ay;
    const double p[] = {-dx, dx, -dy, dy};
    const double q[] = {ax - c->x0, c->x1 - ax, ay - c->y0, c->y1 - ay};
    double lo = 0, hi = 1;

    for (int i = 0; i < 4; i++) {
        if (p[i] == 0) {
            if (q[i] < 0) {
                return 0;
            }
        } else if (p[i] < 0) {
            lo = fmax(lo, q[i] / p[i]);
        } else {
            hi = fmin(hi, q[i] / p[i]);
        }
    }
    *t0 = lo;
    *t1 = hi;
    return lo <= hi;
}

/** @brief Add the visible part of the line from the pen's point to
 *         (@p x, @p y), starting a new piece where the line comes back
 *         into the rectangle. */
static void
line_piece(circuit_clip_t *c, double x, double y)
{
    double t0, t1;

    if (!segment(c, c->px, c->py, x, y, &t0, &t1)) {
        c->pen = 0;
        c->cut = 1;
    } else {
        double ax = t0 > 0 ? c->px + t0 * (x - c->px) : c->px;
        double ay = t0 > 0 ? c->py + t0 * (y - c->py) : c->py;
        double bx = t1 < 1 ? c->px + t1 * (x - c->px) : x;
        double by = t1 < 1 ? c->py + t1 * (y - c->py) : y;

        if (!c->pen || t0 > 0) {
            tiku_vector_path_moveto(c->path, ax, ay);
        }
        tiku_vector_path_lineto(c->path, bx, by);
        c->pen = t1 >= 1;
        if (t0 > 0 || t1 < 1) {
            c->cut = 1;
        }
    }
    c->px = x;
    c->py = y;
}

void
circuit_clip_start(circuit_clip_t *c, tiku_vector_path_t *path,
                   double margin, int width, int height, int area, int pass)
{
    *c = (circuit_clip_t){.path = path, .x0 = -margin, .y0 = -margin,
                          .x1 = width + margin, .y1 = height + margin,
                          .area = area, .pass = pass};
}

void
circuit_clip_clear(circuit_clip_t *c)
{
    tiku_vector_path_clear(c->path);
    memset(c->stage, 0, sizeof c->stage);
    c->drawn = 0;
    c->pen = 0;
    c->cut = 0;
}

void
circuit_clip_move(circuit_clip_t *c, double x, double y)
{
    if (c->pass) {
        tiku_vector_path_moveto(c->path, x, y);
    } else if (c->area) {
        circuit_clip_end(c);
        stage_point(c, 0, x, y);
    } else {
        c->fx = c->px = x;
        c->fy = c->py = y;
        c->pen = x >= c->x0 && x <= c->x1 && y >= c->y0 && y <= c->y1;
        c->cut = !c->pen;
        if (c->pen) {
            tiku_vector_path_moveto(c->path, x, y);
        }
    }
}

void
circuit_clip_line(circuit_clip_t *c, double x, double y)
{
    if (c->pass) {
        tiku_vector_path_lineto(c->path, x, y);
    } else if (c->area) {
        stage_point(c, 0, x, y);
    } else {
        line_piece(c, x, y);
    }
}

void
circuit_clip_close(circuit_clip_t *c)
{
    if (c->pass || (!c->area && !c->cut)) {
        tiku_vector_path_close(c->path);
    } else if (c->area) {
        stage_close(c, 0);
    } else {
        line_piece(c, c->fx, c->fy);
    }
}

void
circuit_clip_end(circuit_clip_t *c)
{
    if (!c->pass && c->area && c->stage[0].started) {
        stage_close(c, 0);
    }
}
