/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_codeview_input.c - the caret, the selection and the scroll moved by
 * keys, the pointer, the scrollbars and the wheel, through the same layout
 * the view paints with; editing keys go on to tiku_codeview_edit.c.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_codeview_private.h"
#include "tiku_ui.h"

#include <string.h>

/* A second press this soon after the last, and this near it, counts as
 * the same click: the desktop's double-click rule. */
#define CLICK_US 400000
#define CLICK_SLOP 6
/* A held scrollbar arrow waits this many ticks, then steps every few:
 * the desktop's scrollbar rule. */
#define ARROW_WAIT 12
#define ARROW_EVERY 4
/* Bytes of lines a tick counts the rows of while rows are kept. */
#define TICK_BYTES ((int64_t)1 << 20)

/** @brief What a character is, for words: a line break, space (a blank
 *         or an ASCII control character), part of a word (ASCII letters,
 *         digits, '_' and every byte past ASCII), or punctuation. */
enum { RUN_NONE, RUN_BREAK, RUN_SPACE, RUN_WORD, RUN_PUNCT };

/** @brief The run kind of the character starting at @p pos. */
static int
run_at(const tiku_codeview_t *v, tiku_edit_pos_t pos)
{
    int b = tiku_editdoc_byte_at(v->doc, pos);

    if (b < 0)
        return RUN_NONE;
    if (b == '\r' || b == '\n')
        return RUN_BREAK;
    if (b <= ' ' || b == 0x7F)
        return RUN_SPACE;
    if (b >= 0x80 || b == '_' || (b >= '0' && b <= '9') ||
        (b >= 'a' && b <= 'z') || (b >= 'A' && b <= 'Z'))
        return RUN_WORD;
    return RUN_PUNCT;
}

/** @brief The run kind of the character ending at @p pos. */
static int
run_before(const tiku_codeview_t *v, tiku_edit_pos_t pos)
{
    return pos > 0 ? run_at(v, tiku_editdoc_prev(v->doc, pos)) : RUN_NONE;
}

tiku_edit_pos_t
tiku_cv_word_right(const tiku_codeview_t *v, tiku_edit_pos_t pos)
{
    tiku_edit_pos_t end = tiku_editdoc_length(v->doc);
    int kind = run_at(v, pos);

    while (pos < end && run_at(v, pos) == kind)
        pos = tiku_editdoc_next(v->doc, pos);
    while (pos < end && run_at(v, pos) == RUN_SPACE)
        pos = tiku_editdoc_next(v->doc, pos);
    return pos;
}

tiku_edit_pos_t
tiku_cv_word_left(const tiku_codeview_t *v, tiku_edit_pos_t pos)
{
    int kind;

    while (pos > 0 && run_before(v, pos) == RUN_SPACE)
        pos = tiku_editdoc_prev(v->doc, pos);
    kind = run_before(v, pos);
    while (pos > 0 && run_before(v, pos) == kind)
        pos = tiku_editdoc_prev(v->doc, pos);
    return pos;
}

/** @brief The run holding the character at @p pos, within its line. */
static tiku_edit_range_t
run_around(const tiku_codeview_t *v, tiku_edit_pos_t pos)
{
    tiku_edit_range_t r;
    int kind = run_at(v, pos);

    r.start = pos;
    r.end = pos;
    if (kind == RUN_NONE || kind == RUN_BREAK)
        return r;
    while (r.start > 0 && run_before(v, r.start) == kind)
        r.start = tiku_editdoc_prev(v->doc, r.start);
    while (run_at(v, r.end) == kind)
        r.end = tiku_editdoc_next(v->doc, r.end);
    return r;
}

/** @brief The run under the row's own @p x on @p row: past its end, its
 *         last run; an empty row gives its start. */
static tiku_edit_range_t
run_under(tiku_codeview_t *v, const tiku_cv_row_t *row, int64_t x)
{
    tiku_edit_pos_t at = tiku_cv_char_at(v, row->line, row->x0 + x);
    tiku_edit_range_t r;

    if (row->end <= row->start) {
        r.start = r.end = row->start;
        return r;
    }
    if (at < 0 || at >= row->end)
        at = tiku_editdoc_prev(v->doc, row->end);
    else if (at < row->start)
        at = row->start;
    return run_around(v, at);
}

