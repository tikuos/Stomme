/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_ttf.h - reading a font somebody dropped in.
 *
 * A TrueType file is a table directory, a character map and a set of
 * quadratic outlines; an OpenType file may hold CFF outlines instead,
 * which tiku_cff.h reads.  Rasterising either is the same signed-area
 * coverage the icon renderer resolves its paths with.  No font library is
 * used, so a device handed a face over a wire can draw with it.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TTF_H_
#define TIKU_TTF_H_

#include <stddef.h>

#include "tiku_glyphpath.h"

typedef struct tiku_ttf tiku_ttf_t;

/**
 * @brief Open a TrueType or OpenType file, the first face of a collection.
 *
 * @return NULL for anything that cannot be drawn with.
 */
tiku_ttf_t *tiku_ttf_open(const char *path);
/** @brief Face @p face of a collection (.ttc); 0 is the first, and the
 *         only one a file of one face has. */
tiku_ttf_t *tiku_ttf_open_face(const char *path, unsigned face);
/** @brief Open the first face held in @p len bytes at @p data, which are
 *         copied. */
tiku_ttf_t *tiku_ttf_open_mem(const unsigned char *data, size_t len);
/** @brief Open a selected collection face from copied memory. */
tiku_ttf_t *tiku_ttf_open_mem_face(const unsigned char *data, size_t len,
                                 unsigned face);

/** @brief Close @p ttf and free its bytes; NULL is ignored. */
void tiku_ttf_close(tiku_ttf_t *ttf);
/** @brief Borrow the immutable original font bytes until the face is closed.
 *  @note This can be a font collection; callers retain the selected index
 *        separately. NULL face returns NULL and stores zero when length exists. */
const unsigned char *tiku_ttf_source(const tiku_ttf_t *ttf, size_t *length);

/* For a reader that places glyphs itself -- a PDF names them by index
 * and draws them at any size and slant -- the face by its parts. */

/** @brief Units per em. */
int tiku_ttf_upem(const tiku_ttf_t *ttf);
/** @brief How many glyphs the face has. */
int tiku_ttf_glyphs(const tiku_ttf_t *ttf);
/** @brief The glyph the character map gives @p cp, or 0. */
unsigned tiku_ttf_glyph(const tiku_ttf_t *ttf, unsigned cp);
/** @brief Glyph @p gid's advance, in font units. */
int tiku_ttf_advance(const tiku_ttf_t *ttf, unsigned gid);
/**
 * @brief Glyph @p gid's outline into @p path, through @p hint, whose
 *        scale is applied to font units (a CFF face's own units).
 * @return 1 when it was drawn, an empty glyph included.
 */
int tiku_ttf_outline(const tiku_ttf_t *ttf, unsigned gid,
                     const tiku_hint_t *hint, tiku_path_t *path);

/**
 * @brief The family name the file gives itself, or the file's name when it
 *        gives none.
 */
const char *tiku_ttf_family(const tiku_ttf_t *ttf);

/** @brief Whether the face calls itself bold. */
int tiku_ttf_bold(const tiku_ttf_t *ttf);

/**
 * @brief Whether the first four bytes of @p path are a TrueType, OpenType
 *        or collection signature; nothing more of the file is read.
 */
int tiku_ttf_is_font(const char *path);

/**
 * @brief Ascent and line height in whole pixels at @p px, from hhea; the
 *        height leaves out the line gap.
 *
 * Both may be NULL.
 */
void tiku_ttf_metrics(const tiku_ttf_t *ttf, int px,
                           int *ascent, int *height);

/**
 * @brief A line of the face in font units, as word processors measure one:
 *        the baseline's depth from its top (hhea's ascent or the Windows
 *        one, whichever is larger) and a single-spaced line's height (the
 *        Windows extent, or hhea's with its gap if that is more).  Either
 *        may be NULL.
 */
void tiku_ttf_line(const tiku_ttf_t *ttf, int *ascent, int *height);

/** @brief Where the face draws an underline and a strikeout, in font
 *         units up from the baseline, and how thick; 0 where it does not
 *         say.  Any may be NULL. */
void tiku_ttf_lines(const tiku_ttf_t *ttf, int *under, int *under_size,
                    int *strike, int *strike_size);

/** @brief One rasterised glyph.  @c cover is w*h bytes of coverage. */
typedef struct {
    int            adv;         /**< the advance, in pixels            */
    int            w, h;
    int            ox, oy;      /**< from the pen, and from the baseline */
    unsigned char *cover;       /**< NULL for a glyph with no ink      */
} tiku_ttf_glyph_t;

/**
 * @brief Draw @p cp at @p px pixels, at most 512.
 *
 * A code point the file has no glyph for is not drawn, and the caller
 * shows its own stand-in.
 *
 * @return 1 when @p out was filled, a glyph with no ink included; 0
 *         otherwise.
 * @note Each call owns its outline. Serialize concurrent rendering on one
 *       face because its hint-zone cache is mutable.
 */
int tiku_ttf_render(const tiku_ttf_t *ttf, unsigned cp, int px,
                         tiku_ttf_glyph_t *out);
/** @brief Rasterize a glyph ID, including glyph zero, at 1..512 pixels.
 *  @return 1 on success; 0 on invalid input or rasterization failure.
 *  @note Output must not own coverage. Serialize rendering on one face. */
int tiku_ttf_render_glyph(const tiku_ttf_t *ttf, unsigned glyph, int px,
                          tiku_ttf_glyph_t *out);

/** @brief Whether the file has a glyph for @p cp at all. */
int tiku_ttf_has(const tiku_ttf_t *ttf, unsigned cp);

/** @brief Free @p glyph's coverage. */
void tiku_ttf_free_glyph(tiku_ttf_glyph_t *glyph);

#endif /* TIKU_TTF_H_ */
