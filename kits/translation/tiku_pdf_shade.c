/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_pdf_shade.c - smooth shadings: a colour for every point, from a
 * function over the plane, along an axis, or between two circles.
 *
 * ISO 32000 8.7.4.5: types 1 to 3 are drawn exactly, per pixel, the
 * axial and radial ones through a table of colours along their axis.
 * The meshes -- triangles free or in a lattice (4, 5), Coons and
 * tensor-product patches (6, 7) -- are read into triangles whose
 * corners carry colours, and filled blending between them (Gouraud).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_pdf_private.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define SHADE_LUT  1024             /* colours along an axis or a radius */
#define MESH_MAX   (1L << 20)       /* triangles one mesh may make       */
#define MESH_WORK  (1L << 26)       /* pixels one mesh may visit to draw  */
#define PATCH_GRID 12               /* a patch's steps each way          */
#define NC         8                /* colour values a vertex carries    */

struct pdf_shade {
    int         type;
    pdf_cs_t   *cs;
    pdf_func_t *fn;                 /* NULL: mesh vertices carry colours  */
    double      coords[6];          /* axis ends, or circle centres, radii */
    double      domain[4];          /* t0 t1, or type 1's x0 x1 y0 y1     */
    int         extend[2];
    pdf_mat_t   matrix;             /* type 1: its domain into the space  */
    double      bbox[4];
    int         has_bbox, has_bg;
    uint32_t    bg;
    uint32_t    flat;               /* a mesh's one tone                  */
    uint32_t    lut[SHADE_LUT];     /* types 2 and 3: colour along t     */
    int         nc;                 /* a mesh vertex's colour values      */
    float      *tri;                /* its triangles: 3 x (x, y, NC)      */
    long        ntri, captri;
};

/** @brief A mesh vertex: where, and its colour values. */
typedef struct {
    double x, y;                    /* in the shading's space            */
    double c[NC];                   /* components, or one t              */
} vtx_t;

/** @brief Up to @p max numbers of array @p a into @p v; @return how many. */
static int
numbers(tiku_pdf_t *doc, pdf_obj_t *a, double *v, int max)
{
    int i, n = 0;

    if (a == NULL || a->kind != PDF_ARRAY) {
        return 0;
    }
    for (i = 0; i < a->u.a.n && n < max; i++) {
        v[n++] = pdf_number(pdf_resolve(doc, a->u.a.item[i]), 0);
    }
    return n;
}

/** @brief The colour the function gives @p in, through the space. */
static uint32_t
colour_of(const pdf_shade_t *s, const double *in, int nin)
{
    double out[PDF_FUNC_MAX_OUT];
    int n = pdf_func_eval(s->fn, in, nin, out);

    if (n < 1) {
        return 0xff808080u;
    }
    return pdf_cs_colour(s->cs, out, n);
}

/** @brief The table of colours along t, for the axial and radial kinds. */
static void
fill_lut(pdf_shade_t *s)
{
    int i;

    for (i = 0; i < SHADE_LUT; i++) {
        double t = s->domain[0] + (s->domain[1] - s->domain[0]) * i /
                                  (SHADE_LUT - 1);

        s->lut[i] = s->fn != NULL ? colour_of(s, &t, 1) : 0xff808080u;
    }
}

/* --------------------------------------------------------------- meshes  */

/** @brief A mesh's packed samples: its bits, widths and Decode ranges. */
typedef struct {
    const unsigned char *p;         /* the stream, decoded               */
    long                 n, bit;
    int                  over;      /* a read ran past the end           */
    int                  bpc, bpf, bpk; /* coordinate, flag, component bits */
    double               dec[4 + 2 * NC];   /* x, y, then each value's range */
} mesh_t;

/** @brief The next @p k bits (up to 32) as a number; -1 past the end. */
static double
bits(mesh_t *m, int k)
{
    double v = 0;
    int i;

    if (k <= 0 || m->bit + k > m->n * 8) {
        m->bit = m->n * 8;
        m->over = 1;
        return -1;
    }
    for (i = 0; i < k; i++, m->bit++) {
        v = v * 2 + (m->p[m->bit >> 3] >> (7 - (m->bit & 7)) & 1);
    }
    return v;
}

