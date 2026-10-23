/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_jbig2.c - JBIG2 (ITU-T T.88): a page from its segments.
 *
 * A stream is segments, each a header and its data (7.2): dictionaries
 * of symbols and of patterns, Huffman tables, and regions -- coded whole,
 * as symbols placed, as halftone cells, or refined from another -- each
 * drawn onto the page its page information segment makes.  A PDF stream
 * carries one page's segments; the segments its pages share come
 * separately, as JBIG2Globals.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_jbig2.h"
#include "tiku_jbig2_private.h"

#include <stdlib.h>
#include <string.h>

#define SEGS_MAX    65536           /* segments one decode holds        */
#define REFS_MAX    4096            /* segments one refers to           */
#define PIXELS_MAX  (1LL << 27)     /* every bitmap's pixels together   */
#define SIDE_MAX    (1L << 24)      /* a page's or a region's side      */

/** @brief Big-endian 16 bits. */
static uint32_t
be16(const unsigned char *p)
{
    return (uint32_t)p[0] << 8 | p[1];
}

uint32_t
jb_be32(const unsigned char *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 |
           (uint32_t)p[2] << 8 | p[3];
}

int
jb_log2(uint32_t n)
{
    int k = 0;

    while (k < 32 && ((uint64_t)1 << k) < n) {
        k++;
    }
    return k;
}

jb_bm_t *
jb_new(jb_dec_t *jd, long w, long h, int fill)
{
    jb_bm_t *b, **grown;

    if (w < 0 || h < 0 || w > SIDE_MAX || h > SIDE_MAX ||
        jd->pixels + (long long)w * h > PIXELS_MAX) {
        return NULL;
    }
    if (jd->npool == jd->cappool) {
        long cap = jd->cappool ? jd->cappool * 2 : 64;

        grown = realloc(jd->pool, (size_t)cap * sizeof *grown);
        if (grown == NULL) {
            return NULL;
        }
        jd->pool = grown;
        jd->cappool = cap;
    }
    if ((b = jb_bm_new((int)w, (int)h, fill)) == NULL) {
        return NULL;
    }
    jd->pool[jd->npool++] = b;
    jd->pixels += (long long)w * h;
    return b;
}

int
jb_work(jb_dec_t *jd, long long pixels)
{
    jd->work += pixels;
    return jd->work > JB_WORK_MAX;
}

jb_seg_t *
jb_referred(jb_dec_t *jd, const jb_seg_t *s, int i)
{
    long k;

    for (k = (long)(s - jd->seg) - 1; i < s->nref && k >= 0; k--) {
        if (jd->seg[k].num == s->ref[i]) {
            return &jd->seg[k];
        }
    }
    return NULL;
}

int
jb_rinfo(const unsigned char *p, long n, jb_rinfo_t *r)
{
    if (n < 17) {
        return -1;
    }
    r->w = jb_be32(p);
    r->h = jb_be32(p + 4);
    r->x = (int32_t)jb_be32(p + 8);
    r->y = (int32_t)jb_be32(p + 12);
    r->op = p[16] & 7;
    return r->op > 4 ? -1 : 0;
}

/* ------------------------------------------------------------ the page  */

/** @brief A page of unknown height grown to @p rows, the new rows its
 *         default pixel (7.4.8.2). */
static int
grow(jb_dec_t *jd, long long rows)
{
    jb_bm_t *pg = jd->page;
    long long more = rows - pg->h;
    unsigned char *px;

    if (jd->page_known || more <= 0) {
        return 0;
    }
    if (rows > SIDE_MAX || jd->pixels + more * pg->w > PIXELS_MAX ||
        (long long)(pg->w + 7) / 8 * rows > jd->omax ||
        (px = realloc(pg->px, (size_t)rows * (size_t)pg->w + 1)) == NULL) {
        return -1;
    }
    memset(px + (size_t)pg->h * (size_t)pg->w, jd->defpixel,
           (size_t)more * (size_t)pg->w);
    jd->pixels += more * pg->w;
    pg->px = px;
    pg->h = (int)rows;
    return 0;
}

