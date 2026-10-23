/*
 * KintsugiOS v0.01 kits -- the foundation KintsugiDesktop and KintsugiTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@kinstugi-os.org>
 *
 * tiku_vector_raster.c - ink on pixels: filling a path, stroking one, and
 * the masks that clip them.
 *
 * A path is flattened into straight pieces in device space as it is
 * built.  Filling sorts the pieces by their tops, walks scanlines SUB
 * times per pixel row over the pieces each one crosses, sorts the
 * crossings, and lays coverage between the ones the winding rule keeps,
 * fractional at the ends; stroking turns each piece and joint into a
 * small polygon and fills them all nonzero, which draws their union.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_vector_raster.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* Only the fault tests' copy of this file takes their realloc, which
 * fails when they say; the kit's own object uses the C library's. */
#ifdef TIKU_VECTOR_TEST_ALLOC
/** @brief The fault tests' realloc, which fails when a test says. */
extern void *tiku_vector_test_realloc(void *p, size_t bytes);
#define realloc tiku_vector_test_realloc
#endif

#define SUB         4               /* scanlines sampled per pixel row  */
#define POINTS_MAX  4000000         /* the most points a path takes     */
#define FLAT_MAX    64              /* pieces a lenient curve becomes   */
#define JOIN_MIN    1.2             /* lenient device width below which
                                       joints are left out              */
#define COORD_MAX   1e6             /* the farthest a coordinate goes   */
#define CURVE_ERROR 0.20            /* px a strict curve or disc strays
                                       from its straight pieces at most */
#define CURVE_DEPTH 24              /* most halvings of a strict curve  */
#define DISC_MAX    32768           /* most points of a strict disc     */
#define DISC_POINTS 24              /* most points of a lenient disc    */
#define DASH_ZERO   0.01            /* px a zero dash or gap is drawn   */
#define DASH_MIN    1e-3            /* px the shortest dash or gap is   */

/* ------------------------------------------------------------ matrices  */

tiku_vector_mat_t
tiku_vector_mat(double a, double b, double c, double d, double e, double f)
{
    tiku_vector_mat_t m = {a, b, c, d, e, f};

    return m;
}

tiku_vector_mat_t
tiku_vector_mat_mul(tiku_vector_mat_t m, tiku_vector_mat_t n)
{
    tiku_vector_mat_t r;

    r.a = m.a * n.a + m.b * n.c;
    r.b = m.a * n.b + m.b * n.d;
    r.c = m.c * n.a + m.d * n.c;
    r.d = m.c * n.b + m.d * n.d;
    r.e = m.e * n.a + m.f * n.c + n.e;
    r.f = m.e * n.b + m.f * n.d + n.f;
    return r;
}

void
tiku_vector_mat_apply(const tiku_vector_mat_t *m, double x, double y,
                      double *ox, double *oy)
{
    *ox = m->a * x + m->c * y + m->e;
    *oy = m->b * x + m->d * y + m->f;
}

double
tiku_vector_mat_scale(const tiku_vector_mat_t *m)
{
    double det = fabs(m->a * m->d - m->b * m->c);

    return det > 0 ? sqrt(det) : hypot(m->a, m->b);
}

/* ---------------------------------------------------------------- paths  */

void
tiku_vector_path_init(tiku_vector_path_t *p)
{
    memset(p, 0, sizeof *p);
}

void
tiku_vector_path_clear(tiku_vector_path_t *p)
{
    p->n = 0;
    p->nsub = 0;
    p->failed = 0;
}

void
tiku_vector_path_free(tiku_vector_path_t *p)
{
    free(p->pt);
    free(p->start);
    free(p->closed);
    tiku_vector_path_init(p);
}

/** @brief Room for one more point, within the path's limit.  @return 0
 *         when there is none, the path then failed. */
