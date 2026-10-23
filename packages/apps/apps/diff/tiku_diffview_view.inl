/* tiku_diffview_view.inl -- part of tiku_diffview.c: the window's layout,
 * painting and pointer input: a toolbar of icon buttons, the two sides'
 * names, two panes of lines or the folders' list, a map of the changes
 * beside the scrollbar, and the status line with its gauge and Stop.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */

#define DV_SIZE_W   90              /* the folder list's size columns     */
#define DV_STATE_W  96              /* and its status column              */
#define DV_TOKENS   512             /* words of a line compared inside it */
#define DV_RANGES   64              /* changed stretches of a line marked */

/** @brief A toolbar button's icon, from the desk's set. */
static const char *dv_tool_icon(int id)
{
    switch (id) {
    case DV_COMPARE: return "compare_files";
    case DV_COMPARE_DIRS: return "compare_folders";
    case DV_SWAP: return "swap_sides";
    case DV_PREV: return "find_prev";
    case DV_NEXT: return "find_next";
    case DV_BACK: return "nav_back";
    }
    return "file";
}

/** @brief What a toolbar button does, as its tip says it. */
static const char *dv_tool_tip(int id)
{
    switch (id) {
    case DV_COMPARE: return "Compare Files (Ctrl+O)";
    case DV_COMPARE_DIRS: return "Compare Folders (Shift+Ctrl+O)";
    case DV_SWAP: return "Swap Sides";
    case DV_PREV: return "Previous Change (Ctrl+[)";
    case DV_NEXT: return "Next Change (Ctrl+])";
    case DV_BACK: return "Back to Folders (Ctrl+Up)";
    }
    return "";
}

/** @brief A toolbar button's side: square, the button height but at
 *         least 30 pixels. */
static int dv_tool_size(void)
{
    int bh = tiku_ui_button_h();

    return bh > 30 ? bh : 30;
}

/** @brief The toolbar's buttons, into the dialog.  @return the row below. */
static int dv_tools_layout(tiku_diffview_t *c, tiku_rect_t r)
{
    static const int ids[] = { DV_COMPARE, DV_COMPARE_DIRS, DV_SWAP, DV_PREV,
                               DV_NEXT, DV_BACK };
    int bw = dv_tool_size(), x = r.x + 6, y = r.y + 4;

    for (size_t i = 0; i < sizeof ids / sizeof ids[0]; i++) {
        if (ids[i] == DV_SWAP || ids[i] == DV_PREV || ids[i] == DV_BACK)
            x += 10;
        (void)tiku_dialog_add(&c->dialog, ids[i], (tiku_rect_t){x, y, bw, bw},
                              TIKU_DIALOG_BUTTON, dv_enabled(c, ids[i]));
        x += bw + 2;
    }
    return y + bw + 4;
}

/** @brief How many rows there are to scroll: lines, or listed paths. */
static long dv_total(const tiku_diffview_t *c)
{
    if (c->mode == DV_FILES && c->files && !c->files->binary)
        return c->files->nrow;
    return c->mode == DV_FOLDERS ? c->nshown : 0;
}

/** @brief How many digits the larger file's last line number takes, at
 *         least three. */
static int dv_digits(const tiku_diffview_t *c)
{
    long n = c->files ? (c->files->nline[0] > c->files->nline[1]
                         ? c->files->nline[0] : c->files->nline[1]) : 0;
    int d = 1;

    while (n >= 10) {
        n /= 10;
        d++;
    }
    return d < 3 ? 3 : d;
}

