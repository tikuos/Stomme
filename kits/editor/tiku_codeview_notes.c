/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_codeview_notes.c - the document's indicators drawn under the text
 * they cover, its annotations drawn as rows under their lines, and the
 * caret taken from one indicator to the next.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_codeview_private.h"
#include "tiku_ui.h"

#include <string.h>

/* Pixels from the text area's left edge to an annotation's text. */
#define ANN_INDENT 4

/** @brief The rows the annotation @p text takes: one, and one more for
 *         each LF in it. */
static int64_t
text_rows(const char *text)
{
    int64_t rows = 1;

    for (; *text; text++)
        rows += *text == '\n';
    return rows;
}

int64_t
tiku_cv_ann_rows(const tiku_codeview_t *v, int64_t line)
{
    const char *text;
    int64_t rows = 0;
    size_t i;

    if (!v->ann_shown)
        return 0;
    for (i = 0; (text = tiku_editdoc_annotation(v->doc, line, i, NULL));
         i++)
        rows += text_rows(text);
    return rows;
}

/** @brief The part of @p line's annotations shown on its annotation row
 *         @p ann: its first byte, its length in @p n and its kind. */
static const char *
ann_piece(const tiku_codeview_t *v, int64_t line, int64_t ann, size_t *n,
          unsigned *kind)
{
    const char *text, *end;
    size_t i;

    for (i = 0; (text = tiku_editdoc_annotation(v->doc, line, i, kind));
         i++) {
        int64_t rows = text_rows(text);

        if (ann >= rows) {
            ann -= rows;
            continue;
        }
        while (ann-- > 0)
            text = strchr(text, '\n') + 1;
        end = strchr(text, '\n');
        *n = end ? (size_t)(end - text) : strlen(text);
        return text;
    }
    *n = 0;
    return NULL;
}

void
tiku_cv_annotation_paint(tiku_codeview_t *v, tiku_surface_t *s,
                         const tiku_cv_row_t *w, int y)
{
    const tiku_font_t *f = tiku_cv_font(v);
    unsigned kind = 0;
    size_t n = 0;
    const char *piece = ann_piece(v, w->line, w->ann, &n, &kind);
    tiku_rect_t row;
    char text[256];

    if (piece == NULL)
        return;
    row.x = tiku_cv_text_x(v);
    row.y = y;
    row.w = (int)tiku_cv_text_w(v);
    row.h = tiku_codeview_row_height(v);
    tiku_fill(s, row, v->marks[kind] != TIKU_CODEVIEW_MARK_NONE ?
              tiku_dim(v->mark_colors[kind], TIKU_C_DOC) : TIKU_C_PANEL);
    if (n >= sizeof text) {
        n = sizeof text - 1;
        while (n > 0 && ((unsigned char)piece[n] & 0xc0) == 0x80)
            n--;
    }
    memcpy(text, piece, n);
    text[n] = '\0';
    tiku_text(s, f, row.x + ANN_INDENT, y + f->ascent, text, TIKU_C_TEXT);
}

/** @brief An indicator being drawn on one row. */
typedef struct {
    tiku_codeview_t *v;
    tiku_surface_t *s;
    const tiku_cv_row_t *w;
    int y;                      /* the row's top                        */
} mark_t;

/** @brief The surface x of the boundary at @p pos on the row, kept a
 *         little outside the text area. */
static int
mark_x(const mark_t *m, tiku_edit_pos_t pos)
{
    int64_t x = tiku_cv_x_of(m->v, m->w->line, pos) - m->w->x0 - m->v->left;
    int64_t most = tiku_cv_text_w(m->v) + 2;

    if (x < -2)
        x = -2;
    if (x > most)
        x = most;
    return tiku_cv_text_x(m->v) + (int)x;
}

/** @brief Draw one indicator's part of the row in its kind's style. */
static int
mark_one(void *ctx, tiku_edit_range_t range, unsigned kind)
{
    const mark_t *m = ctx;
    tiku_codeview_t *v = m->v;
    tiku_rgb_t c = v->mark_colors[kind];
    int x0 = mark_x(m, range.start > m->w->start ? range.start : m->w->start);
    int x1 = mark_x(m, range.end < m->w->end ? range.end : m->w->end);
    int base = m->y + tiku_cv_font(v)->height, x;
    tiku_rect_t box;

    if (x1 <= x0)
        return 0;
    switch (v->marks[kind]) {
    case TIKU_CODEVIEW_MARK_SQUIGGLE:
        for (x = x0; x < x1; x += 2)
            tiku_hline(m->s, x, base + ((x - x0) / 2 & 1),
                       x1 - x < 2 ? x1 - x : 2, c);
        break;
    case TIKU_CODEVIEW_MARK_LINE:
        tiku_hline(m->s, x0, base + 1, x1 - x0, c);
        break;
    case TIKU_CODEVIEW_MARK_BOX:
        box.x = x0;
        box.y = m->y;
        box.w = x1 - x0;
        box.h = tiku_codeview_row_height(v);
        tiku_frame(m->s, box, c);
        break;
    default:
        break;
    }
    return 0;
}

void
tiku_cv_marks_paint(tiku_codeview_t *v, tiku_surface_t *s,
                    const tiku_cv_row_t *w, int y)
{
    uint32_t kinds = 0;
    tiku_edit_range_t span;
    mark_t m;
    unsigned k;

    for (k = 0; k < TIKU_EDIT_INDICATOR_KINDS; k++)
        if (v->marks[k] != TIKU_CODEVIEW_MARK_NONE)
            kinds |= (uint32_t)1 << k;
    if (kinds == 0 || w->end <= w->start)
        return;
    m.v = v;
    m.s = s;
    m.w = w;
    m.y = y;
    span.start = w->start;
    span.end = w->end;
    tiku_editdoc_indicators_each(v->doc, span, kinds, mark_one, &m);
}

tiku_edit_status_t
tiku_codeview_set_mark(tiku_codeview_t *view, unsigned kind,
                       tiku_codeview_mark_t style, tiku_rgb_t color)
{
    if (view == NULL || kind >= TIKU_EDIT_INDICATOR_KINDS ||
        style < TIKU_CODEVIEW_MARK_NONE || style > TIKU_CODEVIEW_MARK_BOX)
        return TIKU_EDIT_INVALID;
    view->marks[kind] = style;
    view->mark_colors[kind] = color;
    return TIKU_EDIT_OK;
}

tiku_edit_status_t
tiku_codeview_goto_indicator(tiku_codeview_t *view, uint32_t kinds,
                             int backward)
{
    tiku_edit_pos_t anchor, caret, from;
    tiku_edit_range_t r;
    tiku_edit_status_t st;

    if (view == NULL)
        return TIKU_EDIT_INVALID;
    st = tiku_codeview_selection(view, &anchor, &caret);
    if (st != TIKU_EDIT_OK)
        return st;
    from = anchor < caret ? anchor : caret;
    st = tiku_editdoc_indicator_next(view->doc, from, kinds, backward, &r,
                                     NULL);
    if (st == TIKU_EDIT_NOTHING)
        st = tiku_editdoc_indicator_next(view->doc, backward ?
                                         tiku_editdoc_length(view->doc) + 1 :
                                         -1, kinds, backward, &r, NULL);
    if (st != TIKU_EDIT_OK)
        return st;
    st = tiku_codeview_select(view, r.start, r.end);
    if (st == TIKU_EDIT_OK)
        tiku_codeview_reveal(view);
    return st;
}
