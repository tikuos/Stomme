/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_office_chart_odf.c - an OpenDocument chart read into the chart
 * model: the chart:chart of an embedded object, its series' values,
 * names and categories taken from the table the object keeps of its
 * data, how each part looks from the object's automatic styles.
 *
 * Ranges name cells of that table ("local-table.$B$2:.$B$5"); a chart
 * whose data lives elsewhere -- a spreadsheet's -- has none here and is
 * not drawn.  Pies go anticlockwise from their angle, as LibreOffice
 * draws them; series without colours of their own take its palette.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_office_chart.h"
#include "tiku_office_odt.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define OC_ROWS (CH_POINTS + 2)     /* the data table's rows, at most    */
#define OC_COLS (2 * CH_SERIES + 2) /* and its columns                   */
#define GRAPHIC "style:graphic-properties"
#define CHARTP  "style:chart-properties"
#define TEXTP   "style:text-properties"

static const uint32_t lo_palette[12] = {
    0xff004586u, 0xffff420eu, 0xffffd320u, 0xff579d1cu, 0xff7e0021u,
    0xff83caffu, 0xff314004u, 0xffaecf00u, 0xff4b1f6fu, 0xffff950eu,
    0xffc5000bu, 0xff0084d1u};

/** @brief A cell of the data table: its number, its words. */
typedef struct {
    double      v;                  /* NAN when it holds none            */
    const char *text;               /* NULL when it says nothing         */
} oc_cell_t;

/** @brief A chart being read: the model, where its styles are, and its
 *         data table. */
typedef struct {
    ch_chart_t            *k;
    const tiku_xml_node_t *styles[3];  /* elements holding style:style  */
    const char            *plot;    /* the plot area's style             */
    const char            *cls;     /* the chart's class, "chart:bar" ... */
    oc_cell_t             *cell;    /* rows by columns                   */
    int                    rows, cols;
} oc_t;

/* ------------------------------------------------------------- styles  */

/** @brief The style called @p name (a style:style, or a number style),
 *         or NULL. */
static const tiku_xml_node_t *
style_of(const oc_t *o, const char *name)
{
    const tiku_xml_node_t *e;
    const char *n;
    int i;

    for (i = 0; name != NULL && i < 3; i++) {
        for (e = tiku_xml_child(o->styles[i], NULL); e != NULL;
             e = tiku_xml_next(e, NULL)) {
            n = tiku_xml_attr(e, "style:name");
            if (n != NULL && strcmp(n, name) == 0) {
                return e;
            }
        }
    }
    return NULL;
}

/** @brief Attribute @p attr of style @p name's @p group properties, or
 *         of the styles it is based on; NULL when none says. */
static const char *
prop(const oc_t *o, const char *name, const char *group, const char *attr)
{
    const tiku_xml_node_t *e;
    const char *v;
    int depth;

    for (depth = 0; name != NULL && depth < 4; depth++) {
        if ((e = style_of(o, name)) == NULL) {
            return NULL;
        }
        if ((v = tiku_xml_attr(tiku_xml_child(e, group), attr)) != NULL) {
            return v;
        }
        name = tiku_xml_attr(e, "style:parent-style-name");
    }
    return NULL;
}

/** @brief @p v as a number; @p dflt when it is missing or none. */
static double
num(const char *v, double dflt)
{
    double d = v != NULL && *v ? atof(v) : dflt;

    return isfinite(d) ? d : dflt;
}

/** @brief Whether @p v says "true". */
static int
yes(const char *v)
{
    return v != NULL && strcmp(v, "true") == 0;
}

/** @brief A "#rrggbb" colour as ARGB; 0 for none. */
static uint32_t
colour(const char *v)
{
    return v != NULL && v[0] == '#' && strlen(v) >= 7
           ? 0xff000000u | (uint32_t)strtoul(v + 1, NULL, 16) : 0;
}

/** @brief Style @p st's text look over @p f: size, face, weight, slant,
 *         colour. */
static void
font_of(const oc_t *o, const char *st, ch_font_t *f)
{
    const char *size = prop(o, st, TEXTP, "fo:font-size");
    const char *face = prop(o, st, TEXTP, "style:font-name");
    const char *wt = prop(o, st, TEXTP, "fo:font-weight");
    const char *it = prop(o, st, TEXTP, "fo:font-style");

    face = face != NULL ? face : prop(o, st, TEXTP, "fo:font-family");
    if (size != NULL && strchr(size, '%') == NULL) {
        f->size = od_length(size, f->size);
    }
    if (face != NULL) {
        snprintf(f->font, sizeof f->font, "%s", face);
    }
    if (wt != NULL) {
        f->bold = strcmp(wt, "bold") == 0 || atoi(wt) >= 600;
    }
    if (it != NULL) {
        f->italic = strcmp(it, "normal") != 0;
    }
    if (colour(prop(o, st, TEXTP, "fo:color")) != 0) {
        f->color = colour(prop(o, st, TEXTP, "fo:color"));
    }
}

/** @brief Style @p st's fill into @p f.  @return 1 when it states one. */
static int
fill_of(const oc_t *o, const char *st, dml_fill_t *f)
{
    const char *kind = prop(o, st, GRAPHIC, "draw:fill");
    const char *c = prop(o, st, GRAPHIC, "draw:fill-color");
    const char *op = prop(o, st, GRAPHIC, "draw:opacity");

    memset(f, 0, sizeof *f);
    if (kind != NULL && strcmp(kind, "none") == 0) {
        f->kind = DML_NOFILL;
        return 1;
    }
    if (colour(c) == 0) {
        return 0;
    }
    f->kind = DML_SOLID;            /* a gradient's or hatch's colour */
    f->color = colour(c);
    if (num(op, 100) < 100) {
        f->color = (f->color & 0xffffffu) |
                   (uint32_t)(fmax(num(op, 100), 0) * 2.55 + 0.5) << 24;
    }
    return 1;
}

