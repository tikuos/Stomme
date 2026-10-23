/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_slider_paint.h - the slider and the stepper, drawn.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_SLIDER_PAINT_H_
#define TIKU_SLIDER_PAINT_H_

#include "tiku_slider.h"
#include "tiku_gfx.h"

/** @brief Draw the track with its knob at the value. */
void tiku_slider_draw(const tiku_slider_t *sl, tiku_surface_t *s,
                      tiku_rect_t r);

/** @brief Draw @p sl as a field of digits with its two arrows. */
void tiku_stepper_draw(const tiku_slider_t *sl, tiku_surface_t *s,
                       tiku_rect_t r);

#endif /* TIKU_SLIDER_PAINT_H_ */
