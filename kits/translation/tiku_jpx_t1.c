/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_jpx_t1.c - JPEG 2000's tier 1 (15444-1 Annex D): a code-block's
 * coefficients from its coding passes, a bitplane at a time.
 *
 * Each plane is three passes over stripes four rows high: significance
 * for coefficients next to significant ones, refinement for those
 * already so, and cleanup for the rest, runs of four at a time.  Every
 * decision is in a context its neighbours make; past the fourth plane
 * the first two passes may be raw bits (bypass).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_jpx_private.h"
#include "tiku_mq.h"

#include <stdlib.h>
#include <string.h>

/* Each coefficient's flags: its own state, and its neighbours'
 * significance and signs, set as each neighbour becomes significant; a
 * coefficient's contexts are taken from its own flags alone. */
#define SIG     0x0001u             /* significant                       */
#define NEG     0x0002u             /* its sign is minus                 */
#define VISIT   0x0004u             /* coded in this plane's first pass  */
#define REF     0x0008u             /* refined at least once             */
#define SIG_N   0x0010u             /* the neighbour above significant   */
#define SIG_S   0x0020u             /* ... below                         */
#define SIG_W   0x0040u             /* ... to the left                   */
#define SIG_E   0x0080u             /* ... to the right                  */
#define SIG_NW  0x0100u             /* ... and at the four corners       */
#define SIG_NE  0x0200u
#define SIG_SW  0x0400u
#define SIG_SE  0x0800u
#define NEG_N   0x1000u             /* the neighbour above negative ...  */
#define NEG_S   0x2000u
#define NEG_W   0x4000u
#define NEG_E   0x8000u
#define NBRS    0x0FF0u             /* any neighbour significant         */

#define CX_RL  17                   /* the run-length context            */
#define RAW_PAST 256                /* raw bytes read past the data, most */
#define CX_UNI 18                   /* the uniform context               */

/* The zero coding contexts (Table D.1) by horizontal, vertical and
 * diagonal neighbours, h * 15 + v * 5 + d: LL and LH, HL, HH. */
static const unsigned char zc_tab[3][45] = {
    {0, 1, 2, 2, 2, 3, 3, 3, 3, 3, 4, 4, 4, 4, 4, 5, 6, 6, 6, 6, 7, 7, 7,
     7, 7, 7, 7, 7, 7, 7, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8},
    {0, 1, 2, 2, 2, 5, 6, 6, 6, 6, 8, 8, 8, 8, 8, 3, 3, 3, 3, 3, 7, 7, 7,
     7, 7, 8, 8, 8, 8, 8, 4, 4, 4, 4, 4, 7, 7, 7, 7, 7, 8, 8, 8, 8, 8},
    {0, 3, 6, 8, 8, 1, 4, 7, 8, 8, 2, 5, 7, 8, 8, 1, 4, 7, 8, 8, 2, 5, 7,
     8, 8, 2, 5, 7, 8, 8, 2, 5, 7, 8, 8, 2, 5, 7, 8, 8, 2, 5, 7, 8, 8},
};

/** @brief A code-block being decoded. */
typedef struct {
    tiku_mq_t            mq;        /* the arithmetic decoder            */
    tiku_mq_cx_t         cx[19];    /* its contexts                      */
    int                  raw;       /* this pass's bits come raw         */
    const unsigned char *rp;        /* the raw segment's bytes           */
    long                 rn, rpos;  /* their count; the next             */
    unsigned             rc;        /* the raw byte being read           */
    int                  rct;       /* its bits left                     */
    uint16_t            *flags;     /* (w + 2) * (h + 2), a border round */
    int32_t             *mag;       /* magnitudes, in halves             */
    int                  w, h, fs;  /* size; the flags' row stride       */
    const unsigned char *zc;        /* the band's zero contexts          */
    unsigned             foot;      /* the flags seen at a stripe's foot:
                                       none from below when causal       */
} t1_t;

/** @brief Contexts to their starting states (Table D.7). */
static void
t1_reset(t1_t *t)
{
    memset(t->cx, 0, sizeof t->cx);
    t->cx[0].i = 4;
    t->cx[CX_RL].i = 3;
    t->cx[CX_UNI].i = 46;
}