/** @brief A line with its line break, the last line to the end. */
static tiku_edit_range_t
line_around(const tiku_codeview_t *v, int64_t line)
{
    tiku_edit_range_t r;

    r.start = tiku_editdoc_line_start(v->doc, line);
    r.end = tiku_editdoc_line_start(v->doc, line + 1);
    return r;
}

/** @brief The caret's row, @p r, and its x from the row's start. */
static int64_t
caret_x(tiku_codeview_t *v, tiku_edit_pos_t caret, tiku_cv_row_t *r)
{
    tiku_cv_row_of(v, caret, caret == v->upstream, r);
    return tiku_cv_x_of(v, r->line, caret) - r->x0;
}

void
tiku_codeview_reveal(tiku_codeview_t *view)
{
    tiku_edit_pos_t anchor, caret;
    int64_t row, rows, x, width;
    tiku_cv_row_t r;

    if (view == NULL ||
        tiku_codeview_selection(view, &anchor, &caret) != TIKU_EDIT_OK)
        return;
    tiku_cv_rows_ready(view);
    tiku_cv_fold_reveal(view, tiku_editdoc_line_of(view->doc, caret));
    x = caret_x(view, caret, &r);
    rows = tiku_codeview_rows_visible(view);
    row = tiku_cv_shown_row(view, r.line, r.sub);
    if (row < 0 || row >= rows) {
        view->top = r.line;
        view->top_sub = r.sub;
    }
    if (row >= rows)
        (void)tiku_cv_rows_step(view, &view->top, &view->top_sub, 1 - rows);
    width = tiku_cv_text_w(view);
    if (x < view->left)
        view->left = x;
    else if (x >= view->left + width - 4)
        view->left = x - width + 8;
    if (view->left > x)
        view->left = x;
    tiku_cv_clamp(view);
}

/** @brief The caret @p rows rows up or down, at the x kept from the
 *         rows' starts; @p upstream says it lands at a row's end.  Rows of
 *         annotations are passed over: going down to the next shown
 *         line's first row, going up to their line's last. */
static tiku_edit_pos_t
vertical(tiku_codeview_t *v, tiku_edit_pos_t caret, int64_t rows,
         int *upstream)
{
    tiku_cv_row_t r;
    int64_t x = caret_x(v, caret, &r), line = r.line, sub = r.sub, next;

    if (v->goal < 0)
        v->goal = x;
    (void)tiku_cv_rows_step(v, &line, &sub, rows);
    tiku_cv_row(v, line, sub, &r);
    if (r.ann >= 0) {
        next = rows > 0 ? tiku_cv_shown_line(v, line + 1, 0) : -1;
        if (next >= 0)
            tiku_cv_row(v, next, 0, &r);
        else
            tiku_cv_row(v, line, tiku_cv_text_rows(v, line) - 1, &r);
    }
    return tiku_cv_row_pos_at(v, &r, v->goal, upstream);
}

/** @brief Where Home goes: the start of the caret's row, or of its line
 *         when the caret is there already. */
static tiku_edit_pos_t
home(tiku_codeview_t *v, tiku_edit_pos_t caret)
{
    tiku_cv_row_t r;

    tiku_cv_row_of(v, caret, caret == v->upstream, &r);
    return caret != r.start ? r.start :
           tiku_editdoc_line_start(v->doc, r.line);
}

/** @brief Where End goes: the end of the caret's row, shown on it, or of
 *         its line when the row is the line's last or the caret is at its
 *         end already; @p upstream says it stays on the row. */
static tiku_edit_pos_t
end(tiku_codeview_t *v, tiku_edit_pos_t caret, int *upstream)
{
    tiku_cv_row_t r;

    tiku_cv_row_of(v, caret, caret == v->upstream, &r);
    *upstream = !r.last && caret != r.end;
    return *upstream ? r.end : tiku_editdoc_line_end(v->doc, r.line);
}

