/*
 * KintsugiOS v0.01 kits -- the foundation KintsugiDesktop and KintsugiTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@kinstugi-os.org>
 *
 * tiku_jpeg.c - JPEG, sequential and progressive: Huffman, dequantise,
 * IDCT, chroma upsample, YCbCr to RGB.
 *
 * A sequential file of one scan is drawn block by block as it is read.
 * Anything else -- progressive scans that each add a band of
 * frequencies or one bit more (T.81 Annex G), or components sent in
 * scans of their own -- keeps every block's coefficients until the
 * last scan, then draws them all.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_jpeg.h"
#include "tiku_image.h"

#include <stdlib.h>
#include <string.h>

#define MAXCOMP 4

/** @brief One Huffman table, in the canonical form the standard defines. */
typedef struct {
    unsigned char bits[17];         /* how many codes of each length 1..16 */
    unsigned char huffval[256];
    int           mincode[17];
    int           maxcode[18];
    int           valptr[17];
    int           present;
} jhuff_t;

/** @brief One image component and where its samples live. */
typedef struct {
    int            id, h, v, tq;    /* sampling factors, quant table      */
    int            td, ta;          /* DC and AC table selectors          */
    int            dcpred;
    int            bw, bh;          /* size in blocks, MCU-aligned        */
    int            ow, oh;          /* its own blocks, a lone scan's grid */
    unsigned char *pix;             /* bw*8 by bh*8 samples               */
    short         *coef;            /* bw*bh blocks of 64, when kept      */
} jcomp_t;

typedef struct {
    const unsigned char *p;
    long                 n, pos;
    unsigned long        buf;
    int                  cnt;
    int                  bad;
} jbits_t;

/** @brief A decode under way: the tables, the frame, the scan. */
typedef struct {
    const unsigned char *src;       /* the file                           */
    long                 n;
    unsigned short       qt[4][64]; /* quantisers, in zig-zag order       */
    jhuff_t              dc[4], ac[4];
    jcomp_t              comp[MAXCOMP];
    int                  w, h, ncomp, hmax, vmax;
    int                  mcux, mcuy;    /* MCUs across and down           */
    int                  restart;   /* MCUs between restarts, or 0        */
    int                  transform; /* Adobe APP14: 2 YCCK, 0 CMYK        */
    int                  progressive;   /* SOF2                           */
    int short_memory; /**< an image-plane allocation was refused */
    int                  kept;      /* coefficients kept for a last IDCT  */
    int                  scans;     /* scans read to their end            */
    int                  ss, se, ah, al;    /* the scan's band and bits  */
    int                  eobrun;    /* blocks left whose band is done     */
    jbits_t              bits;
} jdec_t;

/** @brief The zig-zag order coefficients arrive in. */
static const unsigned char zigzag[64] = {
     0,  1,  8, 16,  9,  2,  3, 10, 17, 24, 32, 25, 18, 11,  4,  5,
    12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13,  6,  7, 14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51,
    58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63
};

/**
 * @brief One bit of entropy-coded data.
 *
 * A 0xFF in the stream is followed by a stuffed 0x00; any other byte after
 * 0xFF is a marker, which ends the data.
 */
static int
getbit(jbits_t *b)
{
    if (b->cnt == 0) {
        int c;

        if (b->pos >= b->n) {
            b->bad = 1;
            return 0;
        }
        c = b->p[b->pos++];
        if (c == 0xff) {
            if (b->pos < b->n && b->p[b->pos] == 0x00) {
                b->pos++;
            } else {
                b->bad = 1;         /* a marker: the scan is over */
                return 0;
            }
        }
        b->buf = (unsigned long)c;
        b->cnt = 8;
    }
    b->cnt--;
    return (int)((b->buf >> b->cnt) & 1ul);
}

/** @brief @p n bits, the first read the most significant. */
static int
getbits(jbits_t *b, int n)
{
    int v = 0;

    while (n-- > 0) {
        v = (v << 1) | getbit(b);
    }
    return v;
}

/** @brief Turn a raw magnitude into its signed value (standard F.2.2.1). */
static int
extend(int v, int t)
{
    return (t == 0) ? 0 : ((v < (1 << (t - 1))) ? v - (1 << t) + 1 : v);
}

