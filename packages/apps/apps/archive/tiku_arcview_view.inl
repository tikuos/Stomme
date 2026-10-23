/* tiku_arcview_view.inl -- part of tiku_arcview.c: the window's layout,
 * painting and pointer input: a toolbar of icon buttons and the find
 * field, a path bar, the folder's rows under column headers, and the
 * status line, with a gauge and Stop while a job runs.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */

#define AV_SIZE_W   86              /* the Size and Packed columns, each */
#define AV_DATE_W   136             /* Modified                          */
#define AV_FIND_W   170

/** @brief A toolbar button's icon, from the desk's set. */
static const char *av_tool_icon(int id)
{
    switch (id) {
    case AV_OPEN: return "folder_open";
    case AV_UP: return "page_up";
    case AV_EXTRACT_ALL: return "extract_all";
    case AV_EXTRACT: return "extract_some";
    case AV_SHOW: return "folder";
    }
    return "file";
}

/** @brief What a toolbar button does, as its tip says it. */
static const char *av_tool_tip(int id)
{
    switch (id) {
    case AV_OPEN: return "Open (Ctrl+O)";
    case AV_UP: return "Enclosing Folder (Ctrl+Up)";
    case AV_EXTRACT_ALL: return "Extract All (Ctrl+E)";
    case AV_EXTRACT: return "Extract Selected (Shift+Ctrl+E)";
    case AV_SHOW: return "Show Extracted in Tracker";
    }
    return "";
}

/** @brief A toolbar button's side: square, the button height but at
 *         least 30 pixels. */
static int av_tool_size(void)
{
    int bh = tiku_ui_button_h();

    return bh > 30 ? bh : 30;
}

/** @brief The toolbar: Open and Up, the two extracts, Show; the find
 *         field at the right.  @return the row below it. */
static int av_tools_layout(tiku_arcview_t *c, tiku_rect_t r, av_layout_t *l)
{
    static const int ids[] = { AV_OPEN, AV_UP, AV_EXTRACT_ALL, AV_EXTRACT,
                               AV_SHOW };
    int bw = av_tool_size(), bh = tiku_ui_button_h(), x = r.x + 6;
    int y = r.y + 4, fw;

    for (size_t i = 0; i < sizeof ids / sizeof ids[0]; i++) {
        if (ids[i] == AV_UP || ids[i] == AV_EXTRACT_ALL ||
            ids[i] == AV_SHOW) x += 10;
        if (x + bw > r.x + r.w - 6) { x = r.x + 6; y += bw + 4; }
        (void)tiku_dialog_add(&c->dialog, ids[i], (tiku_rect_t){x, y, bw, bw},
                              TIKU_DIALOG_BUTTON, av_enabled(c, ids[i]));
        x += bw + 2;
    }
    fw = r.x + r.w - 6 - x - 10;
    if (fw < 100) { x = r.x + 6; y += bw + 4; fw = r.w - 12; }
    if (fw > AV_FIND_W) fw = AV_FIND_W;
    if (fw < 1) fw = 1;
    l->find = (tiku_rect_t){r.x + r.w - 6 - fw, y + (bw - bh) / 2, fw, bh};
    (void)tiku_dialog_add(&c->dialog, AV_FIND_FIELD, l->find,
                          TIKU_DIALOG_INPUT, c->node != NULL);
    return y + bw + 4;
}

