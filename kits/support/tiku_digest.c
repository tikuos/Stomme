/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_digest.c - MD5 (RFC 1321), SHA-1 and SHA-2 (FIPS 180-4), fed in
 * pieces or all at once; HMAC (RFC 2104) and PBKDF2 (RFC 8018) over them;
 * and CRC-32, the check ZIP, gzip and PNG put after their data.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_crypto.h"

#include <string.h>

/* -------------------------------------------------------------- MD5  */

static const uint32_t md5_k[64] = {
    0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a,
    0xa8304613, 0xfd469501, 0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be,
    0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821, 0xf61e2562, 0xc040b340,
    0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
    0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8,
    0x676f02d9, 0x8d2a4c8a, 0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c,
    0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70, 0x289b7ec6, 0xeaa127fa,
    0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
    0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92,
    0xffeff47d, 0x85845dd1, 0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1,
    0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391
};

static const unsigned char md5_r[16] = {
    7, 12, 17, 22, 5, 9, 14, 20, 4, 11, 16, 23, 6, 10, 15, 21
};

/** @brief @p x turned left by @p n bits. */
static uint32_t
rotl32(uint32_t x, int n)
{
    return (x << n) | (x >> (32 - n));
}

/** @brief One 64-byte block into the state. */
static void
md5_block(tiku_md5_t *c, const unsigned char *p)
{
    uint32_t m[16], a = c->s[0], b = c->s[1], cc = c->s[2], d = c->s[3];
    int i;

    for (i = 0; i < 16; i++) {
        m[i] = (uint32_t)p[4 * i] | (uint32_t)p[4 * i + 1] << 8 |
               (uint32_t)p[4 * i + 2] << 16 | (uint32_t)p[4 * i + 3] << 24;
    }
    for (i = 0; i < 64; i++) {
        uint32_t f, t;
        int g;

        if (i < 16) {
            f = (b & cc) | (~b & d);
            g = i;
        } else if (i < 32) {
            f = (d & b) | (~d & cc);
            g = (5 * i + 1) & 15;
        } else if (i < 48) {
            f = b ^ cc ^ d;
            g = (3 * i + 5) & 15;
        } else {
            f = cc ^ (b | ~d);
            g = (7 * i) & 15;
        }
        t = d;
        d = cc;
        cc = b;
        b = b + rotl32(a + f + md5_k[i] + m[g],
                       md5_r[(i >> 4) * 4 + (i & 3)]);
        a = t;
    }
    c->s[0] += a;
    c->s[1] += b;
    c->s[2] += cc;
    c->s[3] += d;
}

void
tiku_md5_init(tiku_md5_t *c)
{
    c->s[0] = 0x67452301;
    c->s[1] = 0xefcdab89;
    c->s[2] = 0x98badcfe;
    c->s[3] = 0x10325476;
    c->n = 0;
}

void
tiku_md5_update(tiku_md5_t *c, const void *data, size_t n)
{
    const unsigned char *p = data;

    while (n > 0) {
        size_t at = (size_t)(c->n & 63), take = 64 - at;

        if (take > n) take = n;
        memcpy(c->buf + at, p, take);
        c->n += take;
        p += take;
        n -= take;
        if ((c->n & 63) == 0) {
            md5_block(c, c->buf);
        }
    }
}

void
tiku_md5_final(tiku_md5_t *c, unsigned char out[16])
{
    uint64_t bits = c->n * 8;
    unsigned char pad = 0x80, len[8];
    int i;

    tiku_md5_update(c, &pad, 1);
    pad = 0;
    while ((c->n & 63) != 56) {
        tiku_md5_update(c, &pad, 1);
    }
    for (i = 0; i < 8; i++) {
        len[i] = (unsigned char)(bits >> (8 * i));
    }
    tiku_md5_update(c, len, 8);
    for (i = 0; i < 16; i++) {
        out[i] = (unsigned char)(c->s[i / 4] >> (8 * (i % 4)));
    }
}

/* ----------------------------------------------------------- SHA-256  */

