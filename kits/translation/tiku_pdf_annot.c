/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_pdf_annot.c - annotations that store no appearance of their own,
 * drawn from what they are.
 *
 * A writer should store how each annotation looks (ISO 32000 12.5.5);
 * some store only what it is: a square and its colours, the quads a
 * highlight covers, the strokes of ink, a note's text.  From those this
 * writes the content that draws them, in page space, for the renderer
 * to run as it runs any other; the font and opacities it names are made
 * once for the document.  Under NeedAppearances, text and choice fields
 * are drawn the same way.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_pdf_private.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CONTENT_MAX (256L * 1024L)  /* bytes one annotation may make    */
#define POINTS_MAX  4096            /* numbers read from one array      */

/** @brief Content being written. */
typedef struct {
    char *p;                        /* the bytes so far                 */
    long  n, cap;
    int   failed;                   /* out of memory, or past the ceiling */
} sb_t;

/** @brief Append @p s. */
static void
put(sb_t *b, const char *s)
{
    long len = (long)strlen(s);

    if (b->failed || b->n + len + 1 > CONTENT_MAX) {
        b->failed = 1;
        return;
    }
    if (b->n + len + 1 > b->cap) {
        long cap = b->cap ? b->cap * 2 : 1024;
        char *p;

        while (cap < b->n + len + 1) {
            cap *= 2;
        }
        p = realloc(b->p, (size_t)cap);
        if (p == NULL) {
            b->failed = 1;
            return;
        }
        b->p = p;
        b->cap = cap;
    }
    memcpy(b->p + b->n, s, (size_t)len + 1);
    b->n += len;
}

/** @brief Append " v" to three places, whatever the locale's decimal
 *         point: content is read in PDF's, not the desktop's. */
static void
num(sb_t *b, double v)
{
    char s[40];
    long m;

    if (!(fabs(v) < 1e7)) {
        v = 0;
    }
    m = lround(fabs(v) * 1000);
    snprintf(s, sizeof s, " %s%ld.%03ld", v < 0 && m != 0 ? "-" : "",
             m / 1000, m % 1000);
    put(b, s);
}

/** @brief Up to @p max numbers of array @p a into @p v.  @return how many. */
static int
numbers(tiku_pdf_t *doc, pdf_obj_t *a, double *v, int max)
{
    int i, n = 0;

    for (i = 0; a != NULL && a->kind == PDF_ARRAY && i < a->u.a.n &&
                n < max; i++) {
        v[n++] = pdf_number(pdf_resolve(doc, a->u.a.item[i]), 0);
    }
    return n;
}

/* ----------------------------------------------------------- the pen  */

/** @brief Colour array @p c as a fill or, with @p stroke, a stroke
 *         operator.  @return 0 when it names none (absent or empty). */
static int
colour(tiku_pdf_t *doc, sb_t *b, pdf_obj_t *c, int stroke)
{
    double v[4];
    int n = numbers(doc, c, v, 4), i;

    if (n != 1 && n != 3 && n != 4) {
        return 0;
    }
    for (i = 0; i < n; i++) {
        num(b, v[i]);
    }
    put(b, n == 1 ? (stroke ? " G" : " g") : n == 3 ? (stroke ? " RG" : " rg")
                                                    : (stroke ? " K" : " k"));
    return 1;
}

/** @brief The border's width: BS's W, else Border's third number; 1 when
 *         neither says. */
static double
border(tiku_pdf_t *doc, pdf_obj_t *a)
{
    pdf_obj_t *w = pdf_get(doc, pdf_get(doc, a, "BS"), "W");
    double v[3];

    if (w != NULL) {
        return pdf_number(w, 1);
    }
    return numbers(doc, pdf_get(doc, a, "Border"), v, 3) == 3 ? v[2] : 1;
}

/** @brief The line width @p w, and a dash when the border is dashed. */
static void
pen(tiku_pdf_t *doc, sb_t *b, pdf_obj_t *a, double w)
{
    pdf_obj_t *bs = pdf_get(doc, a, "BS");
    double d[8];
    int n, i;

    num(b, w);
    put(b, " w");
    if (pdf_is_name(pdf_get(doc, bs, "S"), "D")) {
        n = numbers(doc, pdf_get(doc, bs, "D"), d, 8);
        put(b, " [");
        for (i = 0; i < n; i++) {
            num(b, d[i]);
        }
        put(b, n > 0 ? " ] 0 d" : " 3 ] 0 d");
    }
}

/** @brief The annotation's opacity CA as a state named in the resources,
 *         multiplying with @p multiply, as a highlighter's ink does. */
