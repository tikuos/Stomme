/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_office_chart_plot.c - a chart's series drawn: bars standing or
 * lying, clustered or stacked, from where their axis crosses; lines
 * through their points, straight or smoothed, with their markers; areas
 * down to their base or the series under them; pies and doughnuts from
 * their first slice's angle; scatters and bubbles.  Each point's label
 * is noted as it is drawn and set once all are, over everything.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_office_chart.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define PL_LINE_W  2.25f            /* a line series' width, unsaid      */
#define PL_MK_W    0.75f            /* a marker's outline                */
#define PL_GAP     3.0f             /* between a point and its label     */
#define PL_LEAD    8.0f             /* a pie's moved label, past its edge */
#define PL_SPREAD  256              /* outside labels one pie spreads    */

/** @brief A label noted while its point was drawn. */
typedef struct {
    const ch_series_t *s;           /* whose                             */
    int      i;                     /* which point                       */
    float    x, y;                  /* where                             */
    int      align, valign;         /* as ch_text takes them             */
    double   pct;                   /* a slice's share, or NAN           */
    float    ex, ey;                /* the slice's edge it is set outside
                                       of, to be led from; NAN for none  */
    int      lead;                  /* a line led to it already          */
} pl_label_t;

/** @brief The series being drawn: the group, its axes, its stacks. */
typedef struct {
    ch_chart_t       *k;
    const ch_group_t *g;
    const ch_axis_t  *cat, *val;    /* category (or x) axis, value axis  */
    double            base;         /* where bars and areas grow from    */
    double           *up, *down;    /* each category's stacks so far     */
    double           *total;        /* and its whole, stacked to 100 %   */
    float            *pt;           /* a series' points: x, y, drawn     */
    int              *run;          /* one unbroken run of them          */
    pl_label_t       *lbl;          /* labels noted                      */
    int               nlbl, cap;
} pl_t;

/** @brief Points of a pie's slice, or a doughnut's. */
typedef struct {
    float  cx, cy;                  /* the centre                        */
    float  r0, r1;                  /* inner and outer radius            */
    double a0, a1;                  /* from, to: clockwise from twelve   */
} pl_arc_t;

/* ----------------------------------------------------------------- looks  */

void
ch_point_fill(ch_chart_t *k, const ch_group_t *g, const ch_series_t *s,
              int i, dml_fill_t *f)
{
    int each = g->vary && i >= 0 && (g->nser == 1 || g->kind == CH_PIE ||
                                     g->kind == CH_DOUGHNUT);

    if (s->pt_fill != NULL && i >= 0 && i < s->n && (s->pt_has[i] & 1)) {
        *f = s->pt_fill[i];
    } else if (s->has_fill) {
        *f = s->fill;
    } else {
        memset(f, 0, sizeof *f);
        f->kind = DML_SOLID;
        f->color = ch_palette(k, each ? i : s->idx);
    }
}

void
ch_series_line(ch_chart_t *k, const ch_group_t *g, const ch_series_t *s,
               dml_line_t *l)
{
    int traced = g->kind == CH_LINE || (g->kind == CH_RADAR && !g->filled) ||
                 (g->kind == CH_SCATTER && s->size == NULL);

    memset(l, 0, sizeof *l);
    if (s->has_line) {
        *l = s->line;
    }
    if (traced) {
        l->width = !s->has_line ? (g->kind == CH_SCATTER && !g->lines ? 0
                                                                 : PL_LINE_W)
                 : l->width < 0 ? PL_LINE_W : l->width;
        l->join = s->has_line ? l->join : 1;
        l->cap = s->has_line ? l->cap : 1;
    } else {
        l->width = !s->has_line ? 0 : l->width < 0 ? 0.75f : l->width;
    }
    if (l->color == 0) {
        l->color = traced ? ch_palette(k, s->idx) : 0xff000000u;
    }
}

int
ch_marker_of(const ch_group_t *g, const ch_series_t *s)
{
    static const int turn[] = {CH_MK_DIAMOND, CH_MK_SQUARE, CH_MK_TRIANGLE,
                               CH_MK_X, CH_MK_CIRCLE, CH_MK_PLUS};

    if ((g->kind != CH_LINE && g->kind != CH_SCATTER &&
         g->kind != CH_RADAR) || s->size != NULL) {
        return CH_MK_NONE;
    }
    if (s->marker >= 0) {
        return s->marker;
    }
    return g->markers ? turn[s->idx % 6] : CH_MK_NONE;
}

void
ch_marker_look(ch_chart_t *k, const ch_group_t *g, const ch_series_t *s,
               dml_fill_t *f, dml_line_t *l)
{
    dml_line_t own;
    uint32_t c;

    ch_series_line(k, g, s, &own);
    c = own.width > 0 ? own.color : ch_palette(k, s->idx);
    if (s->has_mk_fill) {
        *f = s->mk_fill;
    } else {
        memset(f, 0, sizeof *f);
        f->kind = DML_SOLID;
        f->color = c;
    }
    memset(l, 0, sizeof *l);
    l->width = PL_MK_W;
    l->color = c;
    if (s->has_mk_line) {
        *l = s->mk_line;
        l->width = l->width < 0 ? PL_MK_W : l->width;
        l->color = l->color != 0 ? l->color : c;
    }
}

void
ch_marker(ch_chart_t *k, int kind, float size, float x, float y,
          const dml_fill_t *f, const dml_line_t *l)
{
    float r = size / 2;
    ch_path_t p;

    ch_path(&p, x - r, y - r, kind != CH_MK_X && kind != CH_MK_PLUS, 1);
    if (kind == CH_MK_SQUARE || kind == CH_MK_DASH) {
        float h = kind == CH_MK_DASH ? r / 3 : r;

        ch_move(&p, x - r, y - h);
        ch_line(&p, x + r, y - h);
        ch_line(&p, x + r, y + h);
        ch_line(&p, x - r, y + h);
    } else if (kind == CH_MK_DIAMOND || kind == CH_MK_TRIANGLE) {
        ch_move(&p, x, y - r);
        ch_line(&p, x + r, kind == CH_MK_DIAMOND ? y : y + r);
        ch_line(&p, kind == CH_MK_DIAMOND ? x : x - r, y + r);
        ch_line(&p, x - r, kind == CH_MK_DIAMOND ? y : y + r);
    } else if (kind == CH_MK_X || kind == CH_MK_PLUS) {
        float d = kind == CH_MK_X ? r : 0, e = kind == CH_MK_X ? r : 0;

        ch_move(&p, x - d, y - r);
        ch_line(&p, x + d, y + r);
        ch_move(&p, x - r, y + e);
        ch_line(&p, x + r, y - e);
    } else if (kind != CH_MK_NONE) {
        ch_move(&p, x, y - r);
        ch_arc(&p, x, y, r, 0, 2 * M_PI);
    }
    if (kind != CH_MK_X && kind != CH_MK_PLUS) {
        ch_close(&p);
    }
    ch_paint(k, &p, kind == CH_MK_X || kind == CH_MK_PLUS ? NULL : f, l,
             size, size);
}

/** @brief The outline point @p i of series @p s is drawn with: its own,
 *         else its series'. */
