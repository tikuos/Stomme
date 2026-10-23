/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_jpx_t2.c - JPEG 2000's tier 2 (15444-1 Annex B): a tile's packets
 * in the order its progressions give them, each a header saying which
 * code-blocks it brings passes of and how many bytes, then the bytes.
 *
 * Headers are bits with a zero stuffed after 0xFF; inclusion and missing
 * bitplanes come by tag trees.  A packet of each layer, resolution,
 * component and precinct, read once however the progressions overlap.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_jpx_private.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

/** @brief A packet header's bits (B.10.1). */
typedef struct {
    const unsigned char *p;
    long                 n, pos;
    unsigned             buf;       /* the byte being read               */
    int                  ct;        /* its bits left                     */
    int                  over;      /* asked for past the bytes          */
} bio_t;

#define VISITS_MAX (1L << 26)      /* grid points a tile's positions see */

/** @brief Where a tile's packets are read from: the body, and the
 *         headers when PPT or PPM carry them apart. */
typedef struct {
    long pos, hpos;                 /* the next body byte; header byte   */
    int  sep;                       /* headers apart from the bodies     */
    long visits;                    /* grid points the positions saw     */
} src_t;

/** @brief The next header bit; a byte after 0xFF holds seven. */
static unsigned
bio_bit(bio_t *b)
{
    if (b->ct == 0) {
        unsigned prev = b->buf;

        if (b->pos >= b->n) {
            b->over = 1;
            return 0;
        }
        b->buf = b->p[b->pos++];
        b->ct = prev == 0xFFu ? 7 : 8;
    }
    b->ct--;
    return (b->buf >> b->ct) & 1u;
}

/** @brief @p k header bits (at most 32). */
static uint32_t
bio_bits(bio_t *b, int k)
{
    uint32_t v = 0;

    while (k-- > 0) {
        v = v << 1 | bio_bit(b);
    }
    return v;
}

/** @brief The header's end: its last byte, and the one stuffed after it
 *         when that was 0xFF. */
static void
bio_align(bio_t *b)
{
    if (b->buf == 0xFFu && b->ct < 8) {
        b->pos++;
    }
    b->ct = 0;
}

/* ----------------------------------------------------------- tag trees  */

int
jpx_tag_init(jpx_tag_t *tt, int w, int h)
{
    int lw[34], lh[34], off[34], nl = 0, l, i, k;

    memset(tt, 0, sizeof *tt);
    tt->w = w;
    tt->h = h;
    if (w <= 0 || h <= 0) {
        return 0;
    }
    for (lw[0] = w, lh[0] = h;; nl++) {
        off[nl] = tt->nnodes;
        tt->nnodes += lw[nl] * lh[nl];
        if (lw[nl] * lh[nl] == 1 || nl == 32) {
            break;
        }
        lw[nl + 1] = (lw[nl] + 1) / 2;
        lh[nl + 1] = (lh[nl] + 1) / 2;
    }
    tt->value = malloc((size_t)tt->nnodes * sizeof *tt->value);
    tt->low = calloc((size_t)tt->nnodes, sizeof *tt->low);
    tt->parent = malloc((size_t)tt->nnodes * sizeof *tt->parent);
    if (tt->value == NULL || tt->low == NULL || tt->parent == NULL) {
        jpx_tag_free(tt);
        return -1;
    }
    for (l = 0; l <= nl; l++) {
        for (i = 0; i < lw[l] * lh[l]; i++) {
            k = off[l] + i;
            tt->value[k] = INT_MAX;
            tt->parent[k] = l == nl ? -1 : off[l + 1] +
                            (i / lw[l] / 2) * lw[l + 1] + (i % lw[l]) / 2;
        }
    }
    return 0;
}

void
jpx_tag_free(jpx_tag_t *tt)
{
    free(tt->value);
    free(tt->low);
    free(tt->parent);
    memset(tt, 0, sizeof *tt);
}

/**
 * @brief Whether leaf @p leaf's value is below @p threshold (B.10.2):
 *        down from the root, each node's value known once its bits say
 *        so, each node's lower bound kept for the next question.
 */
