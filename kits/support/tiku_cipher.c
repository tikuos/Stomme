/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cipher.c - RC4; AES with 128, 192 and 256-bit keys (FIPS 197),
 * a byte at a time, with the S-box and its inverse computed into each
 * key's context, so no table is shared between threads; DES and triple
 * DES (FIPS 46-3, SP 800-67); and RC2 (RFC 2268), which some key files
 * and envelopes are sealed with.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_crypto.h"

#include <string.h>

/* -------------------------------------------------------------- RC4  */

void
tiku_rc4_init(tiku_rc4_t *c, const unsigned char *key, size_t n)
{
    unsigned j = 0, i;

    for (i = 0; i < 256; i++) {
        c->s[i] = (unsigned char)i;
    }
    for (i = 0; n > 0 && i < 256; i++) {
        unsigned char t = c->s[i];

        j = (j + t + key[i % n]) & 255;
        c->s[i] = c->s[j];
        c->s[j] = t;
    }
    c->i = 0;
    c->j = 0;
}

void
tiku_rc4_crypt(tiku_rc4_t *c, unsigned char *data, size_t n)
{
    size_t k;

    for (k = 0; k < n; k++) {
        unsigned char t;

        c->i = (unsigned char)(c->i + 1);
        c->j = (unsigned char)(c->j + c->s[c->i]);
        t = c->s[c->i];
        c->s[c->i] = c->s[c->j];
        c->s[c->j] = t;
        data[k] ^= c->s[(unsigned char)(c->s[c->i] + c->s[c->j])];
    }
}

/* -------------------------------------------------------------- AES  */

/** @brief @p x times two in GF(2^8). */
static unsigned char
xtime(unsigned char x)
{
    return (unsigned char)((x << 1) ^ ((x & 0x80) ? 0x1b : 0));
}

/** @brief @p a times @p b in GF(2^8). */
static unsigned char
gmul(unsigned char a, unsigned char b)
{
    unsigned char p = 0;

    while (b) {
        if (b & 1) p ^= a;
        a = xtime(a);
        b >>= 1;
    }
    return p;
}

/** @brief Compute the S-box and its inverse into @p c: walk the field by
 *         powers of 3, keep each power's multiplicative inverse, and put
 *         that inverse through the affine map. */
static void
sboxes(tiku_aes_t *c)
{
    unsigned char p = 1, q = 1;
    int i;

    do {
        unsigned char x;

        p = (unsigned char)(p ^ (p << 1) ^ ((p & 0x80) ? 0x1b : 0));
        q ^= (unsigned char)(q << 1);
        q ^= (unsigned char)(q << 2);
        q ^= (unsigned char)(q << 4);
        if (q & 0x80) q ^= 0x09;
        x = (unsigned char)(q ^ (q << 1 | q >> 7) ^ (q << 2 | q >> 6) ^
                            (q << 3 | q >> 5) ^ (q << 4 | q >> 4));
        c->sbox[p] = (unsigned char)(x ^ 0x63);
    } while (p != 1);
    c->sbox[0] = 0x63;
    for (i = 0; i < 256; i++) {
        c->inv[c->sbox[i]] = (unsigned char)i;
    }
}

int
tiku_aes_init(tiku_aes_t *c, const unsigned char *key, int bits)
{
    int nk = bits / 32, words, i;
    unsigned char rcon = 1;

    if (bits != 128 && bits != 192 && bits != 256) {
        return -1;
    }
    sboxes(c);
    c->rounds = nk + 6;
    words = 4 * (c->rounds + 1);
    memcpy(c->rk, key, (size_t)nk * 4);
    for (i = nk; i < words; i++) {
        unsigned char t[4];
        int k;

        memcpy(t, c->rk + 4 * (i - 1), 4);
        if (i % nk == 0) {
            unsigned char first = t[0];

            t[0] = (unsigned char)(c->sbox[t[1]] ^ rcon);
            t[1] = c->sbox[t[2]];
            t[2] = c->sbox[t[3]];
            t[3] = c->sbox[first];
            rcon = xtime(rcon);
        } else if (nk > 6 && i % nk == 4) {
            for (k = 0; k < 4; k++) t[k] = c->sbox[t[k]];
        }
        for (k = 0; k < 4; k++) {
            c->rk[4 * i + k] = (unsigned char)(c->rk[4 * (i - nk) + k] ^ t[k]);
        }
    }
    return 0;
}