static void
opacity(tiku_pdf_t *doc, sb_t *b, pdf_obj_t *a, int multiply)
{
    double ca = pdf_number(pdf_get(doc, a, "CA"), 1);
    int k = ca > 0 ? (ca < 1 ? (int)(ca * 20 + 0.5) : 20) : 0;
    char s[16];

    if (k < 20 || multiply) {
        snprintf(s, sizeof s, " /%c%d gs", multiply ? 'M' : 'A', k);
        put(b, s);
    }
}

/** @brief An ellipse in the box from (@p x0, @p y0) to (@p x1, @p y1),
 *         four Bézier quarters. */
static void
ellipse(sb_t *b, double x0, double y0, double x1, double y1)
{
    double cx = (x0 + x1) / 2, cy = (y0 + y1) / 2;
    double rx = (x1 - x0) / 2, ry = (y1 - y0) / 2, k = 0.5523;
    double v[26] = {
        cx + rx, cy,
        cx + rx, cy + ry * k, cx + rx * k, cy + ry, cx, cy + ry,
        cx - rx * k, cy + ry, cx - rx, cy + ry * k, cx - rx, cy,
        cx - rx, cy - ry * k, cx - rx * k, cy - ry, cx, cy - ry,
        cx + rx * k, cy - ry, cx + rx, cy - ry * k, cx + rx, cy
    };
    int i, c;

    num(b, v[0]);
    num(b, v[1]);
    put(b, " m");
    for (c = 0; c < 4; c++) {
        for (i = 0; i < 6; i++) {
            num(b, v[2 + c * 6 + i]);
        }
        put(b, " c");
    }
    put(b, " h");
}

/* ------------------------------------------------------------ shapes  */

/** @brief Square or, with @p round, Circle: the box inset by RD and half
 *         the border, filled with IC and stroked with C. */
static void
shape(tiku_pdf_t *doc, sb_t *b, pdf_obj_t *a, const double *r, int round)
{
    double bw = border(doc, a), rd[4] = {0, 0, 0, 0}, x0, y0, x1, y1;
    int fill = colour(doc, b, pdf_get(doc, a, "IC"), 0);
    int stroke = bw > 0 && colour(doc, b, pdf_get(doc, a, "C"), 1);

    (void)numbers(doc, pdf_get(doc, a, "RD"), rd, 4);
    x0 = r[0] + rd[0] + bw / 2;
    y0 = r[1] + rd[1] + bw / 2;
    x1 = r[2] - rd[2] - bw / 2;
    y1 = r[3] - rd[3] - bw / 2;
    if ((!fill && !stroke) || x1 <= x0 || y1 <= y0) {
        return;
    }
    pen(doc, b, a, bw);
    if (round) {
        ellipse(b, x0, y0, x1, y1);
    } else {
        num(b, x0);
        num(b, y0);
        num(b, x1 - x0);
        num(b, y1 - y0);
        put(b, " re");
    }
    put(b, fill && stroke ? " B" : fill ? " f" : " S");
}

/** @brief A line ending (Table 176) at (@p x, @p y), the line arriving
 *         along the unit (@p ux, @p uy), @p s across; closed ones filled
 *         with IC when @p fill. */
static void
ending(sb_t *b, const char *name, double x, double y, double ux, double uy,
       double s, int fill)
{
    double c = cos(M_PI / 6), sn = sin(M_PI / 6), px = -uy, py = ux;
    int back = name[0] == 'R', closed = strstr(name, "Closed") != NULL;

    if (strstr(name, "Arrow") != NULL) {
        double dx = back ? ux : -ux, dy = back ? uy : -uy;

        num(b, x + s * (dx * c - dy * sn));
        num(b, y + s * (dx * sn + dy * c));
        put(b, " m");
        num(b, x);
        num(b, y);
        put(b, " l");
        num(b, x + s * (dx * c + dy * sn));
        num(b, y + s * (-dx * sn + dy * c));
        put(b, closed ? " l h" : " l S");
    } else if (!strcmp(name, "Circle")) {
        ellipse(b, x - s / 2, y - s / 2, x + s / 2, y + s / 2);
        closed = 1;
    } else if (!strcmp(name, "Square") || !strcmp(name, "Diamond")) {
        static const signed char sq[8] = {1, 1, -1, 1, -1, -1, 1, -1};
        static const signed char di[8] = {1, 0, 0, 1, -1, 0, 0, -1};
        const signed char *k = name[0] == 'S' ? sq : di;
        int i;

        for (i = 0; i < 8; i += 2) {
            num(b, x + k[i] * s / 2);
            num(b, y + k[i + 1] * s / 2);
            put(b, i == 0 ? " m" : " l");
        }
        put(b, " h");
        closed = 1;
    } else if (!strcmp(name, "Butt") || !strcmp(name, "Slash")) {
        double sx = !strcmp(name, "Slash") ? (px * c + ux * sn) : px;
        double sy = !strcmp(name, "Slash") ? (py * c + uy * sn) : py;

        num(b, x + sx * s / 2);
        num(b, y + sy * s / 2);
        put(b, " m");
        num(b, x - sx * s / 2);
        num(b, y - sy * s / 2);
        put(b, " l S");
    }
    if (closed) {
        put(b, fill ? " B" : " S");
    }
}

