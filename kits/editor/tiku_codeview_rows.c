/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_codeview_rows.c - lines broken into rows at the text's width, the
 * rows of the annotations under them, and folded lines taking none: where
 * a line's rows start, how many each line has, and each line's first row,
 * kept in the partition the document keeps its lines in.
 *
 * Lines hidden by folding have no rows, as display lines are counted in
 * Scintilla 5.6.7 src/ContractionState.cxx, Copyright 1998-2007 by Neil
 * Hodgson; see LICENSE.Scintilla and UPSTREAM.md for what changed.
 *
 * SPDX-License-Identifier: Apache-2.0 AND LicenseRef-Scintilla
 */

#include "tiku_codeview_private.h"
#include "tiku_editdoc_private.h"

#include <string.h>

/* Line starts written into the map at a time. */
#define BLOCK 128
/* Bytes of lines counted ahead each time the rows are made ready. */
#define READY_BYTES ((int64_t)1 << 18)

/** @brief A line being broken into rows. */
typedef struct {
    tiku_codeview_t *v;
    int64_t line;
    tiku_edit_pos_t line_start; /* the line's first byte                */
    int64_t width;              /* pixels a row holds                   */
    tiku_edit_pos_t row_start;  /* the row being filled                 */
    int64_t x0;                 /* line x at row_start                  */
    tiku_edit_pos_t opp;        /* where the row may break, or -1       */
    int64_t opp_x;              /* line x at opp                        */
    int blank;                  /* the last character was a blank       */
    int64_t rows;               /* rows started                         */
    int keep;                   /* keep where the rows start            */
    tiku_cv_wrap_t *entry;      /* where the row starts are kept,
                                   once a second row starts             */
} wrap_walk_t;

int
tiku_cv_rows_kept(const tiku_codeview_t *v)
{
    return v->row_lines > 0;
}

int
tiku_cv_wraps(const tiku_codeview_t *v)
{
    return v->row_lines > 0 && v->row_width > 0;
}

/** @brief The pixels a row holds with the wrap option on: the text's
 *         width less a margin the caret fits in; 0 when lines do not wrap
 *         or there is no room to wrap them in. */
static int64_t
break_width(const tiku_codeview_t *v)
{
    int64_t w = tiku_cv_text_w(v) - TIKU_CV_PAD_X;

    return v->options & TIKU_CODEVIEW_WRAP && w > 0 ? w : 0;
}

/** @brief Add a row start to @p e; 0 when there is no room. */
static int
push_row(tiku_cv_wrap_t *e, tiku_edit_pos_t off, int64_t x)
{
    if (e->nrows == e->cap) {
        size_t cap = e->cap ? e->cap * 2 : 16;
        tiku_cv_mark_t *grown = cap <= SIZE_MAX / sizeof *grown ?
            tiku_ed_realloc(e->rows, cap * sizeof *grown) : NULL;

        if (grown == NULL)
            return 0;
        e->rows = grown;
        e->cap = cap;
    }
    e->rows[e->nrows].pos = off;
    e->rows[e->nrows].x = x;
    e->nrows++;
    return 1;
}

/** @brief The entry @p line had, or else an unused one or the one asked
 *         of least recently, taken for @p line with its first row in it;
 *         NULL when there is no room for that. */
static tiku_cv_wrap_t *
take_entry(tiku_codeview_t *v, int64_t line)
{
    tiku_cv_wrap_t *e = &v->wraps[0];
    int i;

    for (i = 0; i < TIKU_CV_WRAPS; i++) {
        tiku_cv_wrap_t *k = &v->wraps[i];

        if (k->line == line) {
            e = k;
            break;
        }
        if (e->line >= 0 && (k->line < 0 || k->used < e->used))
            e = k;
    }
    e->line = line;
    e->used = ++v->wrap_uses;
    e->nrows = 0;
    if (!push_row(e, 0, 0)) {
        e->line = -1;
        return NULL;
    }
    return e;
}

/** @brief Start a row at @p pos, @p x.  A walk that keeps rows starts
 *         keeping them, the first included, when the second starts, and
 *         stops once there is no room. */
