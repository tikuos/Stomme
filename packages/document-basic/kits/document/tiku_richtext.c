/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_richtext.c - checked UTF-8 segmentation and pinned property lookup.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richtext_private.h"

#include <string.h>

/** @brief One inclusive range sharing packed segmentation properties. */
typedef struct {
    uint32_t first, last;     /**< inclusive scalar range */
    unsigned properties;     /**< grapheme/word/Indic/EP packed fields */
} unicode_range_t;

#include "tiku_richtext_data.inl"

tiku_rt_props_t
tiku_rt_properties(uint32_t scalar)
{
    size_t first = 0, last = sizeof unicode_ranges / sizeof *unicode_ranges;
    unsigned value = 0;

    while (first < last) {
        size_t middle = first + (last - first) / 2;

        if (scalar < unicode_ranges[middle].first)
            last = middle;
        else if (scalar > unicode_ranges[middle].last)
            first = middle + 1;
        else {
            value = unicode_ranges[middle].properties;
            break;
        }
    }
    return (tiku_rt_props_t){value & 15, (value >> 4) & 31,
                             (value >> 9) & 3, !!(value & 2048)};
}

tiku_rich_status_t
tiku_rt_decode(const char *text, size_t length, size_t *offset,
                 uint32_t *scalar)
{
    size_t at = *offset, count, i;
    uint32_t cp, minimum;
    unsigned first;

    if (at >= length || !text)
        return TIKU_RICH_INVALID_TEXT;
    first = (unsigned char)text[at++];
    if (first < 0x80) { cp = first; count = 0; minimum = 0; }
    else if (first >= 0xc2 && first <= 0xdf) {
        cp = first & 31; count = 1; minimum = 0x80;
    } else if (first >= 0xe0 && first <= 0xef) {
        cp = first & 15; count = 2; minimum = 0x800;
    } else if (first >= 0xf0 && first <= 0xf4) {
        cp = first & 7; count = 3; minimum = 0x10000;
    } else
        return TIKU_RICH_INVALID_TEXT;
    if (count > length - at)
        return TIKU_RICH_INVALID_TEXT;
    for (i = 0; i < count; i++) {
        unsigned byte = (unsigned char)text[at++];

        if ((byte & 0xc0) != 0x80)
            return TIKU_RICH_INVALID_TEXT;
        cp = (cp << 6) | (byte & 63);
    }
    if (cp < minimum || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff))
        return TIKU_RICH_INVALID_TEXT;
    *scalar = cp;
    *offset = at;
    return TIKU_RICH_OK;
}

tiku_rich_status_t
tiku_richtext_boundaries(const char *text, size_t length,
                           uint8_t *flags, size_t capacity)
{
    tiku_rt_grapheme_t grapheme = {0};
    tiku_rt_word_t word = {0};
    tiku_rt_props_t props;
    uint32_t scalar;
    size_t at = 0, start;
    tiku_rich_status_t status;

    if ((!text && length) || !flags)
        return TIKU_RICH_INVALID;
    if (length == SIZE_MAX || capacity <= length)
        return TIKU_RICH_LIMIT;
    while (at < length) {
        status = tiku_rt_decode(text, length, &at, &scalar);
        if (status != TIKU_RICH_OK)
            return status;
    }
    memset(flags, 0, length + 1);
    at = 0;
    while (at < length) {
        start = at;
        tiku_rt_decode(text, length, &at, &scalar);
        props = tiku_rt_properties(scalar);
        if (tiku_rt_grapheme(&grapheme, props))
            flags[start] |= TIKU_RICH_GRAPHEME;
        if (tiku_rt_word(&word, props, text, length, at))
            flags[start] |= TIKU_RICH_WORD;
    }
    flags[0] = flags[length] = TIKU_RICH_GRAPHEME | TIKU_RICH_WORD;
    return TIKU_RICH_OK;
}