/** @brief Line: L's two points stroked with C, its endings LE after. */
static void
line(tiku_pdf_t *doc, sb_t *b, pdf_obj_t *a)
{
    double l[4], bw = border(doc, a), len, ux, uy, s = 3 * bw + 6;
    pdf_obj_t *le = pdf_get(doc, a, "LE");
    int fill, k;

    if (numbers(doc, pdf_get(doc, a, "L"), l, 4) != 4 || bw <= 0 ||
        !colour(doc, b, pdf_get(doc, a, "C"), 1)) {
        return;
    }
    fill = colour(doc, b, pdf_get(doc, a, "IC"), 0);
    pen(doc, b, a, bw);
    num(b, l[0]);
    num(b, l[1]);
    put(b, " m");
    num(b, l[2]);
    num(b, l[3]);
    put(b, " l S");
    len = hypot(l[2] - l[0], l[3] - l[1]);
    if (le == NULL || le->kind != PDF_ARRAY || le->u.a.n < 2 || len <= 0) {
        return;
    }
    ux = (l[2] - l[0]) / len;
    uy = (l[3] - l[1]) / len;
    put(b, " [] 0 d");
    for (k = 0; k < 2; k++) {
        pdf_obj_t *e = pdf_resolve(doc, le->u.a.item[k]);

        if (e != NULL && e->kind == PDF_NAME) {
            ending(b, (const char *)e->u.s.p, l[2 * k], l[2 * k + 1],
                   k ? ux : -ux, k ? uy : -uy, s, fill);
        }
    }
}

/** @brief Points @p v (@p n numbers) as one path, joined, and closed
 *         with @p close. */
static void
path(sb_t *b, const double *v, int n, int close)
{
    int i;

    for (i = 0; i + 1 < n; i += 2) {
        num(b, v[i]);
        num(b, v[i + 1]);
        put(b, i == 0 ? " m" : " l");
    }
    if (n == 2) {
        num(b, v[0]);
        num(b, v[1]);
        put(b, " l");               /* a dot, which a round cap draws */
    }
    if (close) {
        put(b, " h");
    }
}

/** @brief Polygon (closed, filled with IC) or PolyLine: Vertices stroked
 *         with C. */
static void
poly(tiku_pdf_t *doc, sb_t *b, pdf_obj_t *a, int closed)
{
    double *v = malloc(POINTS_MAX * sizeof *v), bw = border(doc, a);
    int n = v != NULL ? numbers(doc, pdf_get(doc, a, "Vertices"), v,
                                POINTS_MAX) & ~1 : 0;
    int fill = closed && colour(doc, b, pdf_get(doc, a, "IC"), 0);
    int stroke = bw > 0 && colour(doc, b, pdf_get(doc, a, "C"), 1);

    if (n >= 4 && (fill || stroke)) {
        pen(doc, b, a, bw);
        put(b, " 1 j");
        path(b, v, n, closed);
        put(b, fill && stroke ? " B" : fill ? " f" : " S");
    }
    free(v);
}

/** @brief Ink: each of InkList's strokes, round-capped, in C. */
static void
ink(tiku_pdf_t *doc, sb_t *b, pdf_obj_t *a)
{
    pdf_obj_t *list = pdf_get(doc, a, "InkList");
    double *v = malloc(POINTS_MAX * sizeof *v), bw = border(doc, a);
    int i, n;

    if (v != NULL && list != NULL && list->kind == PDF_ARRAY && bw > 0 &&
        colour(doc, b, pdf_get(doc, a, "C"), 1)) {
        pen(doc, b, a, bw);
        put(b, " 1 J 1 j");
        for (i = 0; i < list->u.a.n; i++) {
            n = numbers(doc, pdf_resolve(doc, list->u.a.item[i]), v,
                        POINTS_MAX) & ~1;
            if (n >= 2) {
                path(b, v, n, 0);
                put(b, " S");
            }
        }
    }
    free(v);
}

/* ------------------------------------------------------ text markup  */

/** @brief Whether segments p-q and r-s cross. */
static int
crosses(const double *p, const double *q, const double *r, const double *s)
{
    double d1 = (q[0] - p[0]) * (r[1] - p[1]) - (q[1] - p[1]) * (r[0] - p[0]);
    double d2 = (q[0] - p[0]) * (s[1] - p[1]) - (q[1] - p[1]) * (s[0] - p[0]);
    double d3 = (s[0] - r[0]) * (p[1] - r[1]) - (s[1] - r[1]) * (p[0] - r[0]);
    double d4 = (s[0] - r[0]) * (q[1] - r[1]) - (s[1] - r[1]) * (q[0] - r[0]);

    return ((d1 > 0 && d2 < 0) || (d1 < 0 && d2 > 0)) &&
           ((d3 > 0 && d4 < 0) || (d3 < 0 && d4 > 0));
}

