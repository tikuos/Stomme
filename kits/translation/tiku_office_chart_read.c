/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_office_chart_read.c - a DrawingML chart part (c:chartSpace) read
 * into a chart: the values its series cache from the workbook behind
 * them, their names and categories, how each is filled and outlined,
 * the axes, title and legend, and the places the file gives them.
 *
 * Only the caches are read; the workbook itself is never opened, so a
 * chart shows what the office suite last saw.  Bars, lines, areas, pies,
 * doughnuts, scatters, bubbles and radars are read, 3-D kinds drawn
 * flat; any other kind is passed over.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_office_chart.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------------------------------------------------------------- values  */

/** @brief Attribute val of child @p name of @p e as a number; @p dflt
 *         when either is missing or it is no number. */
static double
val_of(const tiku_xml_node_t *e, const char *name, double dflt)
{
    const char *v = tiku_xml_attr(tiku_xml_child(e, name), "val");
    double d = v != NULL && *v ? atof(v) : dflt;

    return isfinite(d) ? d : dflt;
}

/** @brief Index attribute @p v as a number below @p n; -1 when it is
 *         missing or out of range. */
static int
index_of(const char *v, long n)
{
    char *end;
    long i = v != NULL ? strtol(v, &end, 10) : -1;

    return v != NULL && end != v && i >= 0 && i < n ? (int)i : -1;
}

/** @brief Attribute val of child @p name of @p e, or NULL. */
static const char *
str_of(const tiku_xml_node_t *e, const char *name)
{
    return tiku_xml_attr(tiku_xml_child(e, name), "val");
}

/** @brief Whether boolean element @p k is set: present, and its val
 *         (true when it has none) not false. */
static int
is_on(const tiku_xml_node_t *k)
{
    const char *v = tiku_xml_attr(k, "val");

    return k != NULL && (v == NULL || strcmp(v, "1") == 0 ||
                         strcmp(v, "true") == 0);
}

/** @brief Whether boolean child @p name of @p e is set. */
static int
on(const tiku_xml_node_t *e, const char *name)
{
    return is_on(tiku_xml_child(e, name));
}

/** @brief The element called @p local, whatever its prefix, inside one of
 *         @p e's extensions (c:extLst/c:ext), or NULL: where later
 *         versions of Office keep what the first did not have. */
static const tiku_xml_node_t *
ext_child(const tiku_xml_node_t *e, const char *local)
{
    const tiku_xml_node_t *x, *k;

    for (x = tiku_xml_path(e, "c:extLst/c:ext"); x != NULL;
         x = tiku_xml_next(x, "c:ext")) {
        for (k = tiku_xml_child(x, NULL); k != NULL;
             k = tiku_xml_next(k, NULL)) {
            const char *colon = strchr(k->name, ':');

            if (strcmp(colon != NULL ? colon + 1 : k->name, local) == 0) {
                return k;
            }
        }
    }
    return NULL;
}

/** @brief The words of a c:rich text body: its runs, a line between its
 *         paragraphs and at each break, kept in the chart's memory.
 *         NULL for none. */
static const char *
rich_text(ch_chart_t *k, const tiku_xml_node_t *rich)
{
    const tiku_xml_node_t *p, *r;
    char buf[512], one[256];
    size_t used = 0;

    buf[0] = '\0';
    for (p = tiku_xml_child(rich, "a:p"); p != NULL;
         p = tiku_xml_next(p, "a:p")) {
        if (used > 0 && used + 1 < sizeof buf) {
            buf[used++] = '\n';
            buf[used] = '\0';
        }
        for (r = tiku_xml_child(p, NULL); r != NULL;
             r = tiku_xml_next(r, NULL)) {
            if (tiku_xml_is(r, "a:r") || tiku_xml_is(r, "a:fld") ||
                tiku_xml_is(r, "a:br")) {
                (void)tiku_xml_text(tiku_xml_child(r, "a:t"), one,
                                    sizeof one);
                used += (size_t)snprintf(buf + used, sizeof buf - used,
                                         "%s", tiku_xml_is(r, "a:br")
                                         ? "\n" : one);
                used = used < sizeof buf ? used : sizeof buf - 1;
            }
        }
    }
    return buf[0] ? of_strdup(&k->mem, buf) : NULL;
}

/* ----------------------------------------------------------------- looks  */

/** @brief Text properties @p rpr (an a:defRPr or a:rPr) over @p f. */
static void
rpr_over(ch_chart_t *k, const tiku_xml_node_t *rpr, ch_font_t *f)
{
    const char *v;
    uint32_t c;

    if (rpr == NULL) {
        return;
    }
    if ((v = tiku_xml_attr(rpr, "sz")) != NULL && atof(v) >= 100) {
        f->size = (float)(atof(v) / 100.0);
        f->size = f->size > 400 ? 400 : f->size;
    }
    if ((v = tiku_xml_attr(rpr, "b")) != NULL) {
        f->bold = strcmp(v, "1") == 0 || strcmp(v, "true") == 0;
    }
    if ((v = tiku_xml_attr(rpr, "i")) != NULL) {
        f->italic = strcmp(v, "1") == 0 || strcmp(v, "true") == 0;
    }
    if ((c = dml_color(k->c, tiku_xml_child(rpr, "a:solidFill"))) != 0) {
        f->color = c;
    }
    v = tiku_xml_attr(tiku_xml_child(rpr, "a:latin"), "typeface");
    if (v != NULL && *v) {
        snprintf(f->font, sizeof f->font, "%s", v);
    }
}

/** @brief An element's text properties (its c:txPr) over @p f. */
static void
txpr_over(ch_chart_t *k, const tiku_xml_node_t *e, ch_font_t *f)
{
    rpr_over(k, tiku_xml_path(e, "c:txPr/a:p/a:pPr/a:defRPr"), f);
}

/** @brief The manual layout of @p e when it places it from the frame's
 *         edges: x y w h into @p at.  @return whether it does (@p need
 *         the numbers it must give: 2 a place, 4 a box); @p inner (may be
 *         NULL) whether the box is the plot's inside. */
static int
layout_read(const tiku_xml_node_t *e, float *at, int need, int *inner)
{
    const tiku_xml_node_t *ml = tiku_xml_path(e, "c:layout/c:manualLayout");
    const char *xm = str_of(ml, "c:xMode"), *ym = str_of(ml, "c:yMode");
    const char *target = str_of(ml, "c:layoutTarget");
    static const char *const name[] = {"c:x", "c:y", "c:w", "c:h"};
    int i;

    if (xm == NULL || ym == NULL || strcmp(xm, "edge") != 0 ||
        strcmp(ym, "edge") != 0) {
        return 0;
    }
    for (i = 0; i < need; i++) {
        at[i] = (float)val_of(ml, name[i], -1);
        if (at[i] < 0 || at[i] > 1) {
            return 0;
        }
    }
    if (inner != NULL) {
        *inner = target != NULL && strcmp(target, "inner") == 0;
    }
    return need < 4 || (at[2] > 0.01f && at[3] > 0.01f);
}

/* ------------------------------------------------------------------ data  */