/** @brief Where everything goes in content @p r. */
static av_layout_t av_layout(tiku_arcview_t *c, tiku_rect_t r)
{
    av_layout_t l;
    int fh = tiku_font_plain()->height + 6, below, bh = tiku_ui_button_h();

    memset(&l, 0, sizeof l);
    tiku_dialog_begin(&c->dialog, -1, -1);
    below = av_tools_layout(c, r, &l);
    l.tools = (tiku_rect_t){r.x, r.y, r.w, below - r.y};
    l.crumbs = (tiku_rect_t){r.x + 8, below, r.w - 16, fh};
    l.head = (tiku_rect_t){r.x + 4, below + fh + 2, r.w - 8 - AV_BAR, fh};
    l.status = (tiku_rect_t){r.x + 8, r.y + r.h - (bh > fh ? bh : fh) - 3,
                             r.w - 16, bh > fh ? bh : fh};
    l.body = (tiku_rect_t){l.head.x, l.head.y + l.head.h, l.head.w,
                           l.status.y - 3 - (l.head.y + l.head.h)};
    if (l.body.h < 1) l.body.h = 1;
    l.vbar = (tiku_rect_t){l.body.x + l.body.w, l.head.y, AV_BAR,
                           l.body.h + l.head.h};
    (void)tiku_dialog_add(&c->dialog, AV_LIST, l.body, TIKU_DIALOG_INPUT,
                          c->node != NULL);
    if (c->progress) {
        int cw = tiku_ui_button_w("Stop", 60);

        l.cancel = (tiku_rect_t){l.status.x + l.status.w - cw, l.status.y,
                                 cw, bh};
        int gw = l.status.w - cw - 8;
        if (gw > 168) gw = 168;
        if (gw < 1) gw = 1;
        l.gauge = (tiku_rect_t){l.cancel.x - gw - 8, l.status.y +
                                (bh - 12) / 2, gw, 12};
        (void)tiku_dialog_add(&c->dialog, AV_CANCEL, l.cancel,
                              TIKU_DIALOG_BUTTON, 1);
    }
    tiku_dialog_end(&c->dialog);
    if (c->dialog.focus == TIKU_DIALOG_NONE && c->node)
        (void)tiku_dialog_focus(&c->dialog, AV_LIST);
    tiku_scroll_set_range(&c->vbar, c->nrows,
                          tiku_list_visible_rows(&c->list, l.body));
    tiku_scroll_to(&c->vbar, c->list.top);
    return l;
}

/* -------------------------------------------------------------- paint  */

/** @brief A toolbar button as the desk's toolbars draw one: its icon,
 *         raised under the pointer, sunk while pressed, dimmed when it can
 *         do nothing; Stop is a worded button in the status line. */
static void av_button(tiku_arcview_t *c, tiku_surface_t *s,
                      const tiku_dialog_control_t *d)
{
    unsigned flags = d->enabled ? 0 : TIKU_S_DISABLED;
    int held = tiku_dialog_pressed(&c->dialog, d->id), size = d->rect.h - 6;

    if (d->id == AV_CANCEL) {       /* a word in the status line */
        tiku_ui_button(s, d->rect, "Stop", held ? TIKU_S_PRESSED : 0);
        return;
    }
    if (tiku_dialog_focused(&c->dialog, d->id)) flags |= TIKU_S_FOCUS;
    if (held) flags |= TIKU_S_PRESSED;
    if (c->hover_tool == d->id) flags |= TIKU_S_HOT;
    /* The label is for narration and is not drawn; the icon is the face. */
    tiku_ui_button(s, d->rect, av_label(c, d->id), flags | TIKU_S_ICON);
    if (size > 24) size = 24;
    (void)tiku_icon_paint_dim(s, av_tool_icon(d->id),
                              d->rect.x + (d->rect.w - size) / 2 + held,
                              d->rect.y + (d->rect.h - size) / 2 + held, size,
                              d->enabled ? 0.0f : 0.72f, TIKU_C_PANEL);
}

/** @brief The tip of the toolbar button under the pointer, below it. */
static void av_tool_tip_draw(tiku_arcview_t *c, tiku_surface_t *s,
                             tiku_rect_t r)
{
    for (int i = 0; i < c->dialog.count && c->hover_tool; i++) {
        const tiku_dialog_control_t *d = &c->dialog.control[i];
        const char *text = av_tool_tip(d->id);
        tiku_rect_t tip;

        if (d->id != c->hover_tool || d->kind != TIKU_DIALOG_BUTTON ||
            !*text) continue;
        tip = tiku_ui_tip_size(text);
        tip.x = d->rect.x;
        tip.y = d->rect.y + d->rect.h + 4;
        if (tip.x + tip.w > r.x + r.w - 4) tip.x = r.x + r.w - 4 - tip.w;
        tiku_ui_tip(s, tip, text);
    }
}

/** @brief The toolbar button under the pointer, lit and tipped; none once
 *         something is pressed or typed, or the window loses the focus. */
