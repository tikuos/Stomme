/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_list_paint.c - what tiku_list.h keeps, painted.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdio.h>
#include <string.h>

#include "tiku_list_paint.h"
#include "tiku_dl.h"
#include "tiku_font.h"
#include "tiku_ui.h"

void
tiku_list_draw(const tiku_list_t *l, tiku_surface_t *s, tiku_rect_t body,
               tiku_list_name_fn name, void *ctx)
{
    int rows, i;

    if (l == NULL || s == NULL || name == NULL) {
        return;
    }
    rows = tiku_list_visible_rows(l, body);
    for (i = 0; i < rows; i++) {
        int row = l->top + i;
        const char *n;

        if (row >= l->count) {
            break;
        }
        n = name(ctx, row);
        if (l->head[row]) {
            /* A heading: the group's name in the bold face on the
             * document colour, recorded as text. */
            tiku_rect_t r = tiku_list_row_rect(l, body, row);
            const tiku_font_t *f = tiku_font_bold();

            tiku_fill(s, r, TIKU_C_DOC);
            tiku_text(s, f, r.x + 4,
                           r.y + (r.h - f->height) / 2 + f->ascent,
                           (n != NULL) ? n : "", TIKU_C_TEXT);
            continue;
        }
        tiku_ui_list_row(s, tiku_list_row_rect(l, body, row),
                         (n != NULL) ? n : "", tiku_list_selected(l, row));
    }
}