/** @brief Where @p move takes the caret of the selection from @p anchor
 *         to @p caret, rows up and down at the x kept in the view's goal;
 *         @p up set when it lands at a row's end; -1 for no such move. */
static tiku_edit_pos_t
moved(tiku_codeview_t *view, tiku_codeview_move_t move,
      tiku_edit_pos_t anchor, tiku_edit_pos_t caret, int extend, int *up)
{
    tiku_edit_pos_t lo = anchor < caret ? anchor : caret;
    tiku_edit_pos_t hi = anchor < caret ? caret : anchor;

    *up = 0;
    switch (move) {
    case TIKU_CODEVIEW_CHAR_LEFT:
        caret = !extend && lo < hi ? lo : tiku_editdoc_prev(view->doc, caret);
        break;
    case TIKU_CODEVIEW_CHAR_RIGHT:
        caret = !extend && lo < hi ? hi : tiku_editdoc_next(view->doc, caret);
        break;
    case TIKU_CODEVIEW_WORD_LEFT:
        caret = tiku_cv_word_left(view, caret);
        break;
    case TIKU_CODEVIEW_WORD_RIGHT:
        caret = tiku_cv_word_right(view, caret);
        break;
    case TIKU_CODEVIEW_LINE_UP:
    case TIKU_CODEVIEW_LINE_DOWN:
    case TIKU_CODEVIEW_PAGE_UP:
    case TIKU_CODEVIEW_PAGE_DOWN: {
        int64_t rows = move == TIKU_CODEVIEW_LINE_UP ||
                       move == TIKU_CODEVIEW_LINE_DOWN ? 1 :
                       tiku_codeview_rows_visible(view);

        caret = vertical(view, caret, move == TIKU_CODEVIEW_LINE_UP ||
                         move == TIKU_CODEVIEW_PAGE_UP ? -rows : rows, up);
        break;
    }
    case TIKU_CODEVIEW_LINE_START:
        caret = home(view, caret);
        break;
    case TIKU_CODEVIEW_LINE_END:
        caret = end(view, caret, up);
        break;
    case TIKU_CODEVIEW_DOC_START:
        caret = 0;
        break;
    case TIKU_CODEVIEW_DOC_END:
        caret = tiku_editdoc_length(view->doc);
        break;
    default:
        return -1;
    }
    return caret;
}

/** @brief Whether @p move keeps the x rows up and down go to. */
static int
keeps_goal(tiku_codeview_move_t move)
{
    return move == TIKU_CODEVIEW_LINE_UP || move == TIKU_CODEVIEW_LINE_DOWN ||
           move == TIKU_CODEVIEW_PAGE_UP || move == TIKU_CODEVIEW_PAGE_DOWN;
}

/** @brief The x kept for the caret at @p at, or else @p other. */
static int64_t
goal_of(const tiku_codeview_t *v, tiku_edit_pos_t at, int64_t other)
{
    size_t lo = 0, hi = v->ngoals;

    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;

        if (v->goals[mid].at < at)
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo < v->ngoals && v->goals[lo].at == at ? v->goals[lo].x : other;
}

/**
 * @brief Keep the @p n carets' x in @p g; none kept when there is no room.
 *
 * Rows up and down leave the carets in order, except where the first or
 * last row holds a caret back and another lands before it on that row:
 * goal_of() may then miss a caret there, which goes on from the x it
 * landed at.  Of carets that met, the first one's x is found.
 */
static void
keep_goals(tiku_codeview_t *v, const tiku_cv_goal_t *g, size_t n)
{
    if (n > v->goals_cap) {
        tiku_cv_goal_t *grown = n <= SIZE_MAX / sizeof *grown ?
                                tiku_ed_realloc(v->goals, n * sizeof *grown)
                                : NULL;

        v->ngoals = 0;
        if (grown == NULL)
            return;
        v->goals = grown;
        v->goals_cap = n;
    }
    memcpy(v->goals, g, n * sizeof *g);
    v->ngoals = n;
}