/** @brief The number of points a cache holds, bounded. */
static int
count_of(const tiku_xml_node_t *cache)
{
    double n = val_of(cache, "c:ptCount", 0);

    return n < 0 ? 0 : n > CH_POINTS ? CH_POINTS : (int)n;
}

/** @brief A number cache or literal's values (a c:numRef or c:numLit
 *         under @p e), NAN where a point is missing; their count into
 *         @p n, their format into @p fmt (may be NULL). */
static double *
numbers(ch_chart_t *k, const tiku_xml_node_t *e, int *n, char *fmt)
{
    const tiku_xml_node_t *c = tiku_xml_path(e, "c:numRef/c:numCache");
    const tiku_xml_node_t *pt;
    char buf[64];
    double *v;
    int i;

    c = c != NULL ? c : tiku_xml_child(e, "c:numLit");
    *n = count_of(c);
    if (*n == 0 || (v = of_alloc(&k->mem, (size_t)*n * sizeof *v)) == NULL) {
        return NULL;
    }
    for (i = 0; i < *n; i++) {
        v[i] = NAN;
    }
    for (pt = tiku_xml_child(c, "c:pt"); pt; pt = tiku_xml_next(pt, "c:pt")) {
        int at = index_of(tiku_xml_attr(pt, "idx"), *n);

        if (at >= 0 &&
            tiku_xml_text(tiku_xml_child(pt, "c:v"), buf, sizeof buf) > 0) {
            v[at] = isfinite(atof(buf)) ? atof(buf) : NAN;
        }
    }
    if (fmt != NULL) {
        (void)tiku_xml_text(tiku_xml_child(c, "c:formatCode"), buf,
                            sizeof buf);
        snprintf(fmt, CH_FMT, "%.63s", buf[0] ? buf : "General");
    }
    return v;
}

/** @brief Cache @p c's words: as many as it counts, from the c:pt of
 *         @p pts (itself, or a level of it), "" where one is missing.
 *         Their count into @p n; NULL for none. */
static const char **
cache_words(ch_chart_t *k, const tiku_xml_node_t *c,
            const tiku_xml_node_t *pts, int *n)
{
    const tiku_xml_node_t *pt;
    char buf[160];
    const char **s;
    int i;

    *n = count_of(c);
    if (*n == 0 || (s = of_alloc(&k->mem, (size_t)*n * sizeof *s)) == NULL) {
        *n = 0;
        return NULL;
    }
    for (pt = tiku_xml_child(pts, "c:pt"); pt; pt = tiku_xml_next(pt,
         "c:pt")) {
        int at = index_of(tiku_xml_attr(pt, "idx"), *n);

        if (at >= 0 && s[at] == NULL) {
            (void)tiku_xml_text(tiku_xml_child(pt, "c:v"), buf, sizeof buf);
            s[at] = of_strdup(&k->mem, buf);
        }
    }
    for (i = 0; i < *n; i++) {
        s[i] = s[i] != NULL ? s[i] : "";
    }
    return s;
}

/** @brief A string cache's words (c:strRef, c:strLit, the innermost
 *         level of c:multiLvlStrRef).  Their count into @p n; NULL, with
 *         none counted, for a cache of numbers. */
static const char **
words(ch_chart_t *k, const tiku_xml_node_t *e, int *n)
{
    const tiku_xml_node_t *c = tiku_xml_path(e, "c:strRef/c:strCache");

    c = c != NULL ? c : tiku_xml_child(e, "c:strLit");
    c = c != NULL ? c : tiku_xml_path(e, "c:multiLvlStrRef/"
                                         "c:multiLvlStrCache");
    return cache_words(k, c, tiku_xml_is(c, "c:multiLvlStrCache")
                             ? tiku_xml_child(c, "c:lvl") : c, n);
}

/** @brief An area's edge (the chart's, the plot's), over @p l: only when
 *         its a:ln @p ln names a colour, as Office leaves one that gives
 *         only a width undrawn.  @return whether there is one. */
static int
edge_read(ch_chart_t *k, const tiku_xml_node_t *ln, dml_line_t *l)
{
    if (tiku_xml_child(ln, "a:solidFill") == NULL &&
        tiku_xml_child(ln, "a:gradFill") == NULL &&
        tiku_xml_child(ln, "a:pattFill") == NULL) {
        return 0;
    }
    return dml_line(k->c, ln, l) && l->width > 0;
}

/** @brief A title's words and look: its rich text, or the cell it names
 *         as last seen, and its first run's properties over @p f.  NULL
 *         words for a title that has none. */
static const char *
title_read(ch_chart_t *k, const tiku_xml_node_t *t, ch_font_t *f)
{
    const tiku_xml_node_t *rich = tiku_xml_path(t, "c:tx/c:rich");
    const char *words_of, **cell;
    int n = 0;

    txpr_over(k, t, f);
    rpr_over(k, tiku_xml_path(rich, "a:p/a:pPr/a:defRPr"), f);
    rpr_over(k, tiku_xml_path(rich, "a:p/a:r/a:rPr"), f);
    words_of = rich_text(k, rich);
    cell = words_of == NULL ? words(k, tiku_xml_child(t, "c:tx"), &n) : NULL;
    return cell != NULL && n > 0 && cell[0][0] ? cell[0] : words_of;
}

/** @brief The outer levels of multi-level categories (two at most): a
 *         name where each group starts, NULL through the rest of it. */
static void
levels(ch_chart_t *k, const tiku_xml_node_t *cat, ch_series_t *s)
{
    const tiku_xml_node_t *c = tiku_xml_path(cat, "c:multiLvlStrRef/"
                                                  "c:multiLvlStrCache");
    const tiku_xml_node_t *lv = tiku_xml_child(c, "c:lvl"), *pt;
    char buf[160];
    int n = count_of(c), at;

    for (lv = tiku_xml_next(lv, "c:lvl"); lv != NULL && s->nouter < 2 &&
         n > 0; lv = tiku_xml_next(lv, "c:lvl")) {
        const char **o = of_alloc(&k->mem, (size_t)n * sizeof *o);

        for (pt = tiku_xml_child(lv, "c:pt"); o != NULL && pt != NULL;
             pt = tiku_xml_next(pt, "c:pt")) {
            if ((at = index_of(tiku_xml_attr(pt, "idx"), n)) >= 0) {
                (void)tiku_xml_text(tiku_xml_child(pt, "c:v"), buf,
                                    sizeof buf);
                o[at] = of_strdup(&k->mem, buf);
            }
        }
        if (o != NULL) {
            s->outer[s->nouter++] = o;
        }
    }
}

/** @brief A series' categories: words, or numbers (years, dates) kept as
 *         numbers and written out as their format says. */
static void
categories(ch_chart_t *k, const tiku_xml_node_t *cat, ch_series_t *s)
{
    char buf[160];
    int n = 0, i;

    if ((s->cat = words(k, cat, &n)) != NULL) {
        s->cat = n >= s->n ? s->cat : NULL;
        levels(k, cat, s);
        return;
    }
    s->catv = numbers(k, cat, &n, s->cat_fmt);
    if (s->catv == NULL || n < s->n ||
        (s->cat = of_alloc(&k->mem, (size_t)n * sizeof *s->cat)) == NULL) {
        s->catv = NULL;
        return;
    }
    for (i = 0; i < n; i++) {
        ch_number(s->catv[i], s->cat_fmt, buf, sizeof buf);
        s->cat[i] = isnan(s->catv[i]) ? "" : of_strdup(&k->mem, buf);
    }
}