static void
point_line(pl_t *q, const ch_series_t *s, int i, dml_line_t *l)
{
    ch_series_line(q->k, q->g, s, l);
    if (s->pt_fill != NULL && i >= 0 && i < s->n && (s->pt_has[i] & 2)) {
        *l = s->pt_line[i];
        l->width = l->width < 0 ? 0.75f : l->width;
        l->color = l->color != 0 ? l->color : 0xff000000u;
    }
}

/* ---------------------------------------------------------------- labels  */

const ch_label_t *
ch_own_label(const ch_series_t *s, int i)
{
    int k;

    for (k = 0; k < s->nown; k++) {
        if (s->own[k].idx == i) {
            return &s->own[k];
        }
    }
    return NULL;
}

int
ch_labels_of(const ch_series_t *s, int i)
{
    const ch_label_t *l = ch_own_label(s, i);

    return l != NULL ? l->labels : s->labels;
}

/** @brief Where point @p i of @p s puts its label: CH_AT_*, or -1 where
 *         its kind puts it. */
static int
at_of(const ch_series_t *s, int i)
{
    const ch_label_t *l = ch_own_label(s, i);

    return l != NULL && l->at >= 0 ? l->at : s->lbl_at;
}

/** @brief The look point @p i of @p s's label is set in. */
static const ch_font_t *
font_of(const ch_series_t *s, int i)
{
    const ch_label_t *l = ch_own_label(s, i);

    return l != NULL ? &l->font : &s->lbl_font;
}

/* ---------------------------------------------------------------- points  */

/** @brief Point @p i of series @p s: NAN where it has none, unless the
 *         chart counts missing points as zero. */
static double
value(const pl_t *q, const ch_series_t *s, int i)
{
    double v = i < s->n ? s->v[i] : NAN;

    return isnan(v) && q->k->blanks == 1 ? 0 : v;
}

/** @brief A label for point @p i of @p s noted at @p x, @p y.
 *  @return it, or NULL when there is no room for more. */
static pl_label_t *
note(pl_t *q, const ch_series_t *s, int i, float x, float y, int align,
     int valign, double pct)
{
    const ch_label_t *own = ch_own_label(s, i);
    pl_label_t *l;

    if (q->nlbl >= q->cap) {
        return NULL;
    }
    l = &q->lbl[q->nlbl++];
    l->s = s;
    l->i = i;
    l->x = x + (own != NULL ? own->dx * q->k->w : 0);
    l->y = y + (own != NULL ? own->dy * q->k->h : 0);
    l->align = align;
    l->valign = valign;
    l->pct = pct;
    l->ex = l->ey = NAN;
    l->lead = 0;
    return l;
}

/** @brief The group's stacks emptied, and for one stacked to a hundred
 *         percent each category's whole found. */
static void
stacks_begin(pl_t *q)
{
    int i, s;

    for (i = 0; i < q->k->ncat; i++) {
        q->up[i] = q->down[i] = q->total[i] = 0;
        for (s = 0; s < q->g->nser; s++) {
            double v = value(q, &q->g->ser[s], i);

            q->total[i] += isnan(v) ? 0 : fabs(v);
        }
    }
}

/** @brief Whether the group stacks its series. */
static int
stacking(const pl_t *q)
{
    return q->g->grouping == CH_STACKED || q->g->grouping == CH_PERCENT;
}

/** @brief Bar @p i of series @p s: from @p from to @p to on the value
 *         axis, on its stack.  @return 0 for a point with no value. */
static int
bar_span(pl_t *q, const ch_series_t *s, int i, double *from, double *to)
{
    double v = value(q, s, i), *run;

    if (isnan(v) || i >= q->k->ncat) {
        return 0;
    }
    if (!stacking(q)) {
        *from = q->base;
        *to = v;
        return 1;
    }
    v = q->g->grouping == CH_PERCENT ? (q->total[i] > 0 ? v / q->total[i]
                                                        : 0) : v;
    run = v >= 0 ? &q->up[i] : &q->down[i];
    *from = *run;
    *to = *run += v;
    return 1;
}

/** @brief Line or area point @p i of @p s as its stack puts it, what is
 *         under it into @p under.  NAN for a point with no value. */
static double
piled(pl_t *q, const ch_series_t *s, int i, double *under)
{
    double v = value(q, s, i);

    *under = q->base;
    if (!stacking(q) || i >= q->k->ncat) {
        return v;
    }
    *under = q->up[i];
    if (isnan(v)) {
        return v;
    }
    v = q->g->grouping == CH_PERCENT ? (q->total[i] > 0 ? v / q->total[i]
                                                        : 0) : v;
    return q->up[i] += v;
}

/** @brief @p part added to label @p out, after separator @p sep when
 *         something is there before it. */
static void
add(const char *sep, const char *part, char *out, size_t max)
{
    size_t n = strlen(out);

    if (part == NULL || part[0] == '\0') {
        return;
    }
    snprintf(out + n, max - n, "%s%s", n > 0 ? sep : "", part);
}

/** @brief Point @p l's category into @p out: a scatter's x, its name,
 *         or its number. */
static void
category(const pl_t *q, const pl_label_t *l, char *out, size_t max)
{
    if (l->s->x != NULL) {
        ch_number(l->s->x[l->i], "General", out, max);
    } else if (q->k->cat != NULL && l->i < q->k->nnamed) {
        snprintf(out, max, "%s", q->k->cat[l->i]);
    } else {
        snprintf(out, max, "%d", l->i + 1);
    }
}

/** @brief Field @p type of point @p l's label into @p out: its value,
 *         category, series, share or cell.  @return 0 for one not known,
 *         whose own saved words stand. */
static int
field(const pl_t *q, const pl_label_t *l, const char *type, char *out,
      size_t max)
{
    const ch_series_t *s = l->s;

    if (type == NULL) {
        return 0;
    }
    if (strcmp(type, "VALUE") == 0 || strcmp(type, "YVALUE") == 0) {
        const ch_label_t *own = ch_own_label(s, l->i);

        ch_number(s->v[l->i], own != NULL && own->fmt != NULL ? own->fmt
                  : s->lbl_fmt != NULL ? s->lbl_fmt : s->fmt, out, max);
    } else if (strcmp(type, "CATEGORYNAME") == 0 ||
               strcmp(type, "XVALUE") == 0) {
        category(q, l, out, max);
    } else if (strcmp(type, "SERIESNAME") == 0 && s->name != NULL) {
        snprintf(out, max, "%s", s->name);
    } else if (strcmp(type, "PERCENTAGE") == 0 && !isnan(l->pct)) {
        ch_number(l->pct, "0%", out, max);
    } else if ((strcmp(type, "CELLREF") == 0 ||
                strcmp(type, "CELLRANGE") == 0) && l->i < s->ncells) {
        snprintf(out, max, "%s", s->cells[l->i]);
    } else {
        return 0;
    }
    return 1;
}

/** @brief A label's own rich text with its fields filled in for point
 *         @p l, its paragraphs and breaks as lines, into @p out. */
