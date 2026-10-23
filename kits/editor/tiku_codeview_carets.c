/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_codeview_carets.c - a view's several selections: read in order,
 * added and dropped, one change across all of them as one transaction,
 * the clipboard across them, and the rectangle an Option drag makes over
 * the rows.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_codeview_private.h"
#include "tiku_editdoc_private.h"

#include <string.h>

int
tiku_codeview_has_selection(const tiku_codeview_t *view)
{
    const tiku_ed_view_t *v;
    size_t i;

    if (view == NULL || (v = tiku_ed_view_get(view->doc, view->view)) == NULL)
        return 0;
    if (v->anchor != v->caret)
        return 1;
    for (i = 0; i < v->nmore; i++)
        if (v->more[i].anchor != v->more[i].caret)
            return 1;
    return 0;
}

size_t
tiku_codeview_selections(const tiku_codeview_t *view,
                         tiku_edit_selection_t *out, size_t cap, size_t *main)
{
    if (view == NULL)
        return 0;
    return tiku_editdoc_view_selections(view->doc, view->view, out, cap,
                                        main);
}

tiku_edit_status_t
tiku_codeview_select_many(tiku_codeview_t *view,
                          const tiku_edit_selection_t *selections,
                          size_t count, size_t main)
{
    if (view == NULL)
        return TIKU_EDIT_INVALID;
    view->goal = -1;
    view->upstream = -1;
    return tiku_editdoc_view_select_many(view->doc, view->view, selections,
                                         count, main);
}

int
tiku_cv_many(const tiku_codeview_t *v)
{
    return tiku_editdoc_view_selections(v->doc, v->view, NULL, 0, NULL) > 1;
}

const tiku_edit_selection_t *
tiku_cv_selections(tiku_codeview_t *v, size_t *n, size_t *main)
{
    size_t count = tiku_editdoc_view_selections(v->doc, v->view, NULL, 0,
                                                NULL);

    if (count > v->sels_cap && count <= SIZE_MAX / sizeof *v->sels) {
        tiku_edit_selection_t *grown =
            tiku_ed_realloc(v->sels, count * sizeof *grown);

        if (grown != NULL) {
            v->sels = grown;
            v->sels_cap = count;
        }
    }
    if (count == 0 || count > v->sels_cap) {
        *n = 0;
        *main = 0;
        return NULL;
    }
    *n = tiku_editdoc_view_selections(v->doc, v->view, v->sels, v->sels_cap,
                                      main);
    return v->sels;
}

/** @brief The selections in order and @p one after them, @p n in all, in
 *         a copy the caller frees; NULL when there is no room. */
static tiku_edit_selection_t *
plus_one(tiku_codeview_t *v, tiku_edit_selection_t one, size_t *n)
{
    size_t count, main;
    const tiku_edit_selection_t *s = tiku_cv_selections(v, &count, &main);
    tiku_edit_selection_t *all;

    if (s == NULL || count >= SIZE_MAX / sizeof *all)
        return NULL;
    all = tiku_ed_alloc((count + 1) * sizeof *all);
    if (all == NULL)
        return NULL;
    memcpy(all, s, count * sizeof *all);
    all[count] = one;
    *n = count + 1;
    return all;
}

tiku_edit_status_t
tiku_codeview_add_selection(tiku_codeview_t *view, tiku_edit_pos_t anchor,
                            tiku_edit_pos_t caret)
{
    tiku_edit_selection_t one, *all;
    tiku_edit_status_t st;
    size_t n = 0;

    if (view == NULL)
        return TIKU_EDIT_INVALID;
    one.anchor = anchor;
    one.caret = caret;
    all = plus_one(view, one, &n);
    if (all == NULL)
        return TIKU_EDIT_NO_MEMORY;
    st = tiku_codeview_select_many(view, all, n, n - 1);
    tiku_ed_free(all);
    return st;
}