/** @brief Derive the decoding tables the standard's DECODE needs. */
static void
huff_build(jhuff_t *h)
{
    int code = 0, k = 0, i;

    for (i = 1; i <= 16; i++) {
        h->valptr[i] = k;
        h->mincode[i] = code;
        code += h->bits[i];
        k += h->bits[i];
        h->maxcode[i] = code - 1;
        code <<= 1;
        if (h->bits[i] == 0) {
            h->maxcode[i] = -1;     /* no codes of this length */
        }
    }
    h->maxcode[17] = 0x7fffffff;
}

/** @brief One Huffman-coded value (the standard's DECODE), or -1. */
static int
huff_decode(jbits_t *b, const jhuff_t *h)
{
    if (!h->present) return -1;
    int code = getbit(b), l = 1;

    while (l <= 16 && (h->maxcode[l] < 0 || code > h->maxcode[l])) {
        code = (code << 1) | getbit(b);
        l++;
        if (b->bad) {
            return -1;
        }
    }
    if (l > 16) {
        return -1;
    }
    int at = h->valptr[l] + code - h->mincode[l];
    return at >= 0 && at < 256 ? h->huffval[at] : -1;
}

/** @brief Separable 8x8 inverse DCT in integers, rows then columns: each
 *         output the sum of its inputs times a fixed cosine table. */
static void
idct8(const int *in, unsigned char *out, int stride)
{
    int tmp[64], u, x, y;

    /* Read-only: decoders on several threads share it. */
    static const int cs[8][8] = {
            { 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024 },
            { 1420, 1204,  805,  283, -283, -805,-1204,-1420 },
            { 1338,  554, -554,-1338,-1338, -554,  554, 1338 },
            { 1204, -283,-1420, -805,  805, 1420,  283,-1204 },
            { 1024,-1024,-1024, 1024, 1024,-1024,-1024, 1024 },
            {  805,-1420,  283, 1204,-1204, -283, 1420, -805 },
            {  554,-1338, 1338, -554, -554, 1338,-1338,  554 },
            {  283, -805, 1204,-1420, 1420,-1204,  805, -283 }
    };
    for (y = 0; y < 8; y++) {       /* rows */
        for (x = 0; x < 8; x++) {
            long s = 0;

            for (u = 0; u < 8; u++) {
                s += (long)in[y * 8 + u] * cs[u][x];
            }
            /* Rounded: without the 512, the arithmetic shift
             * of a negative sum biases every block half a
             * level dark. */
            tmp[y * 8 + x] = (int)((s + 512L) >> 10);
        }
    }
    for (x = 0; x < 8; x++) {       /* columns */
        for (y = 0; y < 8; y++) {
            long s = 0;
            int v;

            for (u = 0; u < 8; u++) {
                s += (long)tmp[u * 8 + x] * cs[u][y];
            }
            v = (int)((s + 4096L) >> 13) + 128;
            out[y * stride + x] = (unsigned char)((v < 0) ? 0 :
                                                  ((v > 255) ? 255 : v));
        }
    }
}

/** @brief @p v held to a byte. */
static int
clamp8(int v)
{
    return (v < 0) ? 0 : ((v > 255) ? 255 : v);
}

/** @brief DQT: 8-bit tables.  @return 0, 1 for a 16-bit one (not read),
 *         or -1 when malformed. */
static int
read_dqt(jdec_t *d, long i, int len)
{
    const unsigned char *src = d->src;
    long k = i + 2;

    while (k < i + len) {
        int prec = src[k] >> 4, id = src[k] & 15, z;

        k++;
        if (id > 3 || (prec != 0 && prec != 1) ||
            k + 64 * (prec + 1) > i + len) {
            return -1;
        }
        if (prec) {
            return 1;
        }
        for (z = 0; z < 64; z++) {
            d->qt[id][z] = src[k++];
        }
    }
    return 0;
}