static int
room(tiku_vector_path_t *p)
{
    int limit = p->max_points > 0 && p->max_points < POINTS_MAX
                ? p->max_points : POINTS_MAX;

    if (p->failed) {
        return 0;
    }
    if (p->n >= limit) {
        p->failed = TIKU_VECTOR_LIMIT;
        return 0;
    }
    if (p->n + 1 > p->cap) {
        int cap = p->cap ? p->cap * 2 : 256;
        float *pt;

        if (cap > limit) {
            cap = limit;
        }
        pt = realloc(p->pt, (size_t)cap * 2 * sizeof *pt);
        if (pt == NULL) {
            p->failed = TIKU_VECTOR_NOMEM;
            return 0;
        }
        p->pt = pt;
        p->cap = cap;
    }
    return 1;
}

/** @brief @p v as a coordinate the rasteriser can hold: clamped to
 *         COORD_MAX either way, 0 for a NaN. */
static float
sane(double v)
{
    if (!(v > -COORD_MAX)) {
        return (v == v) ? (float)-COORD_MAX : 0.0f;
    }
    if (!(v < COORD_MAX)) {
        return (float)COORD_MAX;
    }
    return (float)v;
}

/** @brief One more point on the current subpath, room allowing; a strict
 *         path fails on one it cannot hold. */
static void
add_point(tiku_vector_path_t *p, double x, double y)
{
    if (p->strict && (!isfinite(x) || !isfinite(y) ||
        fabs(x) > COORD_MAX || fabs(y) > COORD_MAX)) {
        p->failed = TIKU_VECTOR_BAD;
        return;
    }
    if (!room(p)) {
        return;
    }
    p->pt[p->n * 2] = sane(x);
    p->pt[p->n * 2 + 1] = sane(y);
    p->n++;
    p->cx = sane(x);
    p->cy = sane(y);
}

void
tiku_vector_path_moveto(tiku_vector_path_t *p, double x, double y)
{
    if (p->failed) {
        return;
    }
    if (p->nsub >= (p->max_points > 0 ? p->max_points : POINTS_MAX)) {
        p->failed = TIKU_VECTOR_LIMIT;
        return;
    }
    if (p->nsub + 1 > p->subcap) {
        int cap = p->subcap ? p->subcap * 2 : 16;
        int *s = realloc(p->start, (size_t)cap * sizeof *s);
        int *c;

        if (s == NULL) {
            p->failed = TIKU_VECTOR_NOMEM;
            return;
        }
        /* Once realloc succeeds the old start array is gone, so the path
         * takes the new one before the closed flags grow: a failure there
         * leaves it owning both arrays. */
        p->start = s;
        c = realloc(p->closed, (size_t)cap * sizeof *c);
        if (c == NULL) {
            p->failed = TIKU_VECTOR_NOMEM;
            return;
        }
        p->closed = c;
        p->subcap = cap;
    }
    p->start[p->nsub] = p->n;
    p->closed[p->nsub] = 0;
    p->nsub++;
    add_point(p, x, y);
    p->sx = p->cx;
    p->sy = p->cy;
}

void
tiku_vector_path_lineto(tiku_vector_path_t *p, double x, double y)
{
    if (p->nsub == 0) {
        tiku_vector_path_moveto(p, x, y);
        return;
    }
    add_point(p, x, y);
}

/** @brief How far (@p x, @p y) lies from the segment (@p ax, @p ay) to
 *         (@p bx, @p by).  A cubic lies inside its control points' hull,
 *         so their two distances from its chord bound how far it strays. */
static double
chord_distance(double x, double y, double ax, double ay, double bx,
               double by)
{
    double dx = bx - ax, dy = by - ay, len = dx * dx + dy * dy;
    double t = len ? ((x - ax) * dx + (y - ay) * dy) / len : 0;

    t = fmax(0, fmin(1, t));
    return hypot(x - ax - t * dx, y - ay - t * dy);
}

/**
 * @brief The cubic from (@p x0, @p y0) by (@p x1, @p y1) and (@p x2,
 *        @p y2) to (@p x3, @p y3), halved until each half's control points
 *        lie within CURVE_ERROR of its chord, then added as that chord.
 *
 * A curve that needs more than CURVE_DEPTH halvings fails @p p with
 * TIKU_VECTOR_LIMIT; @p depth is how many this half has had.
 */
