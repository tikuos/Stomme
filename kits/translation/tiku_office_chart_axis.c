/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_office_chart_axis.c - a chart's axes: the scale each value axis
 * takes, Excel's way where the file leaves it to the program (zero kept
 * in unless the values sit far above it, a step of one, two or five
 * times a power of ten, a little room past the extremes); where values
 * and categories fall; the room labels take, turned or thinned to fit;
 * and the gridlines, lines, ticks, labels and titles drawn.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_office_chart.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define AX_TICK   4.0f              /* a tick mark's length, points      */
#define AX_GAP    3.0f              /* between a tick and its label      */
#define AX_TICKS  1000              /* ticks one axis draws, at most     */

/* ----------------------------------------------------------------- range  */

/** @brief @p v taken into the running extremes @p lo, @p hi. */
static void
take(double v, double *lo, double *hi)
{
    if (!isnan(v)) {
        *lo = v < *lo ? v : *lo;
        *hi = v > *hi ? v : *hi;
    }
}

/** @brief The extremes of stacked group @p g: each category's positive
 *         values summed, and its negative ones. */
static void
stacks(const ch_chart_t *k, const ch_group_t *g, double *lo, double *hi)
{
    int i, s;

    for (i = 0; i < k->ncat; i++) {
        double up = 0, down = 0;

        for (s = 0; s < g->nser; s++) {
            double v = i < g->ser[s].n ? g->ser[s].v[i] : NAN;

            up += v > 0 ? v : 0;
            down += v < 0 ? v : 0;
        }
        take(up, lo, hi);
        take(down, lo, hi);
    }
}

/** @brief The values group @p g shows along axis @p ai into the extremes;
 *         @p pct set for a group stacked to a hundred percent. */
static void
group_span(const ch_chart_t *k, const ch_group_t *g, int ai, double *lo,
           double *hi, int *pct)
{
    int s, i, x = g->kind == CH_SCATTER && g->cat_ax == ai;

    if (g->val_ax != ai && !x) {
        return;
    }
    if (!x && g->grouping == CH_PERCENT && g->kind != CH_SCATTER) {
        stacks(k, g, lo, hi);
        *lo = *lo < 0 ? -1 : 0;
        *hi = *hi > 0 ? 1 : 0;
        *pct = 1;
        return;
    }
    if (!x && g->grouping == CH_STACKED && g->kind != CH_SCATTER) {
        stacks(k, g, lo, hi);
        return;
    }
    for (s = 0; s < g->nser; s++) {
        const ch_series_t *r = &g->ser[s];

        for (i = 0; i < r->n; i++) {
            take(!x ? r->v[i] : r->x != NULL ? r->x[i] : i + 1, lo, hi);
        }
    }
}

/* ----------------------------------------------------------------- scale  */

/** @brief The step one, two or five times a power of ten that parts
 *         @p range into at most @p most intervals. */
static double
nice(double range, int most)
{
    double raw = range / (most > 0 ? most : 1), p, m;

    if (!(raw > 0) || !isfinite(raw)) {
        return 1;
    }
    p = pow(10, floor(log10(raw)));
    m = raw / p;
    return (m <= 1 ? 1 : m <= 2 ? 2 : m <= 5 ? 5 : 10) * p;
}

/** @brief The nice step after @p step. */
static double
nicer(double step)
{
    double p = pow(10, floor(log10(step))), m = step / p;

    return (m < 1.5 ? 2 : m < 3 ? 5 : 10) * p;
}

/** @brief A logarithmic axis' scale: whole powers of its base about the
 *         values, the file's bounds where it gives them. */
static void
log_scale(ch_axis_t *a, double mn, double mx)
{
    double b = log(a->log);

    mn = mn > 0 ? mn : 1;
    mx = mx > mn ? mx : mn * a->log;
    a->lo = a->has_min && a->min > 0 ? a->min
                                     : pow(a->log, floor(log(mn) / b));
    a->hi = a->has_max && a->max > a->lo ? a->max
                                         : pow(a->log, ceil(log(mx) / b));
    a->hi = a->hi > a->lo ? a->hi : a->lo * a->log;
    a->step = a->log;
}

/** @brief Where an automatic scale starts and ends about @p mn .. @p mx:
 *         values all of one sign are measured from zero, unless all sit
 *         in the sixth farthest from it, when the scale stops half the
 *         range short of them; every other end has a twentieth of the
 *         range spare. */
