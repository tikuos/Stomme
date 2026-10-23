/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_poseview_select.c - clicks and the selection they make, the
 * pivot a range extends from, the band sweep, and the repaint damage the
 * view hands its owner.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_trk_poseview_private.h"

/**
 * @brief Select or deselect one pose, stamping when it joined.
 *
 * The slow-second-click rename reads the stamp, so the click that selected
 * a pose does not also arm its rename.
 */
void
pv_set_selected(tiku_trk_pose_t *p, int on)
{
    if (p == NULL) {
        return;
    }
    if (on && !p->selected) {
        p->sel_time = tiku_trk_now();
    }
    p->selected = on ? 1 : 0;
}

/** @brief Every pose unselected; the pivots are the caller's to mend. */
void
pv_clear_selection(tiku_trk_poseview_t *v)
{
    int i;

    for (i = 0; i < v->count; i++) {
        v->poses[i].selected = 0;
    }
}

void
tiku_trk_poseview_select_none(tiku_trk_poseview_t *v)
{
    if (v != NULL) {
        pv_clear_selection(v);
        v->pivot = -1;              /* PVS-008: empty selection, no pivot */
        v->real_pivot = -1;
        v->last_added = -1;
    }
}

int
tiku_trk_poseview_select_after_removal(tiku_trk_poseview_t *v, int index)
{
    int n;

    if (v == NULL || tiku_trk_poseview_mode(v) != TIKU_TRK_MODE_LIST) {
        /* Icon modes have no next row: nothing is selected. */
        return -1;
    }
    n = tiku_trk_poseview_count(v);
    if (n == 0) {
        tiku_trk_poseview_select_none(v);
        return -1;
    }
    /* The row that has taken the removed one's index -- the next one down
     * before the removal.  Past the end means the last row went, and the
     * new last row is selected instead. */
    if (index < 0) {
        index = 0;
    }
    if (index >= n) {
        index = n - 1;
    }
    tiku_trk_poseview_click(v, index, 0u);
    return index;
}

void
tiku_trk_poseview_select_all(tiku_trk_poseview_t *v)
{
    int i, n;

    if (v == NULL) {
        return;
    }
    n = tiku_trk_poseview_count(v);
    if (v->single_only) {
        /* In a single-selection view the command selects the first pose
         * (PVS-009). */
        n = (n > 0) ? 1 : 0;
    }
    for (i = 0; i < n; i++) {
        pv_set_selected(tiku_trk_poseview_at(v, i), 1);
    }
    if (n > 0) {
        /* The pivot moves to the first row even when one was set, so the
         * next shift-click extends from this selection. */
        v->pivot = tiku_trk_poseview_at(v, 0)->index;
        v->last_added = tiku_trk_poseview_at(v, n - 1)->index;
    }
}

/**
 * @brief The display row of pose index @p pi: the index itself when the
 *        view is not filtering, else its position in the filtered list.
 *        @return the row, or -1 when @p pi is out of range or filtered
 *        out.
 */
int
pv_display_of(const tiku_trk_poseview_t *v, int pi)
{
    int i;

    if (pi < 0) {
        return -1;
    }
    if (!v->filtering) {
        return (pi < v->count) ? pi : -1;
    }
    for (i = 0; i < v->filtered_count; i++) {
        if (v->filtered[i] == pi) {
            return i;
        }
    }
    return -1;
}

/**
 * @brief Select, or with @p on clear, the range between two pose indices.
 *
 * The anchors are pose-array indices, which survive a filter change; the
 * walk is over the displayed list, so a range never selects a filtered-out
 * row.
 */
static void
select_range(tiku_trk_poseview_t *v, int from_pose, int to_pose, int on)
{
    int a = pv_display_of(v, from_pose), b = pv_display_of(v, to_pose);
    int i, lo, hi;

    if (a < 0 || b < 0) {
        return;
    }
    lo = (a < b) ? a : b;
    hi = (a < b) ? b : a;
    for (i = lo; i <= hi; i++) {
        tiku_trk_pose_t *p = tiku_trk_poseview_at(v, i);
        pv_set_selected(p, on);
    }
}

/** @brief Select, or with @p on clear, the icon-mode rectangle from a pose's
 *         location to the point @p x, @p y. */