static int
tag_decode(jpx_tag_t *tt, bio_t *b, int leaf, int threshold)
{
    int path[34], np = 0, node = leaf, low = 0;

    while (node >= 0 && np < 34) {
        path[np++] = node;
        node = tt->parent[node];
    }
    while (np > 0) {
        node = path[--np];
        if (low > tt->low[node]) {
            tt->low[node] = low;
        } else {
            low = tt->low[node];
        }
        while (low < threshold && low < tt->value[node] && !b->over) {
            if (bio_bit(b)) {
                tt->value[node] = low;
            } else {
                low++;
            }
        }
        tt->low[node] = low;
    }
    return tt->value[leaf] < threshold;
}

/* ------------------------------------------------------- one packet  */

/** @brief How many coding passes the packet brings (Table B.4). */
static int
num_passes(bio_t *b)
{
    uint32_t v;

    if (!bio_bit(b)) return 1;
    if (!bio_bit(b)) return 2;
    if ((v = bio_bits(b, 2)) < 3) return 3 + (int)v;
    if ((v = bio_bits(b, 5)) < 31) return 6 + (int)v;
    return 37 + (int)bio_bits(b, 7);
}

/** @brief How many passes segment @p s of a code-block may hold: one
 *         each when every pass is terminated; with bypass, the first
 *         ten, then two raw and one arithmetic by turns. */
static int
seg_room(const jpx_cblk_t *cb, int s, int style)
{
    if (style & JPX_TERMALL) {
        return 1;
    }
    if (style & JPX_BYPASS) {
        if (s == 0) {
            return 10;
        }
        return cb->seg[s - 1].maxpasses == 1 ? 2 :
               cb->seg[s - 1].maxpasses == 10 ? 2 : 1;
    }
    return 109;
}

/** @brief A new codeword segment on @p cb, room made for it.
 *  @return 0, or -1 past the most a code-block can have. */
static int
seg_add(jpx_cblk_t *cb, int style)
{
    if (cb->nseg == cb->capseg) {
        int cap = cb->capseg ? cb->capseg * 2 : (style & (JPX_TERMALL |
                                                          JPX_BYPASS)) ? 8 : 1;
        jpx_seg_t *grown;

        cap = cap < JPX_SEGS ? cap : JPX_SEGS;
        if (cb->nseg == JPX_SEGS ||
            (grown = realloc(cb->seg, (size_t)cap * sizeof *grown)) == NULL) {
            return -1;
        }
        cb->seg = grown;
        cb->capseg = cap;
    }
    memset(&cb->seg[cb->nseg], 0, sizeof cb->seg[0]);
    cb->seg[cb->nseg].maxpasses = seg_room(cb, cb->nseg, style);
    cb->nseg++;
    return 0;
}

/** @brief Floor of log2 @p v, @p v at least 1. */
static int
floor_log2(int v)
{
    int k = 0;

    while (v > 1) {
        v >>= 1;
        k++;
    }
    return k;
}

/**
 * @brief A code-block's part of a packet header (B.10.3 to B.10.7):
 *        inclusion, missing bitplanes the first time, passes, Lblock,
 *        and a length for each codeword segment the passes reach.
 */
static int
cblk_header(bio_t *b, jpx_pband_t *pb, int k, const jpx_band_t *band,
            int layer, int style, int roi)
{
    jpx_cblk_t *cb = &pb->cblk[k];
    int included, n, zbp = 0, take;

    cb->nnew = 0;
    included = cb->incl ? (int)bio_bit(b)
                        : tag_decode(&pb->incl, b, k, layer + 1);
    if (!included || b->over) {
        return b->over ? -1 : 0;
    }
    if (!cb->incl) {
        while (!tag_decode(&pb->zbp, b, k, zbp + 1) && zbp < 80 && !b->over) {
            zbp++;
        }
        cb->numbps = band->mb + roi - zbp;
        cb->lblock = 3;
        cb->incl = 1;
    }
    n = num_passes(b);
    while (bio_bit(b) && cb->lblock < 32 && !b->over) {
        cb->lblock++;
    }
    if ((cb->nseg == 0 ||
         cb->seg[cb->nseg - 1].npasses == cb->seg[cb->nseg - 1].maxpasses) &&
        seg_add(cb, style) != 0) {
        return -1;
    }
    cb->pfirst = cb->nseg - 1;
    for (;;) {
        int s = cb->nseg - 1, bits;

        take = cb->seg[s].maxpasses - cb->seg[s].npasses;
        take = take < n ? take : n;
        bits = cb->lblock + floor_log2(take);
        cb->seg[s].newlen = (long)bio_bits(b, bits > 32 ? 32 : bits);
        cb->seg[s].npasses += take;
        cb->npasses += take;
        cb->nnew = 1;
        if ((n -= take) <= 0) {
            break;
        }
        if (seg_add(cb, style) != 0) {
            return -1;
        }
    }
    return b->over ? -1 : 0;
}

