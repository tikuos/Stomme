/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_office_dml_geom.c - a shape's outline, as paths.
 *
 * DrawingML describes geometry in guides -- formulas over the shape's
 * size and its adjust values ("+- r 0 dx1") -- and paths through the
 * points they name (ECMA-376 part 1, 20.1.9).  Custom geometry is read
 * as written; the presets used most are written here in the same terms,
 * read by the same evaluator, and stars and polygons are made directly.
 * A preset not here is drawn as its bounding rectangle.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_office_dml.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define EMU      12700.0            /* EMUs to a point                   */
#define ANGLE    60000.0            /* an angle's units to a degree      */
#define GUIDES   192                /* names a shape may define          */
#define DEG      (M_PI / 180.0)

/** @brief The names a shape's formulas see, and their values (EMUs,
 *         60000ths of a degree, or plain numbers). */
typedef struct {
    struct { char name[24]; double v; } g[GUIDES];
    int n;
} env_t;

/** @brief A path being built: the geometry, its current subpath, where
 *         the pen is, and the scale path coordinates are in. */
typedef struct {
    dml_geom_t *geom;
    dml_sub_t  *sub;
    double      x, y;               /* the pen, in EMUs                  */
    double      sx, sy;             /* path units to EMUs                */
    int         bad;                /* out of memory                     */
} pen_t;

/** @brief A preset: its adjust values, guides, paths and text box, in the
 *         formula language custom geometry is written in. */
typedef struct {
    const char *name, *av, *gd, *path, *text;
} preset_t;

/* ------------------------------------------------------------ guides  */

/** @brief Name @p name set to @p v, over any value it had. */
static void
set(env_t *e, const char *name, double v)
{
    int i;

    for (i = 0; i < e->n; i++) {
        if (strcmp(e->g[i].name, name) == 0) {
            e->g[i].v = v;
            return;
        }
    }
    if (e->n < GUIDES) {
        snprintf(e->g[e->n].name, sizeof e->g[0].name, "%s", name);
        e->g[e->n++].v = v;
    }
}

/** @brief A token's value: a number, or a name set before; 0 otherwise. */
static double
value(const env_t *e, const char *tok, size_t n)
{
    char buf[24];
    size_t k = n > 0 && tok[0] == '-' ? 1 : 0;
    int i;

    if (n == 0) {
        return 0;
    }
    while (k < n && ((tok[k] >= '0' && tok[k] <= '9') || tok[k] == '.')) {
        k++;
    }
    snprintf(buf, sizeof buf, "%.*s", (int)n, tok);
    if (k == n) {
        return atof(buf);           /* a number; "3cd4" is a name */
    }
    for (i = e->n - 1; i >= 0; i--) {
        if (strcmp(e->g[i].name, buf) == 0) {
            return e->g[i].v;
        }
    }
    return 0;
}

/** @brief Formula @p f ("+- r 0 dx1": r plus 0 less dx1) worked out. */
static double
formula(const env_t *e, const char *f)
{
    double a[3] = {0, 0, 0};
    char op[8];
    const char *p = f;
    int k = 0;

    while (*p == ' ') {
        p++;
    }
    snprintf(op, sizeof op, "%.*s", (int)strcspn(p, " "), p);
    p += strcspn(p, " ");
    while (*p && k < 3) {
        size_t n;

        while (*p == ' ') {
            p++;
        }
        n = strcspn(p, " ");
        a[k++] = value(e, p, n);
        p += n;
    }
    if (strcmp(op, "*/") == 0) return a[2] != 0 ? a[0] * a[1] / a[2] : 0;
    if (strcmp(op, "+-") == 0) return a[0] + a[1] - a[2];
    if (strcmp(op, "+/") == 0) return a[2] != 0 ? (a[0] + a[1]) / a[2] : 0;
    if (strcmp(op, "?:") == 0) return a[0] > 0 ? a[1] : a[2];
    if (strcmp(op, "abs") == 0) return fabs(a[0]);
    if (strcmp(op, "at2") == 0) return atan2(a[1], a[0]) / DEG * ANGLE;
    if (strcmp(op, "cat2") == 0) return a[0] * cos(atan2(a[2], a[1]));
    if (strcmp(op, "sat2") == 0) return a[0] * sin(atan2(a[2], a[1]));
    if (strcmp(op, "cos") == 0) return a[0] * cos(a[1] / ANGLE * DEG);
    if (strcmp(op, "sin") == 0) return a[0] * sin(a[1] / ANGLE * DEG);
    if (strcmp(op, "tan") == 0) return a[0] * tan(a[1] / ANGLE * DEG);
    if (strcmp(op, "max") == 0) return fmax(a[0], a[1]);
    if (strcmp(op, "min") == 0) return fmin(a[0], a[1]);
    if (strcmp(op, "mod") == 0) return sqrt(a[0] * a[0] + a[1] * a[1] +
                                            a[2] * a[2]);
    if (strcmp(op, "pin") == 0) return a[1] < a[0] ? a[0] : a[1] > a[2]
                                       ? a[2] : a[1];
    if (strcmp(op, "sqrt") == 0) return a[0] > 0 ? sqrt(a[0]) : 0;
    return a[0];                    /* "val" */
}