static void
new_row(wrap_walk_t *w, tiku_edit_pos_t pos, int64_t x)
{
    if (w->keep && w->entry == NULL) {
        w->entry = take_entry(w->v, w->line);
        w->keep = w->entry != NULL;
    }
    w->rows++;
    w->row_start = pos;
    w->x0 = x;
    w->opp = -1;
    if (w->entry != NULL && !push_row(w->entry, pos - w->line_start, x)) {
        w->entry->line = -1;
        w->entry = NULL;
        w->keep = 0;
    }
}

/**
 * @brief Place one character: a row breaks before a character that would
 *        pass its width, after the last run of blanks in it when there is
 *        one, and holds at least one character.  Blanks never start a
 *        row; past the width they hang at the end of theirs.
 */
static int
wrap_cell(void *ctx, const tiku_cv_cell_t *c)
{
    wrap_walk_t *w = ctx;
    int blank = c->cp == ' ' || c->kind == TIKU_CV_TAB;

    if (!blank && w->blank && c->pos > w->row_start) {
        w->opp = c->pos;
        w->opp_x = c->x;
    }
    w->blank = blank;
    if (blank || c->pos == w->row_start ||
        c->x + c->adv - w->x0 <= w->width)
        return 0;
    if (w->opp > w->row_start)
        new_row(w, w->opp, w->opp_x);
    if (c->pos > w->row_start && c->x + c->adv - w->x0 > w->width)
        new_row(w, c->pos, c->x);
    return 0;
}

/** @brief Rows @p line has in the map, counted or not. */
static int64_t
mapped(const tiku_codeview_t *v, int64_t line)
{
    return tiku_ed_lines_start(&v->row_map, line + 1) -
           tiku_ed_lines_start(&v->row_map, line);
}

/** @brief Count @p line's rows, its text's and its annotations', and put
 *         the count in the map; with @p keep, a line whose text takes more
 *         than one row also keeps where they start, in the entry put in
 *         @p kept.  @return the count. */
static int64_t
measure(tiku_codeview_t *v, int64_t line, int keep, tiku_cv_wrap_t **kept)
{
    wrap_walk_t w;
    int64_t old, rows;

    memset(&w, 0, sizeof w);
    w.v = v;
    w.line = line;
    w.line_start = tiku_editdoc_line_start(v->doc, line);
    w.width = v->row_width;
    w.row_start = w.line_start;
    w.opp = -1;
    w.rows = 1;
    w.keep = keep;
    if (v->row_fold[line] & TIKU_CV_HIDDEN) {
        rows = 0;
    } else {
        if (w.width > 0)
            (void)tiku_cv_walk_near(v, line, 0, 0, wrap_cell, &w);
        rows = w.rows + tiku_cv_ann_rows(v, line);
    }
    old = mapped(v, line);
    if (rows != old)
        tiku_ed_lines_shift(&v->row_map, line, rows - old);
    v->row_known[line] = 1;
    if (line == v->unknown_from)
        v->unknown_from++;
    if (kept != NULL)
        *kept = w.entry;
    return rows;
}

int64_t
tiku_cv_line_rows(tiku_codeview_t *v, int64_t line)
{
    if (!tiku_cv_rows_kept(v) || line < 0 || line >= v->row_lines)
        return 1;
    if (!v->row_known[line])
        return measure(v, line, 1, NULL);
    return mapped(v, line);
}

int64_t
tiku_cv_text_rows(tiku_codeview_t *v, int64_t line)
{
    int64_t rows = tiku_cv_line_rows(v, line) - tiku_cv_ann_rows(v, line);

    return rows > 0 ? rows : 1;
}

/** @brief Where the rows of @p line, which has more than one, start:
 *         kept, or walked again; NULL when there is no room for them. */
static tiku_cv_wrap_t *
wrap_of(tiku_codeview_t *v, int64_t line, int64_t rows)
{
    tiku_cv_wrap_t *e = NULL;
    int i;

    for (i = 0; i < TIKU_CV_WRAPS; i++) {
        if (v->wraps[i].line == line) {
            e = &v->wraps[i];
            e->used = ++v->wrap_uses;
            break;
        }
    }
    if (e == NULL && measure(v, line, 1, &e) != rows)
        return NULL;
    return e != NULL && (int64_t)e->nrows == rows ? e : NULL;
}

