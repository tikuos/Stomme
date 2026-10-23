/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_keyfile.c - key files read: PKCS#12 (RFC 7292), its MAC checked
 * and its bags walked, each shrouded one opened by PKCS#12's own password
 * schemes or by PBES2 (RFC 8018); and PKCS#8 and PKCS#1, as DER or PEM.
 * The key is kept with the certificate whose public key is its own.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_cms_private.h"
#include "tiku_crypto.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define KEY_DEPTH  4                /* bags of safe contents in others  */
#define KEY_ROUNDS 10000000L        /* the most hashing a file may ask  */
#define KEY_CERTS  16               /* certificates a file may carry    */

#define OID_DATA     "1.2.840.113549.1.7.1"
#define OID_ENC_DATA "1.2.840.113549.1.7.6"
#define OID_KEY_BAG  "1.2.840.113549.1.12.10.1.1"
#define OID_SHROUDED "1.2.840.113549.1.12.10.1.2"
#define OID_CERT_BAG "1.2.840.113549.1.12.10.1.3"
#define OID_SAFE_BAG "1.2.840.113549.1.12.10.1.6"
#define OID_X509     "1.2.840.113549.1.9.22.1"
#define OID_PBES2    "1.2.840.113549.1.5.13"
#define OID_PBKDF2   "1.2.840.113549.1.5.12"
#define OID_RSA      "1.2.840.113549.1.1.1"
#define OID_RSA_PSS  "1.2.840.113549.1.1.10"
#define OID_EC_KEY   "1.2.840.10045.2.1"

/** @brief What reading a file gathers. */
typedef struct {
    const char    *password;
    int            none;            /* an empty password is no bytes     */
    unsigned char *key;             /* the first private key, owned      */
    size_t         key_len;
    int            rsa1;            /* it is PKCS#1, not PKCS#8          */
    unsigned char *cert[KEY_CERTS]; /* certificates' DER, owned          */
    size_t         cert_len[KEY_CERTS];
    int            ncert;
    int            wrong;           /* a password opened nothing         */
    int            unsupported;     /* a scheme this does not read       */
} gather_t;

/* ---------------------------------------------------------- passwords  */

/** @brief @p utf8 as PKCS#12's password: UTF-16 big-endian and two zero
 *         bytes, or with @p none nothing at all (what some writers mean by
 *         an empty one).  @return its length with @p out to free(), or -1. */
static long
bmp_password(const char *utf8, int none, unsigned char **out)
{
    const unsigned char *s = (const unsigned char *)utf8;
    size_t n = strlen(utf8), at = 0, i = 0, more;
    unsigned long cp;
    unsigned char *b = malloc(4 * n + 2);

    if ((*out = b) == NULL) {
        return -1;
    }
    while (!none && i < n) {
        cp = s[i++];
        more = cp >= 0xf0 ? 3 : cp >= 0xe0 ? 2 : cp >= 0xc0 ? 1 : 0;
        cp &= 0x7fu >> more;
        for (; more > 0 && i < n && (s[i] & 0xc0) == 0x80; more--) {
            cp = cp << 6 | (s[i++] & 63);
        }
        if (cp >= 0x10000) {        /* a surrogate pair */
            cp -= 0x10000;
            b[at++] = (unsigned char)(0xd8 | cp >> 18);
            b[at++] = (unsigned char)(cp >> 10);
            cp = 0xdc00 | (cp & 0x3ff);
        }
        b[at++] = (unsigned char)(cp >> 8);
        b[at++] = (unsigned char)cp;
    }
    if (!none) {
        b[at++] = 0;
        b[at++] = 0;
    }
    return (long)at;
}

/** @brief The block digest @p alg works in, as PKCS#12's derivation
 *         takes it. */
static size_t
block_of(int alg)
{
    return alg == TIKU_HASH_SHA384 || alg == TIKU_HASH_SHA512 ? 128 : 64;
}

/** @brief PKCS#12's I: the salt and then the password, each repeated
 *         to whole blocks of @p v.  @return it to free(), @p len long. */
static unsigned char *
p12_input(const unsigned char *pw, size_t pwlen, const unsigned char *salt,
          size_t slen, size_t v, size_t *len)
{
    size_t sl = (slen + v - 1) / v * v, pl = (pwlen + v - 1) / v * v, i;
    unsigned char *in = malloc(sl + pl + 1);

    if (in != NULL) {
        for (i = 0; i < sl; i++) in[i] = salt[i % slen];
        for (i = 0; i < pl; i++) in[sl + i] = pw[i % pwlen];
    }
    *len = sl + pl;
    return in;
}

