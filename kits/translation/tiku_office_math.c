/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_office_math.c - equations set as TeX sets them: the tree's tokens
 * as glyphs of the math face, letters in Unicode's mathematical italic
 * (or bold, script, fraktur ...), rows spaced by the classes of their
 * atoms, characters grown to what they cover from the face's own bigger
 * variants and parts, or drawn when it has none; the structures are in
 * tiku_office_math_set.c.  Equations are set once the faces are known,
 * before the pages, and drawn as glyphs, rules and strokes whose text
 * can be found and copied.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_office_math.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------ the tree  */

of_mnode_t *
om_node(of_arena_t *a, int kind, int nkid)
{
    of_mnode_t *n = of_alloc(a, sizeof *n);

    if (n == NULL) {
        return NULL;
    }
    n->kind = (unsigned char)kind;
    n->nkid = nkid > 0 ? nkid : 0;
    if (n->nkid > 0 &&
        (n->kid = of_alloc(a, (size_t)n->nkid * sizeof *n->kid)) == NULL) {
        return NULL;
    }
    return n;
}

of_mnode_t *
om_token(of_arena_t *a, const char *text, int cls, int variant)
{
    of_mnode_t *n = om_node(a, OM_TOKEN, 0);

    if (n == NULL || (n->text = of_strdup(a, text ? text : "")) == NULL) {
        return NULL;
    }
    n->cls = (unsigned char)cls;
    n->variant = (unsigned char)variant;
    return n;
}

