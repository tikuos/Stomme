/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_jpx.c - a JPEG 2000 codestream (15444-1 Annex A) decoded: its
 * main header's styles, its tile-parts gathered into tiles, and each
 * tile taken down to code-blocks, filled by its packets, and put back
 * together into the components' samples.
 *
 * A tile's own header overrides the main one's styles, a component's
 * over the whole's (A.6); precincts and code-blocks are laid out as
 * Annex B draws them, anchored at the grid's origin, not the tile's.
 * A picture too big to fit is decoded without its finest resolutions.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_jpx_private.h"

#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define TILES_MAX  65535            /* tiles one codestream has          */
#define PREC_MAX   16               /* bits a component may have here    */
#define WORK_MAX   (6LL << 30)      /* coefficient bitplanes decoded     */
#define LAYOUT_MAX (256L << 20)     /* bytes one tile's layout may take  */

/** @brief A marker segment: which, and its bytes after the length. */
typedef struct {
    unsigned             m;         /* the marker                        */
    const unsigned char *p;         /* its parameters                    */
    long                 n;         /* their bytes                       */
} mseg_t;

/** @brief A header's marker segments, however many it has. */
typedef struct {
    mseg_t *s;                      /* in the order they came            */
    int     n, cap;                 /* how many; room for                */
} segs_t;

/** @brief Big-endian 16 bits. */
static uint32_t
be16(const unsigned char *p)
{
    return (uint32_t)p[0] << 8 | p[1];
}

/** @brief Big-endian 32 bits. */
static uint32_t
be32(const unsigned char *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 |
           (uint32_t)p[2] << 8 | p[3];
}

/** @brief Ceiling of @p a over @p b, @p b above zero. */
static int64_t
cdiv(int64_t a, int64_t b)
{
    return a >= 0 ? (a + b - 1) / b : -((-a) / b);
}

/** @brief Floor of @p a over @p b, @p b above zero. */
static int64_t
fdiv(int64_t a, int64_t b)
{
    return a >= 0 ? a / b : -((-a + b - 1) / b);
}

/* ------------------------------------------------------------- styles  */

/** @brief Room for @p n components' styles in @p st.  @return 0, -1. */
static int
style_alloc(jpx_style_t *st, int n)
{
    st->ccod = calloc((size_t)n, sizeof *st->ccod);
    st->quant = calloc((size_t)n, sizeof *st->quant);
    st->roi = calloc((size_t)n, sizeof *st->roi);
    return st->ccod && st->quant && st->roi ? 0 : -1;
}

/** @brief @p st's component arrays given back. */
static void
style_free(jpx_style_t *st)
{
    free(st->ccod);
    free(st->quant);
    free(st->roi);
    st->ccod = NULL;
    st->quant = NULL;
    st->roi = NULL;
}

/** @brief Tile @p t's component styles made copies of its own, from the
 *         main header's it shared, for @p n components.  @return 0, -1. */
static int
style_own(jpx_tile_t *t, int n)
{
    const jpx_style_t shared = t->st;

    t->ownst = 1;
    if (style_alloc(&t->st, n) != 0) {
        return -1;
    }
    memcpy(t->st.ccod, shared.ccod, (size_t)n * sizeof *t->st.ccod);
    memcpy(t->st.quant, shared.quant, (size_t)n * sizeof *t->st.quant);
    memcpy(t->st.roi, shared.roi, (size_t)n * sizeof *t->st.roi);
    return 0;
}

/** @brief SPcod or SPcoc (A.6.1) into @p cc, precinct sizes when
 *         @p prec says they are given.  @return 0, or -1. */
static int
spcod(const unsigned char *p, long n, int prec, jpx_ccod_t *cc)
{
    int r;

    if (n < 5) {
        return -1;
    }
    cc->nl = p[0];
    cc->cbw = p[1] + 2;
    cc->cbh = p[2] + 2;
    cc->style = p[3];
    cc->reversible = p[4] == 1;
    if (cc->nl > JPX_LEVELS || cc->cbw > 10 || cc->cbh > 10 ||
        cc->cbw + cc->cbh > 12 || (prec && n < 5 + cc->nl + 1)) {
        return -1;
    }
    for (r = 0; r <= cc->nl; r++) {
        cc->ppx[r] = prec ? p[5 + r] & 15 : 15;
        cc->ppy[r] = prec ? p[5 + r] >> 4 : 15;
        if (r > 0 && (cc->ppx[r] == 0 || cc->ppy[r] == 0)) {
            return -1;
        }
    }
    return 0;
}

/** @brief QCD's or QCC's step sizes (A.6.4) into @p q. */
static int
quant(const unsigned char *p, long n, jpx_quant_t *q)
{
    int i;

    if (n < 1) {
        return -1;
    }
    q->style = p[0] & 31;
    q->guard = p[0] >> 5;
    if (q->style == 0) {
        q->n = (int)(n - 1);
    } else if (q->style == 1 || q->style == 2) {
        q->n = (int)((n - 1) / 2);
    } else {
        return -1;
    }
    q->n = q->n < 3 * JPX_LEVELS + 1 ? q->n : 3 * JPX_LEVELS + 1;
    for (i = 0; i < q->n; i++) {
        q->step[i] = (uint16_t)(q->style == 0 ? p[1 + i] : be16(p + 1 + 2 * i));
    }
    return q->n > 0 ? 0 : -1;
}

