/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_office_table.c - tables placed on pages.
 *
 * Every cell is laid out first, as a column as wide as the grid columns
 * it spans, so each row's height is known before any is placed: the
 * tallest cell, or what the row asks for, and a merged cell's rows
 * stretched to hold it.  Rows then go down the page, a new page taking
 * the rows that do not fit, with the table's heading rows again on top.
 * A row is not split: one taller than a page is left to run over.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_office_pager.h"

#include <stdlib.h>
#include <string.h>

/** @brief A cell laid out, waiting to be placed. */
typedef struct {
    const of_cell_t *c;             /* the cell                          */
    int   row, col, span;           /* where it is in the grid           */
    int   rows;                     /* rows it covers, merged downwards  */
    float h;                        /* its contents' height              */
    int   frag, nfrag;              /* its contents, in the table's list */
} tb_cell_t;

/** @brief A table being placed: its columns, cells and rows' heights. */
typedef struct {
    const of_table_t *t;
    float    *xs;                   /* each grid column's left, and end  */
    int       ncol;
    float     x;                    /* the table's left on the page      */
    tb_cell_t *cell;                /* laid out, row by row              */
    int       ncell;
    float    *rh;                   /* each row's height                 */
    of_vec_t  frags;                /* the cells' contents               */
} tb_t;

/** @brief Margin @p k of cell @p c: its own, else the table's. */
static float
margin(const of_table_t *t, const of_cell_t *c, int k)
{
    return c->margin[k] >= 0 ? c->margin[k] : t->margin[k];
}

/** @brief The grid's columns: as the table gives them, or shared equally
 *         when it gives none; made to fit when far too wide. */
static int
grid(tb_t *tb, float width)
{
    const of_table_t *t = tb->t;
    int n = t->ngrid, i, most = 1;
    float total = 0, scale = 1;

    for (i = 0; n == 0 && i < t->nrow; i++) {
        int k, cols = 0;

        for (k = 0; k < t->row[i].ncell; k++) {
            cols += t->row[i].cell[k].span > 0 ? t->row[i].cell[k].span : 1;
        }
        most = cols > most ? cols : most;
    }
    tb->ncol = n > 0 ? n : most;
    if ((tb->xs = calloc((size_t)tb->ncol + 1, sizeof *tb->xs)) == NULL) {
        return -1;
    }
    for (i = 0; i < tb->ncol; i++) {
        total += n > 0 && t->grid[i] > 0 ? t->grid[i] : width / tb->ncol;
    }
    if (total > width * 1.5f || total <= 0) {
        scale = total > 0 ? width / total : 1;
    }
    for (i = 0; i < tb->ncol; i++) {
        float w = n > 0 && t->grid[i] > 0 ? t->grid[i] : width / tb->ncol;

        tb->xs[i + 1] = tb->xs[i] + w * scale;
    }
    return 0;
}

/** @brief The cell above that a continuing merge at grid column @p col
 *         belongs to, in rows before @p row.  @return it, or NULL. */
static tb_cell_t *
merge_head(tb_t *tb, int row, int col)
{
    int i;

    for (i = tb->ncell - 1; i >= 0; i--) {
        tb_cell_t *c = &tb->cell[i];

        if (c->row < row && c->col == col && c->c->vmerge != 2) {
            return c->row + c->rows == row ? c : NULL;
        }
    }
    return NULL;
}

/** @brief Every cell laid out in its width, row by row; a merge's
 *         continuing cells lengthen the one that began it. */