/** @brief The state XORed with round key @p r. */
static void
add_key(unsigned char *s, const tiku_aes_t *c, int r)
{
    int i;

    for (i = 0; i < 16; i++) {
        s[i] ^= c->rk[16 * r + i];
    }
}

/** @brief Row @p r of the state turned left by @p by columns. */
static void
turn_row(unsigned char *s, int r, int by)
{
    unsigned char t[4];
    int k;

    for (k = 0; k < 4; k++) {
        t[k] = s[r + 4 * ((k + by) & 3)];
    }
    for (k = 0; k < 4; k++) {
        s[r + 4 * k] = t[k];
    }
}

void
tiku_aes_encrypt(const tiku_aes_t *c, const unsigned char in[16],
                 unsigned char out[16])
{
    unsigned char s[16];
    int round, i;

    memcpy(s, in, 16);
    add_key(s, c, 0);
    for (round = 1; round <= c->rounds; round++) {
        for (i = 0; i < 16; i++) s[i] = c->sbox[s[i]];
        for (i = 1; i < 4; i++) turn_row(s, i, i);
        if (round < c->rounds) {
            for (i = 0; i < 16; i += 4) {
                unsigned char a0 = s[i], a1 = s[i + 1], a2 = s[i + 2];
                unsigned char a3 = s[i + 3], all = a0 ^ a1 ^ a2 ^ a3;

                s[i] ^= all ^ xtime(a0 ^ a1);
                s[i + 1] ^= all ^ xtime(a1 ^ a2);
                s[i + 2] ^= all ^ xtime(a2 ^ a3);
                s[i + 3] ^= all ^ xtime(a3 ^ a0);
            }
        }
        add_key(s, c, round);
    }
    memcpy(out, s, 16);
}

void
tiku_aes_decrypt(const tiku_aes_t *c, const unsigned char in[16],
                 unsigned char out[16])
{
    unsigned char s[16];
    int round, i;

    memcpy(s, in, 16);
    add_key(s, c, c->rounds);
    for (round = c->rounds - 1; round >= 0; round--) {
        for (i = 1; i < 4; i++) turn_row(s, i, 4 - i);
        for (i = 0; i < 16; i++) s[i] = c->inv[s[i]];
        add_key(s, c, round);
        if (round > 0) {
            for (i = 0; i < 16; i += 4) {
                unsigned char a0 = s[i], a1 = s[i + 1], a2 = s[i + 2];
                unsigned char a3 = s[i + 3];

                s[i] = gmul(a0, 14) ^ gmul(a1, 11) ^ gmul(a2, 13) ^
                       gmul(a3, 9);
                s[i + 1] = gmul(a0, 9) ^ gmul(a1, 14) ^ gmul(a2, 11) ^
                           gmul(a3, 13);
                s[i + 2] = gmul(a0, 13) ^ gmul(a1, 9) ^ gmul(a2, 14) ^
                           gmul(a3, 11);
                s[i + 3] = gmul(a0, 11) ^ gmul(a1, 13) ^ gmul(a2, 9) ^
                           gmul(a3, 14);
            }
        }
    }
    memcpy(out, s, 16);
}

void
tiku_aes_cbc_encrypt(const tiku_aes_t *c, unsigned char iv[16],
                     unsigned char *data, size_t n)
{
    size_t at;
    int i;

    for (at = 0; at + 16 <= n; at += 16) {
        for (i = 0; i < 16; i++) data[at + i] ^= iv[i];
        tiku_aes_encrypt(c, data + at, data + at);
        memcpy(iv, data + at, 16);
    }
}

void
tiku_aes_cbc_decrypt(const tiku_aes_t *c, unsigned char iv[16],
                     unsigned char *data, size_t n)
{
    unsigned char next[16];
    size_t at;
    int i;

    for (at = 0; at + 16 <= n; at += 16) {
        memcpy(next, data + at, 16);
        tiku_aes_decrypt(c, data + at, data + at);
        for (i = 0; i < 16; i++) data[at + i] ^= iv[i];
        memcpy(iv, next, 16);
    }
}

/* -------------------------------------------------------------- DES  */

