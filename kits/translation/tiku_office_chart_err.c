/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_office_chart_err.c - error bars: how far each point may be off,
 * drawn as a line along its value (or a scatter's x) with a cap at each
 * end -- a fixed amount, a percentage of the value, the standard error,
 * amounts of the file's own for each point, or so many standard
 * deviations of the series, drawn about its mean as Excel draws them.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_office_chart.h"

#include <math.h>

#define EB_CAP 3.0f                 /* half a cap's length, points       */

/** @brief How far the bars of point @p i (its own value @p v) run, up
 *         into @p up and down into @p down; where they are centred into
 *         @p mid when it is the series' mean. */
static void
amount(const ch_err_t *b, int i, double v, double *mid, double *up,
       double *down)
{
    switch (b->kind) {
    case CH_EV_PERCENT:
        *up = *down = fabs(v) * b->val / 100;
        break;
    case CH_EV_STDDEV:
        *mid = b->mean;
        *up = *down = b->sd * b->val;
        break;
    case CH_EV_STDERR:
        *up = *down = b->count > 0 ? b->sd / sqrt(b->count) : 0;
        break;
    case CH_EV_CUSTOM:
        *up = b->plus != NULL && i < b->nplus && !isnan(b->plus[i])
              ? b->plus[i] : 0;
        *down = b->minus != NULL && i < b->nminus && !isnan(b->minus[i])
                ? b->minus[i] : 0;
        break;
    default:
        *up = *down = b->val;
        break;
    }
    *up = b->type == CH_EB_MINUS ? 0 : fabs(*up);
    *down = b->type == CH_EB_PLUS ? 0 : fabs(*down);
}

/** @brief A line from @p p0 to @p p1: horizontal at y @p at when
 *         @p horiz, else vertical at x @p at. */
static void
along(ch_chart_t *k, int horiz, float p0, float p1, float at,
      const dml_line_t *l)
{
    if (horiz) {
        ch_segment(k, p0, at, p1, at, l);
    } else {
        ch_segment(k, at, p0, at, p1, l);
    }
}

void
ch_err_stats(const ch_series_t *s, ch_err_t *b)
{
    const double *v = b->dir == 'x' ? s->x : s->v;
    double sum = 0, sq = 0;
    int i, n = 0;

    for (i = 0; v != NULL && i < s->n; i++) {
        if (!isnan(v[i])) {
            sum += v[i];
            n++;
        }
    }
    b->count = n;
    b->mean = n > 0 ? sum / n : 0;
    for (i = 0; v != NULL && i < s->n; i++) {
        if (!isnan(v[i])) {
            sq += (v[i] - b->mean) * (v[i] - b->mean);
        }
    }
    b->sd = n > 1 ? sqrt(sq / (n - 1)) : 0;
}

void
ch_err(ch_chart_t *k, const ch_err_t *b, int i, double v, double mid,
       float at, const ch_axis_t *a)
{
    double up, down;
    float lo, hi;

    if (isnan(v) || isnan(mid)) {
        return;
    }
    amount(b, i, v, &mid, &up, &down);
    lo = ch_place(a, mid - down);
    hi = ch_place(a, mid + up);
    if (up == 0 && down == 0) {
        return;
    }
    along(k, a->horiz, lo, hi, at, &b->line);
    if (b->cap && down > 0) {
        along(k, !a->horiz, at - EB_CAP, at + EB_CAP, lo, &b->line);
    }
    if (b->cap && up > 0) {
        along(k, !a->horiz, at - EB_CAP, at + EB_CAP, hi, &b->line);
    }
}
