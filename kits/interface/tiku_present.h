/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_present.h - a software frame copied into a native backing buffer,
 * reporting the rectangles that changed.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_PRESENT_H_
#define TIKU_PRESENT_H_
#include "tiku_gfx.h"
/** @brief The rectangles a host keeps room for in one update. */
#define TIKU_PRESENT_RECTS 64
/**
 * @brief Update native RGB backing pixels from @p src and report the
 *        native rectangles that changed.
 *
 * The frame is compared in 64x32 native tiles, and changed tiles side by
 * side in a row are joined.  When more than @p cap rectangles change, one
 * rectangle bounding them all is reported, so no damage is dropped.
 *
 * @param stride  Pixels from one row of @p dst to the next
 * @param force   Nonzero to copy and report the whole frame
 * @return The rectangles in @p rects, 0 when nothing changed, or -1 for
 *         invalid arguments
 * @note Pass @p force on first use and after an exposure.
 */
int tiku_present_update(tiku_rgb_t *dst, int w, int h, int stride,
    const tiku_surface_t *src, int force, tiku_rect_t *rects, int cap);
/**
 * @brief tiku_present_update(), comparing only the tiles under the logical
 *        rectangle @p bounds of the source.
 *
 * The caller says nothing outside @p bounds changed since the last
 * update; NULL means the whole surface.  @p bounds is ignored when the
 * native size is not the source's own scale, since the tiles then
 * resample.
 */
int tiku_present_update_in(tiku_rgb_t *dst, int w, int h, int stride,
    const tiku_surface_t *src, int force, const tiku_rect_t *bounds,
    tiku_rect_t *rects, int cap);
#endif