static void
fields_text(const pl_t *q, const pl_label_t *l, const tiku_xml_node_t *rich,
            char *out, size_t max)
{
    const tiku_xml_node_t *p, *r;
    char part[160];
    size_t n;

    out[0] = '\0';
    for (p = tiku_xml_child(rich, "a:p"); p; p = tiku_xml_next(p, "a:p")) {
        n = strlen(out);
        snprintf(out + n, max - n, "%s", n > 0 ? "\n" : "");
        for (r = tiku_xml_child(p, NULL); r; r = tiku_xml_next(r, NULL)) {
            part[0] = '\0';
            if (tiku_xml_is(r, "a:br")) {
                snprintf(part, sizeof part, "\n");
            } else if (!tiku_xml_is(r, "a:fld") ||
                       !field(q, l, tiku_xml_attr(r, "type"), part,
                              sizeof part)) {
                (void)tiku_xml_text(tiku_xml_child(r, "a:t"), part,
                                    sizeof part);
            }
            n = strlen(out);
            snprintf(out + n, max - n, "%s", part);
        }
    }
}

/** @brief Label @p l's words: its own (fields filled in), or its cells',
 *         its series' name, its category, its value and its share, as
 *         it asks. */
static void
label_text(const pl_t *q, const pl_label_t *l, char *out, size_t max)
{
    const ch_series_t *s = l->s;
    const ch_label_t *own = ch_own_label(s, l->i);
    const char *sep = own != NULL && own->sep != NULL ? own->sep : s->sep;
    const char *fmt = own != NULL && own->fmt != NULL ? own->fmt
                      : s->lbl_fmt;
    int shows = ch_labels_of(s, l->i), share = fmt != NULL &&
                (shows & CH_LBL_PCT) && strchr(fmt, '%') != NULL;
    char part[160];

    out[0] = '\0';
    if (own != NULL && own->rich != NULL) {
        fields_text(q, l, own->rich, out, max);
        return;
    }
    if (own != NULL && own->text != NULL) {
        snprintf(out, max, "%s", own->text);
        return;
    }
    if (s->show_cells && l->i < s->ncells) {
        add(sep, s->cells[l->i], out, max);
    }
    if (shows & CH_LBL_SER) {
        add(sep, s->name, out, max);
    }
    if (shows & CH_LBL_CAT) {
        category(q, l, part, sizeof part);
        add(sep, part, out, max);
    }
    if (shows & CH_LBL_VAL) {           /* a percent format is the share's */
        ch_number(s->v[l->i], fmt != NULL && !share ? fmt : s->fmt, part,
                  sizeof part);
        add(sep, part, out, max);
    }
    if ((shows & CH_LBL_PCT) && !isnan(l->pct)) {
        ch_number(l->pct, share ? fmt : "0%", part, sizeof part);
        add(sep, part, out, max);
    }
}

/* ------------------------------------------------------------------ bars  */

/** @brief Bar @p i's label: past its end, inside it, at its middle or at
 *         its base, @p base and @p end where its value begins and ends. */
static void
bar_label(pl_t *q, const ch_series_t *s, int i, const ch_box_t *b,
          float base, float end)
{
    int at = at_of(s, i) >= 0 ? at_of(s, i) : stacking(q) ? CH_AT_CENTRE
                                                      : CH_AT_OUT;
    float d = end >= base ? 1 : -1, mid, p, ext;

    mid = q->g->across ? b->y + b->h / 2 : b->x + b->w / 2;
    p = at == CH_AT_OUT ? end + d * PL_GAP : at == CH_AT_IN
        ? end - d * PL_GAP : at == CH_AT_BASE ? base + d * PL_GAP
        : (base + end) / 2;
    ext = at == CH_AT_OUT || at == CH_AT_BASE ? d : at == CH_AT_IN ? -d : 0;
    if (q->g->across) {
        note(q, s, i, p, mid, ext > 0 ? 0 : ext < 0 ? 2 : 1, 1, NAN);
    } else {
        note(q, s, i, mid, p, 1, ext > 0 ? 0 : ext < 0 ? 2 : 1, NAN);
    }
}

/** @brief Bar @p i of @p s, its near edge @p lead from the category's
 *         middle and @p bw across (both signed along the axis). */
static void
bar(pl_t *q, const ch_series_t *s, int i, float lead, float bw)
{
    double from, to;
    float c, a, b;
    dml_fill_t f;
    dml_line_t l;
    ch_box_t box;
    int e;

    if (!bar_span(q, s, i, &from, &to)) {
        return;
    }
    c = ch_place(q->cat, ch_cat(q->k, i)) + lead;
    a = ch_place(q->val, from);
    b = ch_place(q->val, to);
    q->pt[3 * i] = fminf(c, c + bw);    /* for its series' lines */
    q->pt[3 * i + 1] = fmaxf(c, c + bw);
    q->pt[3 * i + 2] = b;
    box.x = q->g->across ? fminf(a, b) : fminf(c, c + bw);
    box.y = q->g->across ? fminf(c, c + bw) : fminf(a, b);
    box.w = q->g->across ? fabsf(b - a) : fabsf(bw);
    box.h = q->g->across ? fabsf(bw) : fabsf(b - a);
    ch_point_fill(q->k, q->g, s, i, &f);
    if (s->invert && s->v[i] < 0 && (s->pt_fill == NULL ||
                                     !(s->pt_has[i] & 1))) {
        f = s->invert_fill;
    }
    point_line(q, s, i, &l);
    if (box.w > 0.01f && box.h > 0.01f) {
        ch_rect(q->k, &box, &f, l.width > 0 ? &l : NULL);
    }
    for (e = 0; e < s->nerr; e++) {
        if (s->err[e].dir == 'y') {
            ch_err(q->k, &s->err[e], i, s->v[i], to, c + bw / 2, q->val);
        }
    }
    if (ch_labels_of(s, i) != 0) {
        bar_label(q, s, i, &box, a, b);
    }
}

/** @brief A stacked series' tops joined, bar to bar, by its group's
 *         series lines: from the edges and tops its @p n bars noted. */
static void
series_lines(pl_t *q, int n)
{
    int i;

    for (i = 0; q->g->serlines && stacking(q) && i + 1 < n; i++) {
        const float *a = q->pt + 3 * i, *b = a + 3;

        if (isnan(a[2]) || isnan(b[2])) {
            continue;
        }
        if (q->g->across) {
            ch_segment(q->k, a[2], a[1], b[2], b[0], &q->g->ser_line);
        } else {
            ch_segment(q->k, a[1], a[2], b[0], b[2], &q->g->ser_line);
        }
    }
}

/** @brief The group's bars: each category's band shared among its series
 *         side by side, or one stack, the gap and overlap the file's. */
static void
bars(pl_t *q)
{
    const ch_group_t *g = q->g;
    int m = stacking(q) ? 1 : g->nser, si, i;
    double band = fabs(q->cat->to - q->cat->from) / (q->cat->hi - q->cat->lo);
    double ov = g->overlap / 100, bw = band / (m - (m - 1) * ov + g->gap /
                                               100);
    float dir = ch_place(q->cat, ch_cat(q->k, 1)) >=
                ch_place(q->cat, ch_cat(q->k, 0)) ? 1 : -1;

    for (si = 0; si < g->nser; si++) {
        const ch_series_t *s = &g->ser[si];
        double lead = -(m * bw - (m - 1) * ov * bw) / 2 +
                      (m > 1 ? si : 0) * bw * (1 - ov);

        for (i = 0; i < s->n; i++) {
            q->pt[3 * i + 2] = NAN;
            bar(q, s, i, (float)(dir * lead), (float)(dir * bw));
        }
        series_lines(q, s->n);
    }
}

