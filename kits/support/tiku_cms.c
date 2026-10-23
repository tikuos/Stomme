/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cms.c - X.509 certificates (RFC 5280) and CMS (RFC 5652).  Signed
 * data is checked: the content's digest against the signed attributes,
 * and those against the signer's key.  RFC 3161 timestamp tokens are
 * signed data and are checked as such.  Enveloped data is opened with an
 * RSA key.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_cms_private.h"
#include "tiku_crypto.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CMS_DEPTH 2                 /* how deep signed data may nest    */
#define SIG_MISMATCH 100            /* well formed, and not the key's   */
#define CMS_TRIES 64                /* the most unnamed recipients tried */

#define OID_SIGNED     "1.2.840.113549.1.7.2"
#define OID_ENVELOPED  "1.2.840.113549.1.7.3"
#define OID_TSTINFO    "1.2.840.113549.1.9.16.1.4"
#define OID_MSG_DIGEST "1.2.840.113549.1.9.4"
#define OID_SIGN_TIME  "1.2.840.113549.1.9.5"
#define OID_STAMP      "1.2.840.113549.1.9.16.2.14"
#define OID_RSA        "1.2.840.113549.1.1.1"
#define OID_RSA_PSS    "1.2.840.113549.1.1.10"
#define OID_RSA_OAEP   "1.2.840.113549.1.1.7"
#define OID_MGF1       "1.2.840.113549.1.1.8"
#define OID_EC_KEY     "1.2.840.10045.2.1"

/* ------------------------------------------------ digests and ciphers  */

int
cms_digest(const tiku_der_t *oid)
{
    static const char *const names[] = { NULL, "1.2.840.113549.2.5",
        "1.3.14.3.2.26", "2.16.840.1.101.3.4.2.4", "2.16.840.1.101.3.4.2.1",
        "2.16.840.1.101.3.4.2.2", "2.16.840.1.101.3.4.2.3" };
    int i = tiku_der_oid_in(oid, names, 7);

    return i > 0 ? i : 0;
}

/** @brief The CBC ciphers containers name: kind and key length. */
static const struct {
    const char *oid;
    int         kind;
    size_t      klen;               /* 0: any (RC2)                     */
} cipher_names[] = {
    { "2.16.840.1.101.3.4.1.2", CMS_AES, 16 },
    { "2.16.840.1.101.3.4.1.22", CMS_AES, 24 },
    { "2.16.840.1.101.3.4.1.42", CMS_AES, 32 },
    { "1.2.840.113549.3.7", CMS_DES3, 24 },
    { "1.3.14.3.2.7", CMS_DES, 8 },
    { "1.2.840.113549.3.2", CMS_RC2, 0 },
};

/** @brief Which of cipher_names the AlgorithmIdentifier @p alg names, its
 *         parameters into @p param.  @return the index, or -1. */
static int
cipher_of(const tiku_der_t *alg, tiku_der_t *param)
{
    tiku_der_cursor_t c;
    tiku_der_t oid;
    size_t i;

    tiku_der_enter(alg, &c);
    if (tiku_der_take(&c, TIKU_DER_OID, &oid) != 0) {
        return -1;
    }
    memset(param, 0, sizeof *param);
    (void)tiku_der_next(&c, param);
    for (i = 0; i < sizeof cipher_names / sizeof cipher_names[0]; i++) {
        if (tiku_der_oid(&oid, cipher_names[i].oid)) {
            return (int)i;
        }
    }
    return -1;
}

size_t
cms_cipher_key_len(const tiku_der_t *alg)
{
    tiku_der_t param;
    int i = cipher_of(alg, &param);

    return i >= 0 ? cipher_names[i].klen : 0;
}

long
cms_cbc(int kind, const unsigned char *key, size_t klen, int bits,
        const unsigned char *iv, unsigned char *data, size_t n)
{
    unsigned char chain[16], next[16];
    size_t bs = kind == CMS_AES ? 16 : 8, at, i, pad;
    tiku_aes_t aes;
    tiku_des3_t des;
    tiku_rc2_t rc2;

    if (kind == CMS_RC4) {
        tiku_rc4_t rc4;

        tiku_rc4_init(&rc4, key, klen);
        tiku_rc4_crypt(&rc4, data, n);
        cms_wipe(&rc4, sizeof rc4);
        return (long)n;
    }
    if (n == 0 || n % bs != 0 ||
        (kind == CMS_AES && tiku_aes_init(&aes, key, (int)klen * 8) != 0) ||
        (kind == CMS_RC2 && tiku_rc2_init(&rc2, key, klen, bits) != 0) ||
        ((kind == CMS_DES3 || kind == CMS_DES) &&
         tiku_des3_init(&des, key, kind == CMS_DES ? 8 : klen) != 0) ||
        (kind == CMS_DES && klen != 8)) {
        cms_wipe(&aes, sizeof aes);
        cms_wipe(&des, sizeof des);
        cms_wipe(&rc2, sizeof rc2);
        return -1;
    }
    memcpy(chain, iv, bs);
    for (at = 0; at < n; at += bs) {
        memcpy(next, data + at, bs);
        if (kind == CMS_AES) tiku_aes_decrypt(&aes, data + at, data + at);
        else if (kind == CMS_RC2) tiku_rc2_decrypt(&rc2, data + at, data + at);
        else tiku_des3_decrypt(&des, data + at, data + at);
        for (i = 0; i < bs; i++) data[at + i] ^= chain[i];
        memcpy(chain, next, bs);
    }
    cms_wipe(&aes, sizeof aes);
    cms_wipe(&des, sizeof des);
    cms_wipe(&rc2, sizeof rc2);
    pad = data[n - 1];
    if (pad == 0 || pad > bs) {
        return -1;
    }
    for (i = n - pad; i < n; i++) {
        if (data[i] != pad) return -1;
    }
    return (long)(n - pad);
}