/* ---------------------------------------------------------------- series  */

/** @brief Where a label's c:dLblPos @p p puts it: CH_AT_*, or @p dflt
 *         for none, or "bestFit" (-1). */
static int
label_at(const char *p, int dflt)
{
    static const struct { const char *name; int at; } pos[] = {
        {"outEnd", CH_AT_OUT}, {"inEnd", CH_AT_IN}, {"ctr", CH_AT_CENTRE},
        {"inBase", CH_AT_BASE}, {"t", CH_AT_ABOVE}, {"b", CH_AT_BELOW},
        {"l", CH_AT_LEFT}, {"r", CH_AT_RIGHT}, {"bestFit", -1}};
    size_t i;

    for (i = 0; p != NULL && i < sizeof pos / sizeof pos[0]; i++) {
        if (strcmp(p, pos[i].name) == 0) {
            return pos[i].at;
        }
    }
    return dflt;
}

/** @brief What a c:dLbls or c:dLbl shows (CH_LBL_*): @p dflt when it
 *         says nothing of it. */
static int
shows(const tiku_xml_node_t *dl, int dflt)
{
    if (tiku_xml_child(dl, "c:showVal") == NULL &&
        tiku_xml_child(dl, "c:showPercent") == NULL &&
        tiku_xml_child(dl, "c:showCatName") == NULL &&
        tiku_xml_child(dl, "c:showSerName") == NULL) {
        return dflt;
    }
    return (on(dl, "c:showVal") ? CH_LBL_VAL : 0) |
           (on(dl, "c:showPercent") ? CH_LBL_PCT : 0) |
           (on(dl, "c:showCatName") ? CH_LBL_CAT : 0) |
           (on(dl, "c:showSerName") ? CH_LBL_SER : 0);
}

/** @brief A point's own label's separator, number format and the move
 *         made to it by hand (a manual layout's offsets). */
static void
own_extras(ch_chart_t *k, const tiku_xml_node_t *e, ch_label_t *l)
{
    const tiku_xml_node_t *nf = tiku_xml_child(e, "c:numFmt");
    const tiku_xml_node_t *ml = tiku_xml_path(e, "c:layout/c:manualLayout");
    const char *linked = tiku_xml_attr(nf, "sourceLinked");
    char sep[16];

    if (tiku_xml_child(e, "c:separator") != NULL) {
        (void)tiku_xml_text(tiku_xml_child(e, "c:separator"), sep,
                            sizeof sep);
        l->sep = of_strdup(&k->mem, sep);
    }
    if (tiku_xml_attr(nf, "formatCode") != NULL &&
        (linked == NULL || strcmp(linked, "1") != 0)) {
        l->fmt = of_strdup(&k->mem, tiku_xml_attr(nf, "formatCode"));
    }
    if (ml != NULL && str_of(ml, "c:xMode") == NULL) {   /* offsets */
        l->dx = (float)fmin(fmax(val_of(ml, "c:x", 0), -1), 1);
        l->dy = (float)fmin(fmax(val_of(ml, "c:y", 0), -1), 1);
    }
}

/** @brief The points' own labels of a series' c:dLbls: taken away, put
 *         elsewhere, or in words of their own. */
static void
own_labels(ch_chart_t *k, const tiku_xml_node_t *dl, ch_series_t *s)
{
    const tiku_xml_node_t *e;
    int n = 0;

    for (e = tiku_xml_child(dl, "c:dLbl"); e; e = tiku_xml_next(e, "c:dLbl")) {
        n += n < CH_POINTS;
    }
    s->own = n > 0 ? of_alloc(&k->mem, (size_t)n * sizeof *s->own) : NULL;
    for (e = tiku_xml_child(dl, "c:dLbl"); e != NULL && s->own != NULL &&
         s->nown < n; e = tiku_xml_next(e, "c:dLbl")) {
        ch_label_t *l = &s->own[s->nown];

        if ((l->idx = index_of(str_of(e, "c:idx"), CH_POINTS)) < 0) {
            continue;
        }
        l->text = rich_text(k, tiku_xml_path(e, "c:tx/c:rich"));
        l->rich = tiku_xml_path(e, "c:tx/c:rich/a:p/a:fld") != NULL
                  ? tiku_xml_path(e, "c:tx/c:rich") : NULL;
        l->labels = on(e, "c:delete") ? 0 : shows(e, s->labels);
        l->labels = l->text != NULL && !on(e, "c:delete") && l->labels == 0
                    ? CH_LBL_VAL : l->labels;
        l->at = label_at(str_of(e, "c:dLblPos"), -1);
        l->sppr = tiku_xml_child(e, "c:spPr");
        own_extras(k, e, l);
        l->font = s->lbl_font;
        txpr_over(k, e, &l->font);
        rpr_over(k, tiku_xml_path(e, "c:tx/c:rich/a:p/a:r/a:rPr"), &l->font);
        s->nown++;
    }
}

/** @brief Which labels a c:dLbls asks for, where and how, over @p s's. */
static void
labels_read(ch_chart_t *k, const tiku_xml_node_t *dl, ch_series_t *s)
{
    const char *linked, *f;
    char sep[sizeof s->sep];

    if (dl == NULL) {
        return;
    }
    s->labels = on(dl, "c:delete") ? 0 : shows(dl, 0);
    s->lbl_at = label_at(str_of(dl, "c:dLblPos"), s->lbl_at);
    f = tiku_xml_attr(tiku_xml_child(dl, "c:numFmt"), "formatCode");
    linked = tiku_xml_attr(tiku_xml_child(dl, "c:numFmt"), "sourceLinked");
    if (f != NULL && (linked == NULL || strcmp(linked, "1") != 0)) {
        s->lbl_fmt = of_strdup(&k->mem, f);
    }
    if (tiku_xml_text(tiku_xml_child(dl, "c:separator"), sep, sizeof sep)) {
        snprintf(s->sep, sizeof s->sep, "%s", sep);
    }
    s->lbl_sppr = tiku_xml_child(dl, "c:spPr") != NULL
                  ? tiku_xml_child(dl, "c:spPr") : s->lbl_sppr;
    s->show_cells = is_on(ext_child(dl, "showDataLabelsRange")) ||
                    s->show_cells;
    txpr_over(k, dl, &s->lbl_font);
}

/** @brief A series' marker: its symbol, size, fill and outline. */
static void
marker_read(ch_chart_t *k, const tiku_xml_node_t *mk, ch_series_t *s)
{
    static const char *const sym[] = {"none", "circle", "square", "diamond",
                                      "triangle", "x", "plus", "dash"};
    const tiku_xml_node_t *sp = tiku_xml_child(mk, "c:spPr");
    const char *v = str_of(mk, "c:symbol");
    size_t i;

    if (v != NULL) {
        s->marker = CH_MK_CIRCLE;   /* a star, a dot, a picture: round */
        for (i = 0; i < sizeof sym / sizeof sym[0]; i++) {
            s->marker = strcmp(v, sym[i]) == 0 ? (int)i : s->marker;
        }
    }
    s->mk_size = (float)val_of(mk, "c:size", 5);
    s->mk_size = s->mk_size < 2 ? 2 : s->mk_size > 72 ? 72 : s->mk_size;
    s->has_mk_fill = dml_fill(k->c, sp, &s->mk_fill);
    s->mk_line.width = -1;
    s->has_mk_line = dml_line(k->c, tiku_xml_child(sp, "a:ln"), &s->mk_line);
}