static void av_tool_hover(tiku_arcview_t *c, const tiku_event_t *ev)
{
    int tool = 0;

    if (ev->type == TIKU_EVENT_POINTER_MOVE &&
        c->dialog.capture == TIKU_DIALOG_NONE) {
        for (int i = 0; i < c->dialog.count; i++) {
            const tiku_dialog_control_t *d = &c->dialog.control[i];
            if (d->kind == TIKU_DIALOG_BUTTON && d->id != AV_CANCEL &&
                av_in(d->rect, ev->x, ev->y))
                tool = d->id;
        }
    }
    if (tool != c->hover_tool) {
        c->hover_tool = tool;
        av_changed(c);
    }
}

/** @brief The folders from the root to the one shown, each a crumb:
 *         their nodes into @p nodes and places into @p at, at most
 *         @p max.  @return how many. */
static int av_crumbs(const tiku_arcview_t *c, tiku_rect_t r, int *nodes,
                     tiku_rect_t *at, int max)
{
    int chain[64], n = 0, x = r.x, sep = tiku_measure_width(
        TIKU_MEASURE_PLAIN, " > ");

    for (int i = c->cwd; i >= 0 && n < 64; i = c->node[i].parent)
        chain[n++] = i;
    if (n > max) n = max;
    for (int k = 0; k < n; k++) {
        int node = chain[n - 1 - k];
        int w = tiku_measure_width(TIKU_MEASURE_PLAIN, c->node[node].name);

        nodes[k] = node;
        at[k] = (tiku_rect_t){x, r.y, w, r.h};
        x += w + sep;
    }
    return n;
}

/** @brief The path bar: the archive, then each folder down to the one
 *         shown; the last in plain ink, the others faint. */
static void av_crumbs_draw(tiku_arcview_t *c, tiku_surface_t *s,
                           tiku_rect_t r)
{
    int nodes[32], n;
    tiku_rect_t at[32];

    if (!c->node) return;
    n = av_crumbs(c, r, nodes, at, 32);
    for (int k = 0; k < n; k++) {
        tiku_text_line_draw(s, at[k], TIKU_MEASURE_PLAIN,
                            c->node[nodes[k]].name, k == n - 1
                            ? TIKU_C_TEXT : tiku_ink(TIKU_INK_FAINT));
        if (k < n - 1)
            tiku_text_line_draw(s, (tiku_rect_t){at[k].x + at[k].w, r.y,
                                at[k + 1].x - at[k].x - at[k].w, r.h},
                                TIKU_MEASURE_PLAIN, " > ",
                                tiku_ink(TIKU_INK_FAINT));
    }
}

/** @brief How many columns fit in @p body: Name, then Size, Packed and
 *         Modified as the width allows. */
static int av_columns(tiku_rect_t body)
{
    if (body.w >= 160 + 2 * AV_SIZE_W + AV_DATE_W) return 4;
    if (body.w >= 160 + 2 * AV_SIZE_W) return 3;
    if (body.w >= 120 + AV_SIZE_W) return 2;
    return 1;
}

/** @brief The Name column's width in @p body: what the other columns
 *         that fit leave. */
static int av_name_w(tiku_rect_t body)
{
    int n = av_columns(body);
    return body.w - (n >= 2 ? AV_SIZE_W : 0) -
           (n >= 3 ? AV_SIZE_W : 0) - (n == 4 ? AV_DATE_W : 0);
}

/** @brief A time as the list shows it, or a dash for none. */
static void av_when(int64_t t, char *out, size_t max)
{
    time_t tt = (time_t)t;
    struct tm tm;

    if (t == INT64_MIN || !localtime_r(&tt, &tm)) {
        snprintf(out, max, "-");    /* the list's face is single-byte */
        return;
    }
    strftime(out, max, "%Y-%m-%d %H:%M", &tm);
}

/** @brief The icon a row's node takes: a folder, a text file, an
 *         archive, or a file, by the name's extension. */
static const char *av_row_icon(const av_node_t *n)
{
    const char *dot = strrchr(n->name, '.');

    if (n->dir) return "folder";
    if (dot && (!strcasecmp(dot, ".txt") || !strcasecmp(dot, ".md") ||
                !strcasecmp(dot, ".c") || !strcasecmp(dot, ".h")))
        return "text_file";
    if (dot && (!strcasecmp(dot, ".zip") || !strcasecmp(dot, ".tgz") ||
                !strcasecmp(dot, ".gz") || !strcasecmp(dot, ".tar")))
        return "archive_file";
    return "file";
}