static void
select_icon_range(tiku_trk_poseview_t *v, int from_pose, int x, int y, int on)
{
    tiku_trk_pose_t *anchor;
    int ax = 0, ay = 0, i, n;
    int lox, loy, hix, hiy;

    if (from_pose < 0 || from_pose >= v->count) {
        return;
    }
    anchor = &v->poses[from_pose];
    tiku_trk_poseview_pose_location(v, anchor, &ax, &ay);
    lox = (x < ax) ? x : ax;
    loy = (y < ay) ? y : ay;
    hix = (x < ax) ? ax : x;
    hiy = (y < ay) ? ay : y;
    /* A zero-width or zero-height rectangle still catches the poses it
     * touches, so it is widened to one pixel for the half-open test below
     * (PVS-005). */
    if (hix - lox < 1) { hix = lox + 1; }
    if (hiy - loy < 1) { hiy = loy + 1; }

    n = tiku_trk_poseview_count(v);
    for (i = n - 1; i >= 0; i--) {
        tiku_trk_pose_t *p = tiku_trk_poseview_at(v, i);
        int px, py, pw, ph;

        if (p == NULL || !p->placed) {
            continue;
        }
        tiku_trk_poseview_pose_rect(v, p, &px, &py, &pw, &ph);
        if (px + pw > lox && px < hix && py + ph > loy && py < hiy) {
            pv_set_selected(p, on);
        }
    }
}

/** @brief Restore the selection/pivot invariant after every click branch. */
static void
repair_click_pivot(tiku_trk_poseview_t *v, const tiku_trk_pose_t *clicked)
{
    if (tiku_trk_poseview_selection_count(v) == 0) {
        v->pivot = -1;
        v->real_pivot = -1;
        v->last_added = -1;
    } else if (v->pivot < 0 && clicked != NULL) {
        /* The pivot falls to the clicked pose, even when a Shift+Command
         * range deselected it but left others selected. */
        v->pivot = clicked->index;
    }
}

void
tiku_trk_poseview_set_multiple(tiku_trk_poseview_t *v, int on)
{
    if (v != NULL) {
        v->single_only = !on;
        if (!on) {
            /* An existing selection goes down to its first pose. */
            int i, n = tiku_trk_poseview_count(v), kept = 0;

            for (i = 0; i < n; i++) {
                tiku_trk_pose_t *p = tiku_trk_poseview_at(v, i);

                if (p != NULL && p->selected) {
                    if (kept) {
                        p->selected = 0;
                    } else {
                        kept = 1;
                    }
                }
            }
        }
    }
}

int
tiku_trk_poseview_multiple(const tiku_trk_poseview_t *v)
{
    return (v != NULL) ? !v->single_only : 1;
}

void
tiku_trk_poseview_click(tiku_trk_poseview_t *v, int index, unsigned mods)
{
    tiku_trk_pose_t *p = tiku_trk_poseview_at(v, index);
    int x = 0, y = 0;

    if (p != NULL) {
        int w = 0, h = 0;

        /* A coordinate-free click lands just inside the pose's rect: an
         * icon-mode pose location is the cell's top-left, which may be
         * empty margin beside centred art. */
        tiku_trk_poseview_pose_rect(v, p, &x, &y, &w, &h);
        x++;
        y++;
    }
    tiku_trk_poseview_click_at(v, index, x, y, mods);
}