/** @brief Room for each of series @p s's points' own looks.
 *  @return 0, or -1 out of memory. */
static int
points_room(ch_chart_t *k, ch_series_t *s)
{
    int i;

    s->pt_fill = of_alloc(&k->mem, (size_t)s->n * sizeof *s->pt_fill);
    s->pt_line = of_alloc(&k->mem, (size_t)s->n * sizeof *s->pt_line);
    s->pt_has = of_alloc(&k->mem, (size_t)s->n);
    s->pt_explode = of_alloc(&k->mem, (size_t)s->n * sizeof *s->pt_explode);
    if (s->pt_fill == NULL || s->pt_line == NULL || s->pt_has == NULL ||
        s->pt_explode == NULL) {
        s->pt_fill = NULL;
        return -1;
    }
    for (i = 0; i < s->n; i++) {
        s->pt_explode[i] = s->explode;
    }
    return 0;
}

/** @brief Each point's own fill, outline and pull, from the series'
 *         c:dPt; after the series' own, which they start from. */
static void
points_read(ch_chart_t *k, const tiku_xml_node_t *ser, ch_series_t *s)
{
    const tiku_xml_node_t *dp, *sp;
    int i;

    for (dp = tiku_xml_child(ser, "c:dPt"); dp != NULL;
         dp = tiku_xml_next(dp, "c:dPt")) {
        if ((i = index_of(str_of(dp, "c:idx"), s->n)) < 0) {
            continue;
        }
        if (s->pt_fill == NULL && points_room(k, s) != 0) {
            return;
        }
        sp = tiku_xml_child(dp, "c:spPr");
        s->pt_line[i] = s->line;
        s->pt_has[i] = (unsigned char)(dml_fill(k->c, sp, &s->pt_fill[i]) |
                                       dml_line(k->c, tiku_xml_child(sp,
                                                "a:ln"), &s->pt_line[i]) << 1);
        s->pt_explode[i] = (float)fmin(fmax(val_of(dp, "c:explosion",
                                                   s->explode), 0), 400);
    }
}

/** @brief A trendline's own name: its c:name's words. */
static const char *
trend_name(ch_chart_t *k, const tiku_xml_node_t *e)
{
    char buf[160];

    (void)tiku_xml_text(e, buf, sizeof buf);
    return buf[0] ? of_strdup(&k->mem, buf) : NULL;
}

/** @brief A trendline's label: whether it says the equation and R^2,
 *         its format, its look, where it was moved by hand. */
static void
trend_label(ch_chart_t *k, const tiku_xml_node_t *e, ch_trend_t *t)
{
    const tiku_xml_node_t *lb = tiku_xml_child(e, "c:trendlineLbl");
    const tiku_xml_node_t *ml = tiku_xml_path(lb, "c:layout/c:manualLayout");
    const char *f = tiku_xml_attr(tiku_xml_child(lb, "c:numFmt"),
                                  "formatCode");

    t->eq = on(e, "c:dispEq");
    t->r2 = on(e, "c:dispRSqr");
    t->lbl_fmt = f != NULL && strcmp(f, "General") != 0
                 ? of_strdup(&k->mem, f) : NULL;
    t->lbl_font = k->base;
    txpr_over(k, lb, &t->lbl_font);
    if (ml != NULL && str_of(ml, "c:xMode") == NULL) {
        t->lbl_dx = (float)fmin(fmax(val_of(ml, "c:x", 0), -1), 1);
        t->lbl_dy = (float)fmin(fmax(val_of(ml, "c:y", 0), -1), 1);
    }
}

/** @brief A series' error bars (two at most, as x and y): which way,
 *         which ends, how much, how drawn. */
static void
errbars_read(ch_chart_t *k, const tiku_xml_node_t *ser, ch_series_t *s)
{
    static const char *const kinds[] = {"fixedVal", "percentage", "stdDev",
                                        "stdErr", "cust"};
    const tiku_xml_node_t *e;
    const char *v;
    size_t i;

    for (e = tiku_xml_child(ser, "c:errBars"); e != NULL && s->nerr < 2;
         e = tiku_xml_next(e, "c:errBars")) {
        ch_err_t *b = &s->err[s->nerr++];

        v = str_of(e, "c:errDir");
        b->dir = v != NULL && v[0] == 'x' ? 'x' : 'y';
        v = str_of(e, "c:errBarType");
        b->type = v == NULL || strcmp(v, "both") == 0 ? CH_EB_BOTH :
                  strcmp(v, "plus") == 0 ? CH_EB_PLUS : CH_EB_MINUS;
        v = str_of(e, "c:errValType");
        for (i = 0; v != NULL && i < sizeof kinds / sizeof kinds[0]; i++) {
            b->kind = strcmp(v, kinds[i]) == 0 ? (int)i : b->kind;
        }
        b->val = fabs(val_of(e, "c:val", 1));
        b->plus = numbers(k, tiku_xml_child(e, "c:plus"), &b->nplus, NULL);
        b->minus = numbers(k, tiku_xml_child(e, "c:minus"), &b->nminus, NULL);
        b->cap = !on(e, "c:noEndCap");
        b->line.width = 0.75f;
        b->line.color = 0xff000000u;
        (void)dml_line(k->c, tiku_xml_path(e, "c:spPr/a:ln"), &b->line);
        ch_err_stats(s, b);
    }
}

/** @brief Whether a series' bars below nought are drawn inverted, and in
 *         what: white, or the fill its invertSolidFillFmt extension gives. */
static void
invert_read(ch_chart_t *k, const tiku_xml_node_t *ser, ch_series_t *s)
{
    const tiku_xml_node_t *x = ext_child(ser, "invertSolidFillFmt"), *sp;

    s->invert = on(ser, "c:invertIfNegative");
    s->invert_fill.kind = DML_SOLID;
    s->invert_fill.color = 0xffffffffu;
    for (sp = tiku_xml_child(x, NULL); sp != NULL;
         sp = tiku_xml_next(sp, NULL)) {
        (void)dml_fill(k->c, sp, &s->invert_fill);
    }
}