/** @brief Each @p v-byte block of I, @p len bytes, made I + B + 1, B being
 *         the @p u bytes of @p a repeated. */
static void
p12_bump(unsigned char *in, size_t len, size_t v, const unsigned char *a,
         size_t u)
{
    size_t i, j;

    for (j = 0; j < len; j += v) {
        unsigned carry = 1;

        for (i = v; i-- > 0;) {
            carry += (unsigned)in[j + i] + a[i % u];
            in[j + i] = (unsigned char)carry;
            carry >>= 8;
        }
    }
}

/**
 * @brief PKCS#12's own derivation (RFC 7292 B.2): @p outlen bytes for
 *        purpose @p id (1 a key, 2 an IV, 3 a MAC's key) from the password
 *        @p pw and @p salt, digested @p rounds times by @p alg.
 * @return 0, or -1.
 */
static int
p12_kdf(int alg, const unsigned char *pw, size_t pwlen,
        const unsigned char *salt, size_t slen, long rounds, int id,
        unsigned char *out, size_t outlen)
{
    unsigned char d[128], a[TIKU_HASH_MAX], *in;
    size_t u, v = block_of(alg), len, take;
    tiku_hash_t h;

    if ((u = tiku_hash_init(&h, alg)) == 0 || rounds < 1 ||
        rounds > KEY_ROUNDS ||
        (in = p12_input(pw, pwlen, salt, slen, v, &len)) == NULL) {
        return -1;
    }
    memset(d, id, v);
    for (;;) {
        (void)tiku_hash_init(&h, alg);
        tiku_hash_update(&h, d, v);
        tiku_hash_update(&h, in, len);
        tiku_hash_final(&h, a);
        for (long r = 1; r < rounds; r++) (void)tiku_hash(alg, a, u, a);
        take = outlen < u ? outlen : u;
        memcpy(out, a, take);
        out += take;
        if ((outlen -= take) == 0) break;
        p12_bump(in, len, v, a, u);
    }
    memset(in, 0, len);
    free(in);
    return 0;
}

/**
 * @brief PKCS#12's password scheme @p which (1 to 6: RC4 128 and 40 bits,
 *        three and two-key triple DES, RC2 128 and 40 bits), with its
 *        parameters @p params and @p g's password, opening @p data in
 *        place.
 * @return the plaintext's length, -1 for a password that does not open
 *         it, -2 for parameters that are not.  With RC4 a wrong password
 *         returns @p n bytes too: there is no padding to check.
 */
static long
p12_open(int which, const tiku_der_t *params, const gather_t *g,
         unsigned char *data, size_t n)
{
    static const int kind[] = { 0, CMS_RC4, CMS_RC4, CMS_DES3, CMS_DES3,
                                CMS_RC2, CMS_RC2 };
    static const size_t klen[] = { 0, 16, 5, 24, 16, 16, 5 };
    unsigned char key[24], iv[8], *pw;
    tiku_der_cursor_t c;
    tiku_der_t salt, iter;
    long rounds, got, pwlen;

    tiku_der_enter(params, &c);
    if (tiku_der_take(&c, TIKU_DER_OCTETS, &salt) != 0 ||
        tiku_der_take(&c, TIKU_DER_INTEGER, &iter) != 0 ||
        tiku_der_int(&iter, &rounds) != 0 || salt.len == 0 ||
        (pwlen = bmp_password(g->password, g->none, &pw)) < 0) {
        return -2;
    }
    got = p12_kdf(TIKU_HASH_SHA1, pw, (size_t)pwlen, salt.body, salt.len,
                  rounds, 1, key, klen[which]) != 0 ||
          p12_kdf(TIKU_HASH_SHA1, pw, (size_t)pwlen, salt.body, salt.len,
                  rounds, 2, iv, 8) != 0 ? -2 :
          cms_cbc(kind[which], key, klen[which], (int)klen[which] * 8, iv,
                  data, n);
    memset(key, 0, sizeof key);
    memset(pw, 0, (size_t)pwlen);
    free(pw);
    return got;
}

/** @brief The HMAC digest PBKDF2's @p prf (an AlgorithmIdentifier) names:
 *         SHA-1 when it names none.  @return TIKU_HASH_*, or 0. */