static const uint32_t sha256_k[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
    0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
    0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
    0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
    0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
    0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

/** @brief @p x turned right by @p n bits. */
static uint32_t
rotr32(uint32_t x, int n)
{
    return (x >> n) | (x << (32 - n));
}

/** @brief One 64-byte block into the eight words of @p h. */
static void
sha256_block(uint32_t *h, const unsigned char *p)
{
    uint32_t w[64], v[8];
    int i;

    for (i = 0; i < 16; i++) {
        w[i] = (uint32_t)p[4 * i] << 24 | (uint32_t)p[4 * i + 1] << 16 |
               (uint32_t)p[4 * i + 2] << 8 | (uint32_t)p[4 * i + 3];
    }
    for (i = 16; i < 64; i++) {
        uint32_t s0 = rotr32(w[i - 15], 7) ^ rotr32(w[i - 15], 18) ^
                      (w[i - 15] >> 3);
        uint32_t s1 = rotr32(w[i - 2], 17) ^ rotr32(w[i - 2], 19) ^
                      (w[i - 2] >> 10);

        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    memcpy(v, h, sizeof v);
    for (i = 0; i < 64; i++) {
        uint32_t t1 = v[7] + (rotr32(v[4], 6) ^ rotr32(v[4], 11) ^
                              rotr32(v[4], 25)) +
                      ((v[4] & v[5]) ^ (~v[4] & v[6])) + sha256_k[i] + w[i];
        uint32_t t2 = (rotr32(v[0], 2) ^ rotr32(v[0], 13) ^
                       rotr32(v[0], 22)) +
                      ((v[0] & v[1]) ^ (v[0] & v[2]) ^ (v[1] & v[2]));

        memmove(v + 1, v, 7 * sizeof v[0]);
        v[4] += t1;
        v[0] = t1 + t2;
    }
    for (i = 0; i < 8; i++) {
        h[i] += v[i];
    }
}

/* ----------------------------------------------------- SHA-384 / 512  */

static const uint64_t sha512_k[80] = {
    0x428a2f98d728ae22ULL, 0x7137449123ef65cdULL, 0xb5c0fbcfec4d3b2fULL,
    0xe9b5dba58189dbbcULL, 0x3956c25bf348b538ULL, 0x59f111f1b605d019ULL,
    0x923f82a4af194f9bULL, 0xab1c5ed5da6d8118ULL, 0xd807aa98a3030242ULL,
    0x12835b0145706fbeULL, 0x243185be4ee4b28cULL, 0x550c7dc3d5ffb4e2ULL,
    0x72be5d74f27b896fULL, 0x80deb1fe3b1696b1ULL, 0x9bdc06a725c71235ULL,
    0xc19bf174cf692694ULL, 0xe49b69c19ef14ad2ULL, 0xefbe4786384f25e3ULL,
    0x0fc19dc68b8cd5b5ULL, 0x240ca1cc77ac9c65ULL, 0x2de92c6f592b0275ULL,
    0x4a7484aa6ea6e483ULL, 0x5cb0a9dcbd41fbd4ULL, 0x76f988da831153b5ULL,
    0x983e5152ee66dfabULL, 0xa831c66d2db43210ULL, 0xb00327c898fb213fULL,
    0xbf597fc7beef0ee4ULL, 0xc6e00bf33da88fc2ULL, 0xd5a79147930aa725ULL,
    0x06ca6351e003826fULL, 0x142929670a0e6e70ULL, 0x27b70a8546d22ffcULL,
    0x2e1b21385c26c926ULL, 0x4d2c6dfc5ac42aedULL, 0x53380d139d95b3dfULL,
    0x650a73548baf63deULL, 0x766a0abb3c77b2a8ULL, 0x81c2c92e47edaee6ULL,
    0x92722c851482353bULL, 0xa2bfe8a14cf10364ULL, 0xa81a664bbc423001ULL,
    0xc24b8b70d0f89791ULL, 0xc76c51a30654be30ULL, 0xd192e819d6ef5218ULL,
    0xd69906245565a910ULL, 0xf40e35855771202aULL, 0x106aa07032bbd1b8ULL,
    0x19a4c116b8d2d0c8ULL, 0x1e376c085141ab53ULL, 0x2748774cdf8eeb99ULL,
    0x34b0bcb5e19b48a8ULL, 0x391c0cb3c5c95a63ULL, 0x4ed8aa4ae3418acbULL,
    0x5b9cca4f7763e373ULL, 0x682e6ff3d6b2b8a3ULL, 0x748f82ee5defb2fcULL,
    0x78a5636f43172f60ULL, 0x84c87814a1f0ab72ULL, 0x8cc702081a6439ecULL,
    0x90befffa23631e28ULL, 0xa4506cebde82bde9ULL, 0xbef9a3f7b2c67915ULL,
    0xc67178f2e372532bULL, 0xca273eceea26619cULL, 0xd186b8c721c0c207ULL,
    0xeada7dd6cde0eb1eULL, 0xf57d4f7fee6ed178ULL, 0x06f067aa72176fbaULL,
    0x0a637dc5a2c898a6ULL, 0x113f9804bef90daeULL, 0x1b710b35131c471bULL,
    0x28db77f523047d84ULL, 0x32caab7b40c72493ULL, 0x3c9ebe0a15c9bebcULL,
    0x431d67c49c100d4cULL, 0x4cc5d4becb3e42b6ULL, 0x597f299cfc657e2aULL,
    0x5fcb6fab3ad6faecULL, 0x6c44198c4a475817ULL
};

/** @brief @p x turned right by @p n bits. */
static uint64_t
rotr64(uint64_t x, int n)
{
    return (x >> n) | (x << (64 - n));
}

/** @brief One 128-byte block into the eight words of @p h. */
static void
sha512_block(uint64_t *h, const unsigned char *p)
{
    uint64_t w[80], v[8];
    int i, k;

    for (i = 0; i < 16; i++) {
        w[i] = 0;
        for (k = 0; k < 8; k++) {
            w[i] = w[i] << 8 | p[8 * i + k];
        }
    }
    for (i = 16; i < 80; i++) {
        uint64_t s0 = rotr64(w[i - 15], 1) ^ rotr64(w[i - 15], 8) ^
                      (w[i - 15] >> 7);
        uint64_t s1 = rotr64(w[i - 2], 19) ^ rotr64(w[i - 2], 61) ^
                      (w[i - 2] >> 6);

        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    memcpy(v, h, sizeof v);
    for (i = 0; i < 80; i++) {
        uint64_t t1 = v[7] + (rotr64(v[4], 14) ^ rotr64(v[4], 18) ^
                              rotr64(v[4], 41)) +
                      ((v[4] & v[5]) ^ (~v[4] & v[6])) + sha512_k[i] + w[i];
        uint64_t t2 = (rotr64(v[0], 28) ^ rotr64(v[0], 34) ^
                       rotr64(v[0], 39)) +
                      ((v[0] & v[1]) ^ (v[0] & v[2]) ^ (v[1] & v[2]));

        memmove(v + 1, v, 7 * sizeof v[0]);
        v[4] += t1;
        v[0] = t1 + t2;
    }
    for (i = 0; i < 8; i++) {
        h[i] += v[i];
    }
}

/* ------------------------------------------------------------ SHA-1  */

/** @brief One 64-byte block into the five words of @p h. */
static void
sha1_block(uint32_t *h, const unsigned char *p)
{
    uint32_t w[80], a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
    int i;

    for (i = 0; i < 16; i++) {
        w[i] = (uint32_t)p[4 * i] << 24 | (uint32_t)p[4 * i + 1] << 16 |
               (uint32_t)p[4 * i + 2] << 8 | (uint32_t)p[4 * i + 3];
    }
    for (i = 16; i < 80; i++) {
        w[i] = rotl32(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    }
    for (i = 0; i < 80; i++) {
        uint32_t f, k, t;

        if (i < 20) {
            f = (b & c) | (~b & d);
            k = 0x5a827999;
        } else if (i < 40) {
            f = b ^ c ^ d;
            k = 0x6ed9eba1;
        } else if (i < 60) {
            f = (b & c) | (b & d) | (c & d);
            k = 0x8f1bbcdc;
        } else {
            f = b ^ c ^ d;
            k = 0xca62c1d6;
        }
        t = rotl32(a, 5) + f + e + k + w[i];
        e = d;
        d = c;
        c = rotl32(b, 30);
        b = a;
        a = t;
    }
    h[0] += a;
    h[1] += b;
    h[2] += c;
    h[3] += d;
    h[4] += e;
}

/* ------------------------------------------------------ any of them  */

/** @brief The block @p alg digests at a time, in bytes. */
static size_t
block_size(int alg)
{
    return alg == TIKU_HASH_SHA384 || alg == TIKU_HASH_SHA512 ? 128 : 64;
}

size_t
tiku_hash_init(tiku_hash_t *h, int alg)
{
    static const uint32_t w1[5] = { 0x67452301, 0xefcdab89, 0x98badcfe,
                                    0x10325476, 0xc3d2e1f0 };
    static const uint32_t w224[8] = { 0xc1059ed8, 0x367cd507, 0x3070dd17,
        0xf70e5939, 0xffc00b31, 0x68581511, 0x64f98fa7, 0xbefa4fa4 };
    static const uint32_t w256[8] = { 0x6a09e667, 0xbb67ae85, 0x3c6ef372,
        0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 };
    static const uint64_t d384[8] = {
        0xcbbb9d5dc1059ed8ULL, 0x629a292a367cd507ULL, 0x9159015a3070dd17ULL,
        0x152fecd8f70e5939ULL, 0x67332667ffc00b31ULL, 0x8eb44a8768581511ULL,
        0xdb0c2e0d64f98fa7ULL, 0x47b5481dbefa4fa4ULL };
    static const uint64_t d512[8] = {
        0x6a09e667f3bcc908ULL, 0xbb67ae8584caa73bULL, 0x3c6ef372fe94f82bULL,
        0xa54ff53a5f1d36f1ULL, 0x510e527fade682d1ULL, 0x9b05688c2b3e6c1fULL,
        0x1f83d9abfb41bd6bULL, 0x5be0cd19137e2179ULL };

    memset(h, 0, sizeof *h);
    h->alg = alg;
    switch (alg) {
    case TIKU_HASH_MD5: tiku_md5_init(&h->md5); return 16;
    case TIKU_HASH_SHA1: memcpy(h->s.w, w1, sizeof w1); return 20;
    case TIKU_HASH_SHA224: memcpy(h->s.w, w224, sizeof w224); return 28;
    case TIKU_HASH_SHA256: memcpy(h->s.w, w256, sizeof w256); return 32;
    case TIKU_HASH_SHA384: memcpy(h->s.d, d384, sizeof d384); return 48;
    case TIKU_HASH_SHA512: memcpy(h->s.d, d512, sizeof d512); return 64;
    }
    h->alg = 0;
    return 0;
}

/** @brief One whole block into @p h's state. */
static void
hash_block(tiku_hash_t *h, const unsigned char *p)
{
    if (h->alg == TIKU_HASH_SHA1) sha1_block(h->s.w, p);
    else if (block_size(h->alg) == 64) sha256_block(h->s.w, p);
    else sha512_block(h->s.d, p);
}

void
tiku_hash_update(tiku_hash_t *h, const void *data, size_t n)
{
    const unsigned char *p = data;
    size_t bs = block_size(h->alg);

    if (h->alg == TIKU_HASH_MD5) {
        tiku_md5_update(&h->md5, data, n);
        return;
    }
    while (h->alg && n > 0) {
        size_t at = (size_t)(h->n % bs), take = bs - at;

        if (at == 0 && n >= bs) {
            hash_block(h, p);           /* straight from the caller */
            take = bs;
        } else {
            if (take > n) take = n;
            memcpy(h->buf + at, p, take);
            if (at + take == bs) {
                hash_block(h, h->buf);
            }
        }
        h->n += take;
        p += take;
        n -= take;
    }
}

void
tiku_hash_final(tiku_hash_t *h, unsigned char *out)
{
    static const unsigned char size[] = { 0, 16, 20, 28, 32, 48, 64 };
    size_t bs = block_size(h->alg), at = (size_t)(h->n % bs), i;
    uint64_t bits = h->n * 8;

    if (h->alg == TIKU_HASH_MD5) {
        tiku_md5_final(&h->md5, out);
        return;
    }
    if (!h->alg) return;
    h->buf[at++] = 0x80;
    if (at > bs - bs / 8) {             /* no room for the length here */
        memset(h->buf + at, 0, bs - at);
        hash_block(h, h->buf);
        at = 0;
    }
    memset(h->buf + at, 0, bs - at);
    for (i = 0; i < 8; i++) {
        h->buf[bs - 1 - i] = (unsigned char)(bits >> (8 * i));
    }
    hash_block(h, h->buf);
    for (i = 0; i < size[h->alg]; i++) {
        out[i] = bs == 64
                 ? (unsigned char)(h->s.w[i / 4] >> (24 - 8 * (i % 4)))
                 : (unsigned char)(h->s.d[i / 8] >> (56 - 8 * (i % 8)));
    }
}

size_t
tiku_hash(int alg, const void *data, size_t n, unsigned char *out)
{
    tiku_hash_t h;
    size_t len = tiku_hash_init(&h, alg);

    tiku_hash_update(&h, data, n);
    tiku_hash_final(&h, out);
    return len;
}

void
tiku_sha256(const void *data, size_t n, unsigned char out[32])
{
    (void)tiku_hash(TIKU_HASH_SHA256, data, n, out);
}

void
tiku_sha384(const void *data, size_t n, unsigned char out[48])
{
    (void)tiku_hash(TIKU_HASH_SHA384, data, n, out);
}

void
tiku_sha512(const void *data, size_t n, unsigned char out[64])
{
    (void)tiku_hash(TIKU_HASH_SHA512, data, n, out);
}

/* ------------------------------------------------------ HMAC, PBKDF2  */

size_t
tiku_hmac_init(tiku_hmac_t *m, int alg, const void *key, size_t n)
{
    unsigned char k[128], pad[128];
    size_t len = tiku_hash_init(&m->inner, alg), bs = block_size(alg), i;

    if (len == 0) {
        m->outer.alg = 0;
        return 0;
    }
    memset(k, 0, sizeof k);
    if (n > bs) {
        tiku_hash_update(&m->inner, key, n);
        tiku_hash_final(&m->inner, k);
    } else if (n > 0) {
        memcpy(k, key, n);
    }
    for (i = 0; i < bs; i++) pad[i] = (unsigned char)(k[i] ^ 0x36);
    (void)tiku_hash_init(&m->inner, alg);
    tiku_hash_update(&m->inner, pad, bs);
    for (i = 0; i < bs; i++) pad[i] = (unsigned char)(k[i] ^ 0x5c);
    (void)tiku_hash_init(&m->outer, alg);
    tiku_hash_update(&m->outer, pad, bs);
    return len;
}

void
tiku_hmac_update(tiku_hmac_t *m, const void *data, size_t n)
{
    tiku_hash_update(&m->inner, data, n);
}

void
tiku_hmac_final(tiku_hmac_t *m, unsigned char *out)
{
    unsigned char mid[TIKU_HASH_MAX];
    tiku_hash_t probe;
    size_t len = tiku_hash_init(&probe, m->inner.alg);

    tiku_hash_final(&m->inner, mid);
    tiku_hash_update(&m->outer, mid, len);
    tiku_hash_final(&m->outer, out);
}

size_t
tiku_hmac(int alg, const void *key, size_t klen, const void *data, size_t n,
          unsigned char *out)
{
    tiku_hmac_t m;
    size_t len = tiku_hmac_init(&m, alg, key, klen);

    if (len != 0) {
        tiku_hmac_update(&m, data, n);
        tiku_hmac_final(&m, out);
    }
    return len;
}

int
tiku_pbkdf2(int alg, const void *pw, size_t pwlen, const void *salt,
            size_t slen, unsigned long rounds, unsigned char *out,
            size_t outlen)
{
    unsigned char u[TIKU_HASH_MAX], t[TIKU_HASH_MAX], be[4];
    tiku_hmac_t keyed, m;
    size_t len = tiku_hmac_init(&keyed, alg, pw, pwlen), take, i;
    uint32_t block;
    unsigned long r;

    if (len == 0 || rounds == 0) {
        return -1;
    }
    for (block = 1; outlen > 0; block++) {
        be[0] = (unsigned char)(block >> 24);
        be[1] = (unsigned char)(block >> 16);
        be[2] = (unsigned char)(block >> 8);
        be[3] = (unsigned char)block;
        m = keyed;
        tiku_hmac_update(&m, salt, slen);
        tiku_hmac_update(&m, be, 4);
        tiku_hmac_final(&m, u);
        memcpy(t, u, len);
        for (r = 1; r < rounds; r++) {
            m = keyed;
            tiku_hmac_update(&m, u, len);
            tiku_hmac_final(&m, u);
            for (i = 0; i < len; i++) t[i] ^= u[i];
        }
        take = outlen < len ? outlen : len;
        memcpy(out, t, take);
        out += take;
        outlen -= take;
    }
    return 0;
}

/* ------------------------------------------------------------ CRC-32  */

/* The table of the reflected polynomial 0xEDB88320, a byte at a time.
 * It is constant, so any thread may read it. */
static const uint32_t crc32_table[256] = {
    0x00000000, 0x77073096, 0xee0e612c, 0x990951ba, 0x076dc419, 0x706af48f,
    0xe963a535, 0x9e6495a3, 0x0edb8832, 0x79dcb8a4, 0xe0d5e91e, 0x97d2d988,
    0x09b64c2b, 0x7eb17cbd, 0xe7b82d07, 0x90bf1d91, 0x1db71064, 0x6ab020f2,
    0xf3b97148, 0x84be41de, 0x1adad47d, 0x6ddde4eb, 0xf4d4b551, 0x83d385c7,
    0x136c9856, 0x646ba8c0, 0xfd62f97a, 0x8a65c9ec, 0x14015c4f, 0x63066cd9,
    0xfa0f3d63, 0x8d080df5, 0x3b6e20c8, 0x4c69105e, 0xd56041e4, 0xa2677172,
    0x3c03e4d1, 0x4b04d447, 0xd20d85fd, 0xa50ab56b, 0x35b5a8fa, 0x42b2986c,
    0xdbbbc9d6, 0xacbcf940, 0x32d86ce3, 0x45df5c75, 0xdcd60dcf, 0xabd13d59,
    0x26d930ac, 0x51de003a, 0xc8d75180, 0xbfd06116, 0x21b4f4b5, 0x56b3c423,
    0xcfba9599, 0xb8bda50f, 0x2802b89e, 0x5f058808, 0xc60cd9b2, 0xb10be924,
    0x2f6f7c87, 0x58684c11, 0xc1611dab, 0xb6662d3d, 0x76dc4190, 0x01db7106,
    0x98d220bc, 0xefd5102a, 0x71b18589, 0x06b6b51f, 0x9fbfe4a5, 0xe8b8d433,
    0x7807c9a2, 0x0f00f934, 0x9609a88e, 0xe10e9818, 0x7f6a0dbb, 0x086d3d2d,
    0x91646c97, 0xe6635c01, 0x6b6b51f4, 0x1c6c6162, 0x856530d8, 0xf262004e,
    0x6c0695ed, 0x1b01a57b, 0x8208f4c1, 0xf50fc457, 0x65b0d9c6, 0x12b7e950,
    0x8bbeb8ea, 0xfcb9887c, 0x62dd1ddf, 0x15da2d49, 0x8cd37cf3, 0xfbd44c65,
    0x4db26158, 0x3ab551ce, 0xa3bc0074, 0xd4bb30e2, 0x4adfa541, 0x3dd895d7,
    0xa4d1c46d, 0xd3d6f4fb, 0x4369e96a, 0x346ed9fc, 0xad678846, 0xda60b8d0,
    0x44042d73, 0x33031de5, 0xaa0a4c5f, 0xdd0d7cc9, 0x5005713c, 0x270241aa,
    0xbe0b1010, 0xc90c2086, 0x5768b525, 0x206f85b3, 0xb966d409, 0xce61e49f,
    0x5edef90e, 0x29d9c998, 0xb0d09822, 0xc7d7a8b4, 0x59b33d17, 0x2eb40d81,
    0xb7bd5c3b, 0xc0ba6cad, 0xedb88320, 0x9abfb3b6, 0x03b6e20c, 0x74b1d29a,
    0xead54739, 0x9dd277af, 0x04db2615, 0x73dc1683, 0xe3630b12, 0x94643b84,
    0x0d6d6a3e, 0x7a6a5aa8, 0xe40ecf0b, 0x9309ff9d, 0x0a00ae27, 0x7d079eb1,
    0xf00f9344, 0x8708a3d2, 0x1e01f268, 0x6906c2fe, 0xf762575d, 0x806567cb,
    0x196c3671, 0x6e6b06e7, 0xfed41b76, 0x89d32be0, 0x10da7a5a, 0x67dd4acc,
    0xf9b9df6f, 0x8ebeeff9, 0x17b7be43, 0x60b08ed5, 0xd6d6a3e8, 0xa1d1937e,
    0x38d8c2c4, 0x4fdff252, 0xd1bb67f1, 0xa6bc5767, 0x3fb506dd, 0x48b2364b,
    0xd80d2bda, 0xaf0a1b4c, 0x36034af6, 0x41047a60, 0xdf60efc3, 0xa867df55,
    0x316e8eef, 0x4669be79, 0xcb61b38c, 0xbc66831a, 0x256fd2a0, 0x5268e236,
    0xcc0c7795, 0xbb0b4703, 0x220216b9, 0x5505262f, 0xc5ba3bbe, 0xb2bd0b28,
    0x2bb45a92, 0x5cb36a04, 0xc2d7ffa7, 0xb5d0cf31, 0x2cd99e8b, 0x5bdeae1d,
    0x9b64c2b0, 0xec63f226, 0x756aa39c, 0x026d930a, 0x9c0906a9, 0xeb0e363f,
    0x72076785, 0x05005713, 0x95bf4a82, 0xe2b87a14, 0x7bb12bae, 0x0cb61b38,
    0x92d28e9b, 0xe5d5be0d, 0x7cdcefb7, 0x0bdbdf21, 0x86d3d2d4, 0xf1d4e242,
    0x68ddb3f8, 0x1fda836e, 0x81be16cd, 0xf6b9265b, 0x6fb077e1, 0x18b74777,
    0x88085ae6, 0xff0f6a70, 0x66063bca, 0x11010b5c, 0x8f659eff, 0xf862ae69,
    0x616bffd3, 0x166ccf45, 0xa00ae278, 0xd70dd2ee, 0x4e048354, 0x3903b3c2,
    0xa7672661, 0xd06016f7, 0x4969474d, 0x3e6e77db, 0xaed16a4a, 0xd9d65adc,
    0x40df0b66, 0x37d83bf0, 0xa9bcae53, 0xdebb9ec5, 0x47b2cf7f, 0x30b5ffe9,
    0xbdbdf21c, 0xcabac28a, 0x53b39330, 0x24b4a3a6, 0xbad03605, 0xcdd70693,
    0x54de5729, 0x23d967bf, 0xb3667a2e, 0xc4614ab8, 0x5d681b02, 0x2a6f2b94,
    0xb40bbe37, 0xc30c8ea1, 0x5a05df1b, 0x2d02ef8d,
};

uint32_t
tiku_crc32(uint32_t crc, const void *data, size_t n)
{
    const unsigned char *p = data;

    crc = ~crc;
    while (n-- > 0) {
        crc = crc32_table[(crc ^ *p++) & 0xffu] ^ (crc >> 8);
    }
    return ~crc;
}
