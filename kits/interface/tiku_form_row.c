/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_form_row.c - the labelled row's split, through the measure seam.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_form_row.h"

#include <stddef.h>

#include "tiku_layout.h"
#include "tiku_measure.h"

void
tiku_form_row_split(tiku_rect_t r, const char *label, int gap,
                    tiku_rect_t *label_out, tiku_rect_t *control_out)
{
    tiku_layout_t l;
    int w = tiku_measure_width(TIKU_MEASURE_PLAIN, label ? label : "");

    tiku_layout_init(&l, 1, gap, 0);
    (void)tiku_layout_add(&l, w, 0, w);     /* the words, their own width */
    (void)tiku_layout_add(&l, 0, 1, 0);     /* the control, the rest      */
    if (label_out != NULL) {
        *label_out = tiku_layout_slot(&l, r, 0);
    }
    if (control_out != NULL) {
        *control_out = tiku_layout_slot(&l, r, 1);
    }
}
