/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_office_pptx_table.c - a slide's graphic frames: tables drawn here,
 * charts, diagrams and the rest by dml_frame().
 *
 * A table's rows are as tall as they say, or as their tallest cell's
 * text needs.  Each cell takes its fill, borders and the weight and
 * colour of its text from its own properties, else from the part of the
 * table style it falls in -- the header row, a banded row, the first
 * column -- else from the style's whole table.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_office_pptx.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define EMU   12700.0f              /* EMUs to a point                   */
#define COLS  64                    /* grid columns a table may have     */
#define ROWS  512                   /* rows it may have                  */

/** @brief A table being drawn: its style, and which parts of it apply. */
typedef struct {
    px_t                  *x;
    const tiku_xml_node_t *style;   /* its a:tblStyle, or NULL           */
    tiku_xml_t            *styles;  /* the deck's tableStyles.xml        */
    int                    first_row, last_row, first_col, last_col;
    int                    band_row, band_col;
    int                    nrow, ncol;
    float                  xs[COLS + 1];  /* grid columns' lefts, and end */
    float                  rh[ROWS];      /* rows' heights               */
} tb_t;

/** @brief Whether flag attribute @p name of @p e is "1" or "true". */
static int
on(const tiku_xml_node_t *e, const char *name)
{
    const char *v = tiku_xml_attr(e, name);

    return v != NULL && (v[0] == '1' || v[0] == 't');
}

/** @brief The table style part cell (@p r, @p c) falls in, from kind
 *         @p n on, the most particular first: header and total rows, then
 *         first and last columns, then bands, then the whole table. */
static const tiku_xml_node_t *
part(const tb_t *t, int r, int c, int n)
{
    static const char *const names[] = {"a:firstRow", "a:lastRow",
        "a:firstCol", "a:lastCol", "a:band1H", "a:band2H", "a:band1V",
        "a:band2V", "a:wholeTbl"};
    int ok[9];
    const tiku_xml_node_t *p;

    ok[0] = t->first_row && r == 0;
    ok[1] = t->last_row && r == t->nrow - 1;
    ok[2] = t->first_col && c == 0;
    ok[3] = t->last_col && c == t->ncol - 1;
    ok[4] = t->band_row && (r - t->first_row) % 2 == 0;
    ok[5] = t->band_row && (r - t->first_row) % 2 == 1;
    ok[6] = t->band_col && (c - t->first_col) % 2 == 0;
    ok[7] = t->band_col && (c - t->first_col) % 2 == 1;
    ok[8] = 1;
    for (; n < 9; n++) {
        if (ok[n] && (p = tiku_xml_child(t->style, names[n])) != NULL) {
            return p;
        }
    }
    return NULL;
}

/** @brief The index of the part @p p is among the style's kinds. */
static int
part_index(const tiku_xml_node_t *p)
{
    static const char *const names[] = {"a:firstRow", "a:lastRow",
        "a:firstCol", "a:lastCol", "a:band1H", "a:band2H", "a:band1V",
        "a:band2V", "a:wholeTbl"};
    int i;

    for (i = 0; p != NULL && i < 9; i++) {
        if (tiku_xml_is(p, names[i])) {
            return i;
        }
    }
    return 9;
}

/** @brief The fill cell (@p r, @p c) takes: its own, else its parts'. */
static int
cell_fill(tb_t *t, const tiku_xml_node_t *tcpr, int r, int c,
          dml_fill_t *f)
{
    const tiku_xml_node_t *p;
    int from = 0;

    if (dml_fill(&t->x->dml, tcpr, f)) {
        return 1;
    }
    while ((p = part(t, r, c, from)) != NULL) {
        const tiku_xml_node_t *st = tiku_xml_child(p, "a:tcStyle");

        if (dml_fill(&t->x->dml, tiku_xml_child(st, "a:fill"), f) ||
            dml_ref_fill(&t->x->dml, tiku_xml_child(st, "a:fillRef"), 0, f))
            return 1;
        from = part_index(p) + 1;
    }
    return 0;
}

/** @brief The edge of part @p p's region side @p k (left, right, top,
 *         bottom) of cell (@p r, @p c) lies on: the region's own edge, or
 *         one between its cells.  Rows parts span a row, column parts a
 *         column, the whole table all of it. */
static const char *
edge_name(const tb_t *t, const tiku_xml_node_t *p, int k, int r, int c)
{
    static const char *const outer[4] = {"a:left", "a:right", "a:top",
                                         "a:bottom"};
    int i = part_index(p), rows = i == 0 || i == 1 || i == 4 || i == 5;
    int cols = i == 2 || i == 3 || i == 6 || i == 7;
    int first = k == 0 ? c == 0 : k == 1 ? c == t->ncol - 1 :
                k == 2 ? r == 0 : r == t->nrow - 1;

    if (k >= 2 && (rows || first)) {
        return outer[k];            /* a row part's own top and bottom */
    }
    if (k < 2 && (cols || first)) {
        return outer[k];
    }
    return k < 2 ? "a:insideV" : "a:insideH";
}