int
jb_place(jb_dec_t *jd, jb_seg_t *s, jb_bm_t *b, const jb_rinfo_t *r)
{
    if (s->type == 4 || s->type == 20 || s->type == 36 || s->type == 40) {
        s->region = b;              /* intermediate: for a refinement */
        return 0;
    }
    if (jd->page == NULL || grow(jd, (long long)r->y + b->h) != 0 ||
        jb_work(jd, (long long)b->w * b->h)) {
        return -1;
    }
    jb_compose(jd->page, b, r->x, r->y, jd->override ? r->op : jd->defop);
    return 0;
}

/** @brief Page information (7.4.8): the page, its default pixel and how
 *         regions combine onto it. */
static int
page_info(jb_dec_t *jd, const jb_seg_t *s)
{
    const unsigned char *p = s->data;
    uint32_t w, h;

    if (s->len < 19 || jd->page != NULL) {
        return -1;
    }
    w = jb_be32(p);
    h = jb_be32(p + 4);
    jd->defpixel = (p[16] >> 2) & 1;
    jd->defop = (p[16] >> 3) & 3;
    jd->override = (p[16] >> 6) & 1;
    jd->page_known = h != 0xFFFFFFFFu;
    if (w == 0 || w > SIDE_MAX || (jd->page_known && (h > SIDE_MAX ||
        (long long)(w + 7) / 8 * h > jd->omax))) {
        return -1;                  /* more than omax holds: no page */
    }
    jd->page = jb_new(jd, w, jd->page_known ? h : 0, jd->defpixel);
    return jd->page != NULL ? 0 : -1;
}

/** @brief End of stripe (7.4.10): the page reaches at least its row. */
static int
stripe(jb_dec_t *jd, const jb_seg_t *s)
{
    if (s->len < 4 || jd->page == NULL) {
        return -1;
    }
    return grow(jd, (long long)jb_be32(s->data) + 1);
}

/* ------------------------------------------------------------- regions  */

/** @brief AT pixels, @p n signed bytes at @p p, into @p at. */
static void
at_pixels(const unsigned char *p, int n, int *at)
{
    int i;

    for (i = 0; i < n; i++) {
        at[i] = (signed char)p[i];
    }
}

/** @brief A generic region segment (7.4.6): its bitmap coded whole. */
static int
generic(jb_dec_t *jd, jb_seg_t *s)
{
    const unsigned char *p = s->data;
    long n = s->len, at = 18;
    jb_rinfo_t ri;
    jb_gen_t g;
    jb_bm_t *b;
    tiku_mq_cx_t *gb;
    tiku_mq_t d;
    int rc;

    memset(&g, 0, sizeof g);
    if (jb_rinfo(p, n, &ri) || n < 18 || (p[17] & 0x10)) {
        return -1;                  /* EXTTEMPLATE is not read */
    }
    g.mmr = p[17] & 1;
    g.tmpl = (p[17] >> 1) & 3;
    g.tpgdon = (p[17] >> 3) & 1;
    if (!g.mmr) {
        if (at + (g.tmpl == 0 ? 8 : 2) > n) {
            return -1;
        }
        at_pixels(p + at, g.tmpl == 0 ? 8 : 2, g.at);
        at += g.tmpl == 0 ? 8 : 2;
    }
    if (ri.h == 0xFFFFFFFFu && n >= at + 4) {
        ri.h = jb_be32(p + n - 4);  /* the height its end gives */
        n -= 4;
    }
    if ((b = jb_new(jd, ri.w, ri.h, 0)) == NULL) {
        return -1;
    }
    if (g.mmr) {
        rc = jb_generic_mmr(p + at, n - at, b) < 0 ? -1 : 0;
    } else if ((gb = calloc(65536, sizeof *gb)) != NULL) {
        tiku_mq_init(&d, p + at, n - at);
        rc = jb_generic(&d, gb, &g, b);
        free(gb);
    } else {
        return -1;
    }
    return jb_place(jd, s, b, &ri) != 0 ? -1 : rc;
}

