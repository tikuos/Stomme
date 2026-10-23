/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_preferences_layout.h - the rectangles a settings panel is built
 * from: a row of action buttons and the inside of an etched group.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_PREFERENCES_LAYOUT_H_
#define TIKU_PREFERENCES_LAYOUT_H_
#include "tiku_geom.h"
/** @brief The most trailing buttons one row lays out. */
#define TIKU_PREFERENCES_ACTIONS_MAX 8
/** @brief One button of an action row. */
typedef struct {
    const char *label;
    int minimum;            /**< least width in pixels, 0 for none */
} tiku_preferences_action_t;

/**
 * @brief Least width of a row: a @p leading button at the left and
 *        @p count trailing buttons at the right, @p gap apart.
 *
 * A button is as wide as tiku_ui_button_w() makes it: its label plus a
 * font height either side, or its minimum when that is wider.
 *
 * @param leading  The left-hand button, or NULL for none
 * @return The width, or -1 for invalid input
 */
int tiku_preferences_actions_width(const tiku_preferences_action_t *leading,
    const tiku_preferences_action_t *trailing, int count, int gap);
/**
 * @brief Place the @p leading button at the left of @p r and the trailing
 *        buttons against its right edge, centred in its height.
 *
 * The outputs are cleared first and set only when the row fits; the
 * buttons placed never overlap.  tiku_preferences_actions_width() gives
 * the width a window needs.
 *
 * @return 1 when the row fits, 0 when @p r is too small, -1 for invalid
 *         input
 * @note @p out has room for @p count rectangles.
 */
int tiku_preferences_actions(tiku_rect_t r,
    const tiku_preferences_action_t *leading,
    const tiku_preferences_action_t *trailing, int count, int gap,
    tiku_rect_t *leading_out, tiku_rect_t *out);
/**
 * @brief The content rectangle inside an etched group's @p frame: below
 *        its title and inside its padding, both measured from the font.
 *
 * A frame too small for both yields an empty rectangle at its corner.
 */
tiku_rect_t tiku_preferences_group_body(tiku_rect_t frame);
#endif