long
cms_decipher(const tiku_der_t *alg, const unsigned char *key, size_t klen,
             unsigned char *data, size_t n)
{
    tiku_der_t param, iv, ver;
    tiku_der_cursor_t c;
    long v = 32;                    /* RC2's version when it gives none */
    int i = cipher_of(alg, &param), bits;

    if (i < 0) {
        return -2;
    }
    iv = param;
    if (cipher_names[i].kind == CMS_RC2 &&
        tiku_der_is(&param, TIKU_DER_SEQUENCE)) {
        tiku_der_enter(&param, &c);
        if (tiku_der_next(&c, &ver) != 0) return -2;
        if (tiku_der_int(&ver, &v) == 0) {
            if (tiku_der_next(&c, &iv) != 0) return -2;
        } else {
            iv = ver;
        }
    }
    if (!tiku_der_is(&iv, TIKU_DER_OCTETS) ||
        iv.len != (cipher_names[i].kind == CMS_AES ? 16u : 8u) ||
        (cipher_names[i].klen != 0 && klen != cipher_names[i].klen)) {
        return cipher_names[i].klen != 0 && klen != cipher_names[i].klen
               ? -1 : -2;
    }
    /* RFC 2268's versions for 40, 64 and 128 effective bits. */
    bits = v == 160 ? 40 : v == 120 ? 64 : v == 58 ? 128 : (int)v;
    if (bits < 1 || bits > 1024) {
        bits = (int)klen * 8;
    }
    return cms_cbc(cipher_names[i].kind, key, klen, bits, iv.body, data, n);
}

/* ------------------------------------------------------ certificates  */

/** @brief Read the subject key identifier, the one extension used here,
 *         from a certificate's extensions @p ext into @p c. */
static void
cert_extensions(const tiku_der_t *ext, cms_cert_t *c)
{
    tiku_der_cursor_t all, one, inner;
    tiku_der_t list, e, oid, value, id;

    tiku_der_enter(ext, &all);
    if (tiku_der_take(&all, TIKU_DER_SEQUENCE, &list) != 0) return;
    tiku_der_enter(&list, &all);
    while (tiku_der_take(&all, TIKU_DER_SEQUENCE, &e) == 0) {
        tiku_der_enter(&e, &one);
        if (tiku_der_take(&one, TIKU_DER_OID, &oid) != 0 ||
            !tiku_der_oid(&oid, "2.5.29.14") ||
            tiku_der_next(&one, &value) != 0 ||
            (tiku_der_is(&value, TIKU_DER_BOOLEAN) &&
             tiku_der_next(&one, &value) != 0) ||
            !tiku_der_is(&value, TIKU_DER_OCTETS)) continue;
        tiku_der_enter(&value, &inner);
        if (tiku_der_take(&inner, TIKU_DER_OCTETS, &id) == 0) {
            c->ski = id.body;
            c->ski_len = id.len;
        }
    }
}

/** @brief A certificate's subjectPublicKeyInfo: its algorithm, parameters
 *         and bits.  @return 0, or -1. */
static int
cert_key_info(const tiku_der_t *spki, cms_cert_t *c)
{
    tiku_der_cursor_t cur, algc;
    tiku_der_t alg, bits;

    tiku_der_enter(spki, &cur);
    if (tiku_der_take(&cur, TIKU_DER_SEQUENCE, &alg) != 0 ||
        tiku_der_take(&cur, TIKU_DER_BITS, &bits) != 0 || bits.len < 1 ||
        bits.body[0] != 0) {
        return -1;
    }
    tiku_der_enter(&alg, &algc);
    if (tiku_der_take(&algc, TIKU_DER_OID, &c->key_alg) != 0) {
        return -1;
    }
    (void)tiku_der_next(&algc, &c->key_param);
    c->key = bits.body + 1;
    c->key_len = bits.len - 1;
    return 0;
}

int
cms_cert_read(const unsigned char *der, size_t n, cms_cert_t *c)
{
    tiku_der_cursor_t top, tbs, cur;
    tiku_der_t e, when;

    memset(c, 0, sizeof *c);
    if (tiku_der_read(der, n, &c->whole) != 0 ||
        !tiku_der_is(&c->whole, TIKU_DER_SEQUENCE)) return -1;
    tiku_der_enter(&c->whole, &top);
    if (tiku_der_take(&top, TIKU_DER_SEQUENCE, &c->tbs) != 0) return -1;
    tiku_der_enter(&c->tbs, &tbs);
    if (tiku_der_next(&tbs, &e) != 0) return -1;
    if (tiku_der_is(&e, 0x80) && tiku_der_next(&tbs, &e) != 0) return -1;
    c->serial = e;
    if (!tiku_der_is(&e, TIKU_DER_INTEGER) ||
        tiku_der_take(&tbs, TIKU_DER_SEQUENCE, &e) != 0 ||
        tiku_der_take(&tbs, TIKU_DER_SEQUENCE, &c->issuer) != 0 ||
        tiku_der_take(&tbs, TIKU_DER_SEQUENCE, &e) != 0) return -1;
    tiku_der_enter(&e, &cur);
    if (tiku_der_next(&cur, &when) != 0 ||
        tiku_der_time(&when, &c->not_before) != 0 ||
        tiku_der_next(&cur, &when) != 0 ||
        tiku_der_time(&when, &c->not_after) != 0 ||
        tiku_der_take(&tbs, TIKU_DER_SEQUENCE, &c->subject) != 0 ||
        tiku_der_take(&tbs, TIKU_DER_SEQUENCE, &e) != 0 ||
        cert_key_info(&e, c) != 0) return -1;
    while (tiku_der_next(&tbs, &e) == 0) {
        if (tiku_der_is(&e, 0x83)) cert_extensions(&e, c);
    }
    return 0;
}

/** @brief The TIKU_EC_* curve that @p oid, from a key's parameters,
 *         names; 0 for another. */
