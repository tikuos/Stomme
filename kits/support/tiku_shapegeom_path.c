/*
 * KintsugiOS v0.01 kits -- the foundation KintsugiDesktop and KintsugiTracker.
 *
 * Authors: Ambuj Varshney <ambuj@kinstugi-os.org>
 *
 * tiku_shapegeom_path.c - a geometry's path program run into an outline:
 * moves, lines, arcs and Béziers in doubles and EMUs, stored as float
 * points.  After a close the pen is back at the subpath's start, and a
 * drawing operation with no subpath open begins one where the pen is.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_shapegeom_private.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/** @brief A path being built, and where the pen is. */
typedef struct {
    tiku_shapegeom_path_t *path; /* the open path, or NULL             */
    size_t path_cap, op_cap, point_cap;
    double x, y;   /* the pen, EMUs                      */
    double x0, y0; /* the open subpath's start           */
    double sx, sy; /* path units to EMUs                 */
    int open;      /* a subpath has begun since M or Z   */
} pen_t;

/** @brief Array @p a grown to hold @p need items of @p size, doubling
 *         from @p *cap.  @return the array, or @p a unchanged with *@p bad
 *         set when memory ran out. */
static void *
grow(void *a, size_t *cap, size_t need, size_t size, int *bad)
{
    size_t c = *cap ? *cap : 16;
    void *p;

    if (need <= *cap) {
        return a;
    }
    while (c < need) {
        c *= 2;
    }
    p = realloc(a, c * size);
    if (p == NULL) {
        *bad = 1;
        return a;
    }
    *cap = c;
    return p;
}

/** @brief A new path in @p out, filled and stroked as given, its
 *         coordinates scaled by @p sx and @p sy. */
static void
open_path(env_t *e, tiku_shapegeom_outline_t *out, pen_t *pen, int fill,
          int stroke)
{
    int bad = 0;

    out->paths = grow(out->paths, &pen->path_cap, out->path_count + 1,
                      sizeof *out->paths, &bad);
    if (bad) {
        sg_refuse(e, TIKU_SHAPEGEOM_NO_MEMORY);
        return;
    }
    pen->path = &out->paths[out->path_count++];
    memset(pen->path, 0, sizeof *pen->path);
    pen->path->fill = (unsigned char)fill;
    pen->path->stroke = (unsigned char)stroke;
    pen->op_cap = pen->point_cap = 0;
    pen->open = 0;
}

/** @brief Operation @p op with its @p n points (EMUs) added to the open
 *         path; the pen moves to the last. */
static void
emit(env_t *e, pen_t *pen, int op, const double *xy, size_t n)
{
    tiku_shapegeom_path_t *p = pen->path;
    int bad = 0;
    size_t i;

    if (e->status != TIKU_SHAPEGEOM_OK) {
        return;
    }
    if (n > e->max_points - e->points) {
        sg_refuse(e, TIKU_SHAPEGEOM_LIMIT);
        return;
    }
    p->ops = grow(p->ops, &pen->op_cap, p->op_count + 1, 1, &bad);
    p->points = grow(p->points, &pen->point_cap, (p->point_count + n) * 2,
                     sizeof *p->points, &bad);
    if (bad) {
        sg_refuse(e, TIKU_SHAPEGEOM_NO_MEMORY);
        return;
    }
    p->ops[p->op_count++] = (unsigned char)op;
    for (i = 0; i < n * 2; i++) {
        p->points[p->point_count * 2 + i] = sg_scaled(e, xy[i], EMU);
    }
    p->point_count += n;
    e->points += n;
    if (n > 0) {
        pen->x = xy[n * 2 - 2];
        pen->y = xy[n * 2 - 1];
    }
}

/** @brief Before a drawing operation: a subpath begun where the pen is,
 *         when a close or the path's start left none open. */
static void
begin(env_t *e, pen_t *pen)
{
    double xy[2] = {pen->x, pen->y};

    if (!pen->open) {
        emit(e, pen, TIKU_SHAPEGEOM_MOVE, xy, 1);
        pen->x0 = pen->x;
        pen->y0 = pen->y;
        pen->open = 1;
    }
}

/** @brief Visual angle @p a (radians) on an ellipse of radii @p wr and
 *         @p hr as the parametric angle of the same point, continuous in
 *         @p a: the two agree on the axes and differ by under a quarter
 *         turn between them. */
static double
param(double wr, double hr, double a)
{
    double d = atan2(wr * sin(a), hr * cos(a)) - a;

    while (d > PI) {
        d -= 2 * PI;
    }
    while (d < -PI) {
        d += 2 * PI;
    }
    return a + d;
}

/**
 * @brief An arc from the pen on the ellipse of radii @p r[0] and @p r[1]
 *        in path units, from visual angle @p st through @p sw (formula
 *        units), as cubic Béziers of at most a quarter turn each.  The
 *        angles are the path's own: they are made parametric on its
 *        ellipse before the path is scaled to the shape.
 */
