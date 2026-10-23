/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_motion.h - short visual feedback that never holds up an action.
 *
 * The timing of an effect, the outline that grows out of an opened icon
 * and the fade a popup comes in with.  Nothing here keeps a clock or a
 * loop: the caller passes the time and repaints while an effect runs.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_MOTION_H_
#define TIKU_MOTION_H_

#include "tiku_gfx.h"

/** @brief How long a menu takes to fade in, in microseconds. */
#define TIKU_MOTION_MENU_US 100000
/** @brief How long an opened icon's outline lasts, in microseconds. */
#define TIKU_MOTION_ICON_US 170000
/** @brief How far the outline reaches outside the icon, in pixels. */
#define TIKU_MOTION_ICON_MARGIN 8

/** @brief One effect's timing.  A zeroed one is idle. */
typedef struct {
    int64_t since;      /**< when it started, monotonic microseconds */
    int64_t duration;   /**< how long it runs, in microseconds       */
    int     active;     /**< nonzero while it runs                   */
} tiku_motion_t;

/**
 * @brief Start @p motion at @p now, to run for @p duration microseconds.
 *
 * A @p now or a @p duration of zero leaves it idle; a caller passes 0 to
 * show the end state, as with Reduce motion on.
 */
void tiku_motion_start(tiku_motion_t *motion, int64_t now, int64_t duration);

/**
 * @brief How far through @p motion is at @p now, from 0 to 1.
 *
 * At or past the end, at a zero @p now and at a time before the start the
 * motion goes idle and the answer is 1; an idle motion stays idle.
 */
float tiku_motion_progress(tiku_motion_t *motion, int64_t now);

/** @brief Cubic ease-out of @p progress, held to 0..1. */
float tiku_motion_ease_out(float progress);

/**
 * @brief Draw the outline around @p icon at @p progress.
 *
 * Two rings, dark outside light: the dark one starts two pixels outside
 * the icon and moves out to TIKU_MOTION_ICON_MARGIN, both fading as they
 * go.  Nothing is drawn at 1.
 * @note Draw over freshly painted content, and repaint
 *       tiku_motion_outline_bounds() when the motion ends or is replaced.
 */
void tiku_motion_outline(tiku_surface_t *surface, tiku_rect_t icon,
                         float progress);

/** @brief Everything tiku_motion_outline() may paint around @p icon. */
tiku_rect_t tiku_motion_outline_bounds(tiku_rect_t icon);

/**
 * @brief Keep the pixels a popup is about to cover, for its fade.
 *
 * Only the part of @p bounds inside the surface's clip is kept, and
 * @p captured says which part.  NULL when there is nothing to fade (a
 * progress below 0 or at 1 and past, no overlap, or no memory); the popup
 * then draws as it is.
 * @note Hand the result to tiku_motion_fade_end() after drawing the popup.
 */
tiku_surface_t *tiku_motion_fade_begin(tiku_surface_t *surface,
                                       tiku_rect_t bounds, float progress,
                                       tiku_rect_t *captured);

/**
 * @brief Blend the kept pixels back over the drawn popup, and free them.
 *
 * They fade out as @p progress eases from 0 to 1.  At most three quarters
 * of them come back, so the first frame already shows the popup, which
 * takes clicks from that frame on.
 */
void tiku_motion_fade_end(tiku_surface_t *surface, tiku_surface_t *under,
                          tiku_rect_t captured, float progress);

#endif
