/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_crypto.h - the digests and ciphers an encrypted or signed
 * document needs: MD5, SHA-1 and SHA-2, HMAC and PBKDF2, CRC-32, RC4,
 * RC2, DES and triple DES, and AES, written here from their standards.
 *
 * Nothing here is constant-time: its running time depends on the keys and
 * data it is given.  MD5, RC4, RC2 and DES are broken; they are here to
 * read what others encrypted or signed, such as a PDF's security
 * handlers, a key file or a signature.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_CRYPTO_H_
#define TIKU_CRYPTO_H_

#include <stddef.h>
#include <stdint.h>

/** @brief MD5 (RFC 1321), fed in pieces. */
typedef struct {
    uint32_t      s[4];
    uint64_t      n;                /* bytes fed                       */
    unsigned char buf[64];
} tiku_md5_t;

/** @brief Start an MD5 digest in @p c. */
void tiku_md5_init(tiku_md5_t *c);
/** @brief Feed @p n bytes at @p data to the digest. */
void tiku_md5_update(tiku_md5_t *c, const void *data, size_t n);
/** @brief Finish the digest into the 16 bytes at @p out. */
void tiku_md5_final(tiku_md5_t *c, unsigned char out[16]);

/** @brief CRC-32 (ISO 3309, as ZIP, gzip and PNG use it) of @p n bytes,
 *         carried on from @p crc: start from 0, feed it in pieces. */
uint32_t tiku_crc32(uint32_t crc, const void *data, size_t n);

/** @brief SHA-256 (FIPS 180-4) of @p n bytes at @p data, all at once. */
void tiku_sha256(const void *data, size_t n, unsigned char out[32]);
/** @brief SHA-384 (FIPS 180-4) of @p n bytes at @p data, all at once. */
void tiku_sha384(const void *data, size_t n, unsigned char out[48]);
/** @brief SHA-512 (FIPS 180-4) of @p n bytes at @p data, all at once. */
void tiku_sha512(const void *data, size_t n, unsigned char out[64]);

/** @brief The digests a key file or a signature can name. */
enum { TIKU_HASH_MD5 = 1, TIKU_HASH_SHA1, TIKU_HASH_SHA224,
       TIKU_HASH_SHA256, TIKU_HASH_SHA384, TIKU_HASH_SHA512 };
#define TIKU_HASH_MAX 64            /* the longest digest, in bytes    */

/** @brief A digest of any TIKU_HASH_* kind, fed in pieces. */
typedef struct {
    int           alg;              /* TIKU_HASH_*; 0 when none        */
    uint64_t      n;                /* bytes fed                       */
    union { uint32_t w[8]; uint64_t d[8]; } s;
    unsigned char buf[128];
    tiku_md5_t    md5;
} tiku_hash_t;

/** @brief Start digest @p alg.  @return its length in bytes, or 0 when
 *         there is no such digest; tiku_hash_update() and
 *         tiku_hash_final() then do nothing. */
size_t tiku_hash_init(tiku_hash_t *h, int alg);
/** @brief Feed @p n bytes at @p data to the digest. */
void tiku_hash_update(tiku_hash_t *h, const void *data, size_t n);
/** @brief Finish into @p out as many bytes as tiku_hash_init() returned. */
void tiku_hash_final(tiku_hash_t *h, unsigned char *out);
/** @brief Digest @p alg of @p n bytes at @p data into @p out, all at
 *         once.  @return the digest's length, or 0 when there is no such
 *         digest. */
size_t tiku_hash(int alg, const void *data, size_t n, unsigned char *out);

/** @brief HMAC (RFC 2104): the inner and outer digests, each already fed
 *         its keyed block, so a copy of a keyed one starts another message
 *         without hashing the key again. */
typedef struct {
    tiku_hash_t inner, outer;
} tiku_hmac_t;

/** @brief Key an HMAC over digest @p alg.  @return its length, or 0. */
size_t tiku_hmac_init(tiku_hmac_t *m, int alg, const void *key, size_t n);
/** @brief Feed @p n bytes at @p data to the HMAC. */
void tiku_hmac_update(tiku_hmac_t *m, const void *data, size_t n);
/** @brief Finish the HMAC into @p out: as many bytes as tiku_hmac_init()
 *         returned. */