/** @brief A component number of one or two bytes as there are fewer or
 *         more than 256 components.  @return it, or -1. */
static int
comp_no(const jpx_t *j, const unsigned char *p, long n, int *len)
{
    int c;

    *len = j->ncomp < 257 ? 1 : 2;
    if (n < *len) {
        return -1;
    }
    c = *len == 1 ? p[0] : (int)be16(p);
    return c < j->ncomp ? c : -1;
}

/** @brief Room for one more progression change in @p st.  @return 0,
 *         or -1 when it cannot be made or the list is full. */
static int
poc_room(jpx_style_t *st)
{
    jpx_prog_t *grown;
    int cap;

    if (st->npoc < st->cappoc) {
        return 0;
    }
    cap = st->cappoc ? st->cappoc * 2 : 8;
    if (cap > JPX_POCS ||
        (grown = realloc(st->poc, (size_t)cap * sizeof *grown)) == NULL) {
        return -1;
    }
    st->poc = grown;
    st->cappoc = cap;
    return 0;
}

/** @brief POC (A.6.6): progression changes, each over its ranges, added
 *         to those @p st has. */
static int
poc(const jpx_t *j, jpx_style_t *st, const unsigned char *p, long n)
{
    int cb = j->ncomp < 257 ? 1 : 2, each = 5 + 2 * cb;

    for (; n >= each && poc_room(st) == 0; p += each, n -= each) {
        jpx_prog_t *g = &st->poc[st->npoc++];

        g->rs = p[0];
        g->cs = cb == 1 ? p[1] : (int)be16(p + 1);
        g->le = (int)be16(p + 1 + cb);
        g->re = p[3 + cb];
        g->ce = cb == 1 ? p[4 + cb] : (int)be16(p + 4 + cb);
        g->ce = g->ce == 0 ? (cb == 1 ? 256 : 16384) : g->ce;
        g->order = p[4 + 2 * cb];
        if (g->order > 4) {
            return -1;
        }
    }
    return 0;
}

/**
 * @brief One marker segment of a header applied to @p st: COD, COC,
 *        QCD, QCC, RGN or POC (A.6).
 * @note The caller gives them in the order that makes a component's own
 *       override the whole's.
 */
static int
apply(jpx_t *j, jpx_style_t *st, const mseg_t *s)
{
    const unsigned char *p = s->p;
    jpx_ccod_t cc;
    jpx_quant_t q;
    int c, len, i;

    switch (s->m) {
    case 0xFF52:                    /* COD */
        if (s->n < 5 || p[1] > 4 || be16(p + 2) < 1 ||
            spcod(p + 5, s->n - 5, p[0] & 1, &cc) != 0) {
            return -1;
        }
        st->order = p[1];
        st->layers = (int)be16(p + 2);
        st->mct = p[4];
        st->sop = (p[0] >> 1) & 1;
        st->eph = (p[0] >> 2) & 1;
        for (i = 0; i < j->ncomp; i++) {
            st->ccod[i] = cc;
        }
        return 0;
    case 0xFF53:                    /* COC */
        c = comp_no(j, p, s->n, &len);
        return c < 0 || s->n < len + 1 ? -1 :
               spcod(p + len + 1, s->n - len - 1, p[len] & 1, &st->ccod[c]);
    case 0xFF5C:                    /* QCD */
        if (quant(p, s->n, &q) != 0) {
            return -1;
        }
        for (i = 0; i < j->ncomp; i++) {
            st->quant[i] = q;
        }
        return 0;
    case 0xFF5D:                    /* QCC */
        c = comp_no(j, p, s->n, &len);
        return c < 0 ? -1 : quant(p + len, s->n - len, &st->quant[c]);
    case 0xFF5E:                    /* RGN: the maximum shift, implicit */
        c = comp_no(j, p, s->n, &len);
        if (c < 0 || s->n < len + 2 || p[len] != 0) {
            return -1;
        }
        st->roi[c] = p[len + 1];
        return 0;
    default:                        /* POC */
        return poc(j, st, p, s->n);
    }
}

/** @brief A header's styles applied in the standard's precedence: the
 *         whole's COD and QCD before any component's COC and QCC. */
static int
header(jpx_t *j, jpx_style_t *st, const mseg_t *s, int n)
{
    static const unsigned order[] = {0xFF52, 0xFF53, 0xFF5C, 0xFF5D, 0xFF5E,
                                     0xFF5F};
    size_t k;
    int i;

    for (k = 0; k < sizeof order / sizeof order[0]; k++) {
        for (i = 0; i < n; i++) {
            if (s[i].m == order[k] && apply(j, st, &s[i]) != 0) {
                return -1;
            }
        }
    }
    return 0;
}