void
tiku_cv_row(tiku_codeview_t *v, int64_t line, int64_t sub,
            tiku_cv_row_t *out)
{
    int64_t rows = tiku_cv_text_rows(v, line);
    tiku_cv_wrap_t *e = rows > 1 ? wrap_of(v, line, rows) : NULL;

    out->line = line;
    out->start = tiku_editdoc_line_start(v->doc, line);
    out->end = tiku_editdoc_line_end(v->doc, line);
    out->x0 = 0;
    out->last = 1;
    out->ann = -1;
    if (sub >= rows && sub < tiku_cv_line_rows(v, line)) {
        out->sub = sub;
        out->ann = sub - rows;
        out->start = out->end;
        return;
    }
    if (sub >= rows)
        sub = rows - 1;
    if (sub < 0)
        sub = 0;
    out->sub = sub;
    if (e != NULL) {
        out->x0 = e->rows[sub].x;
        if (sub + 1 < rows) {
            out->end = out->start + e->rows[sub + 1].pos;
            out->last = 0;
        }
        out->start += e->rows[sub].pos;
    } else if (sub > 0) {
        /* Without room for its rows a line is drawn whole on its first
         * row, and the rest are left empty. */
        out->start = out->end;
    }
}

void
tiku_cv_row_of(tiku_codeview_t *v, tiku_edit_pos_t pos, int upstream,
               tiku_cv_row_t *out)
{
    int64_t line = tiku_editdoc_line_of(v->doc, pos);
    int64_t rows = tiku_cv_text_rows(v, line), sub = 0;
    tiku_cv_wrap_t *e = rows > 1 ? wrap_of(v, line, rows) : NULL;
    tiku_edit_pos_t off = pos - tiku_editdoc_line_start(v->doc, line);
    size_t lo = 0, hi;

    if (e != NULL) {
        hi = e->nrows;
        while (lo < hi) {
            size_t mid = lo + (hi - lo) / 2;

            if (e->rows[mid].pos < off ||
                (!upstream && e->rows[mid].pos == off))
                lo = mid + 1;
            else
                hi = mid;
        }
        sub = (int64_t)lo - 1;
    }
    tiku_cv_row(v, line, sub, out);
}

tiku_edit_pos_t
tiku_cv_row_pos_at(tiku_codeview_t *v, const tiku_cv_row_t *r, int64_t x,
                   int *upstream)
{
    tiku_edit_pos_t pos = tiku_cv_pos_at(v, r->line, r->x0 + x);

    if (pos < r->start)
        pos = r->start;
    if (pos > r->end)
        pos = r->end;
    *upstream = pos == r->end && !r->last;
    return pos;
}

int64_t
tiku_cv_shown_line(const tiku_codeview_t *v, int64_t line, int back)
{
    int64_t lines = tiku_cv_rows_kept(v) ? v->row_lines : 0;

    while (line >= 0 && line < lines &&
           (v->row_fold[line] & TIKU_CV_HIDDEN))
        line += back ? -1 : 1;
    if (line < 0 || line >= tiku_editdoc_lines(v->doc))
        return -1;
    return line;
}

int64_t
tiku_cv_rows_step(tiku_codeview_t *v, int64_t *line, int64_t *sub,
                  int64_t n)
{
    int64_t moved = 0, next, rows;

    if (tiku_cv_shown_line(v, *line, 0) != *line) {
        next = tiku_cv_shown_line(v, *line, 1);
        *line = next >= 0 ? next : 0;
        *sub = 0;
    }
    rows = tiku_cv_line_rows(v, *line);
    if (*sub >= rows)
        *sub = rows - 1;
    if (*sub < 0)
        *sub = 0;
    while (n > 0) {
        next = *sub + n < rows ? -1 : tiku_cv_shown_line(v, *line + 1, 0);
        if (next < 0) {
            if (*sub + n >= rows)
                n = rows - 1 - *sub;
            *sub += n;
            return moved + n;
        }
        moved += rows - *sub;
        n -= rows - *sub;
        *line = next;
        *sub = 0;
        rows = tiku_cv_line_rows(v, next);
    }
    while (n < 0) {
        next = *sub + n >= 0 ? -1 : tiku_cv_shown_line(v, *line - 1, 1);
        if (next < 0) {
            if (*sub + n < 0)
                n = -*sub;
            *sub += n;
            return moved - n;
        }
        moved += *sub + 1;
        n += *sub + 1;
        *line = next;
        *sub = tiku_cv_line_rows(v, next) - 1;
    }
    return moved;
}