static int
prf_of(const tiku_der_t *prf)
{
    static const char *const names[] = { "1.2.840.113549.2.7",
        "1.2.840.113549.2.8", "1.2.840.113549.2.9", "1.2.840.113549.2.10",
        "1.2.840.113549.2.11" };
    static const int alg[] = { TIKU_HASH_SHA1, TIKU_HASH_SHA224,
        TIKU_HASH_SHA256, TIKU_HASH_SHA384, TIKU_HASH_SHA512 };
    tiku_der_cursor_t c;
    tiku_der_t oid;
    int i;

    if (!tiku_der_is(prf, TIKU_DER_SEQUENCE)) {
        return TIKU_HASH_SHA1;
    }
    tiku_der_enter(prf, &c);
    if (tiku_der_take(&c, TIKU_DER_OID, &oid) != 0 ||
        (i = tiku_der_oid_in(&oid, names, 5)) < 0) {
        return 0;
    }
    return alg[i];
}

/** @brief PBKDF2's parameters (RFC 8018 A.2) from @p kdf, its
 *         AlgorithmIdentifier: salt, rounds, the key's length (0 when not
 *         given) and the HMAC's digest.  @return 0, or -1. */
static int
pbkdf2_params(const tiku_der_t *kdf, tiku_der_t *salt, long *rounds,
              long *klen, int *prf)
{
    tiku_der_cursor_t k;
    tiku_der_t e;

    tiku_der_enter(kdf, &k);
    if (tiku_der_take(&k, TIKU_DER_OID, &e) != 0 ||
        !tiku_der_oid(&e, OID_PBKDF2) ||
        tiku_der_take(&k, TIKU_DER_SEQUENCE, &e) != 0) return -1;
    tiku_der_enter(&e, &k);
    if (tiku_der_take(&k, TIKU_DER_OCTETS, salt) != 0 ||
        tiku_der_take(&k, TIKU_DER_INTEGER, &e) != 0 ||
        tiku_der_int(&e, rounds) != 0 || *rounds < 1 ||
        *rounds > KEY_ROUNDS) return -1;
    *klen = 0;
    if (tiku_der_next(&k, &e) == 0 && tiku_der_is(&e, TIKU_DER_INTEGER)) {
        (void)tiku_der_int(&e, klen);
        (void)tiku_der_next(&k, &e);
    }
    return (*prf = prf_of(&e)) != 0 ? 0 : -1;
}

/** @brief PBES2 (RFC 8018 6.2) with its parameters @p params: PBKDF2's
 *         key, and the cipher named, opening @p data in place.
 *         @return the plaintext's length, -1 for a wrong password, -2 for
 *         a scheme this does not read. */
static long
pbes2_open(const tiku_der_t *params, const char *password,
           unsigned char *data, size_t n)
{
    unsigned char key[32];
    tiku_der_cursor_t c;
    tiku_der_t kdf, enc, salt;
    long rounds, klen, got;
    int prf;

    tiku_der_enter(params, &c);
    if (tiku_der_take(&c, TIKU_DER_SEQUENCE, &kdf) != 0 ||
        tiku_der_take(&c, TIKU_DER_SEQUENCE, &enc) != 0 ||
        pbkdf2_params(&kdf, &salt, &rounds, &klen, &prf) != 0) return -2;
    if (klen == 0) klen = (long)cms_cipher_key_len(&enc);
    if (klen == 0) klen = 16;       /* RC2 with no length given */
    if (klen > (long)sizeof key ||
        tiku_pbkdf2(prf, password, strlen(password), salt.body, salt.len,
                    (unsigned long)rounds, key, (size_t)klen) != 0) return -2;
    got = cms_decipher(&enc, key, (size_t)klen, data, n);
    memset(key, 0, sizeof key);
    return got;
}

/**
 * @brief @p n bytes at @p in, sealed by the password scheme the
 *        AlgorithmIdentifier @p alg names, opened with @p g's password.
 * @return the plaintext's length with @p out to free(); -1 for a password
 *         that does not open it; -2 for a scheme this does not read.
 */
static long
pbe_open(const tiku_der_t *alg, const gather_t *g, const unsigned char *in,
         size_t n, unsigned char **out)
{
    static const char *const p12[] = { NULL, "1.2.840.113549.1.12.1.1",
        "1.2.840.113549.1.12.1.2", "1.2.840.113549.1.12.1.3",
        "1.2.840.113549.1.12.1.4", "1.2.840.113549.1.12.1.5",
        "1.2.840.113549.1.12.1.6" };
    tiku_der_cursor_t c;
    tiku_der_t oid, params;
    long got = -2;
    int which;

    *out = NULL;
    tiku_der_enter(alg, &c);
    if (tiku_der_take(&c, TIKU_DER_OID, &oid) != 0 ||
        tiku_der_take(&c, TIKU_DER_SEQUENCE, &params) != 0 ||
        (*out = malloc(n + 1)) == NULL) {
        return -2;
    }
    memcpy(*out, in, n);
    if ((which = tiku_der_oid_in(&oid, p12, 7)) > 0) {
        got = p12_open(which, &params, g, *out, n);
    } else if (tiku_der_oid(&oid, OID_PBES2)) {
        got = pbes2_open(&params, g->password, *out, n);
    }
    if (got < 0) {
        free(*out);
        *out = NULL;
    }
    return got;
}

