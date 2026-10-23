/*
 * KintsugiOS v0.01 kits -- the foundation KintsugiDesktop and KintsugiTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@kinstugi-os.org>
 *
 * tiku_painter.c - the painter's state: transforms, saved states, the
 * path built in device pixels, clips, fills and strokes.  Coverage goes
 * to tiku_painter_fill.c to be coloured and laid over the surface.
 *
 * A path is mapped to device pixels as it is built, so a curve is
 * flattened at the size it is drawn.  A stroke is outlined in device
 * pixels at the width the transform's area scale gives.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_painter_private.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

tiku_affine_t
tiku_affine_identity(void)
{
    tiku_affine_t m = {1, 0, 0, 1, 0, 0};

    return m;
}

tiku_affine_t
tiku_affine_mul(const tiku_affine_t *m, const tiku_affine_t *n)
{
    tiku_affine_t r;

    r.a = m->a * n->a + m->c * n->b;
    r.b = m->b * n->a + m->d * n->b;
    r.c = m->a * n->c + m->c * n->d;
    r.d = m->b * n->c + m->d * n->d;
    r.e = m->a * n->e + m->c * n->f + m->e;
    r.f = m->b * n->e + m->d * n->f + m->f;
    return r;
}

int
tiku_affine_invert(const tiku_affine_t *m, tiku_affine_t *out)
{
    double det = m->a * m->d - m->b * m->c;

    if (det == 0 || !isfinite(det)) {
        return 0;
    }
    out->a = m->d / det;
    out->b = -m->b / det;
    out->c = -m->c / det;
    out->d = m->a / det;
    out->e = -(out->a * m->e + out->c * m->f);
    out->f = -(out->b * m->e + out->d * m->f);
    return 1;
}

void
tiku_affine_apply(const tiku_affine_t *m, double x, double y, double *ox,
                  double *oy)
{
    *ox = m->a * x + m->c * y + m->e;
    *oy = m->b * x + m->d * y + m->f;
}

tiku_paint_t
tiku_paint_solid(uint32_t argb)
{
    tiku_paint_t paint;

    memset(&paint, 0, sizeof paint);
    paint.kind = TIKU_PAINT_SOLID;
    paint.argb = argb;
    return paint;
}

void
tp_fail(tiku_painter_t *p, int status)
{
    if (p->status == TIKU_PAINTER_OK) {
        p->status = status;
    }
}

tiku_affine_t
tp_device(const tiku_painter_t *p)
{
    tiku_affine_t s = {p->scale, 0, 0, p->scale, 0, 0};

    return tiku_affine_mul(&s, &p->st.m);
}

/** @brief The clip box of surface @p s, in device pixels, into @p st. */
static void
surface_box(const tiku_surface_t *s, int scale, tiku_pstate_t *st)
{
    st->x0 = s->clip.x * scale;
    st->y0 = s->clip.y * scale;
    st->x1 = (s->clip.x + s->clip.w) * scale;
    st->y1 = (s->clip.y + s->clip.h) * scale;
}

tiku_painter_t *
tiku_painter_new(tiku_surface_t *s)
{
    tiku_painter_t *p;

    if (s == NULL || (p = calloc(1, sizeof *p)) == NULL) {
        return NULL;
    }
    p->s = s;
    p->scale = s->scale > 0 ? s->scale : 1;
    p->st.m = tiku_affine_identity();
    p->st.alpha = 1;
    surface_box(s, p->scale, &p->st);
    tiku_vector_path_init(&p->path);
    p->path.strict = 1;
    return p;
}

void
tiku_painter_free(tiku_painter_t *p)
{
    if (p == NULL) {
        return;
    }
    while (p->nsaved > 0) {
        tiku_vector_mask_free(&p->saved[--p->nsaved].clip);
    }
    free(p->saved);
    tiku_vector_mask_free(&p->st.clip);
    tiku_vector_path_free(&p->path);
    free(p);
}

tiku_surface_t *
tiku_painter_surface(const tiku_painter_t *p)
{
    return p->s;
}

int
tiku_painter_status(const tiku_painter_t *p)
{
    return p != NULL ? p->status : TIKU_PAINTER_BAD;
}

/** @brief A copy of mask @p m, or NULL bytes kept NULL; a refused copy
 *         fails the painter and keeps the whole box. */
static tiku_vector_mask_t
mask_copy(tiku_painter_t *p, const tiku_vector_mask_t *m)
{
    tiku_vector_mask_t c = *m;

    if (m->a != NULL) {
        size_t n = (size_t)m->w * (size_t)m->h;

        if ((c.a = malloc(n ? n : 1)) == NULL) {
            tp_fail(p, TIKU_PAINTER_NOMEM);
            c.w = c.h = 0;
            return c;
        }
        memcpy(c.a, m->a, n);
    }
    return c;
}

