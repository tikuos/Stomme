/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_logo.h - the TikuOS mark.
 *
 * Two wireframe cubes locked through one another, amber on the edges
 * where they meet.  The mark is kept as geometry: twelve stroked runs and
 * one filled face in the 1024-unit space of the artwork
 * (assets/system/tikuos-logo-haiku.svg), painted through the glyph path
 * rasteriser, so it is sharp at any size and its colours are chosen when
 * it is drawn.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_LOGO_H_
#define TIKU_LOGO_H_

#include "tiku_gfx.h"

/**
 * @brief Flag: fill the mark's square with the palette's light ground,
 *        for a mark shown as a tile.
 */
#define TIKU_LOGO_GROUND 0x1u

/**
 * @brief Draw the mark in its own colours, centred in the largest square
 *        inside @p r.
 *
 * Drawn three ways by the square's side: both cubes at 32 px and up, the
 * near cube alone from 22 to 31, and the near cube filled under 22.  A
 * side under 4 draws nothing.  When the theme has no hue
 * (tiku_theme_achromatic()), every colour is drawn as its grey.
 *
 * @param flags  TIKU_LOGO_GROUND or 0
 */
void tiku_logo_paint(tiku_surface_t *s, tiku_rect_t r, unsigned flags);

/*---------------------------------------------------------------------------*/
/* PALETTES                                                                  */
/*---------------------------------------------------------------------------*/

/**
 * @brief One set of the seven colours the mark is drawn in.
 *
 * The mark is geometry, so a palette recolours it when it is drawn.
 */
typedef struct {
    tiku_rgb_t ground;     /**< the light square the mark sits on   */
    tiku_rgb_t dark;       /**< the near cube's edges               */
    tiku_rgb_t grey;       /**< the far cube's edges                */
    tiku_rgb_t accent;     /**< the lit edges, where the cubes meet */
    tiku_rgb_t tint;       /**< the one filled face                 */
    tiku_rgb_t mix_a;      /**< the near cube's half-lit edge       */
    tiku_rgb_t mix_b;      /**< the far cube's half-lit edge        */
} tiku_logo_palette_t;

/** @brief How many sets are built in.  The first is the mark as drawn. */
int tiku_logo_palette_count(void);

/**
 * @brief Fill @p out with set @p i blended @p toward_next of the way to
 *        set @p i + 1.
 *
 * Only the colours are blended, so a fade between sets costs one frame.
 * @p toward_next is clamped to 0..1, @p i wraps in both directions, and
 * the set after the last is the first.
 */
void tiku_logo_palette(int i, float toward_next, tiku_logo_palette_t *out);

/**
 * @brief Draw the mark as tiku_logo_paint() does, in palette @p p, shaded
 *        by @p shade.
 *
 * @p shade follows tiku_tint(): 1.0 draws the colours as they are, below
 * one lightens them, above one darkens.
 *
 * @param p  The colours, or NULL for set 0
 */
void tiku_logo_paint_with(tiku_surface_t *s, tiku_rect_t r, unsigned flags,
                          const tiku_logo_palette_t *p, float shade);

/**
 * @brief The ground colour of set 0, for a caller drawing next to the
 *        mark.
 *
 * The ground does not follow the theme, except that it is returned as its
 * grey when the theme has no hue.
 */
tiku_rgb_t tiku_logo_ground(void);

#endif /* TIKU_LOGO_H_ */
