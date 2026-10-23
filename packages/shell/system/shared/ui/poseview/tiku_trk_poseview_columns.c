/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_poseview_columns.c - the list-mode columns: their widths and
 * order, saving and restoring them per folder or query, and the horizontal
 * and row scroll.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_trk_poseview_private.h"

/* Used before they are defined. */
static int column_index(const tiku_trk_poseview_t *v, int field);
/* End of the forward declarations. */

void
tiku_trk_poseview_set_font_size(tiku_trk_poseview_t *v, int px)
{
    if (v != NULL && px > 0) {
        v->font_px = px;
    }
}

/** @brief @p v with its bytes in the other order. */
static uint32_t
swap32u(uint32_t v)
{
    return ((v & 0xFFu) << 24) | ((v & 0xFF00u) << 8) |
           ((v >> 8) & 0xFF00u) | ((v >> 24) & 0xFFu);
}

/** @brief @p v with its bytes in the other order, signed. */
static int32_t
swap32(int32_t v)
{
    return (int32_t)swap32u((uint32_t)v);
}

/** @brief A stored width in pixels at the view's font, rounded up so a
 *         column does not lose a pixel. */
static int
scale_in(const tiku_trk_poseview_t *v, int32_t stored)
{
    long n = (long)stored * (long)v->font_px;

    return (int)((n + TIKU_TRK_FONT_REF - 1) / TIKU_TRK_FONT_REF);
}

/** @brief Pixels into stored units, rounded down so repeated round trips
 *         do not creep. */
static int32_t
scale_out(const tiku_trk_poseview_t *v, int px)
{
    return (int32_t)(((long)px * TIKU_TRK_FONT_REF) / (long)v->font_px);
}

int
tiku_trk_poseview_save_columns(tiku_trk_poseview_t *v)
{
    tiku_store_t *store;
    column_rec_t rec[TIKU_TRK_COLUMNS_MAX];
    int i;

    if (v == NULL) {
        return 0;
    }
    if (!v->columns_dirty) {
        /* Nothing about the columns changed since the last save, so
         * nothing is written (TS-036). */
        return 0;
    }
    store = (v->query_layout[0] != '\0' && v->layout_store != NULL)
          ? v->layout_store
          : ((v->backend->ops->state_store != NULL)
             ? v->backend->ops->state_store(v->backend) : NULL);
    if (store == NULL || !tiku_store_writable(store)) {
        return 0;
    }
    for (i = 0; i < v->column_count; i++) {
        memset(&rec[i], 0, sizeof rec[i]);
        rec[i].key = COLUMN_REC_KEY;
        rec[i].version = COLUMN_REC_VERSION;
        rec[i].field = (int32_t)v->columns[i].field;
        rec[i].offset = scale_out(v, v->columns[i].offset);
        rec[i].width = scale_out(v, v->columns[i].width);
        rec[i].alignment = (int32_t)v->columns[i].alignment;
        {
            const char *a = tiku_trk_poseview_attr_of(v,
                                                      v->columns[i].field);

            if (a != NULL) {
                snprintf(rec[i].attr, sizeof rec[i].attr, "%s", a);
            }
        }
    }
    {
        char node[TIKU_PATH_MAX];
        const char *at = (v->query_layout[0] == '\0' &&
                          strcmp(v->path, "/") == 0)
                             ? TIKU_ATTR_DISKS_COLUMNS
                             : (v->state_key[0] && !v->query_layout[0])
                                 ? TIKU_ATTR_DESK_COLUMNS : TIKU_ATTR_COLUMNS;
        const char *nd = (v->query_layout[0] != '\0')
                             ? v->query_layout
                             : pv_state_node(v, node, sizeof node);

        if (tiku_state_write(store, nd, at, rec,
                sizeof rec[0] * (size_t)v->column_count) != 0) {
            return 0;
        }
    }
    v->columns_dirty = 0;
    return v->column_count;
}

/** @brief Order restored columns by their saved offset. */
static int
rec_cmp(const void *a, const void *b)
{
    const column_rec_t *x = a, *y = b;

    return (x->offset < y->offset) ? -1 : ((x->offset > y->offset) ? 1 : 0);
}