static void
reach(double mn, double mx, double *lo, double *hi)
{
    double spare = (mx - mn) * 0.05;

    *lo = mn >= 0 ? (mn <= mx * 5 / 6 ? 0 : mn - (mx - mn) / 2) : mn - spare;
    *hi = mx <= 0 ? (mx >= mn * 5 / 6 ? 0 : mx + (mx - mn) / 2) : mx + spare;
    *lo = mn >= 0 && *lo < 0 ? 0 : *lo;
    *hi = mx <= 0 && *hi > 0 ? 0 : *hi;
}

/** @brief A value axis' scale over @p mn .. @p mx in at most @p most
 *         intervals. */
static void
value_scale(ch_axis_t *a, double mn, double mx, int most)
{
    double lo, hi, step, l = mn, h = mx;
    int tries;

    if (mx <= mn) {
        mn = mx == 0 ? 0 : mx > 0 ? 0 : mx;
        mx = mx == 0 ? 1 : mx > 0 ? mx : 0;
    }
    reach(mn, mx, &lo, &hi);
    if (a->tight && mx > mn) {      /* the data's own reach, a little */
        lo = mn - (mx - mn) * 0.05; /* room at each end                 */
        hi = mx + (mx - mn) * 0.05;
    }
    lo = a->has_min ? a->min : lo;
    hi = a->has_max ? a->max : hi;
    hi = hi > lo ? hi : lo + 1;
    step = a->has_unit ? a->unit : nice(hi - lo, most);
    for (tries = 0; tries < 12; tries++) {
        step = step > 0 && isfinite(step) ? step : 1;
        l = a->has_min ? lo : floor(lo / step) * step;
        h = a->has_max ? hi : ceil(hi / step) * step;
        if (a->has_unit || (h - l) / step <= most + 0.5) {
            break;
        }
        step = nicer(step);
    }
    l = isfinite(l) ? l : lo;
    h = isfinite(h) ? h : hi;
    a->lo = l;
    a->hi = h > l ? h : l + step;
    a->step = (a->hi - a->lo) / step > AX_TICKS ? (a->hi - a->lo) / AX_TICKS
                                                : step;
}

void
ch_scale(ch_chart_t *k, ch_axis_t *a, float len)
{
    double lo = INFINITY, hi = -INFINITY;
    int g, pct = 0, ai = (int)(a - k->ax), n = k->ncat > 0 ? k->ncat : 1;
    float lh = ch_line_h(&a->font);

    if (a->kind == CH_CAT_AX) {
        a->between = a->partner < 0 || k->ax[a->partner].between;
        a->lo = ch_cat(k, 0) - (a->between || n == 1 ? 0.5 : 0);
        a->hi = ch_cat(k, n - 1) + (a->between || n == 1 ? 0.5 : 0);
        a->step = 1;
        return;
    }
    for (g = 0; g < k->ngrp; g++) {
        group_span(k, &k->grp[g], ai, &lo, &hi, &pct);
    }
    if (lo > hi && !a->has_min && !a->has_max) {
        a->has_min = a->has_max = 1;        /* no values: nought to one */
        a->min = 0;
        a->max = 1;
    }
    lo = lo > hi ? 0 : lo;
    hi = hi < lo ? 1 : hi;
    if (a->log > 1) {
        log_scale(a, lo, hi);
        return;
    }
    len = len > 0 ? len : 1;
    if (pct && !a->has_min && !a->has_max) {
        a->has_min = a->has_max = 1;        /* a whole, and no more */
        a->min = lo;
        a->max = hi;
    }
    value_scale(a, lo, hi, (int)fmin(fmax(a->horiz ? len / (lh * 4.5f)
                                                   : len / (lh * 1.35f), 2),
                                         10));      /* as Excel counts */
}

double
ch_cat(const ch_chart_t *k, double i)
{
    int j = (int)fmin(fmax(floor(i + 0.5), 0), k->ncat - 1);

    return k->catpos != NULL && k->ncat > 0 ? k->catpos[j] + (i - j) : i;
}

