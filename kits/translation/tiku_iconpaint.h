/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_iconpaint.h - drawing with alpha onto an opaque surface: icons,
 * plain, dimmed, selected or streamed; thumbnails and pictures; tinted
 * and inverted rubber bands; and text with a contrast halo.
 *
 * The icon cache hands back premultiplied ARGB; the surface is opaque RGB.
 * One compositor does that conversion for everyone who draws an icon.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_ICONPAINT_H_
#define TIKU_ICONPAINT_H_

#include "tiku_font.h"
#include "tiku_gfx.h"
#include "tiku_thumb.h"

/**
 * @brief Composite icon @p name at @p size over @p s with its top-left at
 *        (@p x, @p y).
 *
 * Renders are cached per name and size: the same icon in every row of a
 * list is rasterised once.
 *
 * @return 1 when it was drawn, 0 for an unknown name or a failed render.
 */
int tiku_icon_paint(tiku_surface_t *s, const char *name,
                        int x, int y, int size);

/**
 * @brief Composite icon @p name dimmed toward @p over, for a disabled or
 *        minimised item.
 *
 * @param mix 0.0 leaves the icon untouched, 1.0 dissolves it into @p over.
 */
int tiku_icon_paint_dim(tiku_surface_t *s, const char *name,
                            int x, int y, int size, float mix,
                            tiku_rgb_t over);

/**
 * @brief Composite the selected variant of an icon (PVL-082): every
 *        channel darkened to 168/255 with alpha kept (IV-018).
 *
 * The variant is cached beside the plain render.  It marks a selected row
 * even when the row's label is off the right edge.
 */
int tiku_icon_paint_selected(tiku_surface_t *s, const char *name,
                                 int x, int y, int size);

/**
 * @brief Draw a loaded thumbnail into @p s at @p size, alpha-blended.
 *
 * The one TIKU_THUMB_SIZE square is scaled at draw time to every icon size
 * the view asks for.
 */
void tiku_thumb_paint(tiku_surface_t *s,
                          const tiku_image_t *th, int x, int y,
                          int size);

/**
 * @brief Whether the pixel at @p px, @p py of icon @p name is opaque:
 *        any alpha counts, and every pixel of a name with no art does.
 *
 * A drag starts only from such a pixel, so the transparent gap between
 * two icons does not start one (IV-057).
 */
int tiku_icon_hit(const char *name, int size, int px, int py);

/**
 * @brief tiku_dl's icon player: a streamed HVIF blob rasterised at
 *        @p size and composited as tiku_icon_paint() does: over, then the
 *        wash at @p mix / 255, then the theme's achromatic setting.
 *
 * Rendered on every call; a streamed blob has no name to cache it under.
 *
 * @return 1 when it was drawn, 0 for a blob that will not parse or render.
 * @note Register it with tiku_dl_set_icon_painter() in anything that plays
 *       display lists; the Interface kit carries icon commands without
 *       knowing what HVIF is.
 */
int tiku_icon_stream_play(tiku_surface_t *s, const void *hvif,
                          size_t hlen, int x, int y, int size,
                          unsigned mix, tiku_rgb_t wash);

/**
 * @brief Blend @p c over the pixels of @p r at @p a / 255.
 *
 * The tinted rubber band is drawn with it, and what it covers stays
 * visible (PVL-069, PVS-030).  On a recording surface it marks the list
 * as missing a draw.
 */
void tiku_paint_tint(tiku_surface_t *s, tiku_rect_t r,
                         tiku_rgb_t c, int a);

/**
 * @brief Draw a rubber band the way @p transparent asks for.
 *
 * Tinted (Tracker's default) puts an edge and a lighter interior over what
 * it covers; otherwise the outline is inverted, which is visible over
 * whatever it lands on and undoes itself (PVL-069, PVS-030, TS-049).
 */
void tiku_paint_band(tiku_surface_t *s, tiku_rect_t r,
                         int transparent);

/**
 * @brief Draw a picture into @p dst, scaled to it, clipped to @p clip.
 *
 * A folder background is drawn with it, once per rectangle its placement
 * gives: tiled, centred or scaled (CW-020, AW-016).  Transparent pixels
 * are skipped, and an achromatic theme greys the rest.
 */
void tiku_paint_image(tiku_surface_t *s, const tiku_image_t *im,
                          tiku_rect_t dst, tiku_rect_t clip);

/**
 * @brief Draw @p text with a contrast halo around it (PVL-071, IV-049).
 *
 * The string is drawn at the eight neighbouring pixels, in black around
 * light ink and white around dark, then once in @p ink, so a label stays
 * readable over a picture of its own colour.
 */
void tiku_paint_text_halo(tiku_surface_t *s,
                              const tiku_font_t *f, int x, int y,
                              const char *text, tiku_rgb_t ink);

#endif /* TIKU_ICONPAINT_H_ */
