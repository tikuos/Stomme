/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_pdf_crypt.c - the security handlers (ISO 32000-2 7.6): the file
 * key from a password (the standard handler) or from a certificate's
 * private key (Adobe.PubSec), and each object's strings and streams
 * decrypted with it as the object is read.
 *
 * Revisions 2 to 4 hash the password with MD5 and give every object a
 * key of its own for RC4 or AES-128; 5 and 6 use AES-256 with one key,
 * found through SHA-2.  A typed password is tried as the standard hashes
 * it, then as typed.  A document sealed to certificates holds, for each
 * recipient, the seed of its key in an envelope only their key opens.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_pdf_private.h"
#include "tiku_cms.h"
#include "tiku_crypto.h"
#include "tiku_saslprep.h"

#include <stdlib.h>
#include <string.h>

#define PW_MAX 1024                 /* bytes a password may come to      */

enum { CRYPT_NONE, CRYPT_RC4, CRYPT_AESV2, CRYPT_AESV3 };

struct pdf_crypt {
    int           r, keylen, stm, str;  /* revision, key bytes, methods */
    int           metadata;         /* EncryptMetadata                  */
    int           owner;            /* opened by the owner's password   */
    long          perms;            /* P                                */
    long          dict_num;         /* the dictionary: its strings clear */
    unsigned char key[32];
};

/* The 32 bytes a short password is padded with (Algorithm 2, step a). */
static const unsigned char pad32[32] = {
    0x28, 0xbf, 0x4e, 0x5e, 0x4e, 0x75, 0x8a, 0x41, 0x64, 0x00, 0x4e, 0x56,
    0xff, 0xfa, 0x01, 0x08, 0x2e, 0x2e, 0x00, 0xb6, 0xd0, 0x68, 0x3e, 0x80,
    0x2f, 0x0c, 0xa9, 0xfe, 0x64, 0x53, 0x69, 0x7a
};

/** @brief The bytes of string @p o, @p need of them at least, or NULL. */
static const unsigned char *
bytes_of(const pdf_obj_t *o, size_t need)
{
    return o != NULL && o->kind == PDF_STRING && o->u.s.n >= need
           ? o->u.s.p : NULL;
}

/* ---------------------------------------------------- revisions 2-4  */

/** @brief @p pw padded or cut to 32 bytes. */
static void
padded(const unsigned char *pw, size_t n, unsigned char *out)
{
    if (n > 32) n = 32;
    memcpy(out, pw, n);
    memcpy(out + n, pad32, 32 - n);
}

/** @brief Algorithm 2: the file key from user password @p pw. */
static void
key_r4(const pdf_crypt_t *c, const unsigned char *pw, size_t pwn,
       const unsigned char *o, const unsigned char *id, size_t idn,
       unsigned char *key)
{
    unsigned char buf[32], d[16], p[4];
    tiku_md5_t m;
    int i;

    padded(pw, pwn, buf);
    for (i = 0; i < 4; i++) {
        p[i] = (unsigned char)((unsigned long)c->perms >> (8 * i));
    }
    tiku_md5_init(&m);
    tiku_md5_update(&m, buf, 32);
    tiku_md5_update(&m, o, 32);
    tiku_md5_update(&m, p, 4);
    tiku_md5_update(&m, id, idn);
    if (c->r >= 4 && !c->metadata) {
        static const unsigned char ff[4] = {0xff, 0xff, 0xff, 0xff};

        tiku_md5_update(&m, ff, 4);
    }
    tiku_md5_final(&m, d);
    for (i = 0; c->r >= 3 && i < 50; i++) {
        tiku_md5_init(&m);
        tiku_md5_update(&m, d, (size_t)c->keylen);
        tiku_md5_final(&m, d);
    }
    memcpy(key, d, (size_t)c->keylen);
}

/** @brief @p x through RC4 twenty times, the key XORed with the round,
 *         down from 19 (Algorithm 7) or up to 19 (Algorithms 3 and 5). */
