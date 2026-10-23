/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_deflate.c - DEFLATE written (RFC 1951): LZ77 and the fixed Huffman
 * codes, in a zlib wrapper (RFC 1950), for the PNG and PDF writers.
 *
 * Each place is looked up by its next three bytes in hash chains over the
 * last 32 KB, trying at most DF_CHAIN earlier places; the longest match
 * found is sent as a length and a distance, and everything goes in one
 * block of the fixed codes.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_inflate.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define DF_WINDOW   32768u          /* how far back a match may reach    */
#define DF_HASH     (1u << 15)
#define DF_CHAIN    48              /* places tried for each match       */
#define DF_MIN      3
#define DF_MAX      258

/** @brief Bytes being written a bit at a time, least significant first. */
typedef struct {
    unsigned char *p;
    size_t n, cap;
    uint32_t bits;
    int nbits, bad;
} df_out_t;

/** @brief One byte onto @p o, the buffer grown as it fills.  A failed
 *         allocation sets o->bad, and later bytes are dropped. */
static void df_byte(df_out_t *o, unsigned char b)
{
    if (o->bad) return;
    if (o->n == o->cap) {
        size_t cap = o->cap ? o->cap * 2 : 65536;
        unsigned char *p = realloc(o->p, cap);

        if (!p) {
            o->bad = 1;
            return;
        }
        o->p = p;
        o->cap = cap;
    }
    o->p[o->n++] = b;
}

/** @brief The low @p n bits of @p v, least significant first. */
static void df_bits(df_out_t *o, uint32_t v, int n)
{
    o->bits |= v << o->nbits;
    o->nbits += n;
    while (o->nbits >= 8) {
        df_byte(o, (unsigned char)o->bits);
        o->bits >>= 8;
        o->nbits -= 8;
    }
}

/** @brief A Huffman code of @p len bits, which DEFLATE sends most
 *         significant first: reversed, then written by df_bits(). */
static void df_code(df_out_t *o, uint32_t code, int len)
{
    uint32_t r = 0;

    for (int i = 0; i < len; i++) r |= ((code >> i) & 1u) << (len - 1 - i);
    df_bits(o, r, len);
}

/** @brief Literal or length symbol @p s in the fixed codes. */
static void df_symbol(df_out_t *o, int s)
{
    if (s < 144) df_code(o, 0x30u + (uint32_t)s, 8);
    else if (s < 256) df_code(o, 0x190u + (uint32_t)(s - 144), 9);
    else if (s < 280) df_code(o, (uint32_t)(s - 256), 7);
    else df_code(o, 0xc0u + (uint32_t)(s - 280), 8);
}

static const uint16_t len_base[29] = {
    3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59,
    67, 83, 99, 115, 131, 163, 195, 227, 258
};
static const uint8_t len_extra[29] = {
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4,
    5, 5, 5, 5, 0
};
static const uint16_t dist_base[30] = {
    1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513,
    769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577
};
static const uint8_t dist_extra[30] = {
    0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10,
    11, 11, 12, 12, 13, 13
};

/** @brief A match of @p len bytes @p dist back: its length's symbol and
 *         extra bits, then its distance's five-bit code and extra bits. */
static void df_match(df_out_t *o, int len, int dist)
{
    int l = 28, d = 29;

    while (len_base[l] > len) l--;
    df_symbol(o, 257 + l);
    df_bits(o, (uint32_t)(len - len_base[l]), len_extra[l]);
    while (dist_base[d] > dist) d--;
    df_code(o, (uint32_t)d, 5);
    df_bits(o, (uint32_t)(dist - dist_base[d]), dist_extra[d]);
}

/** @brief The chain a place's next three bytes hash to. */
static uint32_t df_hash(const unsigned char *p)
{
    return ((uint32_t)p[0] << 10 ^ (uint32_t)p[1] << 5 ^ p[2]) & (DF_HASH - 1);
}

/** @brief The hash chains: @c head holds each hash's latest place, and
 *         @c prev each place's previous one with the same hash, indexed
 *         modulo the window.  Places are stored plus one; 0 is none. */
typedef struct {
    uint32_t *head, *prev;
} df_chains_t;

/** @brief Place @p i entered in its hash's chain. */
static void df_insert(df_chains_t *t, const unsigned char *in, size_t n,
                      size_t i)
{
    uint32_t h;

    if (i + DF_MIN > n) return;
    h = df_hash(in + i);
    t->prev[i & (DF_WINDOW - 1)] = t->head[h];
    t->head[h] = (uint32_t)i + 1;
}

/** @brief The longest match for place @p i within the window, its
 *         distance into @p dist.  @return its length, 0 below DF_MIN. */
static int df_longest(const df_chains_t *t, const unsigned char *in,
                      size_t n, size_t i, int *dist)
{
    uint32_t at = i + DF_MIN <= n ? t->head[df_hash(in + i)] : 0;
    int best = 0, tries = DF_CHAIN;
    size_t most = n - i < DF_MAX ? n - i : DF_MAX;

    while (at && tries-- > 0) {
        size_t j = at - 1, k = 0;

        if (i - j > DF_WINDOW || j >= i) break;
        while (k < most && in[j + k] == in[i + k]) k++;
        if ((int)k > best) {
            best = (int)k;
            *dist = (int)(i - j);
            if (k == most) break;
        }
        at = t->prev[j & (DF_WINDOW - 1)];
    }
    return best >= DF_MIN ? best : 0;
}

unsigned char *
tiku_deflate_zlib(const unsigned char *in, size_t n, size_t *outlen)
{
    df_out_t o = { NULL, 0, 0, 0, 0, 0 };
    df_chains_t t = { calloc(DF_HASH, sizeof(uint32_t)),
                      calloc(DF_WINDOW, sizeof(uint32_t)) };
    uint32_t a = 1, b = 0;
    size_t i = 0;

    df_byte(&o, 0x78);              /* deflate, a 32 KB window */
    df_byte(&o, 0x01);
    df_bits(&o, 1, 1);              /* the final block, */
    df_bits(&o, 1, 2);              /* in the fixed codes */
    while (t.head && t.prev && i < n) {
        int dist = 0, len = df_longest(&t, in, n, i, &dist);

        if (len) {
            df_match(&o, len, dist);
            for (int k = 0; k < len; k++) df_insert(&t, in, n, i + (size_t)k);
            i += (size_t)len;
        } else {
            df_symbol(&o, in[i]);
            df_insert(&t, in, n, i++);
        }
    }
    df_symbol(&o, 256);
    df_bits(&o, 0, 7);              /* to a byte boundary */
    for (i = 0; i < n; i++) {       /* Adler-32 of what went in */
        a = (a + in[i]) % 65521u;
        b = (b + a) % 65521u;
    }
    for (int s = 24; s >= 0; s -= 8)
        df_byte(&o, (unsigned char)((b << 16 | a) >> s));
    if (!t.head || !t.prev || o.bad) {
        free(o.p);
        o.p = NULL;
    }
    free(t.head);
    free(t.prev);
    if (outlen) *outlen = o.p ? o.n : 0;
    return o.p;
}