/** @brief Where everything goes in content @p r. */
static dv_layout_t dv_layout(tiku_diffview_t *c, tiku_rect_t r)
{
    dv_layout_t l;
    int fh = tiku_font_plain()->height + 6, bh = tiku_ui_button_h(), below;

    memset(&l, 0, sizeof l);
    tiku_dialog_begin(&c->dialog, -1, -1);
    below = dv_tools_layout(c, r);
    l.cell = tiku_font_mono_cell(0);
    l.rh = c->mode == DV_FOLDERS ? tiku_measure_row_h() + 2
                                 : tiku_font_mono(0)->height + 3;
    l.names = (tiku_rect_t){r.x + 4, below, r.w - 8 - DV_BAR - DV_MAP, fh};
    l.status = (tiku_rect_t){r.x + 8, r.y + r.h - (bh > fh ? bh : fh) - 3,
                             r.w - 16, bh > fh ? bh : fh};
    l.body = (tiku_rect_t){l.names.x, below + fh, l.names.w,
                           l.status.y - 3 - (below + fh)};
    if (l.body.h < l.rh) l.body.h = l.rh;
    l.left = (tiku_rect_t){l.body.x, l.body.y, (l.body.w - 1) / 2, l.body.h};
    l.right = (tiku_rect_t){l.left.x + l.left.w + 1, l.body.y,
                            l.body.w - l.left.w - 1, l.body.h};
    l.map = (tiku_rect_t){l.body.x + l.body.w, l.body.y, DV_MAP, l.body.h};
    l.vbar = (tiku_rect_t){l.map.x + DV_MAP, l.body.y, DV_BAR, l.body.h};
    l.gutter = dv_digits(c) * l.cell + 10;
    l.list = l.body;
    if (c->mode == DV_FOLDERS) {    /* its column headers above the rows */
        l.list.y += fh;
        l.list.h -= fh;
    }
    l.rows = l.list.h / l.rh;
    (void)tiku_dialog_add(&c->dialog, DV_BODY, l.body, TIKU_DIALOG_INPUT,
                          c->mode != DV_EMPTY);
    if (c->progress) {
        int cw = tiku_ui_button_w("Stop", 60);

        l.cancel = (tiku_rect_t){l.status.x + l.status.w - cw, l.status.y,
                                 cw, bh};
        l.gauge = (tiku_rect_t){l.cancel.x - 176, l.status.y + (bh - 12) / 2,
                                168, 12};
        (void)tiku_dialog_add(&c->dialog, DV_CANCEL, l.cancel,
                              TIKU_DIALOG_BUTTON, 1);
    }
    tiku_dialog_end(&c->dialog);
    if (c->dialog.focus == TIKU_DIALOG_NONE && c->mode != DV_EMPTY)
        (void)tiku_dialog_focus(&c->dialog, DV_BODY);
    tiku_scroll_set_range(&c->vbar, (int)dv_total(c), l.rows);
    return l;
}

/* -------------------------------------------------------------- paint  */

/** @brief @p b mixed over @p a at @p pct percent, keeping @p a's top
 *         byte. */
static tiku_rgb_t dv_mix(tiku_rgb_t a, tiku_rgb_t b, int pct)
{
    tiku_rgb_t out = a & 0xff000000u;

    for (int sh = 0; sh <= 16; sh += 8) {
        int x = (int)((a >> sh) & 255u), y = (int)((b >> sh) & 255u);

        out |= (tiku_rgb_t)((x * (100 - pct) + y * pct) / 100) << sh;
    }
    return out;
}

/** @brief The inks a frame of lines is painted with. */
typedef struct {
    tiku_surface_t *s;
    const dv_layout_t *l;
    const tiku_font_t *mono;
    tiku_rgb_t band[2], word[2];    /* removed on the left, added right   */
    tiku_rgb_t gap, gutter;
    long cur0, cur1;                /* the shown change's rows            */
} dv_paint_t;

/** @brief A toolbar button as the desk's toolbars draw one: its icon,
 *         raised under the pointer, sunk while pressed, dimmed when it can
 *         do nothing; its name carried for narration. */
