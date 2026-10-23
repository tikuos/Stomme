/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_circuit_render.c - a scene drawn into a viewport: each visible
 * object tessellated at the view's scale, stroked or filled through the
 * shared vector rasteriser, and painted or accumulated as coverage, with
 * the caller's cancellation checked between objects.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_circuit_private.h"
#include "tiku_vector_raster.h"
#include "tiku_circuit_artwork.h"

#include <math.h>
#include <stdlib.h>

#define STROKE_ROUND 1          /* the stroker's round cap and round join */
#define STROKE_MITER 4          /* its miter limit, unused by round joins */

/** @brief The scene's error for a vector path's failure @p error. */
static tiku_circuit_err_t
path_error(int error)
{
    return error == TIKU_VECTOR_LIMIT ? TIKU_CIRCUIT_LIMIT
           : error == TIKU_VECTOR_BAD ? TIKU_CIRCUIT_BAD
           : TIKU_CIRCUIT_NOMEM;
}

/** @brief Blend @p colour into @p im wherever mask @p m covers it. */
static void
paint(tiku_image_t *im, const tiku_vector_mask_t *m, uint32_t colour)
{
    for (int y = 0; y < m->h; y++) {
        for (int x = 0; x < m->w; x++) {
            unsigned a = m->a[(size_t)y * m->w + x];
            uint32_t *p = &im->px[(size_t)(y + m->y0) * im->w + x + m->x0];
            uint32_t v = CIRCUIT_OPAQUE;

            for (int shift = 0; shift <= 16; shift += 8) {
                v |= ((((*p >> shift) & 255) * (255 - a) +
                       ((colour >> shift) & 255) * a + 127) / 255) << shift;
            }
            *p = v;
        }
    }
}

/**
 * @brief Build shape @p s's path into @p p in @p v's pixels, arcs
 *        tessellated at the view's tolerance.
 *
 * Geometry reaching past the @p width by @p height viewport is clipped
 * to it, with room for the stroke, so far-off points stay within the
 * rasteriser's coordinates; a shape inside keeps its path as it is.
 *
 * @return OK; LIMIT for a stroke too wide for its clipped outline to stay
 *         within the rasteriser's coordinates, or an arc needing too many
 *         points; BAD; NOMEM
 */
static tiku_circuit_err_t
shape_path(const circuit_shape_t *s, const tiku_circuit_view_t *v,
           int width, int height, tiku_vector_path_t *p)
{
    tiku_circuit_box_t b = s->info.bounds;
    double half = s->fill ? 0 : s->width * v->scale / 2;
    int pass = b.x0 * v->scale + v->x >= -CIRCUIT_CLIP_MARGIN &&
               b.y0 * v->scale + v->y >= -CIRCUIT_CLIP_MARGIN &&
               b.x1 * v->scale + v->x <= width + CIRCUIT_CLIP_MARGIN &&
               b.y1 * v->scale + v->y <= height + CIRCUIT_CLIP_MARGIN;
    circuit_clip_t pen;

    /* The stroke's outline reaches its own half width past the clipped
     * line, and must stay within the rasteriser's coordinates. */
    if (!pass && fmax(width, height) + 2 * half + CIRCUIT_CLIP_MARGIN >
                 CIRCUIT_RASTER_MAX) {
        return TIKU_CIRCUIT_LIMIT;
    }
    tiku_vector_path_clear(p);
    circuit_clip_start(&pen, p, half + CIRCUIT_CLIP_MARGIN, width, height,
                       s->fill, pass);
    size_t count = s->count;

    if (s->kind == CIRCUIT_ARC) {
        double radius = fmax(s->r, s->end_r) * v->scale;
        double step = radius <= CIRCUIT_CURVE_ERROR
                      ? CIRCUIT_COARSE_STEP
                      : 2 * acos(1 - CIRCUIT_CURVE_ERROR / radius);
        double pieces = ceil(fabs(s->sweep) / step);

        if (!isfinite(pieces) || pieces >= CIRCUIT_POINTS_MAX) {
            return TIKU_CIRCUIT_LIMIT;
        }
        count = (size_t)fmax(2, pieces) + 1;
    }
    for (size_t j = 0; j < count; j++) {
        double wx = s->points[j % s->count].x;
        double wy = s->points[j % s->count].y;

        if (s->kind == CIRCUIT_ARC) {
            double a = s->start + s->sweep * j / (count - 1);
            double radius = s->end_r > 0
                            ? s->r + (s->end_r - s->r) * j / (count - 1)
                            : s->r;

            wx = s->cx + radius * cos(a);
            wy = s->cy + radius * sin(a);
        }
        double x = wx * v->scale + v->x, y = wy * v->scale + v->y;

        if (!j) {
            circuit_clip_move(&pen, x, y);
        } else {
            circuit_clip_line(&pen, x, y);
        }
    }
    if (s->closed) {
        circuit_clip_close(&pen);
    }
    int artwork_error = circuit_artwork_outline(s, v, &pen);

    circuit_clip_end(&pen);
    return artwork_error ? path_error(artwork_error) : TIKU_CIRCUIT_OK;
}

