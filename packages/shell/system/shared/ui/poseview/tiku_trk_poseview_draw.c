/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_poseview_draw.c - re-reading the values of the rows on screen,
 * keeping rows in sort order, and the column set: adding, removing and
 * registering columns, and copying a layout between views.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_trk_poseview_private.h"

/**
 * @brief Whether the pose array is ordered under the current sort.
 *
 * The incremental insert is a binary search that needs this to hold, and a
 * changed sort key can break it.
 */
int
pv_poses_sorted(tiku_trk_poseview_t *v)
{
    int i;

    pv_g_sorting = v;
    for (i = 1; i < v->count; i++) {
        if (pv_pose_cmp(&v->poses[i - 1], &v->poses[i]) > 0) {
            pv_g_sorting = NULL;
            return 0;
        }
    }
    pv_g_sorting = NULL;
    return 1;
}

int
tiku_trk_poseview_recheck_order(tiku_trk_poseview_t *v, int at)
{
    tiku_trk_pose_t moved;
    int slot, now_at, was_active, scroll_was;

    if (v == NULL || at < 0 || at >= v->count) {
        return -1;
    }
    moved = v->poses[at];
    slot = pv_sorted_slot(v, &moved);
    if (slot == at || slot == at + 1) {
        return at;                      /* already where it belongs        */
    }
    was_active = (v->active == at);
    scroll_was = v->scroll;
    pv_remove_pose(v, at);
    v->scroll = scroll_was;             /* a re-slot keeps the scroll      */
    now_at = pv_insert_sorted(v, &moved);
    if (now_at >= 0) {
        v->poses[now_at].selected = moved.selected;
        v->poses[now_at].sel_time = moved.sel_time;
        if (was_active) {
            v->active = now_at;
        }
    }
    if (v->filtering) {
        /* filtered[] holds indices into the pose array, which remove and
         * insert have just renumbered: left alone, the filtered listing
         * would point at whatever slid into those slots. */
        pv_rebuild_filter(v);
    }
    return now_at;
}

/**
 * @brief Re-read the value of shown row @p i.
 *
 * The whole cost of a refresh is this one backend read, which a store on
 * the far end of a wire can take seconds over; refresh_until() bounds the
 * sweep by a deadline between reads.
 *
 * @return 1 when what the row shows changed.
 */
static int
refresh_one(tiku_trk_poseview_t *v, int i)
{
    int changed = 0;
    {
        tiku_trk_pose_t *p = tiku_trk_poseview_at(v, i);
        char buf[256];
        char local[TIKU_PATH_MAX];
        int len;

        if (p == NULL || tiku_model_is_container(&p->model)) {
            return 0;
        }
        if (!(p->model.facts.perm & TIKU_P_READ)) {
            return 0;
        }
        /* A board mounted at /devices/<name> names its nodes from its own
         * root, so the policy is asked about the path below the mount. */
        if (tiku_backend_serving(v->backend, p->model.path, local,
                                 sizeof local) == NULL ||
            !tiku_attr_autoread(p->model.facts.meta, local)) {
            return 0;
        }
        len = v->backend->ops->read(v->backend, p->model.path, buf,
                                    sizeof buf - 1);
        if (len < 0 || (size_t)len >= sizeof buf) {
            return 0;
        }
        buf[len] = '\0';
        {
            char *nl = strchr(buf, '\n');
            if (nl != NULL) { *nl = '\0'; }
        }
        if (strcmp(p->value, buf) != 0) {
            snprintf(p->value, sizeof p->value, "%s", buf);
            p->value_valid = 1;
            changed++;
            /* Only the row whose reading changed is repainted (PVL-075). */
            tiku_trk_poseview_damage_row(v, i);
            if (v->sort_field == TIKU_TRK_COL_VALUE ||
                v->sort2_field == TIKU_TRK_COL_VALUE) {
                /* MA-055: the sort key has changed, so the row moves to
                 * its new slot now.  The walk carries on from the same
                 * index and may re-read a row that shifted into it; every
                 * value is re-read on the next tick. */
                (void)tiku_trk_poseview_recheck_order(v, p->index);
            }
            return changed;
        }
        if (!p->value_valid) {
            /* The reading has not changed, but the row has shown a dash
             * since a notice invalidated it: showing the reading again is
             * a change, or the dash stays until something else repaints
             * the row. */
            p->value_valid = 1;
            changed++;
            tiku_trk_poseview_damage_row(v, i);
        }
    }
    return changed;
}

