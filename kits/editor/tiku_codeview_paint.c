/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_codeview_paint.c - a view drawn with the Interface Kit's calls,
 * from the layout's own walk, so a recording surface carries all of it.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_codeview_private.h"
#include "tiku_ui.h"

#include <stdio.h>
#include <string.h>

/* The most bytes of text handed to tiku_text() at once; a longer run in
 * one colour and face is drawn as several pieces. */
#define PIECE_MAX 256

/** @brief One row being painted. */
typedef struct {
    tiku_surface_t *s;
    tiku_codeview_t *view;      /* the view, for its styles             */
    const tiku_font_t *font;
    int x0;                     /* surface x of line x 0, unscrolled   */
    int y;                      /* the row's top                        */
    int step;                   /* the row's height                     */
    int64_t left;               /* line x at the text's left edge       */
    int64_t right;              /* line x past which nothing shows      */
    tiku_edit_pos_t stop;       /* the row's end, where the walk stops  */
    const tiku_edit_selection_t *sels; /* the selections, in order      */
    size_t nsels;
    size_t at;                  /* the first of sels not ended
                                   before the walk's cell               */
    char piece[PIECE_MAX + 5];  /* text waiting to be drawn, and a NUL  */
    size_t len;                 /* bytes in piece                       */
    int64_t piece_x;            /* line x of piece's first character    */
    tiku_rgb_t ink;             /* piece's colour                       */
    const tiku_font_t *face;    /* piece's face                         */
    int stopped;                /* the walk reached the right edge      */
    int invisibles;             /* mark spaces and tabs                 */
    tiku_edit_pos_t start;      /* the line's first byte                */
    const tiku_span_t *runs;    /* the line's runs, or NULL: plain      */
    size_t nruns;
    size_t run;                 /* the run the walk has reached         */
    int64_t run_end;            /* line offset where runs[run] ends     */
    const unsigned *styles;     /* the view's style of each ink         */
    const tiku_font_t *cuts[4]; /* the face in each cut, by the style's
                                   TIKU_CODEVIEW_BOLD and ITALIC        */
    int under;                  /* an underline is being gathered       */
    int64_t under_x, under_end; /* line x the underline runs from,
                                   and to                               */
    tiku_rgb_t under_ink;       /* the underline's colour               */
    tiku_edit_range_t hot;      /* the live hotspot under the pointer,
                                   underlined; empty for none           */
} row_t;

_Static_assert(TIKU_CODEVIEW_BOLD == TIKU_FONT_BOLD &&
               TIKU_CODEVIEW_ITALIC == TIKU_FONT_ITALIC,
               "a style's cut flags are the font kit's");

/** @brief The surface x of line x @p x, which is near the visible part. */
static int
surface_x(const row_t *r, int64_t x)
{
    return r->x0 + (int)(x - r->left);
}

/** @brief Where selection @p s starts. */
static tiku_edit_pos_t
sel_lo(const tiku_edit_selection_t *s)
{
    return s->anchor < s->caret ? s->anchor : s->caret;
}

/** @brief Where selection @p s ends. */
static tiku_edit_pos_t
sel_hi(const tiku_edit_selection_t *s)
{
    return s->anchor < s->caret ? s->caret : s->anchor;
}

/** @brief The first selection that ends after @p pos; they are apart,
 *         so their ends are in order as their starts are. */
static size_t
first_after(const row_t *r, tiku_edit_pos_t pos)
{
    size_t lo = 0, hi = r->nsels;

    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;

        if (sel_hi(&r->sels[mid]) <= pos)
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo;
}

/** @brief The part of @p a inside @p b. */
static tiku_rect_t
meet(tiku_rect_t a, tiku_rect_t b)
{
    int64_t x0 = a.x > b.x ? a.x : b.x;
    int64_t y0 = a.y > b.y ? a.y : b.y;
    int64_t x1 = (int64_t)a.x + a.w < (int64_t)b.x + b.w ?
                 (int64_t)a.x + a.w : (int64_t)b.x + b.w;
    int64_t y1 = (int64_t)a.y + a.h < (int64_t)b.y + b.h ?
                 (int64_t)a.y + a.h : (int64_t)b.y + b.h;
    tiku_rect_t r;

    r.x = (int)x0;
    r.y = (int)y0;
    r.w = x1 > x0 ? (int)(x1 - x0) : 0;
    r.h = y1 > y0 ? (int)(y1 - y0) : 0;
    return r;
}

