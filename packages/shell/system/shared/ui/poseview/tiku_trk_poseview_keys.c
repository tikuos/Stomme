/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_poseview_keys.c - arrow, Page, Home and End navigation, and what
 * each key does to the selection and the scroll.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_trk_poseview_private.h"

/** @brief Row of the shown list holding the active pose, or -1. */
static int
active_row(tiku_trk_poseview_t *v)
{
    int i, n = tiku_trk_poseview_count(v);

    for (i = 0; i < n; i++) {
        if (tiku_trk_poseview_at(v, i)->index == v->active) {
            return i;
        }
    }
    return -1;
}

/**
 * @brief The pose an arrow from @p row lands on in an icon mode (PVK-007).
 *
 * Among the poses in the half-plane the key points at, the nearest by a
 * distance in which movement across the key's axis counts double.
 *
 * @return a display index, or -1 when there is nothing that way.
 */
static int
nearest_in_direction(tiku_trk_poseview_t *v, int row, int key)
{
    int i, n = tiku_trk_poseview_count(v), best = -1;
    long best_score = 0;
    int cx = 0, cy = 0;

    if (n == 0) {
        return -1;
    }
    if (row < 0) {
        /* PVK-008: with nothing selected the first arrow picks the
         * upper-left-most pose, whichever key it was. */
        for (i = 0; i < n; i++) {
            tiku_trk_pose_t *p = tiku_trk_poseview_at(v, i);
            int px, py, pw, ph;

            if (p == NULL || !p->placed) {
                continue;
            }
            tiku_trk_poseview_pose_rect(v, p, &px, &py, &pw, &ph);
            if (best < 0 || py < cy || (py == cy && px < cx)) {
                best = i;
                cx = px;
                cy = py;
            }
        }
        return best;
    }
    {
        tiku_trk_pose_t *from = tiku_trk_poseview_at(v, row);
        int px, py, pw, ph;

        if (from == NULL) {
            return -1;
        }
        tiku_trk_poseview_pose_rect(v, from, &px, &py, &pw, &ph);
        cx = px + pw / 2;
        cy = py + ph / 2;
    }
    for (i = 0; i < n; i++) {
        tiku_trk_pose_t *p = tiku_trk_poseview_at(v, i);
        int px, py, pw, ph, mx, my;
        long along, across, score;

        if (p == NULL || !p->placed || i == row) {
            continue;
        }
        tiku_trk_poseview_pose_rect(v, p, &px, &py, &pw, &ph);
        mx = px + pw / 2;
        my = py + ph / 2;
        switch (key) {
        case TIKU_TRK_KEY_UP:
            if (my >= cy) { continue; }
            along = cy - my;  across = (mx > cx) ? mx - cx : cx - mx;
            break;
        case TIKU_TRK_KEY_DOWN:
            if (my <= cy) { continue; }
            along = my - cy;  across = (mx > cx) ? mx - cx : cx - mx;
            break;
        case TIKU_TRK_KEY_LEFT:
            if (mx >= cx) { continue; }
            along = cx - mx;  across = (my > cy) ? my - cy : cy - my;
            break;
        default:
            if (mx <= cx) { continue; }
            along = mx - cx;  across = (my > cy) ? my - cy : cy - my;
            break;
        }
        /* Across counts double, so a step down stays in its column when the
         * next column's icon is marginally closer. */
        score = along + 2 * across;
        if (best < 0 || score < best_score) {
            best = i;
            best_score = score;
        }
    }
    return best;
}

