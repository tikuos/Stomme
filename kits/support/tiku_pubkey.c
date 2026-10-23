/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_pubkey.c - RSA and ECDSA from RFC 8017 and SEC 1, over numbers of
 * 32-bit limbs: Montgomery products (CIOS) for every modular product and
 * power, and the few plain reductions bit by bit.  The curves are the
 * NIST primes' and Brainpool's (RFC 5639), their points in Jacobian
 * coordinates with the Bernstein-Lange doubling and addition.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_pubkey.h"
#include "tiku_crypto.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define BN_LIMBS 264                /* 8448 bits: 8192 and 8 limbs more  */

/** @brief A number: limbs low first, the top one not zero. */
typedef struct {
    int      n;                     /* limbs in use; 0 for zero          */
    uint32_t d[BN_LIMBS];
} bn_t;

/** @brief Montgomery products modulo an odd number. */
typedef struct {
    bn_t     m, rr;                 /* the modulus; R squared mod it     */
    uint32_t minv;                  /* -1/m mod 2^32                     */
    int      n;                     /* m's limbs; R is 2^(32n)           */
} mont_t;

/* ------------------------------------------------------------ numbers  */

/** @brief Drop the zero limbs at the top. */
static void
bn_trim(bn_t *a)
{
    while (a->n > 0 && a->d[a->n - 1] == 0) {
        a->n--;
    }
}

/** @brief @p a is the small number @p v. */
static void
bn_set(bn_t *a, uint32_t v)
{
    a->n = v != 0;
    a->d[0] = v;
}

/** @brief @p r is @p a. */
static void
bn_copy(bn_t *r, const bn_t *a)
{
    r->n = a->n;
    memcpy(r->d, a->d, (size_t)a->n * sizeof a->d[0]);
}

/** @brief @p len big-endian bytes as a number.  @return 0, or -1 when it
 *         takes more than BN_LIMBS - 4 limbs. */
static int
bn_read(bn_t *a, const unsigned char *b, size_t len)
{
    size_t i, k;

    while (len > 0 && *b == 0) {
        b++;
        len--;
    }
    if (len > (BN_LIMBS - 4) * 4) {
        return -1;
    }
    a->n = (int)((len + 3) / 4);
    memset(a->d, 0, (size_t)a->n * sizeof a->d[0]);
    for (i = 0; i < len; i++) {
        k = len - 1 - i;            /* the byte's place from the low end */
        a->d[k / 4] |= (uint32_t)b[i] << (8 * (k % 4));
    }
    bn_trim(a);
    return 0;
}

/** @brief @p a as exactly @p len big-endian bytes.  @return 0, or -1 when
 *         it does not fit. */
static int
bn_write(const bn_t *a, unsigned char *b, size_t len)
{
    size_t at;
    int i, k;

    memset(b, 0, len);
    for (i = 0; i < a->n; i++) {
        for (k = 0; k < 4; k++) {
            unsigned char v = (unsigned char)(a->d[i] >> (8 * k));

            at = (size_t)i * 4 + (size_t)k;
            if (at < len) {
                b[len - 1 - at] = v;
            } else if (v != 0) {
                return -1;
            }
        }
    }
    return 0;
}

/** @brief @p hex, lower-case hex digits and nothing else, as a number. */
static void
bn_hex(bn_t *a, const char *hex)
{
    unsigned char b[80];
    size_t n = 0;

    for (; hex[0] && hex[1] && n < sizeof b; hex += 2) {
        int hi = hex[0] <= '9' ? hex[0] - '0' : hex[0] - 'a' + 10;
        int lo = hex[1] <= '9' ? hex[1] - '0' : hex[1] - 'a' + 10;

        b[n++] = (unsigned char)(hi << 4 | lo);
    }
    (void)bn_read(a, b, n);
}

/** @brief Order of @p a and @p b: -1, 0 or 1. */
static int
bn_cmp(const bn_t *a, const bn_t *b)
{
    int i;

    if (a->n != b->n) {
        return a->n < b->n ? -1 : 1;
    }
    for (i = a->n - 1; i >= 0; i--) {
        if (a->d[i] != b->d[i]) {
            return a->d[i] < b->d[i] ? -1 : 1;
        }
    }
    return 0;
}

/** @brief How many bits @p a takes. */
static int
bn_bits(const bn_t *a)
{
    uint32_t top;
    int bits;

    if (a->n == 0) {
        return 0;
    }
    bits = 32 * (a->n - 1);
    for (top = a->d[a->n - 1]; top != 0; top >>= 1) {
        bits++;
    }
    return bits;
}

/** @brief Bit @p i of @p a. */
static int
bn_bit(const bn_t *a, int i)
{
    return i / 32 < a->n ? (int)(a->d[i / 32] >> (i % 32)) & 1 : 0;
}

/** @brief @p r = @p a - @p b, @p a no less than @p b; @p r may be either. */
static void
bn_sub(bn_t *r, const bn_t *a, const bn_t *b)
{
    uint64_t borrow = 0, x;
    int i, n = a->n;

    for (i = 0; i < n; i++) {
        x = (uint64_t)a->d[i] - (i < b->n ? b->d[i] : 0) - borrow;
        r->d[i] = (uint32_t)x;
        borrow = (x >> 63) & 1;
    }
    r->n = n;
    bn_trim(r);
}

/** @brief @p r = @p a + @p b; @p r may be either.  @return 0, or -1 when
 *         the sum has more limbs than a number holds. */
static int
bn_add(bn_t *r, const bn_t *a, const bn_t *b)
{
    int i, n = a->n > b->n ? a->n : b->n;
    uint64_t c = 0;

    for (i = 0; i < n; i++) {
        c += (uint64_t)(i < a->n ? a->d[i] : 0) + (i < b->n ? b->d[i] : 0);
        r->d[i] = (uint32_t)c;
        c >>= 32;
    }
    r->n = n;
    if (c != 0) {
        if (n == BN_LIMBS) {
            return -1;
        }
        r->d[r->n++] = (uint32_t)c;
    }
    return 0;
}