/**
 * @brief One quad of QuadPoints as upper left, upper right, lower left,
 *        lower right.  A quad written round its edge, lower left, lower
 *        right, upper right, upper left, is put in that order.
 */
static void
quad(const double *in, double *q)
{
    memcpy(q, in, 8 * sizeof *q);
    if (crosses(q + 2, q + 6, q + 4, q)) {
        double t[8];

        memcpy(t, in, sizeof t);    /* round: LL LR UR UL */
        q[0] = t[6]; q[1] = t[7];
        q[2] = t[4]; q[3] = t[5];
        q[4] = t[0]; q[5] = t[1];
        q[6] = t[2]; q[7] = t[3];
    }
}

/** @brief The mark along the quad at height @p at (0 its foot, 1 its
 *         head), @p w thick; wavy when @p wave. */
static void
mark(sb_t *b, const double *q, double at, double w, int wave)
{
    double x0 = q[4] + (q[0] - q[4]) * at, y0 = q[5] + (q[1] - q[5]) * at;
    double x1 = q[6] + (q[2] - q[6]) * at, y1 = q[7] + (q[3] - q[7]) * at;
    double len = hypot(x1 - x0, y1 - y0), step = w * 4, t;
    double nx = len > 0 ? -(y1 - y0) / len : 0, ny = len > 0 ?
                                                   (x1 - x0) / len : 1;

    if (!(len < 1e5)) {
        return;                     /* longer than any page, or a NaN */
    }
    step = len / step > 2048 ? len / 2048 : step;

    num(b, w);
    put(b, " w");
    num(b, x0);
    num(b, y0);
    put(b, " m");
    for (t = step; wave && t < len && !b->failed; t += step) {
        double f = t / len, up = ((int)(t / step) % 2) ? w * 1.5 : 0;

        num(b, x0 + (x1 - x0) * f + nx * up);
        num(b, y0 + (y1 - y0) * f + ny * up);
        put(b, " l");
    }
    num(b, x1);
    num(b, y1);
    put(b, " l S");
}

/** @brief Highlight, Underline, StrikeOut or Squiggly over each quad of
 *         QuadPoints, in C (a highlighter's yellow when it has none). */
static void
markup(tiku_pdf_t *doc, sb_t *b, pdf_obj_t *a, const char *kind)
{
    double *v = malloc(POINTS_MAX * sizeof *v), q[8];
    int n = v != NULL ? numbers(doc, pdf_get(doc, a, "QuadPoints"), v,
                                POINTS_MAX) : 0, i;
    int hl = !strcmp(kind, "Highlight");

    opacity(doc, b, a, hl);
    if (!colour(doc, b, pdf_get(doc, a, "C"), !hl)) {
        put(b, hl ? " 1 1 0 rg" : " 0 0 0 RG");
    }
    for (i = 0; i + 8 <= n; i += 8) {
        double h;

        quad(v + i, q);
        h = hypot(q[0] - q[4], q[1] - q[5]);
        if (hl) {
            path(b, (const double[]){q[0], q[1], q[2], q[3], q[6], q[7],
                                     q[4], q[5]}, 8, 1);
            put(b, " f");
        } else {
            mark(b, q, !strcmp(kind, "StrikeOut") ? 0.5 : 0.08,
                 h / 16 > 0.5 ? h / 16 : 0.5, !strcmp(kind, "Squiggly"));
        }
    }
    free(v);
}

/* ------------------------------------------------------------- words  */

/** @brief Helvetica's advance widths for ASCII 32 to 126, thousandths
 *         of an em (its AFM); the rest of Latin-1 is taken as 556. */
static const unsigned short helv[95] = {
    278, 278, 355, 556, 556, 889, 667, 191, 333, 333, 389, 584, 278, 333,
    278, 278, 556, 556, 556, 556, 556, 556, 556, 556, 556, 556, 278, 278,
    584, 584, 584, 556, 1015, 667, 667, 722, 722, 667, 611, 778, 722, 278,
    500, 667, 556, 833, 722, 778, 667, 778, 722, 667, 611, 722, 667, 944,
    667, 667, 611, 278, 278, 278, 469, 556, 333, 556, 556, 500, 556, 556,
    278, 556, 556, 222, 222, 500, 222, 833, 556, 556, 556, 556, 333, 500,
    278, 556, 500, 722, 500, 500, 500, 334, 260, 334, 584
};

