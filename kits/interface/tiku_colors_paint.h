/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_colors_paint.h - the palette, drawn.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_COLORS_PAINT_H_
#define TIKU_COLORS_PAINT_H_

#include "tiku_colors.h"
#include "tiku_gfx.h"

/** @brief Draw the grid, the chosen swatch ringed. */
void tiku_colors_draw(const tiku_colors_t *c, tiku_surface_t *s,
                      tiku_rect_t r);

#endif /* TIKU_COLORS_PAINT_H_ */