void
tiku_trk_poseview_copy_columns(tiku_trk_poseview_t *v,
                               const tiku_trk_poseview_t *from)
{
    int i;

    if (v == NULL || from == NULL || from->column_count == 0) {
        return;
    }
    v->column_count = from->column_count;
    for (i = 0; i < from->column_count; i++) {
        v->columns[i] = from->columns[i];
    }
    tiku_trk_poseview_reflow_columns(v);
}

int
tiku_trk_poseview_restore_columns(tiku_trk_poseview_t *v)
{
    tiku_store_t *store;
    column_rec_t rec[TIKU_TRK_COLUMNS_MAX];
    int n, kept = 0, i, j, order = -1;
    size_t got = 0;

    if (v == NULL) {
        return 0;
    }
    store = (v->query_layout[0] != '\0' && v->layout_store != NULL)
          ? v->layout_store
          : ((v->backend->ops->state_store != NULL)
             ? v->backend->ops->state_store(v->backend) : NULL);
    if (store == NULL) {
        return 0;
    }
    {
        char node[TIKU_PATH_MAX];
        const char *at = (v->query_layout[0] == '\0' &&
                          strcmp(v->path, "/") == 0)
                             ? TIKU_ATTR_DISKS_COLUMNS
                             : (v->state_key[0] && !v->query_layout[0])
                                 ? TIKU_ATTR_DESK_COLUMNS : TIKU_ATTR_COLUMNS;
        const char *nd = (v->query_layout[0] != '\0')
                             ? v->query_layout
                             : pv_state_node(v, node, sizeof node);
        int r = tiku_state_read(store, nd, at, rec, sizeof rec);
        if (r <= 0) {
            return 0;
        }
        got = (size_t)r;
    }
    /* A record that is not a whole number of columns, such as a truncated
     * write, is refused. */
    if (got % sizeof rec[0] != 0u) {
        return 0;
    }
    n = (int)(got / sizeof rec[0]);
    if (n > TIKU_TRK_COLUMNS_MAX) {
        return 0;
    }
    for (i = 0; i < n; i++) {
        int swapped = 0;

        if (rec[i].key == swap32u(COLUMN_REC_KEY)) {
            swapped = 1;
            /* Written by a host of the other byte order.  Every scalar in
             * the record is turned, including the key that said so. */
            rec[i].key = swap32u(rec[i].key);
            rec[i].version = swap32(rec[i].version);
            rec[i].field = swap32(rec[i].field);
            rec[i].offset = swap32(rec[i].offset);
            rec[i].width = swap32(rec[i].width);
            rec[i].alignment = swap32(rec[i].alignment);
        }
        if (rec[i].attr[0] != '\0') {
            /* Registered again under this session's numbering, so a layout
             * saved with three attribute columns comes back with the same
             * three whatever order they are registered in now. */
            const char *ttl = NULL;
            int w = 0;

            pv_column_def_v(v, rec[i].field, &ttl, &w);
            rec[i].field = (int32_t)tiku_trk_poseview_attr_column(v,
                               rec[i].attr, rec[i].attr,
                               (int)rec[i].width);
        }
        if (order < 0) {
            order = swapped;
        } else if (order != swapped) {
            /* Records disagreeing about their byte order are refused. */
            return 0;
        }
        if (rec[i].key != COLUMN_REC_KEY ||
            rec[i].version != COLUMN_REC_VERSION) {
            return 0;   /* not a column record, or another version */
        }
        /* TS-032: a value out of range rejects the whole layout; nothing
         * is clamped.  Widths and offsets are bounded after scaling, in the
         * pixels a column will have. */
        if (scale_in(v, rec[i].width) < 0 ||
            scale_in(v, rec[i].width) > 10000 ||
            scale_in(v, rec[i].offset) < 0 ||
            scale_in(v, rec[i].offset) > 10000 ||
            rec[i].alignment < TIKU_TRK_ALIGN_LEFT ||
            rec[i].alignment > TIKU_TRK_ALIGN_CENTER ||
            rec[i].field < 0 ||
            (rec[i].field > TIKU_TRK_COL_ORIGINAL &&
             (rec[i].field < TIKU_TRK_COL_ATTR_BASE ||
              rec[i].field >= TIKU_TRK_COL_ATTR_BASE +
                              TIKU_TRK_COL_ATTR_MAX))) {
            /* A field outside the fixed set is legal only as an attribute
             * column's slot; a record carrying a name was mapped into this
             * session's numbering above. */
            return 0;
        }
        for (j = 0; j < i; j++) {
            /* Two records for one field are refused: the listing would show
             * that column twice, and a doubled name column could not be
             * removed. */
            if (rec[j].field == rec[i].field) {
                return 0;
            }
        }
        if (rec[i].field == TIKU_TRK_COL_NAME) {
            kept = 1;               /* the name column must be in there    */
        }
    }
    if (!kept) {
        return 0;
    }
    /* PVL-033: the saved offsets only sort the records; positions are
     * re-derived from the widths. */
    qsort(rec, (size_t)n, sizeof rec[0], rec_cmp);
    for (i = 0; i < n; i++) {
        const char *title;
        int def_w;

        pv_column_def_v(v, (int)rec[i].field, &title, &def_w);
        v->columns[i].title = title;
        v->columns[i].field = (int)rec[i].field;
        v->columns[i].width = scale_in(v, rec[i].width);
        if (v->columns[i].width < TIKU_TRK_COL_MIN_WIDTH) {
            v->columns[i].width = TIKU_TRK_COL_MIN_WIDTH;
        }
        v->columns[i].alignment = (int)rec[i].alignment;
    }
    v->column_count = n;
    tiku_trk_poseview_reflow_columns(v);
    /* A sort key whose column did not come back falls to the column
     * leading the strip (MA-067). */
    if (column_index(v, v->sort_field) < 0) {
        v->sort_field = pv_sort_fallback(v);
        v->sort_reverse = 0;
    }
    if (v->sort2_field >= 0 && column_index(v, v->sort2_field) < 0) {
        v->sort2_field = -1;
    }
    if (v->sort2_field == v->sort_field) {
        /* The repair above can collapse the two onto one column; a listing
         * whose tie-break is its own key has no tie-break. */
        v->sort2_field = -1;
    }
    pv_resort(v);
    return n;
}