/** @brief @p s (UTF-8) as the Latin-1 bytes Helvetica shows, "?" for the
 *         rest; @return how many, into @p out of @p max. */
static int
latin1(const char *s, unsigned char *out, int max)
{
    int n = 0;

    while (*s != '\0' && n < max) {
        unsigned char c = (unsigned char)*s++;
        unsigned cp = c;

        if (c >= 0xc0 && c < 0xe0 && (*s & 0xc0) == 0x80) {
            cp = (c & 0x1fu) << 6 | ((unsigned char)*s++ & 0x3fu);
        } else if (c >= 0x80) {
            while ((*s & 0xc0) == 0x80) {
                s++;
            }
            cp = '?';
        }
        out[n++] = (unsigned char)(cp < 256 ? cp : '?');
    }
    return n;
}

/** @brief Byte @p c's width in Helvetica at size @p size. */
static double
width_of(unsigned char c, double size)
{
    return (c >= 32 && c < 127 ? helv[c - 32] : 556) * size / 1000;
}

/** @brief @p n bytes shown as a string, the ones PDF escapes escaped. */
static void
show(sb_t *b, const unsigned char *s, int n)
{
    char one[5];
    int i;

    put(b, " (");
    for (i = 0; i < n; i++) {
        if (s[i] == '(' || s[i] == ')' || s[i] == '\\') {
            snprintf(one, sizeof one, "\\%c", s[i]);
        } else if (s[i] < 32 || s[i] >= 127) {
            snprintf(one, sizeof one, "\\%03o", s[i]);
        } else {
            snprintf(one, sizeof one, "%c", s[i]);
        }
        put(b, one);
    }
    put(b, ") Tj");
}

/** @brief Where the line from @p start ends: at a line break, else at
 *         the last space before the words outgrow @p room, else within a
 *         word that alone does.  At least one byte past @p start unless a
 *         line break is there. */
static int
line_end(const unsigned char *s, int n, int start, double room, double size)
{
    double used = 0;
    int i, cut = -1;

    for (i = start; i < n; i++) {
        if (s[i] == '\n' || s[i] == '\r') {
            return i;
        }
        used += width_of(s[i], size);
        cut = s[i] == ' ' ? i : cut;
        if (used > room && i > start) {
            return cut > start ? cut : i;
        }
    }
    return n;
}

/** @brief Text @p s set in the box, Helvetica at @p size, a line at a
 *         time down from the top, as far as the box goes. */
static void
words(sb_t *b, const unsigned char *s, int n, const double *r, double size)
{
    double room = r[2] - r[0] - 4, y = r[3] - 2 - size;
    int start = 0;

    put(b, " BT /Helv");
    num(b, size);
    put(b, " Tf");
    while (start < n && y >= r[1] - size / 4) {
        int end = line_end(s, n, start, room, size), next = end;

        if (end < n && (s[end] == ' ' || s[end] == '\n' || s[end] == '\r')) {
            next = end + 1 + (s[end] == '\r' && end + 1 < n &&
                              s[end + 1] == '\n');
        }
        put(b, " 1 0 0 1");
        num(b, r[0] + 2);
        num(b, y);
        put(b, " Tm");
        show(b, s + start, end - start);
        start = next;
        y -= size * 1.15;
    }
    put(b, " ET");
}

/** @brief DA's size, and its colour for the text and the border alike:
 *         "/Helv 12 Tf 0 0 1 rg".  @return the size. */
static double
default_look(sb_t *b, pdf_obj_t *da)
{
    char *s = da != NULL && da->kind == PDF_STRING ? pdf_text_string(da)
                                                   : NULL;
    double size = 12, v[4];
    int nv = 0;
    char *tok, *save = NULL;

    for (tok = s != NULL ? strtok_r(s, " \t\r\n", &save) : NULL;
         tok != NULL; tok = strtok_r(NULL, " \t\r\n", &save)) {
        if (!strcmp(tok, "Tf") && nv > 0) {
            size = v[nv - 1] > 0 ? v[nv - 1] : 12;
        } else if ((!strcmp(tok, "g") && nv >= 1) ||
                   (!strcmp(tok, "rg") && nv >= 3) ||
                   (!strcmp(tok, "k") && nv >= 4)) {
            int k = tok[0] == 'g' ? 1 : tok[0] == 'r' ? 3 : 4, i;

            for (i = nv - k; i < nv; i++) {
                num(b, v[i]);
            }
            put(b, " ");
            put(b, tok);
            for (i = nv - k; i < nv; i++) {
                num(b, v[i]);
            }
            put(b, k == 1 ? " G" : k == 3 ? " RG" : " K");
        } else if ((tok[0] >= '0' && tok[0] <= '9') || tok[0] == '.' ||
                   tok[0] == '-') {
            v[nv < 4 ? nv++ : 3] = strtod(tok, NULL);
            continue;
        }
        nv = 0;
    }
    free(s);
    return size > 72 ? 72 : size;
}

