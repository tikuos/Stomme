/*
 * KintsugiOS v0.01 kits -- the foundation KintsugiDesktop and KintsugiTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@kinstugi-os.org>
 *
 * tiku_painter_fill.c - coverage coloured and laid over the surface: a
 * solid colour, a gradient read from a table of 1024 steps, or an image
 * sampled through the inverse transform.
 *
 * A gradient mixes its stops as straight colours and premultiplies the
 * result, as cairo does; each pixel then takes the premultiplied source
 * plus the destination times what the source leaves uncovered, on the
 * surface's channel values as they are.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_painter_private.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/** @brief Steps in a gradient's colour table. */
#define STEPS 1024

/** @brief A premultiplied colour, each channel 0 to 1. */
typedef struct {
    float r, g, b, a;
} pcolor_t;

/** @brief Where a pixel's colour comes from: the paint or image, and the
 *         map from device pixels back to user space. */
typedef struct {
    const tiku_paint_t *paint;      /* the paint, or NULL for an image   */
    pcolor_t            solid;      /* SOLID: its colour                 */
    pcolor_t           *table;      /* gradients: STEPS colours          */
    tiku_affine_t       inv;        /* device pixel to user point        */
    const uint32_t     *img;        /* an image's pixels                 */
    int                 iw, ih, stride, smooth;  /* its size and reading */
} source_t;

/** @brief Straight 0xAARRGGBB @p argb premultiplied. */
static pcolor_t
premul(uint32_t argb)
{
    pcolor_t c;

    c.a = (float)(argb >> 24) / 255.0f;
    c.r = (float)((argb >> 16) & 0xFF) / 255.0f * c.a;
    c.g = (float)((argb >> 8) & 0xFF) / 255.0f * c.a;
    c.b = (float)(argb & 0xFF) / 255.0f * c.a;
    return c;
}

/** @brief @p a and @p b mixed, @p t of the way to @p b. */
static pcolor_t
mix(pcolor_t a, pcolor_t b, float t)
{
    pcolor_t c;

    c.r = a.r + (b.r - a.r) * t;
    c.g = a.g + (b.g - a.g) * t;
    c.b = a.b + (b.b - a.b) * t;
    c.a = a.a + (b.a - a.a) * t;
    return c;
}

/** @brief Straight colours @p a and @p b mixed, @p t of the way to @p b,
 *         then premultiplied. */
static pcolor_t
straight_mix(uint32_t a, uint32_t b, float t)
{
    pcolor_t x = premul(a | 0xFF000000u), y = premul(b | 0xFF000000u);
    pcolor_t c = mix(x, y, t);
    float al = ((float)(a >> 24) + ((float)(b >> 24) - (float)(a >> 24)) * t)
               / 255.0f;

    c.r *= al;
    c.g *= al;
    c.b *= al;
    c.a = al;
    return c;
}

/** @brief The colour of @p paint's stops at @p t, 0 to 1. */
static pcolor_t
stop_at(const tiku_paint_t *paint, double t)
{
    int i, n = paint->stop_count;

    if (t <= paint->stop[0].offset) {
        return premul(paint->stop[0].argb);
    }
    for (i = 1; i < n; i++) {
        if (t <= paint->stop[i].offset) {
            double span = paint->stop[i].offset - paint->stop[i - 1].offset;

            float u = span > 0 ? (float)((t - paint->stop[i - 1].offset) /
                                         span) : 1.0f;

            return straight_mix(paint->stop[i - 1].argb, paint->stop[i].argb,
                                u);
        }
    }
    return premul(paint->stop[n - 1].argb);
}

/** @brief Whether gradient @p paint's stops can be drawn. */
static int
stops_ok(const tiku_paint_t *paint)
{
    int i;

    if (paint->stop_count < 1 || paint->stop_count > TIKU_PAINT_MAX_STOPS) {
        return 0;
    }
    for (i = 0; i < paint->stop_count; i++) {
        if (!isfinite(paint->stop[i].offset) ||
            (i > 0 && paint->stop[i].offset < paint->stop[i - 1].offset)) {
            return 0;
        }
    }
    return 1;
}

/** @brief Where along linear gradient @p g user point (@p x, @p y) falls,
 *         held to [0, 1]. */
static double
linear_t(const tiku_paint_t *g, double x, double y)
{
    double dx = g->x1 - g->x0, dy = g->y1 - g->y0, len = dx * dx + dy * dy;
    double t = len > 0 ? ((x - g->x0) * dx + (y - g->y0) * dy) / len : 1;

    return t < 0 ? 0 : t > 1 ? 1 : t;
}

