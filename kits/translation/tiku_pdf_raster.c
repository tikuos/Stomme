/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_pdf_raster.c - the PDF kit's drawing seam: its matrices, paths and
 * masks handed to the shared vector rasteriser, and paint laid through a
 * mask in the page's blend mode.
 *
 * Filling and stroking themselves are tiku_vector_raster.c's; this file
 * keeps what is PDF's own: a path's point limit, a line state drawn
 * whatever it holds, the blend modes of ISO 32000 11.3.5 and a knockout
 * group's paint (11.4.8).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_pdf_private.h"

#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------ the seam  */

pdf_mat_t
pdf_mat(double a, double b, double c, double d, double e, double f)
{
    return tiku_vector_mat(a, b, c, d, e, f);
}

pdf_mat_t
pdf_mat_mul(pdf_mat_t m, pdf_mat_t n)
{
    return tiku_vector_mat_mul(m, n);
}

void
pdf_mat_apply(const pdf_mat_t *m, double x, double y, double *ox, double *oy)
{
    tiku_vector_mat_apply(m, x, y, ox, oy);
}

double
pdf_mat_scale(const pdf_mat_t *m)
{
    return tiku_vector_mat_scale(m);
}

void
pdf_path_init(pdf_path_t *p)
{
    tiku_vector_path_init(p);
    p->max_points = PDF_POINTS_MAX;
}

void
pdf_path_clear(pdf_path_t *p)
{
    tiku_vector_path_clear(p);
}

void
pdf_path_free(pdf_path_t *p)
{
    tiku_vector_path_free(p);
    p->max_points = PDF_POINTS_MAX;
}

void
pdf_path_moveto(pdf_path_t *p, double x, double y)
{
    tiku_vector_path_moveto(p, x, y);
}

void
pdf_path_lineto(pdf_path_t *p, double x, double y)
{
    tiku_vector_path_lineto(p, x, y);
}

void
pdf_path_curveto(pdf_path_t *p, double x1, double y1, double x2, double y2,
                 double x3, double y3)
{
    tiku_vector_path_curveto(p, x1, y1, x2, y2, x3, y3);
}

void
pdf_path_close(pdf_path_t *p)
{
    tiku_vector_path_close(p);
}

int
pdf_path_bounds(const pdf_path_t *p, int w, int h, int *x0, int *y0,
                int *bw, int *bh)
{
    return tiku_vector_path_bounds(p, w, h, x0, y0, bw, bh);
}

pdf_mask_t
pdf_path_fill(const pdf_path_t *p, int evenodd, int x0, int y0, int w, int h)
{
    return tiku_vector_path_fill(p, evenodd, x0, y0, w, h);
}

/** @brief @p v held to [@p lo, @p hi]; not a number is @p lo. */
static double
clamp(double v, double lo, double hi)
{
    return !(v >= lo) ? lo : v > hi ? hi : v;
}

void
pdf_path_stroke(const pdf_path_t *p, double width, int cap, int join,
                double miter, const double *dash, int ndash, double phase,
                pdf_path_t *out)
{
    double kept[PDF_DASH_MAX];
    int i;

    /* The rasteriser refuses a stroke it cannot draw as asked; PDF's line
     * state is made drawable first, a dash entry below zero being zero as
     * the renderer's solid-line test takes it. */
    if (dash == NULL || ndash < 0) {
        ndash = 0;
    }
    if (ndash > PDF_DASH_MAX) {
        ndash = PDF_DASH_MAX;
    }
    for (i = 0; i < ndash; i++) {
        kept[i] = clamp(dash[i], 0, DBL_MAX);
    }
    tiku_vector_path_stroke(p, clamp(width, 0, DBL_MAX), cap, join,
                            clamp(miter, 0, DBL_MAX), kept, ndash,
                            isfinite(phase) ? phase : 0, out);
}

void
pdf_mask_free(pdf_mask_t *m)
{
    tiku_vector_mask_free(m);
}

void
pdf_mask_intersect(pdf_mask_t *m, const pdf_mask_t *clip)
{
    tiku_vector_mask_intersect(m, clip);
}

pdf_mask_t
pdf_mask_full(int w, int h)
{
    return tiku_vector_mask_full(w, h);
}