/** @brief A raw bit (D.6): a byte after 0xFF carries seven; ones past
 *         the data or at a marker. */
static int
raw_bit(t1_t *t)
{
    if (t->rct == 0) {
        unsigned b = t->rpos < t->rn ? t->rp[t->rpos] : 0xFFu;

        if (t->rc == 0xFFu && b > 0x8Fu) {
            t->rct = 8;             /* a marker: ones, not read past */
        } else {
            t->rct = t->rc == 0xFFu ? 7 : 8;
            t->rc = b;
            t->rpos++;
        }
    }
    t->rct--;
    return (int)((t->rc >> t->rct) & 1u);
}

/** @brief A decision in context @p cx, or a raw bit in a raw pass. */
static int
t1_bit(t1_t *t, int cx)
{
    return t->raw ? raw_bit(t) : tiku_mq_bit(&t->mq, &t->cx[cx]);
}

/** @brief The zero coding context of a coefficient with flags @p f. */
static int
zc_ctx(const t1_t *t, unsigned f)
{
    unsigned h = ((f >> 6) & 1u) + ((f >> 7) & 1u);
    unsigned v = ((f >> 4) & 1u) + ((f >> 5) & 1u);
    unsigned d = ((f >> 8) & 1u) + ((f >> 9) & 1u) + ((f >> 10) & 1u) +
                 ((f >> 11) & 1u);

    return t->zc[h * 15 + v * 5 + d];
}

/** @brief A neighbour's sign as a contribution: +1, -1, or 0 when it is
 *         not significant (@p sig and @p neg its flags in @p f). */
static int
contrib(unsigned f, unsigned sig, unsigned neg)
{
    return (f & sig) ? ((f & neg) ? -1 : 1) : 0;
}

/** @brief The sign of a coefficient with flags @p f (D.3.2, Table D.3):
 *         decoded in the context its four neighbours' signs make, and
 *         turned by that context's XOR bit. */
static int
sign_bit(t1_t *t, unsigned f)
{
    int h, v, cx, flip;

    if (t->raw) {
        return raw_bit(t);
    }
    h = contrib(f, SIG_W, NEG_W) + contrib(f, SIG_E, NEG_E);
    v = contrib(f, SIG_N, NEG_N) + contrib(f, SIG_S, NEG_S);
    h = h > 1 ? 1 : h < -1 ? -1 : h;
    v = v > 1 ? 1 : v < -1 ? -1 : v;
    flip = h < 0 || (h == 0 && v < 0);
    if (flip) {
        h = -h;
        v = -v;
    }
    cx = h == 1 ? 12 + v : 9 + (v != 0);
    return tiku_mq_bit(&t->mq, &t->cx[cx]) ^ flip;
}

/**
 * @brief The coefficient with flags at @p fp and magnitude at @p m made
 *        significant at plane @p p: its sign decoded from @p f, its
 *        magnitude half a step into the plane, its eight neighbours told.
 */
static void
become(t1_t *t, uint16_t *fp, int32_t *m, int p, unsigned f)
{
    int s = t->fs, neg = sign_bit(t, f);

    *m = 3 << p;
    fp[0] |= (uint16_t)(SIG | (neg ? NEG : 0));
    fp[-s - 1] |= SIG_SE;
    fp[-s] |= (uint16_t)(SIG_S | (neg ? NEG_S : 0));
    fp[-s + 1] |= SIG_SW;
    fp[-1] |= (uint16_t)(SIG_E | (neg ? NEG_E : 0));
    fp[1] |= (uint16_t)(SIG_W | (neg ? NEG_W : 0));
    fp[s - 1] |= SIG_NE;
    fp[s] |= (uint16_t)(SIG_N | (neg ? NEG_N : 0));
    fp[s + 1] |= SIG_NW;
}

/** @brief The flags of row @p k of a stripe at @p fp as its contexts see
 *         them: at the foot, without the row below when causal. */
static unsigned
seen(const t1_t *t, const uint16_t *fp, int k)
{
    return k == 3 ? *fp & t->foot : *fp;
}

/** @brief The significance propagation pass (D.3.1) at plane @p p:
 *         the insignificant coefficients a significant one touches. */
