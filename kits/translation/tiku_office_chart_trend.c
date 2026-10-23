/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_office_chart_trend.c - trendlines: fitted to a series' points by
 * least squares, as a spreadsheet fits them -- a line, an exponential, a
 * logarithm, a power or a polynomial of up to the sixth order -- or a
 * moving average of so many points; drawn across the points' reach and
 * as far past it, forward and back, as the file asks.
 *
 * A category's x is its number, counted from one.  A polynomial is
 * fitted to x moved and scaled about its middle: the sixth powers of
 * dates as serial days are too large for the fit to stay exact.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_office_chart.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define TR_STEPS 64                 /* pieces a fitted curve is drawn in */
#define TR_TERMS 7                  /* a sixth order polynomial's        */

/** @brief A fitted curve: its kind and what fixes it. */
typedef struct {
    int    kind;                    /* CH_TR_*                           */
    int    terms;                   /* a polynomial's coefficients       */
    double c[TR_TERMS];             /* a and b, or the polynomial's      */
    double mid, span;               /* where a polynomial's x is centred */
} tr_fit_t;

/** @brief A trendline's own points: their x and y, only those with
 *         both, and how many. */
typedef struct {
    double *x, *y;
    int     n;
} tr_pts_t;

/* ------------------------------------------------------------------ fits  */

/**
 * @brief The straight line v = a + b u through the points by least
 *        squares, into @p a, @p b: u and v their x and y, or the
 *        logarithms @p ln asks for (1 x's, 2 y's) of those above nought;
 *        with @p fixed, through (0, @p at).  @return 0, or -1 for none.
 */
static int
line_fit(const tr_pts_t *p, int ln, int fixed, double at, double *a,
         double *b)
{
    double n = 0, su = 0, sv = 0, suu = 0, suv = 0, d;
    int i;

    for (i = 0; i < p->n; i++) {
        double x = p->x[i], y = p->y[i];

        if (((ln & 1) && !(x > 0)) || ((ln & 2) && !(y > 0))) {
            continue;
        }
        x = ln & 1 ? log(x) : x;
        y = (ln & 2 ? log(y) : y) - (fixed ? at : 0);
        n++;
        su += x;
        sv += y;
        suu += x * x;
        suv += x * y;
    }
    d = fixed ? suu : n * suu - su * su;
    if (n < (fixed ? 1 : 2) || !(fabs(d) > 1e-300)) {
        return -1;
    }
    *b = fixed ? suv / d : (n * suv - su * sv) / d;
    *a = fixed ? at : (sv - *b * su) / n;
    return isfinite(*a) && isfinite(*b) ? 0 : -1;
}

/**
 * @brief @p m equations of @p m unknowns (rows of @p a, augmented, each
 *        TR_TERMS + 1 wide) solved in place by elimination, pivoting on
 *        the largest; the unknowns into @p out.
 * @return 0, or -1 when they have no one answer.
 */
static int
solve(double a[TR_TERMS][TR_TERMS + 1], int m, double *out)
{
    int col, r, best, j;

    for (col = 0; col < m; col++) {
        for (best = col, r = col + 1; r < m; r++) {
            best = fabs(a[r][col]) > fabs(a[best][col]) ? r : best;
        }
        if (!(fabs(a[best][col]) > 1e-300)) {
            return -1;
        }
        for (j = 0; j <= m && best != col; j++) {
            double t = a[col][j];

            a[col][j] = a[best][j];
            a[best][j] = t;
        }
        for (r = 0; r < m; r++) {
            double f = r != col ? a[r][col] / a[col][col] : 0;

            for (j = col; j <= m && f != 0; j++) {
                a[r][j] -= f * a[col][j];
            }
        }
    }
    for (r = 0; r < m; r++) {
        out[r] = a[r][m] / a[r][r];
    }
    return 0;
}

/** @brief A polynomial of order @p order by least squares, x centred
 *         and scaled first, into @p f.  @return 0, or -1 for none. */