/** @brief The page as it stands under region @p ri, as a bitmap: what a
 *         refinement with no region to refer to refines. */
static jb_bm_t *
page_under(jb_dec_t *jd, const jb_rinfo_t *ri)
{
    jb_bm_t *b;
    long x, y;

    if (jd->page == NULL || (b = jb_new(jd, ri->w, ri->h, 0)) == NULL) {
        return NULL;
    }
    for (y = 0; y < b->h; y++) {
        for (x = 0; x < b->w; x++) {
            b->px[(size_t)y * (size_t)b->w + (size_t)x] = (unsigned char)
                jb_px(jd->page, (long)ri->x + x, (long)ri->y + y);
        }
    }
    return b;
}

/** @brief A generic refinement region segment (7.4.7): a bitmap coded
 *         against the region it refers to, or the page under it. */
static int
refinement(jb_dec_t *jd, jb_seg_t *s)
{
    const unsigned char *p = s->data;
    long n = s->len, at = 18;
    jb_rinfo_t ri;
    jb_ref_t r;
    jb_seg_t *from;
    jb_bm_t *b;
    tiku_mq_cx_t *gr;
    tiku_mq_t d;
    int rc;

    memset(&r, 0, sizeof r);
    if (jb_rinfo(p, n, &ri) || n < 18) {
        return -1;
    }
    r.tmpl = p[17] & 1;
    r.tpgron = (p[17] >> 1) & 1;
    if (r.tmpl == 0) {
        if (at + 4 > n) {
            return -1;
        }
        at_pixels(p + at, 4, r.at);
        at += 4;
    }
    from = s->nref > 0 ? jb_referred(jd, s, 0) : NULL;
    r.ref = from != NULL ? from->region : page_under(jd, &ri);
    if (r.ref == NULL || (b = jb_new(jd, ri.w, ri.h, 0)) == NULL ||
        (gr = calloc(8192, sizeof *gr)) == NULL) {
        return -1;
    }
    tiku_mq_init(&d, p + at, n - at);
    rc = jb_refine(&d, gr, &r, b);
    free(gr);
    return jb_place(jd, s, b, &ri) != 0 ? -1 : rc;
}

/** @brief @p count patterns of @p pw by @p ph cut from @p all, where
 *         they sit side by side, into @p s. */
static int
cut_patterns(jb_dec_t *jd, jb_seg_t *s, const jb_bm_t *all, long count,
             int pw, int ph)
{
    long i, y;

    if ((s->pat = malloc((size_t)count * sizeof *s->pat)) == NULL) {
        return -1;
    }
    for (i = 0; i < count; i++) {
        if ((s->pat[i] = jb_new(jd, pw, ph, 0)) == NULL) {
            return -1;
        }
        for (y = 0; y < ph; y++) {
            memcpy(s->pat[i]->px + y * pw, all->px + y * all->w + i * pw,
                   (size_t)pw);
        }
        s->npat = i + 1;
    }
    return 0;
}

/** @brief The patterns of a pattern dictionary (7.4.4, 6.7): one bitmap
 *         of all of them side by side, cut apart. */