/** @brief @p move for selection @p s, its rows up and down going to x
 *         @p x, or its own when -1, into @p to, the x kept in @p g;
 *         @return 0 for no such move. */
static int
move_one(tiku_codeview_t *v, tiku_codeview_move_t move, int extend,
         const tiku_edit_selection_t *s, int64_t x, int *up,
         tiku_edit_selection_t *to, tiku_cv_goal_t *g)
{
    v->goal = x;
    to->caret = moved(v, move, s->anchor, s->caret, extend, up);
    to->anchor = extend ? s->anchor : to->caret;
    g->at = to->caret;
    g->x = v->goal;
    return to->caret >= 0;
}

/** @brief @p move for every selection; rows up and down keep each caret's
 *         x from the first of them, the main one's in the view's goal. */
static tiku_edit_status_t
move_each(tiku_codeview_t *view, tiku_codeview_move_t move, int extend)
{
    size_t n, main, i;
    const tiku_edit_selection_t *s = tiku_cv_selections(view, &n, &main);
    tiku_edit_selection_t *to = tiku_ed_alloc(n * sizeof *to);
    tiku_cv_goal_t *g = tiku_ed_alloc(n * sizeof *g);
    int64_t goal = view->goal;
    tiku_edit_status_t st = TIKU_EDIT_OK;
    int up = 0, main_up = 0;

    if (s == NULL || to == NULL || g == NULL) {
        tiku_ed_free(to);
        tiku_ed_free(g);
        return TIKU_EDIT_NO_MEMORY;
    }
    /* The carets' x hold only while the main one's is kept: any other
     * move, a selection set or an edit drops it. */
    if (goal < 0)
        view->ngoals = 0;
    for (i = 0; i < n && st == TIKU_EDIT_OK; i++) {
        if (!move_one(view, move, extend, &s[i],
                      goal_of(view, s[i].caret, i == main ? goal : -1), &up,
                      &to[i], &g[i]))
            st = TIKU_EDIT_INVALID;
        if (i == main)
            main_up = up;
    }
    if (st == TIKU_EDIT_OK)
        st = tiku_editdoc_view_select_many(view->doc, view->view, to, n,
                                           main);
    view->goal = keeps_goal(move) && st == TIKU_EDIT_OK ? g[main].x : -1;
    if (view->goal >= 0)
        keep_goals(view, g, n);
    view->upstream = main_up ? to[main].caret : -1;
    tiku_ed_free(to);
    tiku_ed_free(g);
    if (st == TIKU_EDIT_OK)
        tiku_codeview_reveal(view);
    return st;
}

tiku_edit_status_t
tiku_codeview_move(tiku_codeview_t *view, tiku_codeview_move_t move,
                   int extend)
{
    tiku_edit_pos_t anchor, caret;
    tiku_edit_status_t st;
    int up = 0;

    if (view == NULL)
        return TIKU_EDIT_INVALID;
    st = tiku_codeview_selection(view, &anchor, &caret);
    if (st != TIKU_EDIT_OK)
        return st;
    tiku_cv_rows_ready(view);
    if (tiku_cv_many(view))
        return move_each(view, move, extend);
    caret = moved(view, move, anchor, caret, extend, &up);
    if (caret < 0)
        return TIKU_EDIT_INVALID;
    st = tiku_editdoc_view_select(view->doc, view->view,
                                  extend ? anchor : caret, caret);
    if (!keeps_goal(move))
        view->goal = -1;
    view->upstream = up ? caret : -1;
    if (st == TIKU_EDIT_OK)
        tiku_codeview_reveal(view);
    return st;
}

tiku_edit_status_t
tiku_codeview_select_all(tiku_codeview_t *view)
{
    tiku_edit_status_t st;

    if (view == NULL)
        return TIKU_EDIT_INVALID;
    st = tiku_codeview_select(view, 0, tiku_editdoc_length(view->doc));
    if (st == TIKU_EDIT_OK)
        tiku_codeview_reveal(view);
    return st;
}

