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
    while (*text) {
        const tiku_glyph_t *glyph = tiku_font_subset_glyph(face, utf8_next(&text));
        int row, column;
        for (row = 0; row < glyph->h; ++row) {
            for (column = 0; column < glyph->w; ++column) {
                blend(surface, 1, pen + glyph->ox + column, y + glyph->oy + row,
                      color, font->bits[glyph->off + row * glyph->w + column]);
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