/* ------------------------------------------------------------ gathering  */

/** @brief Keep a copy of @p n bytes at @p p as @p g's key, the first one
 *         found; @p rsa1 when it is PKCS#1. */
static void
keep_key(gather_t *g, const unsigned char *p, size_t n, int rsa1)
{
    if (g->key != NULL || (g->key = malloc(n)) == NULL) {
        return;
    }
    memcpy(g->key, p, n);
    g->key_len = n;
    g->rsa1 = rsa1;
}

/** @brief Keep a copy of certificate @p p, @p n bytes, in @p g; one past
 *         KEY_CERTS is dropped. */
static void
keep_cert(gather_t *g, const unsigned char *p, size_t n)
{
    if (g->ncert == KEY_CERTS || (g->cert[g->ncert] = malloc(n)) == NULL) {
        return;
    }
    memcpy(g->cert[g->ncert], p, n);
    g->cert_len[g->ncert++] = n;
}

/** @brief Whether decrypted bytes contain a complete PrivateKeyInfo. */
static int
private_key_info(const unsigned char *p, size_t n)
{
    tiku_der_t whole, e;
    tiku_der_cursor_t c;
    long version;
    if (tiku_der_read(p, n, &whole) != 0 || whole.rawlen != n ||
        !tiku_der_is(&whole, TIKU_DER_SEQUENCE)) return 0;
    tiku_der_enter(&whole, &c);
    return tiku_der_take(&c, TIKU_DER_INTEGER, &e) == 0 &&
           tiku_der_int(&e, &version) == 0 && (version == 0 || version == 1) &&
           tiku_der_take(&c, TIKU_DER_SEQUENCE, &e) == 0 && e.len > 0 &&
           tiku_der_take(&c, TIKU_DER_OCTETS, &e) == 0 && e.len > 0;
}

/** @brief EncryptedPrivateKeyInfo @p e opened into @p g's key. */
static void
shrouded(gather_t *g, const tiku_der_t *e)
{
    tiku_der_cursor_t c;
    tiku_der_t alg, sealed;
    unsigned char *plain;
    long n;

    tiku_der_enter(e, &c);
    if (tiku_der_take(&c, TIKU_DER_SEQUENCE, &alg) != 0 ||
        tiku_der_take(&c, TIKU_DER_OCTETS, &sealed) != 0) {
        return;
    }
    n = pbe_open(&alg, g, sealed.body, sealed.len, &plain);
    if (n == -1) g->wrong = 1;
    if (n == -2) g->unsupported = 1;
    if (n > 0) {
        if (private_key_info(plain, (size_t)n)) {
            keep_key(g, plain, (size_t)n, 0);
        } else {
            g->wrong = 1;
        }
        cms_wipe(plain, (size_t)n);
    }
    free(plain);
}

static void safe_contents(gather_t *g, const unsigned char *p, size_t n,
                          int depth);

/** @brief One SafeBag @p bag: a key, a shrouded key, a certificate, or
 *         nested safe contents. */
static void
safe_bag(gather_t *g, const tiku_der_t *bag, int depth)
{
    tiku_der_cursor_t c, v;
    tiku_der_t oid, value, e, cert;

    tiku_der_enter(bag, &c);
    if (tiku_der_take(&c, TIKU_DER_OID, &oid) != 0 ||
        tiku_der_take(&c, 0x80, &value) != 0) {
        return;
    }
    tiku_der_enter(&value, &v);
    if (tiku_der_next(&v, &e) != 0) {
        return;
    }
    if (tiku_der_oid(&oid, OID_KEY_BAG)) {
        keep_key(g, e.raw, e.rawlen, 0);
    } else if (tiku_der_oid(&oid, OID_SHROUDED)) {
        shrouded(g, &e);
    } else if (tiku_der_oid(&oid, OID_CERT_BAG)) {
        tiku_der_enter(&e, &v);
        if (tiku_der_take(&v, TIKU_DER_OID, &oid) == 0 &&
            tiku_der_oid(&oid, OID_X509) &&
            tiku_der_take(&v, 0x80, &e) == 0) {
            tiku_der_enter(&e, &v);
            if (tiku_der_take(&v, TIKU_DER_OCTETS, &cert) == 0) {
                keep_cert(g, cert.body, cert.len);
            }
        }
    } else if (tiku_der_oid(&oid, OID_SAFE_BAG) && depth < KEY_DEPTH) {
        safe_contents(g, e.raw, e.rawlen, depth + 1);
    }
}