float
ch_place(const ch_axis_t *a, double v)
{
    double f;

    if (a->log > 1) {
        v = v > 0 ? v : a->lo;
        f = (log(v) - log(a->lo)) / (log(a->hi) - log(a->lo));
    } else {
        f = a->hi != a->lo ? (v - a->lo) / (a->hi - a->lo) : 0.5;
    }
    f = isnan(f) ? 0 : fmin(fmax(a->reversed ? 1 - f : f, -100), 100);
    return (float)(a->from + f * (a->to - a->from));
}

double
ch_cross(const ch_chart_t *k, const ch_axis_t *a)
{
    const ch_axis_t *b = a->partner >= 0 ? &k->ax[a->partner] : NULL;
    int how = b != NULL ? b->crosses : 0;
    double v = how == 1 ? a->lo : how == 2 ? a->hi : how == 3 ? b->cross_at
                                                               : 0;

    if (a->kind == CH_CAT_AX) {
        v = how == 2 ? a->hi : how == 3 ? a->lo + b->cross_at - 1 : a->lo;
    }
    v = a->log > 1 && v <= 0 ? a->lo : v;
    return fmin(fmax(v, a->lo), a->hi);
}

/* ---------------------------------------------------------------- labels  */

int
ch_intervals(const ch_axis_t *a)
{
    double n = a->log > 1 ? log(a->hi / a->lo) / log(a->log)
                          : (a->hi - a->lo) / a->step;

    return n > 0 && n <= AX_TICKS ? (int)floor(n + 1e-6) : 0;
}

double
ch_tick(const ch_axis_t *a, int i)
{
    double v = a->log > 1 ? a->lo * pow(a->log, i) : a->lo + a->step * i;

    return a->log <= 1 && fabs(v) < a->step * 1e-9 ? 0 : v;
}

const char *
ch_value_fmt(const ch_chart_t *k, const ch_axis_t *a)
{
    int g, ai = (int)(a - k->ax);

    for (g = 0; g < k->ngrp && a->linked; g++) {
        if (k->grp[g].val_ax == ai && k->grp[g].grouping == CH_PERCENT) {
            return "0%";
        }
        if (k->grp[g].val_ax == ai && k->grp[g].kind != CH_SCATTER) {
            return k->grp[g].ser[0].fmt;
        }
    }
    return a->linked ? "General" : a->fmt;
}

void
ch_label(const ch_chart_t *k, const ch_axis_t *a, int i, char *out,
         size_t max)
{
    if (a->kind == CH_VAL_AX) {
        ch_number(ch_tick(a, i) / (a->units > 0 ? a->units : 1),
                  ch_value_fmt(k, a), out, max);
    } else if (k->catv != NULL && !a->linked && i < k->nnamed) {
        ch_number(k->catv[i], a->fmt, out, max);
    } else if (k->cat != NULL && i < k->nnamed) {
        snprintf(out, max, "%s", k->cat[i]);
    } else {
        snprintf(out, max, "%d", i + 1);
    }
}

/** @brief How many labels axis @p a has: its categories, or its ticks. */
static int
labels(const ch_chart_t *k, const ch_axis_t *a)
{
    return a->kind == CH_CAT_AX ? k->ncat : ch_intervals(a) + 1;
}

/** @brief How many rows of groups a category axis across shows under its
 *         categories: multi-level categories' outer levels. */
static int
groups(const ch_chart_t *k, const ch_axis_t *a)
{
    return a->kind == CH_CAT_AX && a->horiz && !a->flat ? k->nouter : 0;
}

/** @brief The widest of axis @p a's labels. */
static float
widest(ch_chart_t *k, const ch_axis_t *a)
{
    char buf[160];
    float w = 0, one;
    int i, n = labels(k, a), step = a->skip > 0 ? a->skip : 1;

    for (i = 0; i < n; i += step) {
        ch_label(k, a, i, buf, sizeof buf);
        one = ch_width(k, &a->font, buf);
        w = one > w ? one : w;
    }
    return w;
}

/** @brief Label @p s wrapped at its spaces into lines no wider than
 *         @p width (a word longer stays whole), into @p out.
 *  @return how many lines; 0 when a word alone is too wide. */
