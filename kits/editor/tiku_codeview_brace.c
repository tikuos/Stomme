/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_codeview_brace.c - the bracket by the caret and the one it pairs
 * with: found by counting brackets of its kind outward from it, passing
 * over quotes and remarks as the line's colouring marks them, and drawn
 * behind the text.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_codeview_private.h"

#include <string.h>

int
tiku_cv_code_at(tiku_cv_inks_t *k, int64_t at)
{
    tiku_ink_t role;

    while (k->i < k->n && at >= k->start + k->runs[k->i].len) {
        k->start += k->runs[k->i].len;
        k->i++;
    }
    while (k->i > 0 && at < k->start) {
        k->i--;
        k->start -= k->runs[k->i].len;
    }
    if (k->i >= k->n || at < k->start)
        return 1;
    role = tiku_cv_style_role(k->view, k->runs[k->i].ink);
    return role != TIKU_INK_STR && role != TIKU_INK_REMARK;
}

tiku_cv_inks_t
tiku_cv_inks(tiku_codeview_t *v, int64_t line, tiku_edit_pos_t upto)
{
    tiku_cv_inks_t k;

    memset(&k, 0, sizeof k);
    k.view = v;
    k.runs = tiku_cv_style_line(v, line, upto, &k.n);
    if (k.runs == NULL)
        k.n = 0;
    return k;
}

/**
 * @brief Scan from @p from, forward or back, for the @p want that pairs
 *        with a bracket of kind @p same right behind the scan's start.
 *
 * Each @p same met on the way must be closed first.  @return the
 * position, -1 past either end of the document, -2 once
 * TIKU_CODEVIEW_BRACE_SCAN bytes have been read.
 */
static tiku_edit_pos_t
scan(tiku_codeview_t *v, tiku_edit_pos_t from, int forward, int same,
     int want)
{
    int64_t line, lines = tiku_editdoc_lines(v->doc);
    tiku_edit_pos_t read = 0, p, s, e;
    int depth = 0;

    if (from < 0 || from >= tiku_editdoc_length(v->doc))
        return -1;
    line = tiku_editdoc_line_of(v->doc, from);
    for (;;) {
        tiku_cv_inks_t k = {0};
        int coloured = 0;

        s = tiku_editdoc_line_start(v->doc, line);
        e = tiku_editdoc_line_end(v->doc, line);
        p = forward ? (from > s ? from : s) : (from < e ? from : e - 1);
        for (; forward ? p < e : p >= s; p += forward ? 1 : -1) {
            int b = tiku_editdoc_byte_at(v->doc, p);

            if (++read > TIKU_CODEVIEW_BRACE_SCAN)
                return -2;
            if (b != same && b != want)
                continue;
            if (!coloured) {
                k = tiku_cv_inks(v, line, e);
                coloured = 1;
            }
            if (!tiku_cv_code_at(&k, p - s))
                continue;
            if (b == same)
                depth++;
            else if (depth == 0)
                return p;
            else
                depth--;
        }
        line += forward ? 1 : -1;
        if (line < 0 || line >= lines)
            return -1;
        from = forward ? tiku_editdoc_line_start(v->doc, line)
                       : tiku_editdoc_line_end(v->doc, line) - 1;
    }
}

/** @brief The index of byte @p b among the rules' whole pairs of
 *         brackets, or -1. */
static int
bracket_index(const tiku_codeview_rules_t *r, int b)
{
    size_t i, n;

    if (r->brackets == NULL || b <= 0)
        return -1;
    n = strlen(r->brackets) & ~(size_t)1;
    for (i = 0; i < n; i++)
        if ((unsigned char)r->brackets[i] == b)
            return (int)i;
    return -1;
}

/** @brief Whether the byte at @p pos is one of the rules' brackets, in
 *         code; its index in them in @p index. */
static int
bracket_at(tiku_codeview_t *v, const tiku_codeview_rules_t *r,
           tiku_edit_pos_t pos, int *index)
{
    int64_t line;
    tiku_cv_inks_t k;

    if (pos < 0 || pos >= tiku_editdoc_length(v->doc))
        return 0;
    *index = bracket_index(r, tiku_editdoc_byte_at(v->doc, pos));
    if (*index < 0)
        return 0;
    line = tiku_editdoc_line_of(v->doc, pos);
    k = tiku_cv_inks(v, line, pos + 1);
    return tiku_cv_code_at(&k, pos - tiku_editdoc_line_start(v->doc, line));
}