static int
curve_of(const tiku_der_t *oid)
{
    static const char *const names[] = { NULL, "1.2.840.10045.3.1.7",
        "1.3.132.0.34", "1.3.132.0.35", "1.3.36.3.3.2.8.1.1.7",
        "1.3.36.3.3.2.8.1.1.11", "1.3.36.3.3.2.8.1.1.13" };
    int i = tiku_der_oid_in(oid, names, 7);

    return i > 0 ? i : 0;
}

int
cms_cert_key(const cms_cert_t *c, cms_pub_t *k)
{
    tiku_der_t seq, num;
    tiku_der_cursor_t cur;

    memset(k, 0, sizeof *k);
    if (tiku_der_oid(&c->key_alg, OID_RSA) ||
        tiku_der_oid(&c->key_alg, OID_RSA_PSS)) {
        if (tiku_der_read(c->key, c->key_len, &seq) != 0) return -1;
        tiku_der_enter(&seq, &cur);
        if (tiku_der_next(&cur, &num) != 0 ||
            tiku_der_uint(&num, &k->rsa.n.b, &k->rsa.n.n) != 0 ||
            tiku_der_next(&cur, &num) != 0 ||
            tiku_der_uint(&num, &k->rsa.e.b, &k->rsa.e.n) != 0) return -1;
        return 0;
    }
    if (tiku_der_oid(&c->key_alg, OID_EC_KEY) &&
        (k->curve = curve_of(&c->key_param)) != 0) {
        k->point.b = c->key;
        k->point.n = c->key_len;
        return 0;
    }
    return -1;
}

/** @brief The first value of attribute @p oid in Name @p name, as UTF-8
 *         into @p out.  @return 0, or -1 when it has none. */
static int
name_part(const tiku_der_t *name, const char *oid, char *out, size_t cap)
{
    tiku_der_cursor_t rdns, set, pair;
    tiku_der_t rdn, atv, type, value;

    out[0] = 0;
    tiku_der_enter(name, &rdns);
    while (tiku_der_take(&rdns, TIKU_DER_SET, &rdn) == 0) {
        tiku_der_enter(&rdn, &set);
        while (tiku_der_take(&set, TIKU_DER_SEQUENCE, &atv) == 0) {
            tiku_der_enter(&atv, &pair);
            if (tiku_der_take(&pair, TIKU_DER_OID, &type) == 0 &&
                tiku_der_oid(&type, oid) && tiku_der_next(&pair, &value) == 0 &&
                tiku_der_string(&value, out, cap) == 0 && out[0]) {
                return 0;
            }
        }
    }
    return -1;
}

/** @brief A Name as a person reads it: its common name, else its
 *         organisation's, unit's or email, else "an unnamed party". */
static void
name_text(const tiku_der_t *name, char *out, size_t cap)
{
    if (name_part(name, "2.5.4.3", out, cap) == 0 ||
        name_part(name, "2.5.4.10", out, cap) == 0 ||
        name_part(name, "2.5.4.11", out, cap) == 0 ||
        name_part(name, "1.2.840.113549.1.9.1", out, cap) == 0) {
        return;
    }
    snprintf(out, cap, "an unnamed party");
}

/** @brief What cms_cert_read() read, as tiku_cert_info() gives it. */
static void
cert_describe(const cms_cert_t *c, tiku_cert_info_t *out)
{
    memset(out, 0, sizeof *out);
    name_text(&c->subject, out->subject, sizeof out->subject);
    (void)name_part(&c->subject, "2.5.4.10", out->org, sizeof out->org);
    (void)name_part(&c->subject, "1.2.840.113549.1.9.1", out->email,
                    sizeof out->email);
    name_text(&c->issuer, out->issuer, sizeof out->issuer);
    out->not_before = c->not_before;
    out->not_after = c->not_after;
    out->self_signed = c->issuer.rawlen == c->subject.rawlen &&
                       !memcmp(c->issuer.raw, c->subject.raw,
                               c->issuer.rawlen);
}

int
tiku_cert_info(const unsigned char *der, size_t n, tiku_cert_info_t *out)
{
    cms_cert_t c;

    if (cms_cert_read(der, n, &c) != 0) {
        memset(out, 0, sizeof *out);
        return -1;
    }
    cert_describe(&c, out);
    return 0;
}

/* -------------------------------------------------------- signatures  */

/** @brief Say @p text as what was wrong, and return @p status. */
static int
say(tiku_cms_sig_t *out, int status, const char *text)
{
    snprintf(out->why, sizeof out->why, "%s", text);
    out->status = status;
    return status;
}

/** @brief The digest @p alg of @p npieces runs, into @p out.
 *         @return its length, or 0. */
static size_t
digest_of(int alg, const tiku_piece_t *p, int npieces, unsigned char *out)
{
    tiku_hash_t h;
    size_t len = tiku_hash_init(&h, alg);
    int i;

    for (i = 0; i < npieces; i++) {
        tiku_hash_update(&h, p[i].p, p[i].n);
    }
    tiku_hash_final(&h, out);
    return len;
}

/** @brief An RSASSA-PSS AlgorithmIdentifier's parameters: the digest, the
 *         mask's digest and the salt's length, defaulted as RFC 4055 says. */