/** @brief The names every shape's formulas may use, for its size. */
static void
builtins(env_t *e, double w, double h)
{
    static const struct { const char *name; int div; } wd[] = {
        {"wd2", 2}, {"wd3", 3}, {"wd4", 4}, {"wd5", 5}, {"wd6", 6},
        {"wd8", 8}, {"wd10", 10}, {"wd12", 12}, {"wd32", 32}};
    double ss = fmin(w, h);
    size_t i;
    char name[8];

    e->n = 0;
    set(e, "w", w);
    set(e, "h", h);
    set(e, "l", 0);
    set(e, "t", 0);
    set(e, "r", w);
    set(e, "b", h);
    set(e, "hc", w / 2);
    set(e, "vc", h / 2);
    set(e, "ss", ss);
    set(e, "ls", fmax(w, h));
    for (i = 0; i < sizeof wd / sizeof wd[0]; i++) {
        set(e, wd[i].name, w / wd[i].div);
        snprintf(name, sizeof name, "hd%d", wd[i].div);
        set(e, name, h / wd[i].div);
        snprintf(name, sizeof name, "ssd%d", wd[i].div);
        set(e, name, ss / wd[i].div);
    }
    set(e, "ssd16", ss / 16);
    set(e, "cd2", 10800000);
    set(e, "cd4", 5400000);
    set(e, "cd8", 2700000);
    set(e, "3cd4", 16200000);
    set(e, "3cd8", 8100000);
    set(e, "5cd8", 13500000);
    set(e, "7cd8", 18900000);
}

/** @brief Each "name formula;" of @p list set, in order. */
static void
guides(env_t *e, const char *list)
{
    char name[24], f[96];

    while (list != NULL && *list) {
        size_t n = strcspn(list, ";"), k;

        while (*list == ' ') {
            list++;
            n--;
        }
        k = strcspn(list, " ");
        if (k < n && k < sizeof name) {
            snprintf(name, sizeof name, "%.*s", (int)k, list);
            snprintf(f, sizeof f, "%.*s", (int)(n - k), list + k);
            set(e, name, formula(e, f));
        }
        list += n + (list[n] == ';');
    }
}

/* --------------------------------------------------------------- paths  */

/** @brief Room for @p n floats more in the current subpath. */
static float *
room(pen_t *p, int n)
{
    dml_sub_t *s = p->sub;

    if (s == NULL) {
        p->bad = 1;
        return NULL;
    }
    if (s->n + n > s->cap) {
        int cap = s->cap ? s->cap * 2 : 64;
        float *g;

        while (cap < s->n + n) {
            cap *= 2;
        }
        if ((g = realloc(s->pt, (size_t)cap * sizeof *g)) == NULL) {
            p->bad = 1;
            return NULL;
        }
        s->pt = g;
        s->cap = cap;
    }
    s->n += n;
    return s->pt + s->n - n;
}

/** @brief A new subpath, filled and stroked as asked. */
static void
subpath(pen_t *p, int fill, int stroke)
{
    dml_geom_t *g = p->geom;

    if (g->nsub >= (int)(sizeof g->sub / sizeof g->sub[0])) {
        p->sub = &g->sub[g->nsub - 1];  /* more than room: joined on */
        return;
    }
    p->sub = &g->sub[g->nsub++];
    memset(p->sub, 0, sizeof *p->sub);
    p->sub->fill = fill;
    p->sub->stroke = stroke;
}

/** @brief An op and its points, EMUs made points. */
static void
emit(pen_t *p, char op, const double *xy, int n)
{
    float *f = room(p, 1 + n * 2);
    int i;

    if (f == NULL) {
        return;
    }
    f[0] = op;
    for (i = 0; i < n * 2; i++) {
        f[1 + i] = (float)(xy[i] / EMU);
    }
    if (n > 0) {
        p->x = xy[n * 2 - 2];
        p->y = xy[n * 2 - 1];
    }
}

/**
 * @brief An arc from the pen: part of the ellipse of radii @p wr and
 *        @p hr the pen lies on at angle @p st, swept by @p sw (60000ths
 *        of a degree, clockwise, y down), as Béziers of a quarter turn
 *        at most.
 */
static void
arc(pen_t *p, double wr, double hr, double st, double sw)
{
    double a0 = st / ANGLE * DEG, t0, cx, cy, step, k;
    int n, i;

    if (wr <= 0 || hr <= 0 || fabs(sw) < 1) {
        return;
    }
    t0 = atan2(wr * sin(a0), hr * cos(a0));     /* the angle on it */
    cx = p->x - wr * cos(t0);
    cy = p->y - hr * sin(t0);
    step = ceil(fabs(sw / ANGLE) / 90.0);   /* quarter turns: 1 to 16 */
    n = step >= 1 && step <= 16 ? (int)step : step > 16 ? 16 : 1;
    step = sw / ANGLE * DEG / n;
    k = 4.0 / 3.0 * tan(step / 4);
    for (i = 0; i < n; i++) {
        double a = t0 + step * i, b = a + step, c[6];

        c[0] = cx + wr * (cos(a) - k * sin(a));
        c[1] = cy + hr * (sin(a) + k * cos(a));
        c[2] = cx + wr * (cos(b) + k * sin(b));
        c[3] = cy + hr * (sin(b) - k * cos(b));
        c[4] = cx + wr * cos(b);
        c[5] = cy + hr * sin(b);
        emit(p, 'C', c, 3);
    }
}

/** @brief A quadratic Bézier from the pen, as the cubic it is. */
static void
quad(pen_t *p, double x1, double y1, double x, double y)
{
    double c[6] = {p->x + (x1 - p->x) * 2 / 3, p->y + (y1 - p->y) * 2 / 3,
                   x + (x1 - x) * 2 / 3, y + (y1 - y) * 2 / 3, x, y};

    emit(p, 'C', c, 3);
}

/* ------------------------------------------------------------ presets  */

/* In the formula language of 20.1.9: av and gd are "name formula;" lists;
 * a path is ops -- P fill stroke (a subpath), W w h (its units), M, L,
 * A wR hR st sw, Q, C, Z -- over names or numbers; text is its box. */
