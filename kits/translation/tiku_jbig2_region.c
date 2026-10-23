/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_jbig2_region.c - a region of bits decoded: each pixel from the
 * pixels decoded before it (T.88 6.2), a whole region from MMR's runs,
 * or a pixel from its own neighbours and a reference bitmap's (6.3).
 *
 * Every JBIG2 bitmap is one of these in the end: pages coded whole, the
 * glyphs of a symbol dictionary, the planes of a halftone's grey.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_jbig2_private.h"
#include "tiku_fax.h"

#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------ generic  */

/**
 * The four templates (6.2.5.3): for the row being decoded and the two
 * above it, how many pixels the context takes, the rightmost one's
 * offset from x, and the bit the rightmost sits at, the others to its
 * left in the bits above; then the AT pixels' bits, and the context
 * TPGDON codes a row's typicality in (6.2.5.7).
 */
static const struct {
    int      n[3], right[3], shift[3];
    int      nat, atbit[4];
    unsigned sltp;
} gen_tmpl[4] = {
    {{4, 5, 3}, {-1, 2, 1}, {0, 5, 12}, 4, {4, 10, 11, 15}, 0x9B25u},
    {{3, 5, 4}, {-1, 2, 2}, {0, 4, 9}, 1, {3, 0, 0, 0}, 0x0795u},
    {{2, 4, 3}, {-1, 1, 1}, {0, 3, 7}, 1, {2, 0, 0, 0}, 0x00E5u},
    {{4, 5, 0}, {-1, 1, 0}, {0, 5, 0}, 1, {4, 0, 0, 0}, 0x0195u},
};

/**
 * @brief Row @p y of @p out, each pixel in the context of those before
 *        it.  The three rows' pixels ride in shift registers, a pixel in
 *        on the right as x moves on; the AT pixels are read each time.
 */
static void
gen_row(tiku_mq_t *d, tiku_mq_cx_t *gb, const jb_gen_t *g, jb_bm_t *out, long y)
{
    int t = g->tmpl & 3, k, i, bit;
    unsigned r[3] = {0, 0, 0}, m[3], ctx;
    unsigned char *row = out->px + (size_t)y * (size_t)out->w;
    long x;

    for (k = 0; k < 3; k++) {
        m[k] = (1u << gen_tmpl[t].n[k]) - 1;
        for (i = 0; k > 0 && i < gen_tmpl[t].n[k]; i++) {
            r[k] |= (unsigned)jb_px(out, gen_tmpl[t].right[k] - i, y - k)
                    << i;
        }
    }
    for (x = 0; x < out->w; x++) {
        ctx = r[0] << gen_tmpl[t].shift[0] | r[1] << gen_tmpl[t].shift[1] |
              r[2] << gen_tmpl[t].shift[2];
        for (i = 0; i < gen_tmpl[t].nat; i++) {
            ctx |= (unsigned)jb_px(out, x + g->at[2 * i],
                                   y + g->at[2 * i + 1])
                   << gen_tmpl[t].atbit[i];
        }
        bit = g->skip != NULL && jb_px(g->skip, x, y) ? 0
              : tiku_mq_bit(d, &gb[ctx]);
        row[x] = (unsigned char)bit;
        r[0] = ((r[0] << 1) | (unsigned)bit) & m[0];
        r[1] = ((r[1] << 1) |
                (unsigned)jb_px(out, x + 1 + gen_tmpl[t].right[1], y - 1)) &
               m[1];
        r[2] = ((r[2] << 1) |
                (unsigned)jb_px(out, x + 1 + gen_tmpl[t].right[2], y - 2)) &
               m[2];
    }
}

int
jb_generic(tiku_mq_t *d, tiku_mq_cx_t *gb, const jb_gen_t *g, jb_bm_t *out)
{
    int ltp = 0;
    long y;

    for (y = 0; y < out->h; y++) {
        unsigned char *row = out->px + (size_t)y * (size_t)out->w;

        if (g->tpgdon) {
            ltp ^= tiku_mq_bit(d, &gb[gen_tmpl[g->tmpl & 3].sltp]);
            if (ltp) {              /* typical: the row above again */
                if (y > 0) {
                    memcpy(row, row - out->w, (size_t)out->w);
                } else {
                    memset(row, 0, (size_t)out->w);
                }
                continue;
            }
        }
        gen_row(d, gb, g, out, y);
        if (tiku_mq_spent(d)) {
            return -1;
        }
    }
    return 0;
}

