/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_tabs_paint.h - the tab strip, drawn.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TABS_PAINT_H_
#define TIKU_TABS_PAINT_H_

#include "tiku_tabs.h"
#include "tiku_gfx.h"

/**
 * @brief Draw the strip: the current tab lit and open to the body below,
 *        the rest idle with a seam under them.
 *
 * The colours come from the theme's tab roles, the ones a window's title
 * tab wears.
 */
void tiku_tabs_draw(const tiku_tabs_t *t, tiku_surface_t *s,
                    tiku_rect_t strip);

#endif /* TIKU_TABS_PAINT_H_ */