/* ----------------------------------------------------------------- lines  */

/** @brief One unbroken run of @p m points (indices @p r into @p pt) added
 *         to @p p: straight, or smoothed through its neighbours. */
static void
run_path(ch_path_t *p, const float *pt, const int *r, int m, int smooth)
{
    int a;

    for (a = 0; a < m; a++) {
        const float *c = pt + 3 * r[a], *b, *z, *d;

        if (a == 0 || !smooth || m < 3) {
            (a == 0 ? ch_move : ch_line)(p, c[0], c[1]);
            continue;
        }
        b = pt + 3 * r[a - 1];
        z = pt + 3 * r[a > 1 ? a - 2 : a - 1];
        d = pt + 3 * r[a + 1 < m ? a + 1 : a];
        ch_curve(p, b[0] + (c[0] - z[0]) / 6, b[1] + (c[1] - z[1]) / 6,
                 c[0] - (d[0] - b[0]) / 6, c[1] - (d[1] - b[1]) / 6, c[0],
                 c[1]);
    }
}

/** @brief A series' line through its @p n points, broken where one is
 *         missing unless the chart spans them. */
static void
trace(pl_t *q, const ch_series_t *s, int n, const dml_line_t *l)
{
    ch_path_t p;
    int i, j, m;

    ch_path(&p, 0, 0, 0, 1);
    for (i = 0; i < n; i = j) {
        m = 0;
        for (j = i; j < n; j++) {
            if (q->pt[3 * j + 2] != 0) {
                q->run[m++] = j;
            } else if (m > 0 && q->k->blanks != 2) {
                break;
            }
        }
        run_path(&p, q->pt, q->run, m, s->smooth);
    }
    ch_paint(q->k, &p, NULL, l, 0, 0);
}

/** @brief Point @p i's label beside it, clear of its marker @p mk. */
static void
point_label(pl_t *q, const ch_series_t *s, int i, float mk)
{
    int at = at_of(s, i) >= 0 ? at_of(s, i) : CH_AT_RIGHT;
    float x = q->pt[3 * i], y = q->pt[3 * i + 1], r = mk / 2 + PL_GAP;

    if (at == CH_AT_LEFT) {
        note(q, s, i, x - r, y, 2, 1, NAN);
    } else if (at == CH_AT_ABOVE || at == CH_AT_OUT) {
        note(q, s, i, x, y - r, 1, 2, NAN);
    } else if (at == CH_AT_BELOW) {
        note(q, s, i, x, y + r, 1, 0, NAN);
    } else if (at == CH_AT_CENTRE || at == CH_AT_IN) {
        note(q, s, i, x, y, 1, 1, NAN);
    } else {
        note(q, s, i, x + r, y, 0, 1, NAN);
    }
}

/** @brief The group's drop lines from each of @p n points in q->pt down
 *         to where its category axis crosses. */
static void
drops(pl_t *q, int n)
{
    float base = ch_place(q->val, q->base);
    int i;

    for (i = 0; q->g->drop && i < n; i++) {
        if (q->pt[3 * i + 2] != 0) {
            ch_segment(q->k, q->pt[3 * i], q->pt[3 * i + 1], q->pt[3 * i],
                       base, &q->g->drop_line);
        }
    }
}

/** @brief Point @p i's error bars: along its value, and a scatter's
 *         along its x. */
static void
point_errs(pl_t *q, const ch_series_t *s, int i)
{
    int e;

    for (e = 0; e < s->nerr; e++) {
        const ch_err_t *b = &s->err[e];
        double x = s->x != NULL ? s->x[i] : i + 1;

        if (b->dir == 'y') {
            ch_err(q->k, b, i, s->v[i], s->v[i], q->pt[3 * i], q->val);
        } else if (q->g->kind == CH_SCATTER) {
            ch_err(q->k, b, i, x, x, q->pt[3 * i + 1], q->cat);
        }
    }
}

/** @brief A series' line, then its markers and labels, over its @p n
 *         points in q->pt. */
static void
series_points(pl_t *q, const ch_series_t *s, int n)
{
    int mk = ch_marker_of(q->g, s), i;
    dml_fill_t f;
    dml_line_t l;

    ch_series_line(q->k, q->g, s, &l);
    drops(q, n);
    if (l.width > 0) {
        trace(q, s, n, &l);
    }
    ch_marker_look(q->k, q->g, s, &f, &l);
    for (i = 0; i < n; i++) {
        if (q->pt[3 * i + 2] == 0) {
            continue;
        }
        if (mk != CH_MK_NONE) {
            ch_marker(q->k, mk, s->mk_size, q->pt[3 * i], q->pt[3 * i + 1],
                      &f, &l);
        }
        if (ch_labels_of(s, i) != 0) {
            point_label(q, s, i, mk != CH_MK_NONE ? s->mk_size : 0);
        }
        point_errs(q, s, i);
    }
}

/** @brief A line group's high-low lines: each category's lowest point to
 *         its highest. */
static void
hilo(pl_t *q)
{
    int i, s;

    for (i = 0; q->g->hilo && i < q->k->ncat; i++) {
        double lo = INFINITY, hi = -INFINITY;
        float x = ch_place(q->cat, ch_cat(q->k, i));

        for (s = 0; s < q->g->nser; s++) {
            double v = value(q, &q->g->ser[s], i);

            lo = isnan(v) || v >= lo ? lo : v;
            hi = isnan(v) || v <= hi ? hi : v;
        }
        if (hi > lo) {
            ch_segment(q->k, x, ch_place(q->val, lo), x, ch_place(q->val, hi),
                       &q->g->hilo_line);
        }
    }
}

/** @brief A line group's up-down bars: from its first series to its last,
 *         in the group's up fill where the last is higher, its down fill
 *         where lower. */
static void
updown(pl_t *q)
{
    const ch_group_t *g = q->g;
    double band = fabs(q->cat->to - q->cat->from) / (q->cat->hi - q->cat->lo);
    float w = (float)(band / (1 + g->updown_gap / 100));
    ch_box_t b;
    int i;

    for (i = 0; g->updown && g->nser > 1 && i < q->k->ncat; i++) {
        double a = value(q, &g->ser[0], i), z = value(q, &g->ser[g->nser - 1],
                                                     i);
        float ya, yz;

        if (isnan(a) || isnan(z)) {
            continue;
        }
        ya = ch_place(q->val, a);
        yz = ch_place(q->val, z);
        b.x = ch_place(q->cat, ch_cat(q->k, i)) - w / 2;
        b.w = w;
        b.y = fminf(ya, yz);
        b.h = fabsf(yz - ya);
        ch_rect(q->k, &b, z >= a ? &g->up : &g->down, &g->updown_line);
    }
}