/* ---------------------------------------------------------------- paint  */

void
pdf_paint(pdf_pix_t *pix, const pdf_mask_t *m, const pdf_mask_t *clip,
          uint32_t colour, double alpha)
{
    int x, y, ca = (int)(alpha * 255 + 0.5);
    unsigned cr = (colour >> 16) & 0xff, cg = (colour >> 8) & 0xff;
    unsigned cb = colour & 0xff;

    if (m->a == NULL || ca <= 0) {
        return;
    }
    if (ca > 255) {
        ca = 255;
    }
    for (y = 0; y < m->h; y++) {
        int py = m->y0 + y;

        if (py < 0 || py >= pix->h) {
            continue;
        }
        for (x = 0; x < m->w; x++) {
            int px = m->x0 + x, a = m->a[(size_t)y * m->w + x], shape;
            uint32_t *d;

            if (px < 0 || px >= pix->w || a == 0) {
                continue;
            }
            if (clip != NULL && clip->a != NULL) {
                int cx = px - clip->x0, cy = py - clip->y0;

                if (cx < 0 || cx >= clip->w || cy < 0 || cy >= clip->h) {
                    continue;
                }
                a = (a * clip->a[(size_t)cy * clip->w + cx] + 127) / 255;
            }
            shape = a;
            a = (a * ca + 127) / 255;
            if (a == 0) {
                continue;
            }
            d = pix->px + (size_t)py * pix->w + px;
            pdf_blend(pix, d, 0xff000000u | cr << 16 | cg << 8 | cb, a,
                      shape);
        }
    }
}

/** @brief One channel of separable mode @p mode, backdrop @p b and
 *         source @p s, each 0 to 255. */
static int
separable(int mode, int b, int s)
{
    double cb = b / 255.0, cs = s / 255.0, r;

    switch (mode) {
    case PDF_BM_MULTIPLY: return b * s / 255;
    case PDF_BM_SCREEN: return b + s - b * s / 255;
    case PDF_BM_OVERLAY: return separable(PDF_BM_HARDLIGHT, s, b);
    case PDF_BM_DARKEN: return b < s ? b : s;
    case PDF_BM_LIGHTEN: return b > s ? b : s;
    case PDF_BM_DODGE:
        return b == 0 ? 0 : s >= 255 ? 255
               : b * 255 / (255 - s) > 255 ? 255 : b * 255 / (255 - s);
    case PDF_BM_BURN:
        return b == 255 ? 255 : s <= 0 ? 0
               : (255 - b) * 255 / s > 255 ? 0 : 255 - (255 - b) * 255 / s;
    case PDF_BM_HARDLIGHT:
        return s <= 127 ? b * 2 * s / 255
                        : separable(PDF_BM_SCREEN, b, 2 * s - 255);
    case PDF_BM_SOFTLIGHT:
        if (cs <= 0.5) {
            r = cb - (1 - 2 * cs) * cb * (1 - cb);
        } else {
            double d = cb <= 0.25 ? ((16 * cb - 12) * cb + 4) * cb
                                  : sqrt(cb);

            r = cb + (2 * cs - 1) * (d - cb);
        }
        return (int)(r * 255 + 0.5);
    case PDF_BM_DIFFERENCE: return b > s ? b - s : s - b;
    case PDF_BM_EXCLUSION: return b + s - 2 * b * s / 255;
    }
    return s;
}

/** @brief A colour's luminosity, as the non-separable modes weigh it. */
static double
lum(const double *c)
{
    return 0.3 * c[0] + 0.59 * c[1] + 0.11 * c[2];
}

/** @brief @p c given luminosity @p l, brought back into the gamut. */
static void
set_lum(double *c, double l)
{
    double d = l - lum(c), n, x;
    int k;

    for (k = 0; k < 3; k++) {
        c[k] += d;
    }
    l = lum(c);
    n = fmin(c[0], fmin(c[1], c[2]));
    x = fmax(c[0], fmax(c[1], c[2]));
    for (k = 0; k < 3; k++) {
        if (n < 0 && l - n > 1e-9) {
            c[k] = l + (c[k] - l) * l / (l - n);
        }
        if (x > 1 && x - l > 1e-9) {
            c[k] = l + (c[k] - l) * (1 - l) / (x - l);
        }
    }
}