/** @brief Fill the row from line x @p a to @p b, cut to what shows. */
static void
fill_span(row_t *r, int64_t a, int64_t b, tiku_rgb_t c)
{
    tiku_rect_t box;

    if (a < r->left - 1)
        a = r->left - 1;
    if (b > r->right + 1)
        b = r->right + 1;
    if (b <= a)
        return;
    box.x = surface_x(r, a);
    box.y = r->y;
    box.w = (int)(b - a);
    box.h = r->step;
    tiku_fill(r->s, box, c);
}

/** @brief Draw the text gathered so far. */
static void
flush(row_t *r)
{
    if (r->len == 0)
        return;
    r->piece[r->len] = '\0';
    tiku_text(r->s, r->face, surface_x(r, r->piece_x),
              r->y + r->font->ascent, r->piece, r->ink);
    r->len = 0;
}

/** @brief @p f in each cut a style can ask for, as near as the kit has:
 *         without italic where there is none, then as @p f.  A bold face
 *         stays bold in every cut. */
static void
cuts_of(const tiku_font_t *f, const tiku_font_t *cuts[4])
{
    unsigned base = tiku_font_styled(f, TIKU_FONT_BOLD) == f ?
                    TIKU_FONT_BOLD : 0;
    unsigned style;

    for (style = 0; style < 4; style++) {
        unsigned want = style | base;
        const tiku_font_t *c = want != 0 ? tiku_font_styled(f, want) : NULL;

        if (c == NULL && (want & TIKU_FONT_ITALIC))
            c = tiku_font_styled(f, want & ~TIKU_FONT_ITALIC);
        cuts[style] = c != NULL ? c : f;
    }
}

/** @brief Add glyph @p c to the piece in @p face and @p ink, starting a
 *         new piece where either changes.  A glyph @p face draws at
 *         another width than the view's face measured is drawn alone, at
 *         its own place. */
static void
gather(row_t *r, const tiku_cv_cell_t *c, const tiku_font_t *face,
       tiku_rgb_t ink)
{
    int alone = face != r->font && tiku_text_advance(face, c->cp) != c->adv;

    if (r->len > 0 && (ink != r->ink || face != r->face || alone ||
                       r->len + (size_t)c->len > PIECE_MAX))
        flush(r);
    if (r->len == 0) {
        r->piece_x = c->x;
        r->ink = ink;
        r->face = face;
    }
    memcpy(r->piece + r->len, c->bytes, (size_t)c->len);
    r->len += (size_t)c->len;
    if (alone)
        flush(r);
}

/** @brief Draw the underline gathered so far a pixel under the baseline,
 *         in the colour of the text above it. */
static void
underline_flush(row_t *r)
{
    if (!r->under)
        return;
    tiku_hline(r->s, surface_x(r, r->under_x), r->y + r->font->ascent + 1,
               (int)(r->under_end - r->under_x), r->under_ink);
    r->under = 0;
}

/** @brief Gather cell @p c into the underline in @p ink when @p on; one
 *         that is not, or another colour, ends the one gathered. */
static void
underline_cell(row_t *r, const tiku_cv_cell_t *c, int on, tiku_rgb_t ink)
{
    if (r->under && (!on || ink != r->under_ink || c->x != r->under_end))
        underline_flush(r);
    if (!on)
        return;
    if (!r->under) {
        r->under = 1;
        r->under_x = c->x;
        r->under_ink = ink;
    }
    r->under_end = c->x + c->adv;
}

/** @brief A control character: its name in the ground's colour on a box
 *         halfway between the ground and the ink. */