/** @brief The group's lines: up-down bars and high-low lines under them,
 *         each series' points piled on those before when it stacks. */
static void
lines(pl_t *q)
{
    int si, i;

    updown(q);
    hilo(q);
    for (si = 0; si < q->g->nser; si++) {
        const ch_series_t *s = &q->g->ser[si];

        for (i = 0; i < s->n; i++) {
            double under, v = piled(q, s, i, &under);

            q->pt[3 * i] = ch_place(q->cat, ch_cat(q->k, i));
            q->pt[3 * i + 1] = isnan(v) ? 0 : ch_place(q->val, v);
            q->pt[3 * i + 2] = !isnan(v);
        }
        series_points(q, s, s->n);
    }
}

/* ----------------------------------------------------------------- areas  */

/** @brief The group's areas, in order: each down to its base, or to the
 *         series under it when they stack; a missing point counts as
 *         nothing. */
static void
areas(pl_t *q, const ch_box_t *p)
{
    int si, i;

    for (si = 0; si < q->g->nser; si++) {
        const ch_series_t *s = &q->g->ser[si];
        ch_path_t path;
        dml_fill_t f;
        dml_line_t l;

        for (i = 0; i < s->n; i++) {
            double under, v = piled(q, s, i, &under);

            q->pt[3 * i] = ch_place(q->cat, ch_cat(q->k, i));
            q->pt[3 * i + 1] = ch_place(q->val, isnan(v) ? under : v);
            q->pt[3 * i + 2] = ch_place(q->val, under);
            if (ch_labels_of(s, i) != 0 && !isnan(v)) {
                note(q, s, i, q->pt[3 * i], (q->pt[3 * i + 1] +
                     q->pt[3 * i + 2]) / 2, 1, 1, NAN);
            }
        }
        ch_path(&path, p->x, p->y, 1, 1);
        for (i = 0; i < s->n; i++) {
            (i == 0 ? ch_move : ch_line)(&path, q->pt[3 * i],
                                         q->pt[3 * i + 1]);
        }
        for (i = s->n - 1; i >= 0; i--) {
            ch_line(&path, q->pt[3 * i], q->pt[3 * i + 2]);
        }
        ch_close(&path);
        ch_point_fill(q->k, q->g, s, -1, &f);
        ch_series_line(q->k, q->g, s, &l);
        ch_paint(q->k, &path, &f, l.width > 0 ? &l : NULL, p->w, p->h);
        for (i = 0; q->g->drop && i < s->n; i++) {
            ch_segment(q->k, q->pt[3 * i], q->pt[3 * i + 1], q->pt[3 * i],
                       ch_place(q->val, q->base), &q->g->drop_line);
        }
    }
}

/* ------------------------------------------------------------------ pies  */

/** @brief Whether point @p i's label fits inside its slice (its @p share
 *         of a pie of radius @p r) at three fifths of the way out: its
 *         words within the slice's width there, not taller than half the
 *         slice is deep. */
static int
fits(const pl_t *q, const ch_series_t *s, int i, double share, float r)
{
    char text[512];
    double half = share * M_PI;
    float w, h, wide = 1.24f * r * (float)(half >= M_PI / 2 ? 1 : sin(half));
    pl_label_t l;

    l.s = s;
    l.i = i;
    l.pct = share;
    label_text(q, &l, text, sizeof text);
    ch_block_size(q->k, font_of(s, i), text, &w, &h);
    return w <= wide * 0.9f && h <= r * 0.5f;
}

/** @brief Whether point @p i's label is set outside its slice: asked to
 *         be, or placed "best" and too big for it. */
static int
set_outside(const pl_t *q, const ch_series_t *s, int i, double share,
            float r)
{
    int at = at_of(s, i);

    return at == CH_AT_OUT || (at == -1 && !fits(q, s, i, share, r));
}

/** @brief A slice's label: inside it, or outside, led from its edge when
 *         it goes there to fit; saying its @p share of the whole when
 *         asked. */
static void
slice_label(pl_t *q, const ch_series_t *s, int i, const pl_arc_t *c,
            double share)
{
    double mid = (c->a0 + c->a1) / 2;
    int at = at_of(s, i), out = c->r0 <= 0 && set_outside(q, s, i, share,
                                                          c->r1);
    float r = c->r0 > 0 ? (c->r0 + c->r1) / 2 : out
            ? c->r1 + PL_GAP + (at == -1 ? PL_LEAD : 0)
            : at == CH_AT_IN ? c->r1 * 0.8f : at == CH_AT_BASE
            ? c->r1 * 0.35f : c->r1 * 0.62f;
    pl_label_t *l = note(q, s, i, c->cx + r * (float)sin(mid),
                         c->cy - r * (float)cos(mid),
                         !out ? 1 : sin(mid) >= 0 ? 0 : 2, 1, share);

    if (l != NULL && c->r0 <= 0 && (out || hypotf(l->x - c->cx,
                                                  l->y - c->cy) > c->r1)) {
        l->ex = c->cx + c->r1 * (float)sin(mid);    /* led from its edge */
        l->ey = c->cy - c->r1 * (float)cos(mid);
        l->lead = at == -1;
        l->align = l->x >= c->cx ? 0 : 2;
    }
}

/** @brief Point @p i of @p s as a slice of @p c, pulled out as it asks. */
static void
slice(pl_t *q, const ch_series_t *s, int i, pl_arc_t *c, double share)
{
    double mid = (c->a0 + c->a1) / 2;
    float off = (s->pt_explode != NULL ? s->pt_explode[i] : s->explode) /
                100 * c->r1;
    float x = c->cx + off * (float)sin(mid), y = c->cy - off * (float)cos(mid);
    dml_fill_t f;
    dml_line_t l;
    ch_path_t p;

    ch_path(&p, x - c->r1, y - c->r1, 1, 1);
    if (c->r0 > 0) {
        ch_move(&p, x + c->r1 * (float)sin(c->a0), y - c->r1 *
                (float)cos(c->a0));
        ch_arc(&p, x, y, c->r1, c->a0, c->a1);
        ch_line(&p, x + c->r0 * (float)sin(c->a1), y - c->r0 *
                (float)cos(c->a1));
        ch_arc(&p, x, y, c->r0, c->a1, c->a0);
    } else if (c->a1 - c->a0 >= 2 * M_PI - 1e-6) {
        ch_move(&p, x, y - c->r1);
        ch_arc(&p, x, y, c->r1, 0, 2 * M_PI);
    } else {
        ch_move(&p, x, y);
        ch_line(&p, x + c->r1 * (float)sin(c->a0), y - c->r1 *
                (float)cos(c->a0));
        ch_arc(&p, x, y, c->r1, c->a0, c->a1);
    }
    ch_close(&p);
    ch_point_fill(q->k, q->g, s, i, &f);
    point_line(q, s, i, &l);
    ch_paint(q->k, &p, &f, l.width > 0 ? &l : NULL, 2 * c->r1, 2 * c->r1);
    if (ch_labels_of(s, i) != 0) {
        c->cx = x;
        c->cy = y;
        slice_label(q, s, i, c, share);
    }
}

