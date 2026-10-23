/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_slider.h - a value dragged along a track, and a number stepped by
 * a pair of arrows.
 *
 * Both are a number with a floor, a ceiling and a step that the user
 * changes by pointing at it: the slider for a quantity set by feel, such
 * as a volume or a delay, and the stepper for one set by its digits.  The
 * state is the range, the value, the step and whether a drag is under
 * way; the stepper uses the same state.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_SLIDER_H_
#define TIKU_SLIDER_H_

#include "tiku_geom.h"

/** @brief A slider's or a stepper's range, value and drag. */
typedef struct {
    int min, max;
    int value;
    int step;          /**< one arrow click, or one arrow key */
    int tracking;      /**< a press is being followed         */
} tiku_slider_t;

/**
 * @brief Set up over @p min..@p max, starting at @p value, clamped.
 *
 * A backwards range is swapped, and a @p step under 1 is taken as 1.
 */
void tiku_slider_init(tiku_slider_t *sl, int min, int max, int value,
                      int step);

/** @brief Set the value, clamped to the range.  @return the value. */
int tiku_slider_set(tiku_slider_t *sl, int value);

/** @brief Step by @p dir steps (-1 or +1), clamped.  @return the value. */
int tiku_slider_step(tiku_slider_t *sl, int dir);

/** @brief How far along the range the value sits, 0..1. */
float tiku_slider_fraction(const tiku_slider_t *sl);

/**
 * @brief Where the knob sits in a horizontal track @p r.
 *
 * Square, the track's height, and inset so the knob stays wholly inside
 * the track at both ends of its travel.
 */
tiku_rect_t tiku_slider_knob(const tiku_slider_t *sl, tiku_rect_t r);

/**
 * @brief Take a press at @p x in track @p r: jump to it and grab.
 *
 * A press anywhere on the track moves the knob's centre there and leaves
 * the knob held, so the same gesture carries on as a drag.
 *
 * @return the value.
 */
int tiku_slider_press(tiku_slider_t *sl, tiku_rect_t r, int x);

/** @brief Drag to @p x while held.  @return the value. */
int tiku_slider_drag(tiku_slider_t *sl, tiku_rect_t r, int x);

/** @brief Let go. */
void tiku_slider_release(tiku_slider_t *sl);

/*-------------------------------------------------------------------------*
 * The stepper: the same number, shown as digits with two arrows.
 *-------------------------------------------------------------------------*/

/** @brief Which half of a stepper a point is on. */
typedef enum {
    TIKU_STEPPER_HIT_NONE = 0,
    TIKU_STEPPER_HIT_UP,
    TIKU_STEPPER_HIT_DOWN
} tiku_stepper_hit_t;

/**
 * @brief Where a point at @p x,@p y lands on the stepper drawn in @p r:
 *        its arrows are the right-hand tiku_measure_step_w() pixels, up
 *        above down.
 */
tiku_stepper_hit_t tiku_stepper_hit(tiku_rect_t r, int x, int y);

/**
 * @brief Take a press on the stepper: step the value if it hit an arrow.
 * @return nonzero when the value changed.
 */
int tiku_stepper_press(tiku_slider_t *sl, tiku_rect_t r, int x, int y);

#endif /* TIKU_SLIDER_H_ */