static void
checked_curve(tiku_vector_path_t *p, double x0, double y0, double x1,
              double y1, double x2, double y2, double x3, double y3,
              int depth)
{
    if (p->failed) {
        return;
    }
    if (chord_distance(x1, y1, x0, y0, x3, y3) <= CURVE_ERROR &&
        chord_distance(x2, y2, x0, y0, x3, y3) <= CURVE_ERROR) {
        add_point(p, x3, y3);
        return;
    }
    if (depth == CURVE_DEPTH) {
        p->failed = TIKU_VECTOR_LIMIT;
        return;
    }
    /* De Casteljau at t = 1/2: the two halves meet at (fx, fy). */
    double ax = (x0 + x1) / 2, ay = (y0 + y1) / 2;
    double bx = (x1 + x2) / 2, by = (y1 + y2) / 2;
    double cx = (x2 + x3) / 2, cy = (y2 + y3) / 2;
    double dx = (ax + bx) / 2, dy = (ay + by) / 2;
    double ex = (bx + cx) / 2, ey = (by + cy) / 2;
    double fx = (dx + ex) / 2, fy = (dy + ey) / 2;

    checked_curve(p, x0, y0, ax, ay, dx, dy, fx, fy, depth + 1);
    checked_curve(p, fx, fy, ex, ey, cx, cy, x3, y3, depth + 1);
}

void
tiku_vector_path_curveto(tiku_vector_path_t *p, double x1, double y1,
                         double x2, double y2, double x3, double y3)
{
    double x0 = p->cx, y0 = p->cy, len;
    int steps, i;

    if (p->nsub == 0) {
        tiku_vector_path_moveto(p, x0, y0);
    }
    if (p->strict) {
        if (!isfinite(x1) || !isfinite(y1) || !isfinite(x2) ||
            !isfinite(y2) || !isfinite(x3) || !isfinite(y3) ||
            fabs(x1) > COORD_MAX || fabs(y1) > COORD_MAX ||
            fabs(x2) > COORD_MAX || fabs(y2) > COORD_MAX ||
            fabs(x3) > COORD_MAX || fabs(y3) > COORD_MAX) {
            p->failed = TIKU_VECTOR_BAD;
            return;
        }
        checked_curve(p, x0, y0, x1, y1, x2, y2, x3, y3, 0);
        return;
    }
    len = hypot(x1 - x0, y1 - y0) + hypot(x2 - x1, y2 - y1) +
          hypot(x3 - x2, y3 - y2);
    len = sqrt(len * 2.0) + 1.0;       /* a NaN or huge one takes FLAT_MAX */
    steps = len < FLAT_MAX ? (int)len : FLAT_MAX;
    if (steps < 3) {
        steps = 3;
    }
    for (i = 1; i <= steps; i++) {
        double t = (double)i / steps, u = 1.0 - t;
        double x = u * u * u * x0 + 3 * u * u * t * x1 + 3 * u * t * t * x2 +
                   t * t * t * x3;
        double y = u * u * u * y0 + 3 * u * u * t * y1 + 3 * u * t * t * y2 +
                   t * t * t * y3;

        add_point(p, x, y);
    }
}

void
tiku_vector_path_close(tiku_vector_path_t *p)
{
    if (p->nsub > 0) {
        p->closed[p->nsub - 1] = 1;
        p->cx = p->sx;
        p->cy = p->sy;
    }
}