tiku_circuit_err_t
circuit_render_flat(const tiku_circuit_t *d, const tiku_circuit_view_t *v,
                    int (*cancel)(void *), void *ctx, tiku_image_t *out,
                    int transparent, uint32_t colour)
{
    if (!d || !v || !out || out->px || !isfinite(v->scale) ||
        v->scale < CIRCUIT_SCALE_MIN || v->scale > CIRCUIT_SCALE_MAX ||
        !isfinite(v->x) || !isfinite(v->y) ||
        fabs(v->x) > CIRCUIT_SHIFT_MAX || fabs(v->y) > CIRCUIT_SHIFT_MAX ||
        v->width < 1 || v->height < 1) {
        return TIKU_CIRCUIT_BAD;
    }
    if (v->width > TIKU_CIRCUIT_RENDER_SIDE ||
        v->height > TIKU_CIRCUIT_RENDER_SIDE ||
        (size_t)v->width * v->height > TIKU_CIRCUIT_RENDER_PIXELS) {
        return TIKU_CIRCUIT_LIMIT;
    }
    if (cancel && cancel(ctx)) {
        return TIKU_CIRCUIT_CANCELLED;
    }
    tiku_image_t im = {.w = v->width, .h = v->height};

    im.px = malloc((size_t)im.w * im.h * sizeof *im.px);
    if (!im.px) {
        return TIKU_CIRCUIT_NOMEM;
    }
    unsigned char *coverage = (d->coverage || transparent)
                              ? calloc((size_t)im.w * im.h, 1) : NULL;

    if ((d->coverage || transparent) && !coverage) {
        free(im.px);
        return TIKU_CIRCUIT_NOMEM;
    }
    for (size_t i = 0; i < (size_t)im.w * im.h; i++) {
        im.px[i] = CIRCUIT_PAPER;
    }
    tiku_vector_path_t p = {.strict = 1, .max_points = CIRCUIT_POINTS_MAX};
    tiku_vector_path_t stroke = {.strict = 1,
                                 .max_points = CIRCUIT_POINTS_MAX};
    tiku_circuit_err_t err = TIKU_CIRCUIT_OK;

    for (size_t i = 0; i < d->count; i++) {
        if (cancel && cancel(ctx)) {
            err = TIKU_CIRCUIT_CANCELLED;
            break;
        }
        const circuit_shape_t *s = &d->shapes[i];
        tiku_circuit_box_t b = s->info.bounds;

        if (b.x1 * v->scale + v->x < 0 || b.y1 * v->scale + v->y < 0 ||
            b.x0 * v->scale + v->x >= im.w ||
            b.y0 * v->scale + v->y >= im.h) {
            continue;
        }
        if (s->kind == CIRCUIT_GROUP) {
            tiku_circuit_view_t child = *v;

            child.x += s->cx * v->scale;
            child.y += s->cy * v->scale;
            tiku_image_t mask = {0};

            err = circuit_render_flat(s->group, &child, cancel, ctx, &mask,
                                      1, s->colour);
            if (err) {
                break;
            }
            for (int y = 0; y < im.h && !err; y++) {
                if (cancel && cancel(ctx)) {
                    err = TIKU_CIRCUIT_CANCELLED;
                    break;
                }
                for (int x = 0; x < im.w; x++) {
                    size_t k = (size_t)y * im.w + x;
                    unsigned a = mask.px[k] >> 24;

                    if (coverage) {
                        coverage[k] = s->clear
                            ? (coverage[k] * (255 - a) + 127) / 255
                            : a + (coverage[k] * (255 - a) + 127) / 255;
                    } else {
                        uint32_t pixel = CIRCUIT_OPAQUE;

                        for (int shift = 0; shift <= 16; shift += 8) {
                            pixel |= ((((im.px[k] >> shift) & 255) *
                                       (255 - a) +
                                       ((s->colour >> shift) & 255) * a +
                                       127) / 255) << shift;
                        }
                        im.px[k] = pixel;
                    }
                }
            }
            free(mask.px);
            if (err) {
                break;
            }
            continue;
        }
        if (s->kind == CIRCUIT_TEXT) {
            err = circuit_text_paint(d, s, v, &im, cancel, ctx);
            if (err) {
                break;
            }
            continue;
        }
        err = shape_path(s, v, im.w, im.h, &p);
        if (err) {
            break;
        }
        tiku_vector_path_t *ink = &p;

        if (!s->fill) {
            tiku_vector_path_stroke(&p, s->width * v->scale, STROKE_ROUND,
                                    STROKE_ROUND, STROKE_MITER, NULL, 0, 0,
                                    &stroke);
            ink = &stroke;
        }
        if (ink->failed) {
            err = path_error(ink->failed);
            break;
        }
        int x, y, w, h;

        if (!tiku_vector_path_bounds(ink, im.w, im.h, &x, &y, &w, &h)) {
            continue;
        }
        tiku_vector_mask_t m = tiku_vector_path_fill_cancel(ink, s->fill,
                                                            x, y, w, h,
                                                            cancel, ctx);

        if (!m.a) {
            err = cancel && cancel(ctx) ? TIKU_CIRCUIT_CANCELLED
                                        : TIKU_CIRCUIT_NOMEM;
            break;
        }
        if (coverage) {
            circuit_artwork_apply(coverage, im.w, &m, s->clear);
        } else {
            paint(&im, &m, s->colour);
        }
        tiku_vector_mask_free(&m);
    }
    tiku_vector_path_free(&p);
    tiku_vector_path_free(&stroke);
    if (!err && cancel && cancel(ctx)) {
        err = TIKU_CIRCUIT_CANCELLED;
    }
    if (!err && coverage) {
        if (transparent) {
            for (size_t i = 0; i < (size_t)im.w * im.h; i++) {
                im.px[i] = ((uint32_t)coverage[i] << 24) |
                           (colour & ~CIRCUIT_OPAQUE);
            }
        } else {
            circuit_artwork_colour(&im, coverage);
        }
    }
    if (!err && cancel && cancel(ctx)) {
        err = TIKU_CIRCUIT_CANCELLED;
    }
    free(coverage);
    if (err) {
        free(im.px);
        return err;
    }
    *out = im;
    return TIKU_CIRCUIT_OK;
}

tiku_circuit_err_t
tiku_circuit_render(const tiku_circuit_t *d, const tiku_circuit_view_t *v,
                    int (*cancel)(void *), void *ctx, tiku_image_t *out)
{
    if (!d || !out || out->px) {
        return TIKU_CIRCUIT_BAD;
    }
    return d->layer_count ? circuit_stack_render(d, v, cancel, ctx, out)
                          : circuit_render_flat(d, v, cancel, ctx, out, 0, 0);
}