/** @brief @p a shifted right by @p bits. */
static void
bn_shr(bn_t *a, int bits)
{
    int limbs = bits / 32, k = bits % 32, i;

    if (limbs >= a->n) {
        a->n = 0;
        return;
    }
    for (i = 0; i < a->n - limbs; i++) {
        a->d[i] = a->d[i + limbs] >> k;
        if (k != 0 && i + limbs + 1 < a->n) {
            a->d[i] |= a->d[i + limbs + 1] << (32 - k);
        }
    }
    a->n -= limbs;
    bn_trim(a);
}

/** @brief @p r = @p a times @p b; @p r may be either.  @return 0, or -1
 *         when the product would not fit. */
static int
bn_mul(bn_t *r, const bn_t *a, const bn_t *b)
{
    uint32_t t[BN_LIMBS];
    uint64_t c;
    int i, j;

    if (a->n + b->n > BN_LIMBS) {
        return -1;
    }
    memset(t, 0, (size_t)(a->n + b->n) * sizeof t[0]);
    for (i = 0; i < a->n; i++) {
        c = 0;
        for (j = 0; j < b->n; j++) {
            c += (uint64_t)t[i + j] + (uint64_t)a->d[i] * b->d[j];
            t[i + j] = (uint32_t)c;
            c >>= 32;
        }
        t[i + b->n] = (uint32_t)c;
    }
    r->n = a->n + b->n;
    memcpy(r->d, t, (size_t)r->n * sizeof t[0]);
    bn_trim(r);
    return 0;
}

/** @brief @p r = @p a mod @p m, one bit of @p a at a time; @p r may be
 *         @p a.
 *  @note @p m must not be zero. */
static void
bn_mod(bn_t *r, const bn_t *a, const bn_t *m)
{
    bn_t x;
    uint32_t carry, top;
    int i, k;

    x.n = 0;
    for (i = bn_bits(a) - 1; i >= 0; i--) {
        carry = (uint32_t)bn_bit(a, i);
        for (k = 0; k < x.n; k++) {
            top = x.d[k] >> 31;
            x.d[k] = x.d[k] << 1 | carry;
            carry = top;
        }
        if (carry != 0) {
            x.d[x.n++] = carry;
        }
        if (bn_cmp(&x, m) >= 0) {
            bn_sub(&x, &x, m);
        }
    }
    bn_copy(r, &x);
}

/* --------------------------------------------------------- Montgomery  */

/** @brief Montgomery products modulo @p m.  @return 0, or -1 for an even
 *         or empty modulus, or one over BN_LIMBS - 4 limbs. */
static int
mont_init(mont_t *M, const bn_t *m)
{
    uint32_t x;
    int i;

    if (m->n == 0 || !(m->d[0] & 1) || m->n > BN_LIMBS - 4) {
        return -1;
    }
    bn_copy(&M->m, m);
    M->n = m->n;
    x = m->d[0];                    /* its inverse mod 8, then Newton's */
    for (i = 0; i < 5; i++) {
        x *= 2 - m->d[0] * x;
    }
    M->minv = 0u - x;
    bn_set(&M->rr, 1);              /* 1 doubled 64n times is R^2 */
    for (i = 0; i < 64 * M->n; i++) {
        (void)bn_add(&M->rr, &M->rr, &M->rr);
        if (bn_cmp(&M->rr, m) >= 0) {
            bn_sub(&M->rr, &M->rr, m);
        }
    }
    return 0;
}

/** @brief @p r = @p a @p b / R mod the modulus (CIOS), for @p a and @p b
 *         below it; @p r may be either. */
static void
mont_mul(const mont_t *M, bn_t *r, const bn_t *a, const bn_t *b)
{
    uint32_t t[BN_LIMBS + 2], u;
    const uint32_t *m = M->m.d;
    uint64_t c, ai;
    int n = M->n, i, j;

    memset(t, 0, (size_t)(n + 2) * sizeof t[0]);
    for (i = 0; i < n; i++) {
        ai = i < a->n ? a->d[i] : 0;
        c = 0;
        for (j = 0; j < n; j++) {
            c += (uint64_t)t[j] + ai * (j < b->n ? b->d[j] : 0);
            t[j] = (uint32_t)c;
            c >>= 32;
        }
        c += t[n];
        t[n] = (uint32_t)c;
        t[n + 1] = (uint32_t)(c >> 32);
        u = t[0] * M->minv;
        c = ((uint64_t)t[0] + (uint64_t)u * m[0]) >> 32;
        for (j = 1; j < n; j++) {
            c += (uint64_t)t[j] + (uint64_t)u * m[j];
            t[j - 1] = (uint32_t)c;
            c >>= 32;
        }
        c += t[n];
        t[n - 1] = (uint32_t)c;
        t[n] = t[n + 1] + (uint32_t)(c >> 32);
    }
    r->n = n + 1;
    memcpy(r->d, t, (size_t)(n + 1) * sizeof t[0]);
    bn_trim(r);
    if (bn_cmp(r, &M->m) >= 0) {    /* r < 2m, so one subtraction */
        bn_sub(r, r, &M->m);
    }
}

/** @brief @p r = @p a to the power @p e mod the modulus, @p a below it:
 *         four bits of @p e at a time.  @return 0, or -1 for no memory. */
