/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_office_odt_table.c - an OpenDocument table read: its columns,
 * repeated as they say and fitted to the table's width; its rows, header
 * rows marked to repeat atop each page; and its cells with their spans.
 *
 * A cell spanning columns hides the covered cells after it in its row; a
 * cell spanning rows begins a vertical merge that the covered cells below
 * it continue.  Borders and shading are the cells' own.  Repeats are
 * bounded: REPEAT_MAX for one row or cell, ROWS_MAX rows in all.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_office_odt.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GRID_MAX 256                /* columns a table may have          */
#define ROWS_MAX 20000              /* rows, repeats counted             */
#define REPEAT_MAX 64               /* times a row or cell is repeated   */

/** @brief A table being read: its rows, and the merges running down. */
typedef struct {
    of_vec_t rows;                  /* of_row_t                          */
    int      down[GRID_MAX];        /* rows a merge above still covers   */
    int      span[GRID_MAX];        /* the columns that merge spans      */
} od_tab_t;

/** @brief Attribute @p name of @p e as a count of at least 1 and at most
 *         @p most. */
static int
count(const tiku_xml_node_t *e, const char *name, int most)
{
    const char *v = tiku_xml_attr(e, name);
    int n = v != NULL ? atoi(v) : 1;

    return n < 1 ? 1 : n > most ? most : n;
}

/* ------------------------------------------------------------- columns  */

/** @brief The columns under @p e -- themselves, or in groups -- their
 *         widths into @p w (@p rel: shares, "*") and counted in @p n. */
static void
columns_in(od_t *x, const tiku_xml_node_t *e, float *w, int *rel, int *n)
{
    const tiku_xml_node_t *c;

    for (c = tiku_xml_child(e, NULL); c != NULL; c = tiku_xml_next(c, NULL)) {
        if (tiku_xml_is(c, "table:table-column")) {
            const char *st = tiku_xml_attr(c, "table:style-name");
            const char *p = "style:table-column-properties";
            const char *abs = od_attr(x, OD_COLUMN, st, p,
                                      "style:column-width");
            const char *share = od_attr(x, OD_COLUMN, st, p,
                                        "style:rel-column-width");
            int k = count(c, "table:number-columns-repeated", GRID_MAX);

            while (k-- > 0 && *n < GRID_MAX) {
                rel[*n] = abs == NULL && share != NULL;
                w[*n] = rel[*n] ? (float)atof(share) : od_length(abs, 0);
                (*n)++;
            }
        } else if (tiku_xml_is(c, "table:table-columns") ||
                   tiku_xml_is(c, "table:table-header-columns") ||
                   tiku_xml_is(c, "table:table-column-group")) {
            columns_in(x, c, w, rel, n);
        }
    }
}

/** @brief The table's grid: its columns' widths, shares of the table's
 *         width made points, a column with none given an even part.
 *  @return 0, or -1 out of memory. */
static int
grid(od_t *x, const tiku_xml_node_t *e, float width, of_table_t *t)
{
    float w[GRID_MAX], shares = 0, fixed = 0;
    int rel[GRID_MAX], n = 0, i;

    columns_in(x, e, w, rel, &n);
    for (i = 0; i < n; i++) {
        shares += rel[i] ? w[i] : 0;
        fixed += rel[i] ? 0 : w[i];
    }
    if (n == 0 || (t->grid = of_alloc(&x->d->arena, (size_t)n *
                                      sizeof *t->grid)) == NULL) {
        return n == 0 ? 0 : -1;
    }
    for (i = 0; i < n; i++) {
        t->grid[i] = rel[i] ? (shares > 0 ? w[i] / shares : 0) *
                              (width > fixed ? width - fixed : width)
                            : w[i] > 0 ? w[i] : width > 0 ? width / n : 72;
    }
    t->ngrid = n;
    return 0;
}

/* --------------------------------------------------------------- cells  */

/** @brief A cell's properties from its style: shading, borders, padding
 *         and how its contents sit. */
static void
cell_props(od_t *x, const char *st, of_cell_t *c)
{
    static const char *const side[4] = {"top", "left", "bottom", "right"};
    const char *p = "style:table-cell-properties";
    const char *all = od_attr(x, OD_CELL, st, p, "fo:border");
    const char *pad = od_attr(x, OD_CELL, st, p, "fo:padding");
    const char *va = od_attr(x, OD_CELL, st, p, "style:vertical-align");
    char name[32];
    int k;

    c->fill = od_color(od_attr(x, OD_CELL, st, p, "fo:background-color"));
    c->valign = va == NULL ? 0 : strcmp(va, "middle") == 0 ? 1 :
                strcmp(va, "bottom") == 0 ? 2 : 0;
    for (k = 0; k < 4; k++) {
        const char *v;

        snprintf(name, sizeof name, "fo:border-%s", side[k]);
        v = od_attr(x, OD_CELL, st, p, name);
        v = v != NULL ? v : all;
        if (v != NULL) {
            od_border(v, &c->border[k]);
        } else {
            c->border[k].width = -1;    /* the table's */
        }
        snprintf(name, sizeof name, "fo:padding-%s", side[k]);
        v = od_attr(x, OD_CELL, st, p, name);
        v = v != NULL ? v : pad;
        c->margin[k] = v != NULL ? od_length(v, 0) : -1;
    }
}

/** @brief A table:table-cell read into @p c: its looks, the columns it
 *         spans, whether it begins a merge down, its contents.
 *  @return 0, or -1 out of memory. */