static int
patterns(jb_dec_t *jd, jb_seg_t *s)
{
    const unsigned char *p = s->data;
    jb_gen_t g;
    jb_bm_t *all;
    tiku_mq_cx_t *gb;
    tiku_mq_t d;
    long count;
    int rc = -1;

    if (s->len < 7 || (p[1] == 0 || p[2] == 0) || jb_be32(p + 3) >= 65536) {
        return -1;
    }
    memset(&g, 0, sizeof g);
    g.mmr = p[0] & 1;
    g.tmpl = (p[0] >> 1) & 3;
    count = (long)jb_be32(p + 3) + 1;
    g.at[0] = -p[1];                /* A1 one pattern back, A2-A4 nominal */
    g.at[2] = -3;
    g.at[3] = -1;
    g.at[4] = 2;
    g.at[5] = -2;
    g.at[6] = -2;
    g.at[7] = -2;
    if ((all = jb_new(jd, count * p[1], p[2], 0)) == NULL) {
        return -1;
    }
    if (g.mmr) {
        rc = jb_generic_mmr(p + 7, s->len - 7, all) < 0 ? -1 : 0;
    } else if ((gb = calloc(65536, sizeof *gb)) != NULL) {
        tiku_mq_init(&d, p + 7, s->len - 7);
        rc = jb_generic(&d, gb, &g, all);
        free(gb);
    }
    return rc == 0 ? cut_patterns(jd, s, all, count, p[1], p[2]) : -1;
}

/**
 * @brief A halftone's grey values (Annex C.5): bitplanes of Gray code,
 *        the most significant first, each a generic region; a pixel of
 *        @p skip is not coded.  @return w * h values, or NULL.
 */
static uint32_t *
greys(jb_dec_t *jd, const unsigned char *p, long n, jb_gen_t *g, int bpp,
      long w, long h)
{
    jb_bm_t *plane[32];
    uint32_t *v = calloc((size_t)w * (size_t)h + 1, sizeof *v);
    tiku_mq_cx_t *gb = g->mmr ? NULL : calloc(65536, sizeof *gb);
    tiku_mq_t d;
    long at = 0, used, i;
    int j, ok = v != NULL && (g->mmr || gb != NULL);

    g->at[0] = g->tmpl <= 1 ? 3 : 2;
    g->at[1] = -1;
    g->at[2] = -3;
    g->at[3] = -1;
    g->at[4] = 2;
    g->at[5] = -2;
    g->at[6] = -2;
    g->at[7] = -2;
    if (!g->mmr) {
        tiku_mq_init(&d, p, n);
    }
    for (j = bpp - 1; ok && j >= 0; j--) {
        ok = (plane[j] = jb_new(jd, w, h, 0)) != NULL;
        if (ok && g->mmr) {
            ok = (used = jb_generic_mmr(p + at, n - at, plane[j])) >= 0;
            at += ok ? used : 0;
        } else if (ok) {
            ok = jb_generic(&d, gb, g, plane[j]) == 0;
        }
        for (i = 0; ok && j < bpp - 1 && i < w * h; i++) {
            plane[j]->px[i] ^= plane[j + 1]->px[i];
        }
        for (i = 0; ok && i < w * h; i++) {
            v[i] |= (uint32_t)plane[j]->px[i] << j;
        }
    }
    free(gb);
    if (!ok) {
        free(v);
        return NULL;
    }
    return v;
}

/** @brief A value in 256ths of a pixel as a pixel, floored. */
static long
cell(int64_t v)
{
    return (long)(v >= 0 ? v >> 8 : -((-v + 255) >> 8));
}

/** @brief A halftone's grid (7.4.5.1.2): cells across and down, and
 *         where the first and the steps between them go, in 256ths. */
typedef struct {
    long    gw, gh;
    int64_t gx, gy, rx, ry;
} grid_t;

/** @brief Where cell @p m, @p k of @p g lands (6.6.5.2). */
static void
grid_at(const grid_t *g, long m, long k, long *x, long *y)
{
    *x = cell(g->gx + m * g->ry + k * g->rx);
    *y = cell(g->gy + m * g->rx - k * g->ry);
}

/** @brief Which cells of @p g fall wholly outside the region (6.6.5.1),
 *         a pattern @p pat's size each: those are not coded. */