/** @brief One row: its icon and name, then Size, Packed and Modified;
 *         a refused name in the danger ink, an unreadable or encrypted
 *         one faint. */
static void av_row_draw(tiku_arcview_t *c, tiku_surface_t *s, tiku_rect_t r,
                        int row)
{
    const av_node_t *n = &c->node[c->rows[row]];
    int sel = tiku_list_selected(&c->list, row), nw = av_name_w(r);
    tiku_rgb_t ink = sel ? TIKU_C_SELTEXT : TIKU_C_TEXT;
    char text[TIKU_PATH_MAX + 32], cell[48];

    if (sel) tiku_fill(s, r, TIKU_C_SELECT);
    if (!sel && (n->flags & TIKU_ARC_REFUSED)) ink = TIKU_C_DANGER;
    else if (!sel && (n->flags & (TIKU_ARC_UNREADABLE | TIKU_ARC_ENCRYPTED)))
        ink = tiku_ink(TIKU_INK_FAINT);
    (void)tiku_icon_paint(s, av_row_icon(n), r.x + 4, r.y + (r.h - 16) / 2,
                          16);
    snprintf(text, sizeof text, "%s%s", n->name,
             n->flags & TIKU_ARC_ENCRYPTED ? "  (locked)" : "");
    tiku_text_line_draw(s, (tiku_rect_t){r.x + 26, r.y, nw - 30, r.h},
                        TIKU_MEASURE_PLAIN, text, ink);
    if (av_columns(r) < 2) return;
    av_bytes_fit(n->dir ? -1 : n->size, AV_SIZE_W - 8, cell, sizeof cell);
    tiku_text_line_draw(s, (tiku_rect_t){r.x + nw, r.y, AV_SIZE_W - 8, r.h},
                        TIKU_MEASURE_PLAIN, cell, ink);
    if (av_columns(r) < 3) return;
    av_bytes_fit(n->dir ? -1 : n->packed, AV_SIZE_W - 8, cell, sizeof cell);
    tiku_text_line_draw(s, (tiku_rect_t){r.x + nw + AV_SIZE_W, r.y,
                        AV_SIZE_W - 8, r.h}, TIKU_MEASURE_PLAIN, cell, ink);
    if (av_columns(r) < 4) return;
    av_when(n->mtime, cell, sizeof cell);
    tiku_text_line_draw(s, (tiku_rect_t){r.x + nw + 2 * AV_SIZE_W, r.y,
                        AV_DATE_W - 4, r.h}, TIKU_MEASURE_PLAIN, cell, ink);
}

/** @brief The column headers, the sorted one marked, then the rows seen. */
static void av_list_draw(tiku_arcview_t *c, tiku_surface_t *s,
                         const av_layout_t *l)
{
    static const char *const cols[] = { "Name", "Size", "Packed",
                                        "Modified" };
    int widths[4] = { av_name_w(l->body), AV_SIZE_W, AV_SIZE_W, AV_DATE_W };
    int top = c->list.top, seen = tiku_list_visible_rows(&c->list, l->body);
    tiku_rect_t clip = s->clip;

    int columns = av_columns(l->body);
    tiku_ui_list_header(s, l->head, cols, widths, columns,
                        c->sort < columns ? c->sort : -1);
    tiku_fill(s, l->body, TIKU_C_DOC);
    tiku_clip_set(s, l->body);      /* inside the window's own clip */
    for (int row = top; row < c->nrows && row <= top + seen; row++)
        av_row_draw(c, s, tiku_list_row_rect(&c->list, l->body, row), row);
    tiku_clip_set(s, clip);
    if (c->nrows == 0 && c->node) {
        tiku_text_line_draw(s, (tiku_rect_t){l->body.x + 12, l->body.y + 8,
                            l->body.w - 24, tiku_measure_row_h()},
                            TIKU_MEASURE_PLAIN, c->find.text[0]
                            ? "Nothing here matches." : "This folder is empty.",
                            tiku_ink(TIKU_INK_FAINT));
    }
}

/** @brief The status line: what was said at the left; at the right the
 *         folder's count, or the running job's gauge and Stop. */
