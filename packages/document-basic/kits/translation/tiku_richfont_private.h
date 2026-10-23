/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_richfont_private.h - stable native face records and provider callbacks.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_RICHFONT_PRIVATE_H_
#define TIKU_RICHFONT_PRIVATE_H_

#include "tiku_richfont.h"
#include "tiku_richpages.h"

/** @brief One immutable face's family, variant and logical line metrics. */
typedef struct {
    tiku_ttf_t *ttf;          /**< owned parsed font and copied source bytes */
    char family[64];         /**< copied family used for style matching */
    unsigned bold, italic;   /**< actual face variant */
    unsigned index;          /**< selected face in the retained source file */
    int upem, ascent, height; /**< font-unit scale and line metrics */
} tiku_rf_face_t;
/** @brief Stable face slots, owned independently from document allocations. */
struct tiku_richfont {
    unsigned refs;            /**< serialized caller and layout references */
    size_t count;             /**< successfully opened faces */
    tiku_rf_face_t face[32];   /**< bounded font-ID slots */
    unsigned warnings;       /**< discovery substitutions */
};

/** @brief Supply grapheme glyphs and metrics without mutating the document. */
tiku_rich_status_t tiku_rf_shape(void *context, const tiku_rich_chars_t *chars,
    const char *text, size_t length, const uint8_t *boundaries,
    tiku_rich_shape_t *out);

/** @brief Paint one validated pagination piece at the printable-area origin. */
tiku_rich_status_t tiku_rf_draw_piece(tiku_richfont_t *fonts,
    const tiku_richlayout_t *layout, tiku_surface_t *surface,
    double x, double y, double zoom, const tiku_rich_page_piece_t *piece);

#endif /* TIKU_RICHFONT_PRIVATE_H_ */
