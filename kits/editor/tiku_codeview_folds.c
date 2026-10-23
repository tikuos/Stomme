/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_codeview_folds.c - regions the rules or a parser's levels say a
 * line heads, folded and unfolded in one view, the caret kept out of what
 * is folded and what it goes to shown, and the fold column beside the
 * line numbers.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_codeview_private.h"
#include "tiku_ui.h"

#include <string.h>

/* The box drawn in the fold column for a line heading a region. */
#define FOLD_BOX 9

/** @brief How a line moves the depth of regions: @p net, its depth at
 *         its end from its start, and @p dip, the lowest depth on the
 *         way. */
typedef struct {
    int net;
    int dip;
} depth_t;

/** @brief Whether byte @p b is part of a word. */
static int
word_byte(int b)
{
    return b == '_' || b == '$' || (b >= '0' && b <= '9') ||
           (b >= 'a' && b <= 'z') || (b >= 'A' && b <= 'Z');
}

/** @brief Whether the word of @p n bytes at @p pos is @p w, ASCII case
 *         aside. */
static int
word_is(const tiku_codeview_t *v, tiku_edit_pos_t pos, size_t n,
        const char *w)
{
    size_t i;

    if (strlen(w) != n)
        return 0;
    for (i = 0; i < n; i++) {
        int b = tiku_editdoc_byte_at(v->doc, pos + (tiku_edit_pos_t)i);
        int rule = (unsigned char)w[i];

        if (b >= 'a' && b <= 'z')
            b -= 'a' - 'A';
        if (rule >= 'a' && rule <= 'z')
            rule -= 'a' - 'A';
        if (b != rule)
            return 0;
    }
    return 1;
}

/** @brief The index in the rules' blocks of the word at @p pos, @p n
 *         bytes long: even an opener, odd a closer; -1 for neither. */
static int
block_word(const tiku_codeview_rules_t *r, const tiku_codeview_t *v,
           tiku_edit_pos_t pos, size_t n)
{
    int i;

    for (i = 0; r->blocks[i] != NULL; i++)
        if (word_is(v, pos, n, r->blocks[i]))
            return i;
    return -1;
}

/** @brief Count a change of depth into @p d. */
static void
step(depth_t *d, int by)
{
    d->net += by;
    if (d->net < d->dip)
        d->dip = d->net;
}

/** @brief How @p line moves the depth by the rules' brackets of levels:
 *         each opener in code one up, each closer one down. */
static depth_t
by_brackets(tiku_codeview_t *v, const tiku_codeview_rules_t *r,
            int64_t line)
{
    tiku_edit_pos_t s = tiku_editdoc_line_start(v->doc, line), p;
    tiku_edit_pos_t e = tiku_editdoc_line_end(v->doc, line);
    tiku_cv_inks_t k = tiku_cv_inks(v, line, e);
    depth_t d = { 0, 0 };

    for (p = s; p < e; p++) {
        int b = tiku_editdoc_byte_at(v->doc, p);

        if (b <= 0 || !tiku_cv_code_at(&k, p - s))
            continue;
        if (strchr(r->opens, b) != NULL)
            step(&d, 1);
        else if (strchr(r->closes, b) != NULL)
            step(&d, -1);
    }
    return d;
}

/** @brief How @p line moves the depth by the rules' block words: an
 *         opener starting a statement or ending the line's code goes one
 *         up, a closer starting a statement one down. */
static depth_t
by_words(tiku_codeview_t *v, const tiku_codeview_rules_t *r, int64_t line)
{
    tiku_edit_pos_t s = tiku_editdoc_line_start(v->doc, line), p = s;
    tiku_edit_pos_t e = tiku_editdoc_line_end(v->doc, line), last = -1;
    tiku_cv_inks_t k = tiku_cv_inks(v, line, e);
    depth_t d = { 0, 0 };
    int starts = 1, b, at;

    while (r->numbered && p < e && tiku_editdoc_byte_at(v->doc, p) >= '0' &&
           tiku_editdoc_byte_at(v->doc, p) <= '9')
        p++;
    for (; p < e; p++) {
        b = tiku_editdoc_byte_at(v->doc, p);
        if (!tiku_cv_code_at(&k, p - s))
            continue;
        if (b == ':') {
            starts = 1;
        } else if (word_byte(b)) {
            tiku_edit_pos_t w = p;

            while (p + 1 < e && word_byte(tiku_editdoc_byte_at(v->doc, p + 1)))
                p++;
            at = block_word(r, v, w, (size_t)(p + 1 - w));
            if (starts && at >= 0)
                step(&d, at % 2 ? -1 : 1);
            last = starts || at < 0 || at % 2 ? -1 : w;
            starts = 0;
        } else if (b != ' ' && b != '\t') {
            starts = 0;
            last = -1;
        }
    }
    if (last >= 0)
        step(&d, 1);
    return d;
}