tiku_edit_pos_t
tiku_cv_brace(tiku_codeview_t *v, tiku_edit_pos_t *match)
{
    const tiku_codeview_rules_t *r = tiku_cv_rules(v);
    uint64_t gen = tiku_editdoc_generation(v->doc);
    tiku_edit_pos_t anchor, caret, at = -1, m = -1;
    int index = -1;

    *match = -1;
    if (tiku_codeview_selection(v, &anchor, &caret) != TIKU_EDIT_OK ||
        anchor != caret)
        return -1;
    if (v->brace_gen == gen && v->brace_caret == caret &&
        v->brace_rules == (const void *)r) {
        *match = v->brace_match;
        return v->brace_at;
    }
    if (bracket_at(v, r, caret - 1, &index))
        at = caret - 1;
    else if (bracket_at(v, r, caret, &index))
        at = caret;
    if (at >= 0) {
        int same = (unsigned char)r->brackets[index];
        int want = (unsigned char)r->brackets[index ^ 1];

        m = index % 2 == 0 ? scan(v, at + 1, 1, same, want)
                           : scan(v, at - 1, 0, same, want);
    }
    v->brace_gen = gen;
    v->brace_caret = caret;
    v->brace_rules = r;
    v->brace_at = at;
    v->brace_match = m;
    *match = m;
    return at;
}

tiku_edit_pos_t
tiku_cv_open_before(tiku_codeview_t *v, tiku_edit_pos_t pos, char closer)
{
    const tiku_codeview_rules_t *r = tiku_cv_rules(v);
    int index = bracket_index(r, (unsigned char)closer);

    if (index < 0 || index % 2 == 0)
        return -1;
    return scan(v, pos - 1, 0, (unsigned char)closer,
                (unsigned char)r->brackets[index - 1]);
}

tiku_edit_pos_t
tiku_codeview_bracket(tiku_codeview_t *view, tiku_edit_pos_t *match)
{
    tiku_edit_pos_t m = -1, at;

    if (view == NULL) {
        if (match != NULL)
            *match = -1;
        return -1;
    }
    at = tiku_cv_brace(view, &m);
    if (match != NULL)
        *match = m;
    return at;
}

tiku_edit_status_t
tiku_codeview_goto_bracket(tiku_codeview_t *view, int extend)
{
    tiku_edit_pos_t at, m, anchor, caret;
    tiku_edit_status_t st;

    if (view == NULL)
        return TIKU_EDIT_INVALID;
    at = tiku_cv_brace(view, &m);
    if (at < 0 || m < 0)
        return TIKU_EDIT_NOTHING;
    if (m > at) {
        anchor = at;
        caret = m + 1;
    } else {
        anchor = at + 1;
        caret = m;
    }
    st = tiku_codeview_select(view, extend ? anchor : caret, caret);
    if (st == TIKU_EDIT_OK)
        tiku_codeview_reveal(view);
    return st;
}

/** @brief Fill the cell of the bracket at @p pos when its row is shown. */
static void
box(tiku_codeview_t *v, tiku_surface_t *s, tiku_edit_pos_t pos,
    int64_t rows, tiku_rgb_t colour)
{
    int64_t row, x0, x1;
    int step = tiku_codeview_row_height(v);
    tiku_cv_row_t w;
    tiku_rect_t r;

    tiku_cv_row_of(v, pos, 0, &w);
    row = tiku_cv_shown_row(v, w.line, w.sub);
    if (row < 0 || row >= rows || !tiku_codeview_shown(v, w.line))
        return;
    x0 = tiku_cv_x_of(v, w.line, pos) - w.x0 - v->left;
    x1 = tiku_cv_x_of(v, w.line, pos + 1) - w.x0 - v->left;
    if (x1 <= 0 || x0 > tiku_cv_text_w(v))
        return;
    r.x = tiku_cv_text_x(v) + (int)x0;
    r.y = tiku_cv_text_y(v) + (int)row * step;
    r.w = (int)(x1 - x0);
    r.h = step;
    tiku_fill(s, r, colour);
}

void
tiku_cv_brace_paint(tiku_codeview_t *v, tiku_surface_t *s, int64_t rows)
{
    tiku_edit_pos_t at, m;

    if (!(v->options & TIKU_CODEVIEW_BRACES))
        return;
    at = tiku_cv_brace(v, &m);
    if (at < 0 || m == -2)
        return;
    if (m < 0) {
        box(v, s, at, rows, tiku_dim(tiku_ink(TIKU_INK_WRONG), TIKU_C_DOC));
        return;
    }
    box(v, s, at, rows, tiku_dim(TIKU_C_SELECT, TIKU_C_DOC));
    box(v, s, m, rows, tiku_dim(TIKU_C_SELECT, TIKU_C_DOC));
}