static const preset_t presets[] = {
    {"rect", "", "", "M l t L r t L r b L l b Z", "l t r b"},
    {"roundRect", "adj val 16667", "a pin 0 adj 50000;dx1 */ ss a 100000;"
     "x2 +- r 0 dx1;y2 +- b 0 dx1;il */ dx1 29289 100000;ir +- r 0 il;"
     "ib +- b 0 il", "M l dx1 A dx1 dx1 cd2 cd4 L x2 t A dx1 dx1 3cd4 cd4 "
     "L r y2 A dx1 dx1 0 cd4 L dx1 b A dx1 dx1 cd4 cd4 Z", "il il ir ib"},
    {"ellipse", "", "idx cos wd2 2700000;idy sin hd2 2700000;"
     "il +- hc 0 idx;ir +- hc idx 0;it +- vc 0 idy;ib +- vc idy 0",
     "M l vc A wd2 hd2 cd2 cd4 A wd2 hd2 3cd4 cd4 A wd2 hd2 0 cd4 "
     "A wd2 hd2 cd4 cd4 Z", "il it ir ib"},
    {"triangle", "adj val 50000", "a pin 0 adj 100000;x1 */ w a 200000;"
     "x2 */ w a 100000;x3 +- x1 wd2 0", "M l b L x2 t L r b Z",
     "x1 vc x3 b"},
    {"rtTriangle", "", "it */ h 7 12;ir */ w 7 12;ib */ h 11 12",
     "M l t L r b L l b Z", "wd12 it ir ib"},
    {"diamond", "", "ir */ w 3 4;ib */ h 3 4",
     "M l vc L hc t L r vc L hc b Z", "wd4 hd4 ir ib"},
    {"parallelogram", "adj val 25000", "maxAdj */ 100000 w ss;"
     "a pin 0 adj maxAdj;x2 */ ss a 100000;x5 +- r 0 x2",
     "M l b L x2 t L r t L x5 b Z", "x2 t x5 b"},
    {"trapezoid", "adj val 25000", "maxAdj */ 50000 w ss;"
     "a pin 0 adj maxAdj;x2 */ ss a 100000;x3 +- r 0 x2",
     "M l b L x2 t L x3 t L r b Z", "x2 t x3 b"},
    {"hexagon", "adj val 25000", "maxAdj */ 50000 w ss;"
     "a pin 0 adj maxAdj;x1 */ ss a 100000;x2 +- r 0 x1",
     "M l vc L x1 t L x2 t L r vc L x2 b L x1 b Z", "x1 t x2 b"},
    {"octagon", "adj val 29289", "a pin 0 adj 50000;x1 */ ss a 100000;"
     "x2 +- r 0 x1;y2 +- b 0 x1;il */ x1 1 2;ir +- r 0 il;ib +- b 0 il",
     "M l x1 L x1 t L x2 t L r x1 L r y2 L x2 b L x1 b L l y2 Z",
     "il il ir ib"},
    {"rightArrow", "adj1 val 50000;adj2 val 50000", "maxAdj2 */ 100000 w ss;"
     "a1 pin 0 adj1 100000;a2 pin 0 adj2 maxAdj2;dx1 */ ss a2 100000;"
     "x1 +- r 0 dx1;dy1 */ h a1 200000;y1 +- vc 0 dy1;y2 +- vc dy1 0",
     "M l y1 L x1 y1 L x1 t L r vc L x1 b L x1 y2 L l y2 Z", "l y1 x1 y2"},
    {"leftArrow", "adj1 val 50000;adj2 val 50000", "maxAdj2 */ 100000 w ss;"
     "a1 pin 0 adj1 100000;a2 pin 0 adj2 maxAdj2;dx2 */ ss a2 100000;"
     "x2 +- l dx2 0;dy1 */ h a1 200000;y1 +- vc 0 dy1;y2 +- vc dy1 0",
     "M l vc L x2 t L x2 y1 L r y1 L r y2 L x2 y2 L x2 b Z", "x2 y1 r y2"},
    {"upArrow", "adj1 val 50000;adj2 val 50000", "maxAdj2 */ 100000 h ss;"
     "a1 pin 0 adj1 100000;a2 pin 0 adj2 maxAdj2;dy2 */ ss a2 100000;"
     "y2 +- t dy2 0;dx1 */ w a1 200000;x1 +- hc 0 dx1;x2 +- hc dx1 0",
     "M l y2 L hc t L r y2 L x2 y2 L x2 b L x1 b L x1 y2 Z", "x1 y2 x2 b"},
    {"downArrow", "adj1 val 50000;adj2 val 50000", "maxAdj2 */ 100000 h ss;"
     "a1 pin 0 adj1 100000;a2 pin 0 adj2 maxAdj2;dy1 */ ss a2 100000;"
     "y1 +- b 0 dy1;dx1 */ w a1 200000;x1 +- hc 0 dx1;x2 +- hc dx1 0",
     "M l y1 L x1 y1 L x1 t L x2 t L x2 y1 L r y1 L hc b Z", "x1 t x2 y1"},
    {"leftRightArrow", "adj1 val 50000;adj2 val 50000",
     "maxAdj2 */ 50000 w ss;a1 pin 0 adj1 100000;a2 pin 0 adj2 maxAdj2;"
     "x2 */ ss a2 100000;x3 +- r 0 x2;dy */ h a1 200000;y1 +- vc 0 dy;"
     "y2 +- vc dy 0", "M l vc L x2 t L x2 y1 L x3 y1 L x3 t L r vc L x3 b "
     "L x3 y2 L x2 y2 L x2 b Z", "x2 y1 x3 y2"},
    {"upDownArrow", "adj1 val 50000;adj2 val 50000",
     "maxAdj2 */ 50000 h ss;a1 pin 0 adj1 100000;a2 pin 0 adj2 maxAdj2;"
     "y2 */ ss a2 100000;y3 +- b 0 y2;dx */ w a1 200000;x1 +- hc 0 dx;"
     "x2 +- hc dx 0", "M l y2 L hc t L r y2 L x2 y2 L x2 y3 L r y3 L hc b "
     "L l y3 L x1 y3 L x1 y2 Z", "x1 y2 x2 y3"},
    {"chevron", "adj val 50000", "maxAdj */ 100000 w ss;"
     "a pin 0 adj maxAdj;x1 */ ss a 100000;x2 +- r 0 x1",
     "M l t L x2 t L r vc L x2 b L l b L x1 vc Z", "x1 t x2 b"},
    {"homePlate", "adj val 50000", "maxAdj */ 100000 w ss;"
     "a pin 0 adj maxAdj;dx1 */ ss a 100000;x1 +- r 0 dx1",
     "M l t L x1 t L r vc L x1 b L l b Z", "l t x1 b"},
    {"plus", "adj val 25000", "a pin 0 adj 50000;x1 */ ss a 100000;"
     "x2 +- r 0 x1;y2 +- b 0 x1", "M l x1 L x1 x1 L x1 t L x2 t L x2 x1 "
     "L r x1 L r y2 L x2 y2 L x2 b L x1 b L x1 y2 L l y2 Z", "l x1 r y2"},
    {"donut", "adj val 25000", "a pin 0 adj 50000;dr */ ss a 100000;"
     "iwd2 +- wd2 0 dr;ihd2 +- hd2 0 dr", "M l vc A wd2 hd2 cd2 cd4 "
     "A wd2 hd2 3cd4 cd4 A wd2 hd2 0 cd4 A wd2 hd2 cd4 cd4 Z M dr vc "
     "A iwd2 ihd2 cd2 -5400000 A iwd2 ihd2 cd4 -5400000 "
     "A iwd2 ihd2 0 -5400000 A iwd2 ihd2 3cd4 -5400000 Z", "l t r b"},
    {"frame", "adj1 val 12500", "a1 pin 0 adj1 50000;x1 */ ss a1 100000;"
     "x4 +- r 0 x1;y4 +- b 0 x1", "M l t L r t L r b L l b Z M x1 x1 "
     "L x1 y4 L x4 y4 L x4 x1 Z", "x1 x1 x4 y4"},
    {"snip1Rect", "adj val 16667", "a pin 0 adj 50000;dx1 */ ss a 100000;"
     "x1 +- r 0 dx1", "M l t L x1 t L r dx1 L r b L l b Z", "l t r b"},
    {"snip2SameRect", "adj1 val 16667;adj2 val 0", "a1 pin 0 adj1 50000;"
     "a2 pin 0 adj2 50000;tx1 */ ss a1 100000;tx2 +- r 0 tx1;"
     "bx1 */ ss a2 100000;bx2 +- r 0 bx1;by1 +- b 0 bx1",
     "M tx1 t L tx2 t L r tx1 L r by1 L bx2 b L bx1 b L l by1 L l tx1 Z",
     "l t r b"},
    {"round1Rect", "adj val 16667", "a pin 0 adj 50000;dx1 */ ss a 100000;"
     "x1 +- r 0 dx1", "M l t L x1 t A dx1 dx1 3cd4 cd4 L r b L l b Z",
     "l t r b"},
    {"round2SameRect", "adj1 val 16667;adj2 val 0", "a1 pin 0 adj1 50000;"
     "a2 pin 0 adj2 50000;tx1 */ ss a1 100000;tx2 +- r 0 tx1;"
     "bx1 */ ss a2 100000;bx2 +- r 0 bx1;by1 +- b 0 bx1",
     "M tx1 t L tx2 t A tx1 tx1 3cd4 cd4 L r by1 A bx1 bx1 0 cd4 L bx1 b "
     "A bx1 bx1 cd4 cd4 L l tx1 A tx1 tx1 cd2 cd4 Z", "l t r b"},
    {"line", "", "", "P 0 1 M l t L r b", "l t r b"},
    {"straightConnector1", "", "", "P 0 1 M l t L r b", "l t r b"},
    {"bentConnector2", "", "", "P 0 1 M l t L r t L r b", "l t r b"},
    {"bentConnector3", "adj1 val 50000", "x1 */ w adj1 100000",
     "P 0 1 M l t L x1 t L x1 b L r b", "l t r b"},
    {"curvedConnector3", "adj1 val 50000", "x2 */ w adj1 100000;"
     "x1 +/ l x2 2;x3 +/ r x2 2;y3 */ h 3 4", "P 0 1 M l t "
     "C x1 t x2 hd4 x2 vc C x2 y3 x3 b r b", "l t r b"},
    {"flowChartProcess", "", "", "M l t L r t L r b L l b Z", "l t r b"},
    {"flowChartAlternateProcess", "", "x2 +- r 0 ssd6;y2 +- b 0 ssd6",
     "M l ssd6 A ssd6 ssd6 cd2 cd4 L x2 t A ssd6 ssd6 3cd4 cd4 L r y2 "
     "A ssd6 ssd6 0 cd4 L ssd6 b A ssd6 ssd6 cd4 cd4 Z", "l t r b"},
    {"flowChartDecision", "", "x2 */ w 3 4;y2 */ h 3 4",
     "M l vc L hc t L r vc L hc b Z",
     "wd4 hd4 x2 y2"},
    {"flowChartTerminator", "", "", "W 21600 21600 M 3475 0 L 18125 0 "
     "A 3475 10800 3cd4 cd2 L 3475 21600 A 3475 10800 cd4 cd2 Z", "l t r b"},
    {"flowChartDocument", "", "", "W 21600 21600 M 0 0 L 21600 0 L 21600 "
     "17322 C 10800 17322 10800 23922 0 20172 Z", "l t r b"},
    {"flowChartData", "", "x2 */ w 1 5;x3 +- r 0 x2",
     "M x2 t L r t L x3 b L l b Z", "x2 t x3 b"},
    {"flowChartPredefinedProcess", "", "x1 */ w 1 8;x2 */ w 7 8",
     "M l t L r t L r b L l b Z P 0 1 M x1 t L x1 b M x2 t L x2 b",
     "x1 t x2 b"},
    {"flowChartManualInput", "", "", "W 5 5 M 0 1 L 5 0 L 5 5 L 0 5 Z",
     "l t r b"},
    {"flowChartConnector", "", "", "M l vc A wd2 hd2 cd2 cd4 "
     "A wd2 hd2 3cd4 cd4 A wd2 hd2 0 cd4 A wd2 hd2 cd4 cd4 Z", "l t r b"},
    {"flowChartPreparation", "", "x2 */ w 1 5;x3 +- r 0 x2",
     "M l vc L x2 t L x3 t L r vc L x3 b L x2 b Z", "x2 t x3 b"},
    {"flowChartOffpageConnector", "", "", "W 10 10 M 0 0 L 10 0 L 10 8 "
     "L 5 10 L 0 8 Z", "l t r b"},
    {"flowChartPunchedCard", "", "", "W 5 5 M 0 1 L 1 0 L 5 0 L 5 5 L 0 5 Z",
     "l t r b"},
    {"flowChartMerge", "", "x2 */ w 3 4", "M l t L r t L hc b Z",
     "wd4 t x2 vc"},
    {"flowChartExtract", "", "x2 */ w 3 4", "M l b L hc t L r b Z",
     "wd4 vc x2 b"},
    {"flowChartDelay", "", "", "M l t L hc t A wd2 hd2 3cd4 cd2 L l b Z",
     "l t r b"},
    {"flowChartMagneticDisk", "", "y1 */ h 1 6;y3 */ h 5 6",
     "M l y1 A wd2 y1 cd2 cd2 L r y3 A wd2 y1 0 cd2 Z "
     "P 0 1 M r y1 A wd2 y1 0 cd2", "l t r b"},
    {"can", "adj val 25000", "maxAdj */ 50000 h ss;a pin 0 adj maxAdj;"
     "y1 */ ss a 200000;y2 +- y1 y1 0;y3 +- b 0 y1",
     "M l y1 A wd2 y1 cd2 cd2 L r y3 A wd2 y1 0 cd2 Z "
     "P 0 1 M r y1 A wd2 y1 0 cd2", "l y2 r y3"},
    {"cube", "adj val 25000", "a pin 0 adj 100000;y1 */ ss a 100000;"
     "y4 +- b 0 y1;x4 +- r 0 y1", "M l y1 L x4 y1 L x4 b L l b Z "
     "M l y1 L y1 t L r t L x4 y1 Z M x4 b L x4 y1 L r t L r y4 Z",
     "l y1 x4 b"},
    {"heart", "", "dx1 */ w 49 48;dx2 */ w 10 48;x1 +- hc 0 dx1;"
     "x2 +- hc 0 dx2;x3 +- hc dx2 0;x4 +- hc dx1 0;y1 +- t 0 hd3",
     "M hc hd4 C x3 y1 x4 hd4 hc b C x1 hd4 x2 y1 hc hd4 Z", "l t r b"},
    {"teardrop", "adj val 100000", "a pin 0 adj 200000;r2 sqrt 2;"
     "tw */ r2 wd2 1;th */ r2 hd2 1;sw */ tw a 100000;sh */ th a 100000;"
     "dx1 cos sw 2700000;dy1 sin sh 2700000;x1 +- hc dx1 0;"
     "y1 +- vc 0 dy1;x2 +/ hc x1 2;y2 +/ vc y1 2",
     "M l vc A wd2 hd2 cd2 cd4 Q x2 t x1 y1 Q r y2 r vc "
     "A wd2 hd2 0 cd4 A wd2 hd2 cd4 cd4 Z", "l t r b"},
    {"pie", "adj1 val 0;adj2 val 16200000", "stAng pin 0 adj1 21599999;"
     "enAng pin 0 adj2 21599999;sw1 +- enAng 0 stAng;"
     "sw2 +- sw1 21600000 0;swAng ?: sw1 sw1 sw2;wt1 sin wd2 stAng;"
     "ht1 cos hd2 stAng;dx1 cat2 wd2 ht1 wt1;dy1 sat2 hd2 ht1 wt1;"
     "x1 +- hc dx1 0;y1 +- vc dy1 0", "M x1 y1 A wd2 hd2 stAng swAng "
     "L hc vc Z", "l t r b"},
    {"arc", "adj1 val 16200000;adj2 val 0", "stAng pin 0 adj1 21599999;"
     "enAng pin 0 adj2 21599999;sw1 +- enAng 0 stAng;"
     "sw2 +- sw1 21600000 0;swAng ?: sw1 sw1 sw2;wt1 sin wd2 stAng;"
     "ht1 cos hd2 stAng;dx1 cat2 wd2 ht1 wt1;dy1 sat2 hd2 ht1 wt1;"
     "x1 +- hc dx1 0;y1 +- vc dy1 0", "P 0 1 M x1 y1 A wd2 hd2 stAng swAng",
     "l t r b"},
    {"bracketPair", "adj val 16667", "a pin 0 adj 50000;x1 */ ss a 100000;"
     "x2 +- r 0 x1;y2 +- b 0 x1", "P 0 1 M x1 b A x1 x1 cd4 cd4 L l x1 "
     "A x1 x1 cd2 cd4 M x2 t A x1 x1 3cd4 cd4 L r y2 A x1 x1 0 cd4",
     "x1 x1 x2 y2"},
    {"leftBracket", "adj val 8333", "y1 */ ss adj 100000;y2 +- b 0 y1",
     "P 0 1 M r b A w y1 cd4 cd4 L l y1 A w y1 cd2 cd4", "l t r b"},
    {"rightBracket", "adj val 8333", "y1 */ ss adj 100000;y2 +- b 0 y1",
     "P 0 1 M l t A w y1 3cd4 cd4 L r y2 A w y1 0 cd4", "l t r b"},
    {"wedgeRectCallout", "adj1 val -20833;adj2 val 62500",
     "dxPos */ w adj1 100000;dyPos */ h adj2 100000;xPos +- hc dxPos 0;"
     "yPos +- vc dyPos 0;xb */ w 7 12;xa */ w 5 12;yb */ h 7 12;"
     "ya */ h 5 12", "M l t L r t L r b L l b Z M xa b L xPos yPos "
     "L xb b Z", "l t r b"},
    {"wedgeRoundRectCallout", "adj1 val -20833;adj2 val 62500;"
     "adj3 val 16667", "dxPos */ w adj1 100000;dyPos */ h adj2 100000;"
     "xPos +- hc dxPos 0;yPos +- vc dyPos 0;xb */ w 7 12;xa */ w 5 12;"
     "dx1 */ ss adj3 100000;x2 +- r 0 dx1;y2 +- b 0 dx1",
     "M l dx1 A dx1 dx1 cd2 cd4 L x2 t A dx1 dx1 3cd4 cd4 L r y2 "
     "A dx1 dx1 0 cd4 L dx1 b A dx1 dx1 cd4 cd4 Z M xa b L xPos yPos "
     "L xb b Z", "l t r b"},
    {"wedgeEllipseCallout", "adj1 val -20833;adj2 val 62500",
     "dxPos */ w adj1 100000;dyPos */ h adj2 100000;xPos +- hc dxPos 0;"
     "yPos +- vc dyPos 0;xb */ w 7 12;xa */ w 5 12;yb */ h 9 10",
     "M l vc A wd2 hd2 cd2 cd4 A wd2 hd2 3cd4 cd4 A wd2 hd2 0 cd4 "
     "A wd2 hd2 cd4 cd4 Z M xa yb L xPos yPos L xb yb Z", "l t r b"},
    {"mathMinus", "adj1 val 23520", "a1 pin 0 adj1 100000;"
     "dy1 */ h a1 200000;dx1 */ w 73490 200000;y1 +- vc 0 dy1;"
     "y2 +- vc dy1 0;x1 +- hc 0 dx1;x2 +- hc dx1 0",
     "M x1 y1 L x2 y1 L x2 y2 L x1 y2 Z", "x1 y1 x2 y2"},
    {"mathPlus", "adj1 val 23520", "a1 pin 0 adj1 73490;"
     "dx1 */ w 73490 200000;dy1 */ h 73490 200000;dx2 */ ss a1 200000;"
     "x1 +- hc 0 dx1;x2 +- hc 0 dx2;x3 +- hc dx2 0;x4 +- hc dx1 0;"
     "y1 +- vc 0 dy1;y2 +- vc 0 dx2;y3 +- vc dx2 0;y4 +- vc dy1 0",
     "M x1 y2 L x2 y2 L x2 y1 L x3 y1 L x3 y2 L x4 y2 L x4 y3 L x3 y3 "
     "L x3 y4 L x2 y4 L x2 y3 L x1 y3 Z", "x1 y2 x4 y3"},
    {"mathEqual", "adj1 val 23520;adj2 val 11760", "a1 pin 0 adj1 36745;"
     "a2 pin 0 adj2 73490;dy1 */ h a1 100000;dy2 */ h a2 200000;"
     "dx1 */ w 73490 200000;y2 +- vc 0 dy2;y3 +- vc dy2 0;"
     "y1 +- y2 0 dy1;y4 +- y3 dy1 0;x1 +- hc 0 dx1;x2 +- hc dx1 0",
     "M x1 y1 L x2 y1 L x2 y2 L x1 y2 Z M x1 y3 L x2 y3 L x2 y4 L x1 y4 Z",
     "x1 y1 x2 y4"},
    {"foldedCorner", "adj val 16667", "a pin 0 adj 50000;"
     "dy2 */ ss a 100000;dy1 */ dy2 1 5;x1 +- r 0 dy2;x2 +- x1 dy1 0;"
     "y2 +- b 0 dy2;y1 +- y2 dy1 0", "M l t L r t L r y2 L x1 b L l b Z "
     "M x1 b L x2 y1 L r y2 Z", "l t r y2"},
    {"flowChartOnlineStorage", "", "x2 */ w 5 6",
     "M wd6 t L r t A wd6 hd2 3cd4 -10800000 L wd6 b A wd6 hd2 cd4 cd2 Z",
     "wd6 t x2 b"},
};