static void
pss_params(const tiku_der_t *params, int *alg, int *mgf, long *salt)
{
    tiku_der_cursor_t c, in, m;
    tiku_der_t e, a, oid;

    *alg = *mgf = TIKU_HASH_SHA1;
    *salt = 20;
    if (!tiku_der_is(params, TIKU_DER_SEQUENCE)) return;
    tiku_der_enter(params, &c);
    while (tiku_der_next(&c, &e) == 0) {
        tiku_der_enter(&e, &in);
        if (tiku_der_is(&e, 0x80) && tiku_der_take(&in, TIKU_DER_SEQUENCE,
                                                  &a) == 0) {
            tiku_der_enter(&a, &m);
            if (tiku_der_take(&m, TIKU_DER_OID, &oid) == 0) {
                *alg = cms_digest(&oid);
            }
        } else if (tiku_der_is(&e, 0x81) &&
                   tiku_der_take(&in, TIKU_DER_SEQUENCE, &a) == 0) {
            tiku_der_enter(&a, &m);             /* MGF1 and its digest */
            if (tiku_der_take(&m, TIKU_DER_OID, &oid) == 0 &&
                tiku_der_oid(&oid, OID_MGF1) &&
                tiku_der_take(&m, TIKU_DER_SEQUENCE, &a) == 0) {
                tiku_der_enter(&a, &m);
                if (tiku_der_take(&m, TIKU_DER_OID, &oid) == 0) {
                    *mgf = cms_digest(&oid);
                }
            } else {
                *mgf = 0;
            }
        } else if (tiku_der_is(&e, 0x82) && tiku_der_next(&in, &a) == 0) {
            (void)tiku_der_int(&a, salt);
        }
    }
}

/** @brief An RSA signature by @p key: RSASSA-PSS with @p params when
 *         @p pss, else v1.5 over the algorithm's digest @p named or @p alg.
 *         @return TIKU_SIG_GOOD, SIG_MISMATCH, BROKEN or UNCHECKED. */
static int
sig_rsa(const cms_pub_t *key, int pss, const tiku_der_t *params, int named,
        int alg, const tiku_piece_t *msg, int n, const unsigned char *sig,
        size_t siglen, tiku_cms_sig_t *out)
{
    unsigned char d[TIKU_HASH_MAX];
    int mgf = 0, ok;
    long salt;

    if (key->curve != 0) {
        return say(out, TIKU_SIG_BROKEN, "an RSA signature by an EC key");
    }
    if (pss) {
        pss_params(params, &alg, &mgf, &salt);
    } else if (named > 0) {
        alg = named;
    }
    if (!digest_of(alg, msg, n, d) || (pss && !mgf)) {
        return say(out, TIKU_SIG_UNCHECKED, "its digest is not read");
    }
    ok = pss ? tiku_rsa_verify_pss(&key->rsa, alg, mgf, salt, d, sig, siglen)
             : tiku_rsa_verify(&key->rsa, alg, d, sig, siglen);
    return ok ? TIKU_SIG_GOOD : SIG_MISMATCH;
}

/** @brief An ECDSA signature by @p key, its value a DER pair (r, s), over
 *         the digest @p alg of @p msg.  @return TIKU_SIG_GOOD, SIG_MISMATCH
 *         or BROKEN. */
static int
sig_ec(const cms_pub_t *key, int alg, const tiku_piece_t *msg, int n,
       const unsigned char *sig, size_t siglen, tiku_cms_sig_t *out)
{
    unsigned char d[TIKU_HASH_MAX];
    tiku_der_cursor_t c;
    tiku_der_t rs, r, s;
    tiku_num_t rn, sn;
    size_t dl;

    if (key->curve == 0) {
        return say(out, TIKU_SIG_BROKEN, "an EC signature by an RSA key");
    }
    if ((dl = digest_of(alg, msg, n, d)) == 0) {
        return say(out, TIKU_SIG_UNCHECKED, "its digest is not read");
    }
    if (tiku_der_read(sig, siglen, &rs) != 0 ||
        (tiku_der_enter(&rs, &c), tiku_der_take(&c, TIKU_DER_INTEGER, &r)) ||
        tiku_der_take(&c, TIKU_DER_INTEGER, &s) != 0) {
        return say(out, TIKU_SIG_BROKEN, "its value is not a signature");
    }
    rn.b = r.body;
    rn.n = r.len;
    sn.b = s.body;
    sn.n = s.len;
    return tiku_ecdsa_verify(key->curve, key->point.b, key->point.n, d, dl,
                             &rn, &sn) ? TIKU_SIG_GOOD : SIG_MISMATCH;
}

/**
 * @brief Whether @p sig is @p cert's key's signature, as @p sigalg (an
 *        AlgorithmIdentifier) says, of the runs @p msg: digested by the
 *        algorithm's own digest, else @p alg.
 * @return TIKU_SIG_GOOD; SIG_MISMATCH when it is well formed and not the
 *         key's; BROKEN or UNCHECKED with what into @p out.
 */
static int
sig_check(const cms_cert_t *cert, const tiku_der_t *sigalg, int alg,
          const tiku_piece_t *msg, int n, const unsigned char *sig,
          size_t siglen, tiku_cms_sig_t *out)
{
    static const char *const rsa[] = { OID_RSA, "1.2.840.113549.1.1.4",
        "1.2.840.113549.1.1.5", "1.2.840.113549.1.1.14",
        "1.2.840.113549.1.1.11", "1.2.840.113549.1.1.12",
        "1.2.840.113549.1.1.13" };
    static const char *const ec[] = { OID_EC_KEY, NULL, "1.2.840.10045.4.1",
        "1.2.840.10045.4.3.1", "1.2.840.10045.4.3.2", "1.2.840.10045.4.3.3",
        "1.2.840.10045.4.3.4" };
    tiku_der_cursor_t c;
    tiku_der_t oid, params;
    cms_pub_t key;
    int i;

    tiku_der_enter(sigalg, &c);
    if (tiku_der_take(&c, TIKU_DER_OID, &oid) != 0) {
        return say(out, TIKU_SIG_BROKEN, "its algorithm is not named");
    }
    memset(&params, 0, sizeof params);
    (void)tiku_der_next(&c, &params);
    if (cms_cert_key(cert, &key) != 0) {
        return say(out, TIKU_SIG_UNCHECKED, "its key is of a kind not read");
    }
    if ((i = tiku_der_oid_in(&oid, rsa, 7)) >= 0 ||
        tiku_der_oid(&oid, OID_RSA_PSS)) {
        return sig_rsa(&key, i < 0, &params, i, alg, msg, n, sig, siglen,
                       out);
    }
    if ((i = tiku_der_oid_in(&oid, ec, 7)) >= 0) {
        return sig_ec(&key, i > 1 ? i : alg, msg, n, sig, siglen, out);
    }
    return say(out, TIKU_SIG_UNCHECKED, "it is made in a way not read");
}

