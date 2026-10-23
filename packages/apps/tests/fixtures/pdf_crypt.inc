/* Encrypted PDFs as a writer makes them: RC4 (revisions 2 and 3), AES-128
 * (4) and AES-256 (6), a page saying "Secret text" titled "Hidden title".
 * Shared by the kit's test and the viewer's.
 * SPDX-License-Identifier: Apache-2.0 */
#include <stdlib.h>
#include <string.h>

#include "tiku_crypto.h"

/** @brief How a fixture is encrypted. */
typedef struct {
    const char *name;
    int         v, r, bits, aes;
    const char *user, *owner;
    long        p;                  /* -4 allows all; -20 forbids copying */
} enc_t;

static const unsigned char enc_pad32[32] = {
    0x28, 0xbf, 0x4e, 0x5e, 0x4e, 0x75, 0x8a, 0x41, 0x64, 0x00, 0x4e, 0x56,
    0xff, 0xfa, 0x01, 0x08, 0x2e, 0x2e, 0x00, 0xb6, 0xd0, 0x68, 0x3e, 0x80,
    0x2f, 0x0c, 0xa9, 0xfe, 0x64, 0x53, 0x69, 0x7a
};
static const unsigned char enc_file_id[17] = "tikuos-fixture!!";

/** @brief @p pw padded to 32 bytes. */
static void
enc_padded(const char *pw, unsigned char *out)
{
    size_t n = strlen(pw) > 32 ? 32 : strlen(pw);

    memcpy(out, pw, n);
    memcpy(out + n, enc_pad32, 32 - n);
}

/** @brief @p x through RC4 twenty times up, the key XORed with 0..19. */
static void
enc_rc4_up(const unsigned char *key, int n, unsigned char *x, size_t xn)
{
    unsigned char k[16];
    tiku_rc4_t r;
    int i, j;

    for (i = 0; i < 20; i++) {
        for (j = 0; j < n; j++) k[j] = (unsigned char)(key[j] ^ i);
        tiku_rc4_init(&r, k, (size_t)n);
        tiku_rc4_crypt(&r, x, xn);
    }
}

/** @brief Revisions 2 to 4: O (Algorithm 3), the key (2) and U (4, 5). */
static void
enc_keys_r4(const enc_t *e, unsigned char *o, unsigned char *u,
        unsigned char *key)
{
    unsigned char buf[32], d[16], p[4];
    int n = e->bits / 8, i;
    tiku_md5_t m;
    tiku_rc4_t r;

    enc_padded(e->owner, buf);
    tiku_md5_init(&m);
    tiku_md5_update(&m, buf, 32);
    tiku_md5_final(&m, d);
    for (i = 0; e->r >= 3 && i < 50; i++) {
        tiku_md5_init(&m);
        tiku_md5_update(&m, d, 16);
        tiku_md5_final(&m, d);
    }
    enc_padded(e->user, o);
    if (e->r == 2) {
        tiku_rc4_init(&r, d, (size_t)n);
        tiku_rc4_crypt(&r, o, 32);
    } else {
        enc_rc4_up(d, n, o, 32);
    }
    for (i = 0; i < 4; i++) {
        p[i] = (unsigned char)((unsigned long)e->p >> (8 * i));
    }
    enc_padded(e->user, buf);
    tiku_md5_init(&m);
    tiku_md5_update(&m, buf, 32);
    tiku_md5_update(&m, o, 32);
    tiku_md5_update(&m, p, 4);
    tiku_md5_update(&m, enc_file_id, 16);
    tiku_md5_final(&m, d);
    for (i = 0; e->r >= 3 && i < 50; i++) {
        tiku_md5_init(&m);
        tiku_md5_update(&m, d, (size_t)n);
        tiku_md5_final(&m, d);
    }
    memcpy(key, d, (size_t)n);
    memset(u, 0, 32);
    if (e->r == 2) {
        memcpy(u, enc_pad32, 32);
        tiku_rc4_init(&r, key, (size_t)n);
        tiku_rc4_crypt(&r, u, 32);
        return;
    }
    tiku_md5_init(&m);
    tiku_md5_update(&m, enc_pad32, 32);
    tiku_md5_update(&m, enc_file_id, 16);
    tiku_md5_final(&m, u);
    enc_rc4_up(key, n, u, 16);
}