static int
poly_fit(const tr_pts_t *p, int order, tr_fit_t *f)
{
    double a[TR_TERMS][TR_TERMS + 1], lo = INFINITY, hi = -INFINITY;
    int i, r, c, m = order + 1;

    memset(a, 0, sizeof a);
    for (i = 0; i < p->n; i++) {
        lo = fmin(lo, p->x[i]);
        hi = fmax(hi, p->x[i]);
    }
    f->mid = (lo + hi) / 2;
    f->span = hi > lo ? (hi - lo) / 2 : 1;
    for (i = 0; i < p->n; i++) {
        double u = (p->x[i] - f->mid) / f->span, pw[2 * TR_TERMS];

        for (pw[0] = 1, r = 1; r < 2 * m; r++) {
            pw[r] = pw[r - 1] * u;
        }
        for (r = 0; r < m; r++) {
            for (c = 0; c < m; c++) {
                a[r][c] += pw[r + c];
            }
            a[r][m] += pw[r] * p->y[i];
        }
    }
    f->terms = m;
    return p->n > order ? solve(a, m, f->c) : -1;
}

/** @brief Trendline @p t fitted to points @p p, into @p f.
 *  @return 0, or -1 when the points fix no such curve. */
static int
fit(const ch_trend_t *t, const tr_pts_t *p, tr_fit_t *f)
{
    double c = t->icpt > 0 ? log(t->icpt) : 0;
    int err;

    f->kind = t->kind;
    switch (t->kind) {
    case CH_TR_EXP:
        err = line_fit(p, 2, t->has_icpt && t->icpt > 0, c, &f->c[0],
                       &f->c[1]);
        f->c[0] = exp(f->c[0]);
        return err;
    case CH_TR_LOG:
        return line_fit(p, 1, 0, 0, &f->c[0], &f->c[1]);
    case CH_TR_POWER:
        err = line_fit(p, 3, 0, 0, &f->c[0], &f->c[1]);
        f->c[0] = exp(f->c[0]);
        return err;
    case CH_TR_POLY:
        return poly_fit(p, t->order, f);
    default:
        return line_fit(p, 0, t->has_icpt, t->icpt, &f->c[0], &f->c[1]);
    }
}

/** @brief Fitted curve @p f's y at @p x; NAN where it has none. */
static double
value_at(const tr_fit_t *f, double x)
{
    double u, y = 0;
    int i;

    switch (f->kind) {
    case CH_TR_EXP:
        return f->c[0] * exp(f->c[1] * x);
    case CH_TR_LOG:
        return x > 0 ? f->c[0] + f->c[1] * log(x) : NAN;
    case CH_TR_POWER:
        return x > 0 ? f->c[0] * pow(x, f->c[1]) : NAN;
    case CH_TR_POLY:
        u = (x - f->mid) / f->span;
        for (i = f->terms - 1; i >= 0; i--) {
            y = y * u + f->c[i];
        }
        return y;
    default:
        return f->c[0] + f->c[1] * x;
    }
}

/* ----------------------------------------------------------------- lines  */

/** @brief Where (@p x, @p y) falls: a category's number or an x value
 *         along @p xa, y along @p ya; across swaps them. */
static void
place(const ch_axis_t *xa, const ch_axis_t *ya, int across, double x,
      double y, float *px, float *py)
{
    float a = ch_place(xa, xa->kind == CH_CAT_AX ? x - 1 : x);
    float b = ch_place(ya, y);

    *px = across ? b : a;
    *py = across ? a : b;
}

void
ch_trend_line(ch_chart_t *k, const ch_series_t *s, const ch_trend_t *t,
              dml_line_t *l)
{
    memset(l, 0, sizeof *l);
    if (t->has_line) {
        *l = t->line;
    } else {
        l->ndash = 2;
        l->dash[0] = 1;
        l->dash[1] = 2;
        l->cap = 1;
    }
    l->width = l->width < 0 || !t->has_line ? 1.5f : l->width;
    if (l->color == 0) {
        l->color = s->has_fill && s->fill.kind == DML_SOLID ? s->fill.color
                   : s->has_line && s->line.color ? s->line.color
                   : ch_palette(k, s->idx);
    }
}

/** @brief A moving average of @p t's period through points @p p. */
static void
average(ch_path_t *path, const tr_pts_t *p, const ch_trend_t *t,
        const ch_axis_t *xa, const ch_axis_t *ya, int across)
{
    double sum = 0;
    float x, y;
    int i, drawn = 0;

    for (i = 0; i < p->n; i++) {
        sum += p->y[i] - (i >= t->period ? p->y[i - t->period] : 0);
        if (i + 1 >= t->period) {
            place(xa, ya, across, p->x[i], sum / t->period, &x, &y);
            (drawn++ == 0 ? ch_move : ch_line)(path, x, y);
        }
    }
}