int64_t
tiku_cv_line_row(const tiku_codeview_t *v, int64_t line)
{
    return tiku_cv_rows_kept(v) ? tiku_ed_lines_start(&v->row_map, line) : line;
}

int64_t
tiku_cv_rows_total(const tiku_codeview_t *v)
{
    return tiku_cv_rows_kept(v) ? tiku_ed_lines_start(&v->row_map, v->row_lines)
                            : tiku_editdoc_lines(v->doc);
}

int64_t
tiku_cv_top_row(const tiku_codeview_t *v)
{
    return tiku_cv_line_row(v, v->top) + v->top_sub;
}

void
tiku_cv_set_top_row(tiku_codeview_t *v, int64_t row)
{
    int64_t total = tiku_cv_rows_total(v);

    if (row >= total)
        row = total - 1;
    if (row < 0)
        row = 0;
    if (!tiku_cv_rows_kept(v)) {
        v->top = row;
        v->top_sub = 0;
        return;
    }
    v->top = tiku_ed_lines_find(&v->row_map, row);
    v->top_sub = row - tiku_ed_lines_start(&v->row_map, v->top);
}

int64_t
tiku_cv_shown_row(const tiku_codeview_t *v, int64_t line, int64_t sub)
{
    return tiku_cv_line_row(v, line) + sub - tiku_cv_top_row(v);
}

void
tiku_cv_rows_free(tiku_codeview_t *v)
{
    int i;

    if (v->row_lines > 0)
        tiku_ed_lines_free(&v->row_map);
    tiku_ed_free(v->row_known);
    tiku_ed_free(v->row_fold);
    v->row_known = NULL;
    v->row_fold = NULL;
    v->row_lines = 0;
    v->row_cap = 0;
    v->unknown_from = 0;
    v->folds = 0;
    for (i = 0; i < TIKU_CV_WRAPS; i++) {
        tiku_ed_free(v->wraps[i].rows);
        memset(&v->wraps[i], 0, sizeof v->wraps[i]);
        v->wraps[i].line = -1;
    }
}

/** @brief Split the rows of line @p line - 1 so that it and the @p n
 *         lines after it have one each; the map holds room for them. */
static void
one_rows(tiku_codeview_t *v, int64_t line, int64_t n)
{
    int64_t starts[BLOCK], done = 0, k;
    int64_t first = tiku_ed_lines_start(&v->row_map, line - 1) + 1;

    while (done < n) {
        int64_t m = n - done < BLOCK ? n - done : BLOCK;

        for (k = 0; k < m; k++)
            starts[k] = first + done + k;
        tiku_ed_lines_insert_many(&v->row_map, line + done, starts,
                                  (ptrdiff_t)m);
        done += m;
    }
}

/** @brief Room in the counted and fold flags for @p n lines; 0 when
 *         there is none. */
static int
known_room(tiku_codeview_t *v, int64_t n)
{
    int64_t cap;
    unsigned char *grown;

    if (n <= v->row_cap)
        return 1;
    cap = n + n / 4 + 16;
    if (cap < 0 || (uint64_t)cap > SIZE_MAX)
        return 0;
    grown = tiku_ed_realloc(v->row_known, (size_t)cap);
    if (grown == NULL)
        return 0;
    v->row_known = grown;
    grown = tiku_ed_realloc(v->row_fold, (size_t)cap);
    if (grown == NULL)
        return 0;
    v->row_fold = grown;
    v->row_cap = cap;
    return 1;
}

/** @brief Make the map for the document, a row a line, none counted;
 *         0 when there is no room for it. */