/** @brief Columns of @p line's leading blanks, or -1 for a line of none
 *         but blanks. */
static int64_t
indent_of(tiku_codeview_t *v, int64_t line)
{
    tiku_edit_pos_t p = tiku_editdoc_line_start(v->doc, line);
    tiku_edit_pos_t e = tiku_editdoc_line_end(v->doc, line);
    int64_t col = 0;

    for (; p < e; p++) {
        int b = tiku_editdoc_byte_at(v->doc, p);

        if (b == '\t')
            col += v->tab_width - col % v->tab_width;
        else if (b == ' ')
            col++;
        else
            return col;
    }
    return -1;
}

/** @brief The last line of the region headed by @p line when it is told
 *         by indentation: the lines after it indented further, the blank
 *         ones at its end left out; -1 for none. */
static int64_t
indent_end(tiku_codeview_t *v, int64_t line)
{
    int64_t lines = tiku_editdoc_lines(v->doc), ind = indent_of(v, line);
    int64_t l, last = line, read = 0;

    for (l = line + 1; ind >= 0 && l < lines; l++) {
        int64_t i = indent_of(v, l);

        read += tiku_editdoc_line_start(v->doc, l + 1) -
                tiku_editdoc_line_start(v->doc, l);
        if (read > TIKU_CODEVIEW_BRACE_SCAN)
            return -1;
        if (i >= 0 && i <= ind)
            break;
        if (i >= 0)
            last = l;
    }
    return last > line ? last : -1;
}

/** @brief How @p line moves the depth of the rules' regions. */
static depth_t
depth_of(tiku_codeview_t *v, const tiku_codeview_rules_t *r, int64_t line)
{
    return r->blocks != NULL ? by_words(v, r, line) : by_brackets(v, r, line);
}

int64_t
tiku_codeview_fold_end(tiku_codeview_t *view, int64_t line)
{
    const tiku_codeview_rules_t *r;
    int64_t lines, l, read = 0;
    depth_t d;
    int open;

    if (view == NULL || line < 0 || line >= tiku_editdoc_lines(view->doc))
        return -1;
    if (view->fold_levels != NULL)
        return tiku_cv_supplied_fold(view, line);
    r = tiku_cv_rules(view);
    if (r->blocks == NULL && (r->opens == NULL || r->closes == NULL))
        return indent_end(view, line);
    d = depth_of(view, r, line);
    open = d.net - d.dip;
    lines = tiku_editdoc_lines(view->doc);
    for (l = line + 1; open > 0 && l < lines; l++) {
        d = depth_of(view, r, l);
        if (open + d.dip <= 0)
            return l - 1 > line ? l - 1 : -1;
        open += d.net;
        read += tiku_editdoc_line_start(view->doc, l + 1) -
                tiku_editdoc_line_start(view->doc, l);
        if (read > TIKU_CODEVIEW_BRACE_SCAN)
            return -1;
    }
    return -1;
}

int
tiku_cv_fold_head(tiku_codeview_t *v, int64_t line)
{
    const tiku_codeview_rules_t *r = tiku_cv_rules(v);
    int64_t next, lines = tiku_editdoc_lines(v->doc);
    depth_t d;

    if (v->fold_levels != NULL)
        return tiku_codeview_folded(v, line) ||
               (v->fold_generation == tiku_editdoc_generation(v->doc) &&
                line >= 0 && (uint64_t)line < v->fold_level_count &&
                (v->fold_levels[line] & TIKU_CODEVIEW_FOLD_HEADER) != 0);
    if (r->blocks != NULL || (r->opens != NULL && r->closes != NULL)) {
        d = depth_of(v, r, line);
        return d.net > d.dip;
    }
    for (next = line + 1; next < lines && indent_of(v, next) < 0; next++)
        ;
    return indent_of(v, line) >= 0 && next < lines &&
           indent_of(v, next) > indent_of(v, line);
}