/** @brief Fitted curve @p f across @p lo .. @p hi, broken where it has
 *         no value. */
static void
curve(ch_path_t *path, const tr_fit_t *f, double lo, double hi,
      const ch_axis_t *xa, const ch_axis_t *ya, int across)
{
    int i, pen = 0;
    float x, y;

    for (i = 0; i <= TR_STEPS; i++) {
        double u = lo + (hi - lo) * i / TR_STEPS, v = value_at(f, u);

        if (!isfinite(v) || fabs(v) > 1e300) {
            pen = 0;
            continue;
        }
        place(xa, ya, across, u, v, &x, &y);
        (pen ? ch_line : ch_move)(path, x, y);
        pen = 1;
    }
}

/* ----------------------------------------------------------------- label  */

/** @brief Coefficient @p v for a trendline's label, into @p out: in its
 *         format, else to four places, trailing zeros dropped. */
static void
coef(const ch_trend_t *t, double v, char *out, size_t max)
{
    size_t n;

    if (t->lbl_fmt != NULL) {
        ch_number(v, t->lbl_fmt, out, max);
        return;
    }
    snprintf(out, max, "%.4f", fabs(v) < 5e-5 ? 0 : v);
    n = strlen(out);
    while (n > 1 && out[n - 1] == '0' && strchr(out, '.') != NULL) {
        out[--n] = '\0';
    }
    if (n > 1 && out[n - 1] == '.') {
        out[n - 1] = '\0';
    }
}

/** @brief R squared of fit @p f over points @p p: over y, or over the
 *         logarithm of y for exponentials and powers, as Excel takes it. */
static double
r_squared(const tr_fit_t *f, const tr_pts_t *p)
{
    int ln = f->kind == CH_TR_EXP || f->kind == CH_TR_POWER, i, n = 0;
    int xs = f->kind == CH_TR_LOG || f->kind == CH_TR_POWER;
    double mean = 0, all = 0, off = 0, y, e;

    for (i = 0; i < p->n; i++) {
        if ((!ln || p->y[i] > 0) && (!xs || p->x[i] > 0)) {
            mean += ln ? log(p->y[i]) : p->y[i];
            n++;
        }
    }
    mean = n > 0 ? mean / n : 0;
    for (i = 0; i < p->n; i++) {
        if ((!ln || p->y[i] > 0) && (!xs || p->x[i] > 0)) {
            y = ln ? log(p->y[i]) : p->y[i];
            e = value_at(f, p->x[i]);
            e = ln ? (e > 0 ? log(e) : y) : e;
            all += (y - mean) * (y - mean);
            off += (y - e) * (y - e);
        }
    }
    return all > 0 ? 1 - off / all : 1;
}

/** @brief A polynomial fit's coefficients in x itself, from those in its
 *         centred and scaled u = (x - mid) / span, into @p d. */
static void
unscale(const tr_fit_t *f, double *d)
{
    double binom, pw;
    int j, m;

    for (m = 0; m < f->terms; m++) {
        d[m] = 0;
    }
    for (j = 0; j < f->terms; j++) {
        for (m = 0, binom = 1; m <= j; m++) {
            pw = pow(-f->mid, j - m) / pow(f->span, j);
            d[m] += f->c[j] * binom * pw;
            binom = binom * (j - m) / (m + 1);
        }
    }
}

/** @brief A polynomial's equation, highest power first, into @p out. */
static void
poly_text(const ch_trend_t *t, const tr_fit_t *f, char *out, size_t max)
{
    static const char *const sup[] = {"", "", "\xc2\xb2", "\xc2\xb3",
                                      "\xe2\x81\xb4", "\xe2\x81\xb5",
                                      "\xe2\x81\xb6"};
    double d[TR_TERMS];
    char c[48];
    size_t n;
    int m;

    unscale(f, d);
    snprintf(out, max, "y =");
    for (m = f->terms - 1; m >= 0; m--) {
        n = strlen(out);
        coef(t, m == f->terms - 1 ? d[m] : fabs(d[m]), c, sizeof c);
        snprintf(out + n, max - n, "%s%s%s%s", m == f->terms - 1 ? " "
                 : d[m] < 0 ? " - " : " + ", c, m > 0 ? "x" : "", sup[m]);
    }
}

