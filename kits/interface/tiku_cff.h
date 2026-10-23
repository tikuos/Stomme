/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cff.h - PostScript outlines: the CFF and CFF2 font tables.
 *
 * An OpenType file with PostScript outlines keeps them in a CFF or CFF2
 * table: INDEXes and DICTs, and per glyph a Type 2 charstring, a small
 * program that draws the glyph.  A PDF embeds the same table on its own.
 * Charstrings draw cubics where TrueType has quadratics; both end up in
 * the same path.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_CFF_H_
#define TIKU_CFF_H_

#include <stddef.h>

#include "tiku_glyphpath.h"

typedef struct tiku_cff tiku_cff_t;

/**
 * @brief Read the CFF or CFF2 table at @p data, @p len bytes of it.
 * @return the handle, or NULL for a table this reader cannot draw from
 *         (Type 1 charstrings among them).
 * @note The bytes are borrowed, not copied: they must outlive the handle.
 */
tiku_cff_t *tiku_cff_open(const unsigned char *data, size_t len);

/** @brief Free @p cff; NULL is allowed. */
void tiku_cff_close(tiku_cff_t *cff);

/**
 * @brief Run glyph @p gid's charstring into @p path.
 *
 * Coordinates are pushed in device space, scaled and grid-fitted by
 * @p hint, with y downwards, as the path expects.
 *
 * @return 1 when the glyph was drawn (an empty glyph counts), 0 for a
 *         malformed charstring.
 */
int tiku_cff_outline(const tiku_cff_t *cff, unsigned gid,
                          const tiku_hint_t *hint,
                          tiku_path_t *path);

/** @brief Units per em as the font's FontMatrix states it, or 1000 when
 *         it states none or a size outside 16 to 16384. */
float tiku_cff_upem(const tiku_cff_t *cff);

/* A PDF names a glyph by its name, its code in the font's own encoding
 * or, in a CID-keyed font, its CID: the charset answers a name or a CID,
 * the encoding a code. */

/** @brief The glyph called @p name, or 0 when the font has none or is
 *         CID-keyed. */
unsigned tiku_cff_glyph_named(const tiku_cff_t *cff, const char *name);
/** @brief The glyph the font's own encoding gives @p code, or 0: the
 *        standard encoding, or a custom one with its supplements.  A
 *        CID-keyed font has no encoding and gives 0. */
unsigned tiku_cff_glyph_coded(const tiku_cff_t *cff, unsigned code);
/** @brief The glyph with CID @p cid in a CID-keyed font, or 0; in any
 *        other font, glyph @p cid itself while the font has that many. */
unsigned tiku_cff_glyph_cid(const tiku_cff_t *cff, unsigned cid);
/** @brief Whether the font is CID-keyed. */
int tiku_cff_is_cid(const tiku_cff_t *cff);
/** @brief How many glyphs the font has. */
int tiku_cff_glyphs(const tiku_cff_t *cff);

/** @brief Standard string @p sid of the 391 every CFF shares, or NULL. */
const char *tiku_cff_standard_string(unsigned sid);

#endif /* TIKU_CFF_H_ */