int
tiku_codeview_folded(const tiku_codeview_t *view, int64_t line)
{
    return view != NULL && tiku_cv_rows_kept(view) && line >= 0 &&
           line < view->row_lines &&
           (view->row_fold[line] & TIKU_CV_FOLDED) != 0;
}

int
tiku_codeview_shown(const tiku_codeview_t *view, int64_t line)
{
    return view != NULL && line >= 0 &&
           line < tiku_editdoc_lines(view->doc) &&
           tiku_cv_shown_line(view, line, 0) == line;
}

/** @brief Whether either end of selection @p s is on lines @p first to
 *         @p last. */
static int
reaches(const tiku_codeview_t *v, const tiku_edit_selection_t *s,
        int64_t first, int64_t last)
{
    int64_t a = tiku_editdoc_line_of(v->doc, s->anchor);
    int64_t c = tiku_editdoc_line_of(v->doc, s->caret);

    return (a >= first && a <= last) || (c >= first && c <= last);
}

/** @brief Keep the carets out of lines @p first to @p last before folding:
 *         each selection reaching into them becomes the caret at the end
 *         of the line heading them; with no room for that, none moves and
 *         TIKU_EDIT_NO_MEMORY is returned. */
static tiku_edit_status_t
caret_out(tiku_codeview_t *v, int64_t first, int64_t last)
{
    size_t n, main, i, in = 0;
    const tiku_edit_selection_t *s = tiku_cv_selections(v, &n, &main);
    tiku_edit_pos_t end = tiku_editdoc_line_end(v->doc, first - 1);
    tiku_edit_selection_t *to;
    tiku_edit_status_t st;

    if (s == NULL)
        return TIKU_EDIT_NO_MEMORY;
    for (i = 0; i < n; i++)
        in += reaches(v, &s[i], first, last);
    if (in == 0)
        return TIKU_EDIT_OK;
    to = tiku_ed_alloc(n * sizeof *to);
    if (to == NULL)
        return TIKU_EDIT_NO_MEMORY;
    for (i = 0; i < n; i++) {
        to[i] = s[i];
        if (reaches(v, &s[i], first, last))
            to[i].anchor = to[i].caret = end;
    }
    st = tiku_codeview_select_many(v, to, n, main);
    tiku_ed_free(to);
    return st;
}

/** @brief Fold the region @p line heads; TIKU_EDIT_NOTHING when the line
 *         is hidden, folded already or heads none. */
static tiku_edit_status_t
fold(tiku_codeview_t *v, int64_t line)
{
    int64_t end;
    tiku_edit_status_t st;

    if (!tiku_codeview_shown(v, line) || tiku_codeview_folded(v, line))
        return TIKU_EDIT_NOTHING;
    end = tiku_codeview_fold_end(v, line);
    if (end < 0)
        return TIKU_EDIT_NOTHING;
    if (!tiku_cv_rows_keep(v))
        return TIKU_EDIT_NO_MEMORY;
    st = caret_out(v, line + 1, end);
    if (st != TIKU_EDIT_OK)
        return st;
    v->row_fold[line] |= TIKU_CV_FOLDED;
    v->folds++;
    tiku_cv_rows_hide(v, line + 1, end, 0);
    return TIKU_EDIT_OK;
}

/** @brief Unfold the region @p line heads, showing the folded lines after
 *         it but those of the folded regions inside it; a line in a
 *         folded region is only marked unfolded, its lines shown with
 *         that region's. */