/** @brief Series @p s as a ring of slices between @p c's radii, each its
 *         value's share of their whole; a pie's ring has no hole. */
static void
ring(pl_t *q, const ch_series_t *s, const pl_arc_t *c)
{
    double total = 0, a = q->g->first * M_PI / 180;
    pl_arc_t one;
    int i;

    for (i = 0; i < s->n; i++) {
        total += isnan(s->v[i]) ? 0 : fabs(s->v[i]);
    }
    for (i = 0; total > 0 && i < s->n; i++) {
        double share = isnan(s->v[i]) ? 0 : fabs(s->v[i]) / total;

        if (share > 0) {            /* round the way the chart goes */
            one = *c;
            one.a0 = q->g->ccw ? a - share * 2 * M_PI : a;
            one.a1 = q->g->ccw ? a : a + share * 2 * M_PI;
            slice(q, s, i, &one, share);
        }
        a += (q->g->ccw ? -share : share) * 2 * M_PI;
    }
}

/** @brief How far past the edge of a pie of radius @p r its labels set
 *         outside it reach, across into @p far[0] and down into
 *         @p far[1]: nothing when none are. */
static void
outside(const pl_t *q, const ch_series_t *s, float r, float *far)
{
    char text[512];
    double total = 0;
    float w, h;
    pl_label_t l;
    int i;

    far[0] = far[1] = 0;
    for (i = 0; i < s->n; i++) {
        total += isnan(s->v[i]) ? 0 : fabs(s->v[i]);
    }
    for (i = 0; i < s->n; i++) {
        l.pct = total > 0 ? fabs(s->v[i]) / total : NAN;
        if (ch_labels_of(s, i) == 0 || isnan(s->v[i]) ||
            !set_outside(q, s, i, l.pct, r)) {
            continue;
        }
        l.s = s;
        l.i = i;
        label_text(q, &l, text, sizeof text);
        ch_block_size(q->k, font_of(s, i), text, &w, &h);
        far[0] = fmaxf(far[0], w + PL_GAP + PL_LEAD);
        far[1] = fmaxf(far[1], h + PL_GAP);
    }
}

/** @brief Whether point @p i of @p s goes to a pie of pie's second plot:
 *         by its position (the last third, unless the file says how
 *         many), its value, its share of @p total, or the file's list. */
static int
in_second(const ch_group_t *g, const ch_series_t *s, int i, double total)
{
    double v = isnan(s->v[i]) ? 0 : fabs(s->v[i]);
    int last = g->split_at >= 1 ? (int)fmin(g->split_at, s->n)
                                : (s->n + 2) / 3;

    if (g->split == 1 && g->split_at >= 0) {
        return v < g->split_at;
    }
    if (g->split == 2 && g->split_at >= 0) {
        return total > 0 && v / total * 100 < g->split_at;
    }
    if (g->split == 3) {
        return g->second != NULL && i < g->nsecond && g->second[i];
    }
    return i >= s->n - last;
}

/** @brief The "Other" slice of @p c standing for the second plot's
 *         points (@p share of the whole), in the colour after the last
 *         point's, with its label. */
static void
other(pl_t *q, const ch_series_t *s, const pl_arc_t *c, double share,
      double sum)
{
    double mid = (c->a0 + c->a1) / 2;
    char text[160], part[48];
    dml_fill_t f;
    dml_line_t l;
    ch_path_t p;

    ch_path(&p, c->cx - c->r1, c->cy - c->r1, 1, 1);
    ch_move(&p, c->cx, c->cy);
    ch_line(&p, c->cx + c->r1 * (float)sin(c->a0),
            c->cy - c->r1 * (float)cos(c->a0));
    ch_arc(&p, c->cx, c->cy, c->r1, c->a0, c->a1);
    ch_close(&p);
    ch_point_fill(q->k, q->g, s, s->n, &f);
    ch_series_line(q->k, q->g, s, &l);
    ch_paint(q->k, &p, &f, l.width > 0 ? &l : NULL, 2 * c->r1, 2 * c->r1);
    text[0] = '\0';
    if (s->labels & CH_LBL_CAT) {
        add(s->sep, "Other", text, sizeof text);
    }
    if (s->labels & CH_LBL_VAL) {
        ch_number(sum, s->lbl_fmt != NULL ? s->lbl_fmt : s->fmt, part,
                  sizeof part);
        add(s->sep, part, text, sizeof text);
    }
    if (s->labels & CH_LBL_PCT) {
        ch_number(share, "0%", part, sizeof part);
        add(s->sep, part, text, sizeof text);
    }
    ch_text_block(q->k, &s->lbl_font, text,
                  c->cx + c->r1 * 0.62f * (float)sin(mid),
                  c->cy - c->r1 * 0.62f * (float)cos(mid), 1, 1);
}

/** @brief A bar of pie's second plot: its points stacked in the bar
 *         @p at (x y w h), centred on the pie's middle, each its share of
 *         @p sum. */
static void
second_bar(pl_t *q, const ch_series_t *s, double total, double sum,
           const float *at)
{
    float y = at[1] + at[3];
    dml_fill_t f;
    dml_line_t l;
    ch_box_t b;
    int i;

    for (i = 0; sum > 0 && i < s->n; i++) {
        if (!in_second(q->g, s, i, total) || isnan(s->v[i])) {
            continue;
        }
        b.x = at[0];
        b.w = at[2];
        b.h = (float)(fabs(s->v[i]) / sum) * at[3];
        b.y = y - b.h;
        y = b.y;
        ch_point_fill(q->k, q->g, s, i, &f);
        point_line(q, s, i, &l);
        ch_rect(q->k, &b, &f, l.width > 0 ? &l : NULL);
        if (ch_labels_of(s, i) != 0) {
            note(q, s, i, b.x + b.w / 2, b.y + b.h / 2, 1, 1,
                 fabs(s->v[i]) / total);
        }
    }
}

/** @brief A pie (@p c) whose points in @p second (1) or not (0) are
 *         drawn round it from angle @p a, each its share of @p sum, as
 *         shares of @p total for their labels. */
static void
part_ring(pl_t *q, const ch_series_t *s, int second, pl_arc_t *c,
          double a, double sum, double total)
{
    pl_arc_t one;
    int i;

    for (i = 0; sum > 0 && i < s->n; i++) {
        double v = isnan(s->v[i]) ? 0 : fabs(s->v[i]);

        if (in_second(q->g, s, i, total) != second || v == 0) {
            continue;
        }
        one = *c;
        one.a0 = a;
        one.a1 = a + v / sum * 2 * M_PI;
        a = one.a1;
        slice(q, s, i, &one, v / total);
    }
}

/** @brief Draw a pie of pie or bar of pie in plot box @p p: the first
 *         plot's points go round a pie, and the rest make its "Other"
 *         slice, which faces the second plot (a pie or a bar, second_size
 *         percent of the first's size) that shows them one by one.  When
 *         the group has series lines, they join the two plots. */