/** @brief A series' trendlines: their kind, reach, line and name. */
static void
trends_read(ch_chart_t *k, const tiku_xml_node_t *ser, ch_series_t *s)
{
    static const char *const kind[] = {"linear", "exp", "log", "poly",
                                       "power", "movingAvg"};
    const tiku_xml_node_t *e;
    int n = 0;
    size_t i;

    for (e = tiku_xml_child(ser, "c:trendline"); e && n < 4;
         e = tiku_xml_next(e, "c:trendline")) {
        n++;
    }
    s->trend = n > 0 ? of_alloc(&k->mem, (size_t)n * sizeof *s->trend)
                     : NULL;
    for (e = tiku_xml_child(ser, "c:trendline"); e && s->trend != NULL &&
         s->ntrend < n; e = tiku_xml_next(e, "c:trendline")) {
        ch_trend_t *t = &s->trend[s->ntrend++];
        const char *v = str_of(e, "c:trendlineType");

        for (i = 0; v != NULL && i < sizeof kind / sizeof kind[0]; i++) {
            t->kind = strcmp(v, kind[i]) == 0 ? (int)i : t->kind;
        }
        t->order = (int)fmin(fmax(val_of(e, "c:order", 2), 2), 6);
        t->period = (int)fmin(fmax(val_of(e, "c:period", 2), 2), CH_POINTS);
        t->fwd = fmax(val_of(e, "c:forward", 0), 0);
        t->back = fmax(val_of(e, "c:backward", 0), 0);
        t->has_icpt = tiku_xml_child(e, "c:intercept") != NULL;
        t->icpt = val_of(e, "c:intercept", 0);
        t->line.width = -1;
        t->has_line = dml_line(k->c, tiku_xml_path(e, "c:spPr/a:ln"),
                               &t->line);
        t->name = tiku_xml_child(e, "c:name") != NULL
                  ? trend_name(k, tiku_xml_child(e, "c:name")) : NULL;
        trend_label(k, e, t);
    }
}

/** @brief The words a series' labels take from cells ("value from
 *         cells"): the cache of its datalabelsRange extension. */
static void
cells_read(ch_chart_t *k, const tiku_xml_node_t *ser, ch_series_t *s)
{
    const tiku_xml_node_t *r = ext_child(ser, "datalabelsRange"), *c;

    for (c = tiku_xml_child(r, NULL); c != NULL; c = tiku_xml_next(c, NULL)) {
        const char *colon = strchr(c->name, ':');

        if (strcmp(colon != NULL ? colon + 1 : c->name,
                   "dlblRangeCache") == 0) {
            s->cells = cache_words(k, c, c, &s->ncells);
        }
    }
}

/** @brief A series' name: its cached words, or the text it gives. */
static const char *
series_name(ch_chart_t *k, const tiku_xml_node_t *tx)
{
    char buf[160];
    const char **w;
    int n = 0;

    if ((w = words(k, tx, &n)) != NULL && n > 0 && w[0][0]) {
        return w[0];
    }
    (void)tiku_xml_text(tiku_xml_child(tx, "c:v"), buf, sizeof buf);
    return buf[0] ? of_strdup(&k->mem, buf) : NULL;
}

/** @brief One c:ser read into @p s, the @p order'th of the chart: its
 *         name, values, categories and looks; labels as the group's
 *         @p dl says unless it says. */
static int
series_read(ch_chart_t *k, const tiku_xml_node_t *ser, const ch_group_t *g,
            const tiku_xml_node_t *dl, ch_series_t *s, int order)
{
    const tiku_xml_node_t *sp = tiku_xml_child(ser, "c:spPr");
    int xy = g->kind == CH_SCATTER, n = 0;

    s->idx = (int)fmin(fmax(val_of(ser, "c:idx", order), 0), CH_POINTS);
    s->marker = -1;
    s->mk_size = 5;
    s->lbl_at = -1;
    s->lbl_font = k->base;
    snprintf(s->sep, sizeof s->sep, ", ");
    s->v = numbers(k, tiku_xml_child(ser, xy ? "c:yVal" : "c:val"), &s->n,
                   s->fmt);
    if (xy) {
        s->x = numbers(k, tiku_xml_child(ser, "c:xVal"), &n, NULL);
        s->x = n >= s->n ? s->x : NULL;     /* words for x: counted */
        s->size = numbers(k, tiku_xml_child(ser, "c:bubbleSize"), &n, NULL);
        s->size = n >= s->n ? s->size : NULL;
    } else {
        categories(k, tiku_xml_child(ser, "c:cat"), s);
    }
    s->name = series_name(k, tiku_xml_child(ser, "c:tx"));
    s->has_fill = dml_fill(k->c, sp, &s->fill);
    s->line.width = -1;             /* stated without a width: the usual */
    s->has_line = dml_line(k->c, tiku_xml_child(sp, "a:ln"), &s->line);
    s->smooth = on(ser, "c:smooth") || (xy && g->smooth &&
                                        !tiku_xml_child(ser, "c:smooth"));
    marker_read(k, tiku_xml_child(ser, "c:marker"), s);
    s->explode = (float)val_of(ser, "c:explosion", 0);
    s->explode = s->explode < 0 ? 0 : s->explode > 400 ? 400 : s->explode;
    points_read(k, ser, s);
    trends_read(k, ser, s);
    cells_read(k, ser, s);
    errbars_read(k, ser, s);
    invert_read(k, ser, s);
    labels_read(k, dl, s);
    labels_read(k, tiku_xml_child(ser, "c:dLbls"), s);
    own_labels(k, tiku_xml_child(ser, "c:dLbls"), s);
    return s->v != NULL ? 0 : -1;
}

/* ---------------------------------------------------------------- groups  */

/** @brief The kind of chart group element @p e is, or -1 for one not
 *         drawn here. */
static int
kind_of(const tiku_xml_node_t *e)
{
    static const struct { const char *name; int kind; } k[] = {
        {"c:barChart", CH_BAR}, {"c:bar3DChart", CH_BAR},
        {"c:lineChart", CH_LINE}, {"c:line3DChart", CH_LINE},
        {"c:stockChart", CH_LINE}, {"c:areaChart", CH_AREA},
        {"c:area3DChart", CH_AREA}, {"c:pieChart", CH_PIE},
        {"c:pie3DChart", CH_PIE}, {"c:ofPieChart", CH_PIE},
        {"c:doughnutChart", CH_DOUGHNUT}, {"c:scatterChart", CH_SCATTER},
        {"c:bubbleChart", CH_SCATTER}, {"c:radarChart", CH_RADAR}};
    size_t i;

    for (i = 0; i < sizeof k / sizeof k[0]; i++) {
        if (tiku_xml_is(e, k[i].name)) {
            return k[i].kind;
        }
    }
    return -1;
}

/** @brief The index of the chart's axis with id @p id, or -1. */
static int
axis_at(const ch_chart_t *k, const char *id)
{
    int i;

    for (i = 0; id != NULL && i < k->nax; i++) {
        if (k->ax[i].id == strtoul(id, NULL, 10)) {
            return i;
        }
    }
    return -1;
}

/** @brief A group's shape: bars' direction, grouping, gaps, a pie's
 *         first slice, a doughnut's hole, a scatter's lines. */
