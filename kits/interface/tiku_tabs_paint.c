/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_tabs_paint.c - what tiku_tabs.h keeps, painted.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdio.h>
#include <string.h>

#include "tiku_tabs_paint.h"
#include "tiku_dl.h"
#include "tiku_font.h"
#include "tiku_ui.h"

/* Used before it is defined. */
static void draw_strip(const tiku_tabs_t *t, tiku_surface_t *s,
                       tiku_rect_t strip);
/* End of the forward declarations. */

void
tiku_tabs_draw(const tiku_tabs_t *t, tiku_surface_t *s, tiku_rect_t strip)
{
    int n = (t != NULL) ? t->count : 0;

    if (s == NULL || n <= 0) {
        return;
    }
    {
        int rec = tiku_gfx_rec_enter(s);

        if (rec) {
            /* Recorded as the tabs by name with the current one
             * marked. */
            char labels[TIKU_TABS_MAX * TIKU_TABS_LABEL_MAX];
            size_t used = 0u;
            int k;

            for (k = 0; k < n; k++) {
                size_t l = strlen(t->label[k]) + 1u;

                if (used + l > sizeof labels) {
                    break;
                }
                memcpy(labels + used, t->label[k], l);
                used += l;
            }
            (void)tiku_dl_tabs(s->record, strip, n, t->current, labels,
                               used);
        }
        draw_strip(t, s, strip);
        tiku_gfx_rec_leave(s, rec);
        return;
    }
}

/** @brief Draw the strip's pixels; tiku_tabs_draw() records their meaning. */
static void
draw_strip(const tiku_tabs_t *t, tiku_surface_t *s, tiku_rect_t strip)
{
    const tiku_font_t *font = tiku_font_plain();
    int n = t->count;
    int seam = strip.y + strip.h - 1;
    int i;

    /* The line the body's top edge sits on, under the whole strip.  The
     * current tab covers its own stretch of it, which opens the tab onto
     * the body. */
    tiku_hline(s, strip.x, seam, strip.w, tiku_tint(TIKU_C_PANEL, 0.70f));

    for (i = 0; i < n; i++) {
        tiku_rect_t r = tiku_tabs_rect(t, strip, i);
        int active = (i == t->current);
        tiku_rgb_t face = active ? TIKU_C_TAB : TIKU_C_TAB_IDLE;
        int tw = tiku_text_width(font, t->label[i]);
        int tx = r.x + (r.w - tw) / 2;
        int ty = r.y + (r.h - font->height) / 2 + font->ascent;

        /* An idle tab sits two pixels lower and keeps the seam under it;
         * the current one fills to the seam and covers it. */
        if (active) {
            tiku_fill(s, (tiku_rect_t){ r.x, r.y, r.w, r.h }, face);
            tiku_hline(s, r.x, seam, r.w, face);
        } else {
            tiku_fill(s, (tiku_rect_t){ r.x, r.y + 2, r.w, r.h - 3 }, face);
        }
        /* A hairline down the right of every tab but the last divides
         * the tabs. */
        if (i < n - 1) {
            tiku_vline(s, r.x + r.w - 1, r.y + 2, r.h - 3,
                       tiku_tint(TIKU_C_PANEL, 0.70f));
        }
        tiku_text(s, font, tx, ty, t->label[i], TIKU_C_TABTEXT);
    }
}
