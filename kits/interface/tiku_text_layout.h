/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_text_layout.h - fitting text to a width: elision and wrapping,
 * measured through tiku_measure.h and into fixed buffers.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TEXT_LAYOUT_H_
#define TIKU_TEXT_LAYOUT_H_
#include <stddef.h>
#include "tiku_measure.h"

/* The most bytes, NUL included, a layout function writes for one line; the
 * desktop and the board painters share the bound. */
#define TIKU_TEXT_LINE_MAX 256
/* Three dots also render on baked/board faces without a U+2026 glyph. */
#define TIKU_TEXT_ELLIPSIS "..."

/**
 * @brief Fit the first line of @p text to @p width, adding an ellipsis
 *        when text is left out.
 *
 * Valid UTF-8 codepoints are never split; a malformed byte becomes '?' and
 * a control character a space.  A newline ends the line.  When text is
 * left out and the width cannot hold the ellipsis, the line is empty.
 * Codepoints are measured one by one: this is not grapheme or IME layout.
 *
 * @param cap  Bytes of @p out, at most TIKU_TEXT_LINE_MAX used; the output
 *             is terminated whenever @p cap is nonzero
 * @return Nonzero when text was left out
 * @note @p text and @p out must not overlap.
 */
int tiku_text_elide(int face, const char *text, int width,
                    char *out, size_t cap);

/**
 * @brief Write the next line of @p text wrapped to @p width into @p out,
 *        and advance @p text past it.
 *
 * Words wrap at spaces and tabs where they can; a word longer than the
 * line breaks between codepoints.  With @p last set the line takes the
 * rest of the text, elided when it does not fit.  @p out is bounded as
 * for tiku_text_elide().
 *
 * @return 1 for a line, a blank one included, or 0 at the end of the text
 */
int tiku_text_wrap_next(int face, const char **text, int width,
                        char *out, size_t cap, int last);
#endif
