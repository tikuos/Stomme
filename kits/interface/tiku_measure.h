/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_measure.h - how wide text is, asked without a font in hand.
 *
 * A control's behaviour lays rows and tabs out by the size of text and
 * does not know how text is drawn: on the desktop the answer comes from
 * the baked faces (tiku_measure_font.c), on a board from a table of their
 * metrics.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_MEASURE_H_
#define TIKU_MEASURE_H_

/*
 * Faces 0 through 2 name plain, bold and mono. Values 10 through 16 select
 * proportional sizes. TIKU_MEASURE_FONT_BASE introduces encoded family,
 * size and cut handles resolved by the desktop font provider.
 */
#define TIKU_MEASURE_PLAIN 0
#define TIKU_MEASURE_BOLD  1
#define TIKU_MEASURE_MONO  2

/** @brief Encoded desktop font: family bits 10-11, cut bits 8-9, size 0-7. */
#define TIKU_MEASURE_FONT_BASE 0x1000
#define TIKU_MEASURE_FAMILY_CURRENT 0
#define TIKU_MEASURE_FAMILY_SANS 1
#define TIKU_MEASURE_FAMILY_SERIF 2
#define TIKU_MEASURE_FAMILY_MONO 3
#define TIKU_MEASURE_FONT_BOLD 1u
#define TIKU_MEASURE_FONT_ITALIC 2u

/** @brief How wide @p text is in @p face, in pixels. */
int tiku_measure_width(int face, const char *text);

/** @brief The line height of @p face. */
int tiku_measure_height(int face);

/** @brief The ascent of @p face. */
int tiku_measure_ascent(int face);

#ifdef TIKU_DISPLAYLESS
/** @brief Nominal body size from the board's single metrics table. */
static inline int tiku_measure_font_size(void)
{ return tiku_measure_height(TIKU_MEASURE_PLAIN); }
/** @brief The board's available base face for a requested desktop font. */
static inline int tiku_measure_font_face(int family, int pixels, unsigned cut)
{
    (void)pixels;
    return family == TIKU_MEASURE_FAMILY_MONO ? TIKU_MEASURE_MONO :
        cut & TIKU_MEASURE_FONT_BOLD ? TIKU_MEASURE_BOLD : TIKU_MEASURE_PLAIN;
}
/** @brief Fixed metrics-table configuration token. */
static inline unsigned tiku_measure_font_stamp(void) { return 0; }
#else
/** @brief Current nominal desktop body size, without glyph padding. */
int tiku_measure_font_size(void);
/** @brief Resolve a family, pixel size and cut to an available encoded face. */
int tiku_measure_font_face(int family, int pixels, unsigned cut);
/** @brief Family/source configuration token; temporary size changes retain it. */
unsigned tiku_measure_font_stamp(void);
#endif

/**
 * @brief The face for a heading of @p level.
 *
 * On the desktop, levels 1 and 2 are the plain face 4 and 2 pixels over
 * the body size, at most 16; a heading that would be no bigger than the
 * body, and every heading on a board, is the bold face.
 */
int tiku_measure_heading(int level);

/** @brief The height of a list row. */
int tiku_measure_row_h(void);

/** @brief The width of a stepper's square half, the - or the +. */
int tiku_measure_step_w(void);

#endif /* TIKU_MEASURE_H_ */