/** @brief Whether @p cp is in the sorted list @p set of @p n. */
static int
among(const uint16_t *set, size_t n, uint32_t cp)
{
    size_t lo = 0, hi = n;

    while (lo < hi) {
        size_t mid = (lo + hi) / 2;

        if (set[mid] == cp) {
            return 1;
        }
        if (set[mid] < cp) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return 0;
}

int
om_class(uint32_t cp)
{
    static const uint16_t rel[] = {
        ':', '<', '=', '>', 0x2208, 0x2209, 0x220b, 0x220c, 0x221d,
        0x2223, 0x2224, 0x2225, 0x2226, 0x223c, 0x223d, 0x2241, 0x2243,
        0x2245, 0x2248, 0x224d, 0x2250, 0x2252, 0x2253, 0x2254, 0x2255,
        0x225c, 0x225d, 0x225f, 0x2260, 0x2261, 0x2262, 0x2264, 0x2265,
        0x2266, 0x2267, 0x226a, 0x226b, 0x226e, 0x226f, 0x2270, 0x2271,
        0x2272, 0x2273, 0x227a, 0x227b, 0x227c, 0x227d, 0x2282, 0x2283,
        0x2284, 0x2285, 0x2286, 0x2287, 0x2288, 0x2289, 0x228a, 0x228b,
        0x228f, 0x2290, 0x2291, 0x2292, 0x22a2, 0x22a3, 0x22a5, 0x22a8,
        0x22a9, 0x22ab, 0x22b2, 0x22b3, 0x22b4, 0x22b5, 0x22c8, 0x2aaf,
        0x2ab0};
    static const uint16_t bin[] = {
        '*', '+', '-', 0xb1, 0xb7, 0xd7, 0xf7, 0x2020, 0x2021, 0x2022,
        0x2212, 0x2213, 0x2214, 0x2216, 0x2217, 0x2218, 0x2219, 0x2227,
        0x2228, 0x2229, 0x222a, 0x2240, 0x228e, 0x2293, 0x2294, 0x2295,
        0x2296, 0x2297, 0x2298, 0x2299, 0x229a, 0x229b, 0x229d, 0x229e,
        0x229f, 0x22a0, 0x22a1, 0x22c4, 0x22c5, 0x22c6, 0x22ce, 0x22cf,
        0x2a3f};
    static const uint16_t open[] = {'(', '[', '{', 0x2308, 0x230a, 0x2329,
                                    0x27e6, 0x27e8, 0x27ea, 0x2983, 0x3008};
    static const uint16_t close[] = {')', ']', '}', 0x2309, 0x230b, 0x232a,
                                     0x27e7, 0x27e9, 0x27eb, 0x2984,
                                     0x3009};
    static const uint16_t op[] = {
        0x220f, 0x2210, 0x2211, 0x222b, 0x222c, 0x222d, 0x222e, 0x222f,
        0x2230, 0x2231, 0x2232, 0x2233, 0x22c0, 0x22c1, 0x22c2, 0x22c3,
        0x2a00, 0x2a01, 0x2a02, 0x2a03, 0x2a04, 0x2a05, 0x2a06, 0x2a09,
        0x2a0c, 0x2a0d, 0x2a0e, 0x2a0f, 0x2a10, 0x2a11, 0x2a12, 0x2a13,
        0x2a14, 0x2a15, 0x2a16, 0x2a17};

    if ((cp >= 0x2190 && cp <= 0x21ff) || (cp >= 0x27f0 && cp <= 0x27ff) ||
        (cp >= 0x2900 && cp <= 0x297f) ||
        among(rel, sizeof rel / sizeof rel[0], cp)) {
        return OC_REL;              /* arrows are relations, as in TeX */
    }
    return among(bin, sizeof bin / sizeof bin[0], cp) ? OC_BIN :
           among(open, sizeof open / sizeof open[0], cp) ? OC_OPEN :
           among(close, sizeof close / sizeof close[0], cp) ? OC_CLOSE :
           among(op, sizeof op / sizeof op[0], cp) ? OC_OP :
           cp == ',' || cp == ';' ? OC_PUNCT : OC_ORD;
}

of_math_t *
of_math_new(of_doc_t *d, of_mnode_t *root, int display, float size,
            uint32_t color)
{
    of_math_t *m = of_alloc(&d->arena, sizeof *m);

    if (m == NULL || root == NULL) {
        return NULL;
    }
    m->root = root;
    m->display = (unsigned char)(display != 0);
    m->size = size > 0 ? size : 11;
    m->color = color ? color : 0xff000000u;
    m->next = d->maths;             /* each is set alone: any order */
    d->maths = m;
    return m;
}

/* ------------------------------------------------------------ letters  */

/** @brief The next character of UTF-8 @p *s, moving past it; 0 at the
 *         end, U+FFFD for a broken sequence. */
static uint32_t
next_cp(const char **s)
{
    const unsigned char *p = (const unsigned char *)*s;
    uint32_t c = p[0];
    int n = c < 0x80 ? 0 : c < 0xe0 ? 1 : c < 0xf0 ? 2 : 3, i;

    if (c == 0) {
        return 0;
    }
    c = n == 0 ? c : n == 1 ? (c & 0x1f) : n == 2 ? (c & 0x0f) : (c & 7);
    for (i = 1; i <= n; i++) {
        if ((p[i] & 0xc0) != 0x80) {
            *s += i;
            return 0xfffd;
        }
        c = c << 6 | (p[i] & 0x3f);
    }
    *s += n + 1;
    return c;
}

/** @brief Where letterlike symbols stand in for the mathematical ones
 *         Unicode leaves out: a variant's letter, or 0. */
static uint32_t
hole(uint32_t c, int v)
{
    static const struct { char v, c; uint16_t u; } h[] = {
        {OV_ITALIC, 'h', 0x210e}, {OV_SCRIPT, 'B', 0x212c},
        {OV_SCRIPT, 'E', 0x2130}, {OV_SCRIPT, 'F', 0x2131},
        {OV_SCRIPT, 'H', 0x210b}, {OV_SCRIPT, 'I', 0x2110},
        {OV_SCRIPT, 'L', 0x2112}, {OV_SCRIPT, 'M', 0x2133},
        {OV_SCRIPT, 'R', 0x211b}, {OV_SCRIPT, 'e', 0x212f},
        {OV_SCRIPT, 'g', 0x210a}, {OV_SCRIPT, 'o', 0x2134},
        {OV_FRAKTUR, 'C', 0x212d}, {OV_FRAKTUR, 'H', 0x210c},
        {OV_FRAKTUR, 'I', 0x2111}, {OV_FRAKTUR, 'R', 0x211c},
        {OV_FRAKTUR, 'Z', 0x2128}, {OV_DOUBLE, 'C', 0x2102},
        {OV_DOUBLE, 'H', 0x210d}, {OV_DOUBLE, 'N', 0x2115},
        {OV_DOUBLE, 'P', 0x2119}, {OV_DOUBLE, 'Q', 0x211a},
        {OV_DOUBLE, 'R', 0x211d}, {OV_DOUBLE, 'Z', 0x2124}};
    size_t i;

    for (i = 0; i < sizeof h / sizeof h[0]; i++) {
        if (h[i].v == v && (uint32_t)(unsigned char)h[i].c == c) {
            return h[i].u;
        }
    }
    return 0;
}

/** @brief The character letter or digit @p c becomes in variant @p v:
 *         Unicode's mathematical alphanumerics; @p c itself otherwise. */
static uint32_t
styled(uint32_t c, int v)
{
    static const uint32_t latin[][3] = {      /* A, a, 0 for each OV_ */
        [OV_ITALIC] = {0x1d434, 0x1d44e, 0},
        [OV_BOLD] = {0x1d400, 0x1d41a, 0x1d7ce},
        [OV_BOLD_ITALIC] = {0x1d468, 0x1d482, 0x1d7ce},
        [OV_SCRIPT] = {0x1d49c, 0x1d4b6, 0},
        [OV_FRAKTUR] = {0x1d504, 0x1d51e, 0},
        [OV_DOUBLE] = {0x1d538, 0x1d552, 0x1d7d8},
        [OV_SANS] = {0x1d5a0, 0x1d5ba, 0x1d7e2},
        [OV_MONO] = {0x1d670, 0x1d68a, 0x1d7f6}};
    static const uint32_t greek[][2] = {      /* Alpha, alpha */
        [OV_ITALIC] = {0x1d6e2, 0x1d6fc}, [OV_BOLD] = {0x1d6a8, 0x1d6c2},
        [OV_BOLD_ITALIC] = {0x1d71c, 0x1d736}};
    uint32_t h;

    if (v <= OV_NORMAL || v >= OV_TEXT || (h = hole(c, v)) != 0) {
        return v <= OV_NORMAL || v >= OV_TEXT ? c : h;
    }
    if (c >= 'A' && c <= 'Z') {
        return latin[v][0] + c - 'A';
    }
    if (c >= 'a' && c <= 'z') {
        return latin[v][1] + c - 'a';
    }
    if (c >= '0' && c <= '9' && latin[v][2] != 0) {
        return latin[v][2] + c - '0';
    }
    if (v <= OV_BOLD_ITALIC && c >= 0x391 && c <= 0x3a9 && c != 0x3a2) {
        return greek[v][0] + c - 0x391;
    }
    if (v <= OV_BOLD_ITALIC && c >= 0x3b1 && c <= 0x3c9) {
        return greek[v][1] + c - 0x3b1;
    }
    return c;
}

/* -------------------------------------------------------------- items  */

/** @brief Room for one item more, cleared.  NULL out of memory. */
static struct of_mitem *
item(ml_t *ml)
{
    struct of_mitem *t;
    int cap;

    if (ml->nit == ml->itcap) {
        cap = ml->itcap ? ml->itcap * 2 : 64;
        if (cap > (1 << 18) ||
            (t = realloc(ml->it, (size_t)cap * sizeof *t)) == NULL) {
            ml->bad = 1;
            return NULL;
        }
        ml->it = t;
        ml->itcap = cap;
    }
    memset(&ml->it[ml->nit], 0, sizeof ml->it[0]);
    ml->it[ml->nit].color = ml->color;
    return &ml->it[ml->nit++];
}

/** @brief A path's point: @p op at (@p x, @p y).  @return 0, or -1. */
static int
point(ml_t *ml, int op, float x, float y)
{
    float *t;
    int cap;

    if (ml->npt + 3 > ml->ptcap) {
        cap = ml->ptcap ? ml->ptcap * 2 : 96;
        if (cap > (1 << 20) ||
            (t = realloc(ml->pt, (size_t)cap * sizeof *t)) == NULL) {
            ml->bad = 1;
            return -1;
        }
        ml->pt = t;
        ml->ptcap = cap;
    }
    ml->pt[ml->npt++] = (float)op;
    ml->pt[ml->npt++] = x;
    ml->pt[ml->npt++] = y;
    return 0;
}

void
ml_empty(ml_t *ml, mb_t *b)
{
    memset(b, 0, sizeof *b);
    b->from = b->to = ml->nit;
}

void
ml_shift(ml_t *ml, mb_t *b, float dx, float dy)
{
    int i, j;

    for (i = b->from; i < b->to && i < ml->nit; i++) {
        ml->it[i].x += dx;
        ml->it[i].y += dy;
        for (j = 0; ml->it[i].kind == MI_PATH && j < ml->it[i].npt; j++) {
            ml->pt[(ml->it[i].pt + j) * 3 + 1] += dx;
            ml->pt[(ml->it[i].pt + j) * 3 + 2] += dy;
        }
    }
}

void
ml_rule(ml_t *ml, float x, float y, float w, float h)
{
    struct of_mitem *it = item(ml);

    if (it != NULL) {
        it->kind = MI_RULE;
        it->x = x;
        it->y = y;
        it->w = w;
        it->h = h;
    }
}

void
ml_append(ml_t *ml, mb_t *into, mb_t *b, float gap)
{
    ml_shift(ml, b, into->w + gap, 0);
    if (into->to == into->from) {   /* nothing yet: start at b's */
        into->from = b->from;
    }
    into->to = b->to > into->to ? b->to : into->to;
    into->w += gap + b->w;
    into->asc = fmaxf(into->asc, b->asc);
    into->desc = fmaxf(into->desc, b->desc);
    into->ic = b->ic;
    into->italic = b->italic;
    into->rcls = b->rcls;
    into->glyph = 0;
}

void
ml_order(ml_t *ml, int start, const mb_t *seg, int nseg)
{
    int n = ml->nit - start, k = 0, i, j;
    struct of_mitem *tmp = n > 1 ? malloc((size_t)n * sizeof *tmp) : NULL;
    unsigned char *used = tmp != NULL ? calloc((size_t)n, 1) : NULL;

    for (i = 0; used != NULL && i < nseg; i++) {
        for (j = seg[i].from; j < seg[i].to && j < ml->nit; j++) {
            if (j >= start && !used[j - start]) {
                used[j - start] = 1;
                tmp[k++] = ml->it[j];
            }
        }
    }
    for (j = 0; used != NULL && j < n; j++) {   /* anything else after */
        if (!used[j]) {
            tmp[k++] = ml->it[start + j];
        }
    }
    if (used != NULL) {
        memcpy(&ml->it[start], tmp, (size_t)n * sizeof *tmp);
    }
    free(tmp);
    free(used);
}

float
ml_c(const ml_t *ml, int which, const ms_t *st)
{
    return mf_const(ml->f, which, st->size);
}

ms_t
ml_smaller(const ml_t *ml, const ms_t *st, int cramp)
{
    ms_t s = *st;

    s.level = st->level <= 1 ? 2 : 3;
    s.cramped = st->cramped || cramp;
    s.size = ml->base * mf_const(ml->f, s.level == 2 ? MC_SCRIPT_PCT
                                                     : MC_SCRIPTSCRIPT_PCT,
                                 0) / 100;
    s.size = s.size > 1 ? s.size : 1;
    return s;
}

/* ------------------------------------------------------------- tokens  */

/** @brief Glyph of character @p cp at style @p st, standing for @p was,
 *         put at the end of piece @p b.  @return 0, or -1 when no face
 *         draws it. */
static int
glyph(ml_t *ml, uint32_t cp, uint32_t was, int slant, uint32_t color,
      const ms_t *st, mb_t *b)
{
    of_faces_t *fs = ml->f->fs;
    struct of_mitem *it;
    unsigned gid;
    int face = of_face_for(fs, ml->f->face, cp, &gid);
    float k, ink[4];

    if (gid == 0 || (it = item(ml)) == NULL) {
        return -1;
    }
    k = st->size / (float)(fs->face[face].upem > 0 ? fs->face[face].upem
                                                   : 1000);
    mf_ink(ml->f, face, gid, ink);
    it->kind = MI_GLYPH;
    it->face = face;
    it->gid = gid;
    it->cp = was;
    it->color = color;
    it->size = st->size;
    it->italic = (unsigned char)slant;
    it->x = b->w;
    b->asc = fmaxf(b->asc, ink[3] * k);
    b->desc = fmaxf(b->desc, -ink[1] * k);
    b->w += of_face_advance(fs, face, gid, st->size);
    b->ic = face == ml->f->face ? (float)mf_italic(ml->f, gid) * k : 0;
    b->gid = gid;
    b->to = ml->nit;
    return 0;
}

/** @brief Character @p c as UTF-8 in @p out (five bytes of room), ended. */
static void
utf8(uint32_t c, char *out)
{
    int n = c < 0x80 ? 1 : c < 0x800 ? 2 : c < 0x10000 ? 3 : 4, i;

    out[n] = '\0';
    for (i = n - 1; i > 0; i--) {
        out[i] = (char)(0x80 | (c & 0x3f));
        c >>= 6;
    }
    out[0] = (char)(n == 1 ? c : n == 2 ? (0xc0 | c) : n == 3 ? (0xe0 | c)
                                                              : (0xf0 | c));
}

/** @brief Whether @p c is a letter a variant restyles. */
static int
letter(uint32_t c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= 0x391 && c <= 0x3c9);
}