void
tiku_trk_poseview_click_at(tiku_trk_poseview_t *v, int index, int x, int y,
                           unsigned mods)
{
    tiku_trk_pose_t *p;

    if (v == NULL) {
        return;
    }
    if (v->single_only) {
        /* The extending modifiers are dropped, so a modifier click is a
         * plain click (PVS-009). */
        mods &= ~(TIKU_TRK_MOD_SHIFT | TIKU_TRK_MOD_CMD);
    }
    if (index < 0) {
        /* PVS-001: clicking empty space clears the whole selection, and with
         * it any rename waiting to start (PVS-015). */
        tiku_trk_poseview_cancel_edit_wait(v);
        tiku_trk_poseview_select_none(v);
        v->active = -1;
        return;
    }
    p = tiku_trk_poseview_at(v, index);
    if (p == NULL) {
        return;
    }
    /* PVS-015: a click on a different pose disarms a pending rename. */
    if (v->edit_clicked_at != 0 && v->edit_pose != p->index) {
        tiku_trk_poseview_cancel_edit_wait(v);
    }
    v->active = p->index;

    if ((mods & TIKU_TRK_MOD_CMD) && (mods & TIKU_TRK_MOD_SHIFT)) {
        /* PVS-007: the range takes the state the clicked pose does not
         * already have, so Shift+Command on an unselected pose adds the
         * range. */
        int on = !p->selected;

        if (v->pivot >= 0) {
            if (v->mode == TIKU_TRK_MODE_LIST) {
                select_range(v, v->pivot, p->index, on);
            } else {
                select_icon_range(v, v->pivot, x, y, on);
            }
        } else {
            pv_set_selected(p, on);
        }
        if (!on && v->pivot >= 0 && !v->poses[v->pivot].selected) {
            v->pivot = -1;
        }
        if (!on && v->real_pivot >= 0 &&
            !v->poses[v->real_pivot].selected) {
            v->real_pivot = -1;
        }
        repair_click_pivot(v, p);
        return;
    }
    if (mods & TIKU_TRK_MOD_CMD) {
        /* PVS-004: toggle one, leave the rest alone. */
        pv_set_selected(p, !p->selected);
        if (p->selected) {
            v->pivot = p->index;
            v->last_added = p->index;
        } else if (tiku_trk_poseview_selection_count(v) == 0) {
            v->pivot = -1;          /* PVS-008 */
        }
        repair_click_pivot(v, p);
        return;
    }
    if (mods & TIKU_TRK_MOD_SHIFT) {
        /* PVS-005/006: the selection is cleared but the pivot kept, so
         * each shift-click selects the range from the pivot afresh. */
        int pivot = (v->pivot >= 0) ? v->pivot : p->index;
        pv_clear_selection(v);
        v->pivot = pivot;
        if (v->mode == TIKU_TRK_MODE_LIST) {
            select_range(v, pivot, p->index, 1);
        } else {
            select_icon_range(v, pivot, x, y, 1);
        }
        v->last_added = p->index;
        repair_click_pivot(v, p);
        return;
    }
    /* PVS-003/PVS-014: a selected pose stays selected without being stamped
     * again, which keeps a group for a drag and keeps a single pose's stamp
     * for the slow-second-click rename. */
    if (p->selected && (tiku_trk_poseview_selection_count(v) > 1 ||
                        v->pivot == p->index)) {
        return;
    }
    /* PVS-002: otherwise select just this one and make it the pivot. */
    pv_clear_selection(v);
    pv_set_selected(p, 1);
    v->pivot = p->index;
    v->last_added = p->index;
    repair_click_pivot(v, p);
}

void
tiku_trk_poseview_invert_selection(tiku_trk_poseview_t *v)
{
    int i, n;

    if (v == NULL) {
        return;
    }
    n = tiku_trk_poseview_count(v);
    /* The anchors go first; the pivot is set again below when the first
     * row ends up selected. */
    v->pivot = -1;
    v->real_pivot = -1;
    v->last_added = -1;
    for (i = 0; i < n; i++) {
        tiku_trk_pose_t *p = tiku_trk_poseview_at(v, i);

        if (p == NULL) {
            continue;
        }
        pv_set_selected(p, !p->selected);
        if (i == 0 && p->selected) {
            v->pivot = p->index;
        }
    }
}

int
tiku_trk_poseview_select_matching(tiku_trk_poseview_t *v, const char *expr,
                                  tiku_trk_match_t how, int ignore_case,
                                  int invert, char *err, size_t errmax)
{
    int i, n, hits = 0;

    if (v == NULL || expr == NULL) {
        return -1;
    }
    /* The expression is checked before anything is cleared, so a rejected
     * expression leaves the selection as it was. */
    if (how == TIKU_TRK_MATCH_REGEX &&
        !tiku_trk_regex_check(expr, err, errmax)) {
        return -1;
    }
    if (err != NULL && errmax > 0u) {
        err[0] = '\0';
    }
    tiku_trk_poseview_select_none(v);
    n = tiku_trk_poseview_count(v);
    for (i = 0; i < n; i++) {
        tiku_trk_pose_t *p = tiku_trk_poseview_at(v, i);
        int m;

        if (p == NULL) {
            continue;
        }
        m = tiku_trk_match(p->model.name, expr, !ignore_case, how);
        if (m ^ (invert ? 1 : 0)) {
            pv_set_selected(p, 1);
            if (hits == 0) {
                v->pivot = p->index;
                v->active = p->index;
            }
            v->last_added = p->index;
            hits++;
        }
    }
    if (hits > 0) {
        int i, n = tiku_trk_poseview_count(v);

        for (i = 0; i < n; i++) {
            tiku_trk_pose_t *p = tiku_trk_poseview_at(v, i);

            if (p != NULL && p->selected) {
                /* The first match is scrolled into view (PVL-054). */
                tiku_trk_poseview_scroll_into_view(v, i, v->visible_rows);
                break;
            }
        }
    }
    return hits;
}