int
tiku_vector_path_bounds(const tiku_vector_path_t *p, int w, int h, int *x0,
                        int *y0, int *bw, int *bh)
{
    float minx = 1e9f, miny = 1e9f, maxx = -1e9f, maxy = -1e9f;
    int i, ax, ay, bx, by;

    for (i = 0; i < p->n; i++) {
        float x = p->pt[i * 2], y = p->pt[i * 2 + 1];

        if (x < minx) {
            minx = x;
        }
        if (x > maxx) {
            maxx = x;
        }
        if (y < miny) {
            miny = y;
        }
        if (y > maxy) {
            maxy = y;
        }
    }
    if (p->n == 0) {
        return 0;
    }
    ax = (int)floorf(minx);
    ay = (int)floorf(miny);
    bx = (int)ceilf(maxx) + 1;
    by = (int)ceilf(maxy) + 1;
    if (ax < 0) {
        ax = 0;
    }
    if (ay < 0) {
        ay = 0;
    }
    if (bx > w) {
        bx = w;
    }
    if (by > h) {
        by = h;
    }
    if (bx <= ax || by <= ay) {
        return 0;
    }
    *x0 = ax;
    *y0 = ay;
    *bw = bx - ax;
    *bh = by - ay;
    return 1;
}

/* -------------------------------------------------------------- filling  */

/** @brief Where an edge crosses a sub-scanline, and which way it goes. */
typedef struct {
    float x;                        /* across, from the box's left edge  */
    int   dir;                      /* 1 for an edge going down, -1 up   */
} crossing_t;

/** @brief qsort's order for crossings: left to right. */
static int
by_x(const void *a, const void *b)
{
    float d = ((const crossing_t *)a)->x - ((const crossing_t *)b)->x;

    return d < 0 ? -1 : d > 0 ? 1 : 0;
}

/** @brief Lay coverage over [xa, xb) of one sub-scanline into @p acc. */
static void
span(float *acc, int w, float xa, float xb)
{
    int ia, ib, i;

    if (xb <= 0 || xa >= (float)w) {
        return;
    }
    if (xa < 0) {
        xa = 0;
    }
    if (xb > (float)w) {
        xb = (float)w;
    }
    if (xb <= xa) {
        return;
    }
    ia = (int)xa;
    ib = (int)xb;
    if (ia == ib) {
        acc[ia] += xb - xa;
        return;
    }
    acc[ia] += (float)(ia + 1) - xa;
    for (i = ia + 1; i < ib && i < w; i++) {
        acc[i] += 1.0f;
    }
    if (ib < w) {
        acc[ib] += xb - (float)ib;
    }
}

tiku_vector_mask_t
tiku_vector_path_fill(const tiku_vector_path_t *p, int evenodd, int x0,
                      int y0, int w, int h)
{
    return tiku_vector_path_fill_cancel(p, evenodd, x0, y0, w, h, NULL,
                                        NULL);
}

/** @brief One edge of a path's outline, its upper end first. */
typedef struct {
    float xa, ya;                   /* its upper end                     */
    float xb, yb;                   /* its lower end                     */
    int   dir;                      /* 1 when the path runs down it      */
} edge_t;

/** @brief A path's edges sorted by their tops, and those a sample row
 *         crosses.  Sample rows only go down: an edge joins the active
 *         list once a row reaches its top and leaves once a row passes
 *         its bottom. */
typedef struct {
    edge_t *edge;                   /* the edges that are not level      */
    int    *active;                 /* indices of those in play          */
    int     count;                  /* edges                             */
    int     next;                   /* the first edge not yet in play    */
    int     live;                   /* entries of active in use          */
} table_t;

/** @brief qsort's order for edges: by their upper ends, top first. */
static int
by_top(const void *a, const void *b)
{
    float d = ((const edge_t *)a)->ya - ((const edge_t *)b)->ya;

    return d < 0 ? -1 : d > 0 ? 1 : 0;
}

/** @brief The edges of @p p that are not level, sorted, into @p t.
 *  @return 0, or -1 when storage was refused and @p t holds none. */