static int
cell(od_t *x, const tiku_xml_node_t *e, of_cell_t *c)
{
    cell_props(x, tiku_xml_attr(e, "table:style-name"), c);
    c->span = count(e, "table:number-columns-spanned", GRID_MAX);
    c->vmerge = count(e, "table:number-rows-spanned", ROWS_MAX) > 1;
    if (x->depth >= OF_NEST_MAX) {
        return 0;
    }
    x->depth++;
    c->body = od_story(x, e);
    x->depth--;
    return c->body != NULL ? 0 : -1;
}

/** @brief One cell element at grid column @p col: a cell, or a covered
 *         one carrying a merge from above or hidden by a span beside it
 *         (@p skip counts those left).  @return 0, or -1. */
static int
cell_at(od_t *x, od_tab_t *tb, const tiku_xml_node_t *e, int col, int *skip,
        of_vec_t *cells)
{
    of_cell_t *c;

    if (tiku_xml_is(e, "table:covered-table-cell")) {
        if (col < GRID_MAX && tb->down[col] > 0) {
            if ((c = of_vec_push(cells)) == NULL) {
                return -1;
            }
            c->span = tb->span[col];
            c->vmerge = 2;
            memset(c->border, 0, sizeof c->border);
            c->border[0].width = c->border[1].width = -1;
            c->border[2].width = c->border[3].width = -1;
            c->margin[0] = c->margin[1] = c->margin[2] = c->margin[3] = -1;
            tb->down[col]--;
            *skip = tb->span[col] - 1;
        } else if (*skip > 0) {
            (*skip)--;
        }
        return 0;
    }
    if ((c = of_vec_push(cells)) == NULL || cell(x, e, c) != 0) {
        return -1;
    }
    if (col < GRID_MAX && c->vmerge) {
        tb->down[col] = count(e, "table:number-rows-spanned", ROWS_MAX) - 1;
        tb->span[col] = c->span;
    }
    *skip = c->span - 1;
    return 0;
}

/* ---------------------------------------------------------------- rows  */

/** @brief A table:table-row read into @p r, a header row when @p header.
 *  @return 0, or -1 out of memory. */
static int
row(od_t *x, od_tab_t *tb, const tiku_xml_node_t *e, int header,
    of_row_t *r)
{
    const char *st = tiku_xml_attr(e, "table:style-name");
    const char *p = "style:table-row-properties";
    const char *exact = od_attr(x, OD_ROW, st, p, "style:row-height");
    const char *least = od_attr(x, OD_ROW, st, p, "style:min-row-height");
    const tiku_xml_node_t *c;
    of_vec_t cells;
    int col = 0, skip = 0, err = 0, k;

    r->exact = exact != NULL;
    r->height = od_length(exact != NULL ? exact : least, 0);
    r->header = (unsigned char)header;
    of_vec_init(&cells, sizeof(of_cell_t));
    for (c = tiku_xml_child(e, NULL); c != NULL && err == 0;
         c = tiku_xml_next(c, NULL)) {
        if (!tiku_xml_is(c, "table:table-cell") &&
            !tiku_xml_is(c, "table:covered-table-cell")) {
            continue;
        }
        k = count(c, "table:number-columns-repeated", REPEAT_MAX);
        while (k-- > 0 && err == 0 && col < GRID_MAX) {
            err = cell_at(x, tb, c, col++, &skip, &cells);
        }
    }
    r->cell = of_vec_freeze(&cells, &x->d->arena, &r->ncell);
    of_vec_free(&cells);
    return err ? -1 : 0;
}

/** @brief The rows under @p e -- itself, or header rows, row groups --
 *         into the table, repeats bounded.  @return 0, or -1. */
static int
rows_in(od_t *x, od_tab_t *tb, const tiku_xml_node_t *e, int header)
{
    const tiku_xml_node_t *c;
    of_row_t *r;
    int err = 0, k;

    for (c = tiku_xml_child(e, NULL); c != NULL && err == 0;
         c = tiku_xml_next(c, NULL)) {
        if (tiku_xml_is(c, "table:table-row")) {
            k = count(c, "table:number-rows-repeated", REPEAT_MAX);
            while (k-- > 0 && err == 0 && tb->rows.n < ROWS_MAX) {
                err = (r = of_vec_push(&tb->rows)) == NULL ||
                      row(x, tb, c, header, r) != 0;
            }
        } else if (tiku_xml_is(c, "table:table-header-rows")) {
            err = rows_in(x, tb, c, 1);
        } else if (tiku_xml_is(c, "table:table-rows") ||
                   tiku_xml_is(c, "table:table-row-group")) {
            err = rows_in(x, tb, c, header);
        }
    }
    return err ? -1 : 0;
}

int
od_table(od_t *x, const tiku_xml_node_t *e, of_table_t *t)
{
    const char *st = tiku_xml_attr(e, "table:style-name");
    const char *p = "style:table-properties";
    const char *align = od_attr(x, OD_TABLE, st, p, "table:align");
    float width = od_length(od_attr(x, OD_TABLE, st, p, "style:width"), 0);
    od_tab_t *tb = calloc(1, sizeof *tb);
    int err;

    if (tb == NULL) {
        return -1;
    }
    t->align = align == NULL ? OF_LEFT : strcmp(align, "center") == 0
               ? OF_CENTER : strcmp(align, "right") == 0 ? OF_RIGHT
                                                         : OF_LEFT;
    t->indent = od_length(od_attr(x, OD_TABLE, st, p, "fo:margin-left"), 0);
    err = grid(x, e, width, t);
    of_vec_init(&tb->rows, sizeof(of_row_t));
    err = err || rows_in(x, tb, e, 0) != 0;
    t->row = of_vec_freeze(&tb->rows, &x->d->arena, &t->nrow);
    of_vec_free(&tb->rows);
    free(tb);
    return err ? -1 : 0;
}
