/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_choice_paint.h - the popup choice drawn: the closed control and
 * its open popup.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_CHOICE_PAINT_H_
#define TIKU_CHOICE_PAINT_H_
#include "tiku_choice.h"
#include "tiku_gfx.h"
/** @brief Paint the closed control at @p anchor showing @p label, or the
 *         selected option's label when @p label is NULL; drawn disabled
 *         while the control is disabled or nothing is selected. */
void tiku_choice_draw(const tiku_choice_t *c, tiku_surface_t *s,
                     tiku_rect_t anchor, const char *label, unsigned state);
/** @brief Paint the open popup where tiku_choice_popup() puts it, the
 *         hot row highlighted and disabled options dimmed.
 *  @note Paint it last, over the rest of the panel. */
void tiku_choice_draw_popup(const tiku_choice_t *c, tiku_surface_t *s,
                           tiku_rect_t anchor, tiku_rect_t screen);
#endif