static void
rc4_rounds(const unsigned char *key, int n, unsigned char *x, size_t xn,
           int down)
{
    unsigned char k[16];
    tiku_rc4_t r;
    int i, j;

    for (i = 0; i < 20; i++) {
        int round = down ? 19 - i : i;

        for (j = 0; j < n; j++) k[j] = (unsigned char)(key[j] ^ round);
        tiku_rc4_init(&r, k, (size_t)n);
        tiku_rc4_crypt(&r, x, xn);
    }
}

/** @brief Algorithms 4 and 5: whether @p key makes the document's U. */
static int
user_ok_r4(const pdf_crypt_t *c, const unsigned char *key,
           const unsigned char *u, const unsigned char *id, size_t idn)
{
    unsigned char x[32];
    tiku_rc4_t r;
    tiku_md5_t m;

    if (c->r == 2) {
        memcpy(x, pad32, 32);
        tiku_rc4_init(&r, key, (size_t)c->keylen);
        tiku_rc4_crypt(&r, x, 32);
        return memcmp(x, u, 32) == 0;
    }
    tiku_md5_init(&m);
    tiku_md5_update(&m, pad32, 32);
    tiku_md5_update(&m, id, idn);
    tiku_md5_final(&m, x);
    rc4_rounds(key, c->keylen, x, 16, 0);
    return memcmp(x, u, 16) == 0;
}

/** @brief Algorithm 7: the user's padded password out of O, given the
 *         owner's @p pw. */
static void
owner_to_user_r4(const pdf_crypt_t *c, const unsigned char *pw, size_t pwn,
                 const unsigned char *o, unsigned char *user)
{
    unsigned char buf[32], d[16];
    tiku_md5_t m;
    tiku_rc4_t r;
    int i;

    padded(pw, pwn, buf);
    tiku_md5_init(&m);
    tiku_md5_update(&m, buf, 32);
    tiku_md5_final(&m, d);
    for (i = 0; c->r >= 3 && i < 50; i++) {
        tiku_md5_init(&m);
        tiku_md5_update(&m, d, 16);
        tiku_md5_final(&m, d);
    }
    memcpy(user, o, 32);
    if (c->r == 2) {
        tiku_rc4_init(&r, d, (size_t)c->keylen);
        tiku_rc4_crypt(&r, user, 32);
    } else {
        rc4_rounds(d, c->keylen, user, 32, 1);
    }
}

/* ---------------------------------------------------- revisions 5-6  */

/**
 * @brief Algorithm 2.B (revision 6; plain SHA-256 for 5): the hash of
 *        password @p pw with @p salt, and for the owner the 48 bytes of U.
 */
static int
hash_r6(int r, const unsigned char *pw, size_t pwn, const unsigned char *salt,
        const unsigned char *udata, unsigned char out[32])
{
    unsigned char k[64], *k1;
    size_t kn = 32, un = udata != NULL ? 48 : 0, unit, i;
    int round = 0;

    k1 = malloc(64 * (127 + 64 + 48));
    if (k1 == NULL) {
        return -1;
    }
    memcpy(k1, pw, pwn);
    memcpy(k1 + pwn, salt, 8);
    if (un) memcpy(k1 + pwn + 8, udata, un);
    tiku_sha256(k1, pwn + 8 + un, k);
    while (r >= 6) {
        tiku_aes_t aes;
        unsigned char iv[16], last;
        unsigned sum = 0;

        unit = pwn + kn + un;
        for (i = 0; i < 64; i++) {
            memcpy(k1 + i * unit, pw, pwn);
            memcpy(k1 + i * unit + pwn, k, kn);
            if (un) memcpy(k1 + i * unit + pwn + kn, udata, un);
        }
        (void)tiku_aes_init(&aes, k, 128);
        memcpy(iv, k + 16, 16);
        tiku_aes_cbc_encrypt(&aes, iv, k1, unit * 64);
        for (i = 0; i < 16; i++) sum += k1[i];
        last = k1[unit * 64 - 1];
        kn = sum % 3 == 0 ? 32 : sum % 3 == 1 ? 48 : 64;
        if (kn == 32) tiku_sha256(k1, unit * 64, k);
        else if (kn == 48) tiku_sha384(k1, unit * 64, k);
        else tiku_sha512(k1, unit * 64, k);
        round++;
        if (round >= 64 && last <= round - 32) break;
    }
    memcpy(out, k, 32);
    free(k1);
    return 0;
}