void
ml_text(ml_t *ml, const char *text, int v, uint32_t color, int cls,
        const ms_t *st, mb_t *out)
{
    const char *s = text != NULL ? text : "";
    uint32_t c, as;
    int n = 0, slant, vv;

    ml_empty(ml, out);
    while ((c = next_cp(&s)) != 0 && !ml->bad) {
        c = c == '-' ? 0x2212 : c == '\'' ? 0x2032 : c;  /* minus, prime */
        vv = v != OV_AUTO ? v : letter(c) && (c < 0x391 || c > 0x3a9)
             ? OV_ITALIC : OV_NORMAL;   /* capital Greek stands upright */
        as = styled(c, vv);
        slant = 0;
        if (as != c && tiku_ttf_glyph(ml->f->fs->face[ml->f->face].ttf, as)
            == 0) {          /* the face lacks it: slant the plain glyph */
            slant = vv == OV_ITALIC || vv == OV_BOLD_ITALIC;
            as = c;
        }
        if (glyph(ml, as, c, slant, color, st, out) == 0) {
            out->italic = (unsigned char)(letter(c) &&
                                          (vv == OV_ITALIC ||
                                           vv == OV_BOLD_ITALIC));
            n++;
        }
    }
    out->lcls = out->rcls = (unsigned char)cls;
    out->glyph = (unsigned char)(n == 1);
    if (!out->italic) {
        out->ic = 0;
    }
}