static void av_status(tiku_arcview_t *c, tiku_surface_t *s,
                      const av_layout_t *l)
{
    tiku_rect_t r = l->status;
    char where[96] = "";
    int w = 0;

    if (c->progress) {
        long long done = atomic_load(&c->progress->done);
        long long total = atomic_load(&c->progress->total);

        tiku_ui_gauge(s, l->gauge, total > 0 ? (float)done / (float)total
                                             : 0.0f);
        w = r.x + r.w - l->gauge.x + 8;
    } else if (c->node) {
        /* narrowed by Find, or more than the list holds */
        if (c->nrows < c->all_rows)
            snprintf(where, sizeof where, "%d of %d items shown", c->nrows,
                     c->all_rows);
        else
            snprintf(where, sizeof where, "%d item%s", c->nrows,
                     c->nrows == 1 ? "" : "s");
        w = tiku_measure_width(TIKU_MEASURE_PLAIN, where) + 8;
        tiku_text_line_draw(s, (tiku_rect_t){r.x + r.w - w, r.y, w, r.h},
                            TIKU_MEASURE_PLAIN, where, TIKU_C_TEXT);
    }
    tiku_text_line_draw(s, (tiku_rect_t){r.x, r.y, r.w - w - 8, r.h},
                        TIKU_MEASURE_PLAIN, c->hint[0] ? c->hint : c->status,
                        tiku_ink(TIKU_INK_FAINT));
}

/** @brief The whole window, laid out afresh each time. */
static void av_draw(tiku_window_t *w, tiku_surface_t *s, tiku_rect_t r,
                    void *ctx)
{
    tiku_arcview_t *c = ctx;
    tiku_rect_t clip = s->clip;
    av_layout_t l;
    float pos = 0, frac = 1;

    (void)w;
    tiku_clip_set(s, r);
    tiku_ui_panel(s, r);
    l = av_layout(c, r);
    for (int i = 0; i < c->dialog.count; i++) {
        const tiku_dialog_control_t *d = &c->dialog.control[i];
        if (d->kind == TIKU_DIALOG_BUTTON) av_button(c, s, d);
    }
    tiku_ui_textfield_scroll(s, l.find, c->find.text,
                             c->dialog.focus == AV_FIND_FIELD ? c->find.caret
                                                              : -1,
                             c->find.sel_a, c->find.sel_b,
                             c->dialog.focus == AV_FIND_FIELD ? TIKU_S_FOCUS
                                                              : 0,
                             c->find_in.scroll);
    if (!c->find.text[0] && c->dialog.focus != AV_FIND_FIELD)
        tiku_text_line_draw(s, (tiku_rect_t){l.find.x + 6, l.find.y,
                            l.find.w - 12, l.find.h}, TIKU_MEASURE_PLAIN,
                            "Find", tiku_ink(TIKU_INK_FAINT));
    av_crumbs_draw(c, s, l.crumbs);
    if (c->node) av_list_draw(c, s, &l);
    else                            /* no archive: the window stays blank */
        tiku_fill(s, (tiku_rect_t){l.head.x, l.head.y, l.head.w,
                                   l.head.h + l.body.h}, TIKU_C_DOC);
    tiku_frame(s, (tiku_rect_t){l.head.x, l.head.y, l.head.w + AV_BAR,
               l.head.h + l.body.h}, tiku_tint(TIKU_C_PANEL, TIKU_DARKEN_4));
    if (tiku_scroll_max(&c->vbar) > 0) {
        pos = (float)c->vbar.value / (float)tiku_scroll_max(&c->vbar);
        frac = (float)c->vbar.visible / (float)(c->nrows ? c->nrows : 1);
    }
    tiku_ui_scrollbar_held(s, l.vbar, pos, frac, 0, c->vbar.arrow_dir);
    av_status(c, s, &l);
    av_tool_tip_draw(c, s, r);      /* last: nothing paints over it */
    s->clip = clip;
}

/* ------------------------------------------------------------ pointer  */