static int
mont_exp(const mont_t *M, bn_t *r, const bn_t *a, const bn_t *e)
{
    bn_t *t = malloc(17 * sizeof *t), one;
    int i, k, w;

    if (t == NULL) {
        return -1;
    }
    bn_set(&one, 1);
    mont_mul(M, &t[0], &one, &M->rr);   /* 1, as R */
    mont_mul(M, &t[1], a, &M->rr);      /* a, as aR */
    for (i = 2; i < 16; i++) {
        mont_mul(M, &t[i], &t[i - 1], &t[1]);
    }
    bn_copy(&t[16], &t[0]);
    for (i = (bn_bits(e) + 3) / 4 * 4 - 4; i >= 0; i -= 4) {
        w = bn_bit(e, i) | bn_bit(e, i + 1) << 1 | bn_bit(e, i + 2) << 2 |
            bn_bit(e, i + 3) << 3;
        for (k = 0; k < 4; k++) {
            mont_mul(M, &t[16], &t[16], &t[16]);
        }
        if (w != 0) {
            mont_mul(M, &t[16], &t[16], &t[w]);
        }
    }
    mont_mul(M, r, &t[16], &one);       /* out of R */
    memset(t, 0, 17 * sizeof *t);       /* a private power's working values */
    free(t);
    return 0;
}

/* ---------------------------------------------------------------- RSA  */

/** @brief The key's modulus into @p n.  @return its length in bytes, or
 *         0 for one missing, even, or outside 512 to TIKU_RSA_MAX_BITS
 *         bits. */
static size_t
rsa_modulus(const tiku_rsa_key_t *key, bn_t *n)
{
    size_t k;

    if (key->n.n == 0 || bn_read(n, key->n.b, key->n.n) != 0 ||
        bn_bits(n) < 512 || bn_bits(n) > TIKU_RSA_MAX_BITS || !(n->d[0] & 1)) {
        return 0;
    }
    k = (size_t)(bn_bits(n) + 7) / 8;
    return k;
}

/** @brief @p r = @p x to the power @p e mod @p n.  @return 0, or -1. */
static int
power_mod(bn_t *r, const bn_t *x, const tiku_num_t *e, const bn_t *n)
{
    mont_t *M = malloc(sizeof *M);
    bn_t *exp = malloc(sizeof *exp);
    int ok = M != NULL && exp != NULL && e->n > 0 &&
             bn_read(exp, e->b, e->n) == 0 && mont_init(M, n) == 0 &&
             mont_exp(M, r, x, exp) == 0;

    if (exp != NULL) memset(exp, 0, sizeof *exp);
    free(M);
    free(exp);
    return ok ? 0 : -1;
}

/** @brief @p c to the private power, by the primes (RFC 8017 5.1.2 2b):
 *         m1 and m2 mod each, then h = qInv (m1 - m2) mod p, m = m2 + q h.
 *         @return 0, or -1. */
static int
rsa_crt(const tiku_rsa_key_t *k, const bn_t *c, bn_t *m)
{
    bn_t *b = malloc(6 * sizeof *b);    /* p, q, qi, m1, m2, t */
    int ok;

    if (b == NULL) {
        return -1;
    }
    ok = bn_read(&b[0], k->p.b, k->p.n) == 0 && b[0].n > 0 &&
         bn_read(&b[1], k->q.b, k->q.n) == 0 && b[1].n > 0 &&
         bn_read(&b[2], k->qi.b, k->qi.n) == 0;
    if (ok) {
        bn_mod(&b[5], c, &b[0]);
        ok = power_mod(&b[3], &b[5], &k->dp, &b[0]) == 0;
        bn_mod(&b[5], c, &b[1]);
        ok = ok && power_mod(&b[4], &b[5], &k->dq, &b[1]) == 0;
    }
    if (ok) {
        bn_mod(&b[5], &b[4], &b[0]);    /* m2 mod p, to subtract */
        if (bn_cmp(&b[3], &b[5]) < 0) {
            ok = bn_add(&b[3], &b[3], &b[0]) == 0;
        }
        bn_sub(&b[5], &b[3], &b[5]);
        ok = ok && bn_mul(&b[5], &b[5], &b[2]) == 0;
        bn_mod(&b[5], &b[5], &b[0]);
        ok = ok && bn_mul(&b[5], &b[5], &b[1]) == 0 &&
             bn_add(m, &b[5], &b[4]) == 0;
    }
    memset(b, 0, 6 * sizeof *b);
    free(b);
    return ok ? 0 : -1;
}

/** @brief The key's private power of the @p inlen bytes at @p in, as the
 *         @p k bytes of @p em.  @return 0, or -1. */
static int
rsa_private(const tiku_rsa_key_t *key, const unsigned char *in, size_t inlen,
            unsigned char *em, size_t k)
{
    bn_t *b = malloc(3 * sizeof *b);    /* n, c, m */
    int ok;

    if (b == NULL) {
        return -1;
    }
    ok = inlen <= k && rsa_modulus(key, &b[0]) == k &&
         bn_read(&b[1], in, inlen) == 0 && bn_cmp(&b[1], &b[0]) < 0;
    if (ok && key->p.n && key->q.n && key->dp.n && key->dq.n && key->qi.n) {
        ok = rsa_crt(key, &b[1], &b[2]) == 0;
    } else if (ok) {
        ok = power_mod(&b[2], &b[1], &key->d, &b[0]) == 0;
    }
    ok = ok && bn_cmp(&b[2], &b[0]) < 0 && bn_write(&b[2], em, k) == 0;
    memset(b, 0, 3 * sizeof *b);
    free(b);
    return ok ? 0 : -1;
}

/** @brief The key's public power of the signature @p sig, as the @p k
 *         bytes of @p em; a signature shorter than @p k bytes, its leading
 *         zeros left off, is read as its number.  @return 0, or -1. */