tiku_edit_status_t
tiku_codeview_keep_main(tiku_codeview_t *view)
{
    tiku_edit_pos_t anchor, caret;

    if (view == NULL ||
        tiku_codeview_selection(view, &anchor, &caret) != TIKU_EDIT_OK)
        return TIKU_EDIT_INVALID;
    if (!tiku_cv_many(view))
        return TIKU_EDIT_NOTHING;
    return tiku_codeview_select(view, anchor, caret);
}

tiku_edit_status_t
tiku_codeview_carets_to_line_ends(tiku_codeview_t *view)
{
    tiku_edit_pos_t anchor, caret, lo, hi;
    tiku_edit_selection_t *ends;
    tiku_edit_status_t st;
    int64_t first, last, line;
    size_t n;

    if (view == NULL ||
        tiku_codeview_selection(view, &anchor, &caret) != TIKU_EDIT_OK)
        return TIKU_EDIT_INVALID;
    lo = anchor < caret ? anchor : caret;
    hi = anchor < caret ? caret : anchor;
    first = tiku_editdoc_line_of(view->doc, lo);
    last = tiku_editdoc_line_of(view->doc, hi);
    if (last > first && hi == tiku_editdoc_line_start(view->doc, last))
        last--;
    if ((uint64_t)(last - first) >= SIZE_MAX / sizeof *ends)
        return TIKU_EDIT_LIMIT;
    n = (size_t)(last - first + 1);
    ends = tiku_ed_alloc(n * sizeof *ends);
    if (ends == NULL)
        return TIKU_EDIT_NO_MEMORY;
    for (line = first; line <= last; line++)
        ends[line - first].anchor = ends[line - first].caret =
            tiku_editdoc_line_end(view->doc, line);
    st = tiku_codeview_select_many(view, ends, n, n - 1);
    tiku_ed_free(ends);
    if (st == TIKU_EDIT_OK)
        tiku_codeview_reveal(view);
    return st;
}

/** @brief The byte before piece @p i's caret once all the pieces are in:
 *         from its own text, else from the pieces it touches before it,
 *         else from the text before them. */
static int
byte_before(const tiku_codeview_t *v, const tiku_cv_piece_t *p, size_t i)
{
    if (p[i].caret > 0)
        return (unsigned char)p[i].text[p[i].caret - 1];
    while (i > 0 && p[i - 1].end == p[i].start) {
        i--;
        if (p[i].length > 0)
            return (unsigned char)p[i].text[p[i].length - 1];
    }
    return tiku_editdoc_byte_at(v->doc, p[i].start - 1);
}

/** @brief The byte after piece @p i's caret once all @p n pieces are in. */
static int
byte_after(const tiku_codeview_t *v, const tiku_cv_piece_t *p, size_t n,
           size_t i)
{
    if (p[i].caret < p[i].length)
        return (unsigned char)p[i].text[p[i].caret];
    while (i + 1 < n && p[i + 1].start == p[i].end) {
        i++;
        if (p[i].length > 0)
            return (unsigned char)p[i].text[0];
    }
    return tiku_editdoc_byte_at(v->doc, p[i].end);
}

tiku_edit_status_t
tiku_cv_apply_pieces(tiku_codeview_t *v, const tiku_cv_piece_t *p, size_t n,
                     size_t main, int typing)
{
    tiku_edit_replace_t *r;
    tiku_edit_selection_t *after;
    tiku_edit_transaction_t t;
    tiku_edit_status_t st;
    int64_t delta = 0;
    size_t i;

    if (n == 0 || n > SIZE_MAX / sizeof *r)
        return TIKU_EDIT_LIMIT;
    r = tiku_ed_alloc(n * sizeof *r);
    after = tiku_ed_alloc(n * sizeof *after);
    if (r == NULL || after == NULL) {
        tiku_ed_free(r);
        tiku_ed_free(after);
        return TIKU_EDIT_NO_MEMORY;
    }
    for (i = 0; i < n; i++) {
        tiku_edit_pos_t c = p[i].start + delta + (tiku_edit_pos_t)p[i].caret;

        /* A caret that would fall between a CR and its LF goes before the
         * CR. */
        if (byte_before(v, p, i) == '\r' && byte_after(v, p, n, i) == '\n')
            c--;
        r[i].range.start = p[i].start;
        r[i].range.end = p[i].end;
        r[i].text = p[i].length > 0 ? p[i].text : NULL;
        r[i].length = p[i].length;
        after[i].anchor = after[i].caret = c;
        delta += (int64_t)p[i].length - (p[i].end - p[i].start);
    }
    memset(&t, 0, sizeof t);
    t.edits = r;
    t.count = n;
    t.view = v->view;
    t.select = 1;
    t.selections = after;
    t.nselections = n;
    t.main = main;
    t.flags = typing ? TIKU_EDIT_TYPING : 0u;
    st = tiku_editdoc_apply(v->doc, &t);
    tiku_ed_free(r);
    tiku_ed_free(after);
    if (st == TIKU_EDIT_OK)
        tiku_codeview_reveal(v);
    return st;
}