static void dv_button(tiku_diffview_t *c, tiku_surface_t *s,
                      const tiku_dialog_control_t *d)
{
    unsigned flags = d->enabled ? 0 : TIKU_S_DISABLED;
    int held = tiku_dialog_pressed(&c->dialog, d->id), size = d->rect.h - 6;

    if (d->id == DV_CANCEL) {       /* a word in the status line */
        tiku_ui_button(s, d->rect, "Stop", held ? TIKU_S_PRESSED : 0);
        return;
    }
    if (tiku_dialog_focused(&c->dialog, d->id)) flags |= TIKU_S_FOCUS;
    if (held) flags |= TIKU_S_PRESSED;
    if (c->hover_tool == d->id) flags |= TIKU_S_HOT;
    tiku_ui_button(s, d->rect, dv_label(c, d->id), flags | TIKU_S_ICON);
    if (size > 24) size = 24;
    (void)tiku_icon_paint_dim(s, dv_tool_icon(d->id),
                              d->rect.x + (d->rect.w - size) / 2 + held,
                              d->rect.y + (d->rect.h - size) / 2 + held, size,
                              d->enabled ? 0.0f : 0.72f, TIKU_C_PANEL);
}

/** @brief @p l's words, runs of blanks and single marks: where each
 *         starts into @p at (one more for the end), a number for what it
 *         is into @p id.  @return how many; the rest of a long line is
 *         one last word. */
static int dv_tokens(const tiku_diff_line_t *l, long *at, uint32_t *id)
{
    size_t i = 0, end = l->len;
    int n = 0;

    while (end > 0 && (l->text[end - 1] == '\n' || l->text[end - 1] == '\r'))
        end--;
    while (i < end && n < DV_TOKENS) {
        size_t start = i;
        unsigned char ch = (unsigned char)l->text[i];
        uint32_t h = 2166136261u;

        if (isalnum(ch) || ch == '_' || ch >= 0x80) {
            while (i < end && (isalnum((unsigned char)l->text[i]) ||
                               l->text[i] == '_' ||
                               (unsigned char)l->text[i] >= 0x80)) i++;
        } else if (ch == ' ' || ch == '\t') {
            while (i < end && (l->text[i] == ' ' || l->text[i] == '\t')) i++;
        } else {
            i++;
        }
        if (n == DV_TOKENS - 1) i = end;
        for (size_t k = start; k < i; k++)
            h = (h ^ (unsigned char)l->text[k]) * 16777619u;
        at[n] = (long)start;
        id[n++] = h;
    }
    at[n] = (long)i;
    return n;
}

/**
 * @brief What changed inside a changed row: its two lines' words compared
 *        as numbers, and the byte ranges of the words that differ into
 *        @p rng[side] (start, end pairs), their counts into @p nrng.
 */
static void dv_words(const dv_files_t *f, const dv_row_t *rw,
                     long rng[2][2 * DV_RANGES], int nrng[2])
{
    static long at[2][DV_TOKENS + 1];
    static uint32_t id[2][DV_TOKENS];
    int n[2];
    tiku_diff_t d;

    nrng[0] = nrng[1] = 0;
    n[0] = dv_tokens(&f->line[0][rw->a], at[0], id[0]);
    n[1] = dv_tokens(&f->line[1][rw->b], at[1], id[1]);
    if (tiku_diff_ints(id[0], n[0], id[1], n[1], 0, &d) != 0) return;
    for (long k = 0; k < d.nrun; k++) {
        const tiku_diff_run_t *r = &d.run[k];
        int side = r->kind == TIKU_DIFF_ADD;
        long first = side ? r->b : r->a;

        if (r->kind == TIKU_DIFF_SAME || nrng[side] >= DV_RANGES) continue;
        rng[side][2 * nrng[side]] = at[side][first];
        rng[side][2 * nrng[side]++ + 1] = at[side][first + r->n];
    }
    tiku_diff_free(&d);
}

/** @brief Whether byte @p i falls in one of @p n ranges. */
static int dv_in_ranges(const long *rng, int n, long i)
{
    for (int k = 0; k < n; k++)
        if (i >= rng[2 * k] && i < rng[2 * k + 1]) return 1;
    return 0;
}