static void
group_shape(const tiku_xml_node_t *e, ch_group_t *g)
{
    const char *dir = str_of(e, "c:barDir"), *gr = str_of(e, "c:grouping");
    const char *st = str_of(e, "c:scatterStyle");

    st = st != NULL ? st : str_of(e, "c:radarStyle");

    g->across = dir != NULL && strcmp(dir, "bar") == 0;
    g->grouping = gr == NULL ? (g->kind == CH_BAR ? CH_CLUSTERED
                                                  : CH_STANDARD) :
                  strcmp(gr, "stacked") == 0 ? CH_STACKED :
                  strcmp(gr, "percentStacked") == 0 ? CH_PERCENT :
                  strcmp(gr, "clustered") == 0 ? CH_CLUSTERED : CH_STANDARD;
    g->vary = on(e, "c:varyColors") || (g->kind >= CH_PIE &&
                                        g->kind <= CH_DOUGHNUT &&
                                        !tiku_xml_child(e, "c:varyColors"));
    g->gap = (float)val_of(e, "c:gapWidth", 150);
    g->gap = g->gap < 0 ? 0 : g->gap > 500 ? 500 : g->gap;
    g->overlap = (float)val_of(e, "c:overlap", g->grouping == CH_STACKED ||
                               g->grouping == CH_PERCENT ? 100 : 0);
    g->overlap = g->overlap < -100 ? -100 : g->overlap > 100 ? 100
                                                             : g->overlap;
    g->hole = (float)val_of(e, "c:holeSize", 50);
    g->hole = g->hole < 1 ? 1 : g->hole > 90 ? 90 : g->hole;
    g->first = (float)fmod(val_of(e, "c:firstSliceAng", 0), 360);
    g->markers = tiku_xml_child(e, "c:marker") == NULL || on(e, "c:marker");
    g->lines = st == NULL || strstr(st, "ine") != NULL ||
               strncmp(st, "smooth", 6) == 0;
    g->smooth = st != NULL && strncmp(st, "smooth", 6) == 0;
    g->filled = st != NULL && strcmp(st, "filled") == 0;
    g->markers = g->markers && (st == NULL || strstr(st, "arker") != NULL);
    g->bubble_scale = (float)val_of(e, "c:bubbleScale", 100);
    g->bubble_scale = g->bubble_scale < 0 ? 0 : g->bubble_scale > 300
                      ? 300 : g->bubble_scale;
}

/** @brief A pie of pie or bar of pie: which points its second plot
 *         takes, and how big it is. */
static void
of_pie_read(ch_chart_t *k, const tiku_xml_node_t *e, ch_group_t *g)
{
    const char *type = str_of(e, "c:ofPieType"), *v = str_of(e, "c:splitType");
    const tiku_xml_node_t *p;
    int i;

    if (!tiku_xml_is(e, "c:ofPieChart")) {
        return;
    }
    g->of_pie = type != NULL && strcmp(type, "bar") == 0 ? 'b' : 'p';
    g->split = v == NULL || strcmp(v, "auto") == 0 || strcmp(v, "pos") == 0
               ? 0 : strcmp(v, "val") == 0 ? 1 : strcmp(v, "percent") == 0
               ? 2 : 3;
    g->split_at = val_of(e, "c:splitPos", -1);
    g->second_size = (float)fmin(fmax(val_of(e, "c:secondPieSize", 75), 5),
                                 200);
    g->nsecond = CH_POINTS;
    g->second = g->split == 3 ? of_alloc(&k->mem, CH_POINTS) : NULL;
    for (p = tiku_xml_path(e, "c:custSplit/c:secondPiePt"); p != NULL &&
         g->second != NULL; p = tiku_xml_next(p, "c:secondPiePt")) {
        if ((i = index_of(tiku_xml_attr(p, "val"), CH_POINTS)) >= 0) {
            g->second[i] = 1;
        }
    }
}

/** @brief A group's drop lines (from each point down to its axis) and
 *         series lines (joining stacked bars' tops). */
static void
group_lines(ch_chart_t *k, const tiku_xml_node_t *e, ch_group_t *g)
{
    const tiku_xml_node_t *dl = tiku_xml_child(e, "c:dropLines");
    const tiku_xml_node_t *sl = tiku_xml_child(e, "c:serLines");

    g->drop = dl != NULL;
    g->drop_line.width = 0.75f;
    g->drop_line.color = 0xff000000u;
    (void)dml_line(k->c, tiku_xml_path(dl, "c:spPr/a:ln"), &g->drop_line);
    g->serlines = sl != NULL;
    g->ser_line.width = 0.75f;
    g->ser_line.color = 0xff000000u;
    (void)dml_line(k->c, tiku_xml_path(sl, "c:spPr/a:ln"), &g->ser_line);
}

/** @brief A line group's high-low lines and up-down bars. */
static void
group_bars(ch_chart_t *k, const tiku_xml_node_t *e, ch_group_t *g)
{
    const tiku_xml_node_t *hl = tiku_xml_child(e, "c:hiLowLines");
    const tiku_xml_node_t *ud = tiku_xml_child(e, "c:upDownBars");

    g->hilo = hl != NULL;
    g->hilo_line.width = 0.75f;
    g->hilo_line.color = 0xff000000u;
    (void)dml_line(k->c, tiku_xml_path(hl, "c:spPr/a:ln"), &g->hilo_line);
    g->updown = ud != NULL;
    g->updown_gap = (float)val_of(ud, "c:gapWidth", 150);
    g->updown_gap = g->updown_gap < 0 ? 0 : g->updown_gap > 500
                    ? 500 : g->updown_gap;
    if (!dml_fill(k->c, tiku_xml_path(ud, "c:upBars/c:spPr"), &g->up)) {
        g->up.kind = DML_SOLID;
        g->up.color = 0xffffffffu;
    }
    if (!dml_fill(k->c, tiku_xml_path(ud, "c:downBars/c:spPr"), &g->down)) {
        g->down.kind = DML_SOLID;
        g->down.color = 0xff000000u;
    }
    g->updown_line.width = 0.75f;
    g->updown_line.color = 0xff000000u;
    (void)dml_line(k->c, tiku_xml_path(ud, "c:upBars/c:spPr/a:ln"),
                   &g->updown_line);
}

/** @brief One chart group read into @p g, after @p before series: its
 *         shape, its series, and the axes it names.
 *  @return 0, or -1 out of memory. */
static int
group_read(ch_chart_t *k, const tiku_xml_node_t *e, ch_group_t *g,
           int before)
{
    const tiku_xml_node_t *s, *id = tiku_xml_child(e, "c:axId");
    int n = 0;

    g->kind = kind_of(e);
    group_shape(e, g);
    group_bars(k, e, g);
    group_lines(k, e, g);
    of_pie_read(k, e, g);
    for (s = tiku_xml_child(e, "c:ser"); s; s = tiku_xml_next(s, "c:ser")) {
        n++;
    }
    n = n > CH_SERIES ? CH_SERIES : n;
    g->ser = n > 0 ? of_alloc(&k->mem, (size_t)n * sizeof *g->ser) : NULL;
    for (s = tiku_xml_child(e, "c:ser"); s != NULL && g->ser != NULL &&
         g->nser < n; s = tiku_xml_next(s, "c:ser")) {
        if (series_read(k, s, g, tiku_xml_child(e, "c:dLbls"),
                        &g->ser[g->nser], before + g->nser) == 0) {
            g->nser++;
        }
    }
    g->cat_ax = axis_at(k, tiku_xml_attr(id, "val"));
    g->val_ax = axis_at(k, tiku_xml_attr(tiku_xml_next(id, "c:axId"),
                                         "val"));
    if (g->cat_ax == g->val_ax) {
        g->cat_ax = g->val_ax = -1;     /* one axis twice: none */
    }
    return n > 0 && g->ser == NULL ? -1 : 0;
}