/** @brief FreeText: C as its ground, its border (a point wide unless BS
 *         or Border says), then its Contents, as DA says. */
static void
free_text(tiku_pdf_t *doc, sb_t *b, pdf_obj_t *a, const double *r)
{
    char *text = pdf_text_string(pdf_get(doc, a, "Contents"));
    unsigned char bytes[4096];
    int n = text != NULL ? latin1(text, bytes, (int)sizeof bytes) : 0;
    double bw = border(doc, a), size;

    if (colour(doc, b, pdf_get(doc, a, "C"), 0)) {
        num(b, r[0]);
        num(b, r[1]);
        num(b, r[2] - r[0]);
        num(b, r[3] - r[1]);
        put(b, " re f");
    }
    put(b, " 0 g 0 G");
    size = default_look(b, pdf_get(doc, a, "DA"));
    if (bw > 0) {
        pen(doc, b, a, bw);
        num(b, r[0] + bw / 2);
        num(b, r[1] + bw / 2);
        num(b, r[2] - r[0] - bw);
        num(b, r[3] - r[1] - bw);
        put(b, " re S");
    }
    words(b, bytes, n, r, size);
    free(text);
}

/** @brief Text: a note's icon at the box's top left, in C (yellow when
 *         it has none), ruled like a page. */
static void
note(tiku_pdf_t *doc, sb_t *b, pdf_obj_t *a, const double *r)
{
    double s = r[2] - r[0] < r[3] - r[1] ? r[2] - r[0] : r[3] - r[1];
    double x = r[0], y, k;

    s = s > 24 ? 24 : s;
    y = r[3] - s;
    put(b, " 0 G 0.6 w");
    if (!colour(doc, b, pdf_get(doc, a, "C"), 0)) {
        put(b, " 1 0.9 0.3 rg");
    }
    num(b, x + 0.3);
    num(b, y + 0.3);
    num(b, s - 0.6);
    num(b, s - 0.6);
    put(b, " re B");
    for (k = 0.3; k < 0.8; k += 0.2) {
        num(b, x + s * 0.2);
        num(b, y + s * (1 - k));
        put(b, " m");
        num(b, x + s * 0.8);
        num(b, y + s * (1 - k));
        put(b, " l S");
    }
}

/** @brief Stamp: its Name (Draft when none) in capitals, framed in red,
 *         as large as the box allows. */
static void
stamp(tiku_pdf_t *doc, sb_t *b, pdf_obj_t *a, const double *r)
{
    pdf_obj_t *name = pdf_get(doc, a, "Name");
    unsigned char word[64];
    int n = 0, i;
    double w = r[2] - r[0], h = r[3] - r[1], size, used = 0;

    if (name != NULL && name->kind == PDF_NAME) {
        const char *p = (const char *)name->u.s.p;

        while (p[n] != '\0' && n < (int)sizeof word) {
            word[n] = (unsigned char)(p[n] >= 'a' && p[n] <= 'z' ? p[n] - 32
                                                                 : p[n]);
            n++;
        }
    } else {
        memcpy(word, "DRAFT", 5);
        n = 5;
    }
    for (i = 0; i < n; i++) {
        used += width_of(word[i], 1);
    }
    size = used > 0 ? (w - 8) / used : h / 2;
    size = size > h * 0.6 ? h * 0.6 : size;
    put(b, " 0.8 0 0 RG 0.8 0 0 rg 2 w");
    num(b, r[0] + 1);
    num(b, r[1] + 1);
    num(b, w - 2);
    num(b, h - 2);
    put(b, " re S BT /Helv");
    num(b, size);
    put(b, " Tf 1 0 0 1");
    num(b, r[0] + 4);
    num(b, r[1] + (h - size * 0.7) / 2);
    put(b, " Tm");
    show(b, word, n);
    put(b, " ET");
}

/* -------------------------------------------------------------- forms  */

enum {
    FF_MULTILINE = 1 << 12, FF_PASSWORD = 1 << 13, FF_RADIO = 1 << 15,
    FF_PUSH = 1 << 16
};

/** @brief @p key of widget @p a or of the nearest field above it that
 *         has it (ISO 32000 12.7.3.1), else the form's own. */
static pdf_obj_t *
field_key(tiku_pdf_t *doc, pdf_obj_t *a, const char *key)
{
    int depth;

    for (depth = 0; a != NULL && depth < 32; depth++) {
        pdf_obj_t *v = pdf_get(doc, a, key);

        if (v != NULL) {
            return v;
        }
        a = pdf_get(doc, a, "Parent");
    }
    return pdf_get(doc, pdf_get(doc, pdf_root(doc), "AcroForm"), key);
}