int
tiku_trk_poseview_context_click(tiku_trk_poseview_t *v, int index)
{
    tiku_trk_pose_t *p;

    if (v == NULL) {
        return 0;
    }
    if (index < 0) {
        /* Empty space: the menu is about the folder, so the selection is
         * cleared. */
        if (tiku_trk_poseview_selection_count(v) == 0) {
            return 0;
        }
        tiku_trk_poseview_select_none(v);
        v->active = -1;
        return 1;
    }
    p = tiku_trk_poseview_at(v, index);
    if (p == NULL) {
        return 0;
    }
    v->active = p->index;
    if (p->selected) {
        return 0;               /* a group keeps its group                */
    }
    pv_clear_selection(v);
    pv_set_selected(p, 1);
    v->pivot = p->index;
    v->last_added = p->index;
    return 1;
}

int
tiku_trk_poseview_draws_selected(const tiku_trk_poseview_t *v,
                                 const tiku_trk_pose_t *p)
{
    if (v == NULL || p == NULL || !p->selected) {
        return 0;
    }
    /* A band in progress paints its sweep even in an inactive window
     * (PVS-031). */
    return v->selection_visible || v->show_sel_inactive || v->band;
}

void
tiku_trk_poseview_window_activated(tiku_trk_poseview_t *v, int active)
{
    if (v == NULL) {
        return;
    }
    if (v->show_sel_inactive) {
        v->selection_visible = 1;
        return;
    }
    if (active == v->selection_visible) {
        return;
    }
    v->selection_visible = active ? 1 : 0;
    if (!active) {
        /* The pivot is parked, and restored when the window is active
         * again. */
        v->real_pivot = v->pivot;
        v->pivot = -1;
    } else if (v->real_pivot >= 0) {
        v->pivot = v->real_pivot;
        v->real_pivot = -1;
    }
}

void
tiku_trk_poseview_show_selection_when_inactive(tiku_trk_poseview_t *v, int on)
{
    if (v != NULL) {
        v->show_sel_inactive = on ? 1 : 0;
        if (on) {
            v->selection_visible = 1;
            if (v->real_pivot >= 0) {
                v->pivot = v->real_pivot;
                v->real_pivot = -1;
            }
        }
    }
}

void
tiku_trk_poseview_set_band(tiku_trk_poseview_t *v, int on)
{
    if (v == NULL) {
        return;
    }
    if (!on) {
        if (v->band_seen) {
            /* The sweep is over and the band is not drawn again, so the
             * ground it lay on is repainted: a partial frame, left by a
             * value arriving mid-sweep, would otherwise keep the band on
             * screen. */
            tiku_trk_repaint_add(&v->repaint, v->band_rect);
        }
        v->band_seen = 0;       /* the next sweep is a new one */
    }
    if (on && v->band_was != NULL) {
        int i;

        /* The band toggles against the selection as it was when the sweep
         * began, so the snapshot is taken here. */
        for (i = 0; i < v->count; i++) {
            v->band_was[i] = (unsigned char)(v->poses[i].selected ? 1 : 0);
        }
    }
    if (!on && v->pivot >= 0 && v->pivot < v->count &&
        !v->poses[v->pivot].selected) {
        /* A pivot the sweep left unselected is dropped: the next
         * shift-click would otherwise extend from a pose that is no longer
         * part of anything (PVS-029). */
        v->pivot = -1;
    }
    v->band = on ? 1 : 0;
}

int
tiku_trk_poseview_band(const tiku_trk_poseview_t *v)
{
    return (v != NULL) ? v->band : 0;
}

void
tiku_trk_poseview_damage_row(tiku_trk_poseview_t *v, int index)
{
    int rh;

    if (v == NULL || index < 0 || index >= v->count) {
        return;
    }
    rh = (v->row_h > 0) ? v->row_h : 1;
    tiku_trk_repaint_add(&v->repaint,
        (tiku_trk_repaint_rect_t){ 0, (index - v->scroll) * rh,
                                   (v->view_w > 0) ? v->view_w : 1, rh });
}

int
tiku_trk_poseview_repaint_take(tiku_trk_poseview_t *v,
                               tiku_trk_repaint_rect_t *out, int max)
{
    return v != NULL ? tiku_trk_repaint_take(&v->repaint, out, max) : 0;
}

int
tiku_trk_poseview_repaint_take_slide(tiku_trk_poseview_t *v, int *y)
{
    return v != NULL ? tiku_trk_repaint_take_slide(&v->repaint, y) : 0;
}
