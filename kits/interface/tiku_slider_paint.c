/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_slider_paint.c - what tiku_slider.h keeps, painted.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdio.h>
#include <string.h>

#include "tiku_slider_paint.h"
#include "tiku_dl.h"
#include "tiku_font.h"
#include "tiku_ui.h"

void
tiku_slider_draw(const tiku_slider_t *sl, tiku_surface_t *s, tiku_rect_t r)
{
    tiku_rect_t groove, knob;
    int rec;

    if (sl == NULL || s == NULL) {
        return;
    }
    rec = tiku_gfx_rec_enter(s);
    if (rec) {
        /* Recorded as a slider with its range and value. */
        (void)tiku_dl_slider(s->record, r, sl->min, sl->max, sl->value);
    }
    /* The groove is a thin sunken line along the middle of the track,
     * and the knob is drawn over it. */
    groove = (tiku_rect_t){ r.x + r.h / 2, r.y + r.h / 2 - 2,
                            (r.w > r.h) ? r.w - r.h : 0, 4 };
    tiku_ui_sunken(s, groove, TIKU_C_DOC);
    knob = tiku_slider_knob(sl, r);
    tiku_ui_raised(s, knob);
    tiku_gfx_rec_leave(s, rec);
}

void
tiku_stepper_draw(const tiku_slider_t *sl, tiku_surface_t *s, tiku_rect_t r)
{
    const tiku_font_t *f = tiku_font_plain();
    int aw = tiku_ui_step_w();
    tiku_rect_t field, up, down;
    char digits[24];

    if (sl == NULL || s == NULL) {
        return;
    }
    field = (tiku_rect_t){ r.x, r.y, (r.w > aw) ? r.w - aw : 0, r.h };
    up = (tiku_rect_t){ r.x + r.w - aw, r.y, aw, r.h / 2 };
    down = (tiku_rect_t){ up.x, r.y + r.h / 2, aw, r.h - r.h / 2 };

    tiku_ui_sunken(s, field, TIKU_C_DOC);
    snprintf(digits, sizeof digits, "%d", sl->value);
    /* The digits are right-aligned against the arrows. */
    tiku_text(s, f, field.x + field.w - 6 - tiku_text_width(f, digits),
              field.y + (field.h - f->height) / 2 + f->ascent, digits,
              TIKU_C_TEXT);
    /* Each arrow is dimmed when the value cannot move that way. */
    tiku_ui_raised(s, up);
    tiku_ui_arrow(s, up, 0, sl->value < sl->max);
    tiku_ui_raised(s, down);
    tiku_ui_arrow(s, down, 1, sl->value > sl->min);
}