/** @brief A sample of @p k bits mapped into range @p lo, @p hi. */
static double
sample(mesh_t *m, int k, double lo, double hi)
{
    double v = bits(m, k), max = k >= 32 ? 4294967295.0 : (1u << k) - 1.0;

    return v < 0 ? 0 : lo + v * (hi - lo) / max;
}

/** @brief One vertex's place, or @p colour too.  @return 0 past the end. */
static int
read_vtx(mesh_t *m, int nc, vtx_t *v, int colour)
{
    int i;

    v->x = sample(m, m->bpc, m->dec[0], m->dec[1]);
    v->y = sample(m, m->bpc, m->dec[2], m->dec[3]);
    for (i = 0; colour && i < nc; i++) {
        v->c[i] = sample(m, m->bpk, m->dec[4 + 2 * i], m->dec[5 + 2 * i]);
    }
    return !m->over;
}

/** @brief @p v as a float the drawing can use: held to +-1e7, NaN 0. */
static float
fl(double v)
{
    return v > -1e7 ? (v < 1e7 ? (float)v : 1e7f) : v == v ? -1e7f : 0.0f;
}

/** @brief Triangle @p a, @p b, @p c kept for painting. */
static void
keep_tri(pdf_shade_t *s, const vtx_t *a, const vtx_t *b, const vtx_t *c)
{
    const vtx_t *v[3] = {a, b, c};
    float *t;
    int k, i;

    if (s->ntri >= MESH_MAX) {
        return;
    }
    if (s->ntri == s->captri) {
        long cap = s->captri ? s->captri * 2 : 256;

        t = realloc(s->tri, (size_t)cap * 3 * (2 + NC) * sizeof *t);
        if (t == NULL) {
            return;
        }
        s->tri = t;
        s->captri = cap;
    }
    t = s->tri + s->ntri++ * 3 * (2 + NC);
    for (k = 0; k < 3; k++, t += 2 + NC) {
        t[0] = fl(v[k]->x);
        t[1] = fl(v[k]->y);
        for (i = 0; i < NC; i++) {
            t[2 + i] = fl(v[k]->c[i]);
        }
    }
}

/** @brief Type 4: vertices each with a flag -- 0 starts a triangle, 1
 *         and 2 go on from two corners of the last -- byte by byte. */
static void
free_mesh(pdf_shade_t *s, mesh_t *m)
{
    vtx_t tri[3], v;
    int have = 0, started = 0;

    while (m->bit < m->n * 8) {
        double f = bits(m, m->bpf);

        if (f < 0 || !read_vtx(m, s->nc, &v, 1)) {
            break;
        }
        m->bit = (m->bit + 7) & ~7L;    /* each vertex starts a byte */
        if (have == 0 && f != 0) {
            if (!started) {
                continue;               /* nothing yet to go on from */
            }
            if (f == 1) {
                tri[0] = tri[1];        /* on from vb, vc */
            }
            tri[1] = tri[2];            /* f 2: on from va, vc */
            tri[2] = v;
            keep_tri(s, &tri[0], &tri[1], &tri[2]);
            continue;
        }
        tri[have++] = v;
        if (have == 3) {
            keep_tri(s, &tri[0], &tri[1], &tri[2]);
            have = 0;
            started = 1;
        }
    }
}

/** @brief Type 5: rows of @p per vertices, each square of four split in
 *         two triangles. */
static void
lattice(pdf_shade_t *s, mesh_t *m, int per)
{
    vtx_t *row[2];
    int r = 0, i;

    if (per < 2 || per > 65536) {
        return;
    }
    row[0] = malloc((size_t)per * sizeof **row);
    row[1] = malloc((size_t)per * sizeof **row);
    while (row[0] != NULL && row[1] != NULL) {
        vtx_t *now = row[r & 1], *last = row[(r + 1) & 1];

        i = 0;
        while (i < per && read_vtx(m, s->nc, &now[i], 1)) {
            i++;
        }
        if (i < per) {
            break;
        }
        for (i = 0; r > 0 && i + 1 < per; i++) {
            keep_tri(s, &last[i], &last[i + 1], &now[i]);
            keep_tri(s, &last[i + 1], &now[i + 1], &now[i]);
        }
        r++;
    }
    free(row[0]);
    free(row[1]);
}