/* --------------------------------------------------------------- rows  */

/** @brief The space between atoms of classes @p l and @p r at style
 *         @p st: TeX's table, in eighteenths of an em, the ones it only
 *         puts at display and text sizes negative. */
static float
space(int l, int r, const ms_t *st)
{
    static const signed char tab[8][8] = {
        {0, 3, -4, -5, 0, 0, 0, -3}, {3, 3, 0, -5, 0, 0, 0, -3},
        {-4, -4, 0, 0, -4, 0, 0, -4}, {-5, -5, 0, 0, -5, 0, 0, -5},
        {0, 0, 0, 0, 0, 0, 0, 0}, {0, 3, -4, -5, 0, 0, 0, -3},
        {-3, -3, 0, -3, -3, -3, -3, -3}, {-3, 3, -4, -5, -3, 0, -3, -3}};
    int v;

    if (l < 0 || l > 7 || r < 0 || r > 7) {
        return 0;
    }
    v = tab[l][r];
    v = v < 0 ? (st->level >= 2 ? 0 : -v) : v;
    return (float)v * st->size / 18;
}

/** @brief TeX's rules 5 and 6 over pieces @p p (@p k of them): a binary
 *         operator with nothing to operate on, on either side, taken as
 *         an ordinary atom; spaces looked past. */
static void
unary(mb_t *p, int k)
{
    int i, prev = -1;

    for (i = 0; i < k; i++) {
        if (p[i].lcls == OC_NONE) {
            continue;
        }
        if (p[i].lcls == OC_BIN &&
            (prev < 0 || p[prev].rcls == OC_BIN || p[prev].rcls == OC_OP ||
             p[prev].rcls == OC_REL || p[prev].rcls == OC_OPEN ||
             p[prev].rcls == OC_PUNCT)) {
            p[i].lcls = p[i].rcls = OC_ORD;
        }
        if (prev >= 0 && p[prev].rcls == OC_BIN &&
            (p[i].lcls == OC_REL || p[i].lcls == OC_CLOSE ||
             p[i].lcls == OC_PUNCT)) {
            p[prev].lcls = p[prev].rcls = OC_ORD;
        }
        prev = i;
    }
    if (prev >= 0 && p[prev].rcls == OC_BIN) {
        p[prev].lcls = p[prev].rcls = OC_ORD;
    }
}

void
ml_row(ml_t *ml, of_mnode_t *const *kid, int nkid, const ms_t *st,
       mb_t *out)
{
    mb_t *p = nkid > 0 ? malloc((size_t)nkid * sizeof *p) : NULL;
    int i, k = 0, prev = -1;
    float gap;

    ml_empty(ml, out);
    for (i = 0; p != NULL && i < nkid; i++) {
        if (kid[i] != NULL && kid[i]->kind != OM_ALIGN) {
            ml_set(ml, kid[i], st, &p[k++]);
        }
    }
    unary(p, k);
    for (i = 0; i < k; i++) {       /* spaced from the last atom, not space */
        gap = p[i].lcls == OC_NONE || prev < 0 ? 0 :
              space(p[prev].rcls, p[i].lcls, st) +
              (p[prev].italic && !p[i].italic ? p[prev].ic : 0);
        ml_append(ml, out, &p[i], gap);
        prev = p[i].lcls == OC_NONE ? prev : i;
    }
    for (i = 0; i < k && p[i].lcls == OC_NONE; i++) {}
    out->lcls = i < k ? p[i].lcls : k > 0 ? OC_NONE : OC_ORD;
    out->rcls = prev >= 0 ? p[prev].rcls : out->lcls;
    out->glyph = (unsigned char)(k == 1 && p[0].glyph);
    out->gid = k == 1 ? p[0].gid : 0;
    free(p);
}