/** @brief Style @p st's line into @p l: none, or solid or dashed in its
 *         colour and width (a hairline three quarters of a point).
 *  @return 1 when it states one. */
static int
line_of(const oc_t *o, const char *st, dml_line_t *l)
{
    const char *kind = prop(o, st, GRAPHIC, "draw:stroke");
    const char *c = prop(o, st, GRAPHIC, "svg:stroke-color");
    const char *w = prop(o, st, GRAPHIC, "svg:stroke-width");

    if (kind == NULL && c == NULL && w == NULL) {
        return 0;
    }
    memset(l, 0, sizeof *l);
    if (kind != NULL && strcmp(kind, "none") == 0) {
        return 1;                   /* width 0: no line */
    }
    l->color = colour(c) != 0 ? colour(c) : 0xff000000u;
    l->width = od_length(w, 0);
    l->width = l->width > 0 ? l->width : 0.75f;
    l->join = 1;
    if (kind != NULL && strcmp(kind, "dash") == 0) {
        l->dash[0] = 4;
        l->dash[1] = 3;
        l->ndash = 2;
    }
    return 1;
}

/** @brief Number style @p name as a spreadsheet format into @p out:
 *         decimals and grouping, a per cent, a currency's symbol, a
 *         date's parts; "General" for none. */
static void
number_format(const oc_t *o, const char *name, char *out, size_t max)
{
    const tiku_xml_node_t *e = style_of(o, name), *k;
    const char *v;
    size_t n = 0;
    int d;

    snprintf(out, max, "General");
    if (e == NULL || (!tiku_xml_is(e, "number:number-style") &&
                      !tiku_xml_is(e, "number:percentage-style") &&
                      !tiku_xml_is(e, "number:currency-style") &&
                      !tiku_xml_is(e, "number:date-style"))) {
        return;
    }
    out[0] = '\0';
    for (k = tiku_xml_child(e, NULL); k != NULL && n + 16 < max;
         k = tiku_xml_next(k, NULL)) {
        if (tiku_xml_is(k, "number:number")) {
            d = (v = tiku_xml_attr(k, "number:decimal-places")) != NULL
                ? (int)fmin(fmax(atoi(v), 0), 8) : -1;
            n += (size_t)snprintf(out + n, max - n, "%s%s%.*s",
                                  yes(tiku_xml_attr(k, "number:grouping"))
                                  ? "#,##0" : "0", d > 0 ? "." : "",
                                  d > 0 ? d : 0, "00000000");
        } else if (tiku_xml_is(k, "number:day")) {
            n += (size_t)snprintf(out + n, max - n, "d");
        } else if (tiku_xml_is(k, "number:month")) {
            n += (size_t)snprintf(out + n, max - n, yes(tiku_xml_attr(k,
                                  "number:textual")) ? "mmm" : "m");
        } else if (tiku_xml_is(k, "number:year")) {
            n += (size_t)snprintf(out + n, max - n, "yyyy");
        } else if (tiku_xml_is(k, "number:text") ||
                   tiku_xml_is(k, "number:currency-symbol")) {
            char t[16];

            tiku_xml_text(k, t, sizeof t);
            n += (size_t)snprintf(out + n, max - n, "%s%s%s",
                                  strcmp(t, "%") == 0 ? "" : "\"", t,
                                  strcmp(t, "%") == 0 ? "" : "\"");
        }
    }
    if (out[0] == '\0' || (strcmp(out, "0") == 0 && tiku_xml_attr(
            tiku_xml_child(e, "number:number"), "number:decimal-places")
            == NULL)) {
        snprintf(out, max, "General");  /* digits as they come */
    }
}

/* --------------------------------------------------------------- table  */

/** @brief How many rows table part @p t holds, repeats counted. */
static int
rows_in(const tiku_xml_node_t *t, int depth)
{
    const tiku_xml_node_t *e;
    const char *r;
    long n = 0;

    for (e = tiku_xml_child(t, NULL); e != NULL && depth < 4 &&
         n < OC_ROWS; e = tiku_xml_next(e, NULL)) {
        if (tiku_xml_is(e, "table:table-row")) {
            r = tiku_xml_attr(e, "table:number-rows-repeated");
            n += r != NULL && atol(r) > 1 ? atol(r) : 1;
        } else if (tiku_xml_is(e, "table:table-header-rows") ||
                   tiku_xml_is(e, "table:table-rows") ||
                   tiku_xml_is(e, "table:table-row-group")) {
            n += rows_in(e, depth + 1);
        }
    }
    return n < OC_ROWS ? (int)n : OC_ROWS;
}

/** @brief Cell @p e read into @p c: a number, its words. */
static void
cell_read(oc_t *o, const tiku_xml_node_t *e, oc_cell_t *c)
{
    const char *t = tiku_xml_attr(e, "office:value-type");
    const char *v = tiku_xml_attr(e, "office:value");
    char buf[256];

    c->v = NAN;
    c->text = NULL;
    if (t != NULL && v != NULL && (strcmp(t, "float") == 0 ||
                                   strcmp(t, "percentage") == 0 ||
                                   strcmp(t, "currency") == 0)) {
        c->v = strtod(v, NULL);
        c->v = isfinite(c->v) ? c->v : NAN;
    }
    tiku_xml_text(tiku_xml_child(e, "text:p"), buf, sizeof buf);
    if (buf[0] != '\0') {
        c->text = of_strdup(&o->k->mem, buf);
    }
}