/** @brief The row under the pointer, one row past either end of the
 *         visible rows at most, so a drag outside the view scrolls a row
 *         at a time. */
static void
row_under(tiku_codeview_t *v, int y, tiku_cv_row_t *r)
{
    int64_t row = (int64_t)y - tiku_cv_text_y(v);
    int64_t rows = tiku_codeview_rows_visible(v);
    int64_t line = v->top, sub = v->top_sub;

    row = row >= 0 ? row / tiku_codeview_row_height(v) : -1;
    if (row > rows)
        row = rows;
    (void)tiku_cv_rows_step(v, &line, &sub, row);
    tiku_cv_row(v, line, sub, r);
}

/** @brief The selection a drag gives with the pointer at @p x, @p y:
 *         from the press to the character, word or line under it. */
static void
drag_to(tiku_codeview_t *v, int x, int y)
{
    int64_t lx = (int64_t)x - tiku_cv_text_x(v) + v->left;
    tiku_edit_pos_t anchor, caret;
    tiku_edit_range_t r;
    tiku_cv_row_t row;
    int up = 0;

    tiku_cv_rows_ready(v);
    row_under(v, y, &row);
    if (v->rect) {
        if (tiku_cv_rectangle(v, row.line, row.sub, lx) == TIKU_EDIT_OK)
            tiku_codeview_reveal(v);
        return;
    }
    if (v->held != NULL) {
        caret = tiku_cv_row_pos_at(v, &row, lx, &up);
        if (tiku_cv_add_drag(v, v->pressed.start, caret) == TIKU_EDIT_OK)
            tiku_codeview_reveal(v);
        return;
    }
    if (v->unit == 1) {
        if (tiku_codeview_selection(v, &anchor, &caret) != TIKU_EDIT_OK)
            return;
        caret = tiku_cv_row_pos_at(v, &row, lx, &up);
    } else {
        r = v->unit == 2 ? run_under(v, &row, lx) : line_around(v, row.line);
        if (r.start < v->pressed.start) {
            anchor = v->pressed.end;
            caret = r.start;
        } else if (r.end > v->pressed.end) {
            anchor = v->pressed.start;
            caret = r.end;
        } else {
            anchor = v->pressed.start;
            caret = v->pressed.end;
        }
    }
    if (tiku_editdoc_view_select(v->doc, v->view, anchor, caret) ==
        TIKU_EDIT_OK) {
        v->upstream = up ? caret : -1;
        tiku_codeview_reveal(v);
    }
}

/** @brief A press with Option starts a rectangle at the row under it,
 *         one with Command adds a caret there or takes away the one of
 *         several selections there; @return 0 for a press with neither. */
static int
press_adding(tiku_codeview_t *v, const tiku_event_t *e, int64_t lx)
{
    tiku_cv_row_t row;
    tiku_edit_pos_t at;
    int up;

    if (!(e->modifiers & (TIKU_MOD_OPTION | TIKU_MOD_CMD)))
        return 0;
    v->clicks = 0;
    v->goal = -1;
    v->dragging = 1;
    v->unit = 1;
    tiku_cv_rows_ready(v);
    row_under(v, e->y, &row);
    if (e->modifiers & TIKU_MOD_OPTION) {
        v->rect = 1;
        v->rect_x = lx;
        v->rect_line = row.line;
        v->rect_sub = row.sub;
        (void)tiku_cv_rectangle(v, row.line, row.sub, lx);
    } else {
        at = tiku_cv_row_pos_at(v, &row, lx, &up);
        v->pressed.start = v->pressed.end = at;
        (void)tiku_cv_add_press(v, at);
    }
    tiku_codeview_reveal(v);
    return 1;
}

/** @brief The press is over: no rectangle, no selection being added. */
static void
release(tiku_codeview_t *v)
{
    v->dragging = 0;
    v->rect = 0;
    tiku_ed_free(v->held);
    v->held = NULL;
    v->nheld = 0;
}