int
tiku_trk_poseview_refresh_values(tiku_trk_poseview_t *v, int first, int count)
{
    int i, n, changed = 0;

    if (v == NULL) {
        return 0;
    }
    n = tiku_trk_poseview_count(v);
    for (i = first; i < first + count && i < n; i++) {
        changed += refresh_one(v, i);
    }
    return changed;
}

/**
 * @brief Re-read up to REFRESH_WINDOW rows from the first one shown, for
 *        as long as @p deadline allows.
 *
 * The cursor survives the tick: each tick reads what the deadline allows,
 * at least one row, and leaves the rest for the next.  It advances before
 * each read, so a row whose read overruns is not retried every tick ahead
 * of the rows behind it.
 *
 * @return how many rows' values changed.
 */
static int
refresh_until(tiku_trk_poseview_t *v, int64_t deadline)
{
    int n = tiku_trk_poseview_count(v);
    int window = (n < REFRESH_WINDOW) ? n : REFRESH_WINDOW;
    int changed = 0, done = 0;

    if (window <= 0) {
        return 0;
    }
    /* Outside the rows on screen -- the view scrolled, or the listing
     * changed under it -- start again at the top of what is visible. */
    if (v->refresh_at < v->scroll || v->refresh_at >= v->scroll + window ||
        v->refresh_at >= n) {
        v->refresh_at = v->scroll;
    }
    while (done < window) {
        int i = v->refresh_at;

        v->refresh_at++;
        if (v->refresh_at >= v->scroll + window || v->refresh_at >= n) {
            v->refresh_at = v->scroll;
        }
        changed += refresh_one(v, i);
        done++;
        if (tiku_trk_now() >= deadline) {
            break;
        }
    }
    return changed;
}

int
tiku_trk_poseview_pump(tiku_trk_poseview_t *v, int timeout_ms)
{
    int64_t deadline;

    if (v == NULL || v->backend->ops->pump == NULL) {
        return 0;
    }
    /* One deadline for the whole tick: the pump spends what it needs and
     * the value sweep gets what is left, though it reads at least one row,
     * so a tick costs about what it was given. */
    deadline = tiku_trk_now() + (int64_t)timeout_ms * 1000;
    (void)v->backend->ops->pump(v->backend, timeout_ms);
    return refresh_until(v, deadline);
}

/** @brief Title and default width for a field, attribute columns
 *         included; an unknown field answers as the name column. */
void
pv_column_def_v(const tiku_trk_poseview_t *v, int field, const char **title,
             int *width)
{
    if (v != NULL && field >= TIKU_TRK_COL_ATTR_BASE) {
        int at = field - TIKU_TRK_COL_ATTR_BASE;

        if (at >= 0 && at < v->attrcol_count) {
            *title = v->attrcol[at].title;
            *width = v->attrcol[at].width;
            return;
        }
    }
    switch (field) {
    case TIKU_TRK_COL_KIND:  *title = "Kind";  *width = 140; break;
    case TIKU_TRK_COL_VALUE: *title = "Value"; *width = 190; break;
    case TIKU_TRK_COL_SIZE:  *title = "Size";  *width =  90; break;
    case TIKU_TRK_COL_CAP:   *title = "Cap";   *width =  60; break;
    case TIKU_TRK_COL_DATE:  *title = "Modified"; *width = 150; break;
    case TIKU_TRK_COL_PATH:  *title = "Location"; *width = 200; break;
    case TIKU_TRK_COL_PERM:  *title = "Permissions"; *width = 90; break;
    case TIKU_TRK_COL_ORIGINAL: *title = "Original path"; *width = 220; break;
    default:                 *title = "Name";  *width = 240; break;
    }
}

/**
 * @brief The sort key a view falls back to when its sort column goes: the
 *        column leading the strip, or the name with no columns (MA-067).
 */
int
pv_sort_fallback(const tiku_trk_poseview_t *v)
{
    return (v->column_count > 0) ? v->columns[0].field : TIKU_TRK_COL_NAME;
}

/** @brief A new column's alignment: right for the size, left for the
 *         rest. */