static jb_bm_t *
grid_skip(jb_dec_t *jd, const grid_t *g, const jb_bm_t *pat,
          const jb_rinfo_t *ri)
{
    jb_bm_t *skip = jb_new(jd, g->gw, g->gh, 0);
    long m, k, x, y;

    for (m = 0; skip != NULL && m < g->gh; m++) {
        for (k = 0; k < g->gw; k++) {
            grid_at(g, m, k, &x, &y);
            skip->px[m * g->gw + k] = (unsigned char)
                (x + pat->w <= 0 || x >= (long)ri->w ||
                 y + pat->h <= 0 || y >= (long)ri->h);
        }
    }
    return skip;
}

/** @brief Each cell of @p g as the pattern its grey value @p v names,
 *         drawn into @p reg by operator @p op.  @return 0, or -1 past
 *         the decode's bound. */
static int
grid_draw(jb_dec_t *jd, jb_bm_t *reg, const grid_t *g, const jb_seg_t *pd,
          const uint32_t *v, int op)
{
    long m, k, x, y;

    for (m = 0; m < g->gh; m++) {
        if (jb_work(jd, (long long)g->gw * pd->pat[0]->w * pd->pat[0]->h)) {
            return -1;
        }
        for (k = 0; k < g->gw; k++) {
            uint32_t i = v[m * g->gw + k];

            grid_at(g, m, k, &x, &y);
            jb_compose(reg, pd->pat[i < (uint32_t)pd->npat ? i : pd->npat - 1],
                       x, y, op);
        }
    }
    return 0;
}

/** @brief A halftone region segment (7.4.5, 6.6): a grid of cells, each
 *         the pattern its grey value names. */
static int
halftone(jb_dec_t *jd, jb_seg_t *s)
{
    const unsigned char *p = s->data;
    jb_seg_t *pd = s->nref > 0 ? jb_referred(jd, s, 0) : NULL;
    jb_rinfo_t ri;
    jb_gen_t g;
    grid_t grid;
    jb_bm_t *reg;
    uint32_t *v;

    if (jb_rinfo(p, s->len, &ri) || s->len < 38 || pd == NULL ||
        pd->type != 16 || pd->npat < 1) {
        return -1;
    }
    memset(&g, 0, sizeof g);
    g.mmr = p[17] & 1;
    g.tmpl = (p[17] >> 1) & 3;
    grid.gw = (long)jb_be32(p + 18);
    grid.gh = (long)jb_be32(p + 22);
    grid.gx = (int32_t)jb_be32(p + 26);
    grid.gy = (int32_t)jb_be32(p + 30);
    grid.rx = be16(p + 34);
    grid.ry = be16(p + 36);
    if ((long long)grid.gw * grid.gh > (1LL << 26) ||
        (reg = jb_new(jd, ri.w, ri.h, (p[17] >> 7) & 1)) == NULL ||
        ((p[17] & 8) &&
         (g.skip = grid_skip(jd, &grid, pd->pat[0], &ri)) == NULL) ||
        (v = greys(jd, p + 38, s->len - 38, &g, jb_log2((uint32_t)pd->npat),
                   grid.gw, grid.gh)) == NULL) {
        return -1;
    }
    if (grid_draw(jd, reg, &grid, pd, v, (p[17] >> 4) & 7) != 0) {
        free(v);
        return -1;
    }
    free(v);
    return jb_place(jd, s, reg, &ri);
}

/* ------------------------------------------------------------ segments  */

/**
 * @brief The segment header at @p p (7.2.2 to 7.2.7) into @p s.
 * @return its length; 0 when the bytes end inside it; -1 when it is
 *         not one.
 */