/** @brief A cubic Bezier's point at @p t, of @p p (x, y pairs). */
static void
bezier(const double *p, double t, double *x, double *y)
{
    double u = 1 - t, b0 = u * u * u, b1 = 3 * t * u * u, b2 = 3 * t * t * u;
    double b3 = t * t * t;

    *x = b0 * p[0] + b1 * p[2] + b2 * p[4] + b3 * p[6];
    *y = b0 * p[1] + b1 * p[3] + b2 * p[5] + b3 * p[7];
}

/**
 * @brief Patch point (@p u, @p v): a tensor patch's sixteen points
 *        weighted, or a Coons patch's four edges joined, less its corners
 *        (8.7.4.5.7).  @p g is the 4 by 4 grid, g[i][j] at u = i/3,
 *        v = j/3, as x, y.
 */
static void
patch_at(const double g[4][4][2], int tensor, double u, double v, double *x,
         double *y)
{
    double e[4][8], p[4][2];
    int i, j;

    if (tensor) {
        for (i = 0; i < 4; i++) {
            for (j = 0; j < 4; j++) {
                e[i][2 * j] = g[i][j][0];
                e[i][2 * j + 1] = g[i][j][1];
            }
            bezier(e[i], v, &p[i][0], &p[i][1]);
        }
        for (i = 0; i < 4; i++) {
            e[0][2 * i] = p[i][0];
            e[0][2 * i + 1] = p[i][1];
        }
        bezier(e[0], u, x, y);
        return;
    }
    for (i = 0; i < 4; i++) {       /* C1, C2 along u; D1, D2 along v */
        e[0][2 * i] = g[i][0][0];  e[0][2 * i + 1] = g[i][0][1];
        e[1][2 * i] = g[i][3][0];  e[1][2 * i + 1] = g[i][3][1];
        e[2][2 * i] = g[0][i][0];  e[2][2 * i + 1] = g[0][i][1];
        e[3][2 * i] = g[3][i][0];  e[3][2 * i + 1] = g[3][i][1];
    }
    bezier(e[0], u, &p[0][0], &p[0][1]);
    bezier(e[1], u, &p[1][0], &p[1][1]);
    bezier(e[2], v, &p[2][0], &p[2][1]);
    bezier(e[3], v, &p[3][0], &p[3][1]);
    for (i = 0; i < 2; i++) {
        double corners = (1 - u) * (1 - v) * g[0][0][i] + (1 - u) * v *
                         g[0][3][i] + u * (1 - v) * g[3][0][i] +
                         u * v * g[3][3][i];
        double at = (1 - v) * p[0][i] + v * p[1][i] + (1 - u) * p[2][i] +
                    u * p[3][i] - corners;

        if (i == 0) {
            *x = at;
        } else {
            *y = at;
        }
    }
}

/** @brief A patch cut into PATCH_GRID squared steps, its colour bilinear
 *         between the corners @p c (at u, v of 00, 03, 33, 30). */
static void
patch_tris(pdf_shade_t *s, const double g[4][4][2], int tensor,
           const vtx_t *c)
{
    vtx_t row[2][PATCH_GRID + 1];
    int i, j, k;

    for (j = 0; j <= PATCH_GRID; j++) {
        for (i = 0; i <= PATCH_GRID; i++) {
            double u = (double)i / PATCH_GRID, v = (double)j / PATCH_GRID;
            vtx_t *p = &row[j & 1][i];

            patch_at(g, tensor, u, v, &p->x, &p->y);
            for (k = 0; k < NC; k++) {
                p->c[k] = (1 - u) * (1 - v) * c[0].c[k] + (1 - u) * v *
                          c[1].c[k] + u * v * c[2].c[k] + u * (1 - v) *
                          c[3].c[k];
            }
        }
        for (i = 0; j > 0 && i < PATCH_GRID; i++) {
            vtx_t *a = &row[(j - 1) & 1][i], *b = &row[(j - 1) & 1][i + 1];
            vtx_t *d = &row[j & 1][i], *e = &row[j & 1][i + 1];

            keep_tri(s, a, b, d);
            keep_tri(s, b, e, d);
        }
    }
}