/** @brief The certificate among SignedData's @p certs that signer id
 *         @p sid names: by issuer and serial number, or by key id.
 *         @return 0 with it in @p c, or -1. */
static int
signer_cert(const tiku_der_t *certs, const tiku_der_t *sid, cms_cert_t *c)
{
    tiku_der_cursor_t all, id;
    tiku_der_t e, issuer, serial;

    if (tiku_der_is(sid, TIKU_DER_SEQUENCE)) {
        tiku_der_enter(sid, &id);
        if (tiku_der_take(&id, TIKU_DER_SEQUENCE, &issuer) != 0 ||
            tiku_der_take(&id, TIKU_DER_INTEGER, &serial) != 0) return -1;
    }
    tiku_der_enter(certs, &all);
    while (tiku_der_next(&all, &e) == 0) {
        if (!tiku_der_is(&e, TIKU_DER_SEQUENCE) ||
            cms_cert_read(e.raw, e.rawlen, c) != 0) continue;
        if (tiku_der_is(sid, TIKU_DER_SEQUENCE)
            ? c->issuer.rawlen == issuer.rawlen &&
              !memcmp(c->issuer.raw, issuer.raw, issuer.rawlen) &&
              c->serial.len == serial.len &&
              !memcmp(c->serial.body, serial.body, serial.len)
            : tiku_der_is(sid, 0x80) && c->ski_len == sid->len &&
              c->ski != NULL && !memcmp(c->ski, sid->body, sid->len)) {
            return 0;
        }
    }
    return -1;
}

/** @brief The value of attribute @p oid among @p attrs (a SET of them).
 *         @return 0 with its first value in @p v, or -1. */
static int
attribute(const tiku_der_t *attrs, const char *oid, tiku_der_t *v)
{
    tiku_der_cursor_t all, one, set;
    tiku_der_t a, type, values;

    tiku_der_enter(attrs, &all);
    while (tiku_der_take(&all, TIKU_DER_SEQUENCE, &a) == 0) {
        tiku_der_enter(&a, &one);
        if (tiku_der_take(&one, TIKU_DER_OID, &type) == 0 &&
            tiku_der_oid(&type, oid) &&
            tiku_der_take(&one, TIKU_DER_SET, &values) == 0) {
            tiku_der_enter(&values, &set);
            return tiku_der_next(&set, v);
        }
    }
    return -1;
}

/** @brief A TSTInfo's imprint checked against @p content, when there is
 *         any, and its time taken.  @return a TIKU_SIG_* for the imprint. */
static int
tst_info(const unsigned char *p, size_t n, const tiku_piece_t *content,
         int npieces, tiku_cms_sig_t *out)
{
    unsigned char d[TIKU_HASH_MAX];
    tiku_der_cursor_t c;
    tiku_der_t e, imprint, alg, hashed;
    size_t dl;

    if (tiku_der_read(p, n, &e) != 0) goto bad;
    tiku_der_enter(&e, &c);
    if (tiku_der_take(&c, TIKU_DER_INTEGER, &e) != 0 ||
        tiku_der_take(&c, TIKU_DER_OID, &e) != 0 ||
        tiku_der_take(&c, TIKU_DER_SEQUENCE, &imprint) != 0 ||
        tiku_der_take(&c, TIKU_DER_INTEGER, &e) != 0 ||
        tiku_der_next(&c, &e) != 0 ||
        tiku_der_time(&e, &out->stamped_at) != 0) goto bad;
    tiku_der_enter(&imprint, &c);
    if (tiku_der_take(&c, TIKU_DER_SEQUENCE, &alg) != 0 ||
        tiku_der_take(&c, TIKU_DER_OCTETS, &hashed) != 0) goto bad;
    tiku_der_enter(&alg, &c);
    if (tiku_der_take(&c, TIKU_DER_OID, &e) != 0) goto bad;
    if (npieces == 0) return TIKU_SIG_GOOD;
    if ((dl = digest_of(cms_digest(&e), content, npieces, d)) == 0) {
        return say(out, TIKU_SIG_UNCHECKED, "its imprint's digest is not read");
    }
    if (dl != hashed.len || memcmp(d, hashed.body, dl) != 0) {
        return say(out, TIKU_SIG_CHANGED, "what it dates has changed");
    }
    return TIKU_SIG_GOOD;
bad:
    return say(out, TIKU_SIG_BROKEN, "its timestamp is not one");
}

static int verify(const unsigned char *der, size_t n,
                  const tiku_piece_t *content, int npieces,
                  tiku_cms_sig_t *out, int depth);

/** @brief A timestamp token among a signer's unsigned attributes, over
 *         its signature @p sig: checked, and its time and authority
 *         kept in @p out. */
static void
signer_stamp(const tiku_der_t *unsigned_attrs, const tiku_der_t *sig,
             tiku_cms_sig_t *out, int depth)
{
    tiku_cms_sig_t *t = malloc(sizeof *t);
    tiku_piece_t over = { sig->body, sig->len };
    tiku_der_t token;

    if (t == NULL || attribute(unsigned_attrs, OID_STAMP, &token) != 0) {
        free(t);
        return;
    }
    out->stamp = verify(token.raw, token.rawlen, &over, 1, t, depth + 1);
    out->stamped_at = t->stamped_at;
    snprintf(out->stamp_by, sizeof out->stamp_by, "%s", t->cert.subject);
    free(t);
}

