/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_jbig2_code.c - how JBIG2's bits are coded: the integers the MQ
 * arithmetic decoder carries (T.88 Annex A), the fifteen standard
 * Huffman tables and the ones a stream brings (Annex B), and the bitmaps
 * both fill.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_jbig2_private.h"

#include <stdlib.h>
#include <string.h>

/** @brief A bit of an integer, its context the bits so far (A.2). */
static int
ia_bit(tiku_mq_t *d, jb_iax_t *ia, unsigned *prev)
{
    int b = tiku_mq_bit(d, &ia->cx[*prev]);

    *prev = *prev < 256 ? (*prev << 1) | (unsigned)b
                        : ((((*prev << 1) | (unsigned)b) & 511u) | 256u);
    return b;
}

/** @brief @p k bits of an integer's magnitude. */
static uint32_t
ia_bits(tiku_mq_t *d, jb_iax_t *ia, unsigned *prev, int k)
{
    uint32_t v = 0;

    while (k-- > 0) {
        v = v << 1 | (uint32_t)ia_bit(d, ia, prev);
    }
    return v;
}

int
jb_int(tiku_mq_t *d, jb_iax_t *ia, int32_t *v)
{
    static const int width[6] = {2, 4, 6, 8, 12, 32};
    static const uint32_t base[6] = {0, 4, 20, 84, 340, 4436};
    unsigned prev = 1;
    int s = ia_bit(d, ia, &prev), k = 0;
    uint64_t x;

    while (k < 5 && ia_bit(d, ia, &prev)) {
        k++;                        /* each 1 a longer magnitude */
    }
    x = (uint64_t)ia_bits(d, ia, &prev, width[k]) + base[k];
    if (s && x == 0) {
        return 1;
    }
    if (x > 0x7FFFFFFFu) {
        x = 0x7FFFFFFFu;            /* past what any field holds */
    }
    *v = s ? -(int32_t)x : (int32_t)x;
    return 0;
}

long
jb_iaid(tiku_mq_t *d, tiku_mq_cx_t *cx, int len)
{
    unsigned long prev = 1;
    int i;

    for (i = 0; i < len; i++) {
        prev = prev << 1 | (unsigned long)tiku_mq_bit(d, &cx[prev]);
    }
    return (long)(prev - (1UL << len));
}

/* ------------------------------------------------------------ Huffman  */

int
jb_bits(jb_bits_t *b, int k, uint32_t *v)
{
    uint32_t x = 0;

    if (k < 0 || k > 32 || b->bit + k > b->n * 8) {
        return -1;
    }
    while (k-- > 0) {
        x = x << 1 | ((b->p[b->bit >> 3] >> (7 - (b->bit & 7))) & 1u);
        b->bit++;
    }
    *v = x;
    return 0;
}

void
jb_align(jb_bits_t *b)
{
    b->bit = (b->bit + 7) & ~7L;
}

/* The standard tables, B.1 to B.15, one after another: prefix length,
 * range length and low end of each line, in the order the standard
 * lists them, which orders codes of one length. */