/** @brief Where selection @p s starts. */
static tiku_edit_pos_t
lo_of(const tiku_edit_selection_t *s)
{
    return s->anchor < s->caret ? s->anchor : s->caret;
}

/** @brief Where selection @p s ends. */
static tiku_edit_pos_t
hi_of(const tiku_edit_selection_t *s)
{
    return s->anchor < s->caret ? s->caret : s->anchor;
}

/** @brief The view's selections as pieces with no text, each replacing
 *         its selection, in an array the caller frees; NULL when there is
 *         no room. */
static tiku_cv_piece_t *
pieces_of(tiku_codeview_t *v, size_t *n, size_t *main)
{
    const tiku_edit_selection_t *s = tiku_cv_selections(v, n, main);
    tiku_cv_piece_t *p;
    size_t i;

    if (s == NULL || *n > SIZE_MAX / sizeof *p)
        return NULL;
    p = tiku_ed_alloc(*n * sizeof *p);
    if (p == NULL)
        return NULL;
    memset(p, 0, *n * sizeof *p);
    for (i = 0; i < *n; i++) {
        p[i].start = lo_of(&s[i]);
        p[i].end = hi_of(&s[i]);
    }
    return p;
}

tiku_edit_status_t
tiku_cv_insert_each(tiku_codeview_t *v, const char *text, size_t length,
                    int typing)
{
    size_t n, main, i;
    tiku_cv_piece_t *p = pieces_of(v, &n, &main);
    tiku_edit_status_t st;

    if (p == NULL)
        return TIKU_EDIT_NO_MEMORY;
    for (i = 0; i < n; i++) {
        p[i].text = text;
        p[i].length = length;
        p[i].caret = length;
    }
    st = tiku_cv_apply_pieces(v, p, n, main, typing);
    tiku_ed_free(p);
    return st;
}

tiku_edit_status_t
tiku_cv_delete_each(tiku_codeview_t *v, int forward, int words)
{
    size_t n, main, i;
    tiku_cv_piece_t *p = pieces_of(v, &n, &main);
    tiku_edit_status_t st;

    if (p == NULL)
        return TIKU_EDIT_NO_MEMORY;
    for (i = 0; i < n; i++) {
        if (p[i].start == p[i].end && forward)
            p[i].end = words ? tiku_cv_word_right(v, p[i].start)
                             : tiku_editdoc_next(v->doc, p[i].start);
        else if (p[i].start == p[i].end)
            p[i].start = words ? tiku_cv_word_left(v, p[i].end)
                               : tiku_editdoc_prev(v->doc, p[i].end);
        /* A delete stops where the piece before it ends. */
        if (i > 0 && p[i].start < p[i - 1].end)
            p[i].start = p[i - 1].end;
        if (p[i].end < p[i].start)
            p[i].end = p[i].start;
    }
    st = tiku_cv_apply_pieces(v, p, n, main, !words);
    tiku_ed_free(p);
    return st;
}

/** @brief Where each of the @p n lines of @p text starts and ends, its
 *         line break left out, in pairs the caller frees.  A line break
 *         is LF, CR or CRLF. */