void tiku_hmac_final(tiku_hmac_t *m, unsigned char *out);
/** @brief The HMAC of @p n bytes at once.  @return its length, or 0. */
size_t tiku_hmac(int alg, const void *key, size_t klen, const void *data,
                 size_t n, unsigned char *out);

/** @brief PBKDF2 (RFC 8018): @p outlen bytes from a password and salt,
 *         HMAC over @p alg @p rounds times.  @return 0, or -1 for no
 *         such digest or no rounds. */
int tiku_pbkdf2(int alg, const void *pw, size_t pwlen, const void *salt,
                size_t slen, unsigned long rounds, unsigned char *out,
                size_t outlen);

/** @brief RC4's state: the byte permutation and its two indices. */
typedef struct {
    unsigned char s[256];
    unsigned char i, j;
} tiku_rc4_t;

/** @brief The key schedule for @p n bytes of @p key. */
void tiku_rc4_init(tiku_rc4_t *c, const unsigned char *key, size_t n);
/** @brief XOR @p n bytes of @p data with the stream, in place, going on
 *         from where the last call stopped. */
void tiku_rc4_crypt(tiku_rc4_t *c, unsigned char *data, size_t n);

/** @brief AES (FIPS 197): a key's round keys and the tables they use. */
typedef struct {
    unsigned char rk[240];          /* room for 15 round keys of 16 bytes */
    int           rounds;
    unsigned char sbox[256], inv[256];
} tiku_aes_t;

/** @brief Round keys for a @p bits-bit key (128, 192 or 256).
 *         @return 0, or -1 for any other length. */
int tiku_aes_init(tiku_aes_t *c, const unsigned char *key, int bits);
/** @brief One 16-byte block enciphered; in and out may be the same. */
void tiku_aes_encrypt(const tiku_aes_t *c, const unsigned char in[16],
                      unsigned char out[16]);
/** @brief One 16-byte block deciphered; in and out may be the same. */
void tiku_aes_decrypt(const tiku_aes_t *c, const unsigned char in[16],
                      unsigned char out[16]);
/** @brief CBC encryption over @p n bytes in place, the chain carried in
 *         @p iv from one call to the next; bytes past the last whole
 *         block are left as they are. */
void tiku_aes_cbc_encrypt(const tiku_aes_t *c, unsigned char iv[16],
                          unsigned char *data, size_t n);
/** @brief CBC decryption over @p n bytes in place, the chain carried in
 *         @p iv from one call to the next; bytes past the last whole
 *         block are left as they are. */
void tiku_aes_cbc_decrypt(const tiku_aes_t *c, unsigned char iv[16],
                          unsigned char *data, size_t n);

/** @brief DES (FIPS 46-3), and three of it one after another (EDE, SP
 *         800-67): each key's sixteen round keys, 48 bits each. */
typedef struct {
    uint64_t k[3][16];
} tiku_des3_t;

/** @brief Round keys for @p n bytes of @p key: 24 for three keys, 16 for
 *         two (the first again as the third), 8 for DES alone.
 *         @return 0, or -1 for any other length. */
int tiku_des3_init(tiku_des3_t *c, const unsigned char *key, size_t n);
/** @brief One 8-byte block enciphered: E(K3, D(K2, E(K1, in))); in and
 *         out may be the same. */
void tiku_des3_encrypt(const tiku_des3_t *c, const unsigned char in[8],
                       unsigned char out[8]);
/** @brief One 8-byte block deciphered: D(K1, E(K2, D(K3, in))); in and
 *         out may be the same. */
void tiku_des3_decrypt(const tiku_des3_t *c, const unsigned char in[8],
                       unsigned char out[8]);

/** @brief RC2 (RFC 2268): the expanded key, 64 words. */
typedef struct {
    uint16_t k[64];
} tiku_rc2_t;

/** @brief The expanded key for @p n bytes of @p key (1 to 128), of which
 *         @p bits are effective (1 to 1024).  @return 0, or -1. */
int tiku_rc2_init(tiku_rc2_t *c, const unsigned char *key, size_t n,
                  int bits);
/** @brief One 8-byte block enciphered; in and out may be the same. */
void tiku_rc2_encrypt(const tiku_rc2_t *c, const unsigned char in[8],
                      unsigned char out[8]);
/** @brief One 8-byte block deciphered; in and out may be the same. */
void tiku_rc2_decrypt(const tiku_rc2_t *c, const unsigned char in[8],
                      unsigned char out[8]);

#endif /* TIKU_CRYPTO_H_ */