static int
column_alignment(int field)
{
    return (field == TIKU_TRK_COL_SIZE) ? TIKU_TRK_ALIGN_RIGHT
                                        : TIKU_TRK_ALIGN_LEFT;
}

int
tiku_trk_poseview_types_present(const tiku_trk_poseview_t *v,
                                const char **out, int max)
{
    int i, n = 0;

    if (v == NULL || out == NULL) {
        return 0;
    }
    for (i = 0; i < v->count && n < max; i++) {
        const char *ty = v->poses[i].model.type;
        int k, seen = 0;

        if (ty[0] == '\0' || tiku_model_is_container(&v->poses[i].model)) {
            /* A folder's type is the folder type, which says nothing about
             * what the listing holds. */
            continue;
        }
        for (k = 0; k < n; k++) {
            if (strcmp(out[k], ty) == 0) {
                seen = 1;
                break;
            }
        }
        if (!seen) {
            out[n++] = ty;
        }
    }
    return n;
}

int
tiku_trk_poseview_has_column(const tiku_trk_poseview_t *v, int field)
{
    int i;

    for (i = 0; v != NULL && i < v->column_count; i++) {
        if (v->columns[i].field == field) {
            return 1;
        }
    }
    return 0;
}

int
tiku_trk_poseview_copy_layout(const tiku_trk_poseview_t *v)
{
    int i;

    if (v == NULL || v->column_count <= 0) {
        return 0;
    }
    int serial = pv_g_layout_clip.serial;

    memset(&pv_g_layout_clip, 0, sizeof pv_g_layout_clip);
    pv_g_layout_clip.serial = serial;
    for (i = 0; i < v->column_count && i < TIKU_TRK_COLUMNS_MAX; i++) {
        const tiku_trk_column_t *src = &v->columns[i];

        pv_g_layout_clip.column[i].field = src->field;
        pv_g_layout_clip.column[i].width = src->width;
        pv_g_layout_clip.column[i].alignment = src->alignment;
        snprintf(pv_g_layout_clip.column[i].title,
                 sizeof pv_g_layout_clip.column[i].title, "%s",
                 src->title != NULL ? src->title : "");
        pv_g_layout_clip.count++;
    }
    pv_g_layout_clip.sort_field = v->sort_field;
    pv_g_layout_clip.sort_secondary = v->sort2_field;
    pv_g_layout_clip.reverse = v->sort_reverse;
    pv_g_layout_clip.serial++;
    pv_g_layout_clip_valid = 1;
    return pv_g_layout_clip.count;
}

int
tiku_trk_poseview_paste_layout(tiku_trk_poseview_t *v)
{
    tiku_trk_column_t next[TIKU_TRK_COLUMNS_MAX];
    int i, n = 0, name = 0;

    if (v == NULL || !pv_g_layout_clip_valid || pv_g_layout_clip.count <= 0) {
        return 0;
    }
    memset(next, 0, sizeof next);
    for (i = 0; i < pv_g_layout_clip.count && n < TIKU_TRK_COLUMNS_MAX; i++) {
        const tiku_trk_layout_column_t *src = &pv_g_layout_clip.column[i];

        /* The name column is required; the other built-ins are always
         * known.  An attribute column is taken only when this view has
         * registered it already. */
        if (src->field >= TIKU_TRK_COL_ATTR_BASE &&
            !tiku_trk_poseview_attr_of(v, src->field)) {
            continue;
        }
        if (src->field == TIKU_TRK_COL_NAME) {
            name = 1;
        }
        next[n].field = src->field;
        next[n].width = src->width < TIKU_TRK_COL_MIN_WIDTH
                            ? TIKU_TRK_COL_MIN_WIDTH : src->width;
        next[n].alignment = src->alignment;
        {
            const char *title = NULL;
            int ignored_width = next[n].width;

            pv_column_def_v(v, src->field, &title, &ignored_width);
            /* Built-in titles are static and attribute titles are owned by
             * this view, so the pointer stays valid as long as the view. */
            next[n].title = title;
        }
        n++;
    }
    if (!name) {
        return 0;
    }
    memcpy(v->columns, next, (size_t)n * sizeof next[0]);
    v->column_count = n;
    tiku_trk_poseview_reflow_columns(v);
    v->sort_field = tiku_trk_poseview_has_column(v, pv_g_layout_clip.sort_field)
                        ? pv_g_layout_clip.sort_field : TIKU_TRK_COL_NAME;
    v->sort2_field =
        tiku_trk_poseview_has_column(v, pv_g_layout_clip.sort_secondary)
            ? pv_g_layout_clip.sort_secondary : -1;
    if (v->sort2_field == v->sort_field) {
        v->sort2_field = -1;
    }
    v->sort_reverse = pv_g_layout_clip.reverse != 0;
    pv_resort(v);
    if (v->filtering) {
        pv_rebuild_filter(v);
    }
    v->columns_dirty = 1;
    v->state_dirty = 1;
    return n;
}