/* The standard's tables, bits numbered from 1 at the most significant. */
static const unsigned char des_ip[64] = {
    58, 50, 42, 34, 26, 18, 10, 2, 60, 52, 44, 36, 28, 20, 12, 4,
    62, 54, 46, 38, 30, 22, 14, 6, 64, 56, 48, 40, 32, 24, 16, 8,
    57, 49, 41, 33, 25, 17, 9, 1, 59, 51, 43, 35, 27, 19, 11, 3,
    61, 53, 45, 37, 29, 21, 13, 5, 63, 55, 47, 39, 31, 23, 15, 7
};
static const unsigned char des_fp[64] = {
    40, 8, 48, 16, 56, 24, 64, 32, 39, 7, 47, 15, 55, 23, 63, 31,
    38, 6, 46, 14, 54, 22, 62, 30, 37, 5, 45, 13, 53, 21, 61, 29,
    36, 4, 44, 12, 52, 20, 60, 28, 35, 3, 43, 11, 51, 19, 59, 27,
    34, 2, 42, 10, 50, 18, 58, 26, 33, 1, 41, 9, 49, 17, 57, 25
};
static const unsigned char des_e[48] = {
    32, 1, 2, 3, 4, 5, 4, 5, 6, 7, 8, 9, 8, 9, 10, 11, 12, 13,
    12, 13, 14, 15, 16, 17, 16, 17, 18, 19, 20, 21, 20, 21, 22, 23, 24, 25,
    24, 25, 26, 27, 28, 29, 28, 29, 30, 31, 32, 1
};
static const unsigned char des_p[32] = {
    16, 7, 20, 21, 29, 12, 28, 17, 1, 15, 23, 26, 5, 18, 31, 10,
    2, 8, 24, 14, 32, 27, 3, 9, 19, 13, 30, 6, 22, 11, 4, 25
};
static const unsigned char des_pc1[56] = {
    57, 49, 41, 33, 25, 17, 9, 1, 58, 50, 42, 34, 26, 18,
    10, 2, 59, 51, 43, 35, 27, 19, 11, 3, 60, 52, 44, 36,
    63, 55, 47, 39, 31, 23, 15, 7, 62, 54, 46, 38, 30, 22,
    14, 6, 61, 53, 45, 37, 29, 21, 13, 5, 28, 20, 12, 4
};
static const unsigned char des_pc2[48] = {
    14, 17, 11, 24, 1, 5, 3, 28, 15, 6, 21, 10, 23, 19, 12, 4,
    26, 8, 16, 7, 27, 20, 13, 2, 41, 52, 31, 37, 47, 55, 30, 40,
    51, 45, 33, 48, 44, 49, 39, 56, 34, 53, 46, 42, 50, 36, 29, 32
};
static const unsigned char des_s[8][64] = {
    { 14, 4, 13, 1, 2, 15, 11, 8, 3, 10, 6, 12, 5, 9, 0, 7,
      0, 15, 7, 4, 14, 2, 13, 1, 10, 6, 12, 11, 9, 5, 3, 8,
      4, 1, 14, 8, 13, 6, 2, 11, 15, 12, 9, 7, 3, 10, 5, 0,
      15, 12, 8, 2, 4, 9, 1, 7, 5, 11, 3, 14, 10, 0, 6, 13 },
    { 15, 1, 8, 14, 6, 11, 3, 4, 9, 7, 2, 13, 12, 0, 5, 10,
      3, 13, 4, 7, 15, 2, 8, 14, 12, 0, 1, 10, 6, 9, 11, 5,
      0, 14, 7, 11, 10, 4, 13, 1, 5, 8, 12, 6, 9, 3, 2, 15,
      13, 8, 10, 1, 3, 15, 4, 2, 11, 6, 7, 12, 0, 5, 14, 9 },
    { 10, 0, 9, 14, 6, 3, 15, 5, 1, 13, 12, 7, 11, 4, 2, 8,
      13, 7, 0, 9, 3, 4, 6, 10, 2, 8, 5, 14, 12, 11, 15, 1,
      13, 6, 4, 9, 8, 15, 3, 0, 11, 1, 2, 12, 5, 10, 14, 7,
      1, 10, 13, 0, 6, 9, 8, 7, 4, 15, 14, 3, 11, 5, 2, 12 },
    { 7, 13, 14, 3, 0, 6, 9, 10, 1, 2, 8, 5, 11, 12, 4, 15,
      13, 8, 11, 5, 6, 15, 0, 3, 4, 7, 2, 12, 1, 10, 14, 9,
      10, 6, 9, 0, 12, 11, 7, 13, 15, 1, 3, 14, 5, 2, 8, 4,
      3, 15, 0, 6, 10, 1, 13, 8, 9, 4, 5, 11, 12, 7, 2, 14 },
    { 2, 12, 4, 1, 7, 10, 11, 6, 8, 5, 3, 15, 13, 0, 14, 9,
      14, 11, 2, 12, 4, 7, 13, 1, 5, 0, 15, 10, 3, 9, 8, 6,
      4, 2, 1, 11, 10, 13, 7, 8, 15, 9, 12, 5, 6, 3, 0, 14,
      11, 8, 12, 7, 1, 14, 2, 13, 6, 15, 0, 9, 10, 4, 5, 3 },
    { 12, 1, 10, 15, 9, 2, 6, 8, 0, 13, 3, 4, 14, 7, 5, 11,
      10, 15, 4, 2, 7, 12, 9, 5, 6, 1, 13, 14, 0, 11, 3, 8,
      9, 14, 15, 5, 2, 8, 12, 3, 7, 0, 4, 10, 1, 13, 11, 6,
      4, 3, 2, 12, 9, 5, 15, 10, 11, 14, 1, 7, 6, 0, 8, 13 },
    { 4, 11, 2, 14, 15, 0, 8, 13, 3, 12, 9, 7, 5, 10, 6, 1,
      13, 0, 11, 7, 4, 9, 1, 10, 14, 3, 5, 12, 2, 15, 8, 6,
      1, 4, 11, 13, 12, 3, 7, 14, 10, 15, 6, 8, 0, 5, 9, 2,
      6, 11, 13, 8, 1, 4, 10, 7, 9, 5, 0, 15, 14, 2, 3, 12 },
    { 13, 2, 8, 4, 6, 15, 11, 1, 10, 9, 3, 14, 5, 0, 12, 7,
      1, 15, 13, 8, 10, 3, 7, 4, 12, 5, 6, 11, 0, 14, 9, 2,
      7, 11, 4, 1, 9, 12, 14, 2, 0, 6, 10, 13, 15, 3, 5, 8,
      2, 1, 14, 7, 4, 10, 8, 13, 15, 12, 9, 0, 3, 5, 6, 11 }
};