/**
 * @brief Line @p l at @p r, from column @p c->hoff, tabs to DV_TAB
 *        columns and control characters shown as '?': the ranges given
 *        lit behind it in @p word, a stretch at a time, then the text.
 */
static void dv_text(tiku_diffview_t *c, const dv_paint_t *p, tiku_rect_t r,
                    const tiku_diff_line_t *l, const long *rng, int nrng,
                    tiku_rgb_t word)
{
    char out[1024];
    int col = 0, k = 0, cols = r.w / p->l->cell + 1, lit = -1;
    size_t end = l->len;

    while (end > 0 && (l->text[end - 1] == '\n' || l->text[end - 1] == '\r'))
        end--;
    for (size_t i = 0; i < end && col < c->hoff + cols; i++) {
        unsigned char ch = (unsigned char)l->text[i];
        int w = ch == '\t' ? DV_TAB - col % DV_TAB : 1;
        int in = nrng && dv_in_ranges(rng, nrng, (long)i);

        for (int q = 0; q < w && col < c->hoff + cols; q++, col++) {
            if (col < c->hoff) continue;
            if (in && lit < 0) lit = col;
            if (!in && lit >= 0) {
                tiku_fill(p->s, (tiku_rect_t){r.x + (lit - c->hoff) *
                          p->l->cell, r.y, (col - lit) * p->l->cell, r.h},
                          word);
                lit = -1;
            }
            if (k < (int)sizeof out - 1)
                out[k++] = ch == '\t' ? ' ' : ch < 32 || ch == 127 ? '?'
                                                                   : (char)ch;
        }
    }
    if (lit >= 0)
        tiku_fill(p->s, (tiku_rect_t){r.x + (lit - c->hoff) * p->l->cell, r.y,
                  (col - lit) * p->l->cell, r.h}, word);
    out[k] = '\0';
    tiku_text(p->s, p->mono, r.x, r.y + p->mono->ascent + 1, out, TIKU_C_TEXT);
}

/** @brief One side of row @p row in @p pane: its band, line number and
 *         text, or the empty band facing a line the other side has. */
static void dv_half(tiku_diffview_t *c, const dv_paint_t *p, tiku_rect_t pane,
                    long row, int side, long rng[2][2 * DV_RANGES],
                    const int nrng[2])
{
    const dv_row_t *rw = &c->files->row[row];
    long line = side ? rw->b : rw->a;
    int y = pane.y + (int)(row - c->vbar.value) * p->l->rh;
    tiku_rect_t band = { pane.x, y, pane.w, p->l->rh };
    char num[24];

    if (line < 0) {
        tiku_fill(p->s, band, p->gap);
        return;
    }
    tiku_fill(p->s, band, rw->kind == DV_ROW_SAME ? TIKU_C_DOC
                                                  : p->band[side]);
    tiku_fill(p->s, (tiku_rect_t){pane.x, y, p->l->gutter - 4, p->l->rh},
              rw->kind == DV_ROW_SAME ? p->gutter : p->band[side]);
    if (row >= p->cur0 && row < p->cur1)       /* the change shown */
        tiku_fill(p->s, (tiku_rect_t){pane.x, y, 3, p->l->rh}, TIKU_C_FOCUS);
    snprintf(num, sizeof num, "%ld", line + 1);
    tiku_text(p->s, p->mono, pane.x + p->l->gutter - 8 -
              (int)strlen(num) * p->l->cell, y + p->mono->ascent + 1, num,
              tiku_ink(TIKU_INK_FAINT));
    dv_text(c, p, (tiku_rect_t){pane.x + p->l->gutter, y,
            pane.w - p->l->gutter, p->l->rh},
            &c->files->line[side][line], rng[side],
            rw->kind == DV_ROW_CHANGE ? nrng[side] : 0, p->word[side]);
}