static int
rsa_public(const tiku_rsa_key_t *key, const unsigned char *sig, size_t siglen,
           unsigned char *em, size_t k)
{
    bn_t *b = malloc(3 * sizeof *b);    /* n, s, m */
    int ok;

    if (b == NULL) {
        return -1;
    }
    ok = siglen <= k && rsa_modulus(key, &b[0]) == k &&
         bn_read(&b[1], sig, siglen) == 0 && bn_cmp(&b[1], &b[0]) < 0 &&
         power_mod(&b[2], &b[1], &key->e, &b[0]) == 0 &&
         bn_write(&b[2], em, k) == 0;
    free(b);
    return ok ? 0 : -1;
}

/** @brief MGF1 (RFC 8017 B.2.1) over @p alg: @p len bytes of mask from
 *         @p seed, XORed into @p out. */
static void
mgf1_xor(int alg, const unsigned char *seed, size_t slen, unsigned char *out,
         size_t len)
{
    unsigned char h[TIKU_HASH_MAX], be[4];
    tiku_hash_t ctx;
    uint32_t counter;
    size_t hl, take, i;

    for (counter = 0; len > 0; counter++) {
        if ((hl = tiku_hash_init(&ctx, alg)) == 0) {
            return;
        }
        be[0] = (unsigned char)(counter >> 24);
        be[1] = (unsigned char)(counter >> 16);
        be[2] = (unsigned char)(counter >> 8);
        be[3] = (unsigned char)counter;
        tiku_hash_update(&ctx, seed, slen);
        tiku_hash_update(&ctx, be, 4);
        tiku_hash_final(&ctx, h);
        take = len < hl ? len : hl;
        for (i = 0; i < take; i++) {
            out[i] ^= h[i];
        }
        out += take;
        len -= take;
    }
}

/** @brief The DER DigestInfo prefix for @p alg, its NULL parameters in
 *         or out, into @p out.  @return its length, or 0. */
static size_t
digest_info(int alg, int with_null, unsigned char *out)
{
    static const unsigned char md5[] = { 0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d,
                                         0x02, 0x05 };
    static const unsigned char sha1[] = { 0x2b, 0x0e, 0x03, 0x02, 0x1a };
    static const unsigned char sha2[] = { 0x60, 0x86, 0x48, 0x01, 0x65, 0x03,
                                          0x04, 0x02, 0x00 };
    unsigned char oid[9], probe[TIKU_HASH_MAX];
    size_t olen, hl = tiku_hash(alg, "", 0, probe), at = 0;

    if (alg == TIKU_HASH_MD5) {
        memcpy(oid, md5, olen = sizeof md5);
    } else if (alg == TIKU_HASH_SHA1) {
        memcpy(oid, sha1, olen = sizeof sha1);
    } else if (hl != 0) {
        memcpy(oid, sha2, olen = sizeof sha2);
        oid[8] = (unsigned char)(alg == TIKU_HASH_SHA224 ? 4 :
                                 alg == TIKU_HASH_SHA256 ? 1 :
                                 alg == TIKU_HASH_SHA384 ? 2 : 3);
    } else {
        return 0;
    }
    out[at++] = 0x30;
    out[at++] = (unsigned char)(8 + olen + hl - (with_null ? 0 : 2));
    out[at++] = 0x30;
    out[at++] = (unsigned char)(4 + olen - (with_null ? 0 : 2));
    out[at++] = 0x06;
    out[at++] = (unsigned char)olen;
    memcpy(out + at, oid, olen);
    at += olen;
    if (with_null) {
        out[at++] = 0x05;
        out[at++] = 0x00;
    }
    out[at++] = 0x04;
    out[at++] = (unsigned char)hl;
    return at;
}

/** @brief The key's modulus length in bytes, 0 for a key this cannot
 *         use. */
static size_t
rsa_bytes(const tiku_rsa_key_t *key)
{
    bn_t *n = malloc(sizeof *n);
    size_t k = n != NULL ? rsa_modulus(key, n) : 0;

    free(n);
    return k;
}

int
tiku_rsa_verify(const tiku_rsa_key_t *key, int alg,
                const unsigned char *digest, const unsigned char *sig,
                size_t siglen)
{
    unsigned char *em = malloc(TIKU_RSA_MAX_BITS / 8), pre[32];
    size_t k = rsa_bytes(key), i = 2, pl, hl;
    int ok = 0, form;
    unsigned char probe[TIKU_HASH_MAX];

    hl = tiku_hash(alg, "", 0, probe);
    if (em == NULL || hl == 0 || k == 0 ||
        rsa_public(key, sig, siglen, em, k) != 0) {
        free(em);
        return 0;
    }
    if (em[0] == 0x00 && em[1] == 0x01) {
        while (i < k && em[i] == 0xff) i++;
        for (form = 1; form >= 0 && !ok && i >= 10 && i < k && em[i] == 0;
             form--) {
            pl = digest_info(alg, form, pre);
            ok = k - i - 1 == pl + hl && memcmp(em + i + 1, pre, pl) == 0 &&
                 memcmp(em + i + 1 + pl, digest, hl) == 0;
        }
    }
    free(em);
    return ok;
}

/**
 * @brief EMSA-PSS's encoded message @p em: @p bits bits, one fewer than
 *        the modulus has, in its @p k bytes, unmasked with MGF1 over
 *        @p mgf (RFC 8017 9.1.2 steps 3-10): its data block ending where
 *        H begins.
 * @return where the salt starts, with its length in @p salt_len and the
 *         block's end in @p db; or -1 for an encoding that is not one.
 */
static long
pss_unmask(unsigned char *em, size_t k, int bits, size_t hl, int mgf,
           size_t *db, size_t *salt_len)
{
    size_t em_len = (size_t)(bits + 7) / 8, i;

    if (em_len < k && em[0] != 0) return -1;
    if (em_len < k) memmove(em, em + 1, em_len);
    if (em_len < hl + 2 || em[em_len - 1] != 0xbc ||
        (em[0] >> (8 - (8 * em_len - (size_t)bits))) != 0) return -1;
    *db = em_len - hl - 1;
    mgf1_xor(mgf, em + *db, hl, em, *db);
    em[0] &= (unsigned char)(0xff >> (8 * em_len - (size_t)bits));
    for (i = 0; i < *db && em[i] == 0; i++) {}
    if (i == *db || em[i] != 0x01) return -1;
    *salt_len = *db - i - 1;
    return (long)(i + 1);
}