/** @brief A code-block's new bytes appended from @p p at @p *pos, as its
 *         header's lengths say.  @return 0, or -1 when they run out. */
static int
cblk_body(jpx_cblk_t *cb, const unsigned char *p, long n, long *pos)
{
    int s;

    for (s = cb->pfirst; cb->nnew && s < cb->nseg; s++) {
        long len = cb->seg[s].newlen;

        if (len < 0 || len > n - *pos) {
            return -1;
        }
        if (cb->len + len > cb->cap) {
            long cap = (cb->len + len) * 2 + 64;
            unsigned char *grown = realloc(cb->data, (size_t)cap);

            if (grown == NULL) {
                return -1;
            }
            cb->data = grown;
            cb->cap = cap;
        }
        if (len > 0) {
            memcpy(cb->data + cb->len, p + *pos, (size_t)len);
        }
        cb->len += len;
        cb->seg[s].len += len;
        *pos += len;
    }
    return 0;
}

/** @brief Past a marker @p m (0xFF91 SOP, 6 bytes, or 0xFF92 EPH, 2)
 *         when it stands at @p *pos. */
static void
skip_marker(const unsigned char *p, long n, long *pos, unsigned m)
{
    if (*pos + 2 <= n && p[*pos] == 0xFF && p[*pos + 1] == (m & 0xFF)) {
        *pos += m == 0xFF91u ? 6 : 2;
    }
}

/**
 * @brief Packet (@p l, @p r, @p c, @p p) of tile @p t (B.9): its header,
 *        from the body or from PPT/PPM's headers, then its code-blocks'
 *        bytes.  @return 0, or -1 when the data runs out.
 */
static int
packet(jpx_tile_t *t, int l, int r, int c, int p, src_t *src)
{
    jpx_tcomp_t *tc = &t->comp[c];
    jpx_res_t *res = &tc->res[r];
    const unsigned char *hp = src->sep ? t->hdr : t->data;
    long hn = src->sep ? t->hlen : t->len, *hpos = src->sep ? &src->hpos
                                                              : &src->pos;
    bio_t b;
    int bi, k;

    if (t->st.sop) {
        skip_marker(t->data, t->len, &src->pos, 0xFF91u);
    }
    memset(&b, 0, sizeof b);
    b.p = hp + *hpos;
    b.n = hn - *hpos;
    if (b.n <= 0) {
        return -1;
    }
    if (bio_bit(&b)) {              /* not empty */
        for (bi = 0; bi < res->nbands; bi++) {
            jpx_pband_t *pb = &res->prec[p * res->nbands + bi];

            for (k = 0; k < pb->cw * pb->ch; k++) {
                if (cblk_header(&b, pb, k, &res->band[bi], l,
                                t->st.ccod[c].style, t->st.roi[c]) != 0) {
                    return -1;
                }
            }
        }
    } else {
        for (bi = 0; bi < res->nbands; bi++) {
            jpx_pband_t *pb = &res->prec[p * res->nbands + bi];

            for (k = 0; k < pb->cw * pb->ch; k++) {
                pb->cblk[k].nnew = 0;
            }
        }
    }
    bio_align(&b);
    *hpos += b.pos;
    if (t->st.eph) {
        skip_marker(hp, hn, hpos, 0xFF92u);
    }
    for (bi = 0; bi < res->nbands; bi++) {
        jpx_pband_t *pb = &res->prec[p * res->nbands + bi];

        for (k = 0; k < pb->cw * pb->ch; k++) {
            if (cblk_body(&pb->cblk[k], t->data, t->len, &src->pos) != 0) {
                return -1;
            }
        }
    }
    return 0;
}

/* ----------------------------------------------------- progressions  */

/** @brief Packet (@p l, @p r, @p c, @p p) read when it is the next of
 *         its precinct, and not twice.  @return 0, or -1 at the end. */
