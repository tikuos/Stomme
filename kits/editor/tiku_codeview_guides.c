/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_codeview_guides.c - a ruler at a column and dotted guides at each
 * level of a line's indentation, drawn behind the text, and the zoom of
 * the view's monospaced face.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_codeview_private.h"

/* The zoom steps a view takes, two pixels each. */
#define ZOOM_LEAST (-2)
#define ZOOM_MOST 4
/* The furthest column a ruler may stand at. */
#define RULER_MOST 10000u
/* Pixels from one dot of an indentation guide to the next. */
#define GUIDE_DOTS 3

tiku_edit_status_t
tiku_codeview_set_zoom(tiku_codeview_t *view, int steps)
{
    if (view == NULL || (steps != 0 && view->font != NULL))
        return TIKU_EDIT_INVALID;
    if (steps < ZOOM_LEAST || steps > ZOOM_MOST)
        return TIKU_EDIT_LIMIT;
    if (view->zoom != steps) {
        view->zoom = steps;
        view->goal = -1;
        view->upstream = -1;
        tiku_cv_widths_free(view);
        tiku_codeview_reveal(view);
    }
    return TIKU_EDIT_OK;
}

int
tiku_codeview_zoom(const tiku_codeview_t *view)
{
    return view != NULL ? view->zoom : 0;
}

tiku_edit_status_t
tiku_codeview_set_ruler(tiku_codeview_t *view, unsigned column)
{
    if (view == NULL)
        return TIKU_EDIT_INVALID;
    if (column > RULER_MOST)
        return TIKU_EDIT_LIMIT;
    view->ruler = column;
    return TIKU_EDIT_OK;
}

unsigned
tiku_codeview_ruler(const tiku_codeview_t *view)
{
    return view != NULL ? view->ruler : 0;
}

/** @brief The columns of @p line's indentation, its tabs reaching their
 *         stops, counted no further than @p most. */
static int64_t
indent_columns(tiku_codeview_t *v, int64_t line, int64_t most)
{
    tiku_edit_pos_t p = tiku_editdoc_line_start(v->doc, line);
    tiku_edit_pos_t end = tiku_editdoc_line_end(v->doc, line);
    int64_t col = 0;

    for (; p < end && col < most; p++) {
        int c = tiku_editdoc_byte_at(v->doc, p);

        if (c == ' ')
            col++;
        else if (c == '\t')
            col += v->tab_width - col % v->tab_width;
        else
            break;
    }
    return col;
}

void
tiku_cv_guides_paint(tiku_codeview_t *v, tiku_surface_t *s,
                     const tiku_cv_row_t *w, int y)
{
    int space = tiku_text_advance(tiku_cv_font(v), ' ');
    int height = tiku_codeview_row_height(v), dy;
    int64_t left = tiku_cv_text_x(v);
    int64_t x0 = left - v->left - w->x0;
    int64_t right = left + tiku_cv_text_w(v);
    int64_t col, every, guide;
    tiku_rgb_t ink = tiku_dim(tiku_ink(TIKU_INK_FAINT), TIKU_C_DOC);

    if (space <= 0)
        return;
    if (v->ruler > 0) {
        int64_t x = x0 + (int64_t)v->ruler * space;

        if (x >= left && x < right)
            tiku_vline(s, (int)x, y, height, ink);
    }
    if (!(v->options & TIKU_CODEVIEW_INDENT_GUIDES))
        return;
    col = indent_columns(v, w->line, (right - x0) / space + 1);
    every = v->indent_spaces > 0 ? v->indent_spaces : v->tab_width;
    for (guide = every; guide <= col; guide += every) {
        int64_t x = x0 + guide * space;

        if (x < left)
            continue;
        if (x >= right)
            break;
        for (dy = 1; dy < height; dy += GUIDE_DOTS)
            tiku_vline(s, (int)x, y + dy, 1, ink);
    }
}