/** @brief The preset called @p name, or NULL. */
static const preset_t *
find_preset(const char *name)
{
    size_t i;

    for (i = 0; name != NULL && i < sizeof presets / sizeof presets[0]; i++)
        if (strcmp(presets[i].name, name) == 0) {
            return &presets[i];
        }
    return NULL;
}

/** @brief The next token of @p s into @p out, @p s moved past it.
 *  @return 0 at the end. */
static int
token(const char **s, char *out, size_t max)
{
    size_t n;

    while (**s == ' ') {
        (*s)++;
    }
    n = strcspn(*s, " ");
    if (n == 0) {
        return 0;
    }
    snprintf(out, max, "%.*s", (int)n, *s);
    *s += n;
    return 1;
}

/** @brief The next @p n tokens of @p s as values, into @p v, scaled from
 *         path units to EMUs pairwise when @p scale. */
static void
args(const env_t *e, const pen_t *p, const char **s, double *v, int n,
     int scale)
{
    char tok[24];
    int i;

    for (i = 0; i < n; i++) {
        v[i] = token(s, tok, sizeof tok) ? value(e, tok, strlen(tok)) : 0;
        if (scale) {
            v[i] *= i % 2 ? p->sy : p->sx;
        }
    }
}

/** @brief A preset's path program run over the environment. */
static void
run_path(const env_t *e, pen_t *p, const char *prog, double w, double h)
{
    char op[8];
    double v[6];

    subpath(p, 1, 1);
    p->sx = p->sy = 1;
    while (token(&prog, op, sizeof op) && !p->bad) {
        if (op[0] == 'P') {
            args(e, p, &prog, v, 2, 0);
            subpath(p, v[0] != 0, v[1] != 0);
            p->sx = p->sy = 1;
        } else if (op[0] == 'W') {
            args(e, p, &prog, v, 2, 0);
            p->sx = v[0] > 0 ? w / v[0] : 1;
            p->sy = v[1] > 0 ? h / v[1] : 1;
        } else if (op[0] == 'M' || op[0] == 'L') {
            args(e, p, &prog, v, 2, 1);
            emit(p, op[0], v, 1);
        } else if (op[0] == 'A') {
            args(e, p, &prog, v, 4, 0);
            arc(p, v[0] * p->sx, v[1] * p->sy, v[2], v[3]);
        } else if (op[0] == 'Q') {
            args(e, p, &prog, v, 4, 1);
            quad(p, v[0], v[1], v[2], v[3]);
        } else if (op[0] == 'C') {
            args(e, p, &prog, v, 6, 1);
            emit(p, 'C', v, 3);
        } else if (op[0] == 'Z') {
            emit(p, 'Z', v, 0);
        }
    }
}