/** @brief Algorithm 2.B: revision 6's hash of @p pw with @p salt. */
static void
enc_hash_r6(const char *pw, const unsigned char *salt, const unsigned char *u,
        unsigned char out[32])
{
    static unsigned char k1[64 * (127 + 64 + 48)];
    size_t pwn = strlen(pw), un = u != NULL ? 48 : 0, kn = 32, unit, i;
    unsigned char k[64];
    int round = 0;

    memcpy(k1, pw, pwn);
    memcpy(k1 + pwn, salt, 8);
    if (un) memcpy(k1 + pwn + 8, u, un);
    tiku_sha256(k1, pwn + 8 + un, k);
    for (;;) {
        tiku_aes_t aes;
        unsigned char iv[16], last;
        unsigned sum = 0;

        unit = pwn + kn + un;
        for (i = 0; i < 64; i++) {
            memcpy(k1 + i * unit, pw, pwn);
            memcpy(k1 + i * unit + pwn, k, kn);
            if (un) memcpy(k1 + i * unit + pwn + kn, u, un);
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
        if (++round >= 64 && last <= round - 32) break;
    }
    memcpy(out, k, 32);
}

/** @brief Revision 6: U, UE, O and OE around file key @p key. */
static void
enc_keys_r6(const enc_t *e, const unsigned char *key, unsigned char *u,
        unsigned char *ue, unsigned char *o, unsigned char *oe)
{
    unsigned char ik[32], iv[16];
    tiku_aes_t aes;

    memcpy(u + 32, "uvsalt..ukeysalt", 16);
    enc_hash_r6(e->user, u + 32, NULL, u);
    enc_hash_r6(e->user, u + 40, NULL, ik);
    memcpy(ue, key, 32);
    memset(iv, 0, 16);
    (void)tiku_aes_init(&aes, ik, 256);
    tiku_aes_cbc_encrypt(&aes, iv, ue, 32);
    memcpy(o + 32, "ovsalt..okeysalt", 16);
    enc_hash_r6(e->owner, o + 32, u, o);
    enc_hash_r6(e->owner, o + 40, u, ik);
    memcpy(oe, key, 32);
    memset(iv, 0, 16);
    (void)tiku_aes_init(&aes, ik, 256);
    tiku_aes_cbc_encrypt(&aes, iv, oe, 32);
}

/** @brief Object @p num's @p n bytes encrypted as a writer would: RC4,
 *         or AES with an IV before and padding after.  @return length. */
static long
enc_seal(const enc_t *e, const unsigned char *key, long num,
     const unsigned char *in, long n, unsigned char *out)
{
    unsigned char k[32], b[5] = {(unsigned char)num, (unsigned char)(num >> 8),
                                 (unsigned char)(num >> 16), 0, 0};
    int kn = e->bits / 8 + 5 < 16 ? e->bits / 8 + 5 : 16, pad, i;
    tiku_md5_t m;

    if (e->r >= 5) {
        memcpy(k, key, 32);
        kn = 32;
    } else {
        tiku_md5_init(&m);
        tiku_md5_update(&m, key, (size_t)(e->bits / 8));
        tiku_md5_update(&m, b, 5);
        if (e->aes) tiku_md5_update(&m, "sAlT", 4);
        tiku_md5_final(&m, k);
    }
    if (!e->aes) {
        tiku_rc4_t r;

        memcpy(out, in, (size_t)n);
        tiku_rc4_init(&r, k, (size_t)kn);
        tiku_rc4_crypt(&r, out, (size_t)n);
        return n;
    }
    {
        tiku_aes_t aes;
        unsigned char iv[16];

        for (i = 0; i < 16; i++) out[i] = (unsigned char)(0xa0 + i);
        memcpy(iv, out, 16);
        memcpy(out + 16, in, (size_t)n);
        pad = 16 - (int)(n % 16);
        for (i = 0; i < pad; i++) out[16 + n + i] = (unsigned char)pad;
        (void)tiku_aes_init(&aes, k, kn * 8);
        tiku_aes_cbc_encrypt(&aes, iv, out + 16, (size_t)(n + pad));
        return 16 + n + pad;
    }
}

/* ------------------------------------------------------------ writing  */

typedef struct { char *p; long n, cap, off[16]; } encw_t;

/** @brief Append @p n bytes. */
static void
enc_put(encw_t *b, const void *s, long n)
{
    if (b->n + n + 1 > b->cap) {
        b->cap = (b->n + n + 1) * 2;
        b->p = realloc(b->p, (size_t)b->cap);
    }
    memcpy(b->p + b->n, s, (size_t)n);
    b->n += n;
}

/** @brief Append the text @p s. */
static void
enc_puts(encw_t *b, const char *s)
{
    enc_put(b, s, (long)strlen(s));
}

/** @brief Append @p n bytes as a hex string. */
static void
enc_hex(encw_t *b, const unsigned char *p, long n)
{
    char two[3];
    long i;

    enc_puts(b, "<");
    for (i = 0; i < n; i++) {
        snprintf(two, sizeof two, "%02x", p[i]);
        enc_puts(b, two);
    }
    enc_puts(b, ">");
}

/** @brief One page saying "Secret text", its title "Hidden title", both
 *         encrypted as @p e says. */
static encw_t
enc_make(const enc_t *e)
{
    static const char text[] = "BT /F1 12 Tf 72 700 Td (Secret text) Tj ET";
    unsigned char key[32], o[48], u[48], oe[32], ue[32], sealed[128];
    encw_t b = {0};
    char line[160];
    long n, xref;
    int i;

    for (i = 0; i < 32; i++) key[i] = (unsigned char)(i * 13 + 5);
    if (e->r >= 5) enc_keys_r6(e, key, u, ue, o, oe);
    else enc_keys_r4(e, o, u, key);
    enc_puts(&b, "%PDF-1.7\n");
    b.off[1] = b.n;
    enc_puts(&b, "1 0 obj << /Type /Catalog /Pages 2 0 R >> endobj\n");
    b.off[2] = b.n;
    enc_puts(&b, "2 0 obj << /Type /Pages /Kids [3 0 R] /Count 1 >> endobj\n");
    b.off[3] = b.n;
    enc_puts(&b, "3 0 obj << /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] "
              "/Resources << /Font << /F1 << /Type /Font /Subtype /Type1 "
              "/BaseFont /Helvetica >> >> >> /Contents 4 0 R >> endobj\n");
    n = enc_seal(e, key, 4, (const unsigned char *)text, (long)strlen(text),
             sealed);
    b.off[4] = b.n;
    snprintf(line, sizeof line, "4 0 obj << /Length %ld >>\nstream\n", n);
    enc_puts(&b, line);
    enc_put(&b, sealed, n);
    enc_puts(&b, "\nendstream endobj\n");
    n = enc_seal(e, key, 5, (const unsigned char *)"Hidden title", 12, sealed);
    b.off[5] = b.n;
    enc_puts(&b, "5 0 obj << /Title ");
    enc_hex(&b, sealed, n);
    enc_puts(&b, " >> endobj\n");
    b.off[6] = b.n;
    snprintf(line, sizeof line, "6 0 obj << /Filter /Standard /V %d /R %d "
             "/Length %d /P %ld /O ", e->v, e->r, e->bits, e->p);
    enc_puts(&b, line);
    enc_hex(&b, o, e->r >= 5 ? 48 : 32);
    enc_puts(&b, " /U ");
    enc_hex(&b, u, e->r >= 5 ? 48 : 32);
    if (e->v >= 4) {
        snprintf(line, sizeof line, " /CF << /StdCF << /CFM /%s /Length %d "
                 "/AuthEvent /DocOpen >> >> /StmF /StdCF /StrF /StdCF",
                 e->r >= 5 ? "AESV3" : "AESV2", e->bits / 8);
        enc_puts(&b, line);
    }
    if (e->r >= 5) {
        enc_puts(&b, " /OE ");
        enc_hex(&b, oe, 32);
        enc_puts(&b, " /UE ");
        enc_hex(&b, ue, 32);
    }
    enc_puts(&b, " >> endobj\n");
    xref = b.n;
    enc_puts(&b, "xref\n0 7\n0000000000 65535 f \n");
    for (i = 1; i < 7; i++) {
        snprintf(line, sizeof line, "%010ld 00000 n \n", b.off[i]);
        enc_puts(&b, line);
    }
    enc_puts(&b, "trailer << /Size 7 /Root 1 0 R /Info 5 0 R /Encrypt 6 0 R "
              "/ID [");
    enc_hex(&b, enc_file_id, 16);
    enc_hex(&b, enc_file_id, 16);
    snprintf(line, sizeof line, "] >>\nstartxref\n%ld\n%%%%EOF\n", xref);
    enc_puts(&b, line);
    return b;
}
