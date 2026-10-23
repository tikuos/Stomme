/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_colors.h - a grid of colours, one of them chosen.
 *
 * The R5 colour control: a fixed grid of swatches a colour is picked
 * from.  This holds the swatches, which one is current, where each sits
 * and which one a click lands on; tiku_colors_paint.h draws it.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_COLORS_H_
#define TIKU_COLORS_H_

#include "tiku_geom.h"

/** @brief The most swatches a palette holds. */
#define TIKU_COLORS_MAX 64

/** @brief A palette: its swatches, how many across, and the chosen one. */
typedef struct {
    tiku_rgb_t swatch[TIKU_COLORS_MAX];
    int        count;
    int        cols;       /* swatches across; rows follow from count */
    int        current;    /* -1 when nothing is chosen               */
} tiku_colors_t;

/** @brief An empty palette @p cols swatches wide, or 8 when @p cols is
 *         not positive. */
void tiku_colors_init(tiku_colors_t *c, int cols);

/**
 * @brief Fill @p c with the default palette, 32 swatches eight across:
 *        dark, full and soft hues, and a grey ramp.
 *
 * For a control whose caller has no palette of its own.
 */
void tiku_colors_default(tiku_colors_t *c);

/** @brief Add one swatch, chosen when it is the first.
 *  @return its index, or -1 when full. */
int tiku_colors_add(tiku_colors_t *c, tiku_rgb_t rgb);

/** @brief How many swatches there are. */
int tiku_colors_count(const tiku_colors_t *c);

/** @brief The chosen index, or -1. */
int tiku_colors_current(const tiku_colors_t *c);

/** @brief The chosen colour; black when nothing is chosen. */
tiku_rgb_t tiku_colors_value(const tiku_colors_t *c);

/** @brief Choose @p index, clamped to the palette.
 *  @return the chosen index, or -1 when the palette is empty. */
int tiku_colors_select(tiku_colors_t *c, int index);

/**
 * @brief Choose the swatch nearest @p rgb by squared RGB distance.
 * @return the index, or -1 when the palette is empty.
 */
int tiku_colors_select_nearest(tiku_colors_t *c, tiku_rgb_t rgb);

/** @brief Where swatch @p index sits in the grid drawn in @p r. */
tiku_rect_t tiku_colors_rect(const tiku_colors_t *c, tiku_rect_t r,
                             int index);

/** @brief Which swatch a point lands on, or -1. */
int tiku_colors_hit(const tiku_colors_t *c, tiku_rect_t r, int x, int y);

#endif /* TIKU_COLORS_H_ */