static void
of_pie(pl_t *q, const ch_box_t *p)
{
    const ch_series_t *s = &q->g->ser[0];
    float size = q->g->second_size / 100, r, at[4];
    double total = 0, sum = 0, half;
    pl_arc_t c, c2;
    int i;

    for (i = 0; i < s->n; i++) {
        total += isnan(s->v[i]) ? 0 : fabs(s->v[i]);
    }
    for (i = 0; i < s->n; i++) {
        sum += in_second(q->g, s, i, total) && !isnan(s->v[i])
               ? fabs(s->v[i]) : 0;
    }
    r = fminf(p->h / 2, p->w / (2.6f + 2 * size)) * 0.9f;
    c.cx = p->x + (p->w - (2 + 0.6f + 2 * size) * r) / 2 + r;
    c.cy = p->y + p->h / 2;
    c.r0 = 0;
    c.r1 = r;
    half = total > 0 ? sum / total * M_PI : 0;  /* "Other" faces right */
    part_ring(q, s, 0, &c, M_PI / 2 + half, total, total);
    c2 = c;
    c2.a0 = M_PI / 2 - half;
    c2.a1 = M_PI / 2 + half;
    if (sum > 0) {
        other(q, s, &c2, sum / total, sum);
    }
    at[0] = c.cx + r + 0.6f * r;    /* the gap between the two plots */
    at[1] = c.cy - r * size;
    at[2] = q->g->of_pie == 'b' ? r * size * 0.7f : 2 * r * size;
    at[3] = 2 * r * size;
    if (q->g->of_pie == 'b') {
        second_bar(q, s, total, sum, at);
    } else {
        c2.cx = at[0] + r * size;
        c2.r1 = r * size;
        part_ring(q, s, 1, &c2, M_PI / 2 - half, sum, total);
    }
    for (i = -1; q->g->serlines && sum > 0 && i <= 1; i += 2) {
        ch_segment(q->k, c.cx + r * (float)sin(M_PI / 2 - i * half),
                   c.cy - r * (float)cos(M_PI / 2 - i * half),
                   q->g->of_pie == 'b' ? at[0] : at[0] + r * size,
                   c.cy + i * r * size, &q->g->ser_line);
    }
}

/** @brief Labels @p on (@p n of them) put in their order down the page. */
static void
down_page(pl_label_t **on, int n)
{
    pl_label_t *t;
    int i, j;

    for (i = 1; i < n; i++) {
        for (j = i; j > 0 && on[j - 1]->y > on[j]->y; j--) {
            t = on[j];
            on[j] = on[j - 1];
            on[j - 1] = t;
        }
    }
}

/** @brief The labels set outside a pie on one side (@p side: 0 its
 *         right, 2 its left), noted from @p first on, kept from covering
 *         each other: in their order down the page each is pushed below
 *         the one above -- one moved by hand stays -- and a line led to
 *         it from its slice when it moved, or went out to fit. */
static void
spread(pl_t *q, int first, int side)
{
    static const dml_line_t lead = {.width = 0.75f, .color = 0xffa6a6a6u};
    pl_label_t *on[PL_SPREAD];
    char text[512];
    float w, h, below = -INFINITY, y;
    int n = 0, i;

    for (i = first; i < q->nlbl && n < PL_SPREAD; i++) {
        if (!isnan(q->lbl[i].ex) && q->lbl[i].align == side) {
            on[n++] = &q->lbl[i];
        }
    }
    down_page(on, n);
    for (i = 0; i < n; i++) {
        const ch_label_t *own = ch_own_label(on[i]->s, on[i]->i);
        int moved = own != NULL && (own->dx != 0 || own->dy != 0);

        label_text(q, on[i], text, sizeof text);
        ch_block_size(q->k, font_of(on[i]->s, on[i]->i), text, &w, &h);
        y = moved ? on[i]->y : fmaxf(on[i]->y, below + h / 2 + 1);
        if (moved || on[i]->lead || y - on[i]->y > 2) {
            ch_segment(q->k, on[i]->ex, on[i]->ey,
                       on[i]->x + (side == 0 ? -1 : 1), y, &lead);
        }
        on[i]->y = y;
        below = y + h / 2;
    }
}

/** @brief A pie (its first series) or a doughnut (a ring for each, the
 *         first innermost), in the middle of plot box @p p: filling it
 *         when the file sets it by hand, else with room kept round a pie
 *         for labels set outside it. */
static void
pies(pl_t *q, const ch_box_t *p)
{
    const ch_group_t *g = q->g;
    int rings = g->kind == CH_DOUGHNUT ? g->nser : 1, si, i, first;
    float most = 0, r = fminf(p->w, p->h) / 2 *
                        (q->k->has_plot_at && q->k->plot_inner ? 1 : 0.92f);
    float far[2], hole;
    pl_arc_t c;

    if (g->of_pie) {
        of_pie(q, p);
        return;
    }
    if (g->kind == CH_PIE && !q->k->has_plot_at) {  /* labels' room */
        outside(q, &g->ser[0], r, far);
        r = fmaxf(fminf(r, fminf(p->w / 2 - far[0], p->h / 2 - far[1])),
                  r / 4);
    }
    first = q->nlbl;
    for (si = 0; si < rings; si++) {
        const ch_series_t *s = &g->ser[si];

        most = s->explode > most ? s->explode : most;
        for (i = 0; s->pt_explode != NULL && i < s->n; i++) {
            most = s->pt_explode[i] > most ? s->pt_explode[i] : most;
        }
    }
    r /= 1 + most / 100;
    hole = g->kind == CH_DOUGHNUT ? r * g->hole / 100 : 0;
    c.cx = p->x + p->w / 2;
    c.cy = p->y + p->h / 2;
    for (si = 0; r > 0 && si < rings; si++) {
        c.r0 = hole + (r - hole) * si / rings;
        c.r1 = hole + (r - hole) * (si + 1) / rings;
        ring(q, &g->ser[si], &c);
    }
    spread(q, first, 0);
    spread(q, first, 2);
}

/* -------------------------------------------------------------- scatters  */

/** @brief A bubble series: each point a circle, its area its size's
 *         share of the group's largest, @p big across. */
static void
bubbles(pl_t *q, const ch_series_t *s, double most, float big)
{
    dml_fill_t f;
    dml_line_t l;
    ch_path_t p;
    int i;

    for (i = 0; i < s->n; i++) {
        float r = most > 0 && s->size[i] > 0
                  ? big / 2 * (float)sqrt(s->size[i] / most) : 0;
        float x = q->pt[3 * i], y = q->pt[3 * i + 1];

        if (q->pt[3 * i + 2] == 0 || !(r > 0)) {
            continue;
        }
        ch_path(&p, x - r, y - r, 1, 1);
        ch_move(&p, x, y - r);
        ch_arc(&p, x, y, r, 0, 2 * M_PI);
        ch_close(&p);
        ch_point_fill(q->k, q->g, s, i, &f);
        point_line(q, s, i, &l);
        ch_paint(q->k, &p, &f, l.width > 0 ? &l : NULL, 2 * r, 2 * r);
        if (ch_labels_of(s, i) != 0) {
            note(q, s, i, x, y, 1, 1, NAN);
        }
    }
}

/** @brief The group's scatters: points at their x and y, a line through
 *         them and their markers, or bubbles, in plot box @p p. */
