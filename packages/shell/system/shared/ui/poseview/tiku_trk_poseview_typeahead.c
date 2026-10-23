/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_poseview_typeahead.c - typing to reach a pose, and the
 * type-ahead filter: its terms, the rows they let through, and Return.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_trk_poseview_private.h"

/**
 * @brief Type-ahead scoring: 1000 for a name starting with @p typed, else
 *        1/(offset + 1) for a name containing it.
 *
 * @return Score, higher is better; 0 means not a candidate.
 */
static float
type_score(const char *name, const char *typed)
{
    const char *hit;

    if (typed[0] == '\0') {
        return 0.0f;
    }
    if (strncasecmp(name, typed, strlen(typed)) == 0) {
        return 1000.0f;
    }
    hit = strcasestr(name, typed);
    if (hit == NULL) {
        return 0.0f;
    }
    return 1.0f / (float)(hit - name + 1);
}

int
tiku_trk_poseview_filter_pose(tiku_trk_poseview_t *v,
                              const tiku_trk_pose_t *p)
{
    unsigned char found[TIKU_TRK_FILTER_TERMS];
    int left, i, j;

    if (v == NULL || p == NULL) {
        return 0;
    }
    left = v->filter_count;
    memset(found, 0, sizeof found);
    /* An empty term matches everything and counts as found, so a filter
     * with nothing typed shows every row. */
    for (j = 0; j < v->filter_count; j++) {
        if (v->filter[j][0] == '\0') {
            found[j] = 1;
            left--;
        }
    }
    if (left == 0) {
        return 1;
    }
    /* Every column is searched, not just the name, and each term may land
     * in a different one. */
    for (i = 0; i < v->column_count; i++) {
        char buf[160];
        const char *text = tiku_trk_poseview_cell_text(v, p,
                                                       v->columns[i].field,
                                                       buf, sizeof buf);
        if (text == NULL) {
            continue;
        }
        for (j = 0; j < v->filter_count; j++) {
            if (found[j]) {
                continue;
            }
            if (strcasestr(text, v->filter[j]) != NULL) {
                if (--left == 0) {
                    return 1;
                }
                found[j] = 1;
            }
        }
    }
    return 0;
}

/**
 * @brief Rebuild the filtered list, unselecting every row the filter
 *        leaves out.
 */
void
pv_rebuild_filter(tiku_trk_poseview_t *v)
{
    int i;

    v->filtered_count = 0;
    for (i = 0; i < v->count; i++) {
        if (tiku_trk_poseview_filter_pose(v, &v->poses[i])) {
            v->filtered[v->filtered_count++] = i;
        } else {
            /* A row the filter leaves out is deselected, so no command
             * acts on a hidden row. */
            v->poses[i].selected = 0;
        }
    }
}

int
tiku_trk_poseview_filter_terms(const tiku_trk_poseview_t *v)
{
    return (v != NULL) ? v->filter_count : 0;
}

/** @brief The last term, which is the one being typed into. */
static char *
last_term(tiku_trk_poseview_t *v)
{
    return v->filter[(v->filter_count > 0) ? v->filter_count - 1 : 0];
}

int
tiku_trk_poseview_filter_term(tiku_trk_poseview_t *v)
{
    if (v == NULL || !v->filtering) {
        return 0;
    }
    if (last_term(v)[0] == '\0') {
        return 0;                   /* refuse: an empty term widens it     */
    }
    if (v->filter_count >= TIKU_TRK_FILTER_TERMS) {
        return 0;
    }
    v->filter[v->filter_count++][0] = '\0';
    return 1;
}

/**
 * @brief Re-apply the filter after a term changed.
 *
 * A term that grew can only take rows away, so the filtered list is walked
 * and thinned in place.  A term that shrank, or a term that went away, can
 * bring rows back, and nothing but a full rebuild can find them.
 */
void
pv_filter_changed(tiku_trk_poseview_t *v, int grew)
{
    if (!v->filtering) {
        return;
    }
    if (grew) {
        int i, w = 0;

        for (i = 0; i < v->filtered_count; i++) {
            if (tiku_trk_poseview_filter_pose(v, &v->poses[v->filtered[i]])) {
                v->filtered[w++] = v->filtered[i];
            } else {
                v->poses[v->filtered[i]].selected = 0;
            }
        }
        v->filtered_count = w;
    } else {
        pv_rebuild_filter(v);
    }
    /* PVL-061: a shrinking list pulls the scroll back, so the window is not
     * scrolled past the last row. */
    if (v->visible_rows > 0 && v->scroll > 0 &&
        v->scroll + v->visible_rows > v->filtered_count) {
        v->scroll = v->filtered_count - v->visible_rows;
        if (v->scroll < 0) {
            v->scroll = 0;
        }
    }
}