/** @brief Row @p e into the table from row @p r, as many times as it is
 *         repeated.  @return how many rows it took. */
static int
row_read(oc_t *o, const tiku_xml_node_t *e, int r)
{
    const char *rep = tiku_xml_attr(e, "table:number-rows-repeated");
    const tiku_xml_node_t *c;
    int times = rep != NULL && atol(rep) > 1 ? (int)fmin(atol(rep),
                                                         OC_ROWS) : 1;
    int col = 0, n, i, t;
    oc_cell_t cell;

    times = times < o->rows - r ? times : o->rows - r;
    for (c = tiku_xml_child(e, NULL); c != NULL && col < o->cols;
         c = tiku_xml_next(c, NULL)) {
        if (!tiku_xml_is(c, "table:table-cell") &&
            !tiku_xml_is(c, "table:covered-table-cell")) {
            continue;
        }
        rep = tiku_xml_attr(c, "table:number-columns-repeated");
        n = rep != NULL && atol(rep) > 1 ? (int)fmin(atol(rep), OC_COLS)
                                         : 1;
        cell_read(o, c, &cell);
        for (i = 0; i < n && col < o->cols; i++, col++) {
            for (t = 0; t < times; t++) {
                o->cell[(size_t)(r + t) * (size_t)o->cols + (size_t)col] =
                    cell;
            }
        }
    }
    return times;
}

/** @brief The rows of table part @p t read from row @p r.  @return the
 *         row after them. */
static int
rows_read(oc_t *o, const tiku_xml_node_t *t, int r, int depth)
{
    const tiku_xml_node_t *e;

    for (e = tiku_xml_child(t, NULL); e != NULL && depth < 4 &&
         r < o->rows; e = tiku_xml_next(e, NULL)) {
        if (tiku_xml_is(e, "table:table-row")) {
            r += row_read(o, e, r);
        } else if (tiku_xml_is(e, "table:table-header-rows") ||
                   tiku_xml_is(e, "table:table-rows") ||
                   tiku_xml_is(e, "table:table-row-group")) {
            r = rows_read(o, e, r, depth + 1);
        }
    }
    return r;
}

/** @brief The chart's data table read.  @return 0, or -1 out of
 *         memory. */
static int
table_read(oc_t *o, const tiku_xml_node_t *t)
{
    size_t i, n;

    o->rows = rows_in(t, 0);
    o->cols = OC_COLS;
    n = (size_t)o->rows * (size_t)o->cols;
    if (n == 0) {
        return 0;
    }
    if ((o->cell = of_alloc(&o->k->mem, n * sizeof *o->cell)) == NULL) {
        return -1;
    }
    for (i = 0; i < n; i++) {
        o->cell[i].v = NAN;
    }
    (void)rows_read(o, t, 0, 0);
    return 0;
}

/** @brief One cell's place, "$B$2" after its table's name ("t.", or "."
 *         alone), as a column and row from 0.  @return 0, or -1. */
static int
cell_at(const char *s, int *col, int *row)
{
    const char *dot = NULL, *p;
    long c = 0, r = 0;
    int quoted = 0;

    for (p = s; *p; p++) {
        quoted ^= *p == '\'';
        dot = *p == '.' && !quoted ? p : dot;
    }
    for (p = dot != NULL ? dot + 1 : s, p += *p == '$'; (*p | 0x20) >= 'a'
         && (*p | 0x20) <= 'z' && c < 100000; p++) {
        c = c * 26 + ((*p | 0x20) - 'a' + 1);
    }
    for (p += *p == '$'; *p >= '0' && *p <= '9' && r < 10000000; p++) {
        r = r * 10 + (*p - '0');
    }
    *col = (int)c - 1;
    *row = (int)r - 1;
    return c > 0 && r > 0 ? 0 : -1;
}

/** @brief The cells address @p a names (its first range), in order, into
 *         @p out, room for @p max.  @return how many. */
static int
cells(const oc_t *o, const char *a, const oc_cell_t **out, int max)
{
    char one[2][160];
    int c[4], n = 0, i, j;
    const char *colon, *space;

    if (a == NULL || o->cell == NULL) {
        return 0;
    }
    space = strchr(a, ' ');
    colon = strchr(a, ':');
    colon = colon != NULL && (space == NULL || colon < space) ? colon : NULL;
    snprintf(one[0], sizeof one[0], "%.*s", (int)(colon != NULL ? colon - a
             : space != NULL ? space - a : (long)strlen(a)), a);
    snprintf(one[1], sizeof one[1], "%.*s", colon == NULL ? 0 :
             (int)(space != NULL ? space - colon - 1 : (long)strlen(colon + 1)),
             colon != NULL ? colon + 1 : "");
    if (cell_at(one[0], &c[0], &c[1]) != 0) {
        return 0;
    }
    if (colon == NULL || cell_at(one[1], &c[2], &c[3]) != 0) {
        c[2] = c[0];                /* one cell */
        c[3] = c[1];
    }
    for (i = c[1]; i <= c[3] && i < o->rows; i++) {
        for (j = c[0]; j <= c[2] && j < o->cols && n < max; j++) {
            out[n++] = &o->cell[(size_t)i * (size_t)o->cols + (size_t)j];
        }
    }
    return n;
}

/** @brief The numbers of the cells address @p a names, into a list the
 *         chart keeps, how many in @p n.  NULL for none. */
static double *
numbers(oc_t *o, const char *a, int *n)
{
    const oc_cell_t **c = malloc(CH_POINTS * sizeof *c);
    double *v = NULL;
    int i;

    *n = c != NULL ? cells(o, a, c, CH_POINTS) : 0;
    if (*n > 0 && (v = of_alloc(&o->k->mem, (size_t)*n * sizeof *v)) != NULL) {
        for (i = 0; i < *n; i++) {
            v[i] = c[i]->v;
        }
    }
    free(c);
    *n = v != NULL ? *n : 0;
    return v;
}