/** @brief A SignerInfo's parts. */
typedef struct {
    tiku_der_t sid, dalg, attrs, salg, sig, unsigned_attrs;
} signer_info_t;

/** @brief Read SignerInfo @p si into @p s.  @return 0, or -1. */
static int
signer_read(const tiku_der_t *si, signer_info_t *s)
{
    tiku_der_cursor_t c;
    tiku_der_t e;

    memset(s, 0, sizeof *s);
    tiku_der_enter(si, &c);
    if (tiku_der_take(&c, TIKU_DER_INTEGER, &e) != 0 ||
        tiku_der_next(&c, &s->sid) != 0 ||
        tiku_der_take(&c, TIKU_DER_SEQUENCE, &s->dalg) != 0 ||
        tiku_der_next(&c, &e) != 0) {
        return -1;
    }
    if (tiku_der_is(&e, 0x80)) {
        s->attrs = e;
        if (tiku_der_next(&c, &e) != 0) return -1;
    }
    s->salg = e;
    if (tiku_der_take(&c, TIKU_DER_OCTETS, &s->sig) != 0) {
        return -1;
    }
    if (tiku_der_next(&c, &e) == 0 && tiku_der_is(&e, 0x81)) {
        s->unsigned_attrs = e;
    }
    return 0;
}

/**
 * @brief The signed attributes of @p s, holding the content's digest
 *        @p cd (@p cdl bytes), checked against the signature by @p cert;
 *        the signing time taken.  @return the status, CHANGED when the
 *        signature is good but the digest is another's.
 */
static int
signed_attrs(const signer_info_t *s, const cms_cert_t *cert,
             const unsigned char *cd, size_t cdl, tiku_cms_sig_t *out)
{
    unsigned char *set;
    tiku_der_t md, when;
    tiku_piece_t over;
    int status;

    if (attribute(&s->attrs, OID_MSG_DIGEST, &md) != 0 ||
        !tiku_der_is(&md, TIKU_DER_OCTETS)) {
        return say(out, TIKU_SIG_BROKEN, "it signs no digest");
    }
    if (attribute(&s->attrs, OID_SIGN_TIME, &when) == 0) {
        (void)tiku_der_time(&when, &out->signed_at);
    }
    if ((set = malloc(s->attrs.rawlen)) == NULL) {
        return say(out, TIKU_SIG_UNCHECKED, "there is no memory to check it");
    }
    memcpy(set, s->attrs.raw, s->attrs.rawlen);
    /* The signature covers the attributes under a SET tag; the
     * SignerInfo holds them under [0]. */
    set[0] = 0x31;
    over.p = set;
    over.n = s->attrs.rawlen;
    status = sig_check(cert, &s->salg, out->digest, &over, 1, s->sig.body,
                       s->sig.len, out);
    free(set);
    if (status == SIG_MISMATCH) {
        return say(out, TIKU_SIG_BROKEN, "it is not its certificate's key's");
    }
    if (status == TIKU_SIG_GOOD &&
        (md.len != cdl || memcmp(md.body, cd, cdl) != 0)) {
        return say(out, TIKU_SIG_CHANGED, "the content is not what was signed");
    }
    return status;
}

/**
 * @brief Check one SignerInfo @p si of a SignedData over @p content.
 *
 * Finds the signer's certificate in @p certs and digests the content.  With
 * signed attributes, the digest is checked against them and the signature
 * over them; without, the signature is checked over the content.  A
 * timestamp in the unsigned attributes is checked too, at depth 0 only.
 *
 * @return the TIKU_SIG_* status, also stored in out->status.
 */
static int
signer(const tiku_der_t *si, const tiku_der_t *certs,
       const tiku_piece_t *content, int npieces, tiku_cms_sig_t *out,
       int depth)
{
    unsigned char cd[TIKU_HASH_MAX];
    tiku_der_cursor_t c;
    tiku_der_t oid;
    signer_info_t s;
    cms_cert_t cert;
    size_t cdl;
    int status;

    if (signer_read(si, &s) != 0) {
        return say(out, TIKU_SIG_BROKEN, "its signer is not described");
    }
    if (certs->len == 0 || signer_cert(certs, &s.sid, &cert) != 0) {
        return say(out, TIKU_SIG_UNCHECKED, "its certificate is not in it");
    }
    cert_describe(&cert, &out->cert);
    tiku_der_enter(&s.dalg, &c);
    if (tiku_der_take(&c, TIKU_DER_OID, &oid) != 0 ||
        (out->digest = cms_digest(&oid)) == 0 ||
        (cdl = digest_of(out->digest, content, npieces, cd)) == 0) {
        return say(out, TIKU_SIG_UNCHECKED, "its digest is not read");
    }
    if (s.attrs.len != 0) {
        status = signed_attrs(&s, &cert, cd, cdl, out);
    } else if ((status = sig_check(&cert, &s.salg, out->digest, content,
                                   npieces, s.sig.body, s.sig.len, out)) ==
               SIG_MISMATCH) {
        /* With no signed attributes, a changed content and another key
         * fail alike. */
        status = say(out, TIKU_SIG_CHANGED, "the content is not what was "
                     "signed, or not by this certificate's key");
    }
    if (s.unsigned_attrs.len != 0 && depth == 0) {
        signer_stamp(&s.unsigned_attrs, &s.sig, out, depth);
    }
    out->status = status;
    return status;
}

/** @brief SignedData's parts: its content's type, its certificates, its
 *         first signer, and the content it holds (joined, owned). */
typedef struct {
    tiku_der_t     eoid, certs, si;
    unsigned char *inner;           /* NULL when it holds none           */
    long           len;
} signed_data_t;

/** @brief Read ContentInfo @p der, @p n bytes, as SignedData.
 *         @return 0, or -1 with what is wrong in @p out. */