/**
 * @brief Types 6 and 7: patches, each with a flag.  0 brings all its
 *        points and four colours; 1, 2 and 3 take the first edge and its
 *        two colours from the last patch's second, third or fourth.
 */
static void
patches(pdf_shade_t *s, mesh_t *m, int tensor)
{
    /* The order the points come in: the edge round from g[0][0], then a
     * tensor patch's four inside. */
    static const unsigned char at[16][2] = {
        {0, 0}, {0, 1}, {0, 2}, {0, 3}, {1, 3}, {2, 3}, {3, 3}, {3, 2},
        {3, 1}, {3, 0}, {2, 0}, {1, 0}, {1, 1}, {1, 2}, {2, 2}, {2, 1}
    };
    double g[4][4][2], last[4][4][2];
    vtx_t c[4], lc[4], v;
    int first = 1, i, npt = tensor ? 16 : 12;

    while (m->bit < m->n * 8) {
        double f = bits(m, m->bpf);
        int from = f == 0 || first ? 0 : 4, k;

        if (f < 0 || f > 3) {
            break;
        }
        if (from) {                 /* the shared edge, from the last */
            static const unsigned char edge[4][4][2] = {
                {{0, 0}}, {{0, 3}, {1, 3}, {2, 3}, {3, 3}},
                {{3, 3}, {3, 2}, {3, 1}, {3, 0}},
                {{3, 0}, {2, 0}, {1, 0}, {0, 0}}
            };
            for (k = 0; k < 4; k++) {
                g[0][k][0] = last[edge[(int)f][k][0]][edge[(int)f][k][1]][0];
                g[0][k][1] = last[edge[(int)f][k][0]][edge[(int)f][k][1]][1];
            }
            c[0] = lc[(int)f];
            c[1] = lc[((int)f + 1) & 3];
        }
        for (i = from; i < npt; i++) {
            (void)read_vtx(m, s->nc, &v, 0);
            g[at[i][0]][at[i][1]][0] = v.x;
            g[at[i][0]][at[i][1]][1] = v.y;
        }
        for (i = from ? 2 : 0; i < 4; i++) {
            for (k = 0; k < s->nc; k++) {
                c[i].c[k] = sample(m, m->bpk, m->dec[4 + 2 * k],
                                   m->dec[5 + 2 * k]);
            }
        }
        m->bit = (m->bit + 7) & ~7L;    /* each patch starts a byte */
        if (m->over) {
            break;
        }
        patch_tris(s, (const double (*)[4][2])g, tensor, c);
        memcpy(last, g, sizeof last);
        memcpy(lc, c, sizeof lc);
        first = 0;
    }
}

/** @brief A mesh shading's stream read into triangles.  @p s->nc is the
 *         colour values each vertex brings: one t with a Function, else
 *         the space's components. */