/** @brief SafeContents @p p: each bag in it. */
static void
safe_contents(gather_t *g, const unsigned char *p, size_t n, int depth)
{
    tiku_der_cursor_t c;
    tiku_der_t all, bag;

    if (tiku_der_read(p, n, &all) != 0) {
        g->wrong = 1;               /* not DER: a wrong password's output */
        return;
    }
    tiku_der_enter(&all, &c);
    while (tiku_der_take(&c, TIKU_DER_SEQUENCE, &bag) == 0) {
        safe_bag(g, &bag, depth);
    }
}

/** @brief One ContentInfo of the authenticated safe: plain safe contents,
 *         or sealed ones opened with the password. */
static void
safe_part(gather_t *g, const tiku_der_t *ci)
{
    tiku_der_cursor_t c;
    tiku_der_t oid, e, eci, alg, sealed;
    unsigned char *plain;
    long n;

    tiku_der_enter(ci, &c);
    if (tiku_der_take(&c, TIKU_DER_OID, &oid) != 0 ||
        tiku_der_take(&c, 0x80, &e) != 0) return;
    tiku_der_enter(&e, &c);
    if (tiku_der_next(&c, &e) != 0) return;
    if (tiku_der_oid(&oid, OID_DATA)) {
        if ((n = tiku_der_octets(&e, &plain)) >= 0) {
            safe_contents(g, plain, (size_t)n, 0);
        }
        free(plain);
        return;
    }
    if (!tiku_der_oid(&oid, OID_ENC_DATA)) return;
    tiku_der_enter(&e, &c);
    if (tiku_der_take(&c, TIKU_DER_INTEGER, &e) != 0 ||
        tiku_der_take(&c, TIKU_DER_SEQUENCE, &eci) != 0) return;
    tiku_der_enter(&eci, &c);
    if (tiku_der_take(&c, TIKU_DER_OID, &e) != 0 ||
        tiku_der_take(&c, TIKU_DER_SEQUENCE, &alg) != 0 ||
        tiku_der_take(&c, 0x80, &sealed) != 0) return;
    if ((n = tiku_der_octets(&sealed, &plain)) >= 0) {
        unsigned char *open;
        long m = pbe_open(&alg, g, plain, (size_t)n, &open);

        if (m == -1) g->wrong = 1;
        if (m == -2) g->unsupported = 1;
        if (m >= 0) {
            safe_contents(g, open, (size_t)m, 0);
            memset(open, 0, (size_t)m);
        }
        free(open);
    }
    free(plain);
}

/** @brief A PFX's MacData (RFC 7292 4): its digest, the MAC it holds,
 *         the salt and the rounds.  @p hash is 0 for a digest algorithm
 *         cms_digest() does not name.  @return 0, or -1 for a MacData
 *         not laid out as the RFC has it. */
static int
mac_data(const tiku_der_t *mac, int *hash, tiku_der_t *digest,
         tiku_der_t *salt, long *rounds)
{
    tiku_der_cursor_t c, di;
    tiku_der_t info, alg, e;

    *rounds = 1;
    tiku_der_enter(mac, &c);
    if (tiku_der_take(&c, TIKU_DER_SEQUENCE, &info) != 0 ||
        tiku_der_take(&c, TIKU_DER_OCTETS, salt) != 0 ||
        (tiku_der_next(&c, &e) == 0 && tiku_der_int(&e, rounds) != 0)) {
        return -1;
    }
    tiku_der_enter(&info, &di);
    if (tiku_der_take(&di, TIKU_DER_SEQUENCE, &alg) != 0 ||
        tiku_der_take(&di, TIKU_DER_OCTETS, digest) != 0) return -1;
    tiku_der_enter(&alg, &c);
    if (tiku_der_take(&c, TIKU_DER_OID, &e) != 0) return -1;
    *hash = cms_digest(&e);
    return 0;
}

/** @brief What mac_holds() found short of a MAC it could compare. */
enum { MAC_UNREAD = -1, MAC_UNSUPPORTED = -2, MAC_BAD = -3 };