/** @brief The @p n bits table @p t picks from the @p width-bit @p in. */
static uint64_t
permute(uint64_t in, const unsigned char *t, int n, int width)
{
    uint64_t out = 0;
    int i;

    for (i = 0; i < n; i++) {
        out = out << 1 | ((in >> (width - t[i])) & 1);
    }
    return out;
}

/** @brief One key's sixteen round keys, in the order enciphering uses. */
static void
des_schedule(uint64_t *rk, const unsigned char *key)
{
    static const unsigned char shifts[16] = { 1, 1, 2, 2, 2, 2, 2, 2, 1, 2,
                                              2, 2, 2, 2, 2, 1 };
    uint64_t k = 0, cd;
    uint32_t c, d;
    int i;

    for (i = 0; i < 8; i++) {
        k = k << 8 | key[i];
    }
    cd = permute(k, des_pc1, 56, 64);
    c = (uint32_t)(cd >> 28) & 0xfffffff;
    d = (uint32_t)cd & 0xfffffff;
    for (i = 0; i < 16; i++) {
        c = ((c << shifts[i]) | (c >> (28 - shifts[i]))) & 0xfffffff;
        d = ((d << shifts[i]) | (d >> (28 - shifts[i]))) & 0xfffffff;
        rk[i] = permute((uint64_t)c << 28 | d, des_pc2, 48, 56);
    }
}

/** @brief The round function: @p r expanded, keyed, through the S-boxes
 *         and permuted. */
static uint32_t
des_f(uint32_t r, uint64_t k)
{
    uint64_t x = permute(r, des_e, 48, 32) ^ k;
    uint32_t out = 0;
    int i;

    for (i = 0; i < 8; i++) {
        unsigned six = (unsigned)(x >> (42 - 6 * i)) & 63;
        unsigned row = (six & 32) >> 4 | (six & 1), col = (six >> 1) & 15;

        out = out << 4 | des_s[i][row * 16 + col];
    }
    return (uint32_t)permute(out, des_p, 32, 32);
}

/** @brief One block through one key's sixteen rounds; with @p back the
 *         round keys run in reverse order, which deciphers. */
static uint64_t
des_block(const uint64_t *rk, uint64_t in, int back)
{
    uint64_t b = permute(in, des_ip, 64, 64);
    uint32_t l = (uint32_t)(b >> 32), r = (uint32_t)b, t;
    int i;

    for (i = 0; i < 16; i++) {
        t = r;
        r = l ^ des_f(r, rk[back ? 15 - i : i]);
        l = t;
    }
    return permute((uint64_t)r << 32 | l, des_fp, 64, 64);
}