/* ------------------------------------------------------ grown shapes  */

/** @brief A stroked path of @p n points, @p xy (x y pairs, curves' as
 *         three) with operations @p op, @p lw wide. */
static void
stroke(ml_t *ml, const unsigned char *op, const float *xy, int n, float lw)
{
    struct of_mitem *it = item(ml);
    int i, first = ml->npt / 3;

    if (it == NULL) {
        return;
    }
    for (i = 0; i < n; i++) {
        if (point(ml, op[i], xy[2 * i], xy[2 * i + 1]) != 0) {
            ml->nit--;
            return;
        }
    }
    it->kind = MI_PATH;
    it->w = lw;
    it->pt = first;
    it->npt = n;
}

/** @brief Character @p cp drawn as strokes, @p h high (from 0 up) and
 *         @p w wide: a bracket (round, square, angled, a ceiling or a
 *         floor), a bar, a brace or a root.
 *  @return 1 when it was one drawn so, 0 when it was not. */
static int
drawn_upright(ml_t *ml, uint32_t cp, float w, float h, float lw)
{
    static const unsigned char lines[] = {MP_MOVE, MP_LINE, MP_LINE,
                                          MP_LINE, MP_LINE};
    static const unsigned char curve[] = {MP_MOVE, MP_CURVE, MP_CURVE,
                                          MP_CURVE, MP_CURVE, MP_CURVE,
                                          MP_CURVE};
    float l = lw, r = w - lw, m = w / 2, xy[14];

    switch (cp) {
    case '(': case ')':
        xy[0] = cp == '(' ? r : l;  xy[1] = -h;
        xy[2] = cp == '(' ? l : r;  xy[3] = -h * 0.7f;
        xy[4] = cp == '(' ? l : r;  xy[5] = -h * 0.3f;
        xy[6] = cp == '(' ? r : l;  xy[7] = 0;
        stroke(ml, curve, xy, 4, lw);
        return 1;
    case '[': case ']': case 0x2308: case 0x2309: case 0x230a: case 0x230b:
        xy[0] = xy[6] = cp == '[' || cp == 0x2308 || cp == 0x230a ? r : l;
        xy[2] = xy[4] = cp == '[' || cp == 0x2308 || cp == 0x230a ? l : r;
        xy[1] = xy[3] = -h;
        xy[5] = xy[7] = 0;
        if (cp == 0x2308 || cp == 0x2309) {         /* a ceiling */
            xy[6] = xy[4];
        } else if (cp == 0x230a || cp == 0x230b) {  /* a floor */
            xy[0] = xy[2];
        }
        stroke(ml, lines, xy, 4, lw);
        return 1;
    case '|': case 0x2223: case 0x2016: case 0x2225:
        xy[0] = xy[2] = cp == 0x2016 || cp == 0x2225 ? m - lw * 1.5f : m;
        xy[1] = -h;
        xy[3] = 0;
        stroke(ml, lines, xy, 2, lw);
        if (cp == 0x2016 || cp == 0x2225) {
            xy[0] = xy[2] = m + lw * 1.5f;
            stroke(ml, lines, xy, 2, lw);
        }
        return 1;
    case 0x27e8: case 0x2329: case 0x3008: case 0x27e9: case 0x232a:
    case 0x3009:
        xy[0] = xy[4] = cp == 0x27e8 || cp == 0x2329 || cp == 0x3008 ? r : l;
        xy[2] = cp == 0x27e8 || cp == 0x2329 || cp == 0x3008 ? l : r;
        xy[1] = -h;
        xy[3] = -h / 2;
        xy[5] = 0;
        stroke(ml, lines, xy, 3, lw);
        return 1;
    case '{': case '}':
        xy[0] = cp == '{' ? r : l;   xy[1] = -h;
        xy[2] = m;                   xy[3] = -h;
        xy[4] = m;                   xy[5] = -h / 2;
        xy[6] = cp == '{' ? l : r;   xy[7] = -h / 2;
        xy[8] = m;                   xy[9] = -h / 2;
        xy[10] = m;                  xy[11] = 0;
        xy[12] = cp == '{' ? r : l;  xy[13] = 0;
        stroke(ml, curve, xy, 7, lw);
        return 1;
    case 0x221a:                    /* a root: tick, down, up to its top */
        xy[0] = 0;          xy[1] = -h * 0.42f;
        xy[2] = w * 0.22f;  xy[3] = -h * 0.5f;
        xy[4] = w * 0.55f;  xy[5] = 0;
        xy[6] = w;          xy[7] = -h;
        stroke(ml, lines, xy, 4, lw);
        return 1;
    default:
        return 0;
    }
}

/** @brief Character @p cp drawn as strokes across @p w, @p h high (from
 *         0 up): a brace over or under, an arrow, a hat, a tilde, a bar.
 *  @return 1 when it was one drawn so, 0 when it was not. */
