/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_font_portable.c - immutable subset metrics and coverage drawing.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_font_subset.h"
#include "tiku_measure.h"
#include <limits.h>
#include "tiku_font_utf8.inl"
#include "tiku_font_blend.inl"

const tiku_font_t *tiku_font_plain(void) { return tiku_font_subset_face(0, 12); }
const tiku_font_t *tiku_font_bold(void) { return tiku_font_subset_face(1, 12); }
const tiku_font_t *tiku_font_at(int pixels) { return tiku_font_subset_face(0, pixels); }
const tiku_font_t *tiku_font_mono(int pixels) { (void)pixels;return tiku_font_plain(); }
int tiku_font_size(void) { return 12; }
unsigned tiku_font_generation(void) { return 1; }

/** @brief Resolve a selected immutable face; reject foreign or outline faces. */
static int face_number(const tiku_font_t *font)
{
    if (font == tiku_font_plain()) return 0;
    if (font == tiku_font_bold()) return 1;
    return -1;
}

int tiku_text_advance(const tiku_font_t *font, unsigned codepoint)
{
    const tiku_glyph_t *glyph = tiku_font_subset_glyph(face_number(font), codepoint);
    return glyph ? glyph->adv : 0;
}

int tiku_text_width(const tiku_font_t *font, const char *text)
{
    int width = 0;
    if (!text || face_number(font) < 0) return 0;
    while (*text) {
        int advance = tiku_text_advance(font, utf8_next(&text));
        if (width > INT_MAX - advance) return INT_MAX;
        width += advance;
    }
    return width;
}

int tiku_text_height(const tiku_font_t *font)
{
    return face_number(font) >= 0 ? font->height : 0;
}

void tiku_text(tiku_surface_t *surface, const tiku_font_t *font, int x, int y,
               const char *text, tiku_rgb_t color)
{
    int face = face_number(font), pen = x;
    if (!surface || !text || face < 0 || surface->scale > 1) return;
    if (surface->clip.w <= 0 || surface->clip.h <= 0) return;
    while (*text) {
        const tiku_glyph_t *glyph = tiku_font_subset_glyph(face, utf8_next(&text));
        int64_t left = (int64_t)pen + glyph->ox, top = (int64_t)y + glyph->oy;
        int64_t first_x = surface->clip.x - left;
        int64_t first_y = surface->clip.y - top;
        int64_t end_x = (int64_t)surface->clip.x + surface->clip.w - left;
        int64_t end_y = (int64_t)surface->clip.y + surface->clip.h - top;
        if (first_x < 0) first_x = 0;
        if (first_y < 0) first_y = 0;
        if (end_x > glyph->w) end_x = glyph->w;
        if (end_y > glyph->h) end_y = glyph->h;
        if (first_x < end_x && first_y < end_y) {
            int x0 = (int)first_x, x1 = (int)end_x;
            int y0 = (int)first_y, y1 = (int)end_y;
            for (int row = y0; row < y1; ++row) {
                for (int column = x0; column < x1; ++column) {
                    blend(surface, 1, (int)(left + column), (int)(top + row),
                          color, font->bits[glyph->off + row * glyph->w + column]);
                }
            }
        }
        if (pen > INT_MAX - glyph->adv) return;
        pen += glyph->adv;
    }
}

int tiku_text_centered(tiku_surface_t *surface, const tiku_font_t *font,
                        tiku_rect_t rectangle, const char *text, tiku_rgb_t color)
{
    int x;
    if (face_number(font) < 0) return rectangle.x;
    x = rectangle.x + (rectangle.w - tiku_text_width(font, text)) / 2;
    tiku_text(surface, font, x,
              rectangle.y + (rectangle.h - font->height) / 2 + font->ascent,
              text, color);
    return x;
}

/** @brief Resolve plain/bold metrics; the control profile uses plain for other handles. */
static const tiku_font_t *measure_face(int face)
{
    return face == TIKU_MEASURE_BOLD ? tiku_font_bold() : tiku_font_plain();
}

int tiku_measure_width(int face, const char *text) { return tiku_text_width(measure_face(face), text); }
int tiku_measure_height(int face) { return measure_face(face)->height; }
int tiku_measure_ascent(int face) { return measure_face(face)->ascent; }
int tiku_measure_heading(int level) { (void)level; return TIKU_MEASURE_BOLD; }
int tiku_measure_row_h(void) { return tiku_font_plain()->height + 7; }
int tiku_measure_step_w(void) { return tiku_font_plain()->height + 11; }
int tiku_measure_font_size(void) { return 12; }
unsigned tiku_measure_font_stamp(void) { return 1; }
int tiku_measure_font_face(int family, int pixels, unsigned cut)
{
    (void)family; (void)pixels;
    return cut & TIKU_MEASURE_FONT_BOLD ? TIKU_MEASURE_BOLD : TIKU_MEASURE_PLAIN;
}