/** @brief The words of the cells address @p a names -- a number's own
 *         where it has none -- into a list the chart keeps, how many in
 *         @p n.  NULL for none. */
static const char **
words(oc_t *o, const char *a, int *n)
{
    const oc_cell_t **c = malloc(CH_POINTS * sizeof *c);
    const char **w = NULL;
    char buf[64];
    int i;

    *n = c != NULL ? cells(o, a, c, CH_POINTS) : 0;
    if (*n > 0 && (w = of_alloc(&o->k->mem, (size_t)*n * sizeof *w)) != NULL) {
        for (i = 0; i < *n; i++) {
            if (c[i]->text == NULL && !isnan(c[i]->v)) {
                ch_number(c[i]->v, "General", buf, sizeof buf);
            }
            w[i] = c[i]->text != NULL ? c[i]->text : isnan(c[i]->v) ? ""
                   : of_strdup(&o->k->mem, buf);
        }
    }
    free(c);
    *n = w != NULL ? *n : 0;
    return w;
}

/* ------------------------------------------------------------- series  */

/** @brief The kind a chart class names ("chart:bar" ...), or -1 for one
 *         not drawn here. */
static int
kind_of(const char *cls)
{
    static const struct { const char *name; int kind; } k[] = {
        {"chart:bar", CH_BAR}, {"chart:line", CH_LINE},
        {"chart:area", CH_AREA}, {"chart:circle", CH_PIE},
        {"chart:ring", CH_DOUGHNUT}, {"chart:scatter", CH_SCATTER},
        {"chart:bubble", CH_SCATTER}, {"chart:radar", CH_RADAR},
        {"chart:filled-radar", CH_RADAR}, {"chart:stock", CH_LINE}};
    size_t i;

    for (i = 0; cls != NULL && i < sizeof k / sizeof k[0]; i++) {
        if (strcmp(cls, k[i].name) == 0) {
            return k[i].kind;
        }
    }
    return -1;
}

/** @brief A named symbol as a marker. */
static int
marker_of(const char *type, const char *name)
{
    static const struct { const char *name; int mk; } m[] = {
        {"square", CH_MK_SQUARE}, {"diamond", CH_MK_DIAMOND},
        {"arrow-down", CH_MK_TRIANGLE}, {"arrow-up", CH_MK_TRIANGLE},
        {"arrow-right", CH_MK_TRIANGLE}, {"arrow-left", CH_MK_TRIANGLE},
        {"circle", CH_MK_CIRCLE}, {"x", CH_MK_X}, {"plus", CH_MK_PLUS},
        {"asterisk", CH_MK_X}, {"star", CH_MK_X},
        {"horizontal-bar", CH_MK_DASH}, {"vertical-bar", CH_MK_DASH},
        {"bow-tie", CH_MK_DIAMOND}, {"hourglass", CH_MK_DIAMOND}};
    size_t i;

    if (type == NULL || strcmp(type, "automatic") == 0) {
        return -1;
    }
    if (strcmp(type, "none") == 0) {
        return CH_MK_NONE;
    }
    for (i = 0; name != NULL && i < sizeof m / sizeof m[0]; i++) {
        if (strcmp(name, m[i].name) == 0) {
            return m[i].mk;
        }
    }
    return CH_MK_SQUARE;
}

/** @brief Where a label position puts a point's label. */
static int
label_at(const char *v)
{
    static const struct { const char *name; int at; } p[] = {
        {"outside", CH_AT_OUT}, {"inside", CH_AT_IN},
        {"center", CH_AT_CENTRE}, {"top", CH_AT_ABOVE},
        {"bottom", CH_AT_BELOW}, {"left", CH_AT_LEFT},
        {"right", CH_AT_RIGHT}, {"near-origin", CH_AT_BASE}};
    size_t i;

    for (i = 0; v != NULL && i < sizeof p / sizeof p[0]; i++) {
        if (strcmp(v, p[i].name) == 0) {
            return p[i].at;
        }
    }
    return -1;
}

/** @brief Series style @p st's look into @p s: fill, line, marker, what
 *         its labels say and where, curves, a slice pulled out. */
static void
series_look(oc_t *o, const char *st, ch_series_t *s)
{
    const char *says = prop(o, st, CHARTP, "chart:data-label-number");
    const char *ip = prop(o, st, CHARTP, "chart:interpolation");
    const char *sym = prop(o, st, CHARTP, "chart:symbol-type");
    const char *w = prop(o, st, CHARTP, "chart:symbol-width");

    ip = ip != NULL ? ip : prop(o, o->plot, CHARTP, "chart:interpolation");
    sym = sym != NULL ? sym : prop(o, o->plot, CHARTP, "chart:symbol-type");
    s->has_fill = fill_of(o, st, &s->fill);
    s->line.width = -1;
    s->has_line = line_of(o, st, &s->line);
    s->marker = marker_of(sym, prop(o, st, CHARTP, "chart:symbol-name"));
    s->mk_size = w != NULL ? (float)fmin(fmax(od_length(w, 5), 2), 40) : 5;
    s->smooth = ip != NULL && strcmp(ip, "none") != 0;
    s->labels = (says != NULL && strstr(says, "value") ? CH_LBL_VAL : 0) |
                (says != NULL && strstr(says, "percentage") ? CH_LBL_PCT
                                                            : 0) |
                (yes(prop(o, st, CHARTP, "chart:data-label-text"))
                 ? CH_LBL_CAT : 0) |
                (yes(prop(o, st, CHARTP, "chart:data-label-series"))
                 ? CH_LBL_SER : 0);
    s->lbl_at = label_at(prop(o, st, CHARTP, "chart:label-position"));
    s->explode = (float)fmin(fmax(num(prop(o, st, CHARTP,
                                           "chart:pie-offset"), 0), 0), 400);
    font_of(o, st, &s->lbl_font);
    number_format(o, tiku_xml_attr(style_of(o, st), "style:data-style-name"),
                  s->fmt, sizeof s->fmt);
}

