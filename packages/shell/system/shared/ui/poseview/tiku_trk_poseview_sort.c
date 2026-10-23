/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_poseview_sort.c - choosing the sort key, its direction and the
 * tie-break column.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_trk_poseview_private.h"

void
tiku_trk_poseview_sort_by(tiku_trk_poseview_t *v, int field)
{
    if (v == NULL) {
        return;
    }
    if (v->sort_field == field) {
        v->sort_reverse = !v->sort_reverse;
    } else {
        v->sort_field = field;
        v->sort_reverse = 0;
    }
    pv_resort(v);
    if (v->filtering) {
        pv_rebuild_filter(v);
    }
}

int
tiku_trk_poseview_sort_field(const tiku_trk_poseview_t *v)
{
    return (v != NULL) ? v->sort_field : 0;
}

int
tiku_trk_poseview_sort_reverse(const tiku_trk_poseview_t *v)
{
    return (v != NULL) ? v->sort_reverse : 0;
}

int
tiku_trk_poseview_sort_secondary(const tiku_trk_poseview_t *v)
{
    return (v != NULL) ? v->sort2_field : -1;
}

void
tiku_trk_poseview_set_secondary(tiku_trk_poseview_t *v, int field)
{
    if (v == NULL) {
        return;
    }
    /* A field with no column is not taken as the secondary. */
    if (field >= 0 && !tiku_trk_poseview_has_column(v, field)) {
        field = -1;
    }
    v->sort2_field = (field == v->sort_field) ? -1 : field;
    pv_resort(v);
    if (v->filtering) {
        pv_rebuild_filter(v);
    }
}

void
tiku_trk_poseview_sort_click(tiku_trk_poseview_t *v, int field, int shift)
{
    if (v == NULL) {
        return;
    }
    if (field == v->sort_field) {
        if (shift && v->sort2_field >= 0) {
            /* TS-003: swap the two, so the tie-break becomes the key and
             * the key becomes the tie-break. */
            int old = v->sort_field;

            v->sort_field = v->sort2_field;
            v->sort2_field = old;
        } else {
            /* With no secondary, Shift reverses the sort as a plain click
             * does. */
            v->sort_reverse = !v->sort_reverse;
        }
    } else if (shift) {
        /* TS-004: Shift on another column sets the tie-break, or clears it
         * when that column is already the tie-break. */
        v->sort2_field = (field == v->sort2_field) ? -1 : field;
    } else {
        v->sort_field = field;
        v->sort_reverse = 0;
    }
    if (v->sort2_field == v->sort_field) {
        v->sort2_field = -1;
    }
    pv_resort(v);
    if (v->filtering) {
        pv_rebuild_filter(v);
    }
    v->state_dirty = 1;
}