static int
signed_data(const unsigned char *der, size_t n, signed_data_t *sd,
            tiku_cms_sig_t *out)
{
    tiku_der_cursor_t c, enc;
    tiku_der_t e, econ;

    memset(sd, 0, sizeof *sd);
    sd->len = -1;
    if (tiku_der_read(der, n, &e) != 0) {
        return say(out, TIKU_SIG_BROKEN, "it is not a signature"), -1;
    }
    tiku_der_enter(&e, &c);
    if (tiku_der_take(&c, TIKU_DER_OID, &e) != 0 ||
        !tiku_der_oid(&e, OID_SIGNED) || tiku_der_take(&c, 0x80, &e) != 0 ||
        (tiku_der_enter(&e, &c), tiku_der_take(&c, TIKU_DER_SEQUENCE, &e)) ||
        (tiku_der_enter(&e, &c), tiku_der_take(&c, TIKU_DER_INTEGER, &e)) ||
        tiku_der_take(&c, TIKU_DER_SET, &e) != 0 ||
        tiku_der_take(&c, TIKU_DER_SEQUENCE, &econ) != 0) {
        return say(out, TIKU_SIG_BROKEN, "it is not signed data"), -1;
    }
    tiku_der_enter(&econ, &enc);
    if (tiku_der_take(&enc, TIKU_DER_OID, &sd->eoid) != 0) {
        return say(out, TIKU_SIG_BROKEN, "its content is not described"), -1;
    }
    if (tiku_der_take(&enc, 0x80, &e) == 0) {   /* the content it holds */
        tiku_der_enter(&e, &enc);
        if (tiku_der_next(&enc, &e) == 0) {
            sd->len = tiku_der_octets(&e, &sd->inner);
        }
    }
    while (tiku_der_next(&c, &e) == 0 && !tiku_der_is(&e, TIKU_DER_SET)) {
        if (tiku_der_is(&e, 0x80)) sd->certs = e;
    }
    if (!tiku_der_is(&e, TIKU_DER_SET) ||
        (tiku_der_enter(&e, &c), tiku_der_take(&c, TIKU_DER_SEQUENCE,
                                               &sd->si))) {
        return say(out, TIKU_SIG_BROKEN, "it has no signer"), -1;
    }
    return 0;
}

/** @brief tiku_cms_verify(), @p depth tokens deep. */
static int
verify(const unsigned char *der, size_t n, const tiku_piece_t *content,
       int npieces, tiku_cms_sig_t *out, int depth)
{
    signed_data_t sd;
    tiku_piece_t held;
    int status;

    memset(out, 0, sizeof *out);
    if (depth > CMS_DEPTH) {
        return say(out, TIKU_SIG_BROKEN, "signature nesting exceeds the limit");
    }
    if (signed_data(der, n, &sd, out) != 0) {
        free(sd.inner);
        return out->status;
    }
    out->timestamp = tiku_der_oid(&sd.eoid, OID_TSTINFO);
    held.p = sd.inner;
    held.n = sd.len > 0 ? (size_t)sd.len : 0;
    if (sd.len >= 0 && held.n <= sizeof out->econtent) {
        memcpy(out->econtent, sd.inner, held.n);
        out->econtent_len = held.n;
    }
    if (!out->timestamp && npieces > 0) {
        status = signer(&sd.si, &sd.certs, content, npieces, out, depth);
    } else if (sd.len < 0) {
        status = say(out, TIKU_SIG_BROKEN, "it holds no content");
    } else if ((status = signer(&sd.si, &sd.certs, &held, 1, out, depth)) ==
               TIKU_SIG_GOOD && out->timestamp) {
        status = tst_info(sd.inner, held.n, content, npieces, out);
        out->stamp = status;
        snprintf(out->stamp_by, sizeof out->stamp_by, "%s", out->cert.subject);
    }
    free(sd.inner);
    out->status = status;
    return status;
}

int
tiku_cms_verify(const unsigned char *der, size_t n,
                const tiku_piece_t *content, int npieces, tiku_cms_sig_t *out)
{
    return verify(der, n, content, npieces, out, 0);
}

int
tiku_cms_verify_raw(const unsigned char *cert, size_t certlen,
                    const unsigned char *sig, size_t siglen, int alg,
                    const tiku_piece_t *content, int npieces,
                    tiku_cms_sig_t *out)
{
    static const unsigned char rsa_alg[] = { 0x30, 0x0d, 0x06, 0x09, 0x2a,
        0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x01, 0x05, 0x00 };
    cms_cert_t c;
    tiku_der_t salg;

    memset(out, 0, sizeof *out);
    out->digest = alg;
    if (cms_cert_read(cert, certlen, &c) != 0) {
        return say(out, TIKU_SIG_UNCHECKED, "its certificate is not one");
    }
    cert_describe(&c, &out->cert);
    (void)tiku_der_read(rsa_alg, sizeof rsa_alg, &salg);
    out->status = sig_check(&c, &salg, alg, content, npieces, sig, siglen,
                            out);
    if (out->status == SIG_MISMATCH) {
        return say(out, TIKU_SIG_CHANGED, "the content is not what was "
                   "signed, or not by this certificate's key");
    }
    return out->status;
}

/* --------------------------------------------------------- envelopes  */

/** @brief Whether recipient id @p rid names @p key's certificate. */
static int
names_key(const tiku_key_t *key, const tiku_der_t *rid)
{
    tiku_der_cursor_t c;
    tiku_der_t issuer, serial;
    cms_cert_t cert;

    if (key->cert == NULL || cms_cert_read(key->cert, key->cert_len,
                                           &cert) != 0) {
        return 0;
    }
    if (tiku_der_is(rid, 0x80)) {
        return cert.ski != NULL && cert.ski_len == rid->len &&
               !memcmp(cert.ski, rid->body, rid->len);
    }
    tiku_der_enter(rid, &c);
    return tiku_der_take(&c, TIKU_DER_SEQUENCE, &issuer) == 0 &&
           tiku_der_take(&c, TIKU_DER_INTEGER, &serial) == 0 &&
           issuer.rawlen == cert.issuer.rawlen &&
           !memcmp(issuer.raw, cert.issuer.raw, issuer.rawlen) &&
           serial.len == cert.serial.len &&
           !memcmp(serial.body, cert.serial.body, serial.len);
}

