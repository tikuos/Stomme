/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_codeview.c - a view's life: its document, its selection, how it
 * is set up and where it is scrolled.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_codeview_private.h"
#include "tiku_editdoc_private.h"

#include <string.h>

/**
 * @brief Follow a change to the document, then ask the host to paint
 *        again.
 *
 * The first row keeps the text it showed when a change lands above it,
 * and moves to a change that covers it.  Widths, rows and colouring states
 * are carried across a change, checkpoints past its start are dropped,
 * and a folded region it touches unfolds; a load drops them all, and the
 * list and tip, and scrolls back to the start.  Any change ends a drag, a
 * held thumb, the x kept for moves up and down, and the hotspot shown.
 */
static void
heard(void *ctx, tiku_editdoc_t *doc, const tiku_edit_change_t *change)
{
    tiku_codeview_t *v = ctx;
    tiku_edit_range_t before, after;
    int64_t first, last, delta;

    v->goal = -1;
    v->upstream = -1;
    v->dragging = 0;
    v->held_bar = 0;
    tiku_cv_hot_forget(v);
    if (change->count == 0)
        return;
    if (change->cause == TIKU_EDIT_LOADED) {
        tiku_cv_assist_free(v);
        v->top = 0;
        v->top_sub = 0;
        v->left = 0;
        tiku_cv_widths_free(v);
        tiku_cv_rows_free(v);
        tiku_cv_style_reset(v);
        tiku_cv_marks_change(v, 0);
        if (v->host.invalidate != NULL)
            v->host.invalidate(v->host.ctx, v);
        return;
    }
    tiku_edit_change_span(change, 0, &before, &after);
    tiku_cv_marks_change(v, before.start);
    first = tiku_editdoc_line_of(doc, after.start);
    tiku_edit_change_span(change, change->count - 1, &before, &after);
    delta = change->lines_after - change->lines_before;
    last = tiku_editdoc_line_of(doc, after.end) - delta;
    tiku_cv_widths_change(v, first, last, last + delta, change->lines_after);
    tiku_cv_rows_change(v, first, last, last + delta, change->lines_after);
    tiku_cv_style_change(v, first, last, last + delta, change->lines_after);
    tiku_cv_fold_changed(v, first, last + delta);
    if (last < v->top) {
        v->top += delta;
    } else if (first < v->top) {
        v->top = first;
        v->top_sub = 0;
    }
    tiku_cv_clamp(v);
    if (v->host.invalidate != NULL)
        v->host.invalidate(v->host.ctx, v);
}

tiku_edit_status_t
tiku_codeview_create(tiku_editdoc_t *doc, tiku_codeview_t **view)
{
    tiku_codeview_t *v;
    tiku_edit_status_t st;

    if (view == NULL)
        return TIKU_EDIT_INVALID;
    *view = NULL;
    if (doc == NULL)
        return TIKU_EDIT_INVALID;
    v = tiku_ed_alloc(sizeof *v);
    if (v == NULL)
        return TIKU_EDIT_NO_MEMORY;
    memset(v, 0, sizeof *v);
    v->doc = doc;
    v->tab_width = TIKU_CODEVIEW_TAB_DEFAULT;
    v->focused = 1;
    v->goal = -1;
    v->upstream = -1;
    v->widest = -1;
    strcpy(v->newline, "\n");
    v->exec_fill = TIKU_RGB(211, 238, 185);
    tiku_text_input_reset(&v->input);
    tiku_cv_marks_free(v);
    tiku_cv_rows_free(v);
    v->kept_line = -1;
    st = tiku_editdoc_view_attach(doc, &v->view);
    if (st == TIKU_EDIT_OK) {
        st = tiku_editdoc_listen(doc, heard, v, &v->listener);
        if (st != TIKU_EDIT_OK)
            tiku_editdoc_view_detach(doc, v->view);
    }
    if (st != TIKU_EDIT_OK) {
        tiku_ed_free(v);
        return st;
    }
    tiku_editdoc_retain(doc);
    *view = v;
    return TIKU_EDIT_OK;
}