static void
paint_name(row_t *r, const tiku_cv_cell_t *c, int selected)
{
    tiku_rgb_t ground = selected ? TIKU_C_SELECT : TIKU_C_DOC;
    tiku_rgb_t ink = selected ? TIKU_C_SELTEXT : TIKU_C_TEXT;
    tiku_rect_t box;
    char name[5];

    (void)tiku_cv_control_name(c->cp, name);
    box.x = surface_x(r, c->x) + 1;
    box.y = r->y + 1;
    box.w = c->adv - 2;
    box.h = r->font->height - 1;
    if (box.w > 0 && box.h > 0)
        tiku_fill(r->s, box, tiku_dim(ink, ground));
    tiku_text(r->s, r->font, surface_x(r, c->x) + TIKU_CV_NAME_PAD,
              r->y + r->font->ascent, name, ground);
}

/** @brief A space's dot or a tab's dash in the middle of its cell, its
 *         ink faint against its ground. */
static void
paint_mark(row_t *r, const tiku_cv_cell_t *c, int selected, tiku_rgb_t ink)
{
    tiku_rgb_t ground = selected ? TIKU_C_SELECT : TIKU_C_DOC;

    tiku_hline(r->s, surface_x(r, c->x) + c->adv / 2,
               r->y + r->font->height / 2, c->kind == TIKU_CV_TAB ? 3 : 1,
               tiku_dim(ink, ground));
}

/** @brief The ink of the run holding line offset @p at, plain past the
 *         runs; the walk asks in order, so the run only moves forward. */
static tiku_ink_t
run_ink(row_t *r, int64_t at)
{
    while (r->run < r->nruns && at >= r->run_end) {
        r->run++;
        if (r->run < r->nruns)
            r->run_end += r->runs[r->run].len;
    }
    return r->run < r->nruns ? r->runs[r->run].ink : TIKU_INK_PLAIN;
}

/** @brief The style the view draws @p ink with. */
static unsigned
style_of(const unsigned *styles, tiku_ink_t ink)
{
    return (unsigned)ink < TIKU_CODEVIEW_STYLE_COUNT ? styles[ink] : 0;
}

/** @brief Paint one character, gathering text into pieces and underlines
 *         into lines. */
static int
paint_cell(void *ctx, const tiku_cv_cell_t *c)
{
    row_t *r = ctx;
    tiku_ink_t which = run_ink(r, c->pos - r->start);
    unsigned style = style_of(r->styles, which);
    tiku_rgb_t ink = tiku_cv_style_color(r->view, which);
    int selected;

    if (c->pos >= r->stop)
        return 1;
    while (r->at < r->nsels && sel_hi(&r->sels[r->at]) <= c->pos)
        r->at++;
    selected = r->at < r->nsels && sel_lo(&r->sels[r->at]) <= c->pos;
    if (selected)
        ink = TIKU_C_SELTEXT;
    if (c->x >= r->right) {
        r->stopped = 1;
        return 1;
    }
    if (c->x + c->adv <= r->left)
        return 0;
    underline_cell(r, c, (style & TIKU_CODEVIEW_UNDERLINE) ||
                         (c->pos >= r->hot.start && c->pos < r->hot.end),
                   ink);
    if (c->kind == TIKU_CV_GLYPH) {
        gather(r, c, r->cuts[style & 3u], ink);
    } else {
        flush(r);
        if (c->kind == TIKU_CV_CONTROL)
            paint_name(r, c, selected);
    }
    if (r->invisibles && (c->cp == ' ' || c->kind == TIKU_CV_TAB))
        paint_mark(r, c, selected, ink);
    return 0;
}

/** @brief Fill one character's cell with its style's own background, when
 *         it has one; the walk paints them before the selections and the
 *         indicators' marks, which are drawn over them. */
static int
background_cell(void *ctx, const tiku_cv_cell_t *c)
{
    row_t *r = ctx;
    const tiku_codeview_t *v = r->view;
    unsigned id;

    if (c->pos >= r->stop || c->x >= r->right)
        return 1;
    if (c->x + c->adv <= r->left)
        return 0;
    id = (unsigned)run_ink(r, c->pos - r->start);
    if (id < TIKU_CODEVIEW_STYLE_COUNT && v->style_defined[id] &&
        (v->style_defs[id].colors & TIKU_CODEVIEW_STYLE_BACK))
        fill_span(r, c->x, c->x + c->adv, v->style_defs[id].background);
    return 0;
}