static int
table_open(const tiku_vector_path_t *p, table_t *t)
{
    int sub, i;

    memset(t, 0, sizeof *t);
    t->edge = malloc((size_t)(p->n + 1) * sizeof *t->edge);
    t->active = malloc((size_t)(p->n + 1) * sizeof *t->active);
    if (t->edge == NULL || t->active == NULL) {
        free(t->edge);
        free(t->active);
        memset(t, 0, sizeof *t);
        return -1;
    }
    for (sub = 0; sub < p->nsub; sub++) {
        int first = p->start[sub];
        int end = (sub + 1 < p->nsub) ? p->start[sub + 1] : p->n;
        int n = end - first;

        for (i = 0; i < n; i++) {
            const float *a = p->pt + (first + i) * 2;
            const float *b = p->pt + (first + (i + 1) % n) * 2;
            edge_t *e = &t->edge[t->count];

            if ((i + 1 == n && n < 2) || a[1] == b[1]) {
                continue;
            }
            e->dir = a[1] < b[1] ? 1 : -1;
            if (a[1] > b[1]) {
                const float *swap = a;

                a = b;
                b = swap;
            }
            e->xa = a[0];
            e->ya = a[1];
            e->xb = b[0];
            e->yb = b[1];
            t->count++;
        }
    }
    qsort(t->edge, (size_t)t->count, sizeof *t->edge, by_top);
    return 0;
}

/** @brief @p t's storage given back. */
static void
table_close(table_t *t)
{
    free(t->edge);
    free(t->active);
}

/** @brief Where the edges cross sample row @p y, less @p x0, into
 *         @p cross, unsorted.  Rows must come in increasing @p y.
 *  @return how many. */
static int
table_cross(table_t *t, float y, int x0, crossing_t *cross)
{
    int i, n = 0;

    while (t->next < t->count && t->edge[t->next].ya <= y) {
        t->active[t->live++] = t->next++;
    }
    for (i = 0; i < t->live;) {
        const edge_t *e = &t->edge[t->active[i]];

        if (y >= e->yb) {
            t->active[i] = t->active[--t->live];
            continue;
        }
        cross[n].x = e->xa + (e->xb - e->xa) * (y - e->ya) /
                     (e->yb - e->ya) - (float)x0;
        cross[n].dir = e->dir;
        n++;
        i++;
    }
    return n;
}

tiku_vector_mask_t
tiku_vector_path_fill_cancel(const tiku_vector_path_t *p, int evenodd,
                             int x0, int y0, int w, int h,
                             int (*cancel)(void *), void *ctx)
{
    tiku_vector_mask_t m = {x0, y0, w, h, NULL};
    crossing_t *cross;
    table_t t;
    float *acc;
    int row, s, i;

    if (p->n == 0 || w <= 0 || h <= 0 || p->failed ||
        (size_t)w > SIZE_MAX / (size_t)h ||
        (size_t)w + 1 > SIZE_MAX / sizeof *acc) {
        m.w = m.h = 0;
        return m;
    }
    m.a = calloc((size_t)w * (size_t)h, 1);
    acc = calloc((size_t)w + 1, sizeof *acc);
    cross = malloc((size_t)(p->n + 1) * sizeof *cross);
    if (m.a == NULL || acc == NULL || cross == NULL ||
        table_open(p, &t) != 0) {
        free(m.a);
        free(acc);
        free(cross);
        m.a = NULL;
        m.w = m.h = 0;
        return m;
    }
    for (row = 0; row < h; row++) {
        int touched = 0;

        if (cancel && cancel(ctx)) {
            tiku_vector_mask_free(&m);
            break;
        }
        for (s = 0; s < SUB; s++) {
            float y = (float)(y0 + row) + ((float)s + 0.5f) / SUB;
            int ncross = table_cross(&t, y, x0, cross), k, wind = 0;

            if (ncross < 2) {
                continue;
            }
            qsort(cross, (size_t)ncross, sizeof *cross, by_x);
            for (k = 0; k + 1 < ncross; k++) {
                wind += evenodd ? 1 : cross[k].dir;
                if (evenodd ? (wind & 1) : (wind != 0)) {
                    span(acc, w, cross[k].x, cross[k + 1].x);
                    touched = 1;
                }
            }
        }
        if (touched) {
            unsigned char *out = m.a + (size_t)row * (size_t)w;

            for (i = 0; i < w; i++) {
                float v = acc[i] * (255.0f / SUB);

                out[i] = (unsigned char)(v > 255.0f ? 255 : v + 0.5f);
                acc[i] = 0;
            }
        }
    }
    free(acc);
    free(cross);
    table_close(&t);
    return m;
}