static void
scatter(pl_t *q, const ch_box_t *p)
{
    double most = 0;
    int si, i;

    for (si = 0; si < q->g->nser; si++) {
        const ch_series_t *s = &q->g->ser[si];

        for (i = 0; s->size != NULL && i < s->n; i++) {
            most = s->size[i] > most ? s->size[i] : most;
        }
    }
    for (si = 0; si < q->g->nser; si++) {
        const ch_series_t *s = &q->g->ser[si];

        for (i = 0; i < s->n; i++) {
            double v = value(q, s, i), x = s->x != NULL ? s->x[i] : i + 1;

            q->pt[3 * i] = ch_place(q->cat, isnan(x) ? 0 : x);
            q->pt[3 * i + 1] = ch_place(q->val, isnan(v) ? 0 : v);
            q->pt[3 * i + 2] = !isnan(v) && !isnan(x);
        }
        if (s->size != NULL) {
            bubbles(q, s, most, fminf(p->w, p->h) / 4 *
                    q->g->bubble_scale / 100);
        } else {
            series_points(q, s, s->n);
        }
    }
}

/* ------------------------------------------------------------------ plot  */

void
ch_clip(const ch_chart_t *k, const ch_box_t *p)
{
    const float x[4] = {p->x, p->x + p->w, p->x + p->w, p->x};
    const float y[4] = {p->y, p->y, p->y + p->h, p->y + p->h};
    const dml_mat_t *m = k->m;
    int i;

    tiku_pdf_writer_save(k->c->w);
    for (i = 0; i < 4; i++) {
        (i == 0 ? tiku_pdf_writer_move : tiku_pdf_writer_line)(
            k->c->w, m->a * x[i] + m->c * y[i] + m->e,
            m->b * x[i] + m->d * y[i] + m->f);
    }
    tiku_pdf_writer_close(k->c->w);
    tiku_pdf_writer_clip(k->c->w, 0);
}

/** @brief The group's series' trendlines, held inside plot box @p p. */
static void
trends(pl_t *q, const ch_box_t *p)
{
    int s;

    for (s = 0; s < q->g->nser; s++) {
        if (q->g->ser[s].ntrend > 0 && q->g->kind != CH_AREA &&
            q->g->kind != CH_PIE && q->g->kind != CH_DOUGHNUT) {
            ch_clip(q->k, p);
            ch_trends(q->k, &q->g->ser[s], q->cat, q->val,
                      q->g->kind == CH_BAR && q->g->across);
            tiku_pdf_writer_restore(q->k->c->w);
        }
    }
}

/** @brief One group drawn in plot box @p p, its labels noted. */
static void
group(pl_t *q, const ch_group_t *g, const ch_box_t *p)
{
    int axes = g->kind != CH_PIE && g->kind != CH_DOUGHNUT;

    if (axes && (g->cat_ax < 0 || g->val_ax < 0)) {
        return;
    }
    if (g->kind == CH_RADAR) {
        ch_radar(q->k, g, p);
        return;
    }
    q->g = g;
    q->cat = axes ? &q->k->ax[g->cat_ax] : NULL;
    q->val = axes ? &q->k->ax[g->val_ax] : NULL;
    q->base = axes ? ch_cross(q->k, q->val) : 0;
    stacks_begin(q);
    if (g->kind == CH_BAR || g->kind == CH_AREA) {
        ch_clip(q->k, p);
        if (g->kind == CH_BAR) {
            bars(q);
        } else {
            areas(q, p);
        }
        tiku_pdf_writer_restore(q->k->c->w);
    } else if (g->kind == CH_LINE) {
        lines(q);
    } else if (g->kind == CH_SCATTER) {
        scatter(q, p);
    } else {
        pies(q, p);
    }
    trends(q, p);
}

/** @brief Label @p l set: its box (the file's ground and edge for it,
 *         when it gives them) behind its words, in its own look. */
static void
label_draw(ch_chart_t *k, const pl_label_t *l, const char *text)
{
    const ch_label_t *own = ch_own_label(l->s, l->i);
    const tiku_xml_node_t *sp = own != NULL && own->sppr != NULL
                                ? own->sppr : l->s->lbl_sppr;
    const ch_font_t *f = font_of(l->s, l->i);
    dml_fill_t fill;
    dml_line_t edge;
    ch_box_t b;

    memset(&edge, 0, sizeof edge);
    edge.width = 0.75f;
    if (sp != NULL && text[0] != '\0') {
        int filled = dml_fill(k->c, sp, &fill) && fill.kind != DML_NOFILL;
        int edged = tiku_xml_path(sp, "a:ln/a:solidFill") != NULL &&
                    dml_line(k->c, tiku_xml_child(sp, "a:ln"), &edge) &&
                    edge.width > 0;

        ch_block_size(k, f, text, &b.w, &b.h);
        b.x = l->x - (l->align == 1 ? b.w / 2 : l->align == 2 ? b.w : 0) - 2;
        b.y = l->y - (l->valign == 1 ? b.h / 2 : l->valign == 2 ? b.h : 0)
              - 1;
        b.w += 4;
        b.h += 2;
        if (filled || edged) {
            ch_rect(k, &b, filled ? &fill : NULL, edged ? &edge : NULL);
        }
    }
    ch_text_block(k, f, text, l->x, l->y, l->align, l->valign);
}

/** @brief How many labels the chart's points ask for, at most. */
static int
label_count(const ch_chart_t *k)
{
    int g, s, n = 0;

    for (g = 0; g < k->ngrp; g++) {
        for (s = 0; s < k->grp[g].nser; s++) {
            n += k->grp[g].ser[s].labels != 0 || k->grp[g].ser[s].nown > 0
                 ? k->grp[g].ser[s].n : 0;
        }
    }
    return n;
}

void
ch_plot(ch_chart_t *k, const ch_box_t *p)
{
    static const int order[] = {CH_RADAR, CH_AREA, CH_BAR, CH_PIE,
                                CH_DOUGHNUT, CH_LINE, CH_SCATTER};
    size_t n = (size_t)(k->ncat > 0 ? k->ncat : 1);
    char text[512];
    pl_t q;
    int o, g;

    memset(&q, 0, sizeof q);
    q.k = k;
    q.up = of_alloc(&k->mem, n * sizeof *q.up);
    q.down = of_alloc(&k->mem, n * sizeof *q.down);
    q.total = of_alloc(&k->mem, n * sizeof *q.total);
    q.pt = of_alloc(&k->mem, 3 * n * sizeof *q.pt);
    q.run = of_alloc(&k->mem, n * sizeof *q.run);
    q.cap = label_count(k);
    q.lbl = q.cap > 0 ? of_alloc(&k->mem, (size_t)q.cap * sizeof *q.lbl)
                      : NULL;
    if (k->mem.bad) {
        return;
    }
    for (o = 0; o < (int)(sizeof order / sizeof order[0]); o++) {
        for (g = 0; g < k->ngrp; g++) {
            if (k->grp[g].kind == order[o]) {
                group(&q, &k->grp[g], p);
            }
        }
    }
    for (o = 0; o < q.nlbl; o++) {
        label_text(&q, &q.lbl[o], text, sizeof text);
        label_draw(k, &q.lbl[o], text);
    }
}