static int
drawn_across(ml_t *ml, uint32_t cp, float w, float h, float lw)
{
    static const unsigned char lines[] = {MP_MOVE, MP_LINE, MP_LINE,
                                          MP_MOVE, MP_LINE, MP_LINE};
    static const unsigned char curve[] = {MP_MOVE, MP_CURVE, MP_CURVE,
                                          MP_CURVE, MP_CURVE, MP_CURVE,
                                          MP_CURVE};
    int under = cp == 0x23df || cp == 0xfe38 || cp == 0x23b5;
    float y0 = under ? -h : 0, y1 = under ? 0 : -h, m = h / 2, xy[14];

    if (cp == 0x23de || cp == 0x23df || cp == 0xfe37 || cp == 0xfe38) {
        xy[0] = 0;          xy[1] = y0;     /* a brace's two halves */
        xy[2] = 0;          xy[3] = -m;
        xy[4] = w / 2;      xy[5] = -m;
        xy[6] = w / 2;      xy[7] = y1;
        xy[8] = w / 2;      xy[9] = -m;
        xy[10] = w;         xy[11] = -m;
        xy[12] = w;         xy[13] = y0;
        stroke(ml, curve, xy, 7, lw);
    } else if (cp == 0x2192 || cp == 0x20d7 || cp == 0x2190 ||
               cp == 0x20d6 || cp == 0x2194 || cp == 0x20e1) {
        int left = cp == 0x2190 || cp == 0x20d6 || cp == 0x2194 ||
                   cp == 0x20e1;
        int right = !left || cp == 0x2194 || cp == 0x20e1;

        xy[0] = 0;  xy[1] = -m;  xy[2] = w;  xy[3] = -m;
        stroke(ml, lines, xy, 2, lw);
        xy[0] = w - h;  xy[1] = 0;  xy[2] = w;  xy[3] = -m;
        xy[4] = w - h;  xy[5] = -h;
        if (right) {
            stroke(ml, lines, xy, 3, lw);
        }
        xy[0] = xy[4] = h;
        xy[2] = 0;
        if (left) {
            stroke(ml, lines, xy, 3, lw);
        }
    } else if (cp == '^' || cp == 0x302 || cp == 0x2c6 || cp == 0x23b4 ||
               cp == 0x23b5) {
        xy[0] = 0;  xy[1] = cp == 0x23b5 ? -h : 0;
        xy[2] = cp == 0x23b4 || cp == 0x23b5 ? 0 : w / 2;
        xy[3] = cp == 0x23b5 ? 0 : -h;
        xy[4] = cp == 0x23b4 || cp == 0x23b5 ? w : w;
        xy[5] = cp == 0x23b4 ? -h : cp == 0x23b5 ? 0 : 0;
        stroke(ml, lines, xy, 3, lw);
        if (cp == 0x23b4 || cp == 0x23b5) {     /* its other end */
            xy[0] = w;  xy[1] = cp == 0x23b5 ? 0 : -h;
            xy[2] = w;  xy[3] = cp == 0x23b5 ? -h : 0;
            stroke(ml, lines, xy, 2, lw);
        }
    } else if (cp == '~' || cp == 0x303 || cp == 0x2dc) {
        xy[0] = 0;          xy[1] = -h * 0.3f;
        xy[2] = w * 0.25f;  xy[3] = -h * 1.2f;
        xy[4] = w * 0.75f;  xy[5] = h * 0.2f;
        xy[6] = w;          xy[7] = -h * 0.7f;
        stroke(ml, curve, xy, 4, lw);
    } else if (cp == 0xaf || cp == 0x203e || cp == 0x305 || cp == 0x332 ||
               cp == '_' || cp == 0x2015 || cp == 0x2212) {     /* a bar */
        ml_rule(ml, 0, -m - lw / 2, w, lw);
    } else {
        return 0;
    }
    return 1;
}

/** @brief Parts @p p of the math face, standing for @p cp, placed in
 *         piece @p out: one bigger glyph on its own baseline, or parts end
 *         to end from 0 (upward, or rightward). */
static void
place_parts(ml_t *ml, const mf_parts_t *p, uint32_t cp, int vert,
            const ms_t *st, mb_t *out)
{
    const of_face_t *fc = &ml->f->fs->face[ml->f->face];
    float k = st->size / (float)ml->f->upem, ink[4];
    struct of_mitem *it;
    int i;

    for (i = 0; i < p->n && (it = item(ml)) != NULL; i++) {
        mf_ink(ml->f, ml->f->face, p->gid[i], ink);
        it->kind = MI_GLYPH;
        it->face = ml->f->face;
        it->gid = p->gid[i];
        it->cp = i == 0 ? cp : 0;   /* found as the one character */
        it->size = st->size;
        if (!p->built) {            /* a glyph of its own */
            out->asc = ink[3] * k;
            out->desc = -ink[1] * k;
            out->w = (float)tiku_ttf_advance(fc->ttf, p->gid[i]) * k;
            out->gid = p->gid[i];
            out->glyph = 1;
        } else if (vert) {          /* its ink's foot at its place */
            it->y = -((float)p->at[i] - ink[1]) * k;
            out->w = fmaxf(out->w, (float)tiku_ttf_advance(fc->ttf,
                                                           p->gid[i]) * k);
        } else {
            it->x = (float)p->at[i] * k;
            out->asc = fmaxf(out->asc, ink[3] * k);
            out->desc = fmaxf(out->desc, -ink[1] * k);
        }
    }
    if (p->built) {
        out->asc = vert ? (float)p->size * k : out->asc;
        out->w = vert ? out->w : (float)p->size * k;
    }
    out->to = ml->nit;
}