static tiku_edit_status_t
unfold(tiku_codeview_t *v, int64_t line)
{
    int64_t l, lines = tiku_editdoc_lines(v->doc), end;

    if (!tiku_codeview_folded(v, line))
        return TIKU_EDIT_NOTHING;
    v->row_fold[line] &= (unsigned char)~TIKU_CV_FOLDED;
    v->folds--;
    if (v->row_fold[line] & TIKU_CV_HIDDEN)
        return TIKU_EDIT_OK;
    for (l = line + 1; l < lines && (v->row_fold[l] & TIKU_CV_HIDDEN); l++) {
        tiku_cv_rows_hide(v, l, l, 1);
        end = v->row_fold[l] & TIKU_CV_FOLDED ? tiku_codeview_fold_end(v, l)
                                              : -1;
        if (end > l)
            l = end;
    }
    return TIKU_EDIT_OK;
}

tiku_edit_status_t
tiku_codeview_fold(tiku_codeview_t *view, int64_t line, int open)
{
    tiku_edit_status_t st;

    if (view == NULL)
        return TIKU_EDIT_INVALID;
    if (line < 0 || line >= tiku_editdoc_lines(view->doc))
        return TIKU_EDIT_INVALID_RANGE;
    tiku_cv_rows_ready(view);
    st = open ? unfold(view, line) : fold(view, line);
    if (st == TIKU_EDIT_OK)
        tiku_cv_clamp(view);
    return st;
}

tiku_edit_status_t
tiku_codeview_fold_all(tiku_codeview_t *view, int open)
{
    int64_t line;
    int any = 0;

    if (view == NULL)
        return TIKU_EDIT_INVALID;
    tiku_cv_rows_ready(view);
    for (line = tiku_editdoc_lines(view->doc) - 1; line >= 0; line--) {
        tiku_edit_status_t st = open ? unfold(view, line) :
                                tiku_cv_fold_head(view, line) ?
                                fold(view, line) : TIKU_EDIT_NOTHING;

        if (st == TIKU_EDIT_NO_MEMORY)
            return st;
        any |= st == TIKU_EDIT_OK;
    }
    if (open && tiku_cv_rows_kept(view))
        tiku_cv_rows_hide(view, 0, view->row_lines - 1, 1);
    tiku_cv_clamp(view);
    return any ? TIKU_EDIT_OK : TIKU_EDIT_NOTHING;
}

/** @brief Whether @p line, which headed a folded region, heads one still,
 *         and where it ends in @p end. */
static int
heads_still(tiku_codeview_t *v, int64_t line, int64_t *end)
{
    *end = tiku_cv_fold_head(v, line) ? tiku_codeview_fold_end(v, line) : -1;
    return *end > line;
}

void
tiku_cv_folds_refit(tiku_codeview_t *v)
{
    int64_t line, end, until = -1;

    if (!tiku_cv_rows_kept(v) || v->folds == 0)
        return;
    for (line = 0; line < v->row_lines; line++) {
        int folded = (v->row_fold[line] & TIKU_CV_FOLDED) != 0;

        if (folded) {
            v->row_fold[line] &= (unsigned char)~TIKU_CV_FOLDED;
            v->folds--;
        }
        tiku_cv_rows_hide(v, line, line, line > until);
        if (!folded || !heads_still(v, line, &end))
            continue;
        if (line <= until ? end > until
                          : caret_out(v, line + 1, end) != TIKU_EDIT_OK)
            continue;
        v->row_fold[line] |= TIKU_CV_FOLDED;
        v->folds++;
        if (line > until)
            until = end;
    }
    tiku_cv_clamp(v);
}

/** @brief The line heading the innermost region @p line is in: @p line
 *         itself when it heads one, else the nearest shown line above
 *         whose region reaches it; -1 for none.  The search stops at a
 *         head more than 4096 lines up whose region does not reach it. */
static int64_t
region_at(tiku_codeview_t *v, int64_t line)
{
    int64_t head;

    if (tiku_cv_fold_head(v, line) && tiku_codeview_fold_end(v, line) > line)
        return line;
    for (head = line - 1; head >= 0; head--) {
        int64_t end;

        if (!tiku_codeview_shown(v, head) || !tiku_cv_fold_head(v, head))
            continue;
        end = tiku_codeview_fold_end(v, head);
        if (end >= line)
            return head;
        if (line - head > 4096)
            break;
    }
    return -1;
}

