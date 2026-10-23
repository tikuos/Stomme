/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_pubkey.h - RSA (RFC 8017) and ECDSA (SEC 1, FIPS 186-4) over big
 * numbers written here: checking a signature, and opening what was sealed
 * to a key.  Nothing here is constant-time: how long a private-key
 * operation takes depends on the key.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_PUBKEY_H_
#define TIKU_PUBKEY_H_

#include <stddef.h>

/** @brief A number as a key file writes it: big-endian bytes. */
typedef struct {
    const unsigned char *b;
    size_t               n;         /* 0 when the key has none          */
} tiku_num_t;

/** @brief An RSA key: its modulus and public exponent; for a private
 *         key its private exponent, and the primes with their CRT numbers
 *         when the file carries them. */
typedef struct {
    tiku_num_t n, e, d, p, q, dp, dq, qi;
} tiku_rsa_key_t;

#define TIKU_RSA_MAX_BITS 8192      /* the longest modulus accepted     */

/** @brief Whether @p sig is @p key's RSASSA-PKCS1-v1_5 signature of the
 *         TIKU_HASH_* @p alg digest @p digest (RFC 8017 8.2.2), with the
 *         digest's NULL parameters there or left out. */
int tiku_rsa_verify(const tiku_rsa_key_t *key, int alg,
                    const unsigned char *digest, const unsigned char *sig,
                    size_t siglen);

/** @brief Whether @p sig is @p key's RSASSA-PSS signature (RFC 8017
 *         8.1.2) of the TIKU_HASH_* @p alg digest @p digest, its mask made
 *         with MGF1 over @p mgf, with @p salt bytes of salt, or any number
 *         of them for -1. */
int tiku_rsa_verify_pss(const tiku_rsa_key_t *key, int alg, int mgf,
                        long salt, const unsigned char *digest,
                        const unsigned char *sig, size_t siglen);

/**
 * @brief What @p in says, sealed to @p key: RSAES-PKCS1-v1_5 (RFC 8017
 *        7.2.2) when @p oaep is 0; RSAES-OAEP (7.1.2) with an empty label
 *        when it names a TIKU_HASH_* digest, the mask made with MGF1 over
 *        @p mgf.  The primes are used when the key has them.
 * @return the message's length, copied to @p out, or -1, also for a
 *         message longer than @p cap.
 */
long tiku_rsa_decrypt(const tiku_rsa_key_t *key, int oaep, int mgf,
                      const unsigned char *in, size_t inlen,
                      unsigned char *out, size_t cap);

/** @brief The curves an ECDSA signature is checked on. */
enum { TIKU_EC_P256 = 1, TIKU_EC_P384, TIKU_EC_P521, TIKU_EC_BP256,
       TIKU_EC_BP384, TIKU_EC_BP512 };

/**
 * @brief Whether (@p r, @p s) is the ECDSA signature (SEC 1 4.1.4) of
 *        @p digest, @p dlen bytes, by the public point @p q on @p curve:
 *        04, x and y, or 02 or 03 and x (SEC 1 2.3.3).
 */
int tiku_ecdsa_verify(int curve, const unsigned char *q, size_t qlen,
                      const unsigned char *digest, size_t dlen,
                      const tiku_num_t *r, const tiku_num_t *s);

#endif /* TIKU_PUBKEY_H_ */
