/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_preferences_layout.c - the rectangles a settings panel is built from.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_preferences_layout.h"
#include "tiku_measure.h"
#include <limits.h>
#include <stdint.h>

/**
 * @brief Width of the button for @p a, as tiku_ui_button_w() measures it,
 *        or -1 when @p a is invalid.
 */
static int button_width(const tiku_preferences_action_t *a)
{
    if (!a || !a->label || a->minimum < 0) return -1;
    int measured = tiku_measure_width(TIKU_MEASURE_PLAIN, a->label) +
                   2 * tiku_measure_height(TIKU_MEASURE_PLAIN);
    return measured > a->minimum ? measured : a->minimum;
}
int tiku_preferences_actions_width(const tiku_preferences_action_t *leading,
    const tiku_preferences_action_t *trailing, int count, int gap)
{
    if (count < 0 || count > TIKU_PREFERENCES_ACTIONS_MAX ||
        (count && !trailing) || gap < 0) return -1;
    int64_t width = 0;
    if (leading) {
        int w = button_width(leading);
        if (w < 0) return -1;
        width = w;
    }
    for (int i = 0; i < count; i++) {
        int w = button_width(&trailing[i]);
        if (w < 0) return -1;
        width += w;
        if (i || leading) width += gap;
    }
    return width <= INT_MAX ? (int)width : -1;
}
int tiku_preferences_actions(tiku_rect_t r,
    const tiku_preferences_action_t *leading,
    const tiku_preferences_action_t *trailing, int count, int gap,
    tiku_rect_t *leading_out, tiku_rect_t *out)
{
    if (leading_out) *leading_out = (tiku_rect_t){0};
    if (out && count >= 0 && count <= TIKU_PREFERENCES_ACTIONS_MAX)
        for (int i = 0; i < count; i++) out[i] = (tiku_rect_t){0};
    int width = tiku_preferences_actions_width(leading, trailing, count, gap);
    if (width < 0 || (count && !out) || (leading && !leading_out)) return -1;
    int height = tiku_measure_step_w();
    if (r.w < width || r.h < height) return 0;
    int y = r.y + (r.h - height) / 2, x = r.x + r.w;
    for (int i = count - 1; i >= 0; i--) {
        int w = button_width(&trailing[i]);
        x -= w;
        out[i] = (tiku_rect_t){x, y, w, height};
        x -= gap;
    }
    if (leading) *leading_out = (tiku_rect_t){r.x, y, button_width(leading), height};
    return 1;
}
tiku_rect_t tiku_preferences_group_body(tiku_rect_t r)
{
    int pad = tiku_measure_height(TIKU_MEASURE_PLAIN) - 1;
    if (pad < 4) pad = 4;
    int top = tiku_measure_height(TIKU_MEASURE_PLAIN) + 11;
    if (r.w < 2 * pad || r.h < top + pad)
        return (tiku_rect_t){r.x, r.y, 0, 0};
    return (tiku_rect_t){r.x + pad, r.y + top, r.w - 2 * pad, r.h - top - pad};
}