void
tiku_painter_save(tiku_painter_t *p)
{
    if (p->nsaved == p->capsaved) {
        int cap = p->capsaved ? p->capsaved * 2 : 8;
        tiku_pstate_t *grown = realloc(p->saved, (size_t)cap * sizeof *grown);

        if (grown == NULL) {
            tp_fail(p, TIKU_PAINTER_NOMEM);
            p->stopped = 1;
            return;
        }
        p->saved = grown;
        p->capsaved = cap;
    }
    p->saved[p->nsaved] = p->st;
    p->saved[p->nsaved++].clip = mask_copy(p, &p->st.clip);
    if (p->st.clip.a != NULL && p->saved[p->nsaved - 1].clip.a == NULL) {
        p->stopped = 1;
    }
}

void
tiku_painter_restore(tiku_painter_t *p)
{
    if (p->nsaved == 0) {
        return;
    }
    tiku_vector_mask_free(&p->st.clip);
    p->st = p->saved[--p->nsaved];
}

void
tiku_painter_transform(tiku_painter_t *p, const tiku_affine_t *m)
{
    p->st.m = tiku_affine_mul(&p->st.m, m);
}

void
tiku_painter_set_transform(tiku_painter_t *p, const tiku_affine_t *m)
{
    p->st.m = *m;
}

tiku_affine_t
tiku_painter_get_transform(const tiku_painter_t *p)
{
    return p->st.m;
}

void
tiku_painter_set_alpha(tiku_painter_t *p, double alpha)
{
    p->st.alpha = isfinite(alpha) ? alpha < 0 ? 0 : alpha > 1 ? 1 : alpha
                                  : 1;
}

void
tiku_painter_new_path(tiku_painter_t *p)
{
    tiku_vector_path_clear(&p->path);
}

/** @brief User point (@p x, @p y) in device pixels into @p dx, @p dy. */
static void
to_device(const tiku_painter_t *p, double x, double y, double *dx,
          double *dy)
{
    tiku_affine_t d = tp_device(p);

    tiku_affine_apply(&d, x, y, dx, dy);
}

void
tiku_painter_move(tiku_painter_t *p, double x, double y)
{
    double dx, dy;

    to_device(p, x, y, &dx, &dy);
    tiku_vector_path_moveto(&p->path, dx, dy);
}

void
tiku_painter_line(tiku_painter_t *p, double x, double y)
{
    double dx, dy;

    to_device(p, x, y, &dx, &dy);
    tiku_vector_path_lineto(&p->path, dx, dy);
}

void
tiku_painter_cubic(tiku_painter_t *p, double ax, double ay, double bx,
                   double by, double x, double y)
{
    double d[6];

    to_device(p, ax, ay, &d[0], &d[1]);
    to_device(p, bx, by, &d[2], &d[3]);
    to_device(p, x, y, &d[4], &d[5]);
    tiku_vector_path_curveto(&p->path, d[0], d[1], d[2], d[3], d[4], d[5]);
}

void
tiku_painter_quad(tiku_painter_t *p, double cx, double cy, double x,
                  double y)
{
    tiku_affine_t inv, d = tp_device(p);
    double sx, sy;

    /* the pen, back in user space, raises the quadratic to a cubic */
    if (!tiku_affine_invert(&d, &inv)) {
        tp_fail(p, TIKU_PAINTER_BAD);
        return;
    }
    tiku_affine_apply(&inv, p->path.cx, p->path.cy, &sx, &sy);
    tiku_painter_cubic(p, sx + 2.0 / 3.0 * (cx - sx),
                       sy + 2.0 / 3.0 * (cy - sy),
                       x + 2.0 / 3.0 * (cx - x), y + 2.0 / 3.0 * (cy - y),
                       x, y);
}

void
tiku_painter_close(tiku_painter_t *p)
{
    tiku_vector_path_close(&p->path);
}

void
tiku_painter_rect(tiku_painter_t *p, double x, double y, double w, double h)
{
    tiku_painter_move(p, x, y);
    tiku_painter_line(p, x + w, y);
    tiku_painter_line(p, x + w, y + h);
    tiku_painter_line(p, x, y + h);
    tiku_painter_close(p);
}

void
tiku_painter_ellipse(tiku_painter_t *p, double cx, double cy, double rx,
                     double ry)
{
    const double k = 0.5522847498307936;    /* four cubics round a circle */
    double kx = k * rx, ky = k * ry;

    tiku_painter_move(p, cx + rx, cy);
    tiku_painter_cubic(p, cx + rx, cy + ky, cx + kx, cy + ry, cx, cy + ry);
    tiku_painter_cubic(p, cx - kx, cy + ry, cx - rx, cy + ky, cx - rx, cy);
    tiku_painter_cubic(p, cx - rx, cy - ky, cx - kx, cy - ry, cx, cy - ry);
    tiku_painter_cubic(p, cx + kx, cy - ry, cx + rx, cy - ky, cx + rx, cy);
    tiku_painter_close(p);
}