/* ------------------------------------------------------------- stroking  */

/**
 * @brief Polygon @p pt of @p n points as a closed subpath of @p out,
 *        always wound the same way round.
 *
 * A stroke is its pieces, caps and joins filled together by the nonzero
 * rule: where two wound opposite ways overlap they cancel, and a round
 * join or cap shows a hole where it meets the line it rounds.
 */
static void
wound(tiku_vector_path_t *out, const double *pt, int n)
{
    double area = 0;
    int i;

    for (i = 0; i < n; i++) {
        int j = (i + 1) % n;

        area += pt[2 * i] * pt[2 * j + 1] - pt[2 * j] * pt[2 * i + 1];
    }
    for (i = 0; i < n; i++) {
        int k = area > 0 ? n - 1 - i : i;   /* the other way: reversed */

        if (i == 0) {
            tiku_vector_path_moveto(out, pt[2 * k], pt[2 * k + 1]);
        } else {
            tiku_vector_path_lineto(out, pt[2 * k], pt[2 * k + 1]);
        }
    }
    tiku_vector_path_close(out);
}

/** @brief A closed quadrilateral, as a subpath of @p out. */
static void
quad(tiku_vector_path_t *out, double ax, double ay, double bx, double by,
     double cx, double cy, double dx, double dy)
{
    double pt[8] = {ax, ay, bx, by, cx, cy, dx, dy};

    wound(out, pt, 4);
}

/**
 * @brief A filled circle of radius @p r at (x, y), as a polygon.
 *
 * A strict disc takes the fewest points, at least 8, whose chords keep
 * within CURVE_ERROR of the circle: r (1 - cos(pi / n)) at most that.  One
 * that needs more than DISC_MAX fails @p out with TIKU_VECTOR_LIMIT.
 */
static void
disc(tiku_vector_path_t *out, double x, double y, double r)
{
    if (out->strict) {
        int n, i;

        if (!isfinite(r) || r < 0 || r > COORD_MAX) {
            out->failed = TIKU_VECTOR_BAD;
            return;
        }
        n = r <= CURVE_ERROR ? 8
            : (int)ceil(M_PI / acos(1 - CURVE_ERROR / r));
        if (n < 8) {
            n = 8;
        }
        if (n > DISC_MAX) {
            out->failed = TIKU_VECTOR_LIMIT;
            return;
        }
        for (i = 0; i < n && !out->failed; i++) {
            double a = -2 * M_PI * i / n;
            double px = x + r * cos(a), py = y + r * sin(a);

            if (!i) {
                tiku_vector_path_moveto(out, px, py);
            } else {
                tiku_vector_path_lineto(out, px, py);
            }
        }
        tiku_vector_path_close(out);
        return;
    }
    int n = r < 2 ? 8 : r < 6 ? 12 : DISC_POINTS, i;
    double pt[2 * DISC_POINTS];

    for (i = 0; i < n; i++) {
        pt[2 * i] = x + r * cos(2 * M_PI * i / n);
        pt[2 * i + 1] = y + r * sin(2 * M_PI * i / n);
    }
    wound(out, pt, n);
}

/** @brief One straight piece, its body and a cap at each end asked for. */
static void
piece(tiku_vector_path_t *out, double ax, double ay, double bx, double by,
      double hw, int cap, int cap_a, int cap_b)
{
    double dx = bx - ax, dy = by - ay, len = hypot(dx, dy), nx, ny;

    if (len <= 0) {
        if (cap == 1) {
            disc(out, ax, ay, hw);
        } else if (cap == 2) {
            quad(out, ax - hw, ay - hw, ax + hw, ay - hw, ax + hw, ay + hw,
                 ax - hw, ay + hw);
        }
        return;
    }
    dx /= len;
    dy /= len;
    if (cap == 2) {
        if (cap_a) {
            ax -= dx * hw;
            ay -= dy * hw;
        }
        if (cap_b) {
            bx += dx * hw;
            by += dy * hw;
        }
    }
    nx = -dy * hw;
    ny = dx * hw;
    quad(out, ax + nx, ay + ny, bx + nx, by + ny, bx - nx, by - ny,
         ax - nx, ay - ny);
    if (cap == 1) {
        if (cap_a) {
            disc(out, ax, ay, hw);
        }
        if (cap_b) {
            disc(out, bx, by, hw);
        }
    }
}