/** @brief Presses and drags on the scrollbar. */
static int av_bar_pointer(tiku_arcview_t *c, const tiku_event_t *ev,
                          const av_layout_t *l)
{
    int at = ev->y - l->vbar.y;

    if (ev->type == TIKU_EVENT_POINTER_DOWN && ev->button == 1 &&
        av_in(l->vbar, ev->x, ev->y)) {
        c->vdrag = tiku_scroll_press(&c->vbar, l->vbar.h, at);
    } else if (ev->type == TIKU_EVENT_POINTER_MOVE && c->vdrag) {
        (void)tiku_scroll_drag(&c->vbar, l->vbar.h, at);
    } else if (ev->type == TIKU_EVENT_POINTER_UP && c->vdrag) {
        c->vdrag = 0;
        tiku_scroll_release(&c->vbar);
    } else {
        return 0;
    }
    (void)tiku_list_scroll_to(&c->list, c->vbar.value, l->body);
    return 1;
}

/** @brief A press on the column headers: that column sorts, or the
 *         sorted one reverses. */
static int av_head_pointer(tiku_arcview_t *c, const tiku_event_t *ev,
                           const av_layout_t *l)
{
    int x = ev->x - l->head.x, col, nw = av_name_w(l->body);

    if (ev->type != TIKU_EVENT_POINTER_DOWN || !av_in(l->head, ev->x, ev->y))
        return 0;
    col = x < nw ? AV_BY_NAME : x < nw + AV_SIZE_W ? AV_BY_SIZE
        : x < nw + 2 * AV_SIZE_W ? AV_BY_PACKED : AV_BY_DATE;
    c->desc = col == c->sort ? !c->desc : 0;
    c->sort = col;
    av_rows(c);
    return 1;
}

/** @brief A press on the path bar: back to the folder clicked. */
static int av_crumbs_pointer(tiku_arcview_t *c, const tiku_event_t *ev,
                             const av_layout_t *l)
{
    int nodes[32], n;
    tiku_rect_t at[32];

    if (ev->type != TIKU_EVENT_POINTER_DOWN || !c->node ||
        !av_in(l->crumbs, ev->x, ev->y)) return 0;
    n = av_crumbs(c, l->crumbs, nodes, at, 32);
    for (int k = 0; k < n - 1; k++)
        if (av_in(at[k], ev->x, ev->y)) av_enter(c, nodes[k]);
    return 1;
}

/** @brief Presses on the rows: one chosen, more with Shift or Command, a
 *         second press on the same row soon after opens it; the wheel. */
static int av_rows_pointer(tiku_arcview_t *c, const tiku_event_t *ev,
                           const av_layout_t *l)
{
    int row;

    if (!c->node || !av_in(l->body, ev->x, ev->y)) return 0;
    if (ev->type == TIKU_EVENT_WHEEL) {
        (void)tiku_list_scroll_to(&c->list, c->list.top + 3 *
                                  (ev->dy > 0 ? -1 : 1), l->body);
        return 1;
    }
    if (ev->type != TIKU_EVENT_POINTER_DOWN || ev->button != 1) return 0;
    row = tiku_list_at(&c->list, l->body, ev->x, ev->y);
    (void)tiku_dialog_focus(&c->dialog, AV_LIST);
    if (row >= 0 && row == c->last_row && !ev->modifiers &&
        ev->time_us - c->last_click < AV_DOUBLE_US) {
        c->last_row = -1;
        tiku_list_select_only(&c->list, row);
        av_open_row(c);
        return 1;
    }
    c->last_row = row;
    c->last_click = ev->time_us;
    tiku_list_click(&c->list, row, ev->modifiers);
    av_menus(c);
    return 1;
}

/** @brief Pointer input for the find field, path bar, headers, bar and
 *         rows; the toolbar's buttons are the dialog's. */
static int av_pointer(tiku_arcview_t *c, const tiku_event_t *ev,
                      const av_layout_t *l)
{
    if (ev->type == TIKU_EVENT_POINTER_DOWN && av_in(l->find, ev->x, ev->y)
        && c->node) {
        tiku_dialog_focus(&c->dialog, AV_FIND_FIELD);
        (void)av_field_event(c, ev, l);
        return 1;
    }
    if (c->find_in.dragging && av_field_event(c, ev, l)) return 1;
    return av_bar_pointer(c, ev, l) || av_head_pointer(c, ev, l) ||
           av_crumbs_pointer(c, ev, l) || av_rows_pointer(c, ev, l);
}