/** @brief The rows of the two files that can be seen, side by side. */
static void dv_files_draw(tiku_diffview_t *c, dv_paint_t *p)
{
    const dv_files_t *f = c->files;
    long rng[2][2 * DV_RANGES];
    int nrng[2] = { 0, 0 };
    tiku_rect_t clip = p->s->clip;

    p->cur0 = c->cur >= 0 ? f->change[c->cur] : -1;
    p->cur1 = c->cur >= 0 ? f->end[c->cur] : -1;
    for (long row = c->vbar.value; row < f->nrow &&
         row < c->vbar.value + p->l->rows + 1; row++) {
        if (f->row[row].kind == DV_ROW_CHANGE) dv_words(f, &f->row[row], rng,
                                                        nrng);
        tiku_clip_set(p->s, p->l->left);
        dv_half(c, p, p->l->left, row, 0, rng, nrng);
        tiku_clip_set(p->s, p->l->right);
        dv_half(c, p, p->l->right, row, 1, rng, nrng);
        tiku_clip_set(p->s, clip);
    }
    tiku_fill(p->s, (tiku_rect_t){p->l->left.x + p->l->left.w, p->l->body.y,
              1, p->l->body.h}, tiku_tint(TIKU_C_PANEL, TIKU_DARKEN_4));
}

/** @brief Where the changes are in the whole file, a mark each, and a
 *         frame round the rows in sight. */
static void dv_map_draw(tiku_diffview_t *c, const dv_paint_t *p)
{
    const dv_files_t *f = c->files;
    tiku_rect_t m = p->l->map;
    long n = f && !f->binary && f->nrow ? f->nrow : 1;

    tiku_fill(p->s, m, dv_mix(TIKU_C_DOC, TIKU_C_PANEL, 50));
    for (long i = 0; f && !f->binary && i < f->nchange; i++) {
        int y0 = m.y + (int)(f->change[i] * m.h / n);
        int y1 = m.y + (int)(f->end[i] * m.h / n);
        int dels = 0, adds = 0;

        for (long r = f->change[i]; r < f->end[i]; r++) {
            dels += f->row[r].a >= 0;
            adds += f->row[r].b >= 0;
        }
        tiku_fill(p->s, (tiku_rect_t){m.x + 1, y0, m.w - 2,
                  y1 - y0 < 2 ? 2 : y1 - y0},
                  !adds ? TIKU_C_DANGER : !dels ? TIKU_C_SUCCESS : TIKU_C_WARN);
    }
    if (f && !f->binary && f->nrow > p->l->rows)
        tiku_frame(p->s, (tiku_rect_t){m.x, m.y + (int)(c->vbar.value * m.h /
                   n), m.w, (int)((long)p->l->rows * m.h / n) + 1},
                   tiku_tint(TIKU_C_PANEL, TIKU_DARKEN_4));
}

/** @brief The width of @p text in the list's face, for fitting sizes. */
static int dv_measure(const char *text, void *ctx)
{
    (void)ctx;
    return tiku_measure_width(TIKU_MEASURE_PLAIN, text);
}

/** @brief One row of the folder list: how it stands, its path, and its
 *         size on each side. */