/** @brief Whether any style the view defines has a background of its
 *         own. */
static int
any_backgrounds(const tiku_codeview_t *v)
{
    unsigned id;

    for (id = 0; id < TIKU_CODEVIEW_STYLE_COUNT; id++)
        if (v->style_defined[id] &&
            (v->style_defs[id].colors & TIKU_CODEVIEW_STYLE_BACK))
            return 1;
    return 0;
}

/** @brief Paint row @p w in the row @p r is set up for: the styles'
 *         backgrounds first, the ruler and indentation guides, the
 *         selections' ground, the indicators' marks, then the text over
 *         them, and on a line's last row a folded line's mark and a
 *         selected line break as one space's width after the text. */
static void
paint_row(tiku_codeview_t *v, row_t *r, const tiku_cv_row_t *w)
{
    tiku_edit_pos_t end = tiku_editdoc_line_end(v->doc, w->line);
    tiku_edit_pos_t next = tiku_editdoc_line_start(v->doc, w->line + 1);
    tiku_edit_pos_t upto = w->end, a, b;
    int64_t width;
    size_t k;

    r->left = v->left + w->x0;
    r->right = r->left + tiku_cv_text_w(v);
    r->stop = w->end;
    r->start = tiku_editdoc_line_start(v->doc, w->line);
    if (!tiku_cv_wraps(v) && end - r->start >= TIKU_CV_LONG) {
        tiku_edit_pos_t at = tiku_cv_char_at(v, w->line, r->right);

        if (at >= 0)
            upto = at;
    }
    r->runs = tiku_cv_style_line(v, w->line, upto, &r->nruns);
    r->run = 0;
    r->run_end = r->nruns > 0 ? r->runs[0].len : 0;
    if (any_backgrounds(v))
        (void)tiku_cv_walk_near(v, w->line, INT64_MAX, r->left,
                                background_cell, r);
    tiku_cv_guides_paint(v, r->s, w, r->y);
    r->at = first_after(r, w->start);
    for (k = r->at; k < r->nsels && sel_lo(&r->sels[k]) < w->end; k++) {
        a = sel_lo(&r->sels[k]) > w->start ? sel_lo(&r->sels[k]) : w->start;
        b = sel_hi(&r->sels[k]) < w->end ? sel_hi(&r->sels[k]) : w->end;
        if (a < b)
            fill_span(r, tiku_cv_x_of(v, w->line, a),
                      tiku_cv_x_of(v, w->line, b), TIKU_C_SELECT);
    }
    tiku_cv_marks_paint(v, r->s, w, r->y);
    r->len = 0;
    r->under = 0;
    r->stopped = 0;
    r->run = 0;
    r->run_end = r->nruns > 0 ? r->runs[0].len : 0;
    width = tiku_cv_walk_near(v, w->line, INT64_MAX, r->left, paint_cell, r);
    underline_flush(r);
    flush(r);
    if (w->last && tiku_codeview_folded(v, w->line))
        tiku_cv_fold_mark(v, r->s, surface_x(r, width), r->y);
    k = first_after(r, end);
    if (w->last && !r->stopped && next > end && k < r->nsels &&
        sel_lo(&r->sels[k]) <= end && sel_hi(&r->sels[k]) >= next)
        fill_span(r, width, width + tiku_text_advance(r->font, ' '),
                  TIKU_C_SELECT);
}

/** @brief Paint the rows from the first shown down to the foot of
 *         @p area, text and annotations; @return how many were painted. */
static int64_t
paint_rows(tiku_codeview_t *v, row_t *r, tiku_rect_t area)
{
    int64_t line = v->top, sub = v->top_sub, rows = 0;
    tiku_cv_row_t w;

    for (;;) {
        r->y = tiku_cv_text_y(v) + (int)rows * r->step;
        if (r->y >= area.y + area.h)
            break;
        tiku_cv_row(v, line, sub, &w);
        if (w.ann >= 0)
            tiku_cv_annotation_paint(v, r->s, &w, r->y);
        else
            paint_row(v, r, &w);
        rows++;
        if (tiku_cv_rows_step(v, &line, &sub, 1) == 0)
            break;
    }
    return rows;
}