/** @brief Whether @p pw opens a revision 5 or 6 document, as its user or
 *         (with @p owner) its owner; the file key into @p key when so. */
static int
open_r6(pdf_crypt_t *c, const unsigned char *pw, size_t pwn,
        const unsigned char *o, const unsigned char *u,
        const unsigned char *oe, const unsigned char *ue, int owner,
        unsigned char *key)
{
    const unsigned char *h = owner ? o : u, *wrapped = owner ? oe : ue;
    unsigned char got[32], ik[32], iv[16];
    tiku_aes_t aes;

    if (pwn > 127) pwn = 127;
    if (hash_r6(c->r, pw, pwn, h + 32, owner ? u : NULL, got) != 0 ||
        memcmp(got, h, 32) != 0 ||
        hash_r6(c->r, pw, pwn, h + 40, owner ? u : NULL, ik) != 0) {
        return 0;
    }
    memcpy(key, wrapped, 32);
    memset(iv, 0, 16);
    (void)tiku_aes_init(&aes, ik, 256);
    tiku_aes_cbc_decrypt(&aes, iv, key, 32);
    return 1;
}

/* ------------------------------------------------------------ opening  */

/** @brief The method a crypt filter named @p name uses (V 4 and 5). */
static int
filter_method(tiku_pdf_t *doc, pdf_obj_t *enc, pdf_obj_t *name)
{
    pdf_obj_t *cf, *cfm;

    if (name == NULL || name->kind != PDF_NAME ||
        pdf_is_name(name, "Identity")) {
        return CRYPT_NONE;          /* absent is Identity too */
    }
    cf = pdf_get(doc, pdf_get(doc, enc, "CF"), (const char *)name->u.s.p);
    cfm = pdf_get(doc, cf, "CFM");
    if (pdf_is_name(cfm, "AESV2")) return CRYPT_AESV2;
    if (pdf_is_name(cfm, "AESV3")) return CRYPT_AESV3;
    if (pdf_is_name(cfm, "V2")) return CRYPT_RC4;
    return CRYPT_NONE;
}

/** @brief Try @p pw on revisions 2 to 4: as the user's, then the owner's. */
static int
open_r4(tiku_pdf_t *doc, pdf_crypt_t *c, pdf_obj_t *enc,
        const unsigned char *pw, size_t pwn)
{
    const unsigned char *o = bytes_of(pdf_get(doc, enc, "O"), 32);
    const unsigned char *u = bytes_of(pdf_get(doc, enc, "U"), 32);
    pdf_obj_t *ids = pdf_get(doc, doc->trailer, "ID");
    pdf_obj_t *id = ids != NULL && ids->kind == PDF_ARRAY && ids->u.a.n > 0
                    ? pdf_resolve(doc, ids->u.a.item[0]) : NULL;
    const unsigned char *idp = bytes_of(id, 0);
    size_t idn = idp != NULL ? id->u.s.n : 0;
    unsigned char user[32];

    if (o == NULL || u == NULL) {
        return 0;
    }
    key_r4(c, pw, pwn, o, idp, idn, c->key);
    if (user_ok_r4(c, c->key, u, idp, idn)) {
        return 1;
    }
    owner_to_user_r4(c, pw, pwn, o, user);
    key_r4(c, user, 32, o, idp, idn, c->key);
    c->owner = user_ok_r4(c, c->key, u, idp, idn);
    return c->owner;
}

/** @brief Revisions 2 to 4: @p password in PDFDocEncoding, as the
 *         standard hashes it (7.6.4.3.1), then its bytes as typed.
 *  @return whether one opened the document. */