static size_t *
line_starts(const char *text, size_t length, size_t *n)
{
    size_t i, k = 0, count = 1, *at;

    for (i = 0; i < length; i++)
        if (text[i] == '\n' ||
            (text[i] == '\r' && (i + 1 == length || text[i + 1] != '\n')))
            count++;
    if (count > SIZE_MAX / sizeof *at / 2)
        return NULL;
    at = tiku_ed_alloc(2 * count * sizeof *at);
    if (at == NULL)
        return NULL;
    at[0] = 0;
    for (i = 0; i < length; i++) {
        if (text[i] == '\r' && i + 1 < length && text[i + 1] == '\n')
            continue;
        if (text[i] == '\n' || text[i] == '\r') {
            at[2 * k + 1] = (text[i] == '\n' && i > 0 &&
                             text[i - 1] == '\r') ? i - 1 : i;
            at[2 * ++k] = i + 1;
        }
    }
    at[2 * k + 1] = length;
    *n = count;
    return at;
}

tiku_edit_status_t
tiku_cv_paste_each(tiku_codeview_t *v, const char *text, size_t length)
{
    size_t n, main, i, lines = 0, *at;
    tiku_cv_piece_t *p = pieces_of(v, &n, &main);
    tiku_edit_status_t st;

    if (p == NULL)
        return TIKU_EDIT_NO_MEMORY;
    at = line_starts(text, length, &lines);
    if (at == NULL) {
        tiku_ed_free(p);
        return TIKU_EDIT_NO_MEMORY;
    }
    /* As many lines as selections: one line each; else all of it each. */
    for (i = 0; i < n; i++) {
        p[i].text = lines == n ? text + at[2 * i] : text;
        p[i].length = lines == n ? at[2 * i + 1] - at[2 * i] : length;
        p[i].caret = p[i].length;
    }
    st = tiku_cv_apply_pieces(v, p, n, main, 0);
    tiku_ed_free(at);
    tiku_ed_free(p);
    return st;
}

/** @brief Bytes the text of the @p n selections takes, a line break of
 *         @p nl bytes between each two that hold any; 0 when none does. */
static size_t
joined_size(const tiku_edit_selection_t *s, size_t n, size_t nl)
{
    size_t i, total = 0;

    for (i = 0; i < n; i++)
        if (lo_of(&s[i]) < hi_of(&s[i]))
            total += (size_t)(hi_of(&s[i]) - lo_of(&s[i])) +
                     (total > 0 ? nl : 0);
    return total;
}

/** @brief Read the text of the @p n selections into @p text, @p cap bytes
 *         of room, the view's line break between each two that hold any;
 *         the bytes written in @p len. */
static tiku_edit_status_t
read_joined(tiku_codeview_t *v, const tiku_edit_selection_t *s, size_t n,
            char *text, size_t cap, size_t *len)
{
    size_t i, nl = strlen(v->newline), got = 0;
    tiku_edit_status_t st = TIKU_EDIT_OK;
    tiku_edit_range_t range;

    *len = 0;
    for (i = 0; i < n && st == TIKU_EDIT_OK; i++) {
        if (lo_of(&s[i]) == hi_of(&s[i]))
            continue;
        if (*len > 0) {
            memcpy(text + *len, v->newline, nl);
            *len += nl;
        }
        range.start = lo_of(&s[i]);
        range.end = hi_of(&s[i]);
        st = tiku_editdoc_read(v->doc, range, text + *len, cap - *len, &got);
        *len += got;
    }
    return st;
}

tiku_edit_status_t
tiku_cv_copy_each(tiku_codeview_t *v)
{
    size_t n, main, total, len = 0;
    const tiku_edit_selection_t *s = tiku_cv_selections(v, &n, &main);
    tiku_edit_status_t st;
    char *text;
    int taken;

    if (s == NULL)
        return TIKU_EDIT_NO_MEMORY;
    total = joined_size(s, n, strlen(v->newline));
    if (total == 0)
        return TIKU_EDIT_NOTHING;
    text = tiku_ed_alloc(total + 1);
    if (text == NULL)
        return TIKU_EDIT_NO_MEMORY;
    st = read_joined(v, s, n, text, total + 1, &len);
    taken = st == TIKU_EDIT_OK &&
            v->host.clip_set(v->host.ctx, text, len) == 0;
    tiku_ed_free(text);
    if (st != TIKU_EDIT_OK)
        return st;
    return taken ? TIKU_EDIT_OK : TIKU_EDIT_LIMIT;
}