int
tiku_trk_poseview_has_layout_clipboard(void)
{
    return pv_g_layout_clip_valid;
}

const tiku_trk_layout_clip_t *
tiku_trk_poseview_layout_clipboard(void)
{
    return pv_g_layout_clip_valid ? &pv_g_layout_clip : NULL;
}

int
tiku_trk_poseview_attr_column(tiku_trk_poseview_t *v, const char *attr,
                              const char *title, int width)
{
    int i;

    if (v == NULL || attr == NULL || attr[0] == '\0') {
        return -1;
    }
    for (i = 0; i < v->attrcol_count; i++) {
        if (strcmp(v->attrcol[i].name, attr) == 0) {
            return TIKU_TRK_COL_ATTR_BASE + i;   /* already has an id */
        }
    }
    if (v->attrcol_count >= TIKU_TRK_COL_ATTR_MAX) {
        return -1;
    }
    i = v->attrcol_count++;
    snprintf(v->attrcol[i].name, sizeof v->attrcol[i].name, "%s", attr);
    snprintf(v->attrcol[i].title, sizeof v->attrcol[i].title, "%s",
             (title != NULL && title[0] != '\0') ? title : attr);
    v->attrcol[i].width = (width > TIKU_TRK_COL_MIN_WIDTH)
                              ? width : TIKU_TRK_COL_MIN_WIDTH;
    return TIKU_TRK_COL_ATTR_BASE + i;
}

const char *
tiku_trk_poseview_attr_of(const tiku_trk_poseview_t *v, int field)
{
    int at = field - TIKU_TRK_COL_ATTR_BASE;

    if (v == NULL || field < TIKU_TRK_COL_ATTR_BASE ||
        at >= v->attrcol_count) {
        return NULL;
    }
    return v->attrcol[at].name;
}

int
tiku_trk_poseview_last_column(const tiku_trk_poseview_t *v, int field)
{
    (void)v;
    return (field == TIKU_TRK_COL_NAME) ? 1 : 0;
}

int
tiku_trk_poseview_toggle_column(tiku_trk_poseview_t *v, int field)
{
    int i;

    if (v == NULL || field == TIKU_TRK_COL_NAME) {
        return 0;              /* the name column is not optional          */
    }
    for (i = 0; i < v->column_count; i++) {
        if (v->columns[i].field != field) {
            continue;
        }
        for (; i < v->column_count - 1; i++) {
            v->columns[i] = v->columns[i + 1];
        }
        v->column_count--;
        tiku_trk_poseview_reflow_columns(v);
        /* A sort on the removed column falls to the column leading the
         * strip. */
        if (v->sort_field == field) {
            v->sort_field = pv_sort_fallback(v);
            v->sort_reverse = 0;
            pv_resort(v);
        }
        if (v->sort2_field == field) {
            v->sort2_field = -1;    /* TS-004: no column, no tie-break     */
        }
        /* PVL-081: the filter matches column text, so a column that has
         * gone can only take rows away; the list is thinned in place. */
        pv_filter_changed(v, 1);
        v->columns_dirty = 1;
    v->state_dirty = 1;
        return 1;
    }
    if (v->column_count >= TIKU_TRK_COLUMNS_MAX) {
        return 0;
    }
    pv_column_def_v(v, field, &v->columns[v->column_count].title,
                 &v->columns[v->column_count].width);
    v->columns[v->column_count].field = field;
    v->columns[v->column_count].alignment = column_alignment(field);
    v->column_count++;
    tiku_trk_poseview_reflow_columns(v);
    /* PVL-081: a new column can only bring rows back, and nothing short of
     * a rebuild can find a row that is not in the filtered list. */
    pv_filter_changed(v, 0);
    v->columns_dirty = 1;
    v->state_dirty = 1;
    return 1;
}