static int
open_r4_any(tiku_pdf_t *doc, pdf_crypt_t *c, pdf_obj_t *enc,
            const char *password)
{
    unsigned char pd[PW_MAX];
    long n = pdf_docenc(password, pd, sizeof pd);
    size_t raw = strlen(password);

    if (n >= 0 && open_r4(doc, c, enc, pd, (size_t)n)) {
        return 1;
    }
    return (n < 0 || (size_t)n != raw || memcmp(pd, password, raw) != 0) &&
           open_r4(doc, c, enc, (const unsigned char *)password, raw);
}

/**
 * @brief Revisions 5 and 6: @p password SASLprep'd, as the standard
 *        hashes it (7.6.4.3.3), then as typed for writers that did not
 *        prepare it; each as the user's, then the owner's.
 * @return whether one opened the document.
 */
static int
open_r6_any(tiku_pdf_t *doc, pdf_crypt_t *c, pdf_obj_t *enc,
            const char *password)
{
    const unsigned char *o = bytes_of(pdf_get(doc, enc, "O"), 48);
    const unsigned char *u = bytes_of(pdf_get(doc, enc, "U"), 48);
    const unsigned char *oe = bytes_of(pdf_get(doc, enc, "OE"), 32);
    const unsigned char *ue = bytes_of(pdf_get(doc, enc, "UE"), 32);
    char prep[PW_MAX];
    const char *tries[2];
    int t;

    tries[0] = tiku_saslprep(password, prep, sizeof prep) >= 0 ? prep : NULL;
    tries[1] = tries[0] != NULL && strcmp(prep, password) == 0 ? NULL
                                                               : password;
    for (t = 0; o != NULL && u != NULL && oe != NULL && ue != NULL && t < 2;
         t++) {
        const unsigned char *pw = (const unsigned char *)tries[t];
        size_t n = pw != NULL ? strlen(tries[t]) : 0;

        if (pw != NULL && (open_r6(c, pw, n, o, u, oe, ue, 0, c->key) ||
                           (c->owner = open_r6(c, pw, n, o, u, oe, ue, 1,
                                               c->key)))) {
            return 1;
        }
    }
    return 0;
}

/* ------------------------------------------------------- certificates  */

/** @brief The bytes of one recipient @p r, a string, in @p *p, @p *n.
 *         @return 0, or -1 for what is not one. */
static int
recipient_bytes(tiku_pdf_t *doc, pdf_obj_t *r, const unsigned char **p,
                size_t *n)
{
    r = pdf_resolve(doc, r);
    if (r == NULL || r->kind != PDF_STRING || r->u.s.n == 0) {
        return -1;
    }
    *p = r->u.s.p;
    *n = r->u.s.n;
    return 0;
}

/** @brief Recipient @p i of @p list, an array of them or the one string a
 *         crypt filter may give.  @return 0, or -1 past the last. */
static int
recipient_at(tiku_pdf_t *doc, pdf_obj_t *list, int i,
             const unsigned char **p, size_t *n)
{
    if (list != NULL && list->kind == PDF_ARRAY) {
        return i < list->u.a.n ? recipient_bytes(doc, list->u.a.item[i], p, n)
                               : -1;
    }
    return i == 0 ? recipient_bytes(doc, list, p, n) : -1;
}

/**
 * @brief The seed @p key's envelope among @p list holds (7.6.5.3): 20
 *        bytes, then the four of the permissions, highest first.
 * @return 0 with the seed in @p seed and the permissions in @p perms (all
 *         granted when the envelope gives none), or -1.
 */