/** @brief Button 1 went down at the event's point. */
static void
press(tiku_codeview_t *v, const tiku_event_t *e)
{
    int64_t lx = (int64_t)e->x - tiku_cv_text_x(v) + v->left;
    tiku_edit_pos_t anchor, caret, was;
    int shift = (e->modifiers & TIKU_MOD_SHIFT) != 0, up = 0;
    tiku_cv_row_t row;

    release(v);
    if (press_adding(v, e, lx))
        return;

    if (!shift && v->clicks > 0 && v->clicks < 3 &&
        e->time_us - v->click_at < CLICK_US &&
        e->x - v->click_x > -CLICK_SLOP && e->x - v->click_x < CLICK_SLOP &&
        e->y - v->click_y > -CLICK_SLOP && e->y - v->click_y < CLICK_SLOP)
        v->clicks++;
    else
        v->clicks = 1;
    v->click_at = e->time_us;
    v->click_x = e->x;
    v->click_y = e->y;
    v->goal = -1;
    v->dragging = 1;
    v->unit = v->clicks;
    tiku_cv_rows_ready(v);
    row_under(v, e->y, &row);
    if (v->clicks == 1) {
        caret = tiku_cv_row_pos_at(v, &row, lx, &up);
        if (!shift ||
            tiku_codeview_selection(v, &anchor, &was) != TIKU_EDIT_OK)
            anchor = caret;
        v->pressed.start = v->pressed.end = caret;
    } else {
        v->pressed = v->clicks == 2 ? run_under(v, &row, lx) :
                                      line_around(v, row.line);
        anchor = v->pressed.start;
        caret = v->pressed.end;
    }
    if (tiku_editdoc_view_select(v->doc, v->view, anchor, caret) ==
        TIKU_EDIT_OK) {
        v->upstream = up ? caret : -1;
        tiku_codeview_reveal(v);
    }
}

/** @brief Whether @p x, @p y is inside @p r. */
static int
inside(tiku_rect_t r, int x, int y)
{
    return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h;
}

/** @brief One column: a space's width in the view's face. */
static int64_t
column(const tiku_codeview_t *v)
{
    int w = tiku_text_advance(tiku_cv_font(v), ' ');

    return w > 0 ? w : 1;
}

/** @brief Scroll by @p delta rows down, or pixels across. */
static void
scroll_by(tiku_codeview_t *v, int across, int64_t delta)
{
    if (across)
        tiku_codeview_scroll_to(v, tiku_cv_top_row(v), v->left + delta);
    else
        tiku_codeview_scroll_to(v, tiku_cv_top_row(v) + delta, v->left);
}

/** @brief One arrow's step: a row down, a column across. */
static int64_t
bar_step(const tiku_codeview_t *v, int across)
{
    return across ? column(v) : 1;
}

/** @brief A page: what is shown, less one step. */
static int64_t
bar_page(tiku_codeview_t *v, int across)
{
    int64_t page = across ? tiku_cv_text_w(v) - column(v)
                          : tiku_codeview_rows_visible(v) - 1;

    return page > bar_step(v, across) ? page : bar_step(v, across);
}

/** @brief Button 1 went down on a scrollbar: an arrow steps and holds,
 *         the track pages, and the thumb is taken. */
static void
bar_press(tiku_codeview_t *v, int across, int x, int y)
{
    tiku_rect_t bar = tiku_cv_bar(v, across), thumb;
    int pos, frac, at, ends, len, start, size;

    tiku_cv_bar_place(v, across, &pos, &frac);
    thumb = tiku_ui_scrollbar_thumb(bar, (float)pos / 1000.0f,
                                    (float)frac / 1000.0f, across);
    at = across ? x - bar.x : y - bar.y;
    ends = across ? bar.h : bar.w;
    len = across ? bar.w : bar.h;
    start = across ? thumb.x - bar.x : thumb.y - bar.y;
    size = across ? thumb.w : thumb.h;
    if (at < ends || at >= len - ends) {
        v->arrow_bar = across + 1;
        v->arrow_dir = at < ends ? -1 : 1;
        v->arrow_pulses = 0;
        scroll_by(v, across, v->arrow_dir * bar_step(v, across));
    } else if (at >= start && at < start + size) {
        v->held_bar = across + 1;
        v->grab = at - start;
    } else {
        scroll_by(v, across, (at < start ? -1 : 1) * bar_page(v, across));
    }
}