/**
 * @brief Whether the PFX's MacData @p mac holds over @p safe with @p g's
 *        password.
 *
 * @return 1 it does; 0 it does not; MAC_UNREAD for a digest algorithm
 *         this does not name, or when memory for the password is short;
 *         MAC_UNSUPPORTED for more than KEY_ROUNDS iterations; MAC_BAD
 *         for a MacData not laid out as RFC 7292 has it, a digest not
 *         the length of its algorithm, or an iteration count below 1.
 */
static int
mac_holds(const tiku_der_t *mac, const gather_t *g, const unsigned char *safe,
          size_t n)
{
    unsigned char key[TIKU_HASH_MAX], got[TIKU_HASH_MAX], *pw;
    tiku_der_t digest, salt;
    long rounds, pwlen;
    int hash, ok;

    if (mac_data(mac, &hash, &digest, &salt, &rounds) != 0 ||
        rounds < 1) return MAC_BAD;
    if (hash == 0) return MAC_UNREAD;
    if (rounds > KEY_ROUNDS) return MAC_UNSUPPORTED;
    if (digest.len != tiku_hash(hash, "", 0, got)) return MAC_BAD;
    if ((pwlen = bmp_password(g->password, g->none, &pw)) < 0) {
        return MAC_UNREAD;
    }
    ok = p12_kdf(hash, pw, (size_t)pwlen, salt.body, salt.len, rounds, 3,
                 key, digest.len) == 0 &&
         tiku_hmac(hash, key, digest.len, safe, n, got) == digest.len &&
         memcmp(got, digest.body, digest.len) == 0;
    memset(pw, 0, (size_t)pwlen);
    memset(key, 0, sizeof key);
    free(pw);
    return ok;
}

/**
 * @brief A PFX (PKCS#12) file: its MAC checked, then every part of its
 *        safe gathered.
 *
 * @return TIKU_KEY_OK when the MAC holds, when there is none, or when it
 *         is one mac_holds() cannot read (MAC_UNREAD), in which case the
 *         safe is gathered unchecked; TIKU_KEY_PASSWORD when the MAC
 *         does not hold; TIKU_KEY_UNSUPPORTED for a MAC of more than
 *         KEY_ROUNDS iterations; TIKU_KEY_BAD for what is not a PFX or
 *         whose MacData is malformed.
 */
static int
pfx_read(const tiku_der_t *pfx, gather_t *g)
{
    tiku_der_cursor_t c, parts;
    tiku_der_t e, auth, mac, ci;
    unsigned char *safe;
    long v, n;
    int held = -1;

    tiku_der_enter(pfx, &c);
    if (tiku_der_take(&c, TIKU_DER_INTEGER, &e) != 0 ||
        tiku_der_int(&e, &v) != 0 || v != 3 ||
        tiku_der_take(&c, TIKU_DER_SEQUENCE, &auth) != 0) return TIKU_KEY_BAD;
    tiku_der_enter(&auth, &parts);
    if (tiku_der_take(&parts, TIKU_DER_OID, &e) != 0 ||
        !tiku_der_oid(&e, OID_DATA) || tiku_der_take(&parts, 0x80, &e) != 0 ||
        (tiku_der_enter(&e, &parts), tiku_der_next(&parts, &e)) ||
        (n = tiku_der_octets(&e, &safe)) < 0) return TIKU_KEY_BAD;
    if (tiku_der_take(&c, TIKU_DER_SEQUENCE, &mac) == 0) {
        held = mac_holds(&mac, g, safe, (size_t)n);
        if (held == MAC_UNSUPPORTED || held == MAC_BAD) {
            free(safe);
            return held == MAC_BAD ? TIKU_KEY_BAD : TIKU_KEY_UNSUPPORTED;
        }
        /* An empty password may mean no password bytes at all. */
        if (held == 0 && g->password[0] == 0) {
            g->none = 1;
            if ((held = mac_holds(&mac, g, safe, (size_t)n)) != 1) g->none = 0;
        }
    }
    if (held == 0) {
        free(safe);
        return TIKU_KEY_PASSWORD;
    }
    if (tiku_der_read(safe, (size_t)n, &e) == 0) {
        tiku_der_enter(&e, &parts);
        while (tiku_der_take(&parts, TIKU_DER_SEQUENCE, &ci) == 0) {
            safe_part(g, &ci);
        }
    }
    free(safe);
    return TIKU_KEY_OK;
}

/* ----------------------------------------------------------------- PEM  */

/** @brief Base64 @p s, up to @p end or its first '=', into @p out;
 *         characters outside the alphabet are skipped.  @return the bytes. */