/** @brief The joint at @p b between pieces from @p a and to @p c. */
static void
joint(tiku_vector_path_t *out, double ax, double ay, double bx, double by,
      double cx, double cy, double hw, int join, double miter)
{
    double d1x = bx - ax, d1y = by - ay, l1 = hypot(d1x, d1y);
    double d2x = cx - bx, d2y = cy - by, l2 = hypot(d2x, d2y);
    double cosang, theta;

    if (l1 <= 0 || l2 <= 0) {
        return;
    }
    d1x /= l1;
    d1y /= l1;
    d2x /= l2;
    d2y /= l2;
    if (join == 1) {
        disc(out, bx, by, hw);
        return;
    }
    cosang = d1x * d2x + d1y * d2y;
    if (cosang > 0.999) {
        return;                     /* straight on: no gap to fill */
    }
    /* The wedge between the two offset edges on the outside of the
     * turn, as a triangle to the corner; a miter adds the tip. */
    {
        double cross = d1x * d2y - d1y * d2x;
        double s = cross > 0 ? -1 : 1;   /* outside is opposite the turn */
        double p1x = bx + s * (-d1y) * hw, p1y = by + s * d1x * hw;
        double p2x = bx + s * (-d2y) * hw, p2y = by + s * d2x * hw;

        theta = acos(cosang < -1 ? -1 : cosang);
        if (join == 0 && miter >= 1 && theta < M_PI &&
            1.0 / sin((M_PI - theta) / 2) <= miter) {
            double mx = (p1x + p2x) / 2 - bx, my = (p1y + p2y) / 2 - by;
            double ml = hypot(mx, my), want = hw / cos(theta / 2);

            if (ml > 0) {
                quad(out, bx, by, p1x, p1y, bx + mx / ml * want,
                     by + my / ml * want, p2x, p2y);
                return;
            }
        }
        {
            double pt[6] = {bx, by, p1x, p1y, p2x, p2y};

            wound(out, pt, 3);
        }
    }
}

/** @brief One subpath's pieces and joints, cut by the dash pattern if
 *         there is one. */
static void
stroke_sub(const tiku_vector_path_t *p, int sub, double hw, int cap, int join,
           double miter, const double *dash, int ndash, double phase,
           tiku_vector_path_t *out)
{
    int first = p->start[sub];
    int end = (sub + 1 < p->nsub) ? p->start[sub + 1] : p->n;
    int n = end - first, closed = p->closed[sub], i, di = 0, on = 1;
    double left = 0, total = 0;
    int pieces = closed ? n : n - 1;

    if (n < 1) {
        return;
    }
    if (n == 1) {
        piece(out, p->pt[first * 2], p->pt[first * 2 + 1],
              p->pt[first * 2], p->pt[first * 2 + 1], hw, cap, 1, 1);
        return;
    }
    for (i = 0; i < ndash; i++) {
        total += dash[i] > 0 ? dash[i] : 0;
    }
    if (ndash > 0 && total > 0) {
        double ph = fmod(phase, total * (ndash & 1 ? 2 : 1));

        left = dash[0];
        while (ph >= left && ph > 0) {
            ph -= left;
            di = (di + 1) % ndash;
            on = !on;
            left = dash[di];
        }
        left -= ph;
    } else {
        ndash = 0;
    }
    for (i = 0; i < pieces && !out->failed; i++) {
        const float *a = p->pt + (first + i) * 2;
        const float *b = p->pt + (first + (i + 1) % n) * 2;
        double ax = a[0], ay = a[1], bx = b[0], by = b[1];
        double len = hypot(bx - ax, by - ay), at = 0;

        if (ndash == 0) {
            piece(out, ax, ay, bx, by, hw, cap, !closed && i == 0,
                  !closed && i == pieces - 1);
        } else {
            while (at < len && !out->failed) {
                double take = left < len - at ? left : len - at;
                double t0 = at / len, t1 = (at + take) / len;

                if (on && take > 0) {
                    piece(out, ax + (bx - ax) * t0, ay + (by - ay) * t0,
                          ax + (bx - ax) * t1, ay + (by - ay) * t1, hw, cap,
                          1, 1);
                }
                at += take;
                left -= take;
                /* A dash or gap shorter than DASH_MIN is drawn DASH_MIN
                 * long: the walk takes a step per entry, and entries of
                 * 1e-17 px would take billions of steps on one line. */
                if (left <= 1e-9) {
                    di = (di + 1) % ndash;
                    on = !on;
                    left = dash[di] <= 0 ? DASH_ZERO
                           : dash[di] < DASH_MIN ? DASH_MIN : dash[di];
                }
            }
        }
        if ((out->strict || hw >= JOIN_MIN / 2) && ndash == 0 &&
            (closed || i + 1 < pieces)) {
            const float *c = p->pt + (first + (i + 2) % n) * 2;

            joint(out, ax, ay, bx, by, c[0], c[1], hw, join, miter);
        }
    }
}