static void
pass_sig(t1_t *t, int p)
{
    int y0, x, k;

    for (y0 = 0; y0 < t->h; y0 += 4) {
        for (x = 0; x < t->w; x++) {
            uint16_t *fp = &t->flags[(y0 + 1) * t->fs + x + 1];
            int32_t *m = &t->mag[y0 * t->w + x];

            for (k = 0; k < 4 && y0 + k < t->h; k++, fp += t->fs, m += t->w) {
                unsigned f = seen(t, fp, k);

                if ((f & SIG) || !(f & NBRS)) {
                    continue;
                }
                if (t1_bit(t, zc_ctx(t, f))) {
                    become(t, fp, m, p, f);
                }
                *fp |= VISIT;
            }
        }
    }
}

/** @brief The magnitude refinement pass (D.3.3) at plane @p p: a bit
 *         more of each coefficient significant before this plane. */
static void
pass_ref(t1_t *t, int p)
{
    int y0, x, k, cx;

    for (y0 = 0; y0 < t->h; y0 += 4) {
        for (x = 0; x < t->w; x++) {
            uint16_t *fp = &t->flags[(y0 + 1) * t->fs + x + 1];
            int32_t *m = &t->mag[y0 * t->w + x];

            for (k = 0; k < 4 && y0 + k < t->h; k++, fp += t->fs, m += t->w) {
                unsigned f = seen(t, fp, k);

                if ((f & (SIG | VISIT)) != SIG) {
                    continue;
                }
                cx = (f & REF) ? 16 : (f & NBRS) ? 15 : 14;
                *m += t1_bit(t, cx) ? 1 << p : -(1 << p);
                *fp |= REF;
            }
        }
    }
}

/** @brief Whether the stripe column at @p fp may go as a run (D.3.4):
 *         none significant, none coded this plane, no neighbour so. */
static int
runnable(const t1_t *t, const uint16_t *fp)
{
    int s = t->fs;

    return !((fp[0] | fp[s] | fp[2 * s] | (fp[3 * s] & t->foot)) &
             (SIG | VISIT | NBRS));
}

/** @brief Row @p k of the stripe column at @p fp (magnitudes at @p m):
 *         coded in the cleanup pass at plane @p p unless done already. */
static void
clean_one(t1_t *t, uint16_t *fp, int32_t *m, int k, int p)
{
    unsigned f = seen(t, fp, k);

    if (!(f & (SIG | VISIT)) &&
        tiku_mq_bit(&t->mq, &t->cx[zc_ctx(t, f)])) {
        become(t, fp, m, p, f);
    }
}

/**
 * @brief The cleanup pass (D.3.4) at plane @p p: every coefficient the
 *        others left, whole columns of four as one run-length decision
 *        where nothing is significant near them.
 */
static void
pass_clean(t1_t *t, int p)
{
    int y0, x, k;

    for (y0 = 0; y0 < t->h; y0 += 4) {
        for (x = 0; x < t->w; x++) {
            uint16_t *fp = &t->flags[(y0 + 1) * t->fs + x + 1];
            int32_t *m = &t->mag[y0 * t->w + x];

            k = 0;
            if (y0 + 3 < t->h && runnable(t, fp)) {
                if (!tiku_mq_bit(&t->mq, &t->cx[CX_RL])) {
                    continue;       /* all four stay insignificant */
                }
                k = tiku_mq_bit(&t->mq, &t->cx[CX_UNI]) << 1;
                k += tiku_mq_bit(&t->mq, &t->cx[CX_UNI]);
                become(t, fp + k * t->fs, m + k * t->w, p,
                       seen(t, fp + k * t->fs, k));
                k++;
            }
            for (; k < 4 && y0 + k < t->h; k++) {
                clean_one(t, fp + k * t->fs, m + k * t->w, k, p);
            }
            for (k = 0; k < 4 && y0 + k < t->h; k++) {
                fp[k * t->fs] &= (uint16_t)~VISIT;
            }
        }
    }
}

/** @brief Segment @p s's decoder begun on its bytes at @p at: raw when
 *         its passes are bypassed, else the MQ decoder. */