static int
pubsec_seed(tiku_pdf_t *doc, pdf_obj_t *list, const tiku_key_t *key,
            unsigned char seed[20], long *perms)
{
    const unsigned char *p;
    unsigned char *open;
    size_t n;
    long got;
    int i;

    for (i = 0; recipient_at(doc, list, i, &p, &n) == 0 && i < 256; i++) {
        if ((got = tiku_cms_open(key, p, n, &open)) >= 20) {
            memcpy(seed, open, 20);
            *perms = got >= 24 ? (long)(int32_t)((uint32_t)open[20] << 24 |
                                                 (uint32_t)open[21] << 16 |
                                                 (uint32_t)open[22] << 8 |
                                                 open[23]) : -1;
            memset(open, 0, (size_t)got);
            free(open);
            return 0;
        }
        free(open);
    }
    return -1;
}

/** @brief The file key from @p seed: the first keylen bytes of the SHA-1
 *         -- SHA-256 for a 32-byte key -- of it, each recipient in @p list,
 *         and four 0xff when @p clear (metadata left clear). */
static void
pubsec_key(tiku_pdf_t *doc, pdf_crypt_t *c, pdf_obj_t *list,
           const unsigned char *seed, int clear)
{
    static const unsigned char ff[4] = { 0xff, 0xff, 0xff, 0xff };
    unsigned char d[32];
    const unsigned char *p;
    tiku_hash_t h;
    size_t n;
    int i;

    (void)tiku_hash_init(&h, c->keylen > 16 ? TIKU_HASH_SHA256
                                            : TIKU_HASH_SHA1);
    tiku_hash_update(&h, seed, 20);
    for (i = 0; recipient_at(doc, list, i, &p, &n) == 0; i++) {
        tiku_hash_update(&h, p, n);
    }
    if (clear) {
        tiku_hash_update(&h, ff, 4);
    }
    tiku_hash_final(&h, d);
    memcpy(c->key, d, (size_t)c->keylen);
    memset(d, 0, sizeof d);
}

/**
 * @brief Adobe.PubSec (7.6.5): the envelope @p key opens gives the seed;
 *        the file key is the SHA-1 -- SHA-256 for AES-256 -- of it, every
 *        recipient's bytes in order and, where metadata is left clear,
 *        four 0xff.  Recipients are the dictionary's (V 1 and 2) or the
 *        crypt filter's.  @return 0; -1 for what is not read; -3 when
 *        @p key is none of the recipients'.
 */
static int
open_pubsec(tiku_pdf_t *doc, pdf_crypt_t *c, pdf_obj_t *enc,
            const tiku_key_t *key, int v)
{
    unsigned char seed[20];
    pdf_obj_t *cf = enc, *list, *meta, *name;

    if (v >= 4) {
        name = pdf_get(doc, enc, c->stm != CRYPT_NONE ? "StmF" : "StrF");
        cf = name != NULL && name->kind == PDF_NAME
             ? pdf_get(doc, pdf_get(doc, enc, "CF"), (const char *)name->u.s.p)
             : NULL;
        if ((meta = pdf_get(doc, cf, "EncryptMetadata")) != NULL &&
            meta->kind == PDF_BOOL) {
            c->metadata = meta->u.b;
        }
    }
    if ((list = pdf_get(doc, cf, "Recipients")) == NULL) {
        return -1;
    }
    if (key == NULL || pubsec_seed(doc, list, key, seed, &c->perms) != 0) {
        return -3;
    }
    pubsec_key(doc, c, list, seed, v >= 4 && !c->metadata);
    memset(seed, 0, sizeof seed);
    return 0;
}

/** @brief A crypt filter's key length for a certificate-sealed document:
 *         AES's own, or RC4's /Length, in bits or (small) in bytes. */
static int
pubsec_keylen(tiku_pdf_t *doc, pdf_obj_t *enc, const pdf_crypt_t *c, int v)
{
    pdf_obj_t *name = pdf_get(doc, enc, c->stm != CRYPT_NONE ? "StmF"
                                                             : "StrF");
    pdf_obj_t *cf = name != NULL && name->kind == PDF_NAME
                    ? pdf_get(doc, pdf_get(doc, enc, "CF"),
                              (const char *)name->u.s.p) : NULL;
    int len = pdf_int(pdf_get(doc, cf != NULL ? cf : enc, "Length"),
                      v == 1 ? 40 : 128);

    if (c->stm == CRYPT_AESV3 || c->str == CRYPT_AESV3) return 32;
    if (c->stm == CRYPT_AESV2 || c->str == CRYPT_AESV2) return 16;
    return len <= 16 ? len : len / 8;
}