void
tiku_codeview_destroy(tiku_codeview_t *view)
{
    if (view == NULL)
        return;
    tiku_cv_assist_free(view);
    tiku_editdoc_unlisten(view->doc, view->listener);
    tiku_editdoc_view_detach(view->doc, view->view);
    tiku_editdoc_release(view->doc);
    tiku_cv_widths_free(view);
    tiku_cv_rows_free(view);
    tiku_cv_style_free(view);
    tiku_cv_marks_free(view);
    tiku_ed_free(view->sels);
    tiku_ed_free(view->held);
    tiku_ed_free(view->goals);
    tiku_ed_free(view->bindings);
    tiku_ed_free(view->zero_pattern);
    tiku_ed_free(view->fold_levels);
    tiku_ed_free(view);
}

tiku_editdoc_t *
tiku_codeview_doc(const tiku_codeview_t *view)
{
    return view != NULL ? view->doc : NULL;
}

tiku_edit_view_t
tiku_codeview_doc_view(const tiku_codeview_t *view)
{
    tiku_edit_view_t none = { 0, 0 };

    return view != NULL ? view->view : none;
}

void
tiku_codeview_set_bounds(tiku_codeview_t *view, tiku_rect_t bounds)
{
    if (view == NULL)
        return;
    if (bounds.w < 0)
        bounds.w = 0;
    if (bounds.h < 0)
        bounds.h = 0;
    view->bounds = bounds;
    tiku_cv_clamp(view);
}

tiku_rect_t
tiku_codeview_bounds(const tiku_codeview_t *view)
{
    tiku_rect_t none = { 0, 0, 0, 0 };

    return view != NULL ? view->bounds : none;
}

void
tiku_codeview_set_font(tiku_codeview_t *view, const tiku_font_t *font)
{
    if (view == NULL)
        return;
    view->font = font;
    view->zoom = 0;
    tiku_cv_clamp(view);
}

const tiku_font_t *
tiku_codeview_font(const tiku_codeview_t *view)
{
    return view != NULL ? tiku_cv_font(view) : NULL;
}

tiku_edit_status_t
tiku_codeview_set_tab_width(tiku_codeview_t *view, int columns)
{
    if (view == NULL)
        return TIKU_EDIT_INVALID;
    if (columns < 1 || columns > TIKU_CODEVIEW_TAB_MAX)
        return TIKU_EDIT_LIMIT;
    view->tab_width = columns;
    return TIKU_EDIT_OK;
}

int
tiku_codeview_tab_width(const tiku_codeview_t *view)
{
    return view != NULL ? view->tab_width : 0;
}

void
tiku_codeview_set_focus(tiku_codeview_t *view, int focused)
{
    if (view != NULL)
        view->focused = focused != 0;
}

int
tiku_codeview_focused(const tiku_codeview_t *view)
{
    return view != NULL && view->focused;
}

tiku_edit_status_t
tiku_codeview_set_marker_fill(tiku_codeview_t *view, unsigned kind,
                              tiku_rgb_t color)
{
    if (view == NULL || kind >= TIKU_EDIT_MARKER_KINDS)
        return TIKU_EDIT_INVALID;
    view->fills[kind] = color;
    view->fill_kinds |= (uint32_t)1 << kind;
    return TIKU_EDIT_OK;
}

void
tiku_codeview_clear_marker_fill(tiku_codeview_t *view, unsigned kind)
{
    if (view != NULL && kind < TIKU_EDIT_MARKER_KINDS)
        view->fill_kinds &= ~((uint32_t)1 << kind);
}

void
tiku_codeview_set_execution_fill(tiku_codeview_t *view, tiku_rgb_t color)
{
    if (view != NULL)
        view->exec_fill = color;
}

void
tiku_codeview_set_caret_line_fill(tiku_codeview_t *view, tiku_rgb_t color)
{
    if (view == NULL)
        return;
    view->caret_fill = color;
    view->caret_fill_set = 1;
}

tiku_edit_status_t
tiku_codeview_set_ink_style(tiku_codeview_t *view, tiku_ink_t ink,
                            unsigned style)
{
    const unsigned known = TIKU_CODEVIEW_BOLD | TIKU_CODEVIEW_ITALIC |
                           TIKU_CODEVIEW_UNDERLINE | TIKU_CODEVIEW_HOTSPOT;

    if (view == NULL || (unsigned)ink >= TIKU_CV_INKS ||
        (style & ~known) != 0)
        return TIKU_EDIT_INVALID;
    view->ink_styles[ink] = style;
    tiku_cv_hot_forget(view);
    return TIKU_EDIT_OK;
}