static int
lay_cells(of_pager_t *pg, tb_t *tb)
{
    const of_table_t *t = tb->t;
    of_pageno_t no = { pg->num > 0 ? pg->num : 1, 0,
                       pg->sec ? pg->sec->num : 0 };
    int r, k;

    for (r = 0; r < t->nrow; r++) {
        int col = 0;

        for (k = 0; k < t->row[r].ncell && col < tb->ncol; k++) {
            const of_cell_t *c = &t->row[r].cell[k];
            int span = c->span > 0 ? c->span : 1;
            tb_cell_t *head = c->vmerge == 2 ? merge_head(tb, r, col) : NULL;
            tb_cell_t *tc = &tb->cell[tb->ncell];
            float w;

            if (span > tb->ncol - col) {
                span = tb->ncol - col;
            }
            if (head != NULL) {
                head->rows++;
                col += span;
                continue;
            }
            w = tb->xs[col + span] - tb->xs[col] - margin(t, c, 1) -
                margin(t, c, 3);
            memset(tc, 0, sizeof *tc);
            tc->c = c;
            tc->row = r;
            tc->col = col;
            tc->span = span;
            tc->rows = 1;
            tc->frag = tb->frags.n;
            tc->h = of_layout_story(pg->d, pg->fs, c->body, w, pg->no ?
                                    pg->no : &no, &tb->frags, pg->a);
            if (tc->h < 0) {
                return -1;
            }
            tc->nfrag = tb->frags.n - tc->frag;
            tb->ncell++;
            col += span;
        }
    }
    return 0;
}

/** @brief Each row's height: its tallest cell's, or what it asks for;
 *         then a merged cell's last row made up to hold it. */
static void
heights(tb_t *tb)
{
    const of_table_t *t = tb->t;
    int i, r;

    for (i = 0; i < tb->ncell; i++) {
        const tb_cell_t *c = &tb->cell[i];
        float need = c->h + margin(t, c->c, 0) + margin(t, c->c, 2);

        if (c->rows == 1 && need > tb->rh[c->row]) {
            tb->rh[c->row] = need;
        }
    }
    for (r = 0; r < t->nrow; r++) {
        if (t->row[r].exact && t->row[r].height > 0) {
            tb->rh[r] = t->row[r].height;
        } else if (t->row[r].height > tb->rh[r]) {
            tb->rh[r] = t->row[r].height;
        }
        if (tb->rh[r] < 1) {
            tb->rh[r] = 1;
        }
    }
    for (i = 0; i < tb->ncell; i++) {
        const tb_cell_t *c = &tb->cell[i];
        float need = c->h + margin(t, c->c, 0) + margin(t, c->c, 2), have = 0;

        for (r = c->row; r < c->row + c->rows && r < t->nrow; r++) {
            have += tb->rh[r];
        }
        if (c->rows > 1 && need > have) {
            tb->rh[c->row + c->rows - 1] += need - have;
        }
    }
}

/** @brief Border @p k of cell @p c: its own when it gives one, else the
 *         table's outer border at the table's edge, its inner one inside. */
static const of_border_t *
border(const tb_t *tb, const tb_cell_t *c, int k)
{
    const of_table_t *t = tb->t;
    int edge = k == 0 ? c->row == 0 : k == 1 ? c->col == 0 :
               k == 2 ? c->row + c->rows >= t->nrow
                      : c->col + c->span >= tb->ncol;

    if (c->c->border[k].width >= 0) {
        return &c->c->border[k];
    }
    return edge ? &t->border[k] : &t->border[k % 2 == 0 ? 4 : 5];
}

/** @brief Intersect a fragment's vertical clip with this page's cell. */
static void
clip_cell_fragment(of_frag_t *f, float top, float bottom)
{
    top -= f->y;
    bottom -= f->y;
    if (!f->clipped || f->clip_top < top) f->clip_top = top;
    if (!f->clipped || f->clip_bottom > bottom) f->clip_bottom = bottom;
    f->clipped = 1;
}

/** @brief Cell @p c drawn with its top at @p y: its shading, contents
 *         and borders. */