int
pdf_crypt_open(tiku_pdf_t *doc, const char *password, const tiku_key_t *key)
{
    pdf_obj_t *raw = pdf_dict_raw(doc->trailer, "Encrypt");
    pdf_obj_t *enc = pdf_resolve(doc, raw), *meta;
    const char *pw = password != NULL ? password : "";
    double perms;
    int v, ok, pubsec;
    pdf_crypt_t *c;

    if (enc == NULL || enc->kind != PDF_DICT) {
        return -1;
    }
    pubsec = pdf_is_name(pdf_get(doc, enc, "Filter"), "Adobe.PubSec");
    if ((!pubsec && !pdf_is_name(pdf_get(doc, enc, "Filter"), "Standard")) ||
        (c = calloc(1, sizeof *c)) == NULL) {
        return -1;
    }
    v = pdf_int(pdf_get(doc, enc, "V"), 0);
    c->r = pdf_int(pdf_get(doc, enc, "R"), 0);
    perms = pdf_number(pdf_get(doc, enc, "P"), 0);   /* signed or not */
    c->perms = perms > -2147483649.0 && perms < 4294967296.0
               ? (long)(int32_t)(uint32_t)(long long)perms : 0;
    c->dict_num = raw->kind == PDF_REF ? raw->u.ref.num : -1;
    meta = pdf_get(doc, enc, "EncryptMetadata");
    c->metadata = meta == NULL || meta->kind != PDF_BOOL || meta->u.b;
    c->keylen = v == 1 ? 5 : pdf_int(pdf_get(doc, enc, "Length"), 40) / 8;
    c->stm = c->str = CRYPT_RC4;
    if (v >= 4) {
        c->stm = filter_method(doc, enc, pdf_get(doc, enc, "StmF"));
        c->str = filter_method(doc, enc, pdf_get(doc, enc, "StrF"));
    }
    if (v == 4) c->keylen = 16;
    if (v == 5) c->keylen = 32;
    if (pubsec) {
        c->keylen = pubsec_keylen(doc, enc, c, v);
        if (v < 1 || v > 5 || v == 3 || c->keylen < 5 || c->keylen > 32 ||
            (c->keylen > 16 && v < 5)) {
            free(c);
            return -1;
        }
        if ((ok = open_pubsec(doc, c, enc, key, v)) != 0) {
            free(c);
            return ok;
        }
        doc->crypt = c;
        return 0;
    }
    if (v < 1 || v > 5 || v == 3 || c->r < 2 || c->r > 6 ||
        c->keylen < 5 || c->keylen > (v < 5 || c->r < 5 ? 16 : 32)) {
        free(c);
        return -1;
    }
    ok = c->r <= 4 ? open_r4_any(doc, c, enc, pw)
                   : open_r6_any(doc, c, enc, pw);
    if (!ok) {
        free(c);
        return -2;
    }
    doc->crypt = c;
    return 0;
}

void
pdf_crypt_free(pdf_crypt_t *c)
{
    free(c);
}

int
pdf_crypt_may_copy(const pdf_crypt_t *c)
{
    return c == NULL || c->owner || (c->perms & 0x10) != 0;
}

int
pdf_crypt_certificate(tiku_pdf_t *doc)
{
    pdf_obj_t *enc = pdf_resolve(doc, pdf_dict_raw(doc->trailer, "Encrypt"));

    return enc != NULL &&
           pdf_is_name(pdf_get(doc, enc, "Filter"), "Adobe.PubSec");
}

/* --------------------------------------------------------- decrypting  */

/** @brief Algorithm 1: object @p num @p gen's own key, for RC4 or AES.
 *  @return its length. */