/** @brief Room for each point's own look in @p s.  @return 0, or -1. */
static int
points_room(oc_t *o, ch_series_t *s)
{
    if (s->pt_fill != NULL || s->n <= 0) {
        return s->n <= 0 ? -1 : 0;
    }
    s->pt_fill = of_alloc(&o->k->mem, (size_t)s->n * sizeof *s->pt_fill);
    s->pt_line = of_alloc(&o->k->mem, (size_t)s->n * sizeof *s->pt_line);
    s->pt_has = of_alloc(&o->k->mem, (size_t)s->n);
    s->pt_explode = of_alloc(&o->k->mem, (size_t)s->n *
                             sizeof *s->pt_explode);
    return s->pt_fill != NULL && s->pt_line != NULL && s->pt_has != NULL &&
           s->pt_explode != NULL ? 0 : -1;
}

/** @brief A series' chart:data-point elements: each point's own fill,
 *         line and pull, as many points as each is repeated for. */
static void
points_read(oc_t *o, const tiku_xml_node_t *e, ch_series_t *s)
{
    const tiku_xml_node_t *p;
    const char *st, *rep, *off;
    int i = 0, n, j;

    for (p = tiku_xml_child(e, "chart:data-point"); p != NULL && i < s->n;
         p = tiku_xml_next(p, "chart:data-point")) {
        rep = tiku_xml_attr(p, "chart:repeated");
        n = rep != NULL && atol(rep) > 1 ? (int)fmin(atol(rep), s->n) : 1;
        st = tiku_xml_attr(p, "chart:style-name");
        if (st == NULL || points_room(o, s) != 0) {
            i += n;
            continue;
        }
        off = prop(o, st, CHARTP, "chart:pie-offset");
        for (j = 0; j < n && i < s->n; j++, i++) {
            s->pt_has[i] = (unsigned char)(fill_of(o, st, &s->pt_fill[i]) |
                                           line_of(o, st, &s->pt_line[i])
                                           << 1);
            s->pt_explode[i] = off != NULL ? (float)fmin(fmax(num(off, 0),
                                                              0), 400)
                                           : s->explode;
        }
    }
}

/** @brief Series element @p e read into @p s, the @p order'th: values
 *         (a bubble's sizes, its y and x from its domains), its name,
 *         its look.  @return 0, or -1 when it has no values. */
static int
series_read(oc_t *o, const tiku_xml_node_t *e, int bubble, ch_series_t *s,
            int order)
{
    const tiku_xml_node_t *d = tiku_xml_child(e, "chart:domain");
    const char *addr = "table:cell-range-address";
    const char **name;
    int n = 0, m = 0;

    s->idx = order;
    s->lbl_font = o->k->base;
    s->lbl_at = -1;
    snprintf(s->sep, sizeof s->sep, "; ");
    s->v = numbers(o, tiku_xml_attr(e, "chart:values-cell-range-address"),
                   &s->n);
    if (bubble && d != NULL) {      /* sizes, then y, then x */
        s->size = s->v;
        s->v = numbers(o, tiku_xml_attr(d, addr), &n);
        s->n = n < s->n ? n : s->n;
        d = tiku_xml_next(d, "chart:domain");
    }
    s->x = d != NULL ? numbers(o, tiku_xml_attr(d, addr), &m) : NULL;
    s->x = m >= s->n ? s->x : NULL;
    name = words(o, tiku_xml_attr(e, "chart:label-cell-address"), &n);
    s->name = n > 0 ? name[0] : NULL;
    series_look(o, tiku_xml_attr(e, "chart:style-name"), s);
    points_read(o, e, s);
    return s->v != NULL && s->n > 0 ? 0 : -1;
}

/* ------------------------------------------------------------- groups  */

/** @brief A property of the plot area's style, else of the primary y
 *         axis' (where LibreOffice keeps bars' gaps). */
static const char *
plot_prop(const oc_t *o, const char *ystyle, const char *attr)
{
    const char *v = prop(o, o->plot, CHARTP, attr);

    return v != NULL ? v : prop(o, ystyle, CHARTP, attr);
}

/** @brief Group @p g's shape for class @p cls: bars across, stacking,
 *         gaps, a pie's first slice (LibreOffice's angle counts from
 *         three o'clock, anticlockwise), a stock chart's lines. */