tiku_edit_status_t
tiku_codeview_fold_at(tiku_codeview_t *view, int open)
{
    tiku_edit_pos_t anchor, caret;
    int64_t line, head;

    if (view == NULL ||
        tiku_codeview_selection(view, &anchor, &caret) != TIKU_EDIT_OK)
        return TIKU_EDIT_INVALID;
    line = tiku_editdoc_line_of(view->doc, caret);
    head = open ? line : region_at(view, line);
    if (head < 0)
        return TIKU_EDIT_NOTHING;
    return tiku_codeview_fold(view, head, open);
}

void
tiku_cv_fold_reveal(tiku_codeview_t *v, int64_t line)
{
    while (!tiku_codeview_shown(v, line)) {
        int64_t head = tiku_cv_shown_line(v, line, 1);

        if (head < 0 || unfold(v, head) != TIKU_EDIT_OK)
            break;
    }
}

void
tiku_cv_fold_changed(tiku_codeview_t *v, int64_t first, int64_t last)
{
    int64_t l, lines = tiku_editdoc_lines(v->doc);

    if (!tiku_cv_rows_kept(v))
        return;
    if (first > 0) {
        tiku_cv_fold_reveal(v, first - 1);
        (void)unfold(v, first - 1);
    }
    for (l = last + 1; l < lines && (v->row_fold[l] & TIKU_CV_HIDDEN); l++) {
        if (v->row_fold[l] & TIKU_CV_FOLDED) {
            v->row_fold[l] &= (unsigned char)~TIKU_CV_FOLDED;
            v->folds--;
        }
        tiku_cv_rows_hide(v, l, l, 1);
    }
}

/** @brief The fold column's left edge on the surface. */
static int
column_x(const tiku_codeview_t *v)
{
    return v->bounds.x + tiku_cv_gutter(v) - TIKU_CV_FOLD_W;
}

void
tiku_cv_folds_paint(tiku_codeview_t *v, tiku_surface_t *s, int64_t line,
                    int y)
{
    tiku_rgb_t ink = tiku_ink(TIKU_INK_FAINT);
    int step = tiku_codeview_row_height(v);
    tiku_rect_t box;

    if (!(v->options & TIKU_CODEVIEW_FOLDS) || !tiku_cv_fold_head(v, line))
        return;
    box.x = column_x(v) + (TIKU_CV_FOLD_W - FOLD_BOX) / 2;
    box.y = y + (step - FOLD_BOX) / 2;
    box.w = FOLD_BOX;
    box.h = FOLD_BOX;
    tiku_fill(s, box, TIKU_C_DOC);
    tiku_frame(s, box, ink);
    tiku_hline(s, box.x + 2, box.y + FOLD_BOX / 2, FOLD_BOX - 4, ink);
    if (tiku_codeview_folded(v, line))
        tiku_vline(s, box.x + FOLD_BOX / 2, box.y + 2, FOLD_BOX - 4, ink);
}

void
tiku_cv_fold_mark(tiku_codeview_t *v, tiku_surface_t *s, int x, int y)
{
    const tiku_font_t *f = tiku_cv_font(v);
    tiku_rect_t box;

    box.x = x + 4;
    box.y = y + 1;
    box.w = tiku_text_width(f, "...") + 4;
    box.h = f->height;
    tiku_frame(s, box, tiku_ink(TIKU_INK_FAINT));
    tiku_text(s, f, box.x + 2, y + f->ascent, "...",
              tiku_ink(TIKU_INK_FAINT));
}

int
tiku_cv_folds_press(tiku_codeview_t *v, const tiku_event_t *e)
{
    tiku_cv_row_t r;

    if (!(v->options & TIKU_CODEVIEW_FOLDS) || e->x < column_x(v) ||
        e->x >= column_x(v) + TIKU_CV_FOLD_W || e->y < tiku_cv_text_y(v) ||
        e->y >= v->bounds.y + v->bounds.h)
        return 0;
    tiku_cv_rows_ready(v);
    (void)tiku_cv_row_under(v, e->y, &r);
    if (r.sub != 0 || r.ann >= 0 || !tiku_cv_fold_head(v, r.line))
        return 1;
    (void)tiku_codeview_fold(v, r.line, tiku_codeview_folded(v, r.line));
    return 1;
}
