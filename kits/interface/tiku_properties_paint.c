/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_properties_paint.c - the property table, drawn.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_properties_paint.h"
#include "tiku_text_layout_paint.h"
#include "tiku_ui.h"

void tiku_properties_draw(const tiku_properties_t *p, tiku_surface_t *s,
                           tiku_rect_t body, int focused)
{
    if (!s || body.w <= 0 || body.h <= 0) return;
    int label_w = tiku_properties_label_width(p, body);
    tiku_ui_sunken(s, body, TIKU_C_DOC);
    for (int i = 0; i < p->count; i++) {
        tiku_rect_t r = tiku_properties_row(p, body, i);
        if (!r.h) continue;
        const tiku_property_t *v = &p->row[i];
        int heading = !!(v->flags & TIKU_PROPERTY_HEADING);
        int selected = i == p->selected && !heading;
        tiku_rgb_t ink = selected ? TIKU_C_SELTEXT : TIKU_C_TEXT;
        tiku_rgb_t ground = selected ? TIKU_C_SELECT : TIKU_C_DOC;
        if (selected) tiku_fill(s, r, ground);
        tiku_text_line_draw(s, (tiku_rect_t){r.x + 8, r.y,
            heading ? r.w - 16 : label_w - 16, r.h},
            heading ? TIKU_MEASURE_BOLD : TIKU_MEASURE_PLAIN, v->label, ink);
        if (heading) continue;
        if (v->flags & TIKU_PROPERTY_UNAVAILABLE) ink = tiku_dim(ink, ground);
        tiku_text_line_draw(s, (tiku_rect_t){r.x + label_w, r.y,
            r.w - label_w - 8, r.h}, TIKU_MEASURE_PLAIN, v->value, ink);
        if (i == focused) tiku_frame(s, tiku_inset(r, 1), TIKU_C_FOCUS);
    }
}