/** @brief Border @p k (left, right, top, bottom) of cell (@p r, @p c):
 *         its own, else the parts' it falls in, most particular first. */
static void
cell_line(tb_t *t, const tiku_xml_node_t *tcpr, int k, int r, int c,
          dml_line_t *l)
{
    static const char *const own[4] = {"a:lnL", "a:lnR", "a:lnT", "a:lnB"};
    const tiku_xml_node_t *p, *b;
    int from = 0;

    memset(l, 0, sizeof *l);
    l->color = 0xff000000u;
    if (dml_line(&t->x->dml, tiku_xml_child(tcpr, own[k]), l)) {
        return;
    }
    while ((p = part(t, r, c, from)) != NULL) {
        b = tiku_xml_child(tiku_xml_path(p, "a:tcStyle/a:tcBdr"),
                           edge_name(t, p, k, r, c));
        if (dml_line(&t->x->dml, tiku_xml_child(b, "a:ln"), l)) {
            return;
        }
        from = part_index(p) + 1;
    }
    l->width = 0;
}

/** @brief A straight edge from (@p x0, @p y0) to (@p x1, @p y1) through
 *         @p m, as line @p l draws it. */
static void
edge(tb_t *t, const dml_line_t *l, float x0, float y0, float x1, float y1,
     const dml_mat_t *m)
{
    float seg[6] = {'M', 0, 0, 'L', 0, 0};
    dml_geom_t g;

    memset(&g, 0, sizeof g);
    seg[1] = x0;
    seg[2] = y0;
    seg[4] = x1;
    seg[5] = y1;
    g.nsub = 1;
    g.sub[0].stroke = 1;
    g.sub[0].pt = seg;
    g.sub[0].n = 6;
    dml_paint_line(&t->x->dml, l, &g, m);
}

/** @brief The weight and colour a cell's text takes from its parts; the
 *         colour, when none gives one, is the theme's text colour. */
static void
cell_text_style(tb_t *t, int r, int c, uint32_t *color, int *bold)
{
    const tiku_xml_node_t *p, *ts;
    const char *b;
    int from = 0;

    *color = 0;
    *bold = 0;
    while ((p = part(t, r, c, from)) != NULL) {
        ts = tiku_xml_child(p, "a:tcTxStyle");
        b = tiku_xml_attr(ts, "b");
        if (b != NULL && !*bold) {
            *bold = strcmp(b, "on") == 0 ? 1 : 2;
        }
        if (*color == 0) {
            *color = dml_color(&t->x->dml, ts);
            *color = *color ? *color : dml_color(&t->x->dml,
                                                 tiku_xml_child(ts,
                                                                "a:fontRef"));
        }
        from = part_index(p) + 1;
    }
    *bold = *bold == 1;
    if (*color == 0 && t->x->dml.theme != NULL) {
        *color = t->x->dml.theme->clr[t->x->dml.map[1]];
    }
}

/** @brief A cell's margins and anchor from its a:tcPr. */
static void
cell_margins(const tiku_xml_node_t *tcpr, float *ins, int *anchor)
{
    static const char *const m[4] = {"marL", "marT", "marR", "marB"};
    static const float dflt[4] = {7.2f, 3.6f, 7.2f, 3.6f};
    const char *a = tiku_xml_attr(tcpr, "anchor");
    int k;

    for (k = 0; k < 4; k++) {
        const char *v = tiku_xml_attr(tcpr, m[k]);

        ins[k] = v != NULL ? (float)atof(v) / EMU : dflt[k];
    }
    *anchor = a == NULL ? 0 : strcmp(a, "ctr") == 0 ? 1 :
              strcmp(a, "b") == 0 ? 2 : 0;
}

/** @brief How far right cell (@p col, spanning @p span) reaches. */
static float
right_of(const tb_t *t, int col, int span)
{
    int end = col + (span > 0 ? span : 1);

    return t->xs[end < t->ncol ? end : t->ncol];
}

/** @brief One cell drawn in @p box (left, top, right, bottom): fill, text
 *         and borders; or, when not @p draw, only measured.  @return its
 *         text's height, its top and bottom margins included. */
static float
cell(tb_t *t, const tiku_xml_node_t *tc, int r, int c, const float *box,
     const dml_mat_t *m, int draw)
{
    const tiku_xml_node_t *tcpr = tiku_xml_child(tc, "a:tcPr");
    float ins[4], h;
    uint32_t color;
    int anchor, bold, k;
    dml_fill_t f;
    dml_geom_t g;
    dml_line_t l;

    cell_margins(tcpr, ins, &anchor);
    cell_text_style(t, r, c, &color, &bold);
    if (draw) {
        dml_mat_t at = {1, 0, 0, 1, box[0], box[1]};

        at = dml_mul(&at, m);
        if (dml_geometry(NULL, box[2] - box[0], box[3] - box[1], &g) == 0 &&
            cell_fill(t, tcpr, r, c, &f)) {
            dml_paint_fill(&t->x->dml, &f, &g, &at, box[2] - box[0],
                           box[3] - box[1]);
        }
        dml_geom_free(&g);
    }
    h = px_body(t->x, tiku_xml_child(tc, "a:txBody"), box, ins, anchor, m,
                color, bold, draw);
    for (k = 0; draw && k < 4; k++) {
        cell_line(t, tcpr, k, r, c, &l);
        if (l.width > 0) {
            edge(t, &l, k == 1 ? box[2] : box[0], k == 3 ? box[3] : box[1],
                 k == 0 ? box[0] : box[2], k == 2 ? box[1] : box[3], m);
        }
    }
    return h + ins[1] + ins[3];
}