/** @brief @p n bytes on one line, centred on the box's height and set
 *         left, centre or right as @p q is 0, 1 or 2; @p size 0 or too
 *         tall for the box fits it instead. */
static void
one_line(sb_t *b, const unsigned char *s, int n, const double *r,
         double size, int q)
{
    double room = r[2] - r[0] - 4, used = 0;
    int i;

    if (size <= 0 || size > (r[3] - r[1]) * 0.8) {
        size = (r[3] - r[1]) * 0.66;
    }
    for (i = 0; i < n; i++) {
        used += width_of(s[i], size);
    }
    put(b, " BT /Helv");
    num(b, size);
    put(b, " Tf 1 0 0 1");
    num(b, r[0] + 2 + (used < room ? (room - used) * (q == 1 ? 0.5 : q == 2)
                                   : 0));
    num(b, r[1] + (r[3] - r[1] - size * 0.72) / 2);
    put(b, " Tm");
    show(b, s, n);
    put(b, " ET");
}

/** @brief A text or choice field's value, set in the box: on one line
 *         as Q places it, or wrapped when multiline; asterisks for a
 *         password. */
static void
field_text(tiku_pdf_t *doc, sb_t *b, pdf_obj_t *a, const double *r, long ff)
{
    pdf_obj_t *v = field_key(doc, a, "V");
    char *text = NULL;
    unsigned char bytes[2048];
    int n = 0, i;
    double size;

    if (v != NULL && v->kind == PDF_ARRAY && v->u.a.n > 0) {
        v = pdf_resolve(doc, v->u.a.item[0]);   /* a list's first pick */
    }
    text = v != NULL && v->kind == PDF_STRING ? pdf_text_string(v) : NULL;
    n = text != NULL ? latin1(text, bytes, (int)sizeof bytes) : 0;
    free(text);
    for (i = 0; (ff & FF_PASSWORD) && i < n; i++) {
        bytes[i] = '*';
    }
    size = default_look(b, field_key(doc, a, "DA"));
    if (ff & FF_MULTILINE) {
        words(b, bytes, n, r, size > 0 ? size : 12);
    } else {
        one_line(b, bytes, n, r, size, pdf_int(field_key(doc, a, "Q"), 0));
    }
}

/** @brief A button's face: a check box's tick or a radio button's dot
 *         when it is on, a push button's caption from MK. */
static void
field_button(tiku_pdf_t *doc, sb_t *b, pdf_obj_t *a, const double *r,
             long ff)
{
    pdf_obj_t *as = pdf_get(doc, a, "AS"), *v = field_key(doc, a, "V");
    pdf_obj_t *state = as != NULL ? as : v;
    double w = r[2] - r[0], h = r[3] - r[1], s = w < h ? w : h;
    double size = default_look(b, field_key(doc, a, "DA"));

    if (ff & FF_PUSH) {
        pdf_obj_t *ca = pdf_get(doc, pdf_get(doc, a, "MK"), "CA");
        char *text = ca != NULL && ca->kind == PDF_STRING
                     ? pdf_text_string(ca) : NULL;
        unsigned char bytes[256];
        int n = text != NULL ? latin1(text, bytes, (int)sizeof bytes) : 0;

        free(text);
        one_line(b, bytes, n, r, size, 1);
        return;
    }
    if (state == NULL || state->kind != PDF_NAME ||
        pdf_is_name(state, "Off")) {
        return;                     /* off: the ground and border alone */
    }
    if (ff & FF_RADIO) {
        ellipse(b, r[0] + w / 2 - s / 4, r[1] + h / 2 - s / 4,
                r[0] + w / 2 + s / 4, r[1] + h / 2 + s / 4);
        put(b, " f");
        return;
    }
    num(b, s / 8);
    put(b, " w 1 J 1 j");
    path(b, (const double[]){r[0] + w / 2 - s * 0.3, r[1] + h / 2,
                             r[0] + w / 2 - s * 0.08, r[1] + h / 2 - s * 0.25,
                             r[0] + w / 2 + s * 0.3, r[1] + h / 2 + s * 0.28},
         6, 0);
    put(b, " S");
}

/** @brief A form field's widget: MK's ground and border, then its value
 *         as its field's type shows one. */
