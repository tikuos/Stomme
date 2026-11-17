/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_font_subset.c - immutable baked face and replacement lookup.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_font_subset.h"

const tiku_font_t *tiku_font_subset_face(int face, int pixels)
{
    return face >= 0 && face < 2 && pixels == 12 ?
        &tiku_font_subset_fonts[face] : NULL;
}

const tiku_glyph_t *tiku_font_subset_glyph(int face, unsigned codepoint)
{
    const tiku_font_t *font = tiku_font_subset_face(face, 12);
    int lo = 0, hi;
    if (font == NULL) {
        return NULL;
    }
    hi = font->count;
    while (lo < hi) {
        int mid = lo + (hi - lo) / 2;
        unsigned cp = (unsigned)font->glyphs[mid].cp;
        if (cp == codepoint) {
            return &font->glyphs[mid];
        }
        if (cp < codepoint) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return &font->glyphs['?' - 32];
}