unsigned
tiku_codeview_ink_style(const tiku_codeview_t *view, tiku_ink_t ink)
{
    if (view == NULL || (unsigned)ink >= TIKU_CV_INKS)
        return 0;
    return view->ink_styles[ink];
}

void
tiku_codeview_set_options(tiku_codeview_t *view, unsigned options)
{
    if (view == NULL)
        return;
    view->options = options & (TIKU_CODEVIEW_LINE_NUMBERS |
                               TIKU_CODEVIEW_INVISIBLES |
                               TIKU_CODEVIEW_VSCROLL |
                               TIKU_CODEVIEW_HSCROLL |
                               TIKU_CODEVIEW_BRACES |
                               TIKU_CODEVIEW_WRAP |
                               TIKU_CODEVIEW_ANNOTATIONS |
                               TIKU_CODEVIEW_FOLDS |
                               TIKU_CODEVIEW_CARET_LINE |
                               TIKU_CODEVIEW_BLOCK_CARET |
                               TIKU_CODEVIEW_INDENT_GUIDES);
    view->held_bar = 0;
    view->arrow_bar = 0;
    tiku_cv_clamp(view);
}

unsigned
tiku_codeview_options(const tiku_codeview_t *view)
{
    return view != NULL ? view->options : 0;
}

int
tiku_codeview_row_height(const tiku_codeview_t *view)
{
    if (view == NULL)
        return 0;
    return tiku_cv_font(view)->height + TIKU_CV_ROW_GAP;
}

int64_t
tiku_codeview_rows_visible(const tiku_codeview_t *view)
{
    int64_t rows;

    if (view == NULL)
        return 0;
    rows = (int64_t)(tiku_cv_area(view).h - TIKU_CV_PAD_Y) /
           tiku_codeview_row_height(view);
    return rows > 0 ? rows : 1;
}

void
tiku_cv_clamp(tiku_codeview_t *v)
{
    int64_t last = tiku_editdoc_lines(v->doc) -
                   tiku_codeview_rows_visible(v);

    tiku_cv_rows_ready(v);
    if (tiku_cv_rows_kept(v))
        return;
    if (v->top > last)
        v->top = last;
    if (v->top < 0)
        v->top = 0;
    if (v->left < 0)
        v->left = 0;
}

void
tiku_cv_clamp_across(tiku_codeview_t *v)
{
    int64_t most;

    tiku_cv_clamp(v);
    if (tiku_cv_wraps(v))
        return;
    most = tiku_cv_widest(v) + TIKU_CV_SLACK - tiku_cv_text_w(v);
    if (v->left > most)
        v->left = most > 0 ? most : 0;
}

int64_t
tiku_codeview_top(const tiku_codeview_t *view)
{
    return view != NULL ? tiku_cv_top_row(view) : 0;
}

int64_t
tiku_codeview_left(const tiku_codeview_t *view)
{
    return view != NULL ? view->left : 0;
}

void
tiku_codeview_scroll_to(tiku_codeview_t *view, int64_t top, int64_t left)
{
    if (view == NULL)
        return;
    tiku_cv_rows_ready(view);
    tiku_cv_set_top_row(view, top);
    view->left = left;
    tiku_cv_clamp_across(view);
}

tiku_edit_status_t
tiku_codeview_selection(const tiku_codeview_t *view, tiku_edit_pos_t *anchor,
                        tiku_edit_pos_t *caret)
{
    if (view == NULL)
        return TIKU_EDIT_INVALID;
    return tiku_editdoc_view_selection(view->doc, view->view, anchor, caret);
}

tiku_edit_status_t
tiku_codeview_select(tiku_codeview_t *view, tiku_edit_pos_t anchor,
                     tiku_edit_pos_t caret)
{
    if (view == NULL)
        return TIKU_EDIT_INVALID;
    view->goal = -1;
    view->upstream = -1;
    return tiku_editdoc_view_select(view->doc, view->view, anchor, caret);
}