static int
wrap(ch_chart_t *k, const ch_font_t *f, const char *s, float width,
     char *out, size_t max)
{
    char line[160], try[160];
    size_t n = 0;
    int lines = 1, whole = 1;

    line[0] = out[0] = '\0';
    while (*s) {
        size_t w = strcspn(s, " ");

        snprintf(try, sizeof try, "%s%s%.*s", line, line[0] ? " " : "",
                 (int)w, s);
        if (line[0] && ch_width(k, f, try) > width) {
            n += (size_t)snprintf(out + n, max - n, "%s\n", line);
            n = n < max ? n : max - 1;
            snprintf(try, sizeof try, "%.*s", (int)w, s);
            lines++;
        }
        whole &= ch_width(k, f, try) <= width || strchr(try, ' ') != NULL;
        snprintf(line, sizeof line, "%s", try);
        s += w;
        s += *s == ' ';
    }
    snprintf(out + n, max - n, "%s", line);
    return whole ? lines : 0;
}

/** @brief The most lines axis @p a's labels take wrapped at @p width; 0
 *         when one will not wrap to fit. */
static int
wrapped(ch_chart_t *k, const ch_axis_t *a, float width)
{
    char buf[160], out[320];
    int i, most = 1, n;

    for (i = 0; i < labels(k, a); i++) {
        ch_label(k, a, i, buf, sizeof buf);
        if ((n = wrap(k, &a->font, buf, width, out, sizeof out)) == 0) {
            return 0;
        }
        most = n > most ? n : most;
    }
    return most;
}

/** @brief A category axis' labels fitted to its categories: across ones
 *         wrapped onto three lines at most, else turned, when they would
 *         touch (unless the file turns them); then thinned, as upright
 *         ones are, until they part. */
static void
fit(ch_chart_t *k, ch_axis_t *a, float wide)
{
    double band = fabs((double)(a->to - a->from)) / (a->hi - a->lo);
    float lh = ch_line_h(&a->font);
    int auto_skip = a->lbl_skip == 0;

    a->turn = a->horiz ? a->turn_file : 0;
    a->skip = auto_skip ? 1 : a->lbl_skip;
    a->wrap_w = 0;
    a->lines = 1;
    if (a->horiz && (!a->turn_set || a->turn_file == 0) &&
        wide > band * a->skip * 0.95 && auto_skip) {
        a->lines = wrapped(k, a, (float)band * 0.9f);
        a->wrap_w = a->lines > 0 && a->lines <= 3 ? (float)band * 0.9f : 0;
        a->turn = a->wrap_w > 0 || a->turn_set ? 0 : -45;
        a->lines = a->wrap_w > 0 ? a->lines : 1;
    }
    while (auto_skip && a->skip < k->ncat &&
           (a->horiz ? (a->turn != 0 ? lh * 1.4 : a->wrap_w > 0
                        ? a->wrap_w : wide + 4) : lh) > band * a->skip) {
        a->skip++;
    }
}

float
ch_axis_room(ch_chart_t *k, ch_axis_t *a)
{
    float lh = ch_line_h(&a->font), wide, room = 0;

    a->room = 0;
    if (a->deleted || !a->used) {
        return 0;
    }
    if (a == ch_table_axis(k)) {    /* a data table's row says them */
        ch_table_room(k, &wide, &room);
        a->room = room + (a->title != NULL ? ch_line_h(&a->title_font) +
                                             AX_GAP : 0);
        return a->room;
    }
    if (a->labels != 0) {
        a->skip = a->kind == CH_CAT_AX && a->lbl_skip > 0 ? a->lbl_skip : 1;
        a->turn = a->horiz ? a->turn_file : 0;
        wide = widest(k, a);
        if (a->kind == CH_CAT_AX) {
            fit(k, a, wide);
        }
        room = !a->horiz ? wide
               : fabsf(sinf(a->turn * (float)M_PI / 180)) * wide +
                 fabsf(cosf(a->turn * (float)M_PI / 180)) * lh * a->lines;
        room += a->horiz ? (lh + AX_GAP) * groups(k, a) : 0;
        room += AX_GAP + (a->tick == 1 || a->tick == 3 ? AX_TICK : 0);
    }
    if (a->title != NULL) {
        room += ch_line_h(&a->title_font) + AX_GAP;
    }
    if (a->units_name != NULL && a->title == NULL) {
        room += ch_line_h(&a->font) + AX_GAP;
    }
    a->room = room;
    return room;
}

/* ------------------------------------------------------------------ lines  */

/** @brief Where tick @p i of axis @p a falls: between categories, on
 *         them, or at a value. */
