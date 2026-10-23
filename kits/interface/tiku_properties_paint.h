/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_properties_paint.h - the property table, drawn.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_PROPERTIES_PAINT_H_
#define TIKU_PROPERTIES_PAINT_H_
#include "tiku_properties.h"
#include "tiku_gfx.h"
/**
 * @brief Draw the table in @p body, with a focus frame on row @p focused,
 *        or on none for -1.
 *
 * Selection is independent of keyboard focus, and a heading is never
 * drawn selected.  Long labels and values are elided; unavailable values
 * are drawn in subdued ink.
 */
void tiku_properties_draw(const tiku_properties_t *p, tiku_surface_t *s,
                           tiku_rect_t body, int focused);
#endif