void
tiku_vector_path_stroke(const tiku_vector_path_t *p, double width, int cap,
                        int join, double miter, const double *dash,
                        int ndash, double phase, tiku_vector_path_t *out)
{
    double hw = width / 2;
    int s, i;

    tiku_vector_path_clear(out);
    if (p->failed) {
        out->failed = p->failed;
        return;
    }
    if (!isfinite(width) || !isfinite(miter) || !isfinite(phase) ||
        width < 0 || ndash < 0 || (ndash && !dash)) {
        out->failed = TIKU_VECTOR_BAD;
        return;
    }
    for (i = 0; i < ndash; i++) {
        if (!isfinite(dash[i]) || dash[i] < 0) {
            out->failed = TIKU_VECTOR_BAD;
            return;
        }
    }
    if (hw < 0.4 && !out->strict) {
        hw = 0.4;                   /* the thinnest line the screen shows */
    }
    for (s = 0; s < p->nsub && !out->failed; s++) {
        stroke_sub(p, s, hw, cap, join, miter, dash, ndash, phase, out);
    }
}

/* ---------------------------------------------------------------- masks  */

void
tiku_vector_mask_free(tiku_vector_mask_t *m)
{
    free(m->a);
    m->a = NULL;
    m->w = m->h = 0;
}

tiku_vector_mask_t
tiku_vector_mask_full(int w, int h)
{
    tiku_vector_mask_t m = {0, 0, w, h, NULL};

    if (w <= 0 || h <= 0 || (size_t)w > SIZE_MAX / (size_t)h) {
        m.w = m.h = 0;
        return m;
    }
    m.a = malloc((size_t)w * (size_t)h);
    if (m.a != NULL) {
        memset(m.a, 255, (size_t)w * (size_t)h);
    } else {
        m.w = m.h = 0;
    }
    return m;
}

void
tiku_vector_mask_intersect(tiku_vector_mask_t *m,
                           const tiku_vector_mask_t *clip)
{
    int x, y;

    if (clip == NULL || clip->a == NULL || m->a == NULL) {
        return;
    }
    for (y = 0; y < m->h; y++) {
        int cy = m->y0 + y - clip->y0;

        for (x = 0; x < m->w; x++) {
            int cx = m->x0 + x - clip->x0;
            unsigned char *a = m->a + (size_t)y * m->w + x;

            if (cy < 0 || cy >= clip->h || cx < 0 || cx >= clip->w) {
                *a = 0;
            } else {
                *a = (unsigned char)((*a * clip->a[(size_t)cy * clip->w + cx]
                                      + 127) / 255);
            }
        }
    }
}