static int
next(jpx_tile_t *t, int l, int r, int c, int p, src_t *src)
{
    jpx_res_t *res = &t->comp[c].res[r];

    if (p < 0 || p >= res->pw * res->ph || res->layers[p] != l) {
        return 0;
    }
    res->layers[p]++;
    return packet(t, l, r, c, p, src);
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

/**
 * @brief The precinct of component @p c at resolution @p r that starts
 *        at reference grid point @p x, @p y (B.12.1.3): -1 when none
 *        does, as the standard's position loops ask it.
 */
static int
at_position(const jpx_t *j, const jpx_tile_t *t, int c, int r, int64_t x,
            int64_t y)
{
    const jpx_tcomp_t *tc = &t->comp[c];
    const jpx_res_t *res;
    int lv, rpx, rpy;
    int64_t sx, sy, trx0, try0;

    if (r > tc->nl) {
        return -1;
    }
    res = &tc->res[r];
    lv = tc->nl - r;
    rpx = res->ppx + lv;
    rpy = res->ppy + lv;
    sx = (int64_t)j->cm[c].dx << lv;
    sy = (int64_t)j->cm[c].dy << lv;
    trx0 = cdiv(t->x0, sx);
    try0 = cdiv(t->y0, sy);
    if (res->pw == 0 || res->ph == 0 || rpx > 60 || rpy > 60 ||
        !(y % ((int64_t)j->cm[c].dy << rpy) == 0 ||
          (y == t->y0 && (try0 << lv) % ((int64_t)1 << rpy) != 0)) ||
        !(x % ((int64_t)j->cm[c].dx << rpx) == 0 ||
          (x == t->x0 && (trx0 << lv) % ((int64_t)1 << rpx) != 0))) {
        return -1;
    }
    return (int)(fdiv(cdiv(x, sx), (int64_t)1 << res->ppx) -
                 fdiv(trx0, (int64_t)1 << res->ppx) +
                 (fdiv(cdiv(y, sy), (int64_t)1 << res->ppy) -
                  fdiv(try0, (int64_t)1 << res->ppy)) * res->pw);
}

/** @brief The greatest common divisor of @p a and @p b. */
static int64_t
gcd(int64_t a, int64_t b)
{
    while (b != 0) {
        int64_t t = a % b;

        a = b;
        b = t;
    }
    return a;
}

/** @brief The step between the reference grid points the position-first
 *         progressions visit: every precinct start of components
 *         @p c0 to @p c1 at @p g's resolutions falls on one. */
static void
steps(const jpx_t *j, const jpx_tile_t *t, const jpx_prog_t *g, int c0,
      int c1, int64_t *sx, int64_t *sy)
{
    int c, r;

    *sx = *sy = 0;
    for (c = c0; c < c1; c++) {
        for (r = g->rs; r < g->re && r <= t->comp[c].nl; r++) {
            int lv = t->comp[c].nl - r;
            const jpx_res_t *res = &t->comp[c].res[r];

            if (res->ppx + lv <= 60 && res->ppy + lv <= 60) {
                *sx = gcd((int64_t)j->cm[c].dx << (res->ppx + lv), *sx);
                *sy = gcd((int64_t)j->cm[c].dy << (res->ppy + lv), *sy);
            }
        }
    }
    if (*sx < 1) *sx = (int64_t)t->x1 - t->x0 + 1;
    if (*sy < 1) *sy = (int64_t)t->y1 - t->y0 + 1;
}

/** @brief Every layer of the precinct at @p x, @p y of component @p c,
 *         resolution @p r.  @return 0, or -1 at the data's end. */
static int
layers_at(const jpx_t *j, jpx_tile_t *t, const jpx_prog_t *g, int c, int r,
          int64_t x, int64_t y, src_t *src)
{
    int p = at_position(j, t, c, r, x, y), l;

    for (l = 0; p >= 0 && l < g->le; l++) {
        if (next(t, l, r, c, p, src) != 0) {
            return -1;
        }
    }
    return 0;
}

/** @brief One grid point's packets: for each component and resolution
 *         in the order @p g's name gives after the position. */
static int
at_point(const jpx_t *j, jpx_tile_t *t, const jpx_prog_t *g, int fixed,
         int64_t x, int64_t y, src_t *src)
{
    int c, r;

    if (g->order == JPX_RPCL) {             /* fixed: the resolution */
        for (c = g->cs; c < g->ce; c++) {
            if (layers_at(j, t, g, c, fixed, x, y, src) != 0) return -1;
        }
    } else if (g->order == JPX_CPRL) {      /* fixed: the component */
        for (r = g->rs; r < g->re; r++) {
            if (layers_at(j, t, g, fixed, r, x, y, src) != 0) return -1;
        }
    } else {
        for (c = g->cs; c < g->ce; c++) {
            for (r = g->rs; r < g->re; r++) {
                if (layers_at(j, t, g, c, r, x, y, src) != 0) return -1;
            }
        }
    }
    return 0;
}

/** @brief The position-first progressions (B.12.1.3 to B.12.1.5): RPCL
 *         a resolution at a time, CPRL a component at a time, PCRP all
 *         at once, over the tile's grid.  @return 0, or -1 at the end. */
static int
by_position(const jpx_t *j, jpx_tile_t *t, const jpx_prog_t *g, src_t *src)
{
    int64_t sx, sy, x, y;
    int a, a0 = 0, a1 = 1;

    if (g->order == JPX_RPCL) {
        a0 = g->rs;
        a1 = g->re;
    } else if (g->order == JPX_CPRL) {
        a0 = g->cs;
        a1 = g->ce;
    }
    for (a = a0; a < a1; a++) {
        if (g->order == JPX_CPRL) {
            steps(j, t, g, a, a + 1, &sx, &sy);
        } else {
            steps(j, t, g, g->cs, g->ce, &sx, &sy);
        }
        for (y = t->y0; y < t->y1; y += sy - (y % sy)) {
            for (x = t->x0; x < t->x1; x += sx - (x % sx)) {
                if (++src->visits > VISITS_MAX ||
                    at_point(j, t, g, a, x, y, src) != 0) {
                    return -1;
                }
            }
        }
    }
    return 0;
}

/** @brief Every packet of progression @p g in its order.  @return 0, or
 *         -1 at the data's end. */
static int
progression(const jpx_t *j, jpx_tile_t *t, const jpx_prog_t *g, src_t *src)
{
    int l, r, c, p;

    if (g->order == JPX_LRCP || g->order == JPX_RLCP) {
        int outer1 = g->order == JPX_LRCP ? g->le : g->re;
        int outer0 = g->order == JPX_LRCP ? 0 : g->rs, o, i;

        for (o = outer0; o < outer1; o++) {
            int in0 = g->order == JPX_LRCP ? g->rs : 0;
            int in1 = g->order == JPX_LRCP ? g->re : g->le;

            for (i = in0; i < in1; i++) {
                l = g->order == JPX_LRCP ? o : i;
                r = g->order == JPX_LRCP ? i : o;
                for (c = g->cs; c < g->ce; c++) {
                    if (r > t->comp[c].nl) {
                        continue;
                    }
                    for (p = 0; p < t->comp[c].res[r].pw *
                                    t->comp[c].res[r].ph; p++) {
                        if (next(t, l, r, c, p, src) != 0) {
                            return -1;
                        }
                    }
                }
            }
        }
        return 0;
    }
    return by_position(j, t, g, src);
}

int
jpx_t2_decode(jpx_t *j, jpx_tile_t *t)
{
    jpx_prog_t whole, *g;
    src_t src;
    int i, n;

    memset(&src, 0, sizeof src);
    src.sep = t->hlen > 0;
    whole.order = t->st.order;
    whole.rs = 0;
    whole.re = JPX_LEVELS + 1;
    whole.cs = 0;
    whole.ce = j->ncomp;
    whole.le = t->st.layers;
    g = t->st.npoc > 0 ? t->st.poc : &whole;
    n = t->st.npoc > 0 ? t->st.npoc : 1;
    for (i = 0; i < n; i++) {
        jpx_prog_t q = g[i];

        q.ce = q.ce < j->ncomp ? q.ce : j->ncomp;
        q.le = q.le < t->st.layers ? q.le : t->st.layers;
        q.re = q.re < JPX_LEVELS + 1 ? q.re : JPX_LEVELS + 1;
        if (progression(j, t, &q, &src) != 0) {
            return -1;
        }
    }
    return 0;
}