static int
object_key(const pdf_crypt_t *c, long num, long gen, int aes,
           unsigned char *out)
{
    unsigned char b[5], d[16];
    tiku_md5_t m;
    int n = c->keylen + 5 < 16 ? c->keylen + 5 : 16;

    b[0] = (unsigned char)num;
    b[1] = (unsigned char)(num >> 8);
    b[2] = (unsigned char)(num >> 16);
    b[3] = (unsigned char)gen;
    b[4] = (unsigned char)(gen >> 8);
    tiku_md5_init(&m);
    tiku_md5_update(&m, c->key, (size_t)c->keylen);
    tiku_md5_update(&m, b, 5);
    if (aes) tiku_md5_update(&m, "sAlT", 4);
    tiku_md5_final(&m, d);
    memcpy(out, d, (size_t)n);
    return n;
}

/** @brief @p n bytes decrypted in place by @p method.  @return how many
 *         remain: AES drops its IV and its padding. */
static long
decrypt(const pdf_crypt_t *c, int method, long num, long gen,
        unsigned char *p, long n)
{
    unsigned char k[32], iv[16];
    int kn;
    long body, pad;

    if (method == CRYPT_NONE || n <= 0) {
        return n;
    }
    if (method == CRYPT_AESV3) {
        memcpy(k, c->key, 32);
        kn = 32;
    } else {
        kn = object_key(c, num, gen, method == CRYPT_AESV2, k);
    }
    if (method == CRYPT_RC4) {
        tiku_rc4_t r;

        tiku_rc4_init(&r, k, (size_t)kn);
        tiku_rc4_crypt(&r, p, (size_t)n);
        return n;
    }
    if (n < 16) {
        return 0;
    }
    {
        tiku_aes_t aes;

        (void)tiku_aes_init(&aes, k, kn * 8);
        memcpy(iv, p, 16);
        body = (n - 16) & ~15L;
        tiku_aes_cbc_decrypt(&aes, iv, p + 16, (size_t)body);
    }
    pad = body > 0 ? p[16 + body - 1] : 0;
    if (pad >= 1 && pad <= 16 && pad <= body) {
        body -= pad;
    }
    memmove(p, p + 16, (size_t)body);
    return body;
}

void
pdf_crypt_object(tiku_pdf_t *doc, pdf_obj_t *o, long num, long gen)
{
    const pdf_crypt_t *c = doc->crypt;
    int i;

    if (c == NULL || o == NULL || num == c->dict_num) {
        return;
    }
    switch (o->kind) {
    case PDF_STRING:
        o->u.s.n = (size_t)decrypt(c, c->str, num, gen, o->u.s.p,
                                   (long)o->u.s.n);
        break;
    case PDF_ARRAY:
        for (i = 0; i < o->u.a.n; i++) {
            pdf_crypt_object(doc, o->u.a.item[i], num, gen);
        }
        break;
    case PDF_DICT:
        for (i = 0; i < o->u.d.n; i++) {
            pdf_crypt_object(doc, o->u.d.val[i], num, gen);
        }
        break;
    case PDF_STREAM:
        pdf_crypt_object(doc, o->u.st.dict, num, gen);
        break;
    default:
        break;
    }
}

long
pdf_crypt_stream(tiku_pdf_t *doc, pdf_obj_t *st, unsigned char *data, long n)
{
    const pdf_crypt_t *c = doc->crypt;
    pdf_obj_t *type = pdf_get(doc, st, "Type"), *f = pdf_get(doc, st, "Filter");

    if (c == NULL || st->u.st.num < 0 || pdf_is_name(type, "XRef") ||
        (!c->metadata && pdf_is_name(type, "Metadata"))) {
        return n;
    }
    /* A stream's own Crypt filter first in its chain says Identity. */
    if (f != NULL && f->kind == PDF_ARRAY && f->u.a.n > 0) {
        f = pdf_resolve(doc, f->u.a.item[0]);
    }
    if (pdf_is_name(f, "Crypt")) {
        return n;
    }
    return decrypt(c, c->stm, st->u.st.num, st->u.st.gen, data, n);
}