int
tiku_trk_poseview_key_nav(tiku_trk_poseview_t *v, int key, unsigned mods)
{
    int n, row, target;

    if (v == NULL) {
        return 0;
    }
    n = tiku_trk_poseview_count(v);
    if (n == 0) {
        return 1;
    }
    /* PVK-003: with an empty selection an arrow seeds, even after Select
     * None, which leaves the active pose remembered. */
    row = (tiku_trk_poseview_selection_count(v) == 0) ? -1 : active_row(v);

    if (v->mode != TIKU_TRK_MODE_LIST &&
        (key == TIKU_TRK_KEY_UP || key == TIKU_TRK_KEY_DOWN ||
         key == TIKU_TRK_KEY_LEFT || key == TIKU_TRK_KEY_RIGHT)) {
        /* In an icon mode an arrow moves geometrically (PVK-007). */
        target = nearest_in_direction(v, row, key);
        if (target < 0) {
            return 1;           /* nothing that way; the selection stays */
        }
    } else {
    switch (key) {
    case TIKU_TRK_KEY_UP:
    case TIKU_TRK_KEY_LEFT:
        if (row < 0) {
            target = n - 1;         /* PVK-003: seed at the last pose */
        } else if (row == 0) {
            /* PVK-002: Left wraps, Up does not. */
            target = (key == TIKU_TRK_KEY_LEFT) ? n - 1 : 0;
        } else {
            target = row - 1;
        }
        break;
    case TIKU_TRK_KEY_DOWN:
    case TIKU_TRK_KEY_RIGHT:
        if (row < 0) {
            target = 0;             /* PVK-003: seed at the first pose */
        } else if (row == n - 1) {
            target = (key == TIKU_TRK_KEY_RIGHT) ? 0 : n - 1;
        } else {
            target = row + 1;
        }
        break;
    case TIKU_TRK_KEY_HOME:  target = 0;     break;
    case TIKU_TRK_KEY_END:   target = n - 1; break;
    case TIKU_TRK_KEY_PGUP:
    case TIKU_TRK_KEY_PGDN: {
        /* A page is one row less than the window shows, so paging skips
         * no row unseen. */
        int page = (v->visible_rows > 1) ? v->visible_rows - 1 : 1;
        /* From the selected row while it is on screen, else from the first
         * visible row (PVK-011). */
        int base = (row >= 0 && row >= v->scroll &&
                    row < v->scroll + v->visible_rows) ? row : v->scroll;

        if (n == 0) {
            target = -1;
        } else if (key == TIKU_TRK_KEY_PGUP) {
            target = (base > page) ? base - page : 0;
        } else {
            target = (base + page < n) ? base + page : n - 1;
        }
        break;
    }
    default:
        return 0;
    }

    if (v->no_autoscroll &&
        (key == TIKU_TRK_KEY_HOME || key == TIKU_TRK_KEY_END ||
         key == TIKU_TRK_KEY_PGUP || key == TIKU_TRK_KEY_PGDN)) {
        /* The Desktop is exempt: it does not scroll, so these keys do
         * nothing there (PVK-013). */
        return 1;
    }
    if (v->mode != TIKU_TRK_MODE_LIST &&
        (key == TIKU_TRK_KEY_HOME || key == TIKU_TRK_KEY_END ||
         key == TIKU_TRK_KEY_PGUP || key == TIKU_TRK_KEY_PGDN)) {
        /* PVK-010/PVL-055: in an icon mode Home, End and Page move the
         * view, in pixels against the extent, and leave the selection
         * alone. */
        int ex, ey, ew, eh, ox, oy;
        int page = (v->view_h > 1) ? v->view_h : 1;

        tiku_trk_poseview_extent(v, &ex, &ey, &ew, &eh);
        tiku_trk_poseview_origin(v, &ox, &oy);
        switch (key) {
        case TIKU_TRK_KEY_HOME: oy = ey; break;
        case TIKU_TRK_KEY_END:  oy = ey + eh; break;
        case TIKU_TRK_KEY_PGUP: oy -= page; break;
        default:                oy += page; break;
        }
        tiku_trk_poseview_set_origin(v, ox, oy);
        return 1;
    }

    }
    if ((mods & TIKU_TRK_MOD_SHIFT) && !v->single_only &&
        (key == TIKU_TRK_KEY_HOME || key == TIKU_TRK_KEY_END ||
         key == TIKU_TRK_KEY_PGUP || key == TIKU_TRK_KEY_PGDN)) {
        /* A jump key with Shift selects everything between where the
         * selection stood and where the jump lands -- Shift+End extends
         * to the end, not by one row (PVK-012). */
        int from = (row >= 0) ? row : target;
        int lo = (from < target) ? from : target;
        int hi = (from > target) ? from : target;
        int r2;

        for (r2 = lo; r2 <= hi; r2++) {
            tiku_trk_pose_t *t2 = tiku_trk_poseview_at(v, r2);

            if (t2 != NULL) {
                pv_set_selected(t2, 1);
            }
        }
        {
            tiku_trk_pose_t *t2 = tiku_trk_poseview_at(v, target);

            if (t2 != NULL) {
                v->last_added = t2->index;
                if (v->pivot < 0) {
                    v->pivot = (row >= 0)
                                   ? tiku_trk_poseview_at(v, row)->index
                                   : t2->index;
                }
            }
        }
    } else if ((mods & TIKU_TRK_MOD_SHIFT) && !v->single_only) {
        /* PVK-005: grow by one, or walk back when the target is already in
         * the selection.  In a single-selection view Shift adds nothing --
         * it is a plain arrow (PVK-006). */
        tiku_trk_pose_t *t = tiku_trk_poseview_at(v, target);

        if (t != NULL && t->selected && v->last_added >= 0) {
            tiku_trk_pose_t *last = tiku_trk_poseview_at(v,
                                        pv_display_of(v, v->last_added));

            if (last != NULL) {
                last->selected = 0;
            }
            /* The walk-back moves the mark to the target, the pose the
             * walk now stands on, so the next step drops the next pose. */
            v->last_added = t->index;
        } else if (t != NULL) {
            pv_set_selected(t, 1);
            v->last_added = t->index;
        }
        if (v->pivot < 0 && t != NULL) {
            v->pivot = t->index;
        }
    } else {
        /* PVK-004: a plain arrow replaces the whole selection. */
        pv_clear_selection(v);
        pv_set_selected(tiku_trk_poseview_at(v, target), 1);
        v->pivot = tiku_trk_poseview_at(v, target)->index;
        v->last_added = v->pivot;
    }
    v->active = tiku_trk_poseview_at(v, target)->index;
    /* PVL-054: the moved selection is scrolled into view. */
    tiku_trk_poseview_scroll_into_view(v, target, v->visible_rows);
    return 1;
}