tiku_edit_status_t
tiku_cv_rectangle(tiku_codeview_t *v, int64_t line, int64_t sub, int64_t x)
{
    int64_t at_line = v->rect_line, at_sub = v->rect_sub;
    int down = line > at_line || (line == at_line && sub >= at_sub);
    tiku_edit_selection_t *s = NULL, *grown;
    size_t n = 0, cap = 0;
    tiku_edit_status_t st;
    tiku_cv_row_t r;
    int up;

    /* Each text row from the press's to the pointer's, from the place
     * nearest the press's x to the one nearest the pointer's. */
    for (;;) {
        tiku_cv_row(v, at_line, at_sub, &r);
        if (r.ann < 0) {
            if (n == cap) {
                cap = cap ? cap * 2 : 16;
                grown = cap <= SIZE_MAX / sizeof *s ?
                        tiku_ed_realloc(s, cap * sizeof *s) : NULL;
                if (grown == NULL) {
                    tiku_ed_free(s);
                    return TIKU_EDIT_NO_MEMORY;
                }
                s = grown;
            }
            s[n].anchor = tiku_cv_row_pos_at(v, &r, v->rect_x, &up);
            s[n].caret = tiku_cv_row_pos_at(v, &r, x, &up);
            n++;
        }
        if ((at_line == line && at_sub == sub) ||
            tiku_cv_rows_step(v, &at_line, &at_sub, down ? 1 : -1) == 0)
            break;
    }
    if (n == 0) {
        tiku_ed_free(s);
        return TIKU_EDIT_NOTHING;
    }
    st = tiku_codeview_select_many(v, s, n, n - 1);
    tiku_ed_free(s);
    return st;
}

tiku_edit_status_t
tiku_cv_add_press(tiku_codeview_t *v, tiku_edit_pos_t pos)
{
    size_t n, main, i;
    const tiku_edit_selection_t *s = tiku_cv_selections(v, &n, &main);
    tiku_edit_selection_t one;

    if (s == NULL)
        return TIKU_EDIT_NO_MEMORY;
    tiku_ed_free(v->held);
    v->held = NULL;
    v->nheld = 0;
    /* A press on one of several selections, or carets, takes it away. */
    for (i = 0; n > 1 && i < n; i++) {
        if (pos >= lo_of(&s[i]) && pos <= hi_of(&s[i])) {
            tiku_edit_selection_t *rest = tiku_ed_alloc(n * sizeof *rest);
            tiku_edit_status_t st;

            if (rest == NULL)
                return TIKU_EDIT_NO_MEMORY;
            memcpy(rest, s, i * sizeof *rest);
            memcpy(rest + i, s + i + 1, (n - i - 1) * sizeof *rest);
            st = tiku_codeview_select_many(v, rest, n - 1,
                                           i < main ? main - 1 :
                                           i == main && i > 0 ? i - 1 :
                                           main);
            tiku_ed_free(rest);
            v->dragging = 0;
            return st;
        }
    }
    one.anchor = one.caret = pos;
    v->held = plus_one(v, one, &v->nheld);
    if (v->held == NULL)
        return TIKU_EDIT_NO_MEMORY;
    v->nheld--;
    return tiku_codeview_select_many(v, v->held, v->nheld + 1, v->nheld);
}

tiku_edit_status_t
tiku_cv_add_drag(tiku_codeview_t *v, tiku_edit_pos_t anchor,
                 tiku_edit_pos_t caret)
{
    if (v->held == NULL)
        return TIKU_EDIT_NOTHING;
    v->held[v->nheld].anchor = anchor;
    v->held[v->nheld].caret = caret;
    return tiku_codeview_select_many(v, v->held, v->nheld + 1, v->nheld);
}