/** @brief How far the thumb of scrollbar @p bar can travel: the track
 *         less both arrow ends and the thumb's own length. */
static int
bar_travel(tiku_codeview_t *v, tiku_rect_t bar, int across)
{
    int pos, frac;
    tiku_rect_t thumb;

    tiku_cv_bar_place(v, across, &pos, &frac);
    thumb = tiku_ui_scrollbar_thumb(bar, (float)pos / 1000.0f,
                                    (float)frac / 1000.0f, across);
    return (across ? bar.w : bar.h) - 2 * (across ? bar.h : bar.w) -
           (across ? thumb.w : thumb.h);
}

/** @brief The thumb held, the pointer at @p x, @p y: put the thumb's
 *         start where the pointer's grab says. */
static void
bar_drag(tiku_codeview_t *v, int x, int y)
{
    int across = v->held_bar - 1;
    tiku_rect_t bar = tiku_cv_bar(v, across);
    int ends, travel, at;
    int64_t total, visible, value;

    ends = across ? bar.h : bar.w;
    travel = bar_travel(v, bar, across);
    if (travel <= 0)
        return;
    at = (across ? x - bar.x : y - bar.y) - ends - v->grab;
    if (at < 0)
        at = 0;
    if (at > travel)
        at = travel;
    tiku_codeview_extent(v, across, &total, &visible, &value);
    value = ((int64_t)at * (total - visible) + travel / 2) / travel;
    if (across)
        tiku_codeview_scroll_to(v, tiku_cv_top_row(v), value);
    else
        tiku_codeview_scroll_to(v, value, v->left);
}

/** @brief A tick with an arrow held: step once the wait is over. */
static int
arrow_tick(tiku_codeview_t *v)
{
    if (v->arrow_bar == 0)
        return 0;
    v->arrow_pulses++;
    if (v->arrow_pulses < ARROW_WAIT ||
        (v->arrow_pulses - ARROW_WAIT) % ARROW_EVERY != 0)
        return 0;
    scroll_by(v, v->arrow_bar - 1,
              v->arrow_dir * bar_step(v, v->arrow_bar - 1));
    return 1;
}

/** @brief The wheel: rows down, or columns across with Shift or a
 *         sideways turn; the caret stays where it is. */
static void
wheel(tiku_codeview_t *v, const tiku_event_t *e)
{
    if (e->modifiers & TIKU_MOD_SHIFT)
        scroll_by(v, 1, (int64_t)e->dy * column(v));
    else if (e->dy != 0)
        scroll_by(v, 0, e->dy);
    if (e->dx != 0)
        scroll_by(v, 1, (int64_t)e->dx * column(v));
}