int
tiku_rsa_verify_pss(const tiku_rsa_key_t *key, int alg, int mgf, long salt,
                    const unsigned char *digest, const unsigned char *sig,
                    size_t siglen)
{
    unsigned char *em = malloc(TIKU_RSA_MAX_BITS / 8), h[TIKU_HASH_MAX];
    unsigned char zero[8] = { 0 };
    size_t k, hl = tiku_hash(alg, "", 0, h), db, sl;
    bn_t *n = malloc(sizeof *n);
    tiku_hash_t ctx;
    long at = -1;

    if (em != NULL && n != NULL && hl != 0 && (k = rsa_modulus(key, n)) &&
        rsa_public(key, sig, siglen, em, k) == 0) {
        at = pss_unmask(em, k, bn_bits(n) - 1, hl, mgf, &db, &sl);
    }
    if (at >= 0 && (salt < 0 || sl == (size_t)salt)) {
        (void)tiku_hash_init(&ctx, alg);
        tiku_hash_update(&ctx, zero, 8);
        tiku_hash_update(&ctx, digest, hl);
        tiku_hash_update(&ctx, em + at, sl);
        tiku_hash_final(&ctx, h);
    }
    at = at >= 0 && (salt < 0 || sl == (size_t)salt) &&
         memcmp(h, em + db, hl) == 0;
    free(em);
    free(n);
    return (int)at;
}

long
tiku_rsa_decrypt(const tiku_rsa_key_t *key, int oaep, int mgf,
                 const unsigned char *in, size_t inlen, unsigned char *out,
                 size_t cap)
{
    unsigned char *em = malloc(TIKU_RSA_MAX_BITS / 8), lh[TIKU_HASH_MAX];
    size_t k = rsa_bytes(key), i, hl = oaep ? tiku_hash(oaep, "", 0, lh) : 0;
    long got = -1;

    if (em == NULL || k == 0 || (oaep && hl == 0) ||
        rsa_private(key, in, inlen, em, k) != 0) {
        free(em);
        return -1;
    }
    if (!oaep && em[0] == 0 && em[1] == 2) {
        for (i = 2; i < k && em[i] != 0; i++) {}
        if (i < k && i >= 10) got = (long)(k - i - 1);
        i++;
    } else if (oaep && k >= 2 * hl + 2 && em[0] == 0) {
        mgf1_xor(mgf, em + 1 + hl, k - hl - 1, em + 1, hl);
        mgf1_xor(mgf, em + 1, hl, em + 1 + hl, k - hl - 1);
        for (i = 1 + 2 * hl; i < k && em[i] == 0; i++) {}
        if (memcmp(em + 1 + hl, lh, hl) == 0 && i < k && em[i] == 1)
            got = (long)(k - ++i);
    }
    if (got >= 0 && (size_t)got <= cap) {
        memcpy(out, em + k - (size_t)got, (size_t)got);
    } else {
        got = -1;
    }
    memset(em, 0, k);
    free(em);
    return got;
}

/* -------------------------------------------------------------- ECDSA  */

/** @brief A curve's numbers in hex: the field's prime, a and b, the base
 *         point and the group's order. */