/** @brief SIZ (A.5.1): the grid, the tiles, the components. */
static int
siz(jpx_t *j, const unsigned char *p, long n)
{
    int64_t tx, ty;
    int c;

    if (n < 36) {
        return -1;
    }
    j->xsiz = be32(p + 2);
    j->ysiz = be32(p + 6);
    j->xosiz = be32(p + 10);
    j->yosiz = be32(p + 14);
    j->xtsiz = be32(p + 18);
    j->ytsiz = be32(p + 22);
    j->xtosiz = be32(p + 26);
    j->ytosiz = be32(p + 30);
    j->ncomp = (int)be16(p + 34);
    if (j->ncomp < 1 || j->ncomp > JPX_COMPS || n < 36 + 3 * j->ncomp ||
        j->xsiz > INT_MAX ||
        j->ysiz > INT_MAX || j->xsiz <= j->xosiz || j->ysiz <= j->yosiz ||
        j->xtsiz == 0 || j->ytsiz == 0 || j->xtosiz > j->xosiz ||
        j->ytosiz > j->yosiz || (uint64_t)j->xtosiz + j->xtsiz <= j->xosiz ||
        (uint64_t)j->ytosiz + j->ytsiz <= j->yosiz ||
        (j->cm = calloc((size_t)j->ncomp, sizeof *j->cm)) == NULL) {
        return -1;
    }
    for (c = 0; c < j->ncomp; c++) {
        j->cm[c].prec = (p[36 + 3 * c] & 0x7F) + 1;
        j->cm[c].sgnd = p[36 + 3 * c] >> 7;
        j->cm[c].dx = p[37 + 3 * c];
        j->cm[c].dy = p[38 + 3 * c];
        if (j->cm[c].prec > PREC_MAX || j->cm[c].dx == 0 ||
            j->cm[c].dy == 0) {
            return -1;
        }
    }
    tx = cdiv((int64_t)j->xsiz - j->xtosiz, j->xtsiz);
    ty = cdiv((int64_t)j->ysiz - j->ytosiz, j->ytsiz);
    if (tx * ty > TILES_MAX ||
        (j->tile = calloc((size_t)(tx * ty), sizeof *j->tile)) == NULL) {
        return -1;
    }
    j->ntx = (int)tx;
    j->nty = (int)ty;
    return style_alloc(&j->main, j->ncomp);
}

/* -------------------------------------------------------- tile-parts  */

/** @brief @p n bytes appended to a growing buffer.  @return 0, or -1. */
static int
append(unsigned char **buf, long *len, long *cap, const unsigned char *p,
       long n)
{
    if (n <= 0) {
        return 0;
    }
    if (*len + n > *cap) {
        long c = (*len + n) * 2;
        unsigned char *g = realloc(*buf, (size_t)c);

        if (g == NULL) {
            return -1;
        }
        *buf = g;
        *cap = c;
    }
    memcpy(*buf + *len, p, (size_t)n);
    *len += n;
    return 0;
}

/** @brief Segment @p m, @p n bytes at @p p, kept on @p l.  @return 0,
 *         or -1 when there is no room. */
static int
seg_keep(segs_t *l, unsigned m, const unsigned char *p, long n)
{
    if (l->n == l->cap) {
        int cap = l->cap ? l->cap * 2 : 16;
        mseg_t *grown = realloc(l->s, (size_t)cap * sizeof *grown);

        if (grown == NULL) {
            return -1;
        }
        l->s = grown;
        l->cap = cap;
    }
    l->s[l->n].m = m;
    l->s[l->n].p = p;
    l->s[l->n++].n = n;
    return 0;
}

/**
 * @brief The marker segments from @p at up to SOS-like @p stop (SOT for
 *        the main header, SOD for a tile-part's) that the decode reads,
 *        kept on @p l.  @return where it stopped, or -1.
 */
static long
markers(const unsigned char *p, long end, long at, unsigned stop, segs_t *l)
{
    while (at + 2 <= end) {
        unsigned m = be16(p + at);
        long len;

        if (m == stop) {
            return at;
        }
        if (m >= 0xFF30 && m <= 0xFF3F) {
            at += 2;                /* reserved, and without a segment */
            continue;
        }
        if ((m & 0xFF00) != 0xFF00 || at + 4 > end ||
            (len = be16(p + at + 2)) < 2 || at + 2 + len > end) {
            return -1;
        }
        if ((m == 0xFF51 || m == 0xFF52 || m == 0xFF53 ||
             (m >= 0xFF5C && m <= 0xFF61)) &&
            seg_keep(l, m, p + at + 4, len - 2) != 0) {
            return -1;
        }
        at += 2 + len;
    }
    return at;
}

/** @brief The packet headers of @p l's segments of marker @p m (PPM or
 *         PPT) onto @p buf, in the order of the index each begins with
 *         (A.7.4, A.7.5), not the order they came in. */
static int
gather(const segs_t *l, unsigned m, unsigned char **buf, long *len,
       long *cap)
{
    int z, i;

    for (z = 0; z < 256; z++) {
        for (i = 0; i < l->n; i++) {
            if (l->s[i].m == m && l->s[i].n > 1 && l->s[i].p[0] == z &&
                append(buf, len, cap, l->s[i].p + 1, l->s[i].n - 1) != 0) {
                return -1;
            }
        }
    }
    return 0;
}

/** @brief This tile-part's share of PPM's headers (A.7.4): Nppm bytes
 *         after a four-byte count, onto its tile's. */
static int
ppm_part(jpx_t *j, long *at, jpx_tile_t *t)
{
    long n;

    if (j->ppm == NULL || *at + 4 > j->ppmlen) {
        return 0;
    }
    n = (long)be32(j->ppm + *at);
    *at += 4;
    n = n < j->ppmlen - *at ? n : j->ppmlen - *at;
    *at += n;
    return append(&t->hdr, &t->hlen, &t->hcap, j->ppm + *at - n, n);
}