static void
group_shape(oc_t *o, const char *cls, const char *ystyle, ch_group_t *g)
{
    const char *v;

    g->across = g->kind == CH_BAR &&
                yes(plot_prop(o, ystyle, "chart:vertical"));
    g->grouping = yes(plot_prop(o, ystyle, "chart:percentage"))
                  ? CH_PERCENT : yes(plot_prop(o, ystyle, "chart:stacked"))
                  ? CH_STACKED : g->kind == CH_BAR ? CH_CLUSTERED
                                                   : CH_STANDARD;
    v = plot_prop(o, ystyle, "chart:gap-width");
    g->gap = (float)fmin(fmax(num(v, 100), 0), 500);
    v = plot_prop(o, ystyle, "chart:overlap");
    g->overlap = (float)fmin(fmax(num(v, g->grouping == CH_STACKED ||
                                      g->grouping == CH_PERCENT ? 100 : 0),
                                  -100), 100);
    g->vary = g->kind == CH_PIE || g->kind == CH_DOUGHNUT;
    g->hole = 50;
    v = plot_prop(o, ystyle, "chart:angle-offset");
    g->first = (float)fmod(fmod(90 - num(v, 90), 360) + 360, 360);
    g->ccw = 1;
    g->markers = 1;
    g->lines = 1;
    g->filled = strcmp(cls, "chart:filled-radar") == 0;
    g->bubble_scale = 100;
    if (strcmp(cls, "chart:stock") == 0) {  /* high-low lines, bars */
        g->hilo = 1;
        g->hilo_line.width = 0.75f;
        g->hilo_line.color = 0xff000000u;
        g->updown_gap = 150;
        g->up.kind = g->down.kind = DML_SOLID;
        g->up.color = 0xffffffffu;
        g->down.color = 0xff000000u;
        g->updown_line = g->hilo_line;
    }
}

/** @brief A stock chart's series @p s: its points joined only by the
 *         high-low lines, its first and last by up-down bars once there
 *         are four (open, low, high, close). */
static void
stock(ch_group_t *g, ch_series_t *s)
{
    if (!g->hilo) {
        return;
    }
    s->has_line = 1;
    s->line.width = 0;
    s->marker = CH_MK_NONE;
    g->updown = g->nser + 1 >= 4;
}

/** @brief Whether a series of @p kind and class @p cls, on the second
 *         axes or not, goes in group @p g (of class @p gcls), the last
 *         made. */
static int
same_group(const ch_group_t *g, int kind, int second, const char *cls,
           const char *gcls)
{
    return g->kind == kind && (g->val_ax == 2) == second &&
           strcmp(cls, gcls) == 0;
}

/** @brief The plot area's series gathered into groups: one for each run
 *         of series of one kind on one axis.  @return 0, or -1 out of
 *         memory. */
static int
groups_read(oc_t *o, const tiku_xml_node_t *pa, const char *ystyle)
{
    const tiku_xml_node_t *e;
    const char *cls, *gcls = "", *ax;
    ch_group_t *g = NULL;
    int n = 0, order = 0, kind, second;

    for (e = tiku_xml_child(pa, "chart:series"); e != NULL && n < CH_SERIES;
         e = tiku_xml_next(e, "chart:series")) {
        n++;
    }
    if (n == 0) {
        return 0;
    }
    if ((o->k->grp = of_alloc(&o->k->mem, (size_t)n * sizeof *g)) == NULL) {
        return -1;
    }
    for (e = tiku_xml_child(pa, "chart:series"); e != NULL && order < n;
         e = tiku_xml_next(e, "chart:series"), order++) {
        cls = tiku_xml_attr(e, "chart:class");
        cls = cls != NULL ? cls : o->cls;
        ax = tiku_xml_attr(e, "chart:attached-axis");
        second = ax != NULL && strstr(ax, "secondary") != NULL;
        if ((kind = kind_of(cls)) < 0) {
            continue;
        }
        if (g == NULL || !same_group(g, kind, second, cls, gcls)) {
            g = &o->k->grp[o->k->ngrp++];
            g->kind = kind;
            group_shape(o, cls, ystyle, g);
            g->cat_ax = second ? 3 : 0;
            g->val_ax = second ? 2 : 1;
            g->ser = of_alloc(&o->k->mem, (size_t)n * sizeof *g->ser);
            gcls = cls;
        }
        if (g->ser != NULL && series_read(o, e, strcmp(cls, "chart:bubble")
                                          == 0, &g->ser[g->nser],
                                          order) == 0) {
            stock(g, &g->ser[g->nser]);
            g->nser++;
        }
    }
    return o->k->mem.bad ? -1 : 0;
}

/* --------------------------------------------------------------- axes  */

/** @brief The words of element @p e's paragraphs, a line each, kept by
 *         the chart; NULL for none. */
static const char *
text_of(oc_t *o, const tiku_xml_node_t *e)
{
    const tiku_xml_node_t *p;
    char buf[512];
    size_t n = 0;

    for (p = tiku_xml_child(e, "text:p"); p != NULL && n + 2 < sizeof buf;
         p = tiku_xml_next(p, "text:p")) {
        if (n > 0) {
            buf[n++] = '\n';
        }
        n += tiku_xml_text(p, buf + n, sizeof buf - n);
        n = n < sizeof buf ? n : sizeof buf - 1;
    }
    buf[n] = '\0';
    return n > 0 ? of_strdup(&o->k->mem, buf) : NULL;
}

/** @brief An axis' line and gridlines: its style's, else LibreOffice's
 *         grey. */
static void
axis_lines(oc_t *o, const tiku_xml_node_t *e, const char *st, ch_axis_t *a)
{
    const tiku_xml_node_t *gr;
    const char *cls;
    dml_line_t *l;

    a->line.width = 0.75f;
    a->line.color = 0xffb3b3b3u;
    (void)line_of(o, st, &a->line);
    a->has_line = a->line.width > 0;
    for (gr = tiku_xml_child(e, "chart:grid"); gr != NULL;
         gr = tiku_xml_next(gr, "chart:grid")) {
        cls = tiku_xml_attr(gr, "chart:class");
        l = cls != NULL && strcmp(cls, "minor") == 0 ? &a->minor_line
                                                     : &a->grid_line;
        *(l == &a->grid_line ? &a->grid : &a->minor) = 1;
        l->width = 0.75f;
        l->color = l == &a->grid_line ? 0xffb3b3b3u : 0xffddddddu;
        (void)line_of(o, tiku_xml_attr(gr, "chart:style-name"), l);
    }
}