static const struct {
    const char *p, *a, *b, *gx, *gy, *n;
} curve_hex[] = {
    { "ffffffff00000001000000000000000000000000ffffffffffffffffffffffff",
      "ffffffff00000001000000000000000000000000fffffffffffffffffffffffc",
      "5ac635d8aa3a93e7b3ebbd55769886bc651d06b0cc53b0f63bce3c3e27d2604b",
      "6b17d1f2e12c4247f8bce6e563a440f277037d812deb33a0f4a13945d898c296",
      "4fe342e2fe1a7f9b8ee7eb4a7c0f9e162bce33576b315ececbb6406837bf51f5",
      "ffffffff00000000ffffffffffffffffbce6faada7179e84f3b9cac2fc632551" },
    { "fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffe"
      "ffffffff0000000000000000ffffffff",
      "fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffe"
      "ffffffff0000000000000000fffffffc",
      "b3312fa7e23ee7e4988e056be3f82d19181d9c6efe8141120314088f5013875a"
      "c656398d8a2ed19d2a85c8edd3ec2aef",
      "aa87ca22be8b05378eb1c71ef320ad746e1d3b628ba79b9859f741e082542a38"
      "5502f25dbf55296c3a545e3872760ab7",
      "3617de4a96262c6f5d9e98bf9292dc29f8f41dbd289a147ce9da3113b5f0b8c0"
      "0a60b1ce1d7e819d7a431d7c90ea0e5f",
      "ffffffffffffffffffffffffffffffffffffffffffffffffc7634d81f4372ddf"
      "581a0db248b0a77aecec196accc52973" },
    { "01ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
      "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
      "ffff",
      "01ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
      "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
      "fffc",
      "0051953eb9618e1c9a1f929a21a0b68540eea2da725b99b315f3b8b489918ef1"
      "09e156193951ec7e937b1652c0bd3bb1bf073573df883d2c34f1ef451fd46b50"
      "3f00",
      "00c6858e06b70404e9cd9e3ecb662395b4429c648139053fb521f828af606b4d"
      "3dbaa14b5e77efe75928fe1dc127a2ffa8de3348b3c1856a429bf97e7e31c2e5"
      "bd66",
      "011839296a789a3bc0045c8a5fb42c7d1bd998f54449579b446817afbd17273e"
      "662c97ee72995ef42640c550b9013fad0761353c7086a272c24088be94769fd1"
      "6650",
      "01ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
      "fffa51868783bf2f966b7fcc0148f709a5d03bb5c9b8899c47aebb6fb71e9138"
      "6409" },
    { "a9fb57dba1eea9bc3e660a909d838d726e3bf623d52620282013481d1f6e5377",
      "7d5a0975fc2c3057eef67530417affe7fb8055c126dc5c6ce94a4b44f330b5d9",
      "26dc5c6ce94a4b44f330b5d9bbd77cbf958416295cf7e1ce6bccdc18ff8c07b6",
      "8bd2aeb9cb7e57cb2c4b482ffc81b7afb9de27e1e3bd23c23a4453bd9ace3262",
      "547ef835c3dac4fd97f8461a14611dc9c27745132ded8e545c1d54c72f046997",
      "a9fb57dba1eea9bc3e660a909d838d718c397aa3b561a6f7901e0e82974856a7" },
    { "8cb91e82a3386d280f5d6f7e50e641df152f7109ed5456b412b1da197fb71123"
      "acd3a729901d1a71874700133107ec53",
      "7bc382c63d8c150c3c72080ace05afa0c2bea28e4fb22787139165efba91f90f"
      "8aa5814a503ad4eb04a8c7dd22ce2826",
      "04a8c7dd22ce28268b39b55416f0447c2fb77de107dcd2a62e880ea53eeb62d5"
      "7cb4390295dbc9943ab78696fa504c11",
      "1d1c64f068cf45ffa2a63a81b7c13f6b8847a3e77ef14fe3db7fcafe0cbd10e8"
      "e826e03436d646aaef87b2e247d4af1e",
      "8abe1d7520f9c2a45cb1eb8e95cfd55262b70b29feec5864e19c054ff9912928"
      "0e4646217791811142820341263c5315",
      "8cb91e82a3386d280f5d6f7e50e641df152f7109ed5456b31f166e6cac0425a7"
      "cf3ab6af6b7fc3103b883202e9046565" },
    { "aadd9db8dbe9c48b3fd4e6ae33c9fc07cb308db3b3c9d20ed6639cca70330871"
      "7d4d9b009bc66842aecda12ae6a380e62881ff2f2d82c68528aa6056583a48f3",
      "7830a3318b603b89e2327145ac234cc594cbdd8d3df91610a83441caea9863bc"
      "2ded5d5aa8253aa10a2ef1c98b9ac8b57f1117a72bf2c7b9e7c1ac4d77fc94ca",
      "3df91610a83441caea9863bc2ded5d5aa8253aa10a2ef1c98b9ac8b57f1117a7"
      "2bf2c7b9e7c1ac4d77fc94cadc083e67984050b75ebae5dd2809bd638016f723",
      "81aee4bdd82ed9645a21322e9c4c6a9385ed9f70b5d916c1b43b62eef4d0098e"
      "ff3b1f78e2d0d48d50d1687b93b97d5f7c6d5047406a5e688b352209bcb9f822",
      "7dde385d566332ecc0eabfa9cf7822fdf209f70024a57b1aa000c55b881f8111"
      "b2dcde494a5f485e5bca4bd88a2763aed1ca2b2fa8f0540678cd1e0f3ad80892",
      "aadd9db8dbe9c48b3fd4e6ae33c9fc07cb308db3b3c9d20ed6639cca70330870"
      "553e5c414ca92619418661197fac10471db1d381085ddaddb58796829ca90069" },
};

/** @brief A point in Jacobian coordinates, Montgomery form; z of 0 is the
 *         point at infinity. */
typedef struct {
    bn_t x, y, z;
} pt_t;

/** @brief A curve made ready: its field's products, a and b in them. */
typedef struct {
    mont_t f;                       /* the field                         */
    bn_t   a, b, n;                 /* a, b as aR, bR; the group order   */
    int    bytes;                   /* a field element's bytes           */
    pt_t   g, q, acc, sum;          /* G, the key, a running sum, G + Q  */
    bn_t   t[14];                   /* the formulas' working numbers     */
} ec_t;

/** @brief @p r = @p a + @p b in the field. */
static void
f_add(const ec_t *c, bn_t *r, const bn_t *a, const bn_t *b)
{
    (void)bn_add(r, a, b);
    if (bn_cmp(r, &c->f.m) >= 0) {
        bn_sub(r, r, &c->f.m);
    }
}

/** @brief @p r = @p a - @p b in the field. */
static void
f_sub(const ec_t *c, bn_t *r, const bn_t *a, const bn_t *b)
{
    bn_t t;

    if (bn_cmp(a, b) >= 0) {
        bn_sub(r, a, b);
    } else {
        (void)bn_add(&t, a, &c->f.m);
        bn_sub(r, &t, b);
    }
}

/** @brief @p r = @p a @p b in the field (both in Montgomery form). */
static void
f_mul(const ec_t *c, bn_t *r, const bn_t *a, const bn_t *b)
{
    mont_mul(&c->f, r, a, b);
}