/** @brief Tile @p idx, made when its first part comes, with the main
 *         header's styles, shared until its own header changes them. */
static jpx_tile_t *
tile_get(jpx_t *j, unsigned idx)
{
    if (j->tile[idx] == NULL &&
        (j->tile[idx] = calloc(1, sizeof **j->tile)) != NULL) {
        j->tile[idx]->st = j->main;
    }
    return j->tile[idx];
}

/**
 * @brief A tile-part header's segments @p s applied to tile @p t: the
 *        tile given copies of its own of the styles they change first,
 *        and its progression changes begun afresh by its first POC.
 */
static int
tile_styles(jpx_t *j, jpx_tile_t *t, const mseg_t *s, int ns)
{
    int i, styles = 0, pocs = 0;

    for (i = 0; i < ns; i++) {
        pocs |= s[i].m == 0xFF5F;
        styles |= s[i].m == 0xFF52 || s[i].m == 0xFF53 ||
                  (s[i].m >= 0xFF5C && s[i].m <= 0xFF5E);
    }
    if (styles && !t->ownst && style_own(t, j->ncomp) != 0) {
        return -1;
    }
    if (pocs && !t->ownpoc) {
        t->st.poc = NULL;
        t->st.npoc = t->st.cappoc = 0;
        t->ownpoc = 1;
    }
    return header(j, &t->st, s, ns);
}

/**
 * @brief Tile-part at @p at (A.4.2): its SOT, its header (styles for
 *        the tile when it is the first part), and its bytes after SOD
 *        onto the tile's.  @return where the next begins, or -1.
 */
static long
tile_part(jpx_t *j, const unsigned char *p, long n, long at, long *ppm_at)
{
    segs_t l = {NULL, 0, 0};
    long end, body = -1;
    unsigned idx;
    uint32_t psot;
    jpx_tile_t *t;

    if (at + 12 > n || be16(p + at + 2) != 10 ||
        (idx = be16(p + at + 4)) >= (unsigned)(j->ntx * j->nty)) {
        return -1;
    }
    psot = be32(p + at + 6);
    end = psot == 0 || psot > (uint32_t)(n - at) ? n : at + (long)psot;
    if ((t = tile_get(j, idx)) != NULL &&
        ((body = markers(p, end, at + 12, 0xFF93, &l)) < 0 ||
         gather(&l, 0xFF61, &t->hdr, &t->hlen, &t->hcap) != 0 ||
         tile_styles(j, t, l.s, l.n) != 0 || ppm_part(j, ppm_at, t) != 0)) {
        body = -1;
    }
    free(l.s);
    if (body < 0) {
        return -1;
    }
    body += 2;                      /* past SOD */
    if (end == n && end - body >= 2 && be16(p + end - 2) == 0xFFD9) {
        end -= 2;                   /* the codestream's EOC */
    }
    if (body < end && append(&t->data, &t->len, &t->cap, p + body,
                             end - body) != 0) {
        return -1;
    }
    return end;
}

/* -------------------------------------------------------- tile layout  */

/** @brief Band @p b's magnitude bitplanes and step (E.1): from the
 *         component's quantization, its level and its gain. */
static void
band_quant(const jpx_t *j, int c, const jpx_style_t *st, int r,
           jpx_band_t *b)
{
    const jpx_quant_t *q = &st->quant[c];
    const jpx_ccod_t *cc = &st->ccod[c];
    int i = r == 0 ? 0 : 3 * (r - 1) + b->orient;
    int nb = r == 0 ? cc->nl : cc->nl - r + 1;
    int gain = b->orient == JPX_LL ? 0 : b->orient == JPX_HH ? 2 : 1;
    int eps, mu = 0;
    uint16_t v = q->n == 0 ? 0 : q->step[i < q->n ? i : q->n - 1];

    if (q->style == 0) {
        eps = v >> 3;
    } else if (q->style == 1) {
        eps = (q->step[0] >> 11) - cc->nl + nb;
        mu = q->step[0] & 0x7FF;
    } else {
        eps = i < q->n ? q->step[i] >> 11 : 0;
        mu = i < q->n ? q->step[i] & 0x7FF : 0;
    }
    b->mb = q->guard + eps - 1;
    b->delta = cc->reversible ? 1.0 :
               ldexp(1.0 + mu / 2048.0, j->cm[c].prec + gain - eps);
}

/** @brief Precinct (@p i, @p k)'s area in band @p b of resolution @p r
 *         (B.6): the resolution's partition, halved in the bands of any
 *         resolution above 0, clipped to the band; into @p a as x0, y0,
 *         x1, y1. */
static void
precinct_area(const jpx_res_t *rs, int r, const jpx_band_t *b, int i, int k,
              int64_t a[4])
{
    int64_t ps = (int64_t)1 << (rs->ppx - (r > 0));
    int64_t qs = (int64_t)1 << (rs->ppy - (r > 0));
    int64_t x0 = (fdiv(rs->x0, (int64_t)1 << rs->ppx) + i) * ps;
    int64_t y0 = (fdiv(rs->y0, (int64_t)1 << rs->ppy) + k) * qs;

    a[0] = x0 > b->x0 ? x0 : b->x0;
    a[1] = y0 > b->y0 ? y0 : b->y0;
    a[2] = x0 + ps < b->x1 ? x0 + ps : b->x1;
    a[3] = y0 + qs < b->y1 ? y0 + qs : b->y1;
}