int
tiku_trk_poseview_install_query_defaults(tiku_backend_t *backend,
                                          tiku_store_t *store)
{
    static const char *const kType[] = {
        "", "application/x-vnd.Be-bookmark", "application/x-person",
        "text/x-email"
    };
    tiku_trk_frame_t def = { 40, 40, 695, 350 };
    int i, installed = 0;

    if (backend == NULL || store == NULL || !tiku_store_writable(store)) {
        return 0;
    }
    for (i = 0; i < (int)(sizeof kType / sizeof kType[0]); i++) {
        tiku_trk_poseview_t *v = tiku_trk_poseview_new(backend,
                                                       "/query-template");
        tiku_trk_frame_t frame;
        unsigned char byte;
        int has_columns, has_frame;

        if (v == NULL) {
            continue;
        }
        tiku_trk_poseview_set_layout_store(v, store);
        tiku_trk_poseview_set_query_type(v, kType[i]);
        has_columns = tiku_state_read(store,
                            tiku_trk_poseview_layout_path(v),
                            TIKU_ATTR_COLUMNS, &byte, sizeof byte) > 0;
        has_frame = tiku_state_read(store,
                          tiku_trk_poseview_layout_path(v),
                          TIKU_ATTR_FRAME, &frame, sizeof frame) ==
                    (int)sizeof frame &&
                    frame.w >= TIKU_TRK_FRAME_MIN_W &&
                    frame.h >= TIKU_TRK_FRAME_MIN_H;
        if (!has_columns) {
            (void)tiku_trk_poseview_set_results(v, NULL, 0);
            /* The install is the change being recorded, so the columns
             * are marked changed for the save (TS-036). */
            v->columns_dirty = 1;
            (void)tiku_trk_poseview_save_columns(v);
        }
        if (!has_frame) {
            (void)tiku_state_write(store,
                                       tiku_trk_poseview_layout_path(v),
                                       TIKU_ATTR_FRAME, &def,
                                       sizeof def);
        }
        if (!has_columns || !has_frame) {
            installed++;
        }
        tiku_trk_poseview_free(v);
    }
    return installed;
}