/** @brief @p r = 2 @p p (dbl-2007-bl, any a); @p r may be @p p. */
static void
pt_double(ec_t *c, pt_t *r, const pt_t *p)
{
    bn_t *xx = &c->t[0], *yy = &c->t[1], *y4 = &c->t[2], *zz = &c->t[3];
    bn_t *s = &c->t[4], *m = &c->t[5], *t = &c->t[6], *z = &c->t[7];

    if (p->z.n == 0 || p->y.n == 0) {
        r->z.n = 0;
        return;
    }
    f_mul(c, xx, &p->x, &p->x);
    f_mul(c, yy, &p->y, &p->y);
    f_mul(c, y4, yy, yy);
    f_mul(c, zz, &p->z, &p->z);
    f_add(c, t, &p->x, yy);             /* S = 2((X + YY)^2 - XX - YYYY) */
    f_mul(c, t, t, t);
    f_sub(c, t, t, xx);
    f_sub(c, t, t, y4);
    f_add(c, s, t, t);
    f_mul(c, t, zz, zz);                /* M = 3 XX + a ZZ^2 */
    f_mul(c, t, t, &c->a);
    f_add(c, m, xx, xx);
    f_add(c, m, m, xx);
    f_add(c, m, m, t);
    f_add(c, z, &p->y, &p->z);          /* Z3 = (Y + Z)^2 - YY - ZZ */
    f_mul(c, z, z, z);
    f_sub(c, z, z, yy);
    f_sub(c, z, z, zz);
    f_mul(c, t, m, m);                  /* X3 = M^2 - 2 S */
    f_sub(c, t, t, s);
    f_sub(c, t, t, s);
    bn_copy(&r->x, t);
    f_sub(c, s, s, t);                  /* Y3 = M (S - X3) - 8 YYYY */
    f_mul(c, s, m, s);
    f_add(c, y4, y4, y4);
    f_add(c, y4, y4, y4);
    f_add(c, y4, y4, y4);
    f_sub(c, &r->y, s, y4);
    bn_copy(&r->z, z);
}

/** @brief @p r = @p p + @p q (add-2007-bl); @p r may be either. */
static void
pt_add(ec_t *c, pt_t *r, const pt_t *p, const pt_t *q)
{
    bn_t *z1 = &c->t[8], *z2 = &c->t[9], *u1 = &c->t[10], *u2 = &c->t[11];
    bn_t *s1 = &c->t[12], *s2 = &c->t[13], *h = &c->t[0], *w = &c->t[1];

    if (p->z.n == 0 || q->z.n == 0) {
        const pt_t *other = p->z.n == 0 ? q : p;
        if (r != other) *r = *other;
        return;
    }
    f_mul(c, z1, &p->z, &p->z);
    f_mul(c, z2, &q->z, &q->z);
    f_mul(c, u1, &p->x, z2);
    f_mul(c, u2, &q->x, z1);
    f_mul(c, s1, &p->y, &q->z);
    f_mul(c, s1, s1, z2);
    f_mul(c, s2, &q->y, &p->z);
    f_mul(c, s2, s2, z1);
    f_sub(c, h, u2, u1);
    f_sub(c, w, s2, s1);
    if (h->n == 0) {
        if (w->n == 0) {
            pt_double(c, r, p);         /* the same point: its double */
        } else {
            r->z.n = 0;                 /* a point and its negative */
        }
        return;
    }
    f_add(c, w, w, w);                  /* r = 2 (S2 - S1) */
    f_add(c, u2, &p->z, &q->z);         /* Z3 = ((Z1 + Z2)^2 - Z1Z1 - Z2Z2) H */
    f_mul(c, u2, u2, u2);
    f_sub(c, u2, u2, z1);
    f_sub(c, u2, u2, z2);
    f_mul(c, &r->z, u2, h);
    f_add(c, z1, h, h);                 /* I = (2H)^2, J = H I, V = U1 I */
    f_mul(c, z1, z1, z1);
    f_mul(c, z2, h, z1);
    f_mul(c, u1, u1, z1);
    f_mul(c, s2, w, w);                 /* X3 = r^2 - J - 2V */
    f_sub(c, s2, s2, z2);
    f_sub(c, s2, s2, u1);
    f_sub(c, s2, s2, u1);
    bn_copy(&r->x, s2);
    f_sub(c, u1, u1, s2);               /* Y3 = r (V - X3) - 2 S1 J */
    f_mul(c, u1, w, u1);
    f_mul(c, s1, s1, z2);
    f_add(c, s1, s1, s1);
    f_sub(c, &r->y, u1, s1);
}

/** @brief @p c's sum, u1 G + u2 Q, one bit of each at a time (Shamir's
 *         trick), into c->acc. */
static void
pt_twin(ec_t *c, const bn_t *u1, const bn_t *u2)
{
    int i, bits = bn_bits(u1) > bn_bits(u2) ? bn_bits(u1) : bn_bits(u2);

    pt_add(c, &c->sum, &c->g, &c->q);
    c->acc.z.n = 0;
    for (i = bits - 1; i >= 0; i--) {
        pt_double(c, &c->acc, &c->acc);
        if (bn_bit(u1, i) && bn_bit(u2, i)) {
            pt_add(c, &c->acc, &c->acc, &c->sum);
        } else if (bn_bit(u1, i)) {
            pt_add(c, &c->acc, &c->acc, &c->g);
        } else if (bn_bit(u2, i)) {
            pt_add(c, &c->acc, &c->acc, &c->q);
        }
    }
}

/** @brief @p x^3 + a @p x + b, all in Montgomery form, into @p r. */
static void
curve_rhs(ec_t *c, bn_t *r, const bn_t *x)
{
    bn_t *t = &c->t[2];

    f_mul(c, r, x, x);
    f_mul(c, r, r, x);
    f_mul(c, t, &c->a, x);
    f_add(c, r, r, t);
    f_add(c, r, r, &c->b);
}

/** @brief The point @p q, SEC 1 encoded, into c->q, in Montgomery form
 *         and checked to be on the curve: a compressed one's y is the root
 *         rhs^((p + 1) / 4), every prime here being 3 mod 4.
 *         @return 0, or -1. */
