/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_text_layout_paint.h - laid-out text, drawn: one elided line or a
 * wrapped block.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TEXT_LAYOUT_PAINT_H_
#define TIKU_TEXT_LAYOUT_PAINT_H_
#include "tiku_text_layout.h"
#include "tiku_gfx.h"
/**
 * @brief Draw @p text as one elided line, centred in the height of @p r
 *        and clipped to it.
 *
 * @note On a board's recording surface the clip is reset afterwards, so a
 *       caller keeping an outer clip sets it again.
 */
void tiku_text_line_draw(tiku_surface_t *s, tiku_rect_t r, int face,
                         const char *text, tiku_rgb_t ink);
/**
 * @brief Draw @p text wrapped to the width of @p r from its top, @p gap
 *        pixels between lines; the last line that fits is elided.
 *
 * @note On a board's recording surface the clip is reset afterwards, so a
 *       caller keeping an outer clip sets it again.
 */
void tiku_text_block_draw(tiku_surface_t *s, tiku_rect_t r, int face,
                          const char *text, tiku_rgb_t ink, int gap);
#endif