/** @brief A shape's own adjust values over the preset's (a:avLst's
 *         a:gd elements, "val n"). */
static void
adjusts(env_t *e, const tiku_xml_node_t *av)
{
    const tiku_xml_node_t *gd;

    for (gd = tiku_xml_child(av, "a:gd"); gd; gd = tiku_xml_next(gd,
                                                                "a:gd")) {
        const char *name = tiku_xml_attr(gd, "name");
        const char *f = tiku_xml_attr(gd, "fmla");

        if (name != NULL && f != NULL) {
            set(e, name, formula(e, f));
        }
    }
}

/** @brief The text box from four names or numbers, in points. */
static void
text_box(const env_t *e, const char *spec, float *box)
{
    char tok[24];
    int i;

    for (i = 0; i < 4; i++) {
        box[i] = token(&spec, tok, sizeof tok)
                 ? (float)(value(e, tok, strlen(tok)) / EMU) : 0;
    }
}

/* ------------------------------------------------ stars and polygons  */

/** @brief A star or a polygon of @p n points about the box's middle: the
 *         inner points at @p inner of the outer radius (1: a polygon). */
static void
star(pen_t *p, double w, double h, int n, double inner)
{
    double v[2], step = M_PI / n;
    int i;

    subpath(p, 1, 1);
    for (i = 0; i < n * 2; i++) {
        double r = i % 2 ? inner : 1, a = -M_PI / 2 + step * i;

        if (inner >= 1 && i % 2) {
            continue;
        }
        v[0] = w / 2 + w / 2 * r * cos(a);
        v[1] = h / 2 + h / 2 * r * sin(a);
        emit(p, i == 0 ? 'M' : 'L', v, 1);
    }
    emit(p, 'Z', v, 0);
}