static void
mesh_load(tiku_pdf_t *doc, pdf_obj_t *dict, pdf_shade_t *s)
{
    unsigned char *data = NULL;
    long n = 0;
    mesh_t m;
    int i;

    memset(&m, 0, sizeof m);
    s->nc = s->fn != NULL ? 1 : s->cs != NULL ? s->cs->n : 1;
    if (s->nc > NC || dict->kind != PDF_STREAM ||
        pdf_stream_data(doc, dict, &data, &n, NULL) != 0) {
        free(data);
        return;
    }
    m.p = data;
    m.n = n;
    m.bpc = pdf_int(pdf_get(doc, dict, "BitsPerCoordinate"), 0);
    m.bpk = pdf_int(pdf_get(doc, dict, "BitsPerComponent"), 0);
    m.bpf = pdf_int(pdf_get(doc, dict, "BitsPerFlag"), 0);
    if (numbers(doc, pdf_get(doc, dict, "Decode"), m.dec, 4 + 2 * NC) >=
            4 + 2 * s->nc && m.bpc >= 1 && m.bpc <= 32 && m.bpk >= 1 &&
        m.bpk <= 16) {
        for (i = 0; s->fn != NULL && i < 2; i++) {
            s->domain[i] = m.dec[4 + i];    /* the t of the table */
        }
        if (s->fn != NULL) {
            fill_lut(s);
        }
        if (s->type == 4 && m.bpf >= 2 && m.bpf <= 8) {
            free_mesh(s, &m);
        } else if (s->type == 5) {
            lattice(s, &m, pdf_int(pdf_get(doc, dict, "VerticesPerRow"), 0));
        } else if ((s->type == 6 || s->type == 7) && m.bpf >= 2 &&
                   m.bpf <= 8) {
            patches(s, &m, s->type == 7);
        }
    }
    free(data);
}

pdf_shade_t *
pdf_shade_load(tiku_pdf_t *doc, pdf_obj_t *dict, pdf_obj_t *res)
{
    pdf_shade_t *s;
    double v[6];
    pdf_obj_t *e;

    if (dict == NULL || (dict->kind != PDF_DICT && dict->kind != PDF_STREAM)) {
        return NULL;
    }
    s = calloc(1, sizeof *s);
    if (s == NULL) {
        return NULL;
    }
    s->type = pdf_int(pdf_get(doc, dict, "ShadingType"), 0);
    s->cs = pdf_cs_load(doc, pdf_get(doc, dict, "ColorSpace"), res);
    s->fn = pdf_func_load(doc, pdf_get(doc, dict, "Function"));
    s->domain[1] = s->domain[3] = 1;
    (void)numbers(doc, pdf_get(doc, dict, "Domain"), s->domain, 4);
    (void)numbers(doc, pdf_get(doc, dict, "Coords"), s->coords, 6);
    e = pdf_get(doc, dict, "Extend");
    if (e != NULL && e->kind == PDF_ARRAY && e->u.a.n == 2) {
        pdf_obj_t *a = pdf_resolve(doc, e->u.a.item[0]);
        pdf_obj_t *b = pdf_resolve(doc, e->u.a.item[1]);

        s->extend[0] = a != NULL && a->kind == PDF_BOOL && a->u.b;
        s->extend[1] = b != NULL && b->kind == PDF_BOOL && b->u.b;
    }
    s->matrix = pdf_mat(1, 0, 0, 1, 0, 0);
    if (numbers(doc, pdf_get(doc, dict, "Matrix"), v, 6) == 6) {
        s->matrix = pdf_mat(v[0], v[1], v[2], v[3], v[4], v[5]);
    }
    s->has_bbox = numbers(doc, pdf_get(doc, dict, "BBox"), s->bbox, 4) == 4;
    if (s->cs != NULL && numbers(doc, pdf_get(doc, dict, "Background"), v,
                                 4) == s->cs->n) {
        s->bg = pdf_cs_colour(s->cs, v, s->cs->n);
        s->has_bg = 1;
    }
    if (s->type == 2 || s->type == 3) {
        fill_lut(s);
    }
    /* A mesh: its triangles; failing those, its Background, else its
     * function half way, else grey, so what it fills keeps its place. */
    if (s->type >= 4 && s->type <= 7) {
        mesh_load(doc, dict, s);
    }
    s->flat = 0xffc8c8c8u;
    if (s->has_bg) {
        s->flat = s->bg;
    } else if (s->fn != NULL && s->cs != NULL) {
        double mid = (s->domain[0] + s->domain[1]) / 2;

        s->flat = colour_of(s, &mid, 1);
    }
    return s;
}

void
pdf_shade_free(pdf_shade_t *s)
{
    if (s != NULL) {
        pdf_cs_free(s->cs);
        pdf_func_free(s->fn);
        free(s->tri);
        free(s);
    }
}