int
tiku_trk_poseview_start_offset(const tiku_trk_poseview_t *v)
{
    /* The row's icon owns the strip to the left of the first column, so the
     * first column starts past it: the mini icon, the placement gap, and
     * one pixel. */
    (void)v;
    return TIKU_TRK_MINI_ICON + TIKU_TRK_PLACE_GAP + 1;
}

void
tiku_trk_poseview_reflow_columns(tiku_trk_poseview_t *v)
{
    int i, at;

    if (v == NULL) {
        return;
    }
    at = tiku_trk_poseview_start_offset(v);
    for (i = 0; i < v->column_count; i++) {
        v->columns[i].offset = at;
        at += v->columns[i].width + TIKU_TRK_COL_MARGIN;
    }
}

/** @brief Index of the column showing @p field, or -1. */
static int
column_index(const tiku_trk_poseview_t *v, int field)
{
    int i;

    for (i = 0; i < v->column_count; i++) {
        if (v->columns[i].field == field) {
            return i;
        }
    }
    return -1;
}

int
tiku_trk_poseview_resize_column(tiku_trk_poseview_t *v, int field, int width)
{
    int i;

    if (v == NULL) {
        return 0;
    }
    i = column_index(v, field);
    if (i < 0) {
        return 0;
    }
    if (width < TIKU_TRK_COL_MIN_WIDTH) {
        width = TIKU_TRK_COL_MIN_WIDTH;
    }
    if (v->columns[i].width == width) {
        return 0;
    }
    v->columns[i].width = width;
    tiku_trk_poseview_reflow_columns(v);
    v->columns_dirty = 1;
    v->state_dirty = 1;
    return 1;
}

int
tiku_trk_poseview_move_column_to(tiku_trk_poseview_t *v, int field, int to)
{
    int from;
    tiku_trk_column_t moved;

    if (v == NULL) {
        return 0;
    }
    from = column_index(v, field);
    if (to >= v->column_count) {
        to = v->column_count - 1;
    }
    if (from < 0 || to < 0 || from == to) {
        return 0;
    }
    moved = v->columns[from];
    if (from < to) {
        int k;

        for (k = from; k < to; k++) {
            v->columns[k] = v->columns[k + 1];
        }
    } else {
        int k;

        for (k = from; k > to; k--) {
            v->columns[k] = v->columns[k - 1];
        }
    }
    v->columns[to] = moved;
    tiku_trk_poseview_reflow_columns(v);
    v->columns_dirty = 1;
    v->state_dirty = 1;
    return 1;
}

int
tiku_trk_poseview_column_after(tiku_trk_poseview_t *v, int field, int after)
{
    int at;

    if (v == NULL) {
        return 0;
    }
    at = column_index(v, after);
    if (at < 0) {
        return 0;
    }
    int from = column_index(v, field);

    if (from < 0 || from == at) {
        return 0;
    }
    return tiku_trk_poseview_move_column_to(v, field, at + (from > at));
}

int
tiku_trk_poseview_move_column(tiku_trk_poseview_t *v, int field, int before)
{
    int from, to;
    tiku_trk_column_t moved;

    if (v == NULL) {
        return 0;
    }
    from = column_index(v, field);
    to = column_index(v, before);
    /* The destination index is read before the source is lifted out:
     * taken afterwards, a rightward move would land one short of where the
     * user dropped it and the column would never pass its neighbour. */
    if (from < 0 || to < 0 || from == to) {
        return 0;
    }
    moved = v->columns[from];
    if (from < to) {
        int k;

        for (k = from; k < to; k++) {
            v->columns[k] = v->columns[k + 1];
        }
    } else {
        int k;

        for (k = from; k > to; k--) {
            v->columns[k] = v->columns[k - 1];
        }
    }
    v->columns[to] = moved;
    tiku_trk_poseview_reflow_columns(v);
    v->columns_dirty = 1;
    v->state_dirty = 1;
    return 1;
}