/** @brief Put back the clip the caller had. */
static void
restore_clip(tiku_surface_t *s, tiku_rect_t saved)
{
    if (saved.x == 0 && saved.y == 0 && saved.w == s->w && saved.h == s->h)
        tiku_clip_reset(s);
    else
        tiku_clip_set(s, saved);
}

/** @brief The cell a block caret covers, found by a walk. */
typedef struct {
    tiku_edit_pos_t at;             /* the caret                        */
    tiku_cv_cell_t cell;            /* the cell starting at the caret   */
    int found;
} block_t;

/** @brief Stop at the cell starting at the caret. */
static int
block_cell(void *ctx, const tiku_cv_cell_t *c)
{
    block_t *b = ctx;

    if (c->pos < b->at)
        return 0;
    if (c->pos == b->at) {
        b->cell = *c;
        b->found = 1;
    }
    return 1;
}

/** @brief The face the character at @p pos on @p line is drawn in: the
 *         cut of @p cuts its ink's style asks for. */
static const tiku_font_t *
face_at(tiku_codeview_t *v, int64_t line, tiku_edit_pos_t pos,
        const tiku_font_t *const *cuts)
{
    int64_t at = pos - tiku_editdoc_line_start(v->doc, line), end = 0;
    size_t n = 0, i;
    const tiku_span_t *runs = tiku_cv_style_line(v, line, pos, &n);

    for (i = 0; runs != NULL && i < n; i++) {
        end += runs[i].len;
        if (at < end)
            return cuts[style_of(v->ink_styles, runs[i].ink) & 3u];
    }
    return cuts[style_of(v->ink_styles, TIKU_INK_PLAIN) & 3u];
}

/** @brief A block caret at surface @p x, @p y on row @p r: the cell after
 *         @p caret filled with the text's ink and its character drawn
 *         over it in the ground's, in its own cut, or a space's width
 *         where the row ends. */
static void
paint_block(tiku_codeview_t *v, tiku_surface_t *s, const tiku_cv_row_t *r,
            tiku_edit_pos_t caret, int x, int y,
            const tiku_font_t *const *cuts)
{
    const tiku_font_t *f = tiku_cv_font(v);
    tiku_rect_t box = { x, y, tiku_text_advance(f, ' '),
                        tiku_codeview_row_height(v) };
    char name[5];
    block_t b;

    memset(&b, 0, sizeof b);
    b.at = caret;
    if (caret < r->end)
        (void)tiku_cv_walk_near(v, r->line, caret, INT64_MAX, block_cell,
                                &b);
    if (b.found)
        box.w = b.cell.adv;
    tiku_fill(s, box, TIKU_C_TEXT);
    if (b.found && b.cell.kind == TIKU_CV_GLYPH) {
        tiku_text(s, face_at(v, r->line, caret, cuts), x, y + f->ascent,
                  b.cell.bytes, TIKU_C_DOC);
    } else if (b.found && b.cell.kind == TIKU_CV_CONTROL) {
        (void)tiku_cv_control_name(b.cell.cp, name);
        tiku_text(s, f, x + TIKU_CV_NAME_PAD, y + f->ascent, name,
                  TIKU_C_DOC);
    }
}

/** @brief A caret: a line the row's height at its x, or a block with
 *         TIKU_CODEVIEW_BLOCK_CARET, kept inside the text when blanks a
 *         row ends with pass its width. */
static void
paint_caret(tiku_codeview_t *v, tiku_surface_t *s, int64_t rows,
            tiku_edit_pos_t caret, const tiku_font_t *const *cuts)
{
    tiku_cv_row_t r;
    int64_t row, x;
    int step = tiku_codeview_row_height(v), sx, sy;

    tiku_cv_row_of(v, caret, caret == v->upstream, &r);
    row = tiku_cv_shown_row(v, r.line, r.sub);
    if (row < 0 || row >= rows || !tiku_codeview_shown(v, r.line))
        return;
    x = tiku_cv_x_of(v, r.line, caret) - r.x0 - v->left;
    if (tiku_cv_wraps(v) && x >= tiku_cv_text_w(v))
        x = tiku_cv_text_w(v) - 1;
    if (x < 0 || x > tiku_cv_text_w(v))
        return;
    sx = tiku_cv_text_x(v) + (int)x;
    sy = tiku_cv_text_y(v) + (int)row * step;
    if (v->options & TIKU_CODEVIEW_BLOCK_CARET)
        paint_block(v, s, &r, caret, sx, sy, cuts);
    else
        tiku_vline(s, sx, sy, step, TIKU_C_TEXT);
}