/* -------------------------------------------------------------- colour  */

/** @brief The table's colour at @p t along the axis, 0 to 1. */
static uint32_t
along(const pdf_shade_t *s, double t)
{
    double k = t * (SHADE_LUT - 1) + 0.5;         /* a NaN is the start */

    return s->lut[k > 0 ? (k < SHADE_LUT - 1 ? (int)k : SHADE_LUT - 1) : 0];
}

/** @brief Type 2: where @p u, @p v falls along the axis, if between its
 *         ends or past one the shading extends. */
static int
axial_at(const pdf_shade_t *s, double u, double v, uint32_t *c)
{
    double dx = s->coords[2] - s->coords[0], dy = s->coords[3] - s->coords[1];
    double d2 = dx * dx + dy * dy;
    double t = d2 > 0 ? ((u - s->coords[0]) * dx + (v - s->coords[1]) * dy) /
                        d2 : 0;

    if (t < 0) {
        if (!s->extend[0]) return 0;
        t = 0;
    } else if (t > 1) {
        if (!s->extend[1]) return 0;
        t = 1;
    }
    *c = along(s, t);
    return 1;
}

/** @brief Where along the circles of type 3's @p c the ones through @p u,
 *         @p v are: the larger first.  @return how many, 0 to 2. */
static int
radial_roots(const double *c, double u, double v, double *root)
{
    double cx = c[3] - c[0], cy = c[4] - c[1], dr = c[5] - c[2];
    double px = u - c[0], py = v - c[1];
    double a = cx * cx + cy * cy - dr * dr;
    double b = px * cx + py * cy + c[2] * dr;
    double k = px * px + py * py - c[2] * c[2], disc;

    if (fabs(a) < 1e-9) {
        if (fabs(b) < 1e-12) return 0;
        root[0] = k / (2 * b);
        return 1;
    }
    disc = b * b - a * k;
    if (disc < 0) return 0;
    root[0] = (b + sqrt(disc)) / a;
    root[1] = (b - sqrt(disc)) / a;
    if (root[1] > root[0]) {
        double t = root[0];

        root[0] = root[1];
        root[1] = t;
    }
    return 2;
}

/** @brief Type 3: the circle, of those between the two and past the ones
 *         extended, that passes through @p u, @p v; the larger when two do. */
static int
radial_at(const pdf_shade_t *s, double u, double v, uint32_t *c)
{
    double root[2], r0 = s->coords[2], dr = s->coords[5] - s->coords[2];
    int n = radial_roots(s->coords, u, v, root), i;

    for (i = 0; i < n; i++) {
        double t = root[i];

        if (r0 + t * dr < 0) continue;
        if (t > 1 && !s->extend[1]) continue;
        if (t < 0 && !s->extend[0]) continue;
        *c = along(s, t < 0 ? 0 : t > 1 ? 1 : t);
        return 1;
    }
    return 0;
}

/** @brief Type 1: the function over its domain, through its Matrix. */
static int
function_at(const pdf_shade_t *s, double u, double v, uint32_t *c)
{
    const pdf_mat_t *m = &s->matrix;
    double det = m->a * m->d - m->b * m->c, in[2];

    if (det == 0 || s->fn == NULL) return 0;
    u -= m->e;
    v -= m->f;
    in[0] = (m->d * u - m->c * v) / det;
    in[1] = (m->a * v - m->b * u) / det;
    if (in[0] < s->domain[0] || in[0] > s->domain[1] ||
        in[1] < s->domain[2] || in[1] > s->domain[3]) return 0;
    *c = colour_of(s, in, 2);
    return 1;
}

/** @brief The shading's colour at @p u, @p v in its own space; 0 where
 *         it paints nothing. */