int
tiku_trk_poseview_column_widest(tiku_trk_poseview_t *v, int field)
{
    int i, n, best = TIKU_TRK_COL_MIN_WIDTH;

    if (v == NULL || v->measure == NULL) {
        return 0;
    }
    /* Measured over the list being shown, so a filtered-out row does not
     * widen the column. */
    n = tiku_trk_poseview_count(v);
    for (i = 0; i < n; i++) {
        tiku_trk_pose_t *p = tiku_trk_poseview_at(v, i);
        char buf[160];
        const char *text;
        int w;

        if (p == NULL) {
            continue;
        }
        /* Measured at an unbounded width: at the column's current width a
         * date or a size is already shortened to fit it. */
        text = pv_cell_text_at(v, p, field, 100000, buf, sizeof buf);
        if (text == NULL || text[0] == '\0') {
            continue;
        }
        w = v->measure(text, v->measure_ctx);
        if (w > best) {
            best = w;
        }
    }
    return best;
}

int
tiku_trk_poseview_fit_column(tiku_trk_poseview_t *v, int field)
{
    int want, i;

    if (v == NULL) {
        return 0;
    }
    i = column_index(v, field);
    if (i < 0) {
        return 0;
    }
    want = tiku_trk_poseview_column_widest(v, field);
    if (want <= 0) {
        return 0;
    }
    /* Nothing to fit: the widest value is at the floor and the column is no
     * wider.  Returning 0 lets the caller start a drag with the click. */
    if (!(want > TIKU_TRK_COL_MIN_WIDTH || want < v->columns[i].width)) {
        return 0;
    }
    /* A fit consumes the double-click even when the width does not
     * change; otherwise its second click would arm a resize on the column
     * just fitted. */
    (void)tiku_trk_poseview_resize_column(v, field, want);
    return 1;
}

int
tiku_trk_poseview_hscroll(const tiku_trk_poseview_t *v)
{
    return (v != NULL) ? v->hscroll : 0;
}

void
tiku_trk_poseview_set_hscroll(tiku_trk_poseview_t *v, int px)
{
    int max;

    if (v == NULL) {
        return;
    }
    max = tiku_trk_poseview_columns_width(v);
    if (v->view_w > 0) {
        /* Scrolling ends where the last column meets the window's right
         * edge (PVL-058). */
        max -= v->view_w;
        if (max < 0) {
            max = 0;
        }
    }
    if (px > max) { px = max; }
    if (px < 0)   { px = 0; }
    v->hscroll = px;
}

int
tiku_trk_poseview_columns_width(const tiku_trk_poseview_t *v)
{
    int i, w;

    if (v == NULL || v->column_count == 0) {
        return 0;
    }
    w = tiku_trk_poseview_start_offset(v);
    for (i = 0; i < v->column_count; i++) {
        w += v->columns[i].width + TIKU_TRK_COL_MARGIN;
    }
    return w;
}

int
tiku_trk_poseview_columns(const tiku_trk_poseview_t *v,
                          const tiku_trk_column_t **out)
{
    if (v == NULL) {
        return 0;
    }
    if (out != NULL) {
        *out = v->columns;
    }
    return v->column_count;
}

int
tiku_trk_poseview_scroll(const tiku_trk_poseview_t *v)
{
    return (v != NULL) ? v->scroll : 0;
}

void
tiku_trk_poseview_scroll_to(tiku_trk_poseview_t *v, int row)
{
    int n;

    if (v == NULL) {
        return;
    }
    n = tiku_trk_poseview_count(v);
    if (row < 0) { row = 0; }
    if (row > n - 1) { row = (n > 0) ? n - 1 : 0; }
    v->scroll = row;
}

void
tiku_trk_poseview_scroll_into_view(tiku_trk_poseview_t *v, int index,
                                   int visible_rows)
{
    if (v == NULL || visible_rows <= 0 || v->no_autoscroll) {
        return;
    }
    if (index < v->scroll) {
        v->scroll = index;
    } else if (index >= v->scroll + visible_rows) {
        v->scroll = index - visible_rows + 1;
    }
    if (v->scroll < 0) {
        v->scroll = 0;
    }
}