static long
seg_header(const unsigned char *p, long n, jb_seg_t *s)
{
    long at = 6, count, size, i;
    int page4;

    if (n < 11) {
        return 0;
    }
    s->num = jb_be32(p);
    s->type = p[4] & 63;
    page4 = (p[4] & 64) != 0;
    count = p[5] >> 5;
    if (count == 7) {               /* the long form, with retain bits */
        count = (long)(jb_be32(p + 5) & 0x1FFFFFFFu);
        at = 9 + (count + 8) / 8;
    } else if (count > 4) {
        return -1;                  /* 5 and 6 are not a short count */
    }
    if (count > REFS_MAX) {
        return -1;
    }
    size = s->num <= 256 ? 1 : s->num <= 65536 ? 2 : 4;
    if (n < at + count * size + (page4 ? 4 : 1) + 4) {
        return 0;
    }
    if (count > 0 && (s->ref = malloc((size_t)count * sizeof *s->ref)) ==
                     NULL) {
        return -1;
    }
    s->nref = (int)count;
    for (i = 0; i < count; i++, at += size) {
        s->ref[i] = size == 1 ? p[at] : size == 2 ? be16(p + at)
                                                  : jb_be32(p + at);
    }
    s->page = page4 ? jb_be32(p + at) : p[at];
    at += page4 ? 4 : 1;
    s->len = jb_be32(p + at) == 0xFFFFFFFFu ? -1 : (long)jb_be32(p + at);
    return at + 4;
}

/** @brief How long an immediate generic region is whose header does not
 *         say (7.2.7): through its end marker and its row count. */
static long
seg_unknown(const unsigned char *p, long n)
{
    long i;
    int mmr;

    if (n < 19) {
        return -1;
    }
    mmr = p[17] & 1;
    for (i = 18; i + 6 <= n; i++) {
        if (mmr ? p[i] == 0 && p[i + 1] == 0
                : p[i] == 0xFF && p[i + 1] == 0xAC) {
            return i + 6;
        }
    }
    return -1;
}

/** @brief @p s added to the decode's segments.  @return 0, or -1. */
static int
seg_push(jb_dec_t *jd, const jb_seg_t *s)
{
    jb_seg_t *grown;

    if (jd->nseg == jd->capseg) {
        long cap = jd->capseg ? jd->capseg * 2 : 32;

        if (cap > SEGS_MAX ||
            (grown = realloc(jd->seg, (size_t)cap * sizeof *grown)) == NULL) {
            return -1;
        }
        jd->seg = grown;
        jd->capseg = cap;
    }
    jd->seg[jd->nseg++] = *s;
    return 0;
}

/**
 * @brief The segments of @p p onto the decode's: each header then its
 *        data, or when @p random, every header and then every data
 *        (the random-access file organisation, 7.1).
 */
static void
seg_list(jb_dec_t *jd, const unsigned char *p, long n, int random)
{
    long at = 0, first = jd->nseg, i, hl;
    jb_seg_t s;

    while (at < n) {
        memset(&s, 0, sizeof s);
        if ((hl = seg_header(p + at, n - at, &s)) <= 0) {
            free(s.ref);
            break;
        }
        at += hl;
        s.data = p + at;
        if (s.len < 0 && (random || (s.type != 38 && s.type != 39) ||
                          (s.len = seg_unknown(p + at, n - at)) < 0)) {
            free(s.ref);
            break;
        }
        if (!random) {
            s.len = s.len < n - at ? s.len : n - at;
            at += s.len;
        }
        if (seg_push(jd, &s) != 0) {
            free(s.ref);
            break;
        }
        if (s.type == 51) {
            break;                  /* end of file */
        }
    }
    for (i = first; random && i < jd->nseg; i++) {
        jd->seg[i].data = p + at;
        jd->seg[i].len = jd->seg[i].len < n - at ? jd->seg[i].len : n - at;
        at += jd->seg[i].len;
    }
}

/** @brief Every segment of page @p page (0: all) in order, until the
 *         page or the file ends or a segment will not read. */