static float
tick_place(const ch_chart_t *k, const ch_axis_t *a, int i)
{
    if (a->kind == CH_CAT_AX) {
        return ch_place(a, ch_cat(k, a->between ? i - 0.5 : i));
    }
    return ch_place(a, ch_tick(a, i));
}

/** @brief How many ticks axis @p a has. */
static int
ticks(const ch_chart_t *k, const ch_axis_t *a)
{
    return a->kind == CH_CAT_AX ? k->ncat + (a->between ? 1 : 0)
                                : ch_intervals(a) + 1;
}

/** @brief A value axis' minor gridlines across plot box @p p: at its
 *         minor unit, or a fifth of its major one. */
static void
minor_grid(ch_chart_t *k, const ch_axis_t *a, const ch_box_t *p)
{
    double step = a->minor_unit > 0 ? a->minor_unit : a->step / 5;
    int i, n;

    if (a->kind != CH_VAL_AX || a->log > 1 || !(step > 0) ||
        !((a->hi - a->lo) / step <= AX_TICKS * 5)) {
        return;
    }
    n = (int)floor((a->hi - a->lo) / step + 1e-6);
    for (i = 1; i < n; i++) {
        float at = ch_place(a, a->lo + step * i);

        if (a->horiz) {
            ch_segment(k, at, p->y, at, p->y + p->h, &a->minor_line);
        } else {
            ch_segment(k, p->x, at, p->x + p->w, at, &a->minor_line);
        }
    }
}

void
ch_grid(ch_chart_t *k, const ch_box_t *p)
{
    int ai, i;

    for (ai = 0; ai < k->nax; ai++) {
        if (k->ax[ai].used && k->ax[ai].minor) {
            minor_grid(k, &k->ax[ai], p);
        }
    }
    for (ai = 0; ai < k->nax; ai++) {
        const ch_axis_t *a = &k->ax[ai];

        for (i = 0; a->used && a->grid && i < ticks(k, a); i++) {
            float at = tick_place(k, a, i);

            if (a->horiz) {
                ch_segment(k, at, p->y, at, p->y + p->h, &a->grid_line);
            } else {
                ch_segment(k, p->x, at, p->x + p->w, at, &a->grid_line);
            }
        }
    }
}

/** @brief Where axis @p a's labels stand: along its line, or at the low
 *         or high end of the axis it crosses. */
static float
label_line(const ch_chart_t *k, const ch_axis_t *a)
{
    const ch_axis_t *b = a->partner >= 0 ? &k->ax[a->partner] : NULL;

    if (b == NULL || a->labels == 1) {
        return a->at;
    }
    return ch_place(b, a->labels == 2 ? b->lo : b->hi);
}

/** @brief Axis @p a's tick marks, @p out the way its labels lie. */
static void
tick_marks(ch_chart_t *k, const ch_axis_t *a, float out)
{
    float in = a->tick == 2 || a->tick == 3 ? -AX_TICK * out : 0;
    float o = a->tick == 1 || a->tick == 3 ? AX_TICK * out : 0;
    int i;

    for (i = 0; (in != 0 || o != 0) && i < ticks(k, a); i++) {
        float t = tick_place(k, a, i);

        if (a->horiz) {
            ch_segment(k, t, a->at + in, t, a->at + o, &a->line);
        } else {
            ch_segment(k, a->at + in, t, a->at + o, t, &a->line);
        }
    }
}

/** @brief The groups of outer level @p lv set @p y past line @p at: each
 *         name centred over its categories, a rule between groups down
 *         through the rows. */
static void
group_row(ch_chart_t *k, const ch_axis_t *a, int lv, float at, float y)
{
    const char **o = k->outer[lv];
    float lh = ch_line_h(&a->font);
    int i, j;

    for (i = 0; i < k->ncat; i = j) {
        for (j = i + 1; j < k->ncat && o[j] == NULL; j++) {
        }
        if (o[i] != NULL) {
            ch_text(k, &a->font, o[i], (ch_place(a, ch_cat(k, i - 0.5)) +
                    ch_place(a, ch_cat(k, j - 0.5))) / 2, y, 1, 0, 0);
        }
        ch_segment(k, ch_place(a, ch_cat(k, i - 0.5)), at,
                   ch_place(a, ch_cat(k, i - 0.5)), y + lh, &a->line);
    }
    ch_segment(k, ch_place(a, ch_cat(k, k->ncat - 0.5)), at,
               ch_place(a, ch_cat(k, k->ncat - 0.5)), y + lh, &a->line);
}