static int
build(tiku_codeview_t *v)
{
    int64_t lines = tiku_editdoc_lines(v->doc);

    if (lines < 1 || !known_room(v, lines))
        return 0;
    if (!tiku_ed_lines_init(&v->row_map))
        return 0;
    if (!tiku_ed_lines_reserve(&v->row_map, (ptrdiff_t)lines)) {
        tiku_ed_lines_free(&v->row_map);
        return 0;
    }
    tiku_ed_lines_shift(&v->row_map, 0, lines);
    one_rows(v, 1, lines - 1);
    memset(v->row_known, 0, (size_t)lines);
    memset(v->row_fold, 0, (size_t)lines);
    v->row_lines = lines;
    v->unknown_from = 0;
    return 1;
}

void
tiku_cv_rows_change(tiku_codeview_t *v, int64_t first, int64_t last_old,
                    int64_t last_new, int64_t lines)
{
    int64_t delta = last_new - last_old, was, i;
    int k;

    if (!tiku_cv_rows_kept(v))
        return;
    if (first < 0 || last_old < first || last_new < first ||
        last_old >= v->row_lines || v->row_lines != lines - delta ||
        !known_room(v, lines) ||
        !tiku_ed_lines_reserve(&v->row_map, (ptrdiff_t)(last_new - first))) {
        tiku_cv_rows_free(v);
        return;
    }
    for (k = 0; k < TIKU_CV_WRAPS; k++) {
        if (v->wraps[k].line >= first && v->wraps[k].line <= last_old)
            v->wraps[k].line = -1;
        else if (v->wraps[k].line > last_old)
            v->wraps[k].line += delta;
    }
    /* Until they are counted, one changed line keeps the rows the changed
     * lines had between them, and several get one row each. */
    was = tiku_ed_lines_start(&v->row_map, last_old + 1) -
          tiku_ed_lines_start(&v->row_map, first);
    for (i = last_old; i > first; i--)
        tiku_ed_lines_remove(&v->row_map, first + 1);
    if (last_new > first) {
        tiku_ed_lines_shift(&v->row_map, first, last_new - first + 1 - was);
        one_rows(v, first + 1, last_new - first);
    }
    /* The changed lines are shown and unfolded. */
    for (i = first; i <= last_old; i++)
        v->folds -= (v->row_fold[i] & TIKU_CV_FOLDED) != 0;
    if (v->row_lines > last_old + 1) {
        memmove(&v->row_known[last_new + 1], &v->row_known[last_old + 1],
                (size_t)(v->row_lines - (last_old + 1)));
        memmove(&v->row_fold[last_new + 1], &v->row_fold[last_old + 1],
                (size_t)(v->row_lines - (last_old + 1)));
    }
    memset(&v->row_known[first], 0, (size_t)(last_new - first + 1));
    memset(&v->row_fold[first], 0, (size_t)(last_new - first + 1));
    v->row_lines = lines;
    if (v->unknown_from > first)
        v->unknown_from = first;
}

void
tiku_cv_rows_hide(tiku_codeview_t *v, int64_t first, int64_t last, int show)
{
    int64_t line;

    for (line = first; tiku_cv_rows_kept(v) && line <= last; line++) {
        int hidden = (v->row_fold[line] & TIKU_CV_HIDDEN) != 0;

        if (hidden == !show)
            continue;
        if (show) {
            v->row_fold[line] &= (unsigned char)~TIKU_CV_HIDDEN;
            (void)measure(v, line, 0, NULL);
        } else {
            tiku_ed_lines_shift(&v->row_map, line, -mapped(v, line));
            v->row_fold[line] |= TIKU_CV_HIDDEN;
            v->row_known[line] = 1;
        }
    }
}

/** @brief The first line not yet counted, or the line count. */
static int64_t
first_unknown(tiku_codeview_t *v)
{
    const unsigned char *at;

    if (v->unknown_from >= v->row_lines)
        return v->row_lines;
    at = memchr(v->row_known + v->unknown_from, 0,
                (size_t)(v->row_lines - v->unknown_from));
    v->unknown_from = at != NULL ? at - v->row_known : v->row_lines;
    return v->unknown_from;
}

/** @brief Count lines not yet counted, from the first, until @p budget
 *         bytes of them are; whether any were. */
