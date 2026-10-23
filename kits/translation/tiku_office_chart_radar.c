/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_office_chart_radar.c - a radar chart: its categories as spokes
 * round a circle, the first at twelve o'clock; its values measured out
 * along them on the value axis' scale; gridlines as polygons through the
 * spokes at each tick; each series a closed line through its points,
 * with its markers, or filled.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_office_chart.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define RD_GAP 4.0f                 /* between the web and its labels    */
#define RD_WEB 20000                /* the web's corners, at most        */

/** @brief The web's centre and radius, and how many spokes it has. */
typedef struct {
    float cx, cy, r;
    int   n;
} rd_web_t;

/** @brief Category @p i's name into @p out. */
static void
spoke_name(const ch_chart_t *k, int i, char *out, size_t max)
{
    if (k->cat != NULL && i < k->nnamed) {
        snprintf(out, max, "%s", k->cat[i]);
    } else {
        snprintf(out, max, "%d", i + 1);
    }
}

/** @brief Where value @p v on spoke @p i falls, into @p x, @p y: its
 *         distance out the value axis' place for it. */
static void
spoke_at(const rd_web_t *w, const ch_axis_t *a, int i, double v, float *x,
         float *y)
{
    double t = 2 * M_PI * i / w->n;
    float r = w->cy - ch_place(a, isnan(v) ? a->lo : v);

    r = r > 0 ? r : 0;
    *x = w->cx + r * (float)sin(t);
    *y = w->cy - r * (float)cos(t);
}

/** @brief The web: a polygon through the spokes at each of value axis
 *         @p a's ticks, the spokes out to the last. */
static void
web(ch_chart_t *k, const rd_web_t *w, const ch_axis_t *a)
{
    int t, i;
    float x, y;
    ch_path_t p;

    for (t = 0; a->grid && (long)w->n * ch_intervals(a) <= RD_WEB &&
         t <= ch_intervals(a); t++) {
        ch_path(&p, w->cx - w->r, w->cy - w->r, 0, 1);
        for (i = 0; i < w->n; i++) {
            spoke_at(w, a, i, ch_tick(a, t), &x, &y);
            (i == 0 ? ch_move : ch_line)(&p, x, y);
        }
        ch_close(&p);
        ch_paint(k, &p, NULL, &a->grid_line, 0, 0);
    }
    for (i = 0; i < w->n; i++) {
        spoke_at(w, a, i, a->hi, &x, &y);
        ch_segment(k, w->cx, w->cy, x, y, a->grid ? &a->grid_line
                                                  : &a->line);
    }
}

/** @brief The axes' labels: each category past its spoke's end, the
 *         value axis' ticks up the first spoke. */
static void
labels(ch_chart_t *k, const rd_web_t *w, const ch_axis_t *cat,
       const ch_axis_t *val)
{
    char buf[160];
    float x, y;
    int i;

    for (i = 0; !cat->deleted && cat->labels != 0 && i < w->n; i++) {
        double t = 2 * M_PI * i / w->n, s = sin(t), c = cos(t);
        float out = w->r + RD_GAP + ch_line_h(&val->font) / 2 *
                    (float)fabs(c);

        spoke_name(k, i, buf, sizeof buf);
        ch_text(k, &cat->font, buf, w->cx + out * (float)s,
                w->cy - out * (float)c, s > 0.1 ? 0 : s < -0.1 ? 2 : 1,
                c > 0.1 ? 2 : c < -0.1 ? 0 : 1, 0);
    }
    for (i = 0; !val->deleted && val->labels != 0 &&
         i <= ch_intervals(val); i++) {
        spoke_at(w, val, 0, ch_tick(val, i), &x, &y);
        ch_label(k, val, i, buf, sizeof buf);
        ch_text(k, &val->font, buf, x - RD_GAP, y, 2, 1, 0);
    }
}

/** @brief Series @p s: a closed line through its points, filled when
 *         its group fills them, and its markers. */
static void
series(ch_chart_t *k, const ch_group_t *g, const ch_series_t *s,
       const rd_web_t *w, const ch_axis_t *a)
{
    int i, mk = ch_marker_of(g, s);
    dml_fill_t f;
    dml_line_t l;
    ch_path_t p;
    float x, y;

    ch_path(&p, w->cx - w->r, w->cy - w->r, 1, 1);
    for (i = 0; i < w->n && i < s->n; i++) {
        spoke_at(w, a, i, s->v[i], &x, &y);
        (i == 0 ? ch_move : ch_line)(&p, x, y);
    }
    ch_close(&p);
    ch_point_fill(k, g, s, -1, &f);
    ch_series_line(k, g, s, &l);
    ch_paint(k, &p, g->filled ? &f : NULL, l.width > 0 ? &l : NULL,
             2 * w->r, 2 * w->r);
    if (mk == CH_MK_NONE) {
        return;
    }
    ch_marker_look(k, g, s, &f, &l);
    for (i = 0; i < w->n && i < s->n; i++) {
        if (!isnan(s->v[i])) {
            spoke_at(w, a, i, s->v[i], &x, &y);
            ch_marker(k, mk, s->mk_size, x, y, &f, &l);
        }
    }
}

void
ch_radar(ch_chart_t *k, const ch_group_t *g, const ch_box_t *p)
{
    ch_axis_t *cat = &k->ax[g->cat_ax], *val = &k->ax[g->val_ax];
    float wide = 0, lh = ch_line_h(&cat->font);
    char buf[160];
    rd_web_t w;
    int i;

    w.n = k->ncat;
    for (i = 0; !cat->deleted && i < w.n; i++) {
        spoke_name(k, i, buf, sizeof buf);
        wide = fmaxf(wide, ch_width(k, &cat->font, buf));
    }
    w.cx = p->x + p->w / 2;
    w.cy = p->y + p->h / 2;
    w.r = fmaxf(fminf(p->w / 2 - wide, p->h / 2 - lh * 1.5f) - RD_GAP, 8);
    val->horiz = 0;
    val->from = w.cy;
    val->to = w.cy - w.r;
    ch_scale(k, val, w.r);
    if (w.n < 1) {
        return;
    }
    web(k, &w, val);
    for (i = 0; i < g->nser; i++) {
        series(k, g, &g->ser[i], &w, val);
    }
    labels(k, &w, cat, val);
}