/** @brief The content key @p sealed holds, opened with @p key by the
 *         RSA scheme @p alg names, v1.5 or OAEP.  @return its length,
 *         the key in @p out, or -1. */
static long
content_key(const tiku_key_t *key, const tiku_der_t *alg,
            const tiku_der_t *sealed, unsigned char *out, size_t cap)
{
    tiku_der_cursor_t c;
    tiku_der_t oid, params;
    int hash = 0, mgf = 0;
    long salt;

    tiku_der_enter(alg, &c);
    if (tiku_der_take(&c, TIKU_DER_OID, &oid) != 0) {
        return -1;
    }
    if (tiku_der_oid(&oid, OID_RSA_OAEP)) {
        memset(&params, 0, sizeof params);
        (void)tiku_der_next(&c, &params);
        /* OAEP's parameters open with the same two fields as PSS's. */
        pss_params(&params, &hash, &mgf, &salt);
        if (hash == 0 || mgf == 0) return -1;
    } else if (!tiku_der_oid(&oid, OID_RSA)) {
        return -1;
    }
    return tiku_rsa_decrypt(&key->rsa, hash, mgf, sealed->body, sealed->len,
                            out, cap);
}

/** @brief EncryptedContentInfo @p eci deciphered with @p cek.
 *         @return the content's length with @p out to free(), or -1. */
static long
open_content(const tiku_der_t *eci, const unsigned char *cek, size_t klen,
             unsigned char **out)
{
    tiku_der_cursor_t c;
    tiku_der_t e, alg, sealed;
    long n, got;

    *out = NULL;
    tiku_der_enter(eci, &c);
    if (tiku_der_take(&c, TIKU_DER_OID, &e) != 0 ||
        tiku_der_take(&c, TIKU_DER_SEQUENCE, &alg) != 0 ||
        tiku_der_take(&c, 0x80, &sealed) != 0 ||
        (n = tiku_der_octets(&sealed, out)) < 0) {
        return -1;
    }
    got = cms_decipher(&alg, cek, klen, *out, (size_t)n);
    if (got < 0) {
        free(*out);
        *out = NULL;
        return -1;
    }
    return got;
}

/** @brief EnvelopedData @p der's recipients and sealed content.
 *         @return 0, or -1 for what is not an envelope. */
static int
envelope(const unsigned char *der, size_t n, tiku_der_t *ris, tiku_der_t *eci)
{
    tiku_der_cursor_t c;
    tiku_der_t e;

    if (tiku_der_read(der, n, &e) != 0) return -1;
    tiku_der_enter(&e, &c);
    if (tiku_der_take(&c, TIKU_DER_OID, &e) != 0 ||
        !tiku_der_oid(&e, OID_ENVELOPED) || tiku_der_take(&c, 0x80, &e) != 0 ||
        (tiku_der_enter(&e, &c), tiku_der_take(&c, TIKU_DER_SEQUENCE, &e)) ||
        (tiku_der_enter(&e, &c), tiku_der_take(&c, TIKU_DER_INTEGER, &e)) ||
        tiku_der_next(&c, ris) != 0) return -1;
    if (tiku_der_is(ris, 0x80) && tiku_der_next(&c, ris) != 0) return -1;
    return tiku_der_is(ris, TIKU_DER_SET) &&
           tiku_der_take(&c, TIKU_DER_SEQUENCE, eci) == 0 ? 0 : -1;
}

/** @brief The content of @p eci, opened with @p key through recipient
 *         @p ri when its id names the key's certificate (@p named 1) or
 *         does not (@p named 0).  @return the content's length with
 *         @p out to free(), or -1. */
static long
recipient(const tiku_key_t *key, const tiku_der_t *ri, int named,
          const tiku_der_t *eci, unsigned char **out)
{
    unsigned char cek[64];
    tiku_der_cursor_t c;
    tiku_der_t e, rid, alg, sealed;
    long klen, got = -1;

    tiku_der_enter(ri, &c);
    if (tiku_der_take(&c, TIKU_DER_INTEGER, &e) != 0 ||
        tiku_der_next(&c, &rid) != 0 ||
        tiku_der_take(&c, TIKU_DER_SEQUENCE, &alg) != 0 ||
        tiku_der_take(&c, TIKU_DER_OCTETS, &sealed) != 0 ||
        names_key(key, &rid) != named) {
        return -1;
    }
    klen = content_key(key, &alg, &sealed, cek, sizeof cek);
    if (klen > 0) got = open_content(eci, cek, (size_t)klen, out);
    memset(cek, 0, sizeof cek);
    return got;
}

long
tiku_cms_open(const tiku_key_t *key, const unsigned char *der, size_t n,
              unsigned char **out)
{
    tiku_der_cursor_t c;
    tiku_der_t ris, eci, ri;
    long got = -1;
    int pass, tries = 0;

    *out = NULL;
    if (key == NULL || envelope(der, n, &ris, &eci) != 0) {
        return -1;
    }
    /* First the recipients whose id names the key's certificate; then,
     * for a key file without a certificate or a writer that named
     * another, up to CMS_TRIES of the rest. */
    for (pass = 1; pass >= 0 && got < 0; pass--) {
        tiku_der_enter(&ris, &c);
        while (got < 0 && tiku_der_next(&c, &ri) == 0) {
            if (!tiku_der_is(&ri, TIKU_DER_SEQUENCE)) continue;
            if (pass != 1 && tries++ >= CMS_TRIES) break;
            got = recipient(key, &ri, pass, &eci, out);
        }
    }
    return got;
}
