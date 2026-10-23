/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cms.h - what keys and signatures travel in: key files (PKCS#12,
 * RFC 7292; PKCS#8, RFC 5958 and 8018, and PKCS#1, as DER or PEM), X.509
 * certificates (RFC 5280), and CMS (RFC 5652): envelopes opened with a
 * key file's key, and signatures checked against the certificate they
 * carry, dated by the timestamp tokens (RFC 3161) they may carry too.
 *
 * TIKU_SIG_GOOD means the content is what the key in the signer's
 * certificate signed; nothing here checks whether that certificate is
 * trusted.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_CMS_H_
#define TIKU_CMS_H_

#include <stddef.h>
#include <stdint.h>

/* ----------------------------------------------------------- key files  */

/** @brief How tiku_key_read() ended: TIKU_KEY_OK, or why the file did
 *         not open. */
enum { TIKU_KEY_OK = 0, TIKU_KEY_PASSWORD, TIKU_KEY_BAD,
       TIKU_KEY_UNSUPPORTED };

/** @brief A private key read from a key file, with its certificate. */
typedef struct tiku_key tiku_key_t;

/**
 * @brief Read a key file: PKCS#12 (.p12, .pfx), or PKCS#8 (encrypted or
 *        not) or PKCS#1, as DER or PEM, unlocked by @p password (UTF-8).
 * @return TIKU_KEY_OK with @p out set; TIKU_KEY_PASSWORD when the password
 *         does not open it; TIKU_KEY_BAD for what is not a key file;
 *         TIKU_KEY_UNSUPPORTED for a key or cipher this does not read.
 */
int tiku_key_read(const void *data, size_t n, const char *password,
                  tiku_key_t **out);

/** @brief Wipe the key's numbers and free it; NULL is ignored. */
void tiku_key_free(tiku_key_t *key);

/** @brief The name on the key's certificate, "" when the file has none. */
const char *tiku_key_name(const tiku_key_t *key);

/**
 * @brief The content of CMS EnvelopedData @p der, opened with @p key: by
 *        the recipient whose id names the key's certificate, else by
 *        trying the other RSA recipients in turn.  The content may be
 *        sealed with triple DES, RC2, DES or AES.
 * @return the content's length with @p out to free(), or -1 when the key
 *         is not one it was sealed to, or it is not an envelope.
 */
long tiku_cms_open(const tiku_key_t *key, const unsigned char *der,
                   size_t n, unsigned char **out);

/* -------------------------------------------------------- certificates  */

/** @brief What a certificate says of itself, as a person reads it. */
typedef struct {
    char    subject[160];           /* common name, else org, unit, email */
    char    org[160];               /* its organisation, "" for none     */
    char    email[96];
    char    issuer[160];            /* common name, else org, unit, email */
    int64_t not_before, not_after;  /* seconds since 1970, UTC           */
    int     self_signed;            /* issuer and subject are one name   */
} tiku_cert_info_t;

/** @brief Read what the DER certificate @p der says of itself into
 *         @p out.  @return 0, or -1 with @p out zeroed. */
int tiku_cert_info(const unsigned char *der, size_t n,
                   tiku_cert_info_t *out);

/* ---------------------------------------------------------- signatures  */

/** @brief How a signature stands. */
enum { TIKU_SIG_GOOD = 1,           /* what the certificate's key signed */
       TIKU_SIG_CHANGED,            /* the content is not what was signed */
       TIKU_SIG_BROKEN,             /* malformed, or not by its key      */
       TIKU_SIG_UNCHECKED };        /* could not be checked              */

/** @brief One run of the content a signature covers. */
typedef struct {
    const unsigned char *p;
    size_t               n;
} tiku_piece_t;

/** @brief A signature, as checked. */
typedef struct {
    int              status;        /* TIKU_SIG_*                        */
    char             why[96];       /* what was wrong, "" when nothing   */
    tiku_cert_info_t cert;          /* the signer's certificate          */
    int              digest;        /* TIKU_HASH_* of the content        */
    int64_t          signed_at;     /* the signer's own claim, or 0      */
    int              stamp;         /* TIKU_SIG_* of a timestamp, 0 none */
    int64_t          stamped_at;    /* the time it gives                 */
    char             stamp_by[160]; /* the authority that gave it        */
    int              timestamp;     /* the signature is a timestamp token */
    unsigned char    econtent[64];  /* the content it holds, if it fits  */
    size_t           econtent_len;  /* bytes in econtent                 */
} tiku_cms_sig_t;

/**
 * @brief Check CMS SignedData @p der, whose content is the @p npieces
 *        runs of @p content (detached), or, with none, the content it
 *        holds.  A timestamp token is checked as one: its imprint against
 *        @p content.  Content it holds that fits in econtent is copied
 *        there.
 * @return @p out->status.
 */
int tiku_cms_verify(const unsigned char *der, size_t n,
                    const tiku_piece_t *content, int npieces,
                    tiku_cms_sig_t *out);

/** @brief Check a bare RSA signature @p sig (PKCS#1 v1.5) of the @p alg
 *         digest of @p content by the DER certificate @p cert.
 *         @return the status. */
int tiku_cms_verify_raw(const unsigned char *cert, size_t certlen,
                        const unsigned char *sig, size_t siglen, int alg,
                        const tiku_piece_t *content, int npieces,
                        tiku_cms_sig_t *out);

#endif /* TIKU_CMS_H_ */