/** @brief The gutter: a shaded strip with a rule at its right edge, each
 *         shown line's number right-aligned a few pixels short of the
 *         rule or the fold column, on the baseline of the line's first
 *         row, and the fold column's boxes. */
static void
paint_gutter(tiku_codeview_t *v, tiku_surface_t *s, int width,
             tiku_rect_t area)
{
    const tiku_font_t *numbers = tiku_font_at(11);
    const tiku_font_t *f = tiku_cv_font(v);
    tiku_rect_t strip = { v->bounds.x, area.y, width, area.h };
    int64_t line = v->top, sub = v->top_sub;
    int y = tiku_cv_text_y(v), step = tiku_codeview_row_height(v);
    int right = strip.x + width -
                (v->options & TIKU_CODEVIEW_FOLDS ? TIKU_CV_FOLD_W : 0);
    char number[24];

    tiku_fill(s, strip, tiku_tint(TIKU_C_DOC, 1.04f));
    tiku_vline(s, strip.x + width - 1, strip.y, strip.h,
               tiku_tint(TIKU_C_DOC, 1.12f));
    for (; y < area.y + area.h; y += step) {
        if (sub == 0 && (v->options & TIKU_CODEVIEW_LINE_NUMBERS)) {
            snprintf(number, sizeof number, "%lld", (long long)(line + 1));
            tiku_text(s, numbers,
                      right - 6 - tiku_text_width(numbers, number),
                      y + f->ascent, number, tiku_ink(TIKU_INK_FAINT));
        }
        if (sub == 0)
            tiku_cv_folds_paint(v, s, line, y);
        if (tiku_cv_rows_step(v, &line, &sub, 1) == 0)
            break;
    }
}

/** @brief The line the caret line's fill goes on: the main caret's while
 *         TIKU_CODEVIEW_CARET_LINE is shown and the view focused, else
 *         -1. */
static int64_t
caret_line(tiku_codeview_t *v)
{
    tiku_edit_pos_t anchor, caret;

    if (!(v->options & TIKU_CODEVIEW_CARET_LINE) || !v->focused ||
        tiku_codeview_selection(v, &anchor, &caret) != TIKU_EDIT_OK)
        return -1;
    return tiku_editdoc_line_of(v->doc, caret);
}

/** @brief Fill the text rows of the execution line, of marked lines and
 *         of the caret line, one colour a row: the execution line's while
 *         it is current, else the highest filled marker kind's, else the
 *         caret line's. */
static void
paint_fills(tiku_codeview_t *v, tiku_surface_t *s, tiku_rect_t area)
{
    int64_t line = v->top, sub = v->top_sub, exec, mine = caret_line(v);
    int y = tiku_cv_text_y(v), step = tiku_codeview_row_height(v);
    tiku_rgb_t own = v->caret_fill_set ? v->caret_fill : TIKU_C_NOTE;

    if (tiku_editdoc_execution(v->doc, &exec) != TIKU_EDIT_OK)
        exec = -1;
    if (v->fill_kinds == 0 && exec < 0 && mine < 0)
        return;
    for (; y < area.y + area.h; y += step) {
        uint32_t kinds = v->fill_kinds ?
                         tiku_editdoc_markers_on(v->doc, line) &
                         v->fill_kinds : 0;
        tiku_rect_t row = { area.x, y, area.w, step };
        int kind = TIKU_EDIT_MARKER_KINDS - 1;
        int text = sub < tiku_cv_text_rows(v, line);

        if (text && line == exec) {
            tiku_fill(s, row, v->exec_fill);
        } else if (text && kinds != 0) {
            while (!(kinds & ((uint32_t)1 << kind)))
                kind--;
            tiku_fill(s, row, v->fills[kind]);
        } else if (text && line == mine) {
            tiku_fill(s, row, own);
        }
        if (tiku_cv_rows_step(v, &line, &sub, 1) == 0)
            break;
    }
}