void
ml_stretch(ml_t *ml, uint32_t cp, int vert, float need, const ms_t *st,
           mb_t *out)
{
    const of_face_t *fc = &ml->f->fs->face[ml->f->face];
    unsigned gid = tiku_ttf_glyph(fc->ttf, cp);
    float k = st->size / (float)ml->f->upem;
    float lw = fmaxf(ml_c(ml, MC_FRAC_RULE, st), st->size * 0.04f);
    float wide = vert ? st->size * 0.42f : need;
    float high = vert ? need : st->size * 0.3f;
    mf_parts_t p;
    char utf[8];
    int end, from;

    ml_empty(ml, out);
    if (gid != 0 && mf_stretch(ml->f, gid, vert, (int)(need / k), &p) == 0) {
        place_parts(ml, &p, cp, vert, st, out);
        return;
    }
    utf8(cp, utf);
    ml_text(ml, utf, OV_NORMAL, ml->color, OC_ORD, st, out);
    end = ml->nit;
    if ((vert ? out->asc + out->desc : out->w) >= need * 0.9f ||
        need <= st->size * 1.1f ||
        !(vert ? drawn_upright(ml, cp, wide, high, lw)
               : drawn_across(ml, cp, wide, high, lw))) {
        return;                     /* big enough, or no shape for it */
    }
    from = out->from;               /* the drawing alone, the glyph gone */
    memmove(&ml->it[from], &ml->it[end],
            (size_t)(ml->nit - end) * sizeof *ml->it);
    ml->nit -= end - from;
    ml_empty(ml, out);
    out->from = from;
    out->w = wide;
    out->asc = high;
}

void
ml_fit(ml_t *ml, uint32_t cp, float room, const ms_t *st, mb_t *out)
{
    const of_face_t *fc = &ml->f->fs->face[ml->f->face];
    unsigned gid = tiku_ttf_glyph(fc->ttf, cp);
    mf_parts_t p;

    if (gid == 0 || !ml->f->has_math) {
        ml_stretch(ml, cp, 0, 0, st, out);
        return;
    }
    memset(&p, 0, sizeof p);
    p.n = 1;
    p.gid[0] = mf_fit(ml->f, gid, 0, (int)(room * (float)ml->f->upem /
                                           st->size));
    ml_empty(ml, out);
    place_parts(ml, &p, cp, 0, st, out);
}

/* ------------------------------------------------------- the others  */

/** @brief A box (a border round it when it asks), a phantom (nothing
 *         drawn, or its extent taken away), a style, or a space. */
static void
plain(ml_t *ml, const of_mnode_t *n, const ms_t *st, mb_t *out)
{
    ms_t s = *st;
    float pad = st->size * 0.1f, lw = st->size * 0.04f;

    if (n->kind == OM_SPACE) {      /* -4 to 40 ems, whatever is written */
        ml_empty(ml, out);
        out->w = isfinite(n->em) ? fmaxf(-4, fminf(n->em, 40)) * st->size
                                 : 0;
        out->lcls = out->rcls = OC_NONE;
        return;
    }
    if (n->kind == OM_STYLE) {
        s.level = n->flags & OMF_DISPLAY ? 0 : n->flags & OMF_INLINE &&
                  s.level == 0 ? 1 : s.level;
        s = n->flags & OMF_SCRIPT ? ml_smaller(ml, &s, 0) : s;
    }
    ml_set(ml, n->nkid > 0 ? n->kid[0] : NULL, &s, out);
    if (n->kind == OM_PHANTOM) {
        ml->nit = (n->flags & OMF_SHOW) ? ml->nit : out->from;
        out->to = ml->nit;
        out->w = n->flags & OMF_ZEROW ? 0 : out->w;
        if (n->flags & OMF_ZEROH) {
            out->asc = out->desc = 0;
        }
    } else if (n->kind == OM_BOX && (n->flags & OMF_BORDER)) {
        ml_shift(ml, out, pad, 0);
        ml_rule(ml, 0, -out->asc - pad, out->w + 2 * pad, lw);
        ml_rule(ml, 0, out->desc + pad - lw, out->w + 2 * pad, lw);
        ml_rule(ml, 0, -out->asc - pad, lw, out->asc + out->desc + 2 * pad);
        ml_rule(ml, out->w + 2 * pad - lw, -out->asc - pad, lw,
                out->asc + out->desc + 2 * pad);
        out->w += 2 * pad;
        out->asc += pad;
        out->desc += pad;
        out->to = ml->nit;
    }
}

void
ml_set(ml_t *ml, const of_mnode_t *n, const ms_t *st, mb_t *out)
{
    uint32_t was = ml->color;

    if (n == NULL || ml->depth > OM_DEPTH || ml->bad) {
        ml_empty(ml, out);
        return;
    }
    ml->color = n->color != 0 ? n->color : ml->color;   /* and within */
    ml->depth++;
    switch (n->kind) {
    case OM_TOKEN:
        ml_text(ml, n->text, n->variant, n->color ? n->color : ml->color,
                n->cls, st, out);
        break;
    case OM_ROW:
        ml_row(ml, n->kid, n->nkid, st, out);
        break;
    case OM_FRAC:
        ml_frac(ml, n, st, out);
        break;
    case OM_SCRIPTS:
        ml_scripts(ml, n, st, out);
        break;
    case OM_ROOT:
        ml_root(ml, n, st, out);
        break;
    case OM_NARY:
        ml_nary(ml, n, st, out);
        break;
    case OM_FENCE:
        ml_fence(ml, n, st, out);
        break;
    case OM_MATRIX: case OM_ARRAY:
        ml_grid(ml, n, st, out);
        break;
    case OM_ACCENT: case OM_UNDEROVER: case OM_BAR: case OM_BRACE:
        ml_over(ml, n, st, out);
        break;
    default:
        plain(ml, n, st, out);
        break;
    }
    ml->depth--;
    ml->color = was;
}