static int
shade_at(const pdf_shade_t *s, double u, double v, uint32_t *c)
{
    if (s->has_bbox && (u < fmin(s->bbox[0], s->bbox[2]) ||
                        u > fmax(s->bbox[0], s->bbox[2]) ||
                        v < fmin(s->bbox[1], s->bbox[3]) ||
                        v > fmax(s->bbox[1], s->bbox[3]))) return 0;
    if (s->type == 2) return axial_at(s, u, v, c);
    if (s->type == 3) return radial_at(s, u, v, c);
    if (s->type == 1) return function_at(s, u, v, c);
    *c = s->flat;                   /* a mesh: where its BBox says it is */
    return s->has_bbox;
}

/* ------------------------------------------------------------ painting  */

/** @brief The pixels to visit: @p m's box, else the clip's, else all. */
static void
extent(const pdf_pix_t *pix, const pdf_mask_t *m, const pdf_mask_t *clip,
       int *box)
{
    const pdf_mask_t *by = m != NULL ? m : clip;

    box[0] = 0;
    box[1] = 0;
    box[2] = pix->w;
    box[3] = pix->h;
    if (by != NULL && by->a != NULL) {
        box[0] = by->x0 > 0 ? by->x0 : 0;
        box[1] = by->y0 > 0 ? by->y0 : 0;
        box[2] = by->x0 + by->w < pix->w ? by->x0 + by->w : pix->w;
        box[3] = by->y0 + by->h < pix->h ? by->y0 + by->h : pix->h;
    }
}

/** @brief How much of mask @p m covers pixel @p x, @p y: 255 when it is
 *         NULL, 0 outside it. */
static int
cover(const pdf_mask_t *m, int x, int y)
{
    if (m == NULL) return 255;
    if (m->a == NULL || x < m->x0 || y < m->y0 || x >= m->x0 + m->w ||
        y >= m->y0 + m->h) return 0;
    return m->a[(size_t)(y - m->y0) * m->w + (x - m->x0)];
}

/** @brief A vertex's colour values as a pixel: through the table along t
 *         when the mesh has a function, else through the space. */
static uint32_t
mesh_colour(const pdf_shade_t *s, const double *c)
{
    if (s->fn != NULL) {
        double span = s->domain[1] - s->domain[0];

        return along(s, span != 0 ? (c[0] - s->domain[0]) / span : 0);
    }
    return pdf_cs_colour(s->cs, c, s->nc);
}

/** @brief Where pixel centre (@p px, @p py) lies in the triangle: its
 *         three weights into @p w, and whether it is inside, an edge two
 *         triangles share going to one of them (ruled top-left), so a
 *         mesh has neither seams nor overlaps. */
static int
inside(const double *x, const double *y, double area, double px, double py,
       double *w)
{
    int k, in = 1;

    for (k = 0; k < 3; k++) {
        int j = (k + 1) % 3, l = (k + 2) % 3;
        int top_left = (y[l] == y[j] && x[l] < x[j]) || y[l] < y[j];

        w[k] = ((x[l] - x[j]) * (py - y[j]) - (y[l] - y[j]) * (px - x[j])) /
               area;
        in &= w[k] > 0 || (w[k] == 0 && (area > 0 ? top_left : !top_left));
    }
    return in;
}

/** @brief The colour triangle @p t's corners blend to at weights @p w. */
static uint32_t
blend_at(const pdf_shade_t *s, const float *t, const double *w)
{
    double c[NC];
    int i;

    for (i = 0; i < s->nc; i++) {
        c[i] = w[0] * t[2 + i] + w[1] * t[(2 + NC) + 2 + i] +
               w[2] * t[2 * (2 + NC) + 2 + i];
    }
    return mesh_colour(s, c);
}

/** @brief One triangle @p t filled, each pixel whose centre it holds
 *         taking the colour its corners blend to there, through @p m and
 *         @p clip.  @return the pixels it looked at. */