/** @brief A scrollbar, its thumb placed in thousandths so a recording
 *         carries it where it is drawn here; an arrow held down is drawn
 *         pressed. */
static void
paint_bar(tiku_codeview_t *v, tiku_surface_t *s, int horizontal)
{
    tiku_rect_t bar = tiku_cv_bar(v, horizontal);
    int pos, frac;

    if (bar.w <= 0 || bar.h <= 0)
        return;
    tiku_cv_bar_place(v, horizontal, &pos, &frac);
    tiku_ui_scrollbar_held(s, bar, (float)pos / 1000.0f,
                           (float)frac / 1000.0f, horizontal,
                           v->arrow_bar == horizontal + 1 ? v->arrow_dir
                                                          : 0);
}

void
tiku_codeview_paint(tiku_codeview_t *view, tiku_surface_t *s)
{
    tiku_rect_t saved, full, area, text;
    size_t main, k;
    int64_t rows;
    int gutter;
    row_t r;

    if (view == NULL || s == NULL)
        return;
    saved = s->clip;
    full = meet(view->bounds, saved);
    if (full.w <= 0 || full.h <= 0)
        return;
    if (view->options & TIKU_CODEVIEW_HSCROLL)
        tiku_cv_clamp_across(view);
    else
        tiku_cv_clamp(view);
    area = tiku_cv_area(view);
    gutter = tiku_cv_gutter(view);
    tiku_clip_set(s, full);
    tiku_fill(s, area, TIKU_C_DOC);
    if (gutter > 0)
        paint_gutter(view, s, gutter, area);
    tiku_clip_set(s, meet(area, full));
    paint_fills(view, s, area);
    tiku_cv_brace_paint(view, s, tiku_codeview_rows_visible(view) + 1);
    tiku_clip_set(s, full);

    memset(&r, 0, sizeof r);
    r.s = s;
    r.view = view;
    r.font = tiku_cv_font(view);
    r.x0 = tiku_cv_text_x(view);
    r.step = tiku_codeview_row_height(view);
    r.invisibles = (view->options & TIKU_CODEVIEW_INVISIBLES) != 0;
    r.styles = view->ink_styles;
    cuts_of(r.font, r.cuts);
    if (!tiku_cv_hot_shown(view, &r.hot))
        r.hot.start = r.hot.end = 0;
    r.sels = tiku_cv_selections(view, &r.nsels, &main);
    if (r.sels == NULL) {
        /* With no room to read them all, the main selection is painted
         * alone. */
        (void)tiku_codeview_selection(view, &view->sel_one.anchor,
                                      &view->sel_one.caret);
        r.sels = &view->sel_one;
        r.nsels = 1;
    }
    text.x = r.x0;
    text.y = area.y;
    text.w = (int)tiku_cv_text_w(view);
    text.h = area.h;
    text = meet(text, saved);
    if (text.w > 0 && text.h > 0) {
        tiku_clip_set(s, text);
        rows = paint_rows(view, &r, area);
        for (k = 0; view->focused && k < r.nsels; k++)
            paint_caret(view, s, rows, r.sels[k].caret, r.cuts);
        tiku_clip_set(s, full);
    }
    tiku_clip_set(s, meet(area, full));
    tiku_cv_assist_paint(view, s);
    tiku_clip_set(s, full);
    paint_bar(view, s, 0);
    paint_bar(view, s, 1);
    if ((view->options & TIKU_CODEVIEW_VSCROLL) &&
        (view->options & TIKU_CODEVIEW_HSCROLL)) {
        tiku_rect_t corner = { view->bounds.x + view->bounds.w - TIKU_CV_BAR,
                               view->bounds.y + view->bounds.h - TIKU_CV_BAR,
                               TIKU_CV_BAR, TIKU_CV_BAR };

        tiku_fill(s, corner, TIKU_C_PANEL);
    }
    restore_clip(s, saved);
}