/* ------------------------------------------------------ set and drawn  */

/** @brief Equation @p m set with face @p f, what draws it kept in @p a.
 *  @return 0, or -1 out of memory. */
static int
set_one(mf_t *f, of_math_t *m, of_arena_t *a)
{
    ml_t ml;
    ms_t st;
    mb_t b;
    int err = 0;

    memset(&ml, 0, sizeof ml);
    ml.f = f;
    ml.color = m->color;
    ml.base = m->size;
    st.level = m->display ? 0 : 1;
    st.cramped = 0;
    st.size = m->size;
    ml_set(&ml, m->root, &st, &b);
    m->w = b.w;
    m->asc = b.asc;
    m->desc = b.desc;
    if (!ml.bad && ml.nit > 0) {
        m->item = of_alloc(a, (size_t)ml.nit * sizeof *m->item);
        m->pt = ml.npt > 0 ? of_alloc(a, (size_t)ml.npt * sizeof *m->pt)
                           : NULL;
        err = m->item == NULL || (ml.npt > 0 && m->pt == NULL) ? -1 : 0;
    }
    if (err == 0 && m->item != NULL) {
        memcpy(m->item, ml.it, (size_t)ml.nit * sizeof *m->item);
        m->nitem = ml.nit;
        if (ml.npt > 0) {
            memcpy(m->pt, ml.pt, (size_t)ml.npt * sizeof *m->pt);
            m->npt = ml.npt;
        }
    }
    free(ml.it);
    free(ml.pt);
    return err;
}

int
of_math_prepare(of_doc_t *d, of_faces_t *fs)
{
    of_math_t *m;
    mf_t f;
    int err = 0;

    for (m = d->maths; m != NULL && m->set; m = m->next) {}
    if (m == NULL || mf_open(&f, fs, NULL) != 0) {
        return 0;                   /* all set, or no faces to set them in */
    }
    for (m = d->maths; m != NULL && err == 0; m = m->next) {
        err = m->set ? 0 : set_one(&f, m, &d->arena);
        m->set = 1;
    }
    mf_close(&f);
    return err;
}

/** @brief Glyph items from @p i on that share a face, size, line,
 *         colour and slant drawn as one run.  @return the item after. */
static int
glyph_run(of_faces_t *fs, const of_math_t *m, int i, float x, float y,
          tiku_pdf_writer_t *w)
{
    const struct of_mitem *a = &m->item[i];
    tiku_pdf_glyph_t g[64];
    of_frag_t fr;
    int n = 0;

    while (i < m->nitem && n < 64 && m->item[i].kind == MI_GLYPH &&
           m->item[i].face == a->face && m->item[i].size == a->size &&
           m->item[i].y == a->y && m->item[i].color == a->color &&
           m->item[i].italic == a->italic) {
        g[n].gid = m->item[i].gid;
        g[n].cp = m->item[i].cp;
        g[n++].x = m->item[i].x - a->x;
        i++;
    }
    memset(&fr, 0, sizeof fr);
    fr.face = a->face;
    fr.size = a->size;
    fr.color = a->color;
    fr.italic = a->italic;
    fr.bold = (unsigned char)fs->face[a->face].f.bold_synth;
    fr.x = x + a->x;
    fr.y = y + a->y;
    fr.g = g;
    fr.ng = n;
    if (a->face >= 0 && a->face < fs->nface) {
        of_draw_glyphs(fs, &fr, 0, 0, w);
    }
    return i;
}

/** @brief Path item @p it stroked, moved by (@p x, @p y). */
static void
path_item(const of_math_t *m, const struct of_mitem *it, float x, float y,
          tiku_pdf_writer_t *w)
{
    tiku_pdf_paint_t p;
    const float *q;
    int j;

    for (j = 0; j < it->npt && (it->pt + j + 1) * 3 <= m->npt; j++) {
        q = &m->pt[(it->pt + j) * 3];
        if (q[0] == MP_MOVE) {
            tiku_pdf_writer_move(w, x + q[1], y + q[2]);
        } else if (q[0] == MP_LINE) {
            tiku_pdf_writer_line(w, x + q[1], y + q[2]);
        } else if (j + 2 < it->npt && (it->pt + j + 3) * 3 <= m->npt) {
            tiku_pdf_writer_curve(w, x + q[1], y + q[2], x + q[4],
                                  y + q[5], x + q[7], y + q[8]);
            j += 2;
        }
    }
    memset(&p, 0, sizeof p);
    p.stroke = it->color;
    p.width = it->w;
    p.cap = p.join = 1;
    tiku_pdf_writer_paint(w, &p);
}

void
of_math_draw(of_faces_t *fs, const of_math_t *m, float x, float y,
             tiku_pdf_writer_t *w)
{
    tiku_pdf_paint_t p;
    int i = 0;

    while (m != NULL && i < m->nitem) {
        const struct of_mitem *it = &m->item[i];

        if (it->kind == MI_GLYPH) {
            i = glyph_run(fs, m, i, x, y, w);
            continue;
        }
        if (it->kind == MI_RULE && it->w > 0 && it->h > 0) {
            memset(&p, 0, sizeof p);
            p.fill = it->color;
            tiku_pdf_writer_rect(w, x + it->x, y + it->y, it->w, it->h);
            tiku_pdf_writer_paint(w, &p);
        } else if (it->kind == MI_PATH) {
            path_item(m, it, x, y, w);
        }
        i++;
    }
}