/* ------------------------------------------------------------------ axes  */

/** @brief A value axis' display units: its labels in thousands,
 *         millions, or its own, and the name said beside it. */
static void
axis_units(ch_chart_t *k, const tiku_xml_node_t *e, ch_axis_t *a)
{
    static const struct { const char *name, *says; double v; } u[] = {
        {"hundreds", "Hundreds", 1e2}, {"thousands", "Thousands", 1e3},
        {"tenThousands", "10000", 1e4},
        {"hundredThousands", "100000", 1e5},
        {"millions", "Millions", 1e6}, {"tenMillions", "10000000", 1e7},
        {"hundredMillions", "100000000", 1e8},
        {"billions", "Billions", 1e9}, {"trillions", "Trillions", 1e12}};
    const tiku_xml_node_t *du = tiku_xml_child(e, "c:dispUnits");
    const tiku_xml_node_t *lbl = tiku_xml_child(du, "c:dispUnitsLbl");
    const char *v = str_of(du, "c:builtInUnit");
    size_t i;

    a->units = val_of(du, "c:custUnit", 0);
    for (i = 0; v != NULL && i < sizeof u / sizeof u[0]; i++) {
        if (strcmp(v, u[i].name) == 0) {
            a->units = u[i].v;
            a->units_name = u[i].says;
        }
    }
    a->units = a->units > 0 ? a->units : 0;
    a->units_name = lbl == NULL || a->units == 0 ? NULL
                    : rich_text(k, tiku_xml_path(lbl, "c:tx/c:rich")) != NULL
                    ? rich_text(k, tiku_xml_path(lbl, "c:tx/c:rich"))
                    : a->units_name;
}

/** @brief An axis' line and gridlines: its own, else the usual ones. */
static void
axis_lines(ch_chart_t *k, const tiku_xml_node_t *e, ch_axis_t *a)
{
    const tiku_xml_node_t *gl = tiku_xml_child(e, "c:majorGridlines");
    const tiku_xml_node_t *ml = tiku_xml_child(e, "c:minorGridlines");

    a->line.width = 0.75f;
    a->line.color = 0xff868686u;
    (void)dml_line(k->c, tiku_xml_path(e, "c:spPr/a:ln"), &a->line);
    a->has_line = a->line.width > 0;
    a->grid = gl != NULL;
    a->grid_line.width = 0.75f;
    a->grid_line.color = 0xffd9d9d9u;
    (void)dml_line(k->c, tiku_xml_path(gl, "c:spPr/a:ln"), &a->grid_line);
    a->minor = ml != NULL;
    a->minor_line.width = 0.75f;
    a->minor_line.color = 0xfff2f2f2u;
    (void)dml_line(k->c, tiku_xml_path(ml, "c:spPr/a:ln"), &a->minor_line);
    a->minor_unit = fmax(val_of(e, "c:minorUnit", 0), 0);
}

/** @brief Where an axis crosses its partner, and how it counts. */
static void
axis_cross(const tiku_xml_node_t *e, ch_axis_t *a)
{
    const char *v = str_of(e, "c:crosses");

    a->crosses = v == NULL || strcmp(v, "autoZero") == 0 ? 0 :
                 strcmp(v, "min") == 0 ? 1 : strcmp(v, "max") == 0 ? 2 : 0;
    if (tiku_xml_child(e, "c:crossesAt") != NULL) {
        a->crosses = 3;
        a->cross_at = val_of(e, "c:crossesAt", 0);
    }
    v = str_of(e, "c:crossBetween");
    a->between = v == NULL || strcmp(v, "midCat") != 0;
    a->log = val_of(tiku_xml_child(e, "c:scaling"), "c:logBase", 0);
    a->log = a->log >= 2 && a->log <= 1000 ? a->log : 0;
    a->lbl_skip = (int)fmin(fmax(val_of(e, "c:tickLblSkip", 0), 0),
                            CH_POINTS);
}

/** @brief One axis element read into @p a. */
static void
axis_read(ch_chart_t *k, const tiku_xml_node_t *e, ch_axis_t *a)
{
    const tiku_xml_node_t *sc = tiku_xml_child(e, "c:scaling");
    const tiku_xml_node_t *nf = tiku_xml_child(e, "c:numFmt");
    const char *v = str_of(e, "c:axPos"), *tick, *lbl, *f;

    a->id = strtoul(str_of(e, "c:axId") != NULL ? str_of(e, "c:axId")
                                                : "0", NULL, 10);
    a->kind = tiku_xml_is(e, "c:valAx") ? CH_VAL_AX : CH_CAT_AX;
    a->date = tiku_xml_is(e, "c:dateAx");
    a->flat = on(e, "c:noMultiLvlLbl");
    a->pos = v != NULL && v[0] != '\0' && v[1] == '\0' &&
              strchr("bltr", v[0]) != NULL ? v[0] : 'b';
    v = str_of(e, "c:baseTimeUnit");
    a->time_unit = v == NULL ? 0 : strcmp(v, "months") == 0 ? 1 :
                   strcmp(v, "years") == 0 ? 2 : 0;
    a->deleted = on(e, "c:delete");
    a->reversed = (v = str_of(sc, "c:orientation")) != NULL &&
                  strcmp(v, "maxMin") == 0;
    a->has_min = tiku_xml_child(sc, "c:min") != NULL;
    a->has_max = tiku_xml_child(sc, "c:max") != NULL;
    a->min = val_of(sc, "c:min", 0);
    a->max = val_of(sc, "c:max", 0);
    a->unit = val_of(e, "c:majorUnit", 0);
    a->has_unit = a->unit > 0;
    axis_lines(k, e, a);
    axis_cross(e, a);
    tick = str_of(e, "c:majorTickMark");
    a->tick = tick == NULL || strcmp(tick, "out") == 0 ? 1 :
              strcmp(tick, "in") == 0 ? 2 : strcmp(tick, "cross") == 0 ? 3
                                                                      : 0;
    lbl = str_of(e, "c:tickLblPos");
    a->labels = lbl == NULL || strcmp(lbl, "nextTo") == 0 ? 1 :
                strcmp(lbl, "low") == 0 ? 2 : strcmp(lbl, "high") == 0 ? 3
                                                                      : 0;
    f = tiku_xml_attr(nf, "formatCode");
    snprintf(a->fmt, sizeof a->fmt, "%s", f != NULL ? f : "General");
    v = tiku_xml_attr(nf, "sourceLinked");
    a->linked = f == NULL || (v != NULL && strcmp(v, "1") == 0);
    a->font = k->base;
    txpr_over(k, e, &a->font);
    v = tiku_xml_attr(tiku_xml_path(e, "c:txPr/a:bodyPr"), "rot");
    a->turn_set = v != NULL && atol(v) != -60000000 &&
                  labs(atol(v)) <= 5400000;
    a->turn_file = a->turn_set ? (float)(atol(v) / 60000.0) : 0;
    axis_units(k, e, a);
    a->title_font = a->font;
    a->title_font.bold = 1;
    if (tiku_xml_child(e, "c:title") != NULL) {
        a->title = title_read(k, tiku_xml_child(e, "c:title"),
                              &a->title_font);
        a->title = a->title != NULL ? a->title : "Axis Title";
    }
}