static int
place_cell(of_pager_t *pg, tb_t *tb, const tb_cell_t *c, float y, int first)
{
    const of_table_t *t = tb->t;
    float x = tb->x + tb->xs[c->col], w = tb->xs[c->col + c->span] -
              tb->xs[c->col], top = margin(t, c->c, 0), off = 0;
    struct { float full, before, visible; } height = {0};
    of_frag_t *f;
    int r, i, err = 0;

    for (r = c->row; r < c->row + c->rows && r < t->nrow; r++) {
        height.full += tb->rh[r];
        if (r < first) height.before += tb->rh[r];
    }
    for (r = first; r < c->row + c->rows && r < t->nrow; r++) {
        if (r > first && pg->lay && y + height.visible + tb->rh[r] >
            pg_limit(pg) + 0.01f) break;
        height.visible += tb->rh[r];
    }
    if (c->c->valign != 0) {
        off = height.full - top - margin(t, c->c, 2) - c->h;
        off = off > 0 ? (c->c->valign == 1 ? off / 2 : off) : 0;
    }
    err |= pg_box(pg->frags, x, y, w, height.visible, c->c->fill);
    for (i = 0; i < c->nfrag && err == 0; i++) {
        if ((f = of_vec_push(pg->frags)) == NULL) {
            return -1;
        }
        *f = ((of_frag_t *)tb->frags.p)[c->frag + i];
        f->x += x + margin(t, c->c, 1);
        f->y += y + top + off - height.before;
        if (c->rows > 1) {
            clip_cell_fragment(f, y, y + height.visible);
        }
    }
    if (first == c->row)
        err |= pg_rule(pg->frags, x, y, w, 0, border(tb, c, 0));
    if (height.before + height.visible >= height.full - 0.01f)
        err |= pg_rule(pg->frags, x, y + height.visible, w, 0, border(tb, c, 2));
    err |= pg_rule(pg->frags, x, y, 0, height.visible, border(tb, c, 1));
    err |= pg_rule(pg->frags, x + w, y, 0, height.visible, border(tb, c, 3));
    return err;
}

/** @brief Row @p r's cells drawn at the pager's y. */
static int
place_row(of_pager_t *pg, tb_t *tb, int r, int restart)
{
    int i, err = 0;

    for (i = 0; i < tb->ncell && err == 0; i++) {
        if (tb->cell[i].row == r || (restart && tb->cell[i].row < r &&
            tb->cell[i].row + tb->cell[i].rows > r)) {
            err = place_cell(pg, tb, &tb->cell[i], pg->y, r);
        }
    }
    pg->y += tb->rh[r];
    pg->content = 1;
    return err;
}

/** @brief The rows placed down the page, a new page taking a row that
 *         does not fit, the table's heading rows placed on it first. */
static int
place_rows(of_pager_t *pg, tb_t *tb)
{
    const of_table_t *t = tb->t;
    int r, k, heads = 0, err = 0;

    while (heads < t->nrow && t->row[heads].header) {
        heads++;
    }
    for (r = 0; r < t->nrow && err == 0; r++) {
        int restart = 0;
        if (pg->lay != NULL && pg->content && pg->y + tb->rh[r] >
            pg_limit(pg) + 0.01f) {
            err = pg_next_column(pg);
            restart = 1;
            for (k = 0; k < heads && r > heads && err == 0; k++) {
                err = place_row(pg, tb, k, 0);
            }
        }
        err |= place_row(pg, tb, r, restart);
    }
    return err;
}

int
pg_table(of_pager_t *pg, const of_table_t *t)
{
    tb_t tb;
    int err, cells = 0, i;
    float total, room;

    memset(&tb, 0, sizeof tb);
    tb.t = t;
    for (i = 0; i < t->nrow; i++) {
        cells += t->row[i].ncell;
    }
    of_vec_init(&tb.frags, sizeof(of_frag_t));
    tb.cell = calloc((size_t)cells + 1, sizeof *tb.cell);
    tb.rh = calloc((size_t)t->nrow + 1, sizeof *tb.rh);
    err = tb.cell == NULL || tb.rh == NULL || grid(&tb, pg->width) != 0;
    if (!err) {
        total = tb.xs[tb.ncol];
        room = pg->width - t->indent - total;
        tb.x = pg->x + t->indent + (t->align == OF_CENTER ? room / 2 :
                                    t->align == OF_RIGHT ? room : 0);
        pg->y += pg->after;
        pg->after = 0;
        err = lay_cells(pg, &tb);
    }
    if (!err) {
        heights(&tb);
        err = place_rows(pg, &tb);
    }
    pg->style = NULL;
    free(tb.xs);
    free(tb.cell);
    free(tb.rh);
    of_vec_free(&tb.frags);
    return err ? -1 : 0;
}