static size_t
base64(const char *s, const char *end, unsigned char *out)
{
    unsigned v = 0;
    int bits = 0, x;
    size_t n = 0;

    for (; s < end && *s != '='; s++) {
        char ch = *s;

        x = ch >= 'A' && ch <= 'Z' ? ch - 'A' : ch >= 'a' && ch <= 'z' ?
            ch - 'a' + 26 : ch >= '0' && ch <= '9' ? ch - '0' + 52 :
            ch == '+' ? 62 : ch == '/' ? 63 : -1;
        if (x < 0) continue;
        v = v << 6 | (unsigned)x;
        if ((bits += 6) >= 8) {
            bits -= 8;
            out[n++] = (unsigned char)(v >> bits);
        }
    }
    return n;
}

/** @brief One PEM block, labelled @p label, of @p n decoded bytes; a
 *         @p sealed one, under OpenSSL's legacy PEM encryption, only marks
 *         @p g as holding what this does not read. */
static void
pem_block(gather_t *g, const char *label, const unsigned char *p, size_t n,
          int sealed)
{
    tiku_der_t e;

    if (sealed || tiku_der_read(p, n, &e) != 0) {
        g->unsupported |= sealed;
        return;
    }
    if (!strcmp(label, "PRIVATE KEY")) keep_key(g, e.raw, e.rawlen, 0);
    else if (!strcmp(label, "RSA PRIVATE KEY")) keep_key(g, e.raw, e.rawlen, 1);
    else if (!strcmp(label, "ENCRYPTED PRIVATE KEY")) shrouded(g, &e);
    else if (!strcmp(label, "CERTIFICATE")) keep_cert(g, e.raw, e.rawlen);
}

/** @brief Each "-----BEGIN" block of PEM text @p s.  @return how many. */
static int
pem_read(const char *s, size_t n, gather_t *g)
{
    const char *at = s, *stop = s + n, *head, *body, *end;
    unsigned char *der;
    char label[40];
    size_t len;
    int count = 0;

    while ((head = strstr(at, "-----BEGIN ")) != NULL && head < stop) {
        head += 11;
        if ((body = strstr(head, "-----")) == NULL ||
            (end = strstr(body + 5, "-----END")) == NULL) break;
        len = (size_t)(body - head);
        snprintf(label, sizeof label, "%.*s", (int)(len < 39 ? len : 39), head);
        body += 5;
        if ((der = malloc((size_t)(end - body))) != NULL) {
            /* OpenSSL's legacy PEM encryption: a Proc-Type header
             * before the base64. */
            const char *proc = strstr(body, "Proc-Type:");
            int sealed = proc != NULL && proc < end;

            pem_block(g, label, der, sealed ? 0 : base64(body, end, der),
                      sealed);
            free(der);
            count++;
        }
        at = end + 8;
    }
    return count;
}

/* ----------------------------------------------------------- the key  */

/** @brief The RSA numbers of the gathered key, copied into @p k.
 *         @return TIKU_KEY_OK, _UNSUPPORTED for a key not RSA, or _BAD. */
static int
key_numbers(const gather_t *g, tiku_key_t *k)
{
    tiku_der_cursor_t c;
    tiku_der_t e, pk;
    tiku_num_t *num[8];
    size_t at = 0;
    int i;

    if (tiku_der_read(g->key, g->key_len, &pk) != 0) return TIKU_KEY_BAD;
    if (!g->rsa1) {                 /* PrivateKeyInfo around RSAPrivateKey */
        tiku_der_enter(&pk, &c);
        if (tiku_der_take(&c, TIKU_DER_INTEGER, &e) != 0 ||
            tiku_der_take(&c, TIKU_DER_SEQUENCE, &e) != 0) return TIKU_KEY_BAD;
        tiku_der_enter(&e, &c);
        if (tiku_der_take(&c, TIKU_DER_OID, &e) != 0) return TIKU_KEY_BAD;
        if (!tiku_der_oid(&e, OID_RSA) && !tiku_der_oid(&e, OID_RSA_PSS)) {
            return TIKU_KEY_UNSUPPORTED;
        }
        tiku_der_enter(&pk, &c);
        (void)tiku_der_next(&c, &e);
        (void)tiku_der_next(&c, &e);
        if (tiku_der_take(&c, TIKU_DER_OCTETS, &e) != 0 ||
            tiku_der_read(e.body, e.len, &pk) != 0) return TIKU_KEY_BAD;
    }
    num[0] = &k->rsa.n; num[1] = &k->rsa.e; num[2] = &k->rsa.d;
    num[3] = &k->rsa.p; num[4] = &k->rsa.q; num[5] = &k->rsa.dp;
    num[6] = &k->rsa.dq; num[7] = &k->rsa.qi;
    if ((k->num = malloc(pk.len)) == NULL) return TIKU_KEY_BAD;
    k->num_len = pk.len;
    tiku_der_enter(&pk, &c);
    if (tiku_der_take(&c, TIKU_DER_INTEGER, &e) != 0) return TIKU_KEY_BAD;
    for (i = 0; i < 8; i++) {
        const unsigned char *b;
        size_t n;

        if (tiku_der_take(&c, TIKU_DER_INTEGER, &e) != 0 ||
            tiku_der_uint(&e, &b, &n) != 0) return TIKU_KEY_BAD;
        memcpy(k->num + at, b, n);
        num[i]->b = k->num + at;
        num[i]->n = n;
        at += n;
    }
    return TIKU_KEY_OK;
}

