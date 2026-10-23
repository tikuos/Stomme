/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_richfont_shape.c - cmap glyphs and advances with explicit diagnostics.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richfont_private.h"

#include <string.h>
#include <strings.h>

/** @brief Decode one scalar from the layout's validated UTF-8 input. */
static uint32_t
scalar(const char *text, size_t *offset)
{
    uint32_t value = (unsigned char)text[(*offset)++];
    unsigned count = value < 0x80 ? 0 : value < 0xe0 ? 1 :
                     value < 0xf0 ? 2 : 3;

    if (count) value &= (1u << (6 - count)) - 1;
    while (count--) value = (value << 6) |
        ((unsigned char)text[(*offset)++] & 63);
    return value;
}

/** @brief Select the closest registered family, weight and slant. */
static size_t
face_for(const tiku_richfont_t *fonts, const tiku_rich_chars_t *chars,
         unsigned *warnings)
{
    size_t i, best = 0;
    unsigned score, distance = 100;

    for (i = 0; i < fonts->count; i++) {
        score = chars->font[0] && strcasecmp(chars->font,
                          fonts->face[i].family) ? 8 : 0;
        score += (chars->bold != fonts->face[i].bold) * 2;
        score += chars->italic != fonts->face[i].italic;
        if (score < distance) { distance = score; best = i; }
    }
    if (distance) *warnings |= TIKU_RICH_LAYOUT_FALLBACK;
    return best;
}

/** @brief Choose a registered fallback face when the preferred cmap misses. */
static size_t
glyph_for(const tiku_richfont_t *fonts, size_t preferred, uint32_t cp,
          unsigned *glyph, unsigned *warnings)
{
    size_t i;

    *glyph = tiku_ttf_glyph(fonts->face[preferred].ttf, cp);
    if (*glyph || cp < 32) return preferred;
    *warnings |= TIKU_RICH_LAYOUT_FALLBACK;
    for (i = 0; i < fonts->count; i++) {
        *glyph = tiku_ttf_glyph(fonts->face[i].ttf, cp);
        if (*glyph) return i;
    }
    return preferred;
}

tiku_rich_status_t
tiku_rf_shape(void *context, const tiku_rich_chars_t *chars,
    const char *text, size_t length, const uint8_t *boundaries,
    tiku_rich_shape_t *out)
{
    tiku_richfont_t *fonts = context;
    size_t end = 1, offset = 0, preferred, face;
    uint32_t cp;
    unsigned glyph;
    double size, ascent, descent;

    if (!fonts || !chars || !text || !length || !boundaries || !out)
        return TIKU_RICH_INVALID;
    memset(out, 0, sizeof *out);
    out->warnings = fonts->warnings;
    while (end < length && !(boundaries[end] & 1)) end++;
    preferred = face_for(fonts, chars, &out->warnings);
    while (offset < end) {
        if (out->glyph_count == 32) return TIKU_RICH_LIMIT;
        cp = scalar(text, &offset);
        face = glyph_for(fonts, preferred, cp, &glyph, &out->warnings);
        size = (double)chars->size / fonts->face[face].upem;
        out->glyphs[out->glyph_count++] = (tiku_rich_glyph_t){
            (uint32_t)face + 1, glyph, out->advance, 0};
        if (cp >= 32)
            out->advance += tiku_ttf_advance(fonts->face[face].ttf, glyph) *
                             size;
        ascent = fonts->face[face].ascent * size;
        descent = (fonts->face[face].height - fonts->face[face].ascent) * size;
        if (ascent > out->ascent) out->ascent = ascent;
        if (descent > out->descent) out->descent = descent;
        if ((cp >= 0x590 && cp <= 0x1cff) ||
            (cp >= 0x200c && cp <= 0x200f) ||
            (cp >= 0x202a && cp <= 0x202e) ||
            (cp >= 0x2066 && cp <= 0x206f) ||
            (cp >= 0xa800 && cp <= 0xabff) ||
            (cp >= 0xfb1d && cp <= 0xfdff) ||
            (cp >= 0xfe70 && cp <= 0xfefc) || cp >= 0x1f000)
            out->warnings |= TIKU_RICH_LAYOUT_SHAPING;
    }
    if (out->glyph_count > 1) out->warnings |= TIKU_RICH_LAYOUT_SHAPING;
    out->bytes = end;
    out->caret_count = 2;
    out->carets[1] = (tiku_rich_shape_caret_t){end, out->advance};
    return TIKU_RICH_OK;
}