#define L(p, r, lo) {p, r, JB_LINE, lo, 0}
#define LO(p, lo)   {p, 32, JB_LOWER, lo, 0}
#define UP(p, lo)   {p, 32, JB_UPPER, lo, 0}
#define OOB(p)      {p, 0, JB_OOB, 0, 0}
static const jb_line_t std_lines[] = {
    /* B.1 */ L(1, 4, 0), L(2, 8, 16), L(3, 16, 272), UP(3, 65808),
    /* B.2 */ L(1, 0, 0), L(2, 0, 1), L(3, 0, 2), L(4, 3, 3), L(5, 6, 11),
    UP(6, 75), OOB(6),
    /* B.3 */ L(8, 8, -256), L(1, 0, 0), L(2, 0, 1), L(3, 0, 2), L(4, 3, 3),
    L(5, 6, 11), LO(8, -257), UP(7, 75), OOB(6),
    /* B.4 */ L(1, 0, 1), L(2, 0, 2), L(3, 0, 3), L(4, 3, 4), L(5, 6, 12),
    UP(5, 76),
    /* B.5 */ L(7, 8, -255), L(1, 0, 1), L(2, 0, 2), L(3, 0, 3), L(4, 3, 4),
    L(5, 6, 12), LO(7, -256), UP(6, 76),
    /* B.6 */ L(5, 10, -2048), L(4, 9, -1024), L(4, 8, -512), L(4, 7, -256),
    L(5, 6, -128), L(5, 5, -64), L(4, 5, -32), L(2, 7, 0), L(3, 7, 128),
    L(3, 8, 256), L(4, 9, 512), L(4, 10, 1024), LO(6, -2049), UP(6, 2048),
    /* B.7 */ L(4, 9, -1024), L(3, 8, -512), L(4, 7, -256), L(5, 6, -128),
    L(5, 5, -64), L(4, 5, -32), L(4, 5, 0), L(5, 5, 32), L(5, 6, 64),
    L(4, 7, 128), L(3, 8, 256), L(3, 9, 512), L(3, 10, 1024), LO(5, -1025),
    UP(5, 2048),
    /* B.8 */ L(8, 3, -15), L(9, 1, -7), L(8, 1, -5), L(9, 0, -3),
    L(7, 0, -2), L(4, 0, -1), L(2, 1, 0), L(5, 0, 2), L(6, 0, 3),
    L(3, 4, 4), L(6, 1, 20), L(4, 4, 22), L(4, 5, 38), L(5, 6, 70),
    L(5, 7, 134), L(6, 7, 262), L(7, 8, 390), L(6, 10, 646), LO(9, -16),
    UP(9, 1670), OOB(2),
    /* B.9 */ L(8, 4, -31), L(9, 2, -15), L(8, 2, -11), L(9, 1, -7),
    L(7, 1, -5), L(4, 1, -3), L(3, 1, -1), L(3, 1, 1), L(5, 1, 3),
    L(6, 1, 5), L(3, 5, 7), L(6, 2, 39), L(4, 5, 43), L(4, 6, 75),
    L(5, 7, 139), L(5, 8, 267), L(6, 8, 523), L(7, 9, 779), L(6, 11, 1291),
    LO(9, -32), UP(9, 3339), OOB(2),
    /* B.10 */ L(7, 4, -21), L(8, 0, -5), L(7, 0, -4), L(5, 0, -3),
    L(2, 2, -2), L(5, 0, 2), L(6, 0, 3), L(7, 0, 4), L(8, 0, 5),
    L(2, 6, 6), L(5, 5, 70), L(6, 5, 102), L(6, 6, 134), L(6, 7, 198),
    L(6, 8, 326), L(6, 9, 582), L(6, 10, 1094), L(7, 11, 2118), LO(8, -22),
    UP(8, 4166), OOB(2),
    /* B.11 */ L(1, 0, 1), L(2, 1, 2), L(4, 0, 4), L(4, 1, 5), L(5, 1, 7),
    L(5, 2, 9), L(6, 2, 13), L(7, 2, 17), L(7, 3, 21), L(7, 4, 29),
    L(7, 5, 45), L(7, 6, 77), UP(7, 141),
    /* B.12 */ L(1, 0, 1), L(2, 0, 2), L(3, 1, 3), L(5, 0, 5), L(5, 1, 6),
    L(6, 1, 8), L(7, 0, 10), L(7, 1, 11), L(7, 2, 13), L(7, 3, 17),
    L(7, 4, 25), L(8, 5, 41), UP(8, 73),
    /* B.13 */ L(1, 0, 1), L(3, 0, 2), L(4, 0, 3), L(5, 0, 4), L(4, 1, 5),
    L(3, 3, 7), L(6, 1, 15), L(6, 2, 17), L(6, 3, 21), L(6, 4, 29),
    L(6, 5, 45), L(7, 6, 77), UP(7, 141),
    /* B.14 */ L(3, 0, -2), L(3, 0, -1), L(1, 0, 0), L(3, 0, 1), L(3, 0, 2),
    /* B.15 */ L(7, 4, -24), L(6, 2, -8), L(5, 1, -4), L(4, 0, -2),
    L(3, 0, -1), L(1, 0, 0), L(3, 0, 1), L(4, 0, 2), L(5, 1, 3),
    L(6, 2, 5), L(7, 4, 9), LO(7, -25), UP(7, 25),
};
#undef L
#undef LO
#undef UP
#undef OOB

/* Where each table starts in std_lines, and its lines. */
static const unsigned char std_at[15] = {0, 4, 11, 20, 26, 34, 48, 63, 84,
                                         106, 127, 140, 153, 166, 171};
static const unsigned char std_n[15] = {4, 7, 9, 6, 8, 14, 15, 21, 22, 21,
                                        13, 13, 13, 5, 13};
typedef char std_lines_counted[sizeof std_lines / sizeof std_lines[0] == 184
                               ? 1 : -1];

void
jb_table_codes(jb_table_t *t)
{
    int count[33] = {0}, i, len, most = 0;
    uint32_t first = 0, code;

    for (i = 0; i < t->n; i++) {
        if (t->line[i].prefl > 0 && t->line[i].prefl <= 32) {
            count[t->line[i].prefl]++;
            most = t->line[i].prefl > most ? t->line[i].prefl : most;
        }
    }
    for (len = 1; len <= most; len++) {
        first = (first + (uint32_t)count[len - 1]) << 1;
        code = first;
        for (i = 0; i < t->n; i++) {
            if (t->line[i].prefl == len) {
                t->line[i].code = code++;
            }
        }
    }
}

void
jb_table_std(int k, jb_table_t *t, jb_line_t *lines)
{
    memcpy(lines, &std_lines[std_at[k - 1]],
           std_n[k - 1] * sizeof *lines);
    t->n = std_n[k - 1];
    t->line = lines;
    jb_table_codes(t);
}

/** @brief A line added to @p t: prefix length @p p, range length @p r
 *         from @p lo.  @return 0, or -1 when memory is short. */