/** @brief Where along radial gradient @p g user point (@p x, @p y)
 *         falls: the largest t whose circle passes through it with a
 *         radius not below zero, held to [0, 1].  @return 0 when no
 *         circle does. */
static int
radial_t(const tiku_paint_t *g, double x, double y, double *t)
{
    double cdx = g->x1 - g->x0, cdy = g->y1 - g->y0, dr = g->r1 - g->r0;
    double px = x - g->x0, py = y - g->y0;
    double a = cdx * cdx + cdy * cdy - dr * dr;
    double b = px * cdx + py * cdy + g->r0 * dr;
    double c = px * px + py * py - g->r0 * g->r0;
    double s;

    if (fabs(a) < 1e-12) {
        if (b == 0) {
            return 0;
        }
        s = c / (2 * b);
    } else {
        double disc = b * b - a * c, r;

        if (disc < 0) {
            return 0;
        }
        r = sqrt(disc);
        s = (b + r) / a;
        if (g->r0 + s * dr < 0) {
            s = (b - r) / a;
            if (g->r0 + s * dr < 0) {
                return 0;
            }
        }
    }
    *t = s < 0 ? 0 : s > 1 ? 1 : s;
    return 1;
}

/** @brief Image pixel (@p i, @p j), held to the image, premultiplied. */
static pcolor_t
texel(const source_t *src, int i, int j)
{
    i = i < 0 ? 0 : i >= src->iw ? src->iw - 1 : i;
    j = j < 0 ? 0 : j >= src->ih ? src->ih - 1 : j;
    return premul(src->img[(size_t)j * src->stride + i]);
}

/** @brief The image's colour at user point (@p u, @p v). */
static pcolor_t
sample(const source_t *src, double u, double v)
{
    pcolor_t top, bottom;
    double fu, fv;
    int i, j;

    if (!src->smooth) {
        return texel(src, (int)floor(u), (int)floor(v));
    }
    u -= 0.5;
    v -= 0.5;
    i = (int)floor(u);
    j = (int)floor(v);
    fu = u - i;
    fv = v - j;
    top = mix(texel(src, i, j), texel(src, i + 1, j), (float)fu);
    bottom = mix(texel(src, i, j + 1), texel(src, i + 1, j + 1), (float)fu);
    return mix(top, bottom, (float)fv);
}

/** @brief The source's colour at device pixel (@p x, @p y), into @p c.
 *  @return 0 where it gives none. */
static int
color_at(const source_t *src, int x, int y, pcolor_t *c)
{
    double ux, uy, t;

    if (src->paint != NULL && src->paint->kind == TIKU_PAINT_SOLID) {
        *c = src->solid;
        return 1;
    }
    tiku_affine_apply(&src->inv, x + 0.5, y + 0.5, &ux, &uy);
    if (src->paint == NULL) {
        *c = sample(src, ux, uy);
        return 1;
    }
    if (src->paint->kind == TIKU_PAINT_LINEAR) {
        t = linear_t(src->paint, ux, uy);
    } else if (!radial_t(src->paint, ux, uy, &t)) {
        return 0;
    }
    *c = src->table[(int)(t * (STEPS - 1) + 0.5)];
    return 1;
}

/** @brief Native pixel (@p x, @p y) of the surface. */
static tiku_rgb_t *
pixel(tiku_painter_t *p, int x, int y)
{
    return p->s->px + (size_t)y * (size_t)(p->s->w * p->scale) + x;
}

/** @brief Colour @p c laid over @p px by @p cover, 0 to 1. */
static void
over(tiku_rgb_t *px, pcolor_t c, float cover)
{
    float keep = 1.0f - c.a * cover;
    float r = (float)((*px >> 16) & 0xFF) * keep + c.r * cover * 255.0f;
    float g = (float)((*px >> 8) & 0xFF) * keep + c.g * cover * 255.0f;
    float b = (float)(*px & 0xFF) * keep + c.b * cover * 255.0f;

    *px = TIKU_RGB((unsigned)(r + 0.5f), (unsigned)(g + 0.5f),
                   (unsigned)(b + 0.5f));
}

/** @brief Coverage @p m laid over the surface with source @p src.  An
 *         opaque solid paint at full coverage and alpha stores the colour
 *         over() would give, worked out once. */
