/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_deskbar_paint.h - Deskbar row backgrounds and window glyphs.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_DESKBAR_PAINT_H_
#define TIKU_DESKBAR_PAINT_H_
#include "tiku_gfx.h"
/** @brief Draw a native Deskbar gradient row in the caller's clip. */
void tiku_deskbar_row_paint(tiku_surface_t *,tiku_rect_t,int selected);
/** @brief Draw a window glyph, hollow when minimized and ringed when current. */
void tiku_deskbar_window_icon(tiku_surface_t *,int x,int y,int minimized,int current);
/** @brief Draw the native task expander, pointing down when expanded. */
void tiku_deskbar_expander(tiku_surface_t *,int x,int y,int expanded);
#endif
