/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_circuit_text.c - outline text in a scene: a string placed through
 * a matrix, its box measured in the substitute font, and its glyphs drawn
 * at the view's scale.  Text with a character the font lacks is refused
 * as TIKU_CIRCUIT_UNSUPPORTED.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_circuit_private.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define TEXT_CORNERS    4       /* a text box's points                   */
#define TEXT_LINE_PITCH 1.4     /* em from one baseline to the next      */
#define TEXT_PAD_X      0.12    /* em the box reaches past the advance   */
#define TEXT_PAD_Y      0.3     /* em the box reaches past the last line */
#define TEXT_BASELINE   0.8     /* em from a line's top to its baseline  */
#define TEXT_BOLD_SHIFT 0.035   /* em between a bold glyph's two passes  */

/** @brief Decode one Unicode scalar from the UTF-8 at @p at and step past
 *         it.  A zero byte ends a sequence early and is left in place, so
 *         a string cut inside a character stops at its terminator. */
static unsigned
next(const char **at)
{
    const unsigned char *s = (const unsigned char *)*at;
    unsigned c = *s++, n = 0;

    if (c >= 0xf0) {
        c &= 7;
        n = 3;
    } else if (c >= 0xe0) {
        c &= 15;
        n = 2;
    } else if (c >= 0xc0) {
        c &= 31;
        n = 1;
    }
    while (n-- && *s) {
        c = c * 64 + (*s++ & 63);
    }
    *at = (const char *)s;
    return c;
}

double
circuit_text_line_width(const tiku_circuit_t *d, const char *text)
{
    if (!d || !d->font || !text) {
        return -1;
    }
    double width = 0;

    while (*text && *text != '\n') {
        unsigned cp = next(&text), gid = tiku_ttf_glyph(d->font, cp);

        if (!gid) {
            return -1;
        }
        width += tiku_ttf_advance(d->font, gid) /
                 (double)tiku_ttf_upem(d->font);
    }
    return width;
}

int
circuit_text_extent(const tiku_circuit_t *d, const char *text,
                    double *width, double *height)
{
    double widest = 0;
    size_t lines = 1;

    for (const char *p = text; p;) {
        double w = circuit_text_line_width(d, p);

        if (w < 0) {
            return 0;
        }
        widest = fmax(widest, w);
        p = strchr(p, '\n');
        if (p) {
            p++;
            lines++;
        }
    }
    *width = widest;
    *height = TEXT_LINE_PITCH * (lines - 1) + 1;
    return 1;
}

tiku_circuit_err_t
circuit_text(tiku_circuit_t *d, const char *name, const char *value,
             const char *text, const double matrix[6], int justify,
             int vertical, int bold, uint32_t colour)
{
    if (!d || !d->font || !text || !matrix) {
        return TIKU_CIRCUIT_MISSING;
    }
    size_t bytes = strlen(text) + 1;

    if (bytes > CIRCUIT_TEXT_MAX + 1 ||
        bytes > CIRCUIT_BYTES_MAX - d->bytes) {
        return TIKU_CIRCUIT_LIMIT;
    }
    if (!name || !value) {
        return TIKU_CIRCUIT_BAD;
    }
    size_t backing = TEXT_CORNERS * sizeof(circuit_point_t) +
                     sizeof(circuit_shape_t) +
                     strnlen(name, CIRCUIT_NAME_BYTES) +
                     strnlen(value, CIRCUIT_NAME_BYTES) + 2;

    if (backing > CIRCUIT_BYTES_MAX - d->bytes - bytes) {
        return TIKU_CIRCUIT_LIMIT;
    }
    for (int i = 0; i < 6; i++) {
        if (!isfinite(matrix[i])) {
            return TIKU_CIRCUIT_BAD;
        }
    }
    double width, h;

    if (!circuit_text_extent(d, text, &width, &h)) {
        return TIKU_CIRCUIT_UNSUPPORTED;
    }
    double x = justify < 0 ? 0 : justify > 0 ? -width : -width / 2;
    double y = vertical < 0 ? 0 : vertical > 0 ? -h : -h / 2;
    circuit_point_t box[TEXT_CORNERS];
    const double dx[] = {x, x + width + TEXT_PAD_X, x + width + TEXT_PAD_X,
                         x};
    const double dy[] = {y, y, y + h + TEXT_PAD_Y, y + h + TEXT_PAD_Y};

    for (int i = 0; i < TEXT_CORNERS; i++) {
        box[i] = (circuit_point_t){
            matrix[0] * dx[i] + matrix[2] * dy[i] + matrix[4],
            matrix[1] * dx[i] + matrix[3] * dy[i] + matrix[5]};
    }
    char *copy = strdup(text);

    if (!copy) {
        return TIKU_CIRCUIT_NOMEM;
    }
    tiku_circuit_err_t e = circuit_add(d, name, value, box, TEXT_CORNERS,
                                       0, colour, 1, 1);

    if (e) {
        free(copy);
        return e;
    }
    circuit_shape_t *s = &d->shapes[d->count - 1];

    s->kind = CIRCUIT_TEXT;
    s->text = copy;
    s->bold = bold;
    memcpy(s->matrix, matrix, sizeof s->matrix);
    s->cx = justify;
    s->cy = y + TEXT_BASELINE;
    d->bytes += bytes;
    return TIKU_CIRCUIT_OK;
}

