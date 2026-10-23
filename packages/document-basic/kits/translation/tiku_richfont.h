/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_richfont.h - owned native fonts for document shaping and glyph drawing.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_RICHFONT_H_
#define TIKU_RICHFONT_H_

#include "tiku_richlayout.h"
#include "tiku_richview.h"
#include "tiku_ttf.h"
#include "tiku_gfx.h"

/** @brief Owned font faces with stable glyph IDs for retained layouts. */
typedef struct tiku_richfont tiku_richfont_t;
/** @brief An explicit installed font face; input strings are copied. */
typedef struct {
    const char *path;
    unsigned index;           /**< collection face index, zero for a single */
    const char *family;       /**< requested family, NULL uses the file name */
    unsigned bold, italic;    /**< actual variant, each zero or one */
} tiku_richfont_source_t;

/** @brief Open 1..32 faces, each at most 32 MiB and together at most 128 MiB.
 *  @note Failure clears *out. Fonts use a separate budget from document
 *        storage. File bytes are read and owned before any layout begins. */
tiku_rich_status_t tiku_richfont_open(const tiku_richfont_source_t *sources,
    size_t count, tiku_richfont_t **out);
/** @brief Discover four variants of a family using the installed font index.
 *  @note NULL or empty family selects sans serif. Missing exact faces are
 *        reported by layout's FALLBACK warning; bold/slant are not synthesized.
 *        The configured font directories are searched by the font index. */
tiku_rich_status_t tiku_richfont_system(const char *family,
                                      tiku_richfont_t **out);
/** @brief Obtain a provider whose retain/release callbacks own layout refs.
 *  @note The provider shapes individual graphemes and maps font advances.
 *        Combining marks, joining/bidi scripts and emoji sequences set the
 *        SHAPING warning. It performs no OpenType substitution or positioning.
 *        Calls on one font set, including rendering, must be serialized. */
tiku_rich_provider_t tiku_richfont_provider(tiku_richfont_t *fonts);
/** @brief Release the caller's font reference; layouts retain their own. */
void tiku_richfont_release(tiku_richfont_t *fonts);
/** @brief Rasterize a provider glyph ID at the specified native pixel size.
 *  @note Size is 1..512. Caller frees successful coverage with
 *        tiku_ttf_free_glyph(). Invalid input clears *out. */
tiku_rich_status_t tiku_richfont_raster(tiku_richfont_t *fonts,
    uint32_t font, uint32_t glyph, int size, tiku_ttf_glyph_t *out);
/** @brief Paint up to line_count lines using retained glyph geometry.
 *  @note Origin and zoom map document points to surface logical pixels.
 *        Zoom is 0.1..8; native glyph sizes exceeding 512 pixels return LIMIT.
 *        Fonts must be the layout's provider. Drawing respects surface clip
 *        and scale. Failure may leave partial pixels; paint into a frame
 *        surface before publication. No window or host service is accessed. */
tiku_rich_status_t tiku_richfont_draw(tiku_richfont_t *fonts,
    const tiku_richlayout_t *layout, tiku_surface_t *surface,
    double x, double y, double zoom, size_t first_line, size_t line_count);
/** @brief Paint visible view lines, selection tint and an optional caret.
 *  @note The viewport is in logical surface pixels and must match the size
 *        passed to view_viewport(). The caller paints the background first.
 *        Selection is an ARGB overlay; caret is RGB and is hidden for a
 *        nonempty selection. Clip is intersected and restored on every path.
 *        Pending/refused geometry paints nothing. Drawing may fail partially;
 *        publish the frame only after OK. This function performs no reflow. */
tiku_rich_status_t tiku_richfont_draw_view(tiku_richfont_t *fonts,
    tiku_richview_t *view, tiku_surface_t *surface, tiku_rect_t viewport,
    uint32_t selection, tiku_rgb_t caret, int show_caret);

#endif /* TIKU_RICHFONT_H_ */