static int
count_some(tiku_codeview_t *v, int64_t budget)
{
    int counted = 0;
    int64_t line;

    while (budget > 0 && (line = first_unknown(v)) < v->row_lines) {
        budget -= tiku_editdoc_line_start(v->doc, line + 1) -
                  tiku_editdoc_line_start(v->doc, line) + 1;
        (void)measure(v, line, 0, NULL);
        counted = 1;
    }
    return counted;
}

int
tiku_cv_rows_keep(tiku_codeview_t *v)
{
    v->folds++;
    tiku_cv_rows_ready(v);
    if (v->folds > 0)
        v->folds--;
    return tiku_cv_rows_kept(v);
}

int
tiku_cv_rows_tick(tiku_codeview_t *v, int64_t budget)
{
    return tiku_cv_rows_kept(v) && count_some(v, budget);
}

int
tiku_codeview_wrapping(tiku_codeview_t *view)
{
    return view != NULL && tiku_cv_rows_kept(view) &&
           first_unknown(view) < view->row_lines;
}

/** @brief Keep the first row inside the document, on a shown line no
 *         further down than where the last row is shown in the last whole
 *         row, and the text unscrolled across while it wraps; count the
 *         rows of the lines shown. */
static void
clamp_rows(tiku_codeview_t *v)
{
    int64_t line = tiku_cv_shown_line(v, v->row_lines - 1, 1), sub, shown;

    if (v->row_width > 0 || v->left < 0)
        v->left = 0;
    if (tiku_cv_shown_line(v, v->top, 0) != v->top) {
        v->top = tiku_cv_shown_line(v, v->top, 1);
        v->top_sub = 0;
    }
    sub = tiku_cv_line_rows(v, line) - 1;
    (void)tiku_cv_rows_step(v, &line, &sub,
                            1 - tiku_codeview_rows_visible(v));
    if (v->top > line || (v->top == line && v->top_sub > sub)) {
        v->top = line;
        v->top_sub = sub;
    }
    if (v->top < 0)
        v->top = 0;
    if (v->top_sub < 0)
        v->top_sub = 0;
    if (v->top_sub >= tiku_cv_line_rows(v, v->top))
        v->top_sub = tiku_cv_line_rows(v, v->top) - 1;
    shown = tiku_cv_line_rows(v, v->top) - v->top_sub;
    for (line = tiku_cv_shown_line(v, v->top + 1, 0);
         line >= 0 && shown <= tiku_codeview_rows_visible(v);
         line = tiku_cv_shown_line(v, line + 1, 0))
        shown += tiku_cv_line_rows(v, line);
}

void
tiku_cv_rows_ready(tiku_codeview_t *v)
{
    int64_t width = break_width(v);
    uint64_t serial = 0;
    int shown = (v->options & TIKU_CODEVIEW_ANNOTATIONS) &&
                tiku_editdoc_annotations(v->doc, &serial) > 0;
    uint32_t key;
    int i;

    if (width < 1 && !shown && v->folds == 0) {
        if (tiku_cv_rows_kept(v))
            tiku_cv_rows_free(v);
        v->top_sub = 0;
        return;
    }
    key = tiku_cv_metrics_key(v);
    if (tiku_cv_rows_kept(v) && v->row_lines != tiku_editdoc_lines(v->doc))
        tiku_cv_rows_free(v);
    if (!tiku_cv_rows_kept(v)) {
        if (!build(v)) {
            v->top_sub = 0;
            return;
        }
        v->top_sub = 0;
    } else if (v->row_width == width && v->row_key == key &&
               v->ann_shown == shown && v->ann_serial == serial) {
        clamp_rows(v);
        (void)count_some(v, READY_BYTES);
        return;
    }
    memset(v->row_known, 0, (size_t)v->row_lines);
    v->unknown_from = 0;
    for (i = 0; i < TIKU_CV_WRAPS; i++)
        v->wraps[i].line = -1;
    v->row_width = width;
    v->row_key = key;
    v->ann_shown = shown;
    v->ann_serial = serial;
    clamp_rows(v);
    (void)count_some(v, READY_BYTES);
}