static void dv_entry_draw(tiku_diffview_t *c, const dv_paint_t *p, long row)
{
    const dv_entry_t *e = &c->folders->e[c->shown[row]];
    tiku_rect_t r = { p->l->list.x, p->l->list.y + (int)(row -
                      c->vbar.value) * p->l->rh, p->l->list.w, p->l->rh };
    int pw = r.w - DV_STATE_W - 2 * DV_SIZE_W, sel = row == c->sel;
    static const char *const said[] = { "same", "changed", "left only",
                                        "right only" };
    tiku_rgb_t tone[4] = { tiku_ink(TIKU_INK_FAINT), TIKU_C_WARN,
                           TIKU_C_DANGER, TIKU_C_SUCCESS };
    char size[32];

    if (sel) tiku_fill(p->s, r, TIKU_C_SELECT);
    tiku_text_line_draw(p->s, (tiku_rect_t){r.x + 8, r.y, DV_STATE_W - 12, r.h},
                        TIKU_MEASURE_PLAIN, said[e->status],
                        sel ? TIKU_C_SELTEXT : tone[e->status]);
    tiku_text_line_draw(p->s, (tiku_rect_t){r.x + DV_STATE_W, r.y, pw - 8,
                        r.h}, TIKU_MEASURE_PLAIN, e->rel,
                        sel ? TIKU_C_SELTEXT : TIKU_C_TEXT);
    for (int s = 0; s < 2; s++) {
        if (e->size[s] < 0) snprintf(size, sizeof size, "-");
        else (void)tiku_attr_size_fit(e->size[s], DV_SIZE_W - 8, dv_measure,
                                      NULL, size, sizeof size);
        tiku_text_line_draw(p->s, (tiku_rect_t){r.x + DV_STATE_W + pw +
                            s * DV_SIZE_W, r.y, DV_SIZE_W - 8, r.h},
                            TIKU_MEASURE_PLAIN, size,
                            sel ? TIKU_C_SELTEXT : TIKU_C_TEXT);
    }
}

/** @brief The folder list: its headers, then the rows in sight. */
static void dv_folders_draw(tiku_diffview_t *c, const dv_paint_t *p)
{
    static const char *const cols[] = { "Status", "Path", "Left", "Right" };
    int widths[4] = { DV_STATE_W, p->l->list.w - DV_STATE_W - 2 * DV_SIZE_W,
                      DV_SIZE_W, DV_SIZE_W };
    tiku_rect_t clip = p->s->clip;

    tiku_ui_list_header(p->s, (tiku_rect_t){p->l->body.x, p->l->body.y,
                        p->l->body.w, p->l->list.y - p->l->body.y}, cols,
                        widths, 4, -1);
    tiku_clip_set(p->s, p->l->list);
    for (long row = c->vbar.value; row < c->nshown &&
         row < c->vbar.value + p->l->rows + 1; row++)
        dv_entry_draw(c, p, row);
    tiku_clip_set(p->s, clip);
    if (!c->nshown)
        tiku_text_line_draw(p->s, (tiku_rect_t){p->l->list.x + 12,
                            p->l->list.y + 8, p->l->list.w - 24, p->l->rh},
                            TIKU_MEASURE_PLAIN, "No differences.",
                            tiku_ink(TIKU_INK_FAINT));
}

/** @brief The two sides' names over their panes; the folder list's two
 *         roots at its left. */
static void dv_names_draw(tiku_diffview_t *c, const dv_paint_t *p)
{
    char both[2 * 256 + 8];

    if (c->mode == DV_FOLDERS) {
        snprintf(both, sizeof both, "%s  :  %s", c->side[0].name,
                 c->side[1].name);
        tiku_text_line_draw(p->s, p->l->names, TIKU_MEASURE_BOLD, both,
                            TIKU_C_TEXT);
        return;
    }
    for (int s = 0; s < 2; s++) {
        const char *name = c->from_folder ? c->rel : c->side[s].name;
        tiku_rect_t r = s ? p->l->right : p->l->left;

        tiku_text_line_draw(p->s, (tiku_rect_t){r.x + 6, p->l->names.y,
                            r.w - 12, p->l->names.h}, TIKU_MEASURE_BOLD,
                            name, TIKU_C_TEXT);
    }
}

/** @brief The status line: what was said at the left; at the right which
 *         change is shown, or how many rows, or the running job's gauge. */