/**
 * @brief The code-blocks of precinct (@p i, @p k) in band @p bi of
 *        resolution @p r (B.7): its area cut by the code-block grid, and
 *        its two tag trees, within the tile's layout budget.
 */
static int
precinct_band(jpx_t *j, jpx_res_t *rs, int r, int bi, int i, int k)
{
    const jpx_band_t *b = &rs->band[bi];
    jpx_pband_t *pb = &rs->prec[((long)k * rs->pw + i) * rs->nbands + bi];
    int64_t a[4], cx0, cy0;
    int u, v;

    precinct_area(rs, r, b, i, k, a);
    cx0 = fdiv(a[0], (int64_t)1 << b->xcb);
    cy0 = fdiv(a[1], (int64_t)1 << b->ycb);
    pb->cw = a[2] > a[0] ? (int)(cdiv(a[2], (int64_t)1 << b->xcb) - cx0) : 0;
    pb->ch = a[3] > a[1] ? (int)(cdiv(a[3], (int64_t)1 << b->ycb) - cy0) : 0;
    j->layout += ((long)pb->cw * pb->ch + 1) *
                 (long)(sizeof *pb->cblk + 8 * sizeof(int));
    if (j->layout > LAYOUT_MAX ||
        (pb->cblk = calloc((size_t)pb->cw * (size_t)pb->ch + 1,
                           sizeof *pb->cblk)) == NULL ||
        jpx_tag_init(&pb->incl, pb->cw, pb->ch) != 0 ||
        jpx_tag_init(&pb->zbp, pb->cw, pb->ch) != 0) {
        return -1;
    }
    for (v = 0; v < pb->ch; v++) {
        for (u = 0; u < pb->cw; u++) {
            jpx_cblk_t *cb = &pb->cblk[v * pb->cw + u];
            int64_t bx0 = (cx0 + u) << b->xcb, by0 = (cy0 + v) << b->ycb;
            int64_t bx1 = bx0 + ((int64_t)1 << b->xcb);
            int64_t by1 = by0 + ((int64_t)1 << b->ycb);

            cb->x0 = (int)(bx0 > a[0] ? bx0 : a[0]);
            cb->y0 = (int)(by0 > a[1] ? by0 : a[1]);
            cb->x1 = (int)(bx1 < a[2] ? bx1 : a[2]);
            cb->y1 = (int)(by1 < a[3] ? by1 : a[3]);
        }
    }
    return 0;
}

/** @brief Band @p bi of resolution @p r of tile-component @p tc (B.5):
 *         its orientation, extent, code-block size and quantization. */
static void
band_setup(const jpx_t *j, const jpx_tile_t *t, int c, jpx_tcomp_t *tc,
           int r, int bi)
{
    jpx_res_t *rs = &tc->res[r];
    jpx_band_t *b = &rs->band[bi];
    const jpx_ccod_t *cc = &t->st.ccod[c];
    int nb = r == 0 ? tc->nl : tc->nl - r + 1, up = r > 0;
    int64_t xo, yo;

    b->orient = r == 0 ? JPX_LL : bi + 1;
    xo = up ? (int64_t)(b->orient & 1) << (nb - 1) : 0;
    yo = up ? (int64_t)(b->orient >> 1) << (nb - 1) : 0;
    b->x0 = (int)cdiv(tc->x0 - xo, (int64_t)1 << nb);
    b->y0 = (int)cdiv(tc->y0 - yo, (int64_t)1 << nb);
    b->x1 = (int)cdiv(tc->x1 - xo, (int64_t)1 << nb);
    b->y1 = (int)cdiv(tc->y1 - yo, (int64_t)1 << nb);
    b->xcb = cc->cbw < rs->ppx - up ? cc->cbw : rs->ppx - up;
    b->ycb = cc->cbh < rs->ppy - up ? cc->cbh : rs->ppy - up;
    band_quant(j, c, &t->st, r, b);
}

/** @brief Resolution @p r of tile-component @p tc: its extent, bands and
 *         precincts (B.5, B.6).  @return 0, or -1. */
static int
resolution(jpx_t *j, const jpx_tile_t *t, int c, jpx_tcomp_t *tc, int r)
{
    const jpx_ccod_t *cc = &t->st.ccod[c];
    jpx_res_t *rs = &tc->res[r];
    int lv = tc->nl - r, bi, i, k;
    int64_t np;

    rs->x0 = (int)cdiv(tc->x0, (int64_t)1 << lv);
    rs->y0 = (int)cdiv(tc->y0, (int64_t)1 << lv);
    rs->x1 = (int)cdiv(tc->x1, (int64_t)1 << lv);
    rs->y1 = (int)cdiv(tc->y1, (int64_t)1 << lv);
    rs->ppx = cc->ppx[r];
    rs->ppy = cc->ppy[r];
    rs->pw = rs->x1 > rs->x0 ? (int)(cdiv(rs->x1, (int64_t)1 << rs->ppx) -
                                     fdiv(rs->x0, (int64_t)1 << rs->ppx)) : 0;
    rs->ph = rs->y1 > rs->y0 ? (int)(cdiv(rs->y1, (int64_t)1 << rs->ppy) -
                                     fdiv(rs->y0, (int64_t)1 << rs->ppy)) : 0;
    rs->nbands = r == 0 ? 1 : 3;
    for (bi = 0; bi < rs->nbands; bi++) {
        band_setup(j, t, c, tc, r, bi);
    }
    np = (int64_t)rs->pw * rs->ph;
    j->layout += (long)((np * rs->nbands + 1) * (int64_t)sizeof *rs->prec +
                        (np + 1) * (int64_t)sizeof *rs->layers);
    if (j->layout > LAYOUT_MAX ||
        (rs->prec = calloc((size_t)(np * rs->nbands + 1),
                           sizeof *rs->prec)) == NULL ||
        (rs->layers = calloc((size_t)(np + 1), sizeof *rs->layers)) == NULL) {
        return -1;
    }
    for (k = 0; k < rs->ph; k++) {
        for (i = 0; i < rs->pw; i++) {
            for (bi = 0; bi < rs->nbands; bi++) {
                if (precinct_band(j, rs, r, bi, i, k) != 0) {
                    return -1;
                }
            }
        }
    }
    return 0;
}