/** @brief DHT: Huffman tables.  @return 0, or -1 when malformed. */
static int
read_dht(jdec_t *d, long i, int len)
{
    const unsigned char *src = d->src;
    long k = i + 2;

    while (k < i + len) {
        int cls = src[k] >> 4, id = src[k] & 15, total = 0, z;
        jhuff_t *t;

        k++;
        if (id > 3 || cls > 1 || k + 16 > i + len) {
            return -1;
        }
        t = cls ? &d->ac[id] : &d->dc[id];
        memset(t, 0, sizeof *t);
        for (z = 1; z <= 16; z++) {
            t->bits[z] = src[k++];
            total += t->bits[z];
        }
        if (total > 256 || k + total > i + len) {
            return -1;
        }
        for (z = 0; z < total; z++) {
            t->huffval[z] = src[k++];
        }
        huff_build(t);
        t->present = 1;
    }
    return 0;
}

/** @brief SOF0, 1 or 2: the frame.  @return 0, 1 for 12-bit samples, or
 *         -1 when malformed. */
static int
read_sof(jdec_t *d, long i, int len)
{
    const unsigned char *src = d->src;
    int c;

    if (len < 8 || d->ncomp) {
        return -1;
    }
    if (src[i + 2] != 8) {
        return 1;
    }
    d->h = (src[i + 3] << 8) | src[i + 4];
    d->w = (src[i + 5] << 8) | src[i + 6];
    d->ncomp = src[i + 7];
    if ((d->ncomp != 1 && d->ncomp != 3 && d->ncomp != 4) || d->w <= 0 ||
        d->h <= 0 || len != 8 + 3 * d->ncomp ||
        (size_t)d->w > TIKU_IMAGE_PIXELS_MAX / (size_t)d->h) {
        return -1;
    }
    for (c = 0; c < d->ncomp; c++) {
        const unsigned char *e = src + i + 8 + c * 3;
        jcomp_t *k = &d->comp[c];

        k->id = e[0];
        k->h = e[1] >> 4;
        k->v = e[1] & 15;
        k->tq = e[2];
        if (k->h < 1 || k->h > 4 || k->v < 1 || k->v > 4 || k->tq > 3) {
            return -1;
        }
        d->hmax = k->h > d->hmax ? k->h : d->hmax;
        d->vmax = k->v > d->vmax ? k->v : d->vmax;
    }
    return 0;
}

/**
 * @brief SOS: the components the scan holds, their tables, its band of
 *        frequencies and its bits.
 * @return how many components, into @p which by index, or -1.
 */
static int
read_sos(jdec_t *d, long i, int len, int *which)
{
    const unsigned char *src = d->src;
    int ns = src[i + 2], c, k;

    if (ns < 1 || ns > d->ncomp || len != 6 + 2 * ns) {
        return -1;
    }
    for (c = 0; c < ns; c++) {
        int id = src[i + 3 + c * 2];

        which[c] = -1;
        for (k = 0; k < d->ncomp; k++) {
            if (d->comp[k].id == id) {
                which[c] = k;
                d->comp[k].td = src[i + 4 + c * 2] >> 4;
                d->comp[k].ta = src[i + 4 + c * 2] & 15;
            }
        }
        if (which[c] < 0 || d->comp[which[c]].td > 3 ||
            d->comp[which[c]].ta > 3) {
            return -1;
        }
    }
    d->ss = src[i + 3 + 2 * ns];
    d->se = src[i + 4 + 2 * ns];
    d->ah = src[i + 5 + 2 * ns] >> 4;
    d->al = src[i + 5 + 2 * ns] & 15;
    if (d->progressive &&
        ((d->ss == 0 ? d->se != 0 : ns != 1 || d->se < d->ss ||
          d->se > 63) || d->ah > 13 || d->al > 13)) {
        return -1;                  /* a band no progressive scan has */
    }
    return ns;
}

/** @brief The plane each component is drawn into, and with d->kept its
 *         coefficients, sized in whole MCUs: the blocks of an MCU that
 *         runs past the picture's edge are decoded into it too.
 *         @return 0, or -1. */