static int
table_line(jb_table_t *t, int p, int r, int kind, int32_t lo)
{
    jb_line_t *l;

    if (t->n % 16 == 0) {
        l = realloc(t->line, (size_t)(t->n + 16) * sizeof *l);
        if (l == NULL) {
            return -1;
        }
        t->line = l;
    }
    l = &t->line[t->n++];
    l->prefl = p;
    l->rangel = r;
    l->kind = kind;
    l->low = lo;
    l->code = 0;
    return 0;
}

int
jb_table_read(const unsigned char *p, long n, jb_table_t *t)
{
    jb_bits_t b = {p, n, 72};
    uint32_t ps, rs, pre, rng;
    int64_t low, high, cur;

    t->n = 0;
    t->line = NULL;
    if (n < 9) {
        return -1;
    }
    ps = ((p[0] >> 1) & 7u) + 1;
    rs = ((p[0] >> 4) & 7u) + 1;
    low = (int32_t)((uint32_t)p[1] << 24 | (uint32_t)p[2] << 16 |
                    (uint32_t)p[3] << 8 | p[4]);
    high = (int32_t)((uint32_t)p[5] << 24 | (uint32_t)p[6] << 16 |
                     (uint32_t)p[7] << 8 | p[8]);
    for (cur = low; cur < high; cur += (int64_t)1 << rng) {
        if (jb_bits(&b, (int)ps, &pre) || jb_bits(&b, (int)rs, &rng) ||
            rng > 31 || t->n >= 4096 ||
            table_line(t, (int)pre, (int)rng, JB_LINE, (int32_t)cur)) {
            return -1;
        }
    }
    if (jb_bits(&b, (int)ps, &pre) ||
        table_line(t, (int)pre, 32, JB_LOWER, (int32_t)(low - 1)) ||
        jb_bits(&b, (int)ps, &pre) ||
        table_line(t, (int)pre, 32, JB_UPPER, (int32_t)high)) {
        return -1;
    }
    if ((p[0] & 1) &&
        (jb_bits(&b, (int)ps, &pre) || table_line(t, (int)pre, 0, JB_OOB, 0))) {
        return -1;
    }
    jb_table_codes(t);
    return 0;
}

int
jb_huff(jb_bits_t *b, const jb_table_t *t, int32_t *v)
{
    uint32_t code = 0, more, bit;
    int len, i;
    int64_t x;

    for (len = 1; len <= 32; len++) {
        if (jb_bits(b, 1, &bit)) {
            return -1;
        }
        code = code << 1 | bit;
        for (i = 0; i < t->n; i++) {
            const jb_line_t *l = &t->line[i];

            if (l->prefl != len || l->code != code) {
                continue;
            }
            if (l->kind == JB_OOB) {
                return 1;
            }
            if (jb_bits(b, l->rangel, &more)) {
                return -1;
            }
            x = l->kind == JB_LOWER ? (int64_t)l->low - more
                                    : (int64_t)l->low + more;
            *v = x > INT32_MAX ? INT32_MAX : x < INT32_MIN ? INT32_MIN
                                                           : (int32_t)x;
            return 0;
        }
    }
    return -1;
}

/* ------------------------------------------------------------ bitmaps  */

jb_bm_t *
jb_bm_new(int w, int h, int fill)
{
    jb_bm_t *b;

    if (w < 0 || h < 0 || (long long)w * h > (1LL << 28)) {
        return NULL;
    }
    b = malloc(sizeof *b);
    if (b == NULL) {
        return NULL;
    }
    b->w = w;
    b->h = h;
    b->px = malloc((size_t)w * (size_t)h + 1);
    if (b->px == NULL) {
        free(b);
        return NULL;
    }
    memset(b->px, fill ? 1 : 0, (size_t)w * (size_t)h);
    return b;
}

void
jb_bm_free(jb_bm_t *b)
{
    if (b != NULL) {
        free(b->px);
        free(b);
    }
}

int
jb_px(const jb_bm_t *b, long x, long y)
{
    if (x < 0 || y < 0 || x >= b->w || y >= b->h) {
        return 0;
    }
    return b->px[(size_t)y * (size_t)b->w + (size_t)x];
}

void
jb_compose(jb_bm_t *dst, const jb_bm_t *src, long x, long y, int op)
{
    long x0 = x < 0 ? -x : 0, y0 = y < 0 ? -y : 0;
    long x1 = src->w, y1 = src->h, i, j;

    if (x + x1 > dst->w) x1 = dst->w - x;
    if (y + y1 > dst->h) y1 = dst->h - y;
    for (j = y0; j < y1; j++) {
        unsigned char *d = dst->px + (size_t)(y + j) * (size_t)dst->w;
        const unsigned char *s = src->px + (size_t)j * (size_t)src->w;

        for (i = x0; i < x1; i++) {
            unsigned char *o = &d[x + i];

            switch (op) {
            case 0: *o |= s[i]; break;
            case 1: *o &= s[i]; break;
            case 2: *o ^= s[i]; break;
            case 3: *o = (unsigned char)(1 ^ *o ^ s[i]); break;
            default: *o = s[i]; break;
            }
        }
    }
}