static long
fill_tri(pdf_pix_t *pix, const pdf_shade_t *s, const float *t,
         const pdf_mat_t *to, const pdf_mask_t *m, const pdf_mask_t *clip,
         const int *box, int ca)
{
    double x[3], y[3], area, w[3];
    int k, b[4], xi, yi;

    for (k = 0; k < 3; k++) {
        pdf_mat_apply(to, t[k * (2 + NC)], t[k * (2 + NC) + 1], &x[k], &y[k]);
    }
    area = (x[1] - x[0]) * (y[2] - y[0]) - (x[2] - x[0]) * (y[1] - y[0]);
    if (!(fabs(area) > 1e-12)) {
        return 0;
    }
    /* Held to the box before an int is made of them: a NaN too. */
    b[0] = (int)fmax(box[0], fmin(box[2], floor(fmin(x[0], fmin(x[1],
                                                                x[2])))));
    b[1] = (int)fmax(box[1], fmin(box[3], floor(fmin(y[0], fmin(y[1],
                                                                y[2])))));
    b[2] = (int)fmin(box[2], fmax(box[0], ceil(fmax(x[0], fmax(x[1],
                                                               x[2])))));
    b[3] = (int)fmin(box[3], fmax(box[1], ceil(fmax(y[0], fmax(y[1],
                                                               y[2])))));
    for (yi = b[1]; yi < b[3]; yi++) {
        for (xi = b[0]; xi < b[2]; xi++) {
            int a = cover(m, xi, yi), shape;

            if (a == 0 || !inside(x, y, area, xi + 0.5, yi + 0.5, w) ||
                (clip != NULL && (a = a * cover(clip, xi, yi) / 255) == 0)) {
                continue;
            }
            shape = a;
            a = (a * ca + 127) / 255;
            if (a > 0) {
                pdf_blend(pix, pix->px + (size_t)yi * pix->w + xi,
                          blend_at(s, t, w), a, shape);
            }
        }
    }
    return (long)(b[2] - b[0]) * (b[3] - b[1]);
}

void
pdf_shade_paint(pdf_pix_t *pix, const pdf_shade_t *s, const pdf_mat_t *to,
                const pdf_mask_t *m, const pdf_mask_t *clip, double alpha,
                int background)
{
    double det = to->a * to->d - to->b * to->c;
    int box[4], x, y, ca = (int)(alpha * 255 + 0.5);

    if (s == NULL || det == 0 || ca <= 0 || (m != NULL && m->a == NULL)) {
        return;
    }
    if (ca > 255) ca = 255;
    extent(pix, m, clip, box);
    if (s->ntri > 0) {
        long t, work;

        for (y = box[1]; background && s->has_bg && y < box[3]; y++) {
            for (x = box[0]; x < box[2]; x++) {
                int a = cover(m, x, y), shape;

                a = clip != NULL ? a * cover(clip, x, y) / 255 : a;
                shape = a;
                a = (a * ca + 127) / 255;
                if (a > 0) {        /* a pattern's ground, under the mesh */
                    pdf_blend(pix, pix->px + (size_t)y * pix->w + x, s->bg,
                              a, shape);
                }
            }
        }
        for (t = 0, work = 0; t < s->ntri && work < MESH_WORK; t++) {
            work += fill_tri(pix, s, s->tri + t * 3 * (2 + NC), to, m, clip,
                             box, ca);
        }
        return;
    }
    for (y = box[1]; y < box[3]; y++) {
        for (x = box[0]; x < box[2]; x++) {
            double px = x + 0.5 - to->e, py = y + 0.5 - to->f;
            double u = (to->d * px - to->c * py) / det;
            double v = (to->a * py - to->b * px) / det;
            int a = cover(m, x, y), shape;
            uint32_t colour;

            if (a == 0 || (clip != NULL && (a = a * cover(clip, x, y) /
                                                255) == 0)) continue;
            shape = a;
            if (!shade_at(s, u, v, &colour)) {
                /* A pattern's fill shows its Background, or for a mesh
                 * its tone, where the shading itself does not reach. */
                if (!background || (!s->has_bg && s->type < 4)) continue;
                colour = s->has_bg ? s->bg : s->flat;
            }
            a = (a * ca + 127) / 255;
            if (a > 0 && (colour >> 24) != 0) {
                pdf_blend(pix, pix->px + (size_t)y * pix->w + x, colour, a,
                          shape);
            }
        }
    }
}