static void dv_status_draw(tiku_diffview_t *c, tiku_surface_t *s,
                           const dv_layout_t *l)
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
    } else if (c->mode == DV_FILES && c->files && c->files->nchange) {
        if (c->cur >= 0)
            snprintf(where, sizeof where, "Change %ld of %ld", c->cur + 1,
                     c->files->nchange);
        else
            snprintf(where, sizeof where, "%ld change%s", c->files->nchange,
                     c->files->nchange == 1 ? "" : "s");
    } else if (c->mode == DV_FOLDERS) {
        snprintf(where, sizeof where, "%ld item%s", c->nshown,
                 c->nshown == 1 ? "" : "s");
    }
    if (where[0]) {
        w = tiku_measure_width(TIKU_MEASURE_PLAIN, where) + 8;
        tiku_text_line_draw(s, (tiku_rect_t){r.x + r.w - w, r.y, w, r.h},
                            TIKU_MEASURE_PLAIN, where, TIKU_C_TEXT);
    }
    tiku_text_line_draw(s, (tiku_rect_t){r.x, r.y, r.w - w - 8, r.h},
                        TIKU_MEASURE_PLAIN, c->hint[0] ? c->hint : c->status,
                        tiku_ink(TIKU_INK_FAINT));
}

/** @brief The tip of the toolbar button under the pointer, below it. */
static void dv_tip_draw(tiku_diffview_t *c, tiku_surface_t *s, tiku_rect_t r)
{
    for (int i = 0; i < c->dialog.count && c->hover_tool; i++) {
        const tiku_dialog_control_t *d = &c->dialog.control[i];
        const char *text = dv_tool_tip(d->id);
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

/** @brief The inks for a frame, from the theme: removed in the danger
 *         ink and added in the success ink, washed into the page. */
static void dv_paint_start(dv_paint_t *p, tiku_surface_t *s,
                           const dv_layout_t *l)
{
    p->s = s;
    p->l = l;
    p->mono = tiku_font_mono(0);
    p->band[0] = dv_mix(TIKU_C_DOC, TIKU_C_DANGER, 14);
    p->word[0] = dv_mix(TIKU_C_DOC, TIKU_C_DANGER, 36);
    p->band[1] = dv_mix(TIKU_C_DOC, TIKU_C_SUCCESS, 16);
    p->word[1] = dv_mix(TIKU_C_DOC, TIKU_C_SUCCESS, 40);
    p->gap = dv_mix(TIKU_C_DOC, TIKU_C_PANEL, 55);
    p->gutter = dv_mix(TIKU_C_DOC, TIKU_C_PANEL, 25);
    p->cur0 = p->cur1 = -1;
}

/** @brief The whole window, laid out afresh each time. */
static void dv_draw(tiku_window_t *w, tiku_surface_t *s, tiku_rect_t r,
                    void *ctx)
{
    tiku_diffview_t *c = ctx;
    tiku_rect_t clip = s->clip;
    dv_layout_t l;
    dv_paint_t p;
    float pos = 0, frac = 1;

    (void)w;
    tiku_clip_set(s, r);
    tiku_ui_panel(s, r);
    l = dv_layout(c, r);
    dv_paint_start(&p, s, &l);
    for (int i = 0; i < c->dialog.count; i++)
        if (c->dialog.control[i].kind == TIKU_DIALOG_BUTTON)
            dv_button(c, s, &c->dialog.control[i]);
    tiku_fill(s, l.body, TIKU_C_DOC);           /* blank until compared */
    if (c->mode != DV_EMPTY) dv_names_draw(c, &p);
    if (c->mode == DV_FILES && c->files && !c->files->binary)
        dv_files_draw(c, &p);
    if (c->mode == DV_FOLDERS && c->folders) dv_folders_draw(c, &p);
    dv_map_draw(c, &p);
    tiku_frame(s, (tiku_rect_t){l.body.x - 1, l.body.y - 1, l.body.w + DV_MAP +
               DV_BAR + 2, l.body.h + 2}, tiku_tint(TIKU_C_PANEL,
                                                    TIKU_DARKEN_4));
    if (tiku_scroll_max(&c->vbar) > 0) {
        pos = (float)c->vbar.value / (float)tiku_scroll_max(&c->vbar);
        frac = (float)c->vbar.visible / (float)(dv_total(c) ? dv_total(c) : 1);
    }
    tiku_ui_scrollbar_held(s, l.vbar, pos, frac, 0, c->vbar.arrow_dir);
    dv_status_draw(c, s, &l);
    dv_tip_draw(c, s, r);           /* last: nothing paints over it */
    s->clip = clip;
}

/* ------------------------------------------------------------ pointer  */

/** @brief The toolbar button under the pointer, lit and tipped; none once
 *         something is pressed or typed, or the window loses the focus. */
static void dv_tool_hover(tiku_diffview_t *c, const tiku_event_t *ev)
{
    int tool = 0;

    if (ev->type == TIKU_EVENT_POINTER_MOVE &&
        c->dialog.capture == TIKU_DIALOG_NONE) {
        for (int i = 0; i < c->dialog.count; i++) {
            const tiku_dialog_control_t *d = &c->dialog.control[i];
            if (d->kind == TIKU_DIALOG_BUTTON && d->id != DV_CANCEL &&
                dv_in(d->rect, ev->x, ev->y))
                tool = d->id;
        }
    }
    if (tool != c->hover_tool) {
        c->hover_tool = tool;
        dv_changed(c);
    }
}

/** @brief Presses and drags on the scrollbar; a press on the map goes to
 *         the place it stands for. */
static int dv_bar_pointer(tiku_diffview_t *c, const tiku_event_t *ev,
                          const dv_layout_t *l)
{
    int at = ev->y - l->vbar.y;

    if (ev->type == TIKU_EVENT_POINTER_DOWN && ev->button == 1 &&
        dv_in(l->map, ev->x, ev->y) && dv_total(c) > 0) {
        dv_scroll(c, (long)(ev->y - l->map.y) * dv_total(c) / l->map.h -
                     l->rows / 2);
    } else if (ev->type == TIKU_EVENT_POINTER_DOWN && ev->button == 1 &&
               dv_in(l->vbar, ev->x, ev->y)) {
        c->vdrag = tiku_scroll_press(&c->vbar, l->vbar.h, at);
    } else if (ev->type == TIKU_EVENT_POINTER_MOVE && c->vdrag) {
        (void)tiku_scroll_drag(&c->vbar, l->vbar.h, at);
    } else if (ev->type == TIKU_EVENT_POINTER_UP && c->vdrag) {
        c->vdrag = 0;
        tiku_scroll_release(&c->vbar);
    } else {
        return 0;
    }
    return 1;
}

/** @brief Presses on the rows: in the files, the change under the pointer
 *         becomes the one shown; in the folder list, a row is chosen and a
 *         second press soon after opens it.  The wheel scrolls either. */
static int dv_body_pointer(tiku_diffview_t *c, const tiku_event_t *ev,
                           const dv_layout_t *l)
{
    long row;

    if (c->mode == DV_EMPTY || !dv_in(l->body, ev->x, ev->y)) return 0;
    if (ev->type == TIKU_EVENT_WHEEL) {
        if (ev->modifiers & TIKU_MOD_SHIFT) dv_hscroll(c, ev->dy > 0 ? -8 : 8);
        else dv_scroll(c, c->vbar.value + 3 * (ev->dy > 0 ? -1 : 1));
        return 1;
    }
    if (ev->type != TIKU_EVENT_POINTER_DOWN || ev->button != 1 ||
        ev->y < l->list.y) return 0;
    (void)tiku_dialog_focus(&c->dialog, DV_BODY);
    row = c->vbar.value + (ev->y - l->list.y) / l->rh;
    if (c->mode == DV_FILES) {
        dv_pick_change(c, row);
        return 1;
    }
    if (row >= c->nshown) return 1;
    if (row == c->last_row && ev->time_us - c->last_click < DV_DOUBLE_US) {
        c->last_row = -1;
        dv_action(c, DV_OPEN_ROW);
        return 1;
    }
    c->sel = row;
    c->last_row = row;
    c->last_click = ev->time_us;
    dv_menus(c);
    return 1;
}