static void
seg_begin(t1_t *t, const jpx_cblk_t *cb, int s, long at, int raw)
{
    long len = cb->seg[s].len;

    if (at + len > cb->len) {
        len = at < cb->len ? cb->len - at : 0;
    }
    t->raw = raw;
    if (raw) {
        t->rp = cb->data + at;
        t->rn = len;
        t->rpos = 0;
        t->rc = 0;
        t->rct = 0;
    } else {
        tiku_mq_init(&t->mq, cb->data + at, len);
    }
}

/** @brief Whether the pass just done ran far past its segment's bytes:
 *         passes a stream claims without the data to fill them. */
static int
spent(const t1_t *t)
{
    return t->raw ? t->rpos > t->rn + RAW_PAST : tiku_mq_spent(&t->mq);
}

/** @brief Every pass the code-block holds, segment by segment (D.4),
 *         until they run out of data. */
static void
passes(t1_t *t, const jpx_cblk_t *cb, int style)
{
    int n, s = 0, in_seg = 0, type, plane;
    long at = 0;

    for (n = 0; n < cb->npasses && s < cb->nseg && (n == 0 || !spent(t));
         n++) {
        type = n == 0 ? 2 : (n - 1) % 3;
        plane = n == 0 ? cb->numbps - 1 : cb->numbps - 2 - (n - 1) / 3;
        if (plane < 0) {
            break;
        }
        if (in_seg == 0) {
            seg_begin(t, cb, s, at, (style & JPX_BYPASS) && n >= 10 &&
                                    type != 2);
        }
        if ((style & JPX_RESET) && n > 0) {
            t1_reset(t);
        }
        if (type == 0) {
            pass_sig(t, plane);
        } else if (type == 1) {
            pass_ref(t, plane);
        } else {
            pass_clean(t, plane);
            if (style & JPX_SEGSYM) {
                int k;

                for (k = 0; k < 4; k++) {
                    (void)tiku_mq_bit(&t->mq, &t->cx[CX_UNI]);
                }
            }
        }
        if (++in_seg == cb->seg[s].npasses) {
            at += cb->seg[s].len;
            s++;
            in_seg = 0;
        }
    }
}

int
jpx_t1_decode(const jpx_cblk_t *cb, const jpx_band_t *b, int style,
              int reversible, int roi, float *out, long stride)
{
    t1_t t;
    int x, y;

    memset(&t, 0, sizeof t);
    t.w = cb->x1 - cb->x0;
    t.h = cb->y1 - cb->y0;
    if (t.w <= 0 || t.h <= 0 || cb->npasses == 0 || cb->numbps <= 0) {
        return 0;                   /* nothing coded: all zero */
    }
    if (cb->numbps > 30 || (long)t.w * t.h > 4096) {
        return -1;                  /* past what the halves can hold */
    }
    t.fs = t.w + 2;
    t.flags = calloc((size_t)t.fs * (size_t)(t.h + 2), sizeof *t.flags);
    t.mag = calloc((size_t)t.w * (size_t)t.h, sizeof *t.mag);
    if (t.flags == NULL || t.mag == NULL) {
        free(t.flags);
        free(t.mag);
        return -1;
    }
    t.zc = zc_tab[b->orient == JPX_HL ? 1 : b->orient == JPX_HH ? 2 : 0];
    t.foot = (style & JPX_VSC) ? 0xFFFFu & ~(SIG_S | SIG_SW | SIG_SE | NEG_S)
                               : 0xFFFFu;
    t1_reset(&t);
    passes(&t, cb, style);
    for (y = 0; y < t.h; y++) {
        for (x = 0; x < t.w; x++) {
            int32_t m = t.mag[y * t.w + x];
            float v;

            if (m == 0) {
                continue;
            }
            if (roi > 0 && roi < 31 && (m >> 1) >= (1L << roi)) {
                m >>= roi;          /* a region of interest's, shifted */
            }
            v = reversible ? (float)(m >> 1) : (float)m * 0.5f *
                                               (float)b->delta;
            out[y * stride + x] =
                (t.flags[(y + 1) * t.fs + x + 1] & NEG) ? -v : v;
        }
    }
    free(t.flags);
    free(t.mag);
    return 0;
}