/** @brief A key the view moves by; 0 for any other. */
static int
key(tiku_codeview_t *v, const tiku_event_t *e)
{
    int extend = (e->modifiers & TIKU_MOD_SHIFT) != 0;
    int cmd = (e->modifiers & TIKU_MOD_CMD) != 0;
    int opt = (e->modifiers & TIKU_MOD_OPTION) != 0;
    tiku_codeview_move_t move;

    switch (e->key) {
    case TIKU_KEY_LEFT:
        move = cmd || opt ? TIKU_CODEVIEW_WORD_LEFT : TIKU_CODEVIEW_CHAR_LEFT;
        break;
    case TIKU_KEY_RIGHT:
        move = cmd || opt ? TIKU_CODEVIEW_WORD_RIGHT :
                            TIKU_CODEVIEW_CHAR_RIGHT;
        break;
    case TIKU_KEY_UP:
    case TIKU_KEY_DOWN:
    case TIKU_KEY_PAGE_UP:
    case TIKU_KEY_PAGE_DOWN:
        if (cmd || opt)
            return 0;
        move = e->key == TIKU_KEY_UP ? TIKU_CODEVIEW_LINE_UP :
               e->key == TIKU_KEY_DOWN ? TIKU_CODEVIEW_LINE_DOWN :
               e->key == TIKU_KEY_PAGE_UP ? TIKU_CODEVIEW_PAGE_UP :
                                            TIKU_CODEVIEW_PAGE_DOWN;
        break;
    case TIKU_KEY_HOME:
    case TIKU_KEY_END:
        if (opt)
            return 0;
        move = e->key == TIKU_KEY_HOME ?
               (cmd ? TIKU_CODEVIEW_DOC_START : TIKU_CODEVIEW_LINE_START) :
               (cmd ? TIKU_CODEVIEW_DOC_END : TIKU_CODEVIEW_LINE_END);
        break;
    default:
        return 0;
    }
    (void)tiku_codeview_move(v, move, extend);
    return 1;
}

/** @brief An event the view's text, scrollbars and caret take, with
 *         @p commit the result tiku_text_input_event() gave for it. */
static int
dispatch(tiku_codeview_t *view, const tiku_event_t *e, int commit)
{
    int across;

    switch (e->type) {
    case TIKU_EVENT_TEXT_INPUT:
        return tiku_cv_edit_commit(view, commit);
    case TIKU_EVENT_KEY_DOWN:
        if (e->key == TIKU_KEY_ESCAPE && e->modifiers == 0)
            return tiku_codeview_keep_main(view) == TIKU_EDIT_OK;
        return key(view, e) || tiku_cv_edit_key(view, e);
    case TIKU_EVENT_POINTER_DOWN:
        if (e->button != 1 || !inside(view->bounds, e->x, e->y))
            return 0;
        for (across = 0; across < 2; across++) {
            if (inside(tiku_cv_bar(view, across), e->x, e->y)) {
                bar_press(view, across, e->x, e->y);
                return 1;
            }
        }
        if (tiku_cv_folds_press(view, e))
            return 1;
        if (!inside(tiku_cv_area(view), e->x, e->y))
            return 0;
        press(view, e);
        return 1;
    case TIKU_EVENT_POINTER_MOVE:
    case TIKU_EVENT_POINTER_UP:
        if (e->type == TIKU_EVENT_POINTER_UP && e->button != 1)
            return 0;
        if (view->held_bar) {
            bar_drag(view, e->x, e->y);
            if (e->type == TIKU_EVENT_POINTER_UP)
                view->held_bar = 0;
            return 1;
        }
        if (view->arrow_bar) {
            if (e->type == TIKU_EVENT_POINTER_UP)
                view->arrow_bar = 0;
            return 1;
        }
        if (!view->dragging)
            return 0;
        drag_to(view, e->x, e->y);
        if (e->type == TIKU_EVENT_POINTER_UP)
            release(view);
        return 1;
    case TIKU_EVENT_WHEEL:
        wheel(view, e);
        return 1;
    case TIKU_EVENT_TICK:
        return arrow_tick(view) | tiku_cv_rows_tick(view, TICK_BYTES);
    case TIKU_EVENT_DEACTIVATED:
        release(view);
        view->held_bar = 0;
        view->arrow_bar = 0;
        return 0;
    default:
        return 0;
    }
}

int
tiku_codeview_event(tiku_codeview_t *view, const tiku_event_t *e)
{
    int commit, taken, bound;

    if (view == NULL || e == NULL)
        return 0;
    view->status = TIKU_EDIT_OK;
    commit = tiku_text_input_event(&view->input, e);
    taken = tiku_cv_assist_event(view, e);
    if (!taken) {
        bound = tiku_cv_binding_event(view, e);
        taken = bound >= 0 ? bound :
                tiku_cv_hot_event(view, e) || dispatch(view, e, commit);
    }
    tiku_cv_assist_follow(view);
    tiku_cv_hot_after(view, e);
    return taken;
}