/** @brief An axis' scale as its style gives it: bounds, step, the
 *         minor step, a logarithm, its direction. */
static void
axis_scale(oc_t *o, const char *st, ch_axis_t *a)
{
    const char *min = prop(o, st, CHARTP, "chart:minimum");
    const char *max = prop(o, st, CHARTP, "chart:maximum");
    const char *step = prop(o, st, CHARTP, "chart:interval-major");
    const char *div = prop(o, st, CHARTP, "chart:interval-minor-divisor");

    a->has_min = isfinite(num(min, NAN));
    a->min = num(min, 0);
    a->has_max = isfinite(num(max, NAN));
    a->max = num(max, 0);
    a->unit = num(step, 0);
    a->has_unit = a->unit > 0;
    a->minor_unit = a->has_unit && num(div, 0) >= 1 ? a->unit / num(div, 1)
                                                    : 0;
    a->log = yes(prop(o, st, CHARTP, "chart:logarithmic")) ? 10 : 0;
    a->reversed = yes(prop(o, st, CHARTP, "chart:reverse-direction"));
}

/** @brief Axis element @p e read into @p a (@p x: the first dimension;
 *         @p values: it measures values): what it shows, its scale, its
 *         lines, its labels' look and format, its title. */
static void
axis_read(oc_t *o, const tiku_xml_node_t *e, ch_axis_t *a, int x,
          int values)
{
    const char *st = tiku_xml_attr(e, "chart:style-name");
    const char *outer = prop(o, st, CHARTP, "chart:tick-marks-major-outer");
    const char *inner = prop(o, st, CHARTP, "chart:tick-marks-major-inner");
    const char *vis = prop(o, st, CHARTP, "chart:visible");

    a->kind = values ? CH_VAL_AX : CH_CAT_AX;
    a->deleted = vis != NULL && strcmp(vis, "false") == 0;
    a->labels = yes(prop(o, st, CHARTP, "chart:display-label")) ||
                prop(o, st, CHARTP, "chart:display-label") == NULL;
    a->tick = (outer == NULL || yes(outer)) + 2 * yes(inner);
    axis_scale(o, st, a);
    axis_lines(o, e, st, a);
    number_format(o, tiku_xml_attr(style_of(o, st), "style:data-style-name"),
                  a->fmt, sizeof a->fmt);
    a->linked = yes(prop(o, st, CHARTP, "chart:link-data-style-to-source")) ||
                strcmp(a->fmt, "General") == 0;
    a->font = o->k->base;
    font_of(o, st, &a->font);
    a->between = 1;                 /* categories between the ticks,
                                       which the value axis says     */
    a->title_font = a->font;
    a->title_font.size = 9;
    font_of(o, tiku_xml_attr(tiku_xml_child(e, "chart:title"),
                             "chart:style-name"), &a->title_font);
    a->title = text_of(o, tiku_xml_child(e, "chart:title"));
}

/** @brief The plot area's axes, primary x and y first, then the second
 *         pair; where the x axis crosses the y (its origin). */
static void
axes_read(oc_t *o, const tiku_xml_node_t *pa, int xvalues)
{
    static const char pos[4] = {'b', 'l', 'r', 't'};
    const tiku_xml_node_t *e;
    const char *dim, *name, *origin;
    int i, slot;

    for (i = 0; i < 4; i++) {       /* x, y, the second y, the second x */
        o->k->ax[i].kind = (i == 0 || i == 3) && !xvalues ? CH_CAT_AX
                                                          : CH_VAL_AX;
        o->k->ax[i].pos = pos[i];
        o->k->ax[i].deleted = 1;    /* unless the file has it */
        o->k->ax[i].tick = 1;
        o->k->ax[i].labels = 1;
        o->k->ax[i].between = 1;
        o->k->ax[i].tight = i == 0 || i == 3;   /* LibreOffice stretches
                                                   only y to nought */
        o->k->ax[i].crosses = i >= 2 ? 2 : 0;   /* the second pair: over
                                                   the far side */
        snprintf(o->k->ax[i].fmt, sizeof o->k->ax[i].fmt, "General");
        o->k->ax[i].linked = 1;
        o->k->ax[i].font = o->k->base;
    }
    o->k->nax = 4;
    for (e = tiku_xml_child(pa, "chart:axis"); e != NULL;
         e = tiku_xml_next(e, "chart:axis")) {
        dim = tiku_xml_attr(e, "chart:dimension");
        name = tiku_xml_attr(e, "chart:name");
        if (dim == NULL || (strcmp(dim, "x") != 0 && strcmp(dim, "y") != 0)) {
            continue;               /* a third dimension: not drawn */
        }
        slot = (dim[0] == 'y') + (name != NULL &&
                                  strstr(name, "secondary") != NULL) * 2;
        slot = slot == 2 ? 3 : slot == 3 ? 2 : slot;
        axis_read(o, e, &o->k->ax[slot], dim[0] == 'x',
                  dim[0] == 'y' || xvalues);
        o->k->ax[slot].pos = pos[slot];
        origin = prop(o, tiku_xml_attr(e, "chart:style-name"), CHARTP,
                      "chart:origin");
        if (slot == 1 && isfinite(num(origin, NAN))) {  /* where x
                                                           crosses it */
            o->k->ax[0].crosses = 3;
            o->k->ax[0].cross_at = num(origin, 0);
        }
    }
}

/* -------------------------------------------------------------- chart  */