/** @brief Tile @p t's share of component @p c laid out: its extent, its
 *         resolutions, and room for the coefficients of those decoded --
 *         none past the first TIKU_JPX_COMPS, whose packets are only read. */
static int
tcomp_setup(jpx_t *j, jpx_tile_t *t, int c)
{
    jpx_tcomp_t *tc = &t->comp[c];
    const jpx_res_t *top;
    int r;

    tc->x0 = (int)cdiv(t->x0, j->cm[c].dx);
    tc->y0 = (int)cdiv(t->y0, j->cm[c].dy);
    tc->x1 = (int)cdiv(t->x1, j->cm[c].dx);
    tc->y1 = (int)cdiv(t->y1, j->cm[c].dy);
    tc->nl = t->st.ccod[c].nl;
    tc->top = tc->nl > j->reduce ? tc->nl - j->reduce : 0;
    if ((tc->res = calloc((size_t)tc->nl + 1, sizeof *tc->res)) == NULL) {
        return -1;
    }
    for (r = 0; r <= tc->nl; r++) {
        if (resolution(j, t, c, tc, r) != 0) {
            return -1;
        }
    }
    if (c >= TIKU_JPX_COMPS) {
        return 0;
    }
    top = &tc->res[tc->top];
    if ((int64_t)(top->x1 - top->x0) * (top->y1 - top->y0) >
        4 * (int64_t)j->max_pixels) {
        return -1;                  /* too few levels to leave any out */
    }
    tc->stride = top->x1 - top->x0;
    tc->coef = calloc((size_t)tc->stride * (size_t)(top->y1 - top->y0) + 1,
                      sizeof *tc->coef);
    return tc->coef != NULL ? 0 : -1;
}

/** @brief Tile @p idx laid out (B.3): its place, and each component's
 *         resolutions and coefficients.  @return 0, or -1. */
static int
tile_setup(jpx_t *j, jpx_tile_t *t, int idx)
{
    int p = idx % j->ntx, q = idx / j->ntx, c;
    int64_t x0 = (int64_t)j->xtosiz + (int64_t)p * j->xtsiz;
    int64_t y0 = (int64_t)j->ytosiz + (int64_t)q * j->ytsiz;

    t->x0 = (int)(x0 > j->xosiz ? x0 : j->xosiz);
    t->y0 = (int)(y0 > j->yosiz ? y0 : j->yosiz);
    t->x1 = (int)(x0 + j->xtsiz < j->xsiz ? x0 + j->xtsiz : j->xsiz);
    t->y1 = (int)(y0 + j->ytsiz < j->ysiz ? y0 + j->ytsiz : j->ysiz);
    j->layout = 0;
    if ((t->comp = calloc((size_t)j->ncomp, sizeof *t->comp)) == NULL) {
        return -1;
    }
    for (c = 0; c < j->ncomp; c++) {
        if (tcomp_setup(j, t, c) != 0) {
            return -1;
        }
    }
    return 0;
}

/** @brief Everything tile_setup() and the packets made, given back. */
static void
tile_free(jpx_t *j, jpx_tile_t *t)
{
    int c, r, i, k;

    for (c = 0; t->comp != NULL && c < j->ncomp; c++) {
        for (r = 0; t->comp[c].res != NULL && r <= t->comp[c].nl; r++) {
            jpx_res_t *rs = &t->comp[c].res[r];

            for (i = 0; rs->prec != NULL && i < rs->pw * rs->ph * rs->nbands;
                 i++) {
                for (k = 0; rs->prec[i].cblk != NULL &&
                            k < rs->prec[i].cw * rs->prec[i].ch; k++) {
                    free(rs->prec[i].cblk[k].data);
                    free(rs->prec[i].cblk[k].seg);
                }
                free(rs->prec[i].cblk);
                jpx_tag_free(&rs->prec[i].incl);
                jpx_tag_free(&rs->prec[i].zbp);
            }
            free(rs->prec);
            free(rs->layers);
        }
        free(t->comp[c].res);
        free(t->comp[c].coef);
    }
    free(t->comp);
    t->comp = NULL;
}

/* -------------------------------------------------------- tile decode  */