/* ----------------------------------------------------------------- chart  */

/** @brief The chart's title: its own words, else its one series' name,
 *         which a chart of one series shows unless the file took the
 *         automatic title away. */
static void
chart_title(ch_chart_t *k, const tiku_xml_node_t *ch)
{
    const tiku_xml_node_t *t = tiku_xml_child(ch, "c:title");
    int one = k->ngrp == 1 && k->grp[0].nser == 1;

    k->title_font = k->base;
    k->title_font.size = k->base.size * 1.2f;
    if (t == NULL && (!one || on(ch, "c:autoTitleDeleted"))) {
        return;
    }
    k->title = title_read(k, t, &k->title_font);
    k->title = k->title == NULL && one ? k->grp[0].ser[0].name : k->title;
    k->title = k->title != NULL || t == NULL ? k->title : "Chart Title";
    k->has_title_at = layout_read(t, k->title_at, 2, NULL);
}

/** @brief The legend: whether shown, where, in what look, and the
 *         entries taken out of it. */
static void
chart_legend(ch_chart_t *k, const tiku_xml_node_t *ch)
{
    const tiku_xml_node_t *lg = tiku_xml_child(ch, "c:legend"), *le;
    const char *pos = str_of(lg, "c:legendPos");
    int n = 0;

    k->legend = lg != NULL;
    k->legend_pos = pos == NULL || strcmp(pos, "tr") == 0 ||
                    !strchr("bltr", pos[0]) ? 'r' : pos[0];
    k->legend_font = k->base;
    txpr_over(k, lg, &k->legend_font);
    k->legend_over = on(lg, "c:overlay");
    k->has_legend_at = layout_read(lg, k->legend_at, 4, NULL);
    for (le = tiku_xml_child(lg, "c:legendEntry"); le != NULL;
         le = tiku_xml_next(le, "c:legendEntry")) {
        n += on(le, "c:delete");
    }
    k->leg_del = n > 0 ? of_alloc(&k->mem, (size_t)n * sizeof(int)) : NULL;
    for (le = tiku_xml_child(lg, "c:legendEntry"); le != NULL &&
         k->leg_del != NULL; le = tiku_xml_next(le, "c:legendEntry")) {
        if (on(le, "c:delete")) {
            k->leg_del[k->nleg_del++] = index_of(str_of(le, "c:idx"),
                                                 CH_POINTS);
        }
    }
}

/** @brief The plot area's data table, if it has one: which rules it
 *         draws, whether its series show their keys, its look. */
static void
table_read(ch_chart_t *k, const tiku_xml_node_t *dt)
{
    k->dtable = dt != NULL;
    k->dt_horz = on(dt, "c:showHorzBorder");
    k->dt_vert = on(dt, "c:showVertBorder");
    k->dt_outline = on(dt, "c:showOutline");
    k->dt_keys = on(dt, "c:showKeys");
    k->dt_line.width = 0.75f;
    k->dt_line.color = 0xffbfbfbfu;
    (void)dml_line(k->c, tiku_xml_path(dt, "c:spPr/a:ln"), &k->dt_line);
    k->dt_font = k->base;
    txpr_over(k, dt, &k->dt_font);
}

/** @brief The plot area's axes, then its groups: the axes first, as the
 *         groups name them.  @return 0, or -1 out of memory. */
static int
plot_read(ch_chart_t *k, const tiku_xml_node_t *pa)
{
    const tiku_xml_node_t *e;
    int n = 0, before = 0;

    for (e = tiku_xml_child(pa, NULL); e != NULL; e = tiku_xml_next(e,
         NULL)) {
        if ((tiku_xml_is(e, "c:catAx") || tiku_xml_is(e, "c:valAx") ||
             tiku_xml_is(e, "c:dateAx")) && k->nax < CH_AXES) {
            axis_read(k, e, &k->ax[k->nax++]);
        }
        n += kind_of(e) >= 0;
        k->known += kind_of(e) >= 0;
    }
    if (n > 0 && (k->grp = of_alloc(&k->mem, (size_t)n *
                                    sizeof *k->grp)) == NULL) {
        return -1;
    }
    for (e = tiku_xml_child(pa, NULL); e != NULL && n > 0;
         e = tiku_xml_next(e, NULL)) {
        if (kind_of(e) < 0) {
            continue;
        }
        if (group_read(k, e, &k->grp[k->ngrp], before) != 0) {
            return -1;
        }
        before += k->grp[k->ngrp].nser;
        k->ngrp += k->grp[k->ngrp].nser > 0;
    }
    table_read(k, tiku_xml_child(pa, "c:dTable"));
    k->has_plot_at = layout_read(pa, k->plot_at, 4, &k->plot_inner);
    k->has_plot_bg = dml_fill(k->c, tiku_xml_child(pa, "c:spPr"),
                              &k->plot_bg);
    k->plot_border.width = 0.75f;
    k->has_plot_border = edge_read(k, tiku_xml_path(pa, "c:spPr/a:ln"),
                                   &k->plot_border);
    return 0;
}

/** @brief The categories the chart shows: the first series' that has
 *         them, as many as its longest series has points. */
static void
chart_categories(ch_chart_t *k)
{
    int g, s, most = 0;

    for (g = 0; g < k->ngrp; g++) {
        for (s = 0; s < k->grp[g].nser; s++) {
            const ch_series_t *r = &k->grp[g].ser[s];

            most = r->n > most ? r->n : most;
            if (k->cat == NULL && r->cat != NULL) {
                k->cat = r->cat;
                k->nnamed = r->n;
                k->outer[0] = r->outer[0];
                k->outer[1] = r->outer[1];
                k->nouter = r->nouter;
                k->catv = r->catv;
                snprintf(k->cat_fmt, sizeof k->cat_fmt, "%s", r->cat_fmt);
            }
        }
    }
    k->ncat = most;
}

int
ch_read_dml(ch_chart_t *k, const tiku_xml_node_t *cs)
{
    const tiku_xml_node_t *ch = tiku_xml_child(cs, "c:chart");
    const tiku_xml_node_t *sp = tiku_xml_child(cs, "c:spPr");
    const char *b = str_of(ch, "c:dispBlanksAs");

    k->base.size = k->base.size > 0 ? k->base.size : 10;
    k->base.color = 0xff595959u;
    txpr_over(k, cs, &k->base);
    k->has_bg = dml_fill(k->c, sp, &k->bg);
    k->border.width = 0.75f;
    k->border.color = 0xff000000u;
    k->has_border = edge_read(k, tiku_xml_child(sp, "a:ln"), &k->border);
    k->blanks = b == NULL || strcmp(b, "gap") == 0 ? 0 :
                strcmp(b, "zero") == 0 ? 1 : 2;
    if (plot_read(k, tiku_xml_child(ch, "c:plotArea")) != 0) {
        return -1;
    }
    chart_categories(k);
    chart_title(k, ch);
    chart_legend(k, ch);
    return k->mem.bad ? -1 : k->ngrp;
}