static void
lay(tiku_painter_t *p, const tiku_vector_mask_t *m, const source_t *src)
{
    float alpha = (float)p->st.alpha / 255.0f;
    int solid = src->paint != NULL && src->paint->kind == TIKU_PAINT_SOLID;
    int whole = solid && src->solid.a == 1.0f && 255.0f * alpha == 1.0f;
    tiku_rgb_t full = 0;
    int x, y;

    if (p->stopped) {
        return;
    }
    tiku_gfx_rec_miss(p->s);
    if (whole) {
        over(&full, src->solid, 255.0f * alpha);
    }
    for (y = 0; y < m->h; y++) {
        const unsigned char *row = m->a + (size_t)y * m->w;
        tiku_rgb_t *px = pixel(p, m->x0, m->y0 + y);

        for (x = 0; x < m->w; x++) {
            pcolor_t c;

            if (row[x] == 0) {
                continue;
            }
            if (whole && row[x] == 255) {
                px[x] = full;
            } else if (solid) {
                over(&px[x], src->solid, (float)row[x] * alpha);
            } else if (color_at(src, m->x0 + x, m->y0 + y, &c)) {
                over(&px[x], c, (float)row[x] * alpha);
            }
        }
    }
}

/** @brief Source @p src set up for @p paint: its colour or table, and the
 *         inverse transform.  @return 0, with the painter failed, when it
 *         cannot be drawn. */
static int
source(tiku_painter_t *p, const tiku_paint_t *paint, source_t *src)
{
    tiku_affine_t d = tp_device(p);
    int i;

    memset(src, 0, sizeof *src);
    src->paint = paint;
    if (paint->kind == TIKU_PAINT_SOLID) {
        src->solid = premul(paint->argb);
        return 1;
    }
    if ((paint->kind != TIKU_PAINT_LINEAR &&
         paint->kind != TIKU_PAINT_RADIAL) || !stops_ok(paint) ||
        !tiku_affine_invert(&d, &src->inv)) {
        tp_fail(p, TIKU_PAINTER_BAD);
        return 0;
    }
    if ((src->table = malloc(STEPS * sizeof *src->table)) == NULL) {
        tp_fail(p, TIKU_PAINTER_NOMEM);
        return 0;
    }
    for (i = 0; i < STEPS; i++) {
        src->table[i] = stop_at(paint, (double)i / (STEPS - 1));
    }
    return 1;
}

void
tp_composite(tiku_painter_t *p, const tiku_vector_mask_t *mask,
             const tiku_paint_t *paint)
{
    source_t src;

    if (source(p, paint, &src)) {
        lay(p, mask, &src);
    }
    free(src.table);
}

void
tiku_painter_image(tiku_painter_t *p, const uint32_t *argb, int w, int h,
                   int stride, int smooth)
{
    tiku_affine_t d = tp_device(p);
    tiku_vector_path_t rect;
    tiku_vector_mask_t m;
    source_t src;
    double pt[8];
    int box[4], i;

    if (argb == NULL || w <= 0 || h <= 0 || stride < w) {
        tp_fail(p, TIKU_PAINTER_BAD);
        return;
    }
    memset(&src, 0, sizeof src);
    if (!tiku_affine_invert(&d, &src.inv)) {
        tp_fail(p, TIKU_PAINTER_BAD);
        return;
    }
    src.img = argb;
    src.iw = w;
    src.ih = h;
    src.stride = stride;
    src.smooth = smooth;
    tiku_affine_apply(&d, 0, 0, &pt[0], &pt[1]);
    tiku_affine_apply(&d, w, 0, &pt[2], &pt[3]);
    tiku_affine_apply(&d, w, h, &pt[4], &pt[5]);
    tiku_affine_apply(&d, 0, h, &pt[6], &pt[7]);
    tiku_vector_path_init(&rect);
    rect.strict = 1;
    tiku_vector_path_moveto(&rect, pt[0], pt[1]);
    for (i = 1; i < 4; i++) {
        tiku_vector_path_lineto(&rect, pt[2 * i], pt[2 * i + 1]);
    }
    tiku_vector_path_close(&rect);
    memset(&m, 0, sizeof m);
    if (tp_box(p, &rect, box)) {
        m = tiku_vector_path_fill(&rect, 0, box[0], box[1], box[2] - box[0],
                                  box[3] - box[1]);
        if (m.a == NULL) {
            tp_fail(p, TIKU_PAINTER_NOMEM);
        } else {
            tiku_vector_mask_intersect(&m, &p->st.clip);
            lay(p, &m, &src);
        }
    }
    tiku_vector_mask_free(&m);
    tiku_vector_path_free(&rect);
}