static void
run(jb_dec_t *jd, uint32_t page)
{
    long i;
    int rc = 0;

    for (i = 0; rc == 0 && i < jd->nseg; i++) {
        jb_seg_t *s = &jd->seg[i];

        if (page != 0 && s->page != 0 && s->page != page) {
            continue;
        }
        switch (s->type) {
        case 0: rc = jb_symbols(jd, s); break;
        case 4: case 6: case 7: rc = jb_text(jd, s); break;
        case 16: rc = patterns(jd, s); break;
        case 20: case 22: case 23: rc = halftone(jd, s); break;
        case 36: case 38: case 39: rc = generic(jd, s); break;
        case 40: case 42: case 43: rc = refinement(jd, s); break;
        case 48: rc = page_info(jd, s); break;
        case 49: case 51: return;
        case 50: rc = stripe(jd, s); break;
        case 53: rc = jb_table_read(s->data, s->len, &s->table); break;
        default: break;             /* profiles, extensions: nothing to do */
        }
    }
}

/** @brief The page into rows of bits, 1 black.  @return the bytes, or
 *         -1 when there is none or it will not fit in @p omax. */
static long
pack(const jb_dec_t *jd, unsigned char **out, long omax, int *w, int *h)
{
    const jb_bm_t *pg = jd->page;
    long rb, x, y;
    unsigned char *o;

    if (pg == NULL || pg->w <= 0 || pg->h <= 0) {
        return -1;
    }
    rb = (pg->w + 7) / 8;
    if (rb * pg->h > omax || (o = calloc((size_t)(rb * pg->h), 1)) == NULL) {
        return -1;
    }
    for (y = 0; y < pg->h; y++) {
        const unsigned char *row = pg->px + (size_t)y * (size_t)pg->w;

        for (x = 0; x < pg->w; x++) {
            if (row[x]) {
                o[y * rb + x / 8] |= (unsigned char)(0x80u >> (x & 7));
            }
        }
    }
    *out = o;
    *w = pg->w;
    *h = pg->h;
    return rb * pg->h;
}

/** @brief Everything the decode made, given back. */
static void
done(jb_dec_t *jd)
{
    long i;

    for (i = 0; i < jd->nseg; i++) {
        free(jd->seg[i].ref);
        free(jd->seg[i].sym);
        free(jd->seg[i].pat);
        free(jd->seg[i].gbkeep);
        free(jd->seg[i].grkeep);
        free(jd->seg[i].table.line);
    }
    for (i = 0; i < jd->npool; i++) {
        jb_bm_free(jd->pool[i]);
    }
    free(jd->seg);
    free(jd->pool);
}

long
tiku_jbig2_decode(const unsigned char *glob, long gn,
                  const unsigned char *data, long n, unsigned char **out,
                  long omax, int *w, int *h)
{
    jb_dec_t jd;
    long made;

    *out = NULL;
    memset(&jd, 0, sizeof jd);
    jd.omax = omax;
    if (glob != NULL && gn > 0) {
        seg_list(&jd, glob, gn, 0);
    }
    if (data != NULL && n > 0) {
        seg_list(&jd, data, n, 0);
    }
    run(&jd, 0);
    made = pack(&jd, out, omax, w, h);
    done(&jd);
    return made;
}

long
tiku_jbig2_file(const unsigned char *data, long n, int page,
                unsigned char **out, long omax, int *w, int *h)
{
    static const unsigned char id[8] = {0x97, 'J', 'B', '2', 0x0D, 0x0A,
                                        0x1A, 0x0A};
    jb_dec_t jd;
    long made, at;

    *out = NULL;
    if (data == NULL || n < 9 || memcmp(data, id, 8) != 0 || page < 1) {
        return -1;
    }
    at = (data[8] & 2) ? 9 : 13;    /* the page count, when known */
    memset(&jd, 0, sizeof jd);
    jd.omax = omax;
    if (n > at) {
        seg_list(&jd, data + at, n - at, !(data[8] & 1));
    }
    run(&jd, (uint32_t)page);
    made = pack(&jd, out, omax, w, h);
    done(&jd);
    return made;
}
