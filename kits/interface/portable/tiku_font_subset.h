/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_font_subset.h - immutable profile-selected UI fonts.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_FONT_SUBSET_H_
#define TIKU_FONT_SUBSET_H_
#include "tiku_font.h"

/** @brief Profile font tables: plain and bold, with no outline source. */
extern const tiku_font_t tiku_font_subset_fonts[2];

/** @brief Return plain (0) or bold (1) at 12 px; NULL for other requests. */
const tiku_font_t *tiku_font_subset_face(int face, int pixels);

/** @brief Return a glyph, using '?' for absent codepoints; NULL for invalid face. */
const tiku_glyph_t *tiku_font_subset_glyph(int face, unsigned codepoint);
#endif