/** @brief The painter failure for rasteriser failure @p f. */
static int
from_vector(int f)
{
    return f == TIKU_VECTOR_NOMEM ? TIKU_PAINTER_NOMEM
           : f == TIKU_VECTOR_LIMIT ? TIKU_PAINTER_LIMIT : TIKU_PAINTER_BAD;
}

int
tp_box(tiku_painter_t *p, const tiku_vector_path_t *path, int box[4])
{
    int x0, y0, w, h;

    if (path->failed) {
        tp_fail(p, from_vector(path->failed));
        return 0;
    }
    if (p->st.x1 <= p->st.x0 || p->st.y1 <= p->st.y0 ||
        !tiku_vector_path_bounds(path, p->st.x1, p->st.y1, &x0, &y0, &w,
                                 &h)) {
        return 0;
    }
    box[0] = x0 > p->st.x0 ? x0 : p->st.x0;
    box[1] = y0 > p->st.y0 ? y0 : p->st.y0;
    box[2] = x0 + w;
    box[3] = y0 + h;
    return box[2] > box[0] && box[3] > box[1];
}

/** @brief Coverage of @p path by @p evenodd over its box in the clip,
 *         into @p m.  @return 0 when it covers nothing or failed. */
static int
coverage(tiku_painter_t *p, const tiku_vector_path_t *path, int evenodd,
         tiku_vector_mask_t *m)
{
    int box[4];

    memset(m, 0, sizeof *m);
    if (!tp_box(p, path, box)) {
        return 0;
    }
    *m = tiku_vector_path_fill(path, evenodd, box[0], box[1],
                               box[2] - box[0], box[3] - box[1]);
    if (m->a == NULL) {
        tp_fail(p, TIKU_PAINTER_NOMEM);
        return 0;
    }
    tiku_vector_mask_intersect(m, &p->st.clip);
    return 1;
}

void
tiku_painter_fill(tiku_painter_t *p, const tiku_paint_t *paint,
                  tiku_fill_rule_t rule)
{
    tiku_vector_mask_t m;

    if (paint == NULL) {
        tp_fail(p, TIKU_PAINTER_BAD);
        return;
    }
    if (coverage(p, &p->path, rule == TIKU_FILL_EVENODD, &m)) {
        tp_composite(p, &m, paint);
    }
    tiku_vector_mask_free(&m);
}

void
tiku_painter_stroke(tiku_painter_t *p, const tiku_paint_t *paint,
                    const tiku_stroke_t *stroke)
{
    tiku_affine_t d = tp_device(p);
    double k = sqrt(fabs(d.a * d.d - d.b * d.c));
    double dash[TIKU_STROKE_MAX_DASHES];
    tiku_vector_path_t out;
    tiku_vector_mask_t m;
    int i;

    if (paint == NULL || stroke == NULL || stroke->dash_count < 0 ||
        stroke->dash_count > TIKU_STROKE_MAX_DASHES) {
        tp_fail(p, TIKU_PAINTER_BAD);
        return;
    }
    for (i = 0; i < stroke->dash_count; i++) {
        dash[i] = stroke->dash[i] * k;
    }
    tiku_vector_path_init(&out);
    out.strict = 1;
    tiku_vector_path_stroke(&p->path, stroke->width * k, stroke->cap,
                            stroke->join,
                            stroke->miter > 0 ? stroke->miter : 10, dash,
                            stroke->dash_count, stroke->dash_offset * k,
                            &out);
    if (coverage(p, &out, 0, &m)) {
        tp_composite(p, &m, paint);
    }
    tiku_vector_mask_free(&m);
    tiku_vector_path_free(&out);
}

void
tiku_painter_clip(tiku_painter_t *p, tiku_fill_rule_t rule)
{
    tiku_vector_mask_t m;

    if (!coverage(p, &p->path, rule == TIKU_FILL_EVENODD, &m)) {
        /* nothing of the path inside, or a path that could not be
         * measured: nothing stays visible */
        tiku_vector_mask_free(&p->st.clip);
        p->st.x1 = p->st.x0;
        p->st.y1 = p->st.y0;
        return;
    }
    tiku_vector_mask_free(&p->st.clip);
    p->st.clip = m;
    p->st.x0 = m.x0;
    p->st.y0 = m.y0;
    p->st.x1 = m.x0 + m.w;
    p->st.y1 = m.y0 + m.h;
}