/** @brief @p c given saturation @p s, its hue kept. */
static void
set_sat(double *c, double s)
{
    int hi = 0, lo = 0, mid, k;

    for (k = 1; k < 3; k++) {
        if (c[k] > c[hi]) {
            hi = k;
        }
        if (c[k] < c[lo]) {
            lo = k;
        }
    }
    if (hi == lo) {
        c[0] = c[1] = c[2] = 0;
        return;
    }
    mid = 3 - hi - lo;
    c[mid] = (c[mid] - c[lo]) * s / (c[hi] - c[lo]);
    c[hi] = s;
    c[lo] = 0;
}

/** @brief The source @p s blended over backdrop @p b by @p mode, all
 *         0 to 255, into @p s. */
static void
mix(int mode, const int *b, int *s)
{
    double cb[3], cs[3], sat;
    int k;

    if (mode < PDF_BM_HUE) {
        for (k = 0; k < 3; k++) {
            s[k] = separable(mode, b[k], s[k]);
        }
        return;
    }
    for (k = 0; k < 3; k++) {
        cb[k] = b[k] / 255.0;
        cs[k] = s[k] / 255.0;
    }
    if (mode == PDF_BM_HUE || mode == PDF_BM_SATURATION) {
        double *from = mode == PDF_BM_HUE ? cs : cb;
        const double *by = mode == PDF_BM_HUE ? cb : cs;

        sat = fmax(by[0], fmax(by[1], by[2])) -
              fmin(by[0], fmin(by[1], by[2]));
        set_sat(from, sat);
        set_lum(from, lum(cb));
        memcpy(cs, from, sizeof cs);
    } else if (mode == PDF_BM_COLOR) {
        set_lum(cs, lum(cb));
    } else {
        double l = lum(cs);

        memcpy(cs, cb, sizeof cs);
        set_lum(cs, l);
    }
    for (k = 0; k < 3; k++) {
        int v = (int)(cs[k] * 255 + 0.5);

        s[k] = v < 0 ? 0 : v > 255 ? 255 : v;
    }
}

int
pdf_blend_mode(const char *name)
{
    static const char *const names[] = {
        "Normal", "Multiply", "Screen", "Overlay", "Darken", "Lighten",
        "ColorDodge", "ColorBurn", "HardLight", "SoftLight", "Difference",
        "Exclusion", "Hue", "Saturation", "Color", "Luminosity"
    };
    int i;

    for (i = 0; name != NULL && i < (int)(sizeof names / sizeof names[0]);
         i++) {
        if (strcmp(name, names[i]) == 0) {
            return i;
        }
    }
    return PDF_BM_NORMAL;           /* Compatible, and what is not known */
}

void
pdf_blend(const pdf_pix_t *pix, uint32_t *d, uint32_t colour, int a,
          int shape)
{
    uint32_t under = pix->knockout ? pix->ground : *d;
    int b[3] = { (int)((under >> 16) & 0xff), (int)((under >> 8) & 0xff),
                 (int)(under & 0xff) };
    int s[3] = { (int)((colour >> 16) & 0xff), (int)((colour >> 8) & 0xff),
                 (int)(colour & 0xff) };
    unsigned ua = (unsigned)a, us = (unsigned)shape, out[3];
    int k;

    if (pix->knockout) {            /* the opacity, apart from the shape */
        if (shape <= 0) {
            return;
        }
        ua = ((unsigned)a * 255u + us / 2) / us;
        ua = ua > 255u ? 255u : ua;
    }
    if (pix->blend != PDF_BM_NORMAL) {
        mix(pix->blend, b, s);
    }
    for (k = 0; k < 3; k++) {
        out[k] = ((unsigned)b[k] * (255u - ua) + (unsigned)s[k] * ua + 127) /
                 255;
        if (pix->knockout) {        /* in place of what the group had */
            unsigned was = (*d >> (16 - 8 * k)) & 0xffu;

            out[k] = (was * (255u - us) + out[k] * us + 127) / 255;
        }
    }
    *d = 0xff000000u | out[0] << 16 | out[1] << 8 | out[2];
}