/** @brief A star or polygon preset by its name: points, and the inner
 *         radius its adjust value gives.  @return 1 when it was one. */
static int
star_preset(pen_t *p, const char *name, const tiku_xml_node_t *av, double w,
            double h)
{
    static const struct { const char *name; int n; double adj, poly; } s[] =
    {{"star4", 4, 12500, 0}, {"star5", 5, 19098, 0}, {"star6", 6, 28868, 0},
     {"star7", 7, 34601, 0}, {"star8", 8, 37500, 0}, {"star10", 10, 42533, 0},
     {"star12", 12, 37500, 0}, {"star16", 16, 37500, 0},
     {"star24", 24, 37500, 0}, {"star32", 32, 37500, 0},
     {"pentagon", 5, 0, 1}, {"heptagon", 7, 0, 1}, {"decagon", 10, 0, 1},
     {"dodecagon", 12, 0, 1}};
    const tiku_xml_node_t *gd = tiku_xml_child(av, "a:gd");
    const char *f = tiku_xml_attr(gd, "fmla");
    size_t i;

    for (i = 0; i < sizeof s / sizeof s[0]; i++) {
        if (strcmp(s[i].name, name) == 0) {
            double adj = f != NULL && strncmp(f, "val ", 4) == 0
                         ? atof(f + 4) : s[i].adj;

            star(p, w, h, s[i].n, s[i].poly ? 1 : adj / 50000.0);
            return 1;
        }
    }
    return 0;
}

