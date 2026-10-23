/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_choice_paint.c - the dropdown, drawn.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_choice_paint.h"
#include "tiku_ui.h"
#include "tiku_measure.h"
#include "tiku_font.h"

void tiku_choice_draw(const tiku_choice_t *c, tiku_surface_t *s,
                     tiku_rect_t anchor, const char *label, unsigned state)
{
    if (!label) label = c->selected >= 0 && c->selected < c->count
                        ? c->item[c->selected].label : "";
    tiku_ui_menufield(s, anchor, label, state |
        (!c->enabled || c->selected < 0 ? TIKU_S_DISABLED : 0));
}
void tiku_choice_draw_popup(const tiku_choice_t *c, tiku_surface_t *s,
                           tiku_rect_t anchor, tiku_rect_t screen)
{
    if (!c->open || !c->enabled) return;
    tiku_rect_t r = tiku_choice_popup(c, anchor, screen);
    int row = tiku_measure_row_h();
    #ifdef TIKU_DISPLAYLESS
    tiku_rect_t old = screen;
    #else
    tiku_rect_t old = s->clip;
    #endif
    tiku_ui_sunken(s, r, TIKU_C_DOC);
    /* Rows are clipped to the popup's inside; the caller's clip comes
     * back after them. */
    tiku_clip_set(s, (tiku_rect_t){r.x + 2, r.y + 2, r.w - 4, r.h - 4});
    for (int i = c->top; i < c->count; i++) {
        tiku_rect_t line = {r.x + 2, r.y + 2 + (i - c->top) * row, r.w - 4, row};
        if (line.y >= r.y + r.h - 2) break;
        tiku_ui_list_row(s, line, c->item[i].label, i == c->hot);
        if (!c->item[i].enabled) {
            tiku_fill(s, line, TIKU_C_DOC);
            tiku_text(s, tiku_font_plain(), line.x + 6,
                line.y + (row - tiku_font_plain()->height) / 2 +
                tiku_font_plain()->ascent, c->item[i].label,
                tiku_dim(TIKU_C_TEXT, TIKU_C_DOC));
        }
    }
    tiku_clip_set(s, old);
}
