/*
 * TikuDesktop Translation Kit.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_richoutput.h - document page preview and vector PDF output.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_RICHOUTPUT_H_
#define TIKU_RICHOUTPUT_H_
#include "tiku_richfont.h"
#include "tiku_richpages.h"

/** @brief Paint one completed page using its retained line/glyph geometry.
 *  @note x/y are the paper origin in logical pixels; zoom is 0.1..8.
 *        Paints paper and content, preserving caller clip. Fonts must match
 *        the page layout. Pixel dimensions are checked before conversion. */
tiku_rich_status_t tiku_richoutput_page(tiku_richfont_t *fonts,
    const tiku_richpages_t *pages, size_t page, tiku_surface_t *surface,
    double x, double y, double zoom);
/** @brief Export completed pages as PDF with embedded fonts and Unicode text.
 *  @note Uses retained glyph IDs/positions, not a second layout algorithm.
 *        Up to 250000 clusters and 500000 glyphs; output at most 64 MiB.
 *        Existing shaping/fallback warnings still apply. cancel is polled
 *        between lines. Blocking worker operation; serialize font-set use.
 *  @return OK with malloc-owned *out; otherwise NULL and zero *length. */
tiku_rich_status_t tiku_richoutput_pdf(tiku_richfont_t *fonts,
    const tiku_richpages_t *pages, int (*cancel)(void *), void *context,
    unsigned char **out, size_t *length);
#endif