/** @brief Jump the selection to the best match for the typed string. */
static void
type_select_best(tiku_trk_poseview_t *v)
{
    int i, n = tiku_trk_poseview_count(v), best = -1;
    float bestscore = 0.0f;

    for (i = 0; i < n; i++) {
        tiku_trk_pose_t *p = tiku_trk_poseview_at(v, i);
        float s = type_score(p->model.name, v->typed);

        if (v->mode == TIKU_TRK_MODE_LIST) {
            /* Every column participates, as the filter's walk does: a
             * kind or a value typed at the list lands on the row that
             * shows it (PVK-029). */
            int c;

            for (c = 0; c < v->column_count; c++) {
                char buf[160];
                const char *text = tiku_trk_poseview_cell_text(v, p,
                    v->columns[c].field, buf, sizeof buf);
                float cs = (text != NULL) ? type_score(text, v->typed)
                                          : 0.0f;

                if (cs > s) {
                    s = cs;
                }
            }
        }
        if (s > bestscore) {
            bestscore = s;
            best = i;
        }
    }
    if (best >= 0) {
        pv_clear_selection(v);
        pv_set_selected(tiku_trk_poseview_at(v, best), 1);
        v->pivot = best;
        v->last_added = best;
        v->active = tiku_trk_poseview_at(v, best)->index;
        /* A match off screen is scrolled into view (PVL-054). */
        tiku_trk_poseview_scroll_into_view(v, best, v->visible_rows);
    }
}

void
tiku_trk_poseview_type_ahead(tiku_trk_poseview_t *v, char ch)
{
    if (v == NULL) {
        return;
    }
    if (v->filtering) {
        char *t = last_term(v);
        size_t n = strlen(t);

        if (n + 1u >= TIKU_TRK_TYPEAHEAD_MAX) {
            return;
        }
        t[n] = ch;
        t[n + 1u] = '\0';
        pv_filter_changed(v, 1);
        return;
    }
    if (v->typed_len + 1 >= TIKU_TRK_TYPEAHEAD_MAX) {
        return;
    }
    v->typed[v->typed_len++] = ch;
    v->typed[v->typed_len] = '\0';
    v->typed_at = tiku_trk_now();
    type_select_best(v);
}

void
tiku_trk_poseview_type_backspace(tiku_trk_poseview_t *v)
{
    if (v == NULL) {
        return;
    }
    if (v->filtering) {
        char *t = last_term(v);
        size_t n = strlen(t);

        if (n > 0u) {
            t[n - 1u] = '\0';
        } else if (v->filter_count > 1) {
            /* Backspacing an empty term removes it, and the '|' before it
             * in the shown string. */
            v->filter_count--;
        }
        pv_filter_changed(v, 0);
        return;
    }
    if (v->typed_len == 0) {
        return;
    }
    v->typed[--v->typed_len] = '\0';
    v->typed_at = tiku_trk_now();   /* backspace restarts the timeout */
    if (v->typed_len > 0) {
        type_select_best(v);
    }
}

void
tiku_trk_poseview_type_clear(tiku_trk_poseview_t *v)
{
    if (v != NULL) {
        v->typed[0] = '\0';
        v->typed_len = 0;
        v->typed_at = 0;
        v->filter_count = 1;
        v->filter[0][0] = '\0';
        if (v->filtering) {
            pv_rebuild_filter(v);
        }
    }
}

const char *
tiku_trk_poseview_type_string(const tiku_trk_poseview_t *v)
{
    if (v == NULL) {
        return "";
    }
    if (v->filtering) {
        /* Terms are shown joined by '|', as the count view draws them. */
        tiku_trk_poseview_t *m = (tiku_trk_poseview_t *)v;
        size_t at = 0;
        int i;

        m->filter_shown[0] = '\0';
        for (i = 0; i < v->filter_count; i++) {
            int n = snprintf(m->filter_shown + at,
                             sizeof m->filter_shown - at, "%s%s",
                             (i > 0) ? "|" : "", v->filter[i]);
            if (n < 0 || (size_t)n >= sizeof m->filter_shown - at) {
                break;
            }
            at += (size_t)n;
        }
        return m->filter_shown;
    }
    return v->typed;
}

void
tiku_trk_poseview_type_tick(tiku_trk_poseview_t *v)
{
    if (v == NULL || v->typed_len == 0 || v->typed_at == 0) {
        return;
    }
    if (tiku_trk_now() - v->typed_at > TIKU_TRK_TYPEAHEAD_US) {
        tiku_trk_poseview_type_clear(v);
    }
}

void
tiku_trk_poseview_set_filtering(tiku_trk_poseview_t *v, int on)
{
    if (v == NULL) {
        return;
    }
    if (!on) {
        /* Leaving the mode drops the terms, so they do not hide rows the
         * next time filtering starts. */
        v->filter_count = 1;
        v->filter[0][0] = '\0';
    }
    v->filtering = on ? 1 : 0;
    pv_rebuild_filter(v);
    if (!on) {
        v->scroll = 0;
    }
}

int
tiku_trk_poseview_is_filtering(const tiku_trk_poseview_t *v)
{
    return (v != NULL) ? v->filtering : 0;
}

int
tiku_trk_poseview_return_pressed(tiku_trk_poseview_t *v, int shift)
{
    if (v == NULL) {
        return 0;
    }
    if (v->filtering && tiku_trk_poseview_selection_count(v) == 0 &&
        tiku_trk_poseview_count(v) > 0) {
        /* PVK-015: with nothing selected, Return selects the top filtered
         * row and makes it the pivot and the active row. */
        tiku_trk_pose_t *p = tiku_trk_poseview_at(v, 0);

        pv_clear_selection(v);
        pv_set_selected(p, 1);
        v->pivot = p->index;
        v->last_added = p->index;
        v->active = p->index;
    }
    /* PVK-016: with Shift the caller also ends filtering, putting the full
     * listing back. */
    return (shift && v->filtering) ? 1 : 0;
}