static void
arc(env_t *e, pen_t *pen, const double *r, double st, double sw)
{
    double wr = r[0] * pen->sx, hr = r[1] * pen->sy;
    double t0, t1, cx, cy, step, k;
    int n, i;

    if (wr <= 0 || hr <= 0 || sw == 0) {
        return; /* a point: the pen stays */
    }
    begin(e, pen);
    t0 = param(r[0], r[1], st / ANGLE * DEG);
    t1 = param(r[0], r[1], (st + sw) / ANGLE * DEG);
    cx = pen->x - wr * cos(t0);
    cy = pen->y - hr * sin(t0);
    /* quarter turns at most, 1 to 64 of them */
    n = (int)fmin(64, fmax(1, ceil(fabs(t1 - t0) / (PI / 2) - 1e-9)));
    step = (t1 - t0) / n;
    k = 4.0 / 3.0 * tan(step / 4);
    for (i = 0; i < n; i++) {
        double a = t0 + step * i, b = i == n - 1 ? t1 : a + step, c[6];

        c[0] = cx + wr * (cos(a) - k * sin(a));
        c[1] = cy + hr * (sin(a) + k * cos(a));
        c[2] = cx + wr * (cos(b) + k * sin(b));
        c[3] = cy + hr * (sin(b) - k * cos(b));
        c[4] = cx + wr * cos(b);
        c[5] = cy + hr * sin(b);
        emit(e, pen, TIKU_SHAPEGEOM_CUBIC, c, 3);
    }
}

/** @brief A quadratic Bézier from the pen, as the cubic it is. */
static void
quad(env_t *e, pen_t *pen, const double *v)
{
    double c[6];

    begin(e, pen);
    c[0] = pen->x + (v[0] - pen->x) * 2 / 3;
    c[1] = pen->y + (v[1] - pen->y) * 2 / 3;
    c[2] = v[2] + (v[0] - v[2]) * 2 / 3;
    c[3] = v[3] + (v[1] - v[3]) * 2 / 3;
    c[4] = v[2];
    c[5] = v[3];
    emit(e, pen, TIKU_SHAPEGEOM_CUBIC, c, 3);
}

/** @brief "P fill stroke w h": a new path for a @p w by @p h EMU shape. */
static void
path_header(env_t *e, tiku_shapegeom_outline_t *out, pen_t *pen, const char **p,
            const char *end, double w, double h)
{
    double fill = sg_next_value(e, p, end), stroke = sg_next_value(e, p, end);
    double pw = sg_next_value(e, p, end), ph = sg_next_value(e, p, end);

    if ((fill != floor(fill) || fill < TIKU_SHAPEGEOM_PATH_NO_FILL ||
         fill > TIKU_SHAPEGEOM_PATH_DARKEN_LESS ||
         (stroke != 0 && stroke != 1) || pw < 0 || ph < 0) &&
        !sg_fault(e)) {
        return;
    }
    fill = fill < 0 || fill > TIKU_SHAPEGEOM_PATH_DARKEN_LESS ? 1 : fill;
    open_path(e, out, pen, (int)fill, stroke != 0);
    pen->sx = pw > 0 ? w / pw : 1;
    pen->sy = ph > 0 ? h / ph : 1;
}

/** @brief The next @p n values of the program, scaled pairwise from path
 *         units to EMUs, into @p v. */
static void
coords(env_t *e, const pen_t *pen, const char **p, const char *end, double *v,
       int n)
{
    int i;

    for (i = 0; i < n; i++) {
        v[i] = sg_next_value(e, p, end) * (i % 2 ? pen->sy : pen->sx);
    }
}

/** @brief One drawing operation @p op of the program, its operands read
 *         from @p p. */
static void
draw_op(env_t *e, pen_t *pen, char op, const char **p, const char *end)
{
    double v[6] = {0, 0, 0, 0, 0, 0};

    switch (op) {
    case 'M':
        coords(e, pen, p, end, v, 2);
        emit(e, pen, TIKU_SHAPEGEOM_MOVE, v, 1);
        pen->x0 = v[0];
        pen->y0 = v[1];
        pen->open = 1;
        break;
    case 'L':
        coords(e, pen, p, end, v, 2);
        begin(e, pen);
        emit(e, pen, TIKU_SHAPEGEOM_LINE, v, 1);
        break;
    case 'A':
        v[0] = sg_next_value(e, p, end);
        v[1] = sg_next_value(e, p, end);
        v[2] = sg_next_value(e, p, end);
        v[3] = sg_next_value(e, p, end);
        arc(e, pen, v, v[2], v[3]);
        break;
    case 'Q':
        coords(e, pen, p, end, v, 4);
        quad(e, pen, v);
        break;
    case 'C':
        coords(e, pen, p, end, v, 6);
        begin(e, pen);
        emit(e, pen, TIKU_SHAPEGEOM_CUBIC, v, 3);
        break;
    default: /* 'Z' */
        if (pen->open) {
            emit(e, pen, TIKU_SHAPEGEOM_CLOSE, v, 0);
            pen->x = pen->x0;
            pen->y = pen->y0;
            pen->open = 0;
        }
        break;
    }
}

void
sg_run_paths(env_t *e, tiku_shapegeom_outline_t *out, const char *prog,
             double w, double h)
{
    const char *end = prog + strlen(prog);
    pen_t pen;
    tok_t op;

    memset(&pen, 0, sizeof pen);
    while (e->status == TIKU_SHAPEGEOM_OK && sg_next(&prog, end, &op)) {
        if (op.n != 1 || strchr("PMLAQCZ", op.s[0]) == NULL) {
            if (!sg_fault(e)) {
                return;
            }
            continue; /* lenient: an unknown word is skipped */
        }
        if (op.s[0] == 'P') {
            path_header(e, out, &pen, &prog, end, w, h);
            continue;
        }
        if (pen.path == NULL) {
            if (!sg_fault(e)) {
                return;
            }
            open_path(e, out, &pen, TIKU_SHAPEGEOM_PATH_FILL, 1);
            pen.sx = pen.sy = 1;
        }
        draw_op(e, &pen, op.s[0], &prog, end);
    }
}