/* --------------------------------------------------------- custom  */

/** @brief One a:path of custom geometry: its commands, in its units. */
static void
cust_path(env_t *e, pen_t *p, const tiku_xml_node_t *path, double w,
          double h)
{
    const tiku_xml_node_t *c, *pt;
    const char *fill = tiku_xml_attr(path, "fill");
    const char *stroke = tiku_xml_attr(path, "stroke");
    double pw = atof(tiku_xml_attr(path, "w") ? tiku_xml_attr(path, "w")
                                              : "0");
    double ph = atof(tiku_xml_attr(path, "h") ? tiku_xml_attr(path, "h")
                                              : "0");

    subpath(p, fill == NULL || strcmp(fill, "none") != 0,
            stroke == NULL || (strcmp(stroke, "0") && strcmp(stroke,
                                                             "false")));
    p->sx = pw > 0 ? w / pw : 1;
    p->sy = ph > 0 ? h / ph : 1;
    for (c = tiku_xml_child(path, NULL); c != NULL && !p->bad;
         c = tiku_xml_next(c, NULL)) {
        double v[6];
        int n = 0;

        for (pt = tiku_xml_child(c, "a:pt"); pt && n < 3;
             pt = tiku_xml_next(pt, "a:pt"), n++) {
            const char *x = tiku_xml_attr(pt, "x"), *y = tiku_xml_attr(pt,
                                                                       "y");

            v[n * 2] = value(e, x ? x : "0", strlen(x ? x : "0")) * p->sx;
            v[n * 2 + 1] = value(e, y ? y : "0", strlen(y ? y : "0")) * p->sy;
        }
        if (tiku_xml_is(c, "a:moveTo") && n >= 1) {
            emit(p, 'M', v, 1);
        } else if (tiku_xml_is(c, "a:lnTo") && n >= 1) {
            emit(p, 'L', v, 1);
        } else if (tiku_xml_is(c, "a:cubicBezTo") && n >= 3) {
            emit(p, 'C', v, 3);
        } else if (tiku_xml_is(c, "a:quadBezTo") && n >= 2) {
            quad(p, v[0], v[1], v[2], v[3]);
        } else if (tiku_xml_is(c, "a:close")) {
            emit(p, 'Z', v, 0);
        } else if (tiku_xml_is(c, "a:arcTo")) {
            const char *a[4] = {tiku_xml_attr(c, "wR"), tiku_xml_attr(c,
                "hR"), tiku_xml_attr(c, "stAng"), tiku_xml_attr(c, "swAng")};
            int k;

            for (k = 0; k < 4; k++) {
                v[k] = a[k] ? value(e, a[k], strlen(a[k])) : 0;
            }
            arc(p, v[0] * p->sx, v[1] * p->sy, v[2], v[3]);
        }
    }
}