/**
 * @brief Tier 1 for every code-block of tile-component @p c up to its
 *        top resolution (D): each one's coefficients into place, its
 *        subband set where the transform leaves it.  @return 0, or -1
 *        past the work bound.
 */
static int
tier1(jpx_t *j, jpx_tile_t *t, int c)
{
    jpx_tcomp_t *tc = &t->comp[c];
    int r, i, k, bi;

    for (r = 0; r <= tc->top; r++) {
        jpx_res_t *rs = &tc->res[r];
        int lw = r ? tc->res[r - 1].x1 - tc->res[r - 1].x0 : 0;
        int lh = r ? tc->res[r - 1].y1 - tc->res[r - 1].y0 : 0;

        for (i = 0; i < rs->pw * rs->ph; i++) {
            for (bi = 0; bi < rs->nbands; bi++) {
                jpx_pband_t *pb = &rs->prec[i * rs->nbands + bi];
                const jpx_band_t *b = &rs->band[bi];
                long ox = (b->orient & 1) ? lw - b->x0 : -b->x0;
                long oy = (b->orient >> 1) ? lh - b->y0 : -b->y0;

                for (k = 0; k < pb->cw * pb->ch; k++) {
                    const jpx_cblk_t *cb = &pb->cblk[k];

                    j->work += (int64_t)(cb->x1 - cb->x0) * (cb->y1 - cb->y0) *
                               (cb->numbps > 0 ? cb->numbps : 0);
                    if (j->work > WORK_MAX) {
                        return -1;
                    }
                    (void)jpx_t1_decode(cb, b, t->st.ccod[c].style,
                                        t->st.ccod[c].reversible,
                                        t->st.roi[c], tc->coef +
                                        (oy + cb->y0) * tc->stride + ox +
                                        cb->x0, tc->stride);
                }
            }
        }
    }
    return 0;
}