static void
widget(tiku_pdf_t *doc, sb_t *b, pdf_obj_t *a, const double *r)
{
    pdf_obj_t *ft = field_key(doc, a, "FT"), *mk = pdf_get(doc, a, "MK");
    long ff = pdf_int(field_key(doc, a, "Ff"), 0);
    double bw = border(doc, a);

    if (colour(doc, b, pdf_get(doc, mk, "BG"), 0)) {
        num(b, r[0]);
        num(b, r[1]);
        num(b, r[2] - r[0]);
        num(b, r[3] - r[1]);
        put(b, " re f");
    }
    if (bw > 0 && colour(doc, b, pdf_get(doc, mk, "BC"), 1)) {
        pen(doc, b, a, bw);
        num(b, r[0] + bw / 2);
        num(b, r[1] + bw / 2);
        num(b, r[2] - r[0] - bw);
        num(b, r[3] - r[1] - bw);
        put(b, " re S");
    }
    put(b, " 0 g 0 G");
    if (pdf_is_name(ft, "Tx") || pdf_is_name(ft, "Ch")) {
        field_text(doc, b, a, r, ff);
    } else if (pdf_is_name(ft, "Btn")) {
        field_button(doc, b, a, r, ff);
    }
}

/* --------------------------------------------------------------- API  */

int
pdf_annot_redraw(tiku_pdf_t *doc, pdf_obj_t *a)
{
    pdf_obj_t *need = pdf_get(doc, pdf_get(doc, pdf_root(doc), "AcroForm"),
                              "NeedAppearances");
    pdf_obj_t *ft;

    if (need == NULL || need->kind != PDF_BOOL || !need->u.b ||
        !pdf_is_name(pdf_get(doc, a, "Subtype"), "Widget")) {
        return 0;
    }
    ft = field_key(doc, a, "FT");
    return pdf_is_name(ft, "Tx") || pdf_is_name(ft, "Ch");
}

pdf_obj_t *
pdf_annot_resources(tiku_pdf_t *doc)
{
    char src[3000];
    pdf_cur_t c;
    int k, at;

    if (doc->annot_res != NULL) {
        return doc->annot_res;
    }
    at = snprintf(src, sizeof src, "<< /Font << /Helv << /Type /Font "
                  "/Subtype /Type1 /BaseFont /Helvetica /Encoding "
                  "/WinAnsiEncoding >> >> /ExtGState <<");
    for (k = 0; k <= 20 && at < (int)sizeof src - 80; k++) {
        at += snprintf(src + at, sizeof src - (size_t)at,
                       " /A%d << /CA %d.%02d /ca %d.%02d >> /M%d << /BM "
                       "/Multiply /CA %d.%02d /ca %d.%02d >>", k,
                       k / 20, k % 20 * 5, k / 20, k % 20 * 5, k,
                       k / 20, k % 20 * 5, k / 20, k % 20 * 5);
    }
    snprintf(src + at, sizeof src - (size_t)at, " >> >>");
    c.p = (const unsigned char *)src;
    c.n = (long)strlen(src);
    c.pos = 0;
    doc->annot_res = pdf_parse(&c, 0);
    return doc->annot_res;
}

unsigned char *
pdf_annot_content(tiku_pdf_t *doc, pdf_obj_t *a, long *n)
{
    pdf_obj_t *st = pdf_get(doc, a, "Subtype");
    const char *kind = st != NULL && st->kind == PDF_NAME
                       ? (const char *)st->u.s.p : "";
    double r[4], v[4];
    sb_t b = {0};

    if (numbers(doc, pdf_get(doc, a, "Rect"), v, 4) != 4) {
        return NULL;
    }
    r[0] = v[0] < v[2] ? v[0] : v[2];
    r[1] = v[1] < v[3] ? v[1] : v[3];
    r[2] = v[0] < v[2] ? v[2] : v[0];
    r[3] = v[1] < v[3] ? v[3] : v[1];
    put(&b, "q");
    if (strcmp(kind, "Highlight") != 0) {
        opacity(doc, &b, a, 0);
    }
    if (!strcmp(kind, "Square") || !strcmp(kind, "Circle")) {
        shape(doc, &b, a, r, kind[0] == 'C');
    } else if (!strcmp(kind, "Line")) {
        line(doc, &b, a);
    } else if (!strcmp(kind, "Polygon") || !strcmp(kind, "PolyLine")) {
        poly(doc, &b, a, kind[4] == 'g');
    } else if (!strcmp(kind, "Ink")) {
        ink(doc, &b, a);
    } else if (!strcmp(kind, "Highlight") || !strcmp(kind, "Underline") ||
               !strcmp(kind, "StrikeOut") || !strcmp(kind, "Squiggly")) {
        markup(doc, &b, a, kind);
    } else if (!strcmp(kind, "FreeText")) {
        free_text(doc, &b, a, r);
    } else if (!strcmp(kind, "Text")) {
        note(doc, &b, a, r);
    } else if (!strcmp(kind, "Stamp")) {
        stamp(doc, &b, a, r);
    } else if (!strcmp(kind, "Widget")) {
        widget(doc, &b, a, r);
    } else {
        free(b.p);
        return NULL;                /* links and the rest draw nothing */
    }
    put(&b, " Q");
    if (b.failed) {
        free(b.p);
        return NULL;
    }
    *n = b.n;
    return (unsigned char *)b.p;
}