static int
planes(jdec_t *d)
{
    int c;

    d->mcux = (d->w + d->hmax * 8 - 1) / (d->hmax * 8);
    d->mcuy = (d->h + d->vmax * 8 - 1) / (d->vmax * 8);
    for (c = 0; c < d->ncomp; c++) {
        jcomp_t *k = &d->comp[c];

        k->bw = d->mcux * k->h;
        k->bh = d->mcuy * k->v;
        k->ow = (d->w * k->h + d->hmax * 8 - 1) / (d->hmax * 8);
        k->oh = (d->h * k->v + d->vmax * 8 - 1) / (d->vmax * 8);
        k->pix = calloc((size_t)k->bw * 8u * (size_t)k->bh * 8u, 1u);
        if (k->pix == NULL) {
            d->short_memory = 1;
            return -1;
        }
        if (d->kept) {
            k->coef = calloc((size_t)k->bw * (size_t)k->bh * 64u,
                             sizeof *k->coef);
            if (k->coef == NULL) {
                d->short_memory = 1;
                return -1;
            }
        }
    }
    return 0;
}

/** @brief A sequential block: the DC difference, then the AC runs, into
 *         @p blk in natural order, still quantised.  @return 0 or -1. */
static int
seq_block(jdec_t *d, jcomp_t *c, int *blk)
{
    int t, k;

    memset(blk, 0, 64 * sizeof *blk);
    t = huff_decode(&d->bits, &d->dc[c->td]);
    if (t < 0 || t > 11) {
        return -1;
    }
    c->dcpred += extend(getbits(&d->bits, t), t);
    if (abs(c->dcpred) > 32767) {
        return -1;
    }
    blk[0] = c->dcpred;
    for (k = 1; k < 64; ) {
        int rs = huff_decode(&d->bits, &d->ac[c->ta]), r, sz;

        if (rs < 0) {
            return -1;
        }
        r = rs >> 4;
        sz = rs & 15;
        if (sz > 10) {
            return -1;
        }
        if (sz == 0) {
            if (r != 15) {
                break;
            }
            k += 16;
            continue;
        }
        k += r;
        if (k > 63) {
            return -1;
        }
        blk[zigzag[k]] = extend(getbits(&d->bits, sz), sz);
        k++;
    }
    return 0;
}

/** @brief A progressive DC scan's block: its first bits, or one more. */
static int
dc_block(jdec_t *d, jcomp_t *c, short *co)
{
    if (d->ah == 0) {
        int t = huff_decode(&d->bits, &d->dc[c->td]);

        if (t < 0 || t > 11) {
            return -1;
        }
        c->dcpred += extend(getbits(&d->bits, t), t);
        if (abs(c->dcpred) > 32767) {
            return -1;
        }
        co[0] = (short)(c->dcpred * (1 << d->al));
    } else if (getbit(&d->bits)) {
        co[0] = (short)(co[0] | (1 << d->al));
    }
    return d->bits.bad ? -1 : 0;
}

/** @brief A progressive AC scan's first bits of a block's band; a run of
 *         blocks with nothing in it is counted, not coded. */
static int
ac_first(jdec_t *d, jcomp_t *c, short *co)
{
    int k;

    if (d->eobrun > 0) {
        d->eobrun--;
        return 0;
    }
    for (k = d->ss; k <= d->se; ) {
        int rs = huff_decode(&d->bits, &d->ac[c->ta]), r, s;

        if (rs < 0) {
            return -1;
        }
        r = rs >> 4;
        s = rs & 15;
        if (s == 0) {
            if (r < 15) {
                d->eobrun = (1 << r) - 1 + (r ? getbits(&d->bits, r) : 0);
                break;
            }
            k += 16;                /* sixteen zeros */
            continue;
        }
        k += r;
        if (k > 63 || s > 10) {
            return -1;
        }
        co[zigzag[k]] = (short)(extend(getbits(&d->bits, s), s) *
                                (1 << d->al));
        k++;
    }
    return d->bits.bad ? -1 : 0;
}

/** @brief A correction bit for a coefficient already nonzero: a 1 moves
 *         it one step of the bit being coded away from zero. */
static void
correct(jdec_t *d, short *v)
{
    int p1 = 1 << d->al;

    if (getbit(&d->bits) && (*v & p1) == 0) {
        *v = (short)(*v >= 0 ? *v + p1 : *v - p1);
    }
}

/**
 * @brief A progressive AC scan's next bit of a block's band (T.81
 *        G.1.2.3): a coefficient new at this bit is 1 or -1 of it, each
 *        one already nonzero that the run passes gets a correction bit.
 */