/** @brief A fit's equation, Excel's way, into @p out. */
static void
equation(const ch_trend_t *t, const tr_fit_t *f, char *out, size_t max)
{
    char a[48], b[48];
    int neg = f->c[0] < 0;

    if (f->kind == CH_TR_POLY) {
        poly_text(t, f, out, max);
        return;
    }
    coef(t, f->kind == CH_TR_EXP || f->kind == CH_TR_POWER ? f->c[0]
            : f->c[1], a, sizeof a);
    coef(t, f->kind == CH_TR_EXP || f->kind == CH_TR_POWER ? f->c[1]
            : fabs(f->c[0]), b, sizeof b);
    if (f->kind == CH_TR_EXP) {
        snprintf(out, max, "y = %se^(%sx)", a, b);
    } else if (f->kind == CH_TR_POWER) {
        snprintf(out, max, "y = %sx^%s", a, b);
    } else {
        snprintf(out, max, "y = %s%s %c %s", a, f->kind == CH_TR_LOG
                 ? "ln(x)" : "x", neg ? '-' : '+', b);
    }
}

/** @brief Trendline @p t's label: its equation and R squared, set by the
 *         end of the curve fitted to @p p (at x @p end), or where it was
 *         moved by hand. */
static void
trend_text(ch_chart_t *k, const ch_trend_t *t, const tr_fit_t *f,
           const tr_pts_t *p, double end, const ch_axis_t *xa,
           const ch_axis_t *ya, int across)
{
    char text[256], r2[48];
    float x, y;

    text[0] = '\0';
    if (t->eq) {
        equation(t, f, text, sizeof text);
    }
    if (t->r2) {
        snprintf(r2, sizeof r2, "%.4f", r_squared(f, p));
        snprintf(text + strlen(text), sizeof text - strlen(text),
                 "%sR\xc2\xb2 = %s", t->eq ? "\n" : "", r2);
    }
    place(xa, ya, across, end, value_at(f, end), &x, &y);
    if (!isfinite(x) || !isfinite(y)) {
        return;
    }
    ch_text_block(k, &t->lbl_font, text, x + t->lbl_dx * k->w,
                  y - 4 + t->lbl_dy * k->h, 2, 2);
}

/* ---------------------------------------------------------------- series  */

/** @brief Series @p s's points with both an x and a y, into @p p (its
 *         arrays of @p s->n from the chart's memory).  @return how many. */
static int
points(ch_chart_t *k, const ch_series_t *s, tr_pts_t *p)
{
    int i;

    p->n = 0;
    p->x = of_alloc(&k->mem, (size_t)s->n * sizeof *p->x);
    p->y = of_alloc(&k->mem, (size_t)s->n * sizeof *p->y);
    for (i = 0; p->x != NULL && p->y != NULL && i < s->n; i++) {
        double x = s->x != NULL ? s->x[i] : i + 1;

        if (!isnan(x) && !isnan(s->v[i])) {
            p->x[p->n] = x;
            p->y[p->n++] = s->v[i];
        }
    }
    return p->x != NULL && p->y != NULL ? p->n : 0;
}

void
ch_trends(ch_chart_t *k, const ch_series_t *s, const ch_axis_t *xa,
          const ch_axis_t *ya, int across)
{
    double lo = INFINITY, hi = -INFINITY;
    ch_path_t path;
    dml_line_t l;
    tr_pts_t p;
    tr_fit_t f;
    int t, i;

    if (points(k, s, &p) < 2) {
        return;
    }
    for (i = 0; i < p.n; i++) {
        lo = fmin(lo, p.x[i]);
        hi = fmax(hi, p.x[i]);
    }
    for (t = 0; t < s->ntrend; t++) {
        const ch_trend_t *tr = &s->trend[t];

        ch_path(&path, 0, 0, 0, 1);
        if (tr->kind == CH_TR_AVERAGE) {
            average(&path, &p, tr, xa, ya, across);
        } else if (fit(tr, &p, &f) == 0) {
            curve(&path, &f, lo - tr->back, hi + tr->fwd, xa, ya, across);
        }
        ch_trend_line(k, s, tr, &l);
        ch_paint(k, &path, NULL, &l, 0, 0);
        if (tr->kind != CH_TR_AVERAGE && (tr->eq || tr->r2) &&
            fit(tr, &p, &f) == 0) {
            trend_text(k, tr, &f, &p, hi + tr->fwd, xa, ya, across);
        }
    }
}