int
tiku_des3_init(tiku_des3_t *c, const unsigned char *key, size_t n)
{
    if (n != 8 && n != 16 && n != 24) {
        return -1;
    }
    des_schedule(c->k[0], key);
    des_schedule(c->k[1], n == 8 ? key : key + 8);
    des_schedule(c->k[2], n == 24 ? key + 16 : n == 16 ? key : key);
    return 0;
}

/** @brief @p b as the 64-bit number its bytes spell, first byte highest. */
static uint64_t
be64(const unsigned char *b)
{
    uint64_t v = 0;
    int i;

    for (i = 0; i < 8; i++) {
        v = v << 8 | b[i];
    }
    return v;
}

/** @brief @p v back into bytes at @p b, highest first. */
static void
put_be64(unsigned char *b, uint64_t v)
{
    int i;

    for (i = 7; i >= 0; i--) {
        b[i] = (unsigned char)v;
        v >>= 8;
    }
}

void
tiku_des3_encrypt(const tiku_des3_t *c, const unsigned char in[8],
                  unsigned char out[8])
{
    uint64_t v = be64(in);

    v = des_block(c->k[0], v, 0);
    v = des_block(c->k[1], v, 1);
    put_be64(out, des_block(c->k[2], v, 0));
}

void
tiku_des3_decrypt(const tiku_des3_t *c, const unsigned char in[8],
                  unsigned char out[8])
{
    uint64_t v = be64(in);

    v = des_block(c->k[2], v, 1);
    v = des_block(c->k[1], v, 0);
    put_be64(out, des_block(c->k[0], v, 1));
}

/* -------------------------------------------------------------- RC2  */

/* PITABLE: a permutation of the bytes, from the digits of pi. */
static const unsigned char rc2_pi[256] = {
    0xd9, 0x78, 0xf9, 0xc4, 0x19, 0xdd, 0xb5, 0xed, 0x28, 0xe9, 0xfd, 0x79,
    0x4a, 0xa0, 0xd8, 0x9d, 0xc6, 0x7e, 0x37, 0x83, 0x2b, 0x76, 0x53, 0x8e,
    0x62, 0x4c, 0x64, 0x88, 0x44, 0x8b, 0xfb, 0xa2, 0x17, 0x9a, 0x59, 0xf5,
    0x87, 0xb3, 0x4f, 0x13, 0x61, 0x45, 0x6d, 0x8d, 0x09, 0x81, 0x7d, 0x32,
    0xbd, 0x8f, 0x40, 0xeb, 0x86, 0xb7, 0x7b, 0x0b, 0xf0, 0x95, 0x21, 0x22,
    0x5c, 0x6b, 0x4e, 0x82, 0x54, 0xd6, 0x65, 0x93, 0xce, 0x60, 0xb2, 0x1c,
    0x73, 0x56, 0xc0, 0x14, 0xa7, 0x8c, 0xf1, 0xdc, 0x12, 0x75, 0xca, 0x1f,
    0x3b, 0xbe, 0xe4, 0xd1, 0x42, 0x3d, 0xd4, 0x30, 0xa3, 0x3c, 0xb6, 0x26,
    0x6f, 0xbf, 0x0e, 0xda, 0x46, 0x69, 0x07, 0x57, 0x27, 0xf2, 0x1d, 0x9b,
    0xbc, 0x94, 0x43, 0x03, 0xf8, 0x11, 0xc7, 0xf6, 0x90, 0xef, 0x3e, 0xe7,
    0x06, 0xc3, 0xd5, 0x2f, 0xc8, 0x66, 0x1e, 0xd7, 0x08, 0xe8, 0xea, 0xde,
    0x80, 0x52, 0xee, 0xf7, 0x84, 0xaa, 0x72, 0xac, 0x35, 0x4d, 0x6a, 0x2a,
    0x96, 0x1a, 0xd2, 0x71, 0x5a, 0x15, 0x49, 0x74, 0x4b, 0x9f, 0xd0, 0x5e,
    0x04, 0x18, 0xa4, 0xec, 0xc2, 0xe0, 0x41, 0x6e, 0x0f, 0x51, 0xcb, 0xcc,
    0x24, 0x91, 0xaf, 0x50, 0xa1, 0xf4, 0x70, 0x39, 0x99, 0x7c, 0x3a, 0x85,
    0x23, 0xb8, 0xb4, 0x7a, 0xfc, 0x02, 0x36, 0x5b, 0x25, 0x55, 0x97, 0x31,
    0x2d, 0x5d, 0xfa, 0x98, 0xe3, 0x8a, 0x92, 0xae, 0x05, 0xdf, 0x29, 0x10,
    0x67, 0x6c, 0xba, 0xc9, 0xd3, 0x00, 0xe6, 0xcf, 0xe1, 0x9e, 0xa8, 0x2c,
    0x63, 0x16, 0x01, 0x3f, 0x58, 0xe2, 0x89, 0xa9, 0x0d, 0x38, 0x34, 0x1b,
    0xab, 0x33, 0xff, 0xb0, 0xbb, 0x48, 0x0c, 0x5f, 0xb9, 0xb1, 0xcd, 0x2e,
    0xc5, 0xf3, 0xdb, 0x47, 0xe5, 0xa5, 0x9c, 0x77, 0x0a, 0xa6, 0x20, 0x68,
    0xfe, 0x7f, 0xc1, 0xad
};