static int
ac_refine(jdec_t *d, jcomp_t *c, short *co)
{
    int k = d->ss;

    for (; d->eobrun == 0 && k <= d->se; k++) {
        int rs = huff_decode(&d->bits, &d->ac[c->ta]), r, s = 0;

        if (rs < 0) {
            return -1;
        }
        r = rs >> 4;
        if ((rs & 15) != 0) {
            s = getbit(&d->bits) ? 1 << d->al : -(1 << d->al);
        } else if (r != 15) {
            d->eobrun = (1 << r) + (r ? getbits(&d->bits, r) : 0);
            break;                  /* the rest by the run below */
        }
        for (; k <= d->se; k++) {   /* to the r'th zero past the nonzero */
            short *v = &co[zigzag[k]];

            if (*v != 0) {
                correct(d, v);
            } else if (--r < 0) {
                break;
            }
        }
        if (s != 0 && k <= d->se) {
            co[zigzag[k]] = (short)s;
        }
    }
    if (d->eobrun > 0) {
        for (; k <= d->se; k++) {
            if (co[zigzag[k]] != 0) {
                correct(d, &co[zigzag[k]]);
            }
        }
        d->eobrun--;
    }
    return d->bits.bad ? -1 : 0;
}

/** @brief The block of @p c at block row @p row and column @p col, read
 *         as the scan says: drawn at once, or into its coefficients. */
static int
block(jdec_t *d, jcomp_t *c, int row, int col)
{
    int blk[64], k;
    short *co;

    if (!d->kept) {
        if (seq_block(d, c, blk) != 0) {
            return -1;
        }
        for (k = 0; k < 64; k++) {
            blk[zigzag[k]] *= d->qt[c->tq][k];
        }
        idct8(blk, c->pix + (long)row * 8 * c->bw * 8 + (long)col * 8,
              c->bw * 8);
        return 0;
    }
    co = c->coef + ((long)row * c->bw + col) * 64;
    if (!d->progressive) {
        if (seq_block(d, c, blk) != 0) {
            return -1;
        }
        for (k = 0; k < 64; k++) {
            co[k] = (short)blk[k];
        }
        return 0;
    }
    if (d->ss == 0) {
        return dc_block(d, c, co);
    }
    return d->ah == 0 ? ac_first(d, c, co) : ac_refine(d, c, co);
}

/** @brief At a restart marker: past it, with the predictors and the run
 *         of empty bands reset. */
static void
resync(jdec_t *d, int ns, const int *which)
{
    jbits_t *b = &d->bits;
    int c;

    b->cnt = 0;
    if (b->bad && b->pos > 0 && d->src[b->pos - 1] == 0xff) {
        b->pos--;                   /* the marker read into: step back */
    }
    while (b->pos + 1 < d->n &&
           !(d->src[b->pos] == 0xff && d->src[b->pos + 1] >= 0xd0 &&
             d->src[b->pos + 1] <= 0xd7)) {
        b->pos++;
    }
    b->pos += 2;
    b->bad = 0;
    for (c = 0; c < ns; c++) {
        d->comp[which[c]].dcpred = 0;
    }
    d->eobrun = 0;
}

/** @brief One scan: MCU by MCU when it holds several components, block
 *         by block over its component's own grid when it holds one.
 *  @return 0, or -1 where the data went bad. */
static int
scan(jdec_t *d, int ns, const int *which)
{
    jcomp_t *one = &d->comp[which[0]];
    long units = ns > 1 ? (long)d->mcux * d->mcuy : (long)one->ow * one->oh;
    int across = ns > 1 ? d->mcux : one->ow, c;
    long u;

    for (c = 0; c < ns; c++) {
        d->comp[which[c]].dcpred = 0;
    }
    d->eobrun = 0;
    for (u = 0; u < units; u++) {
        int my = (int)(u / across), mx = (int)(u % across), by, bx;

        if (d->restart > 0 && u > 0 && u % d->restart == 0) {
            resync(d, ns, which);
        }
        if (ns == 1) {
            if (block(d, one, my, mx) != 0) {
                return -1;
            }
            continue;
        }
        for (c = 0; c < ns; c++) {
            jcomp_t *k = &d->comp[which[c]];

            for (by = 0; by < k->v; by++) {
                for (bx = 0; bx < k->h; bx++) {
                    if (block(d, k, my * k->v + by, mx * k->h + bx) != 0) {
                        return -1;
                    }
                }
            }
        }
    }
    return 0;
}