static int
ec_point(ec_t *c, const unsigned char *q, size_t qlen)
{
    bn_t *rhs = &c->t[0], *x = &c->t[1], *e = &c->t[3];
    size_t k = (size_t)c->bytes;

    if (qlen == 1 + 2 * k && q[0] == 4) {
        if (bn_read(x, q + 1, k) || bn_read(&c->q.y, q + 1 + k, k) ||
            bn_cmp(x, &c->f.m) >= 0 || bn_cmp(&c->q.y, &c->f.m) >= 0)
            return -1;
        mont_mul(&c->f, &c->q.x, x, &c->f.rr);
        mont_mul(&c->f, &c->q.y, &c->q.y, &c->f.rr);
    } else if (qlen == 1 + k && (q[0] == 2 || q[0] == 3)) {
        if (bn_read(x, q + 1, k) || bn_cmp(x, &c->f.m) >= 0) return -1;
        mont_mul(&c->f, &c->q.x, x, &c->f.rr);
        curve_rhs(c, rhs, &c->q.x);
        bn_set(e, 1);
        (void)bn_add(e, &c->f.m, e);
        bn_shr(e, 2);
        bn_set(x, 1);
        mont_mul(&c->f, x, rhs, x);     /* rhs out of Montgomery form */
        if (mont_exp(&c->f, &c->q.y, x, e) != 0) return -1;
        if ((c->q.y.n ? c->q.y.d[0] & 1 : 0) != (uint32_t)(q[0] & 1)) {
            if (c->q.y.n == 0) return -1;
            bn_sub(&c->q.y, &c->f.m, &c->q.y);
        }
        mont_mul(&c->f, &c->q.y, &c->q.y, &c->f.rr);
    } else {
        return -1;
    }
    bn_set(x, 1);
    mont_mul(&c->f, &c->q.z, x, &c->f.rr);
    curve_rhs(c, rhs, &c->q.x);
    f_mul(c, x, &c->q.y, &c->q.y);
    return bn_cmp(x, rhs) == 0 ? 0 : -1;
}

/** @brief Curve @p id made ready, its base point in c->g.  @return 0,
 *         or -1 for no such curve. */
static int
ec_ready(ec_t *c, int id)
{
    bn_t *t = &c->t[0];

    if (id < TIKU_EC_P256 || id > TIKU_EC_BP512) {
        return -1;
    }
    id--;
    bn_hex(t, curve_hex[id].p);
    if (mont_init(&c->f, t) != 0) {
        return -1;
    }
    c->bytes = (bn_bits(t) + 7) / 8;
    bn_hex(t, curve_hex[id].a);
    mont_mul(&c->f, &c->a, t, &c->f.rr);
    bn_hex(t, curve_hex[id].b);
    mont_mul(&c->f, &c->b, t, &c->f.rr);
    bn_hex(&c->n, curve_hex[id].n);
    bn_hex(t, curve_hex[id].gx);
    mont_mul(&c->f, &c->g.x, t, &c->f.rr);
    bn_hex(t, curve_hex[id].gy);
    mont_mul(&c->f, &c->g.y, t, &c->f.rr);
    bn_set(t, 1);
    mont_mul(&c->f, &c->g.z, t, &c->f.rr);
    return 0;
}

/** @brief @p r = @p a @p b mod the group order.  @return 0, or -1 when
 *         the product would not fit. */
static int
order_mul(ec_t *c, bn_t *r, const bn_t *a, const bn_t *b)
{
    if (bn_mul(r, a, b) != 0) {
        return -1;
    }
    bn_mod(r, r, &c->n);
    return 0;
}

int
tiku_ecdsa_verify(int curve, const unsigned char *q, size_t qlen,
                  const unsigned char *digest, size_t dlen,
                  const tiku_num_t *r, const tiku_num_t *s)
{
    ec_t *c = malloc(sizeof *c);
    bn_t *b = malloc(7 * sizeof *b);    /* r, s, e, w, u1, u2, a small one */
    mont_t *order = malloc(sizeof *order);
    int ok = 0, shift;

    if (c == NULL || b == NULL || order == NULL || ec_ready(c, curve) != 0 ||
        ec_point(c, q, qlen) != 0 || bn_read(&b[0], r->b, r->n) != 0 ||
        bn_read(&b[1], s->b, s->n) != 0 || b[0].n == 0 || b[1].n == 0 ||
        bn_cmp(&b[0], &c->n) >= 0 || bn_cmp(&b[1], &c->n) >= 0 ||
        dlen > 128 || bn_read(&b[2], digest, dlen) != 0 ||
        mont_init(order, &c->n) != 0) {
        goto done;
    }
    shift = (int)dlen * 8 - bn_bits(&c->n);  /* e: as many top bits as n has */
    if (shift > 0) {
        bn_shr(&b[2], shift);
    }
    bn_set(&b[6], 2);                   /* w = s^(n - 2) mod n */
    bn_sub(&b[4], &c->n, &b[6]);
    if (mont_exp(order, &b[3], &b[1], &b[4]) != 0 ||
        order_mul(c, &b[4], &b[2], &b[3]) != 0 ||
        order_mul(c, &b[5], &b[0], &b[3]) != 0) {
        goto done;
    }
    pt_twin(c, &b[4], &b[5]);
    if (c->acc.z.n == 0) {
        goto done;
    }
    bn_set(&b[6], 1);                   /* x = X / Z^2, plain, mod n */
    mont_mul(&c->f, &b[1], &c->acc.z, &b[6]);
    bn_set(&b[6], 2);
    bn_sub(&b[2], &c->f.m, &b[6]);
    if (mont_exp(&c->f, &b[3], &b[1], &b[2]) != 0) {
        goto done;
    }
    mont_mul(&c->f, &b[3], &b[3], &c->f.rr);
    f_mul(c, &b[4], &b[3], &b[3]);
    f_mul(c, &b[4], &c->acc.x, &b[4]);
    bn_set(&b[6], 1);
    mont_mul(&c->f, &b[4], &b[4], &b[6]);
    bn_mod(&b[4], &b[4], &c->n);
    ok = bn_cmp(&b[4], &b[0]) == 0;
done:
    free(c);
    free(b);
    free(order);
    return ok;
}