/** @brief Blend @p colour into @p im through the @p w by @p h glyph
 *         @p mask placed at (@p x0, @p y0). */
static void
paint(tiku_image_t *im, int x0, int y0, int w, int h,
      const unsigned char *mask, uint32_t colour)
{
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            unsigned a = mask[(size_t)y * w + x];
            uint32_t *p = &im->px[(size_t)(y0 + y) * im->w + x0 + x];
            uint32_t v = CIRCUIT_OPAQUE;

            for (int shift = 0; shift <= 16; shift += 8) {
                v |= ((((*p >> shift) & 255) * (255 - a) +
                       ((colour >> shift) & 255) * a + 127) / 255) << shift;
            }
            *p = v;
        }
    }
}

int
circuit_text_paint(const tiku_circuit_t *d, const circuit_shape_t *s,
                   const tiku_circuit_view_t *v, tiku_image_t *im,
                   int (*cancel)(void *), void *ctx)
{
    tiku_path_t *path = tiku_path_new();

    if (!path) {
        return TIKU_CIRCUIT_NOMEM;
    }
    tiku_hint_t hint = {.scale = 1.0f / tiku_ttf_upem(d->font)};
    const char *text = s->text;
    double baseline = s->cy, pen = 0;
    double width = circuit_text_line_width(d, text);
    int error = 0;

    while (*text && !error) {
        if (cancel && cancel(ctx)) {
            error = TIKU_CIRCUIT_CANCELLED;
            break;
        }
        unsigned cp = next(&text);

        if (cp == '\n') {
            baseline += TEXT_LINE_PITCH;
            pen = 0;
            width = circuit_text_line_width(d, text);
            continue;
        }
        unsigned gid = tiku_ttf_glyph(d->font, cp);
        double x = pen + (s->cx < 0 ? 0 : s->cx > 0 ? -width : -width / 2);

        for (int pass = 0; pass <= s->bold; pass++) {
            double shift = x + pass * TEXT_BOLD_SHIFT;
            float m[6];

            for (int k = 0; k < 4; k++) {
                m[k] = (float)(s->matrix[k] * v->scale);
            }
            m[4] = (float)((s->matrix[0] * shift + s->matrix[2] * baseline +
                            s->matrix[4]) * v->scale + v->x);
            m[5] = (float)((s->matrix[1] * shift + s->matrix[3] * baseline +
                            s->matrix[5]) * v->scale + v->y);
            tiku_path_reset(path);
            tiku_path_transform(path, m);
            if (!tiku_ttf_outline(d->font, gid, &hint, path) ||
                tiku_path_failed(path)) {
                error = TIKU_CIRCUIT_NOMEM;
                break;
            }
            int ax, ay, w, h;

            tiku_path_bounds(path, &ax, &ay, &w, &h);
            if (w <= 0 || h <= 0) {
                continue;
            }
            int bx = ax + w, by = ay + h;

            ax = ax < 0 ? 0 : ax;
            ay = ay < 0 ? 0 : ay;
            bx = bx > im->w ? im->w : bx;
            by = by > im->h ? im->h : by;
            if (bx <= ax || by <= ay) {
                continue;
            }
            unsigned char *mask = tiku_path_render(path, ax, ay, bx - ax,
                                                   by - ay, 0);

            if (!mask) {
                error = TIKU_CIRCUIT_NOMEM;
                break;
            }
            paint(im, ax, ay, bx - ax, by - ay, mask, s->colour);
            free(mask);
        }
        pen += tiku_ttf_advance(d->font, gid) /
               (double)tiku_ttf_upem(d->font);
    }
    tiku_path_free(path);
    return error;
}