/** @brief The rows walked: measured into @p t->rh when @p draw is 0,
 *         drawn when 1. */
static void
rows(tb_t *t, const tiku_xml_node_t *tbl, const dml_mat_t *m, int draw)
{
    const tiku_xml_node_t *tr, *tc;
    float y = 0;
    int r = 0, c;

    for (tr = tiku_xml_child(tbl, "a:tr"); tr != NULL && r < t->nrow;
         tr = tiku_xml_next(tr, "a:tr"), r++) {
        for (tc = tiku_xml_child(tr, "a:tc"), c = 0; tc != NULL &&
             c < t->ncol; tc = tiku_xml_next(tc, "a:tc"), c++) {
            const char *span = tiku_xml_attr(tc, "gridSpan");
            const char *rs = tiku_xml_attr(tc, "rowSpan");
            float box[4] = {t->xs[c], y, right_of(t, c, span ? atoi(span)
                            : 1), y + t->rh[r]}, h;
            int k;

            if (tiku_xml_attr(tc, "hMerge") || tiku_xml_attr(tc, "vMerge"))
                continue;
            for (k = 1; rs != NULL && k < atoi(rs) && r + k < t->nrow; k++) {
                box[3] += t->rh[r + k];
            }
            h = cell(t, tc, r, c, box, m, draw);
            if (!draw && (rs == NULL || atoi(rs) <= 1) && h > t->rh[r]) {
                t->rh[r] = h;
            }
        }
        y += t->rh[r];
    }
}

/** @brief The table's style from the deck's table styles, by its id or,
 *         when it names none, the deck's default. */
static void
find_style(tb_t *t, const tiku_xml_node_t *pr)
{
    const char *id;
    const tiku_xml_node_t *s;
    char want[64];

    (void)tiku_xml_text(tiku_xml_child(pr, "a:tableStyleId"), want,
                        sizeof want);
    t->styles = of_pkg_xml(t->x->pkg, "ppt/tableStyles.xml");
    id = want[0] ? want : tiku_xml_attr(tiku_xml_root(t->styles), "def");
    for (s = tiku_xml_child(tiku_xml_root(t->styles), "a:tblStyle"); s &&
         id; s = tiku_xml_next(s, "a:tblStyle")) {
        if (tiku_xml_attr(s, "styleId") != NULL &&
            strcmp(tiku_xml_attr(s, "styleId"), id) == 0) {
            t->style = s;
            return;
        }
    }
}

/** @brief A table: its grid, its rows' heights, then its cells. */
static int
table(px_t *x, const tiku_xml_node_t *tbl, const dml_mat_t *m)
{
    const tiku_xml_node_t *pr = tiku_xml_child(tbl, "a:tblPr"), *g, *tr;
    tb_t *t = calloc(1, sizeof *t);

    if (t == NULL) {
        return -1;
    }
    t->x = x;
    find_style(t, pr);
    t->first_row = on(pr, "firstRow");
    t->last_row = on(pr, "lastRow");
    t->first_col = on(pr, "firstCol");
    t->last_col = on(pr, "lastCol");
    t->band_row = on(pr, "bandRow");
    t->band_col = on(pr, "bandCol");
    for (g = tiku_xml_path(tbl, "a:tblGrid/a:gridCol"); g != NULL &&
         t->ncol < COLS; g = tiku_xml_next(g, "a:gridCol"), t->ncol++) {
        t->xs[t->ncol + 1] = t->xs[t->ncol] + (tiku_xml_attr(g, "w") ?
                             (float)atof(tiku_xml_attr(g, "w")) / EMU : 72);
    }
    for (tr = tiku_xml_child(tbl, "a:tr"); tr != NULL && t->nrow < ROWS;
         tr = tiku_xml_next(tr, "a:tr"), t->nrow++) {
        t->rh[t->nrow] = tiku_xml_attr(tr, "h") ?
                         (float)atof(tiku_xml_attr(tr, "h")) / EMU : 18;
    }
    rows(t, tbl, m, 0);
    rows(t, tbl, m, 1);
    tiku_xml_free(t->styles);
    free(t);
    return 0;
}

int
px_frame(px_t *x, const tiku_xml_node_t *frame, const dml_mat_t *m)
{
    const tiku_xml_node_t *tbl = tiku_xml_path(frame, "a:graphic/"
                                               "a:graphicData/a:tbl");
    dml_mat_t fm, none;
    float w, h;

    if (tbl == NULL) {
        return dml_frame(&x->dml, frame, m);
    }
    dml_xfrm(tiku_xml_child(frame, "p:xfrm"), m, &fm, &w, &h, &none);
    return table(x, tbl, &fm);
}