/** @brief Axis @p a's labels, @p out the way they lie from line @p at. */
static void
tick_labels(ch_chart_t *k, const ch_axis_t *a, float at, float out)
{
    float off = (AX_GAP + (a->tick == 1 || a->tick == 3 ? AX_TICK : 0)) * out;
    char buf[160], lines[320];
    int i, n = labels(k, a), step = a->skip > 0 ? a->skip : 1;

    for (i = 0; a->labels != 0 && i < n; i += step) {
        float t = a->kind == CH_CAT_AX ? ch_place(a, ch_cat(k, i))
                                       : tick_place(k, a, i);

        ch_label(k, a, i, buf, sizeof buf);
        if (!a->horiz) {
            ch_text(k, &a->font, buf, at + off, t, out < 0 ? 2 : 0, 1, 0);
        } else if (a->turn != 0) {
            ch_text(k, &a->font, buf, t, at + off, a->turn < 0 ? 2 : 0, 1,
                    a->turn);
        } else {
            wrap(k, &a->font, buf, a->wrap_w > 0 ? a->wrap_w : 1e9f, lines,
                 sizeof lines);
            ch_text_block(k, &a->font, lines, t, at + off, 1,
                          out < 0 ? 2 : 0);
        }
    }
    for (i = 0; out > 0 && i < groups(k, a); i++) {
        group_row(k, a, i, at, at + off + (ch_line_h(&a->font) + AX_GAP) *
                  (a->lines + i));
    }
}

/** @brief The units axis @p a's labels are in ("Millions"), at the far
 *         side of its room beside plot box @p p, near its top end. */
static void
units_name(ch_chart_t *k, const ch_axis_t *a, const ch_box_t *p)
{
    float lh = ch_line_h(&a->font), w = ch_width(k, &a->font,
                                                   a->units_name);

    if (!a->horiz) {
        ch_text(k, &a->font, a->units_name, a->pos == 'r'
                ? p->x + p->w + a->room - lh / 2 : p->x - a->room + lh / 2,
                p->y + w / 2, 1, 1, -90);
    } else {
        ch_text(k, &a->font, a->units_name, p->x + p->w, a->pos == 't'
                ? p->y - a->room : p->y + p->h + a->room, 2,
                a->pos == 't' ? 0 : 2, 0);
    }
}

/** @brief Axis @p a's title, at the far side of the room it was given
 *         beside plot box @p p. */
static void
axis_title(ch_chart_t *k, const ch_axis_t *a, const ch_box_t *p)
{
    float th = ch_line_h(&a->title_font);

    switch (a->pos) {
    case 't':
        ch_text(k, &a->title_font, a->title, p->x + p->w / 2,
                p->y - a->room, 1, 0, 0);
        break;
    case 'l':
        ch_text(k, &a->title_font, a->title, p->x - a->room + th / 2,
                p->y + p->h / 2, 1, 1, -90);
        break;
    case 'r':
        ch_text(k, &a->title_font, a->title, p->x + p->w + a->room - th / 2,
                p->y + p->h / 2, 1, 1, -90);
        break;
    default:
        ch_text(k, &a->title_font, a->title, p->x + p->w / 2,
                p->y + p->h + a->room, 1, 2, 0);
        break;
    }
}

void
ch_axes(ch_chart_t *k, const ch_box_t *p)
{
    int ai;

    for (ai = 0; ai < k->nax; ai++) {
        const ch_axis_t *a = &k->ax[ai];
        float out = a->pos == 't' || a->pos == 'l' ? -1 : 1;

        if (!a->used || a->deleted) {
            continue;
        }
        if (a->has_line) {
            tick_marks(k, a, out);
            if (a->horiz) {
                ch_segment(k, a->from, a->at, a->to, a->at, &a->line);
            } else {
                ch_segment(k, a->at, a->from, a->at, a->to, &a->line);
            }
        }
        if (a != ch_table_axis(k)) {
            tick_labels(k, a, label_line(k, a), out);
        }
        if (a->title != NULL) {
            axis_title(k, a, p);
        } else if (a->units_name != NULL) {
            units_name(k, a, p);
        }
    }
}