/** @brief Where the marker after a scan's data begins, or the data's
 *         length when none follows. */
static long
after_scan(const jdec_t *d)
{
    long p = d->bits.pos > 0 ? d->bits.pos - 1 : 0;

    while (p + 1 < d->n && !(d->src[p] == 0xff && d->src[p + 1] != 0x00 &&
                             d->src[p + 1] != 0xff &&
                             (d->src[p + 1] < 0xd0 || d->src[p + 1] > 0xd7))) {
        p++;
    }
    return p + 1 < d->n ? p : d->n;
}

/** @brief Every kept block dequantised and drawn into its plane. */
static void
draw_kept(jdec_t *d)
{
    int c, row, col, k;

    for (c = 0; c < d->ncomp; c++) {
        jcomp_t *m = &d->comp[c];

        for (row = 0; row < m->bh; row++) {
            for (col = 0; col < m->bw; col++) {
                const short *co = m->coef + ((long)row * m->bw + col) * 64;
                int blk[64];

                for (k = 0; k < 64; k++) {
                    blk[zigzag[k]] = co[zigzag[k]] * d->qt[m->tq][k];
                }
                idct8(blk, m->pix + (long)row * 8 * m->bw * 8 +
                           (long)col * 8, m->bw * 8);
            }
        }
    }
}

/** @brief Component @p c's sample under image pixel (@p x, @p y). */
static int
sample(const jdec_t *d, int c, int x, int y)
{
    const jcomp_t *k = &d->comp[c];

    return k->pix[(long)(y * k->v / d->vmax) * k->bw * 8 +
                  (x * k->h / d->hmax)];
}

/** @brief Convert component planes to ARGB using Adobe's declared transform.
 *  @return Allocated pixels, or NULL. */
static uint32_t *
to_rgb(const jdec_t *d)
{
    uint32_t *px = malloc((size_t)d->w * (size_t)d->h * sizeof *px);
    int y, x;

    for (y = 0; px != NULL && y < d->h; y++) {
        for (x = 0; x < d->w; x++) {
            int Y = sample(d, 0, x, y), cb = 128, cr = 128, r, g, bl;

            if (d->ncomp >= 3) {
                cb = sample(d, 1, x, y);
                cr = sample(d, 2, x, y);
            }
            r = Y + ((91881 * (cr - 128)) >> 16);
            g = Y - ((22554 * (cb - 128) + 46802 * (cr - 128)) >> 16);
            bl = Y + ((116130 * (cb - 128)) >> 16);
            if (d->ncomp == 3 && d->transform == 0) {
                r = Y;
                g = cb;
                bl = cr;
            }
            if (d->ncomp == 4) {
                int k = sample(d, 3, x, y);
                int cy = d->transform == 2 ? 255 - clamp8(r) : Y;
                int ma = d->transform == 2 ? 255 - clamp8(g) : cb;
                int ye = d->transform == 2 ? 255 - clamp8(bl) : cr;

                if (d->transform == 0 || d->transform == 2) {
                    r = cy * k / 255;
                    g = ma * k / 255;
                    bl = ye * k / 255;
                } else {
                    r = (255 - cy) * (255 - k) / 255;
                    g = (255 - ma) * (255 - k) / 255;
                    bl = (255 - ye) * (255 - k) / 255;
                }
            }
            px[(long)y * d->w + x] = 0xff000000u |
                ((uint32_t)clamp8(r) << 16) | ((uint32_t)clamp8(g) << 8) |
                (uint32_t)clamp8(bl);
        }
    }
    return px;
}

/** @brief The scan at marker @p i: planes made at the first, the scan
 *         read.  @return where to read on, or -1 when the decode ends
 *         (@p *rc then says how). */