/** @brief The title and subtitle, a line under it, in the title's look. */
static void
titles(oc_t *o, const tiku_xml_node_t *ch)
{
    const tiku_xml_node_t *t = tiku_xml_child(ch, "chart:title");
    const char *main = text_of(o, t);
    const char *sub = text_of(o, tiku_xml_child(ch, "chart:subtitle"));
    char *both;
    size_t n;

    o->k->title_font = o->k->base;
    o->k->title_font.size = 13;
    font_of(o, tiku_xml_attr(t, "chart:style-name"), &o->k->title_font);
    if (main != NULL && sub != NULL) {
        n = strlen(main) + strlen(sub) + 2;
        if ((both = of_alloc(&o->k->mem, n)) != NULL) {
            snprintf(both, n, "%s\n%s", main, sub);
        }
        main = both;
    }
    o->k->title = main != NULL ? main : sub;
}

/** @brief The legend: whether there is one, where, in what look. */
static void
legend(oc_t *o, const tiku_xml_node_t *ch)
{
    const tiku_xml_node_t *lg = tiku_xml_child(ch, "chart:legend");
    const char *p = tiku_xml_attr(lg, "chart:legend-position");

    o->k->legend = lg != NULL;
    o->k->legend_pos = p == NULL || strstr(p, "end") != NULL ? 'r' :
                       strstr(p, "start") != NULL ? 'l' :
                       strcmp(p, "top") == 0 ? 't' : 'b';
    o->k->legend_font = o->k->base;
    font_of(o, tiku_xml_attr(lg, "chart:style-name"), &o->k->legend_font);
}

/** @brief The chart's and the plot's grounds and edges: the chart's
 *         style, and the wall's, which a pie or doughnut does not draw;
 *         what an empty cell does to a line. */
static void
grounds(oc_t *o, const tiku_xml_node_t *ch, const tiku_xml_node_t *pa)
{
    const char *cs = tiku_xml_attr(ch, "chart:style-name");
    const char *ws = tiku_xml_attr(tiku_xml_child(pa, "chart:wall"),
                                   "chart:style-name");
    const char *blank = prop(o, o->plot, CHARTP, "chart:treat-empty-cells");
    ch_chart_t *k = o->k;

    k->has_bg = fill_of(o, cs, &k->bg) && k->bg.kind != DML_NOFILL;
    k->has_border = line_of(o, cs, &k->border) && k->border.width > 0;
    ws = kind_of(o->cls) == CH_PIE || kind_of(o->cls) == CH_DOUGHNUT
         ? NULL : ws;               /* a pie has no wall */
    k->has_plot_bg = fill_of(o, ws, &k->plot_bg) &&
                     k->plot_bg.kind != DML_NOFILL;
    k->has_plot_border = line_of(o, ws, &k->plot_border) &&
                         k->plot_border.width > 0;
    k->blanks = blank == NULL || strcmp(blank, "leave-gap") == 0 ? 0 :
                strcmp(blank, "use-zero") == 0 ? 1 : 2;
}

/** @brief The categories: the primary x axis' range, as words. */
static void
categories(oc_t *o, const tiku_xml_node_t *pa)
{
    const tiku_xml_node_t *e, *c;
    int g, s;

    for (e = tiku_xml_child(pa, "chart:axis"); e != NULL;
         e = tiku_xml_next(e, "chart:axis")) {
        c = tiku_xml_child(e, "chart:categories");
        if (c != NULL && o->k->cat == NULL) {
            o->k->cat = words(o, tiku_xml_attr(c,
                              "table:cell-range-address"), &o->k->nnamed);
        }
    }
    for (g = 0; g < o->k->ngrp; g++) {
        for (s = 0; s < o->k->grp[g].nser; s++) {
            o->k->ncat = o->k->grp[g].ser[s].n > o->k->ncat
                         ? o->k->grp[g].ser[s].n : o->k->ncat;
        }
    }
}

int
ch_read_odf(ch_chart_t *k, const tiku_xml_node_t *doc,
            const tiku_xml_node_t *styles)
{
    const tiku_xml_node_t *ch = tiku_xml_path(doc,
                                "office:body/office:chart/chart:chart");
    const tiku_xml_node_t *pa = tiku_xml_child(ch, "chart:plot-area"), *e;
    const char *ystyle = NULL;
    oc_t o;

    memset(&o, 0, sizeof o);
    o.k = k;
    o.styles[0] = tiku_xml_child(doc, "office:automatic-styles");
    o.styles[1] = tiku_xml_child(styles, "office:styles");
    o.styles[2] = tiku_xml_child(styles, "office:automatic-styles");
    o.cls = tiku_xml_attr(ch, "chart:class");
    if (ch == NULL || o.cls == NULL ||
        table_read(&o, tiku_xml_child(ch, "table:table")) != 0) {
        return ch == NULL || o.cls == NULL ? 0 : -1;
    }
    o.plot = tiku_xml_attr(pa, "chart:style-name");
    k->palette = lo_palette;
    k->npalette = 12;
    k->base.size = k->base.size > 0 ? k->base.size : 10;
    k->base.color = 0xff000000u;
    k->known = kind_of(o.cls) >= 0;
    axes_read(&o, pa, kind_of(o.cls) == CH_SCATTER);
    for (e = tiku_xml_child(pa, "chart:axis"); e != NULL && ystyle == NULL;
         e = tiku_xml_next(e, "chart:axis")) {
        ystyle = tiku_xml_attr(e, "chart:dimension") != NULL &&
                 strcmp(tiku_xml_attr(e, "chart:dimension"), "y") == 0
                 ? tiku_xml_attr(e, "chart:style-name") : NULL;
    }
    if (groups_read(&o, pa, ystyle) != 0) {
        return -1;
    }
    categories(&o, pa);
    titles(&o, ch);
    legend(&o, ch);
    grounds(&o, ch, pa);
    return k->mem.bad ? -1 : k->ngrp;
}
