/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_mq.c - the MQ arithmetic decoder JBIG2 (T.88 Annex E) and JPEG
 * 2000 (15444-1 Annex C) share: one probability state machine of 47
 * states, a context each kind of decision is made in, and the code
 * register fed a byte at a time, ones past the data or a marker.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_mq.h"

#define MQ_PAST_MAX 256             /* fills of ones past the data      */

/** @brief Table E.1: each state's LPS probability, its next states and
 *         whether an LPS there swaps the more probable symbol. */
static const struct {
    uint16_t      qe;
    unsigned char nmps, nlps, swap;
} qe_tab[47] = {
    {0x5601, 1, 1, 1},   {0x3401, 2, 6, 0},   {0x1801, 3, 9, 0},
    {0x0AC1, 4, 12, 0},  {0x0521, 5, 29, 0},  {0x0221, 38, 33, 0},
    {0x5601, 7, 6, 1},   {0x5401, 8, 14, 0},  {0x4801, 9, 14, 0},
    {0x3801, 10, 14, 0}, {0x3001, 11, 17, 0}, {0x2401, 12, 18, 0},
    {0x1C01, 13, 20, 0}, {0x1601, 29, 21, 0}, {0x5601, 15, 14, 1},
    {0x5401, 16, 14, 0}, {0x5101, 17, 15, 0}, {0x4801, 18, 16, 0},
    {0x3801, 19, 17, 0}, {0x3401, 20, 18, 0}, {0x3001, 21, 19, 0},
    {0x2801, 22, 19, 0}, {0x2401, 23, 20, 0}, {0x2201, 24, 21, 0},
    {0x1C01, 25, 22, 0}, {0x1801, 26, 23, 0}, {0x1601, 27, 24, 0},
    {0x1401, 28, 25, 0}, {0x1201, 29, 26, 0}, {0x1101, 30, 27, 0},
    {0x0AC1, 31, 28, 0}, {0x09C1, 32, 29, 0}, {0x08A1, 33, 30, 0},
    {0x0521, 34, 31, 0}, {0x0441, 35, 32, 0}, {0x02A1, 36, 33, 0},
    {0x0221, 37, 34, 0}, {0x0141, 38, 35, 0}, {0x0111, 39, 36, 0},
    {0x0085, 40, 37, 0}, {0x0049, 41, 38, 0}, {0x0025, 42, 39, 0},
    {0x0015, 43, 40, 0}, {0x0009, 44, 41, 0}, {0x0005, 45, 42, 0},
    {0x0001, 45, 43, 0}, {0x5601, 46, 46, 0},
};

/** @brief Byte @p i of the data, ones past its end. */
static unsigned
mq_byte(const tiku_mq_t *d, long i)
{
    return i < d->n ? d->p[i] : 0xFFu;
}

/** @brief The next byte into C (E.3.4): a marker, 0xFF then more than
 *         0x8F, is not read past, and ones are fed instead. */
static void
mq_in(tiku_mq_t *d)
{
    if (mq_byte(d, d->pos) == 0xFFu) {
        unsigned b1 = mq_byte(d, d->pos + 1);

        if (b1 > 0x8Fu) {
            d->c += 0xFF00u;
            d->ct = 8;
            d->past++;
        } else {
            d->pos++;
            d->c += b1 << 9;
            d->ct = 7;
        }
    } else {
        d->pos++;
        d->c += mq_byte(d, d->pos) << 8;
        d->ct = 8;
        if (d->pos >= d->n) {
            d->past++;
        }
    }
}

void
tiku_mq_init(tiku_mq_t *d, const unsigned char *p, long n)
{
    d->p = p;
    d->n = n > 0 ? n : 0;
    d->pos = 0;
    d->past = 0;
    d->c = mq_byte(d, 0) << 16;
    mq_in(d);
    d->c <<= 7;
    d->ct -= 7;
    d->a = 0x8000u;
}

int
tiku_mq_spent(const tiku_mq_t *d)
{
    return d->past > MQ_PAST_MAX;
}

int
tiku_mq_bit(tiku_mq_t *d, tiku_mq_cx_t *cx)
{
    unsigned qe = qe_tab[cx->i].qe;
    int bit;

    d->a -= qe;
    if ((d->c >> 16) < qe) {        /* the LPS's part of the interval */
        if (d->a < qe) {            /* LPS_EXCHANGE: decided as the MPS */
            bit = cx->mps;
            cx->i = qe_tab[cx->i].nmps;
        } else {
            bit = !cx->mps;
            if (qe_tab[cx->i].swap) {
                cx->mps = (unsigned char)!cx->mps;
            }
            cx->i = qe_tab[cx->i].nlps;
        }
        d->a = qe;
    } else {
        d->c -= qe << 16;
        if (d->a & 0x8000u) {
            return cx->mps;
        }
        if (d->a < qe) {            /* MPS_EXCHANGE: decided as the LPS */
            bit = !cx->mps;
            if (qe_tab[cx->i].swap) {
                cx->mps = (unsigned char)!cx->mps;
            }
            cx->i = qe_tab[cx->i].nlps;
        } else {
            bit = cx->mps;
            cx->i = qe_tab[cx->i].nmps;
        }
    }
    do {                            /* RENORMD */
        if (d->ct == 0) {
            mq_in(d);
        }
        d->a <<= 1;
        d->c <<= 1;
        d->ct--;
    } while (!(d->a & 0x8000u));
    return bit;
}