long
jb_generic_mmr(const unsigned char *p, long n, jb_bm_t *out)
{
    tiku_fax_t f;
    unsigned char *bits = NULL;
    long rb = (out->w + 7) / 8, used = 0, x, y;

    if (out->w == 0 || out->h == 0) {
        return 0;
    }
    f.k = -1;                       /* T.6: Group 4 */
    f.columns = out->w;
    f.rows = out->h;
    f.byte_align = 0;
    f.black_is_1 = 1;
    if (tiku_fax_decode_used(p, n, &f, &bits, rb * out->h + 8, &used) <
        rb * out->h) {
        free(bits);
        return -1;
    }
    for (y = 0; y < out->h; y++) {
        const unsigned char *s = bits + (size_t)y * (size_t)rb;
        unsigned char *row = out->px + (size_t)y * (size_t)out->w;

        for (x = 0; x < out->w; x++) {
            row[x] = (unsigned char)((s[x >> 3] >> (7 - (x & 7))) & 1u);
        }
    }
    free(bits);
    return used;
}

/* --------------------------------------------------------- refinement  */

/** @brief The context of pixel @p x, @p y (6.3.5.3): its neighbours
 *         decoded so far and the reference's around where it falls. */
static unsigned
ref_ctx(const jb_ref_t *r, const jb_bm_t *out, long x, long y)
{
    long rx = x - r->dx, ry = y - r->dy;
    const jb_bm_t *f = r->ref;
    unsigned c = (unsigned)jb_px(out, x - 1, y) |
                 (unsigned)jb_px(out, x + 1, y - 1) << 1 |
                 (unsigned)jb_px(out, x, y - 1) << 2;

    if (r->tmpl == 0) {
        return c | (unsigned)jb_px(out, x + r->at[0], y + r->at[1]) << 3 |
               (unsigned)jb_px(f, rx + 1, ry + 1) << 4 |
               (unsigned)jb_px(f, rx, ry + 1) << 5 |
               (unsigned)jb_px(f, rx - 1, ry + 1) << 6 |
               (unsigned)jb_px(f, rx + 1, ry) << 7 |
               (unsigned)jb_px(f, rx, ry) << 8 |
               (unsigned)jb_px(f, rx - 1, ry) << 9 |
               (unsigned)jb_px(f, rx + 1, ry - 1) << 10 |
               (unsigned)jb_px(f, rx, ry - 1) << 11 |
               (unsigned)jb_px(f, rx + r->at[2], ry + r->at[3]) << 12;
    }
    return c | (unsigned)jb_px(out, x - 1, y - 1) << 3 |
           (unsigned)jb_px(f, rx + 1, ry + 1) << 4 |
           (unsigned)jb_px(f, rx, ry + 1) << 5 |
           (unsigned)jb_px(f, rx + 1, ry) << 6 |
           (unsigned)jb_px(f, rx, ry) << 7 |
           (unsigned)jb_px(f, rx - 1, ry) << 8 |
           (unsigned)jb_px(f, rx, ry - 1) << 9;
}

/** @brief Whether the reference is one colour all around where @p x,
 *         @p y falls (TPGRPIX), that colour into @p v. */
static int
ref_plain(const jb_ref_t *r, long x, long y, int *v)
{
    long rx = x - r->dx, ry = y - r->dy, i, j;

    *v = jb_px(r->ref, rx, ry);
    for (j = -1; j <= 1; j++) {
        for (i = -1; i <= 1; i++) {
            if (jb_px(r->ref, rx + i, ry + j) != *v) {
                return 0;
            }
        }
    }
    return 1;
}

int
jb_refine(tiku_mq_t *d, tiku_mq_cx_t *gr, const jb_ref_t *r, jb_bm_t *out)
{
    int ltp = 0, v;
    long x, y;

    for (y = 0; y < out->h; y++) {
        unsigned char *row = out->px + (size_t)y * (size_t)out->w;

        if (r->tpgron) {
            ltp ^= tiku_mq_bit(d, &gr[r->tmpl ? 0x080u : 0x100u]);
        }
        for (x = 0; x < out->w; x++) {
            if (ltp && ref_plain(r, x, y, &v)) {
                row[x] = (unsigned char)v;  /* typical: as the reference */
                continue;
            }
            row[x] = (unsigned char)tiku_mq_bit(d, &gr[ref_ctx(r, out, x, y)]);
        }
        if (tiku_mq_spent(d)) {
            return -1;
        }
    }
    return 0;
}
