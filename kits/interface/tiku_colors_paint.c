/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_colors_paint.c - what tiku_colors.h keeps, painted.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdio.h>
#include <string.h>

#include "tiku_colors_paint.h"
#include "tiku_dl.h"
#include "tiku_font.h"
#include "tiku_ui.h"

void
tiku_colors_draw(const tiku_colors_t *c, tiku_surface_t *s, tiku_rect_t r)
{
    int i, n = tiku_colors_count(c);

    if (s == NULL || n <= 0) {
        return;
    }
    tiku_ui_sunken(s, r, TIKU_C_DOC);
    for (i = 0; i < n; i++) {
        tiku_rect_t sw = tiku_colors_rect(c, r, i);

        if (sw.w <= 0 || sw.h <= 0) {
            continue;
        }
        tiku_fill(s, sw, c->swatch[i]);
        if (i == c->current) {
            /* The chosen one is ringed white, with black inside: one of
             * the two shows against any swatch. */
            tiku_frame(s, sw, TIKU_RGB(255, 255, 255));
            tiku_frame(s, tiku_inset(sw, 1), TIKU_RGB(0, 0, 0));
        }
    }
}