int
tiku_rc2_init(tiku_rc2_t *c, const unsigned char *key, size_t n, int bits)
{
    unsigned char l[128];
    int t8, i;
    unsigned tm;

    if (n < 1 || n > 128 || bits < 1 || bits > 1024) {
        return -1;
    }
    memcpy(l, key, n);
    for (i = (int)n; i < 128; i++) {
        l[i] = rc2_pi[(l[i - 1] + l[i - (int)n]) & 255];
    }
    t8 = (bits + 7) / 8;
    tm = 255u % (1u << (8 + bits - 8 * t8));
    l[128 - t8] = rc2_pi[l[128 - t8] & tm];
    for (i = 127 - t8; i >= 0; i--) {
        l[i] = rc2_pi[l[i + 1] ^ l[i + t8]];
    }
    for (i = 0; i < 64; i++) {
        c->k[i] = (uint16_t)(l[2 * i] | l[2 * i + 1] << 8);
    }
    return 0;
}

/** @brief @p x turned left by @p n bits, as a 16-bit value. */
static uint16_t
rotl16(uint16_t x, int n)
{
    return (uint16_t)(x << n | x >> (16 - n));
}

void
tiku_rc2_encrypt(const tiku_rc2_t *c, const unsigned char in[8],
                 unsigned char out[8])
{
    static const int s[4] = { 1, 2, 3, 5 };
    uint16_t r[4];
    int i, j = 0, round;

    for (i = 0; i < 4; i++) {
        r[i] = (uint16_t)(in[2 * i] | in[2 * i + 1] << 8);
    }
    for (round = 0; round < 16; round++) {
        for (i = 0; i < 4; i++) {           /* one mixing round */
            r[i] = (uint16_t)(r[i] + c->k[j++] + (r[(i + 3) & 3] &
                              r[(i + 2) & 3]) + (~r[(i + 3) & 3] &
                              r[(i + 1) & 3]));
            r[i] = rotl16(r[i], s[i]);
        }
        if (round == 4 || round == 10) {    /* a mashing round */
            for (i = 0; i < 4; i++) {
                r[i] = (uint16_t)(r[i] + c->k[r[(i + 3) & 3] & 63]);
            }
        }
    }
    for (i = 0; i < 4; i++) {
        out[2 * i] = (unsigned char)r[i];
        out[2 * i + 1] = (unsigned char)(r[i] >> 8);
    }
}

void
tiku_rc2_decrypt(const tiku_rc2_t *c, const unsigned char in[8],
                 unsigned char out[8])
{
    static const int s[4] = { 1, 2, 3, 5 };
    uint16_t r[4];
    int i, j = 63, round;

    for (i = 0; i < 4; i++) {
        r[i] = (uint16_t)(in[2 * i] | in[2 * i + 1] << 8);
    }
    for (round = 15; round >= 0; round--) {
        for (i = 3; i >= 0; i--) {          /* a mixing round undone */
            r[i] = rotl16(r[i], 16 - s[i]);
            r[i] = (uint16_t)(r[i] - c->k[j--] - (r[(i + 3) & 3] &
                              r[(i + 2) & 3]) - (~r[(i + 3) & 3] &
                              r[(i + 1) & 3]));
        }
        if (round == 5 || round == 11) {    /* a mashing round undone */
            for (i = 3; i >= 0; i--) {
                r[i] = (uint16_t)(r[i] - c->k[r[(i + 3) & 3] & 63]);
            }
        }
    }
    for (i = 0; i < 4; i++) {
        out[2 * i] = (unsigned char)r[i];
        out[2 * i + 1] = (unsigned char)(r[i] >> 8);
    }
}