/** @brief The gathered certificate whose public key is @p k's, kept with
 *         it, and its subject as the key's name. */
static void
key_cert(const gather_t *g, tiku_key_t *k)
{
    tiku_cert_info_t info;
    cms_cert_t cert;
    cms_pub_t pub;
    int i;

    for (i = 0; i < g->ncert; i++) {
        if (cms_cert_read(g->cert[i], g->cert_len[i], &cert) != 0 ||
            cms_cert_key(&cert, &pub) != 0 || pub.curve != 0 ||
            pub.rsa.n.n != k->rsa.n.n ||
            memcmp(pub.rsa.n.b, k->rsa.n.b, k->rsa.n.n) != 0 ||
            (k->cert = malloc(g->cert_len[i])) == NULL) continue;
        memcpy(k->cert, g->cert[i], g->cert_len[i]);
        k->cert_len = g->cert_len[i];
        if (tiku_cert_info(k->cert, k->cert_len, &info) == 0) {
            snprintf(k->name, sizeof k->name, "%s", info.subject);
        }
        return;
    }
}

int
tiku_key_read(const void *data, size_t n, const char *password,
              tiku_key_t **out)
{
    gather_t g;
    tiku_der_t e;
    tiku_key_t *k;
    int status = TIKU_KEY_BAD, i;

    *out = NULL;
    memset(&g, 0, sizeof g);
    g.password = password != NULL ? password : "";
    if (data == NULL || n == 0) return TIKU_KEY_BAD;
    if (tiku_der_read(data, n, &e) == 0 && tiku_der_is(&e, TIKU_DER_SEQUENCE)) {
        if ((status = pfx_read(&e, &g)) == TIKU_KEY_BAD) {
            /* A bare PKCS#8 key, encrypted or not, or a PKCS#1 key. */
            tiku_der_cursor_t c;
            tiku_der_t first, second;

            tiku_der_enter(&e, &c);
            if (tiku_der_next(&c, &first) == 0 &&
                tiku_der_next(&c, &second) == 0) {
                if (tiku_der_is(&first, TIKU_DER_SEQUENCE)) shrouded(&g, &e);
                else keep_key(&g, e.raw, e.rawlen,
                              tiku_der_is(&second, TIKU_DER_INTEGER));
                status = TIKU_KEY_OK;
            }
        }
    } else if (memchr(data, '-', n) != NULL) {
        char *text = malloc(n + 1);

        if (text != NULL) {
            memcpy(text, data, n);
            text[n] = 0;
            status = pem_read(text, n, &g) > 0 ? TIKU_KEY_OK : TIKU_KEY_BAD;
            free(text);
        }
    }
    if (status == TIKU_KEY_OK && g.key == NULL) {
        status = g.wrong ? TIKU_KEY_PASSWORD :
                 g.unsupported ? TIKU_KEY_UNSUPPORTED : TIKU_KEY_BAD;
    }
    if (status == TIKU_KEY_OK && (k = calloc(1, sizeof *k)) != NULL) {
        if ((status = key_numbers(&g, k)) == TIKU_KEY_OK) {
            key_cert(&g, k);
            *out = k;
        } else {
            tiku_key_free(k);
        }
    }
    if (g.key != NULL) memset(g.key, 0, g.key_len);
    free(g.key);
    for (i = 0; i < g.ncert; i++) free(g.cert[i]);
    return status;
}

void
tiku_key_free(tiku_key_t *key)
{
    if (key == NULL) {
        return;
    }
    if (key->num != NULL) cms_wipe(key->num, key->num_len);
    free(key->num);
    free(key->cert);
    free(key);
}

const char *
tiku_key_name(const tiku_key_t *key)
{
    return key != NULL ? key->name : "";
}