static long
sos(jdec_t *d, long i, int len, int *rc)
{
    int which[MAXCOMP], ns = d->w > 0 ? read_sos(d, i, len, which) : -1;

    if (ns < 0) {
        return -1;
    }
    if (d->comp[0].pix == NULL) {
        d->kept = d->progressive || ns < d->ncomp;
        if (planes(d) != 0) {
            return -1;
        }
    }
    memset(&d->bits, 0, sizeof d->bits);
    d->bits.p = d->src;
    d->bits.n = d->n;
    d->bits.pos = i + len;
    if (scan(d, ns, which) != 0) {
        if (!d->kept) {
            return -1;              /* a partial frame is no picture */
        }
    } else {
        d->scans++;
    }
    if (!d->kept) {
        /* One scan and nothing after it but the end. */
        long p = d->bits.pos;

        while (p + 1 < d->n && d->src[p] == 0xff && d->src[p + 1] == 0xff) {
            p++;
        }
        if (p + 1 >= d->n || d->src[p] != 0xff || d->src[p + 1] != 0xd9) {
            return -1;
        }
        *rc = 0;
        return -1;
    }
    return d->bits.bad ? d->n : after_scan(d);
}

int
tiku_jpeg_decode(const unsigned char *src, long n, int *ow, int *oh,
                     uint32_t **out)
{
    jdec_t *d;
    long i = 2;
    int rc = -1, c;

    if (src == NULL || n < 4 || (unsigned long)n > TIKU_IMAGE_BYTES_MAX ||
        src[0] != 0xff || src[1] != 0xd8) {
        return -1;                  /* not a JPEG at all */
    }
    d = calloc(1, sizeof *d);
    if (d == NULL) {
        return -2;
    }
    d->src = src;
    d->n = n;
    d->hmax = d->vmax = 1;
    d->transform = -1;
    while (i + 4 <= n && rc < 0) {
        int marker, len, r = 0;

        if (src[i] != 0xff) {
            i++;
            continue;
        }
        marker = src[i + 1];
        i += 2;
        if (marker == 0xd8 || marker == 0x01 || marker == 0xff ||
            (marker >= 0xd0 && marker <= 0xd7)) {
            i -= marker == 0xff;    /* fill before a marker */
            continue;               /* standalone markers carry no length */
        }
        if (marker == 0xd9) {
            break;                  /* the end of the image */
        }
        len = i + 2 <= n ? (src[i] << 8) | src[i + 1] : 0;
        if (len < 2 || i + len > n) {
            break;
        }
        if (marker == 0xdb) {
            r = read_dqt(d, i, len);
        } else if (marker == 0xc4) {
            r = read_dht(d, i, len);
        } else if (marker == 0xdd) {
            d->restart = len == 4 ? (src[i + 2] << 8) | src[i + 3] : 0;
            r = len == 4 ? 0 : -1;
        } else if (marker == 0xc0 || marker == 0xc1 || marker == 0xc2) {
            d->progressive = marker == 0xc2;
            r = read_sof(d, i, len);
        } else if (marker == 0xee && len >= 14 &&
                   memcmp(src + i + 2, "Adobe", 5) == 0) {
            d->transform = src[i + 13];
        } else if (marker == 0xc3 || (marker >= 0xc5 && marker <= 0xcf &&
                                      marker != 0xc8 && marker != 0xcc)) {
            r = 1;                  /* lossless, hierarchical, arithmetic */
        } else if (marker == 0xda) {
            long next = sos(d, i, len, &rc);

            if (next < 0) {
                break;
            }
            i = next;
            continue;
        }
        if (r != 0) {
            rc = r > 0 ? 1 : -1;
            goto done;
        }
        i += len;
    }
    if (d->kept && d->scans > 0 && rc < 0 && !d->short_memory) {
        draw_kept(d);               /* the scans that came */
        rc = 0;
    }
    if (rc == 0) {
        *out = to_rgb(d);
        *ow = d->w;
        *oh = d->h;
        rc = *out != NULL ? 0 : -2;
    }
done:
    if (d->short_memory)
        rc = -2;
    for (c = 0; c < MAXCOMP; c++) {
        free(d->comp[c].pix);
        free(d->comp[c].coef);
    }
    free(d);
    return rc;
}