/** @brief Custom geometry: its guides, text box and paths. */
static void
custom(env_t *e, pen_t *p, const tiku_xml_node_t *cg, double w, double h)
{
    const tiku_xml_node_t *gd, *path, *rect = tiku_xml_child(cg, "a:rect");
    static const char *const side[4] = {"l", "t", "r", "b"};
    int k;

    adjusts(e, tiku_xml_child(cg, "a:avLst"));
    for (gd = tiku_xml_path(cg, "a:gdLst/a:gd"); gd;
         gd = tiku_xml_next(gd, "a:gd")) {
        const char *name = tiku_xml_attr(gd, "name");
        const char *f = tiku_xml_attr(gd, "fmla");

        if (name != NULL && f != NULL) {
            set(e, name, formula(e, f));
        }
    }
    for (k = 0; k < 4; k++) {
        const char *v = tiku_xml_attr(rect, side[k]);

        p->geom->text[k] = (float)((v ? value(e, v, strlen(v)) :
                                    k == 2 ? w : k == 3 ? h : 0) / EMU);
    }
    for (path = tiku_xml_path(cg, "a:pathLst/a:path"); path;
         path = tiku_xml_next(path, "a:path")) {
        cust_path(e, p, path, w, h);
    }
}

int
dml_geometry(const tiku_xml_node_t *sppr, float w, float h, dml_geom_t *g)
{
    const tiku_xml_node_t *pg = tiku_xml_child(sppr, "a:prstGeom");
    const tiku_xml_node_t *cg = tiku_xml_child(sppr, "a:custGeom");
    const char *name = tiku_xml_attr(pg, "prst");
    const preset_t *pr = find_preset(name != NULL ? name : "rect");
    double we = (double)w * EMU, he = (double)h * EMU;
    env_t *e = malloc(sizeof *e);
    pen_t p;

    memset(g, 0, sizeof *g);
    memset(&p, 0, sizeof p);
    p.geom = g;
    if (e == NULL) {
        return -1;
    }
    builtins(e, we, he);
    g->text[2] = w;
    g->text[3] = h;
    if (cg != NULL) {
        custom(e, &p, cg, we, he);
    } else if (name != NULL && star_preset(&p, name, tiku_xml_child(pg,
                                           "a:avLst"), we, he)) {
        g->text[0] = w / 4;         /* stars and polygons: the middle */
        g->text[1] = h / 4;
        g->text[2] = w * 3 / 4;
        g->text[3] = h * 3 / 4;
    } else {
        pr = pr != NULL ? pr : &presets[0];
        guides(e, pr->av);
        adjusts(e, tiku_xml_child(pg, "a:avLst"));
        guides(e, pr->gd);
        run_path(e, &p, pr->path, we, he);
        text_box(e, pr->text, g->text);
    }
    free(e);
    return p.bad ? -1 : 0;
}

void
dml_geom_free(dml_geom_t *g)
{
    int i;

    for (i = 0; i < g->nsub; i++) {
        free(g->sub[i].pt);
    }
    memset(g, 0, sizeof *g);
}