/** @brief Clamp @p v to @p lo..@p hi, @p hi at least @p lo. */
static int64_t
clamp(int64_t v, int64_t lo, int64_t hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

/**
 * @brief Tile-component @p c's samples at its top resolution, level
 *        shifted and clamped (G.1), into its plane (@p pw wide, first
 *        sample at @p px0, @p py0); one with fewer levels than the
 *        reduction left out thinned to match.
 */
static void
store(const jpx_t *j, const jpx_tile_t *t, int c, uint16_t *plane, long pw,
      int64_t px0, int64_t py0)
{
    const jpx_tcomp_t *tc = &t->comp[c];
    const jpx_res_t *rs = &tc->res[tc->top];
    int thin = j->reduce - (tc->nl - tc->top), pr = j->cm[c].prec;
    float lo = j->cm[c].sgnd ? -(float)(1L << (pr - 1)) : 0.0f;
    float mid = j->cm[c].sgnd ? 0.0f : (float)(1L << (pr - 1));
    int64_t s = (int64_t)1 << j->reduce, x, y;

    if (rs->x1 <= rs->x0 || rs->y1 <= rs->y0) {
        return;
    }
    for (y = cdiv(tc->y0, s); y < cdiv(tc->y1, s); y++) {
        int64_t sy = clamp(y << thin, rs->y0, rs->y1 - 1) - rs->y0;

        for (x = cdiv(tc->x0, s); x < cdiv(tc->x1, s); x++) {
            int64_t sx = clamp(x << thin, rs->x0, rs->x1 - 1) - rs->x0;
            float v = tc->coef[sy * tc->stride + sx] + mid;

            v = t->st.ccod[c].reversible ? v : floorf(v + 0.5f);
            v = v < lo ? lo : v > lo + (float)((1L << pr) - 1) ?
                lo + (float)((1L << pr) - 1) : v;
            plane[(y - py0) * pw + (x - px0)] =
                (uint16_t)(v - lo);  /* signed: offset to unsigned */
        }
    }
}

/** @brief Component @p c's plane, after the reduction: its first sample
 *         on its own grid into @p x0, @p y0; its size into @p w, @p h. */
static void
plane_area(const jpx_t *j, int c, int64_t *x0, int64_t *y0, long *w,
           long *h)
{
    int64_t sx = (int64_t)j->cm[c].dx << j->reduce;
    int64_t sy = (int64_t)j->cm[c].dy << j->reduce;

    *x0 = cdiv(j->xosiz, sx);
    *y0 = cdiv(j->yosiz, sy);
    *w = (long)(cdiv(j->xsiz, sx) - *x0);
    *h = (long)(cdiv(j->ysiz, sy) - *y0);
}

/** @brief Whether tile @p t's first three components can be turned
 *         back to RGB together: a transform named, and one size. */
static int
mct_fits(const jpx_t *j, const jpx_tile_t *t)
{
    int c;

    if (!t->st.mct || j->ncomp < 3) {
        return 0;
    }
    for (c = 1; c < 3; c++) {
        const jpx_res_t *a = &t->comp[0].res[t->comp[0].top];
        const jpx_res_t *b = &t->comp[c].res[t->comp[c].top];

        if (b->x1 - b->x0 != a->x1 - a->x0 || b->y1 - b->y0 != a->y1 - a->y0) {
            return 0;
        }
    }
    return 1;
}

/**
 * @brief Tile @p idx decoded into the component planes: laid out, its
 *        packets read, its code-blocks decoded, transformed back, and
 *        stored.  What its data does not reach stays at zero.
 */
static int
tile_decode(jpx_t *j, int idx, uint16_t **planes)
{
    jpx_tile_t *t = j->tile[idx];
    int c, rc = 0, kept = j->ncomp < TIKU_JPX_COMPS ? j->ncomp
                                                    : TIKU_JPX_COMPS;
    int64_t x0, y0;
    long w, h;

    if (tile_setup(j, t, idx) != 0) {
        tile_free(j, t);
        return -1;
    }
    (void)jpx_t2_decode(j, t);
    for (c = 0; rc == 0 && c < kept; c++) {
        rc = tier1(j, t, c) != 0 || jpx_idwt(&t->comp[c],
                                             t->st.ccod[c].reversible) != 0
             ? -1 : 0;
    }
    if (rc == 0 && mct_fits(j, t)) {
        const jpx_res_t *a = &t->comp[0].res[t->comp[0].top];

        jpx_mct(t->comp[0].coef, t->comp[1].coef, t->comp[2].coef,
                (long)(a->x1 - a->x0) * (a->y1 - a->y0),
                t->st.ccod[0].reversible);
    }
    for (c = 0; rc == 0 && c < kept; c++) {
        plane_area(j, c, &x0, &y0, &w, &h);
        store(j, t, c, planes[c], w, x0, y0);
    }
    tile_free(j, t);
    return rc;
}

/* ----------------------------------------------------------- the whole  */

/**
 * @brief The fewest finest resolutions to leave out for the picture to
 *        fit: its finest kept component in @p max_pixels, all of them
 *        in four times that.  @return 0 with j->reduce set, or -1.
 */
static int
fit(jpx_t *j, long max_pixels)
{
    for (j->reduce = 0; j->reduce <= JPX_LEVELS; j->reduce++) {
        int64_t most = 0, total = 0, x0, y0;
        long w, h;
        int c;

        for (c = 0; c < j->ncomp && c < TIKU_JPX_COMPS; c++) {
            plane_area(j, c, &x0, &y0, &w, &h);
            most = (int64_t)w * h > most ? (int64_t)w * h : most;
            total += (int64_t)w * h;
        }
        if (most < 1) {
            return -1;
        }
        if (most <= max_pixels && total <= 4 * (int64_t)max_pixels) {
            return 0;
        }
    }
    return -1;
}

/**
 * @brief The main header (A.4.1): SIZ first, PPM's headers gathered,
 *        then the styles, of which COD and QCD are not optional.  @p at
 *        gets where the first tile-part starts.  @return 0, or -1.
 */
static int
main_header(jpx_t *j, const unsigned char *p, long n, long *at)
{
    segs_t l = {NULL, 0, 0};
    long cap = 0;
    int i, rc = -1;

    if (n >= 4 && be16(p) == 0xFF4F && be16(p + 2) == 0xFF51 &&
        (*at = markers(p, n, 2, 0xFF90, &l)) >= 0 && l.n > 0 &&
        l.s[0].m == 0xFF51 && siz(j, l.s[0].p, l.s[0].n) == 0 &&
        gather(&l, 0xFF60, &j->ppm, &j->ppmlen, &cap) == 0 &&
        header(j, &j->main, l.s + 1, l.n - 1) == 0 && j->main.layers >= 1) {
        for (rc = 0, i = 0; i < j->ncomp; i++) {
            rc = j->main.quant[i].n == 0 ? -1 : rc;
        }
    }
    free(l.s);
    return rc;
}

int
jpx_codestream(jpx_t *j, const unsigned char *p, long n, long max_pixels,
               uint16_t **planes)
{
    int64_t x0, y0;
    long at, ppm_at = 0, w, h;
    int i, c, done = 0;

    memset(planes, 0, TIKU_JPX_COMPS * sizeof *planes);
    j->max_pixels = max_pixels;
    if (main_header(j, p, n, &at) != 0 || fit(j, max_pixels) != 0) {
        return -1;
    }
    while (at + 2 <= n && be16(p + at) == 0xFF90) {
        if ((at = tile_part(j, p, n, at, &ppm_at)) < 0) {
            break;                  /* the tile-parts read so far stand */
        }
    }
    for (c = 0; c < j->ncomp && c < TIKU_JPX_COMPS; c++) {
        plane_area(j, c, &x0, &y0, &w, &h);
        if ((planes[c] = calloc((size_t)w * (size_t)h + 1,
                                sizeof **planes)) == NULL) {
            return -1;
        }
    }
    for (i = 0; i < j->ntx * j->nty; i++) {
        if (j->tile[i] != NULL) {
            if (tile_decode(j, i, planes) != 0) {
                break;              /* those decoded stand */
            }
            done++;
        }
    }
    return done > 0 ? 0 : -1;
}

void
jpx_done(jpx_t *j)
{
    int i;

    for (i = 0; j->tile != NULL && i < j->ntx * j->nty; i++) {
        jpx_tile_t *t = j->tile[i];

        if (t == NULL) {
            continue;
        }
        tile_free(j, t);
        if (t->ownst) {
            style_free(&t->st);
        }
        if (t->ownpoc) {
            free(t->st.poc);
        }
        free(t->data);
        free(t->hdr);
        free(t);
    }
    free(j->tile);
    style_free(&j->main);
    free(j->main.poc);
    free(j->cm);
    free(j->ppm);
    memset(j, 0, sizeof *j);
}
