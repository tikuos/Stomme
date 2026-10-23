/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_saslprep.c - SASLprep (RFC 4013) over the tables that
 * kits/support/tools/gensaslprep.py bakes from Unicode 3.2 at build time:
 * map, normalize as NFKC (decompose, put each starter's marks in order,
 * compose), prohibit, and check directions (RFC 3454 section 6).
 *
 * Hangul syllables come apart and together by rule (UAX 15), the rest
 * by table.  Each character is looked up by binary search as it comes,
 * and nothing is kept between calls.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_saslprep.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "tiku_saslprep_tables.h"

#define IN_MAX   1024               /* code points a password may have   */
#define GROW_MAX 18                 /* the most code points one has in NFKD */

/* Hangul syllables by rule (Unicode 3.2, section 3.12). */
#define S_BASE  0xAC00u
#define L_BASE  0x1100u
#define V_BASE  0x1161u
#define T_BASE  0x11A7u
#define L_COUNT 19u
#define V_COUNT 21u
#define T_COUNT 28u
#define N_COUNT (V_COUNT * T_COUNT)
#define S_COUNT (L_COUNT * N_COUNT)

/** @brief Whether @p cp is in one of the @p n sorted ranges @p r. */
static int
in_ranges(const uint32_t (*r)[2], size_t n, uint32_t cp)
{
    size_t lo = 0, hi = n;

    while (lo < hi) {
        size_t mid = (lo + hi) / 2;

        if (cp < r[mid][0]) {
            hi = mid;
        } else if (cp > r[mid][1]) {
            lo = mid + 1;
        } else {
            return 1;
        }
    }
    return 0;
}

#define IN(t, cp) in_ranges((t), sizeof (t) / sizeof (t)[0], (cp))

/** @brief The canonical combining class of @p cp; 0 for a starter. */
static int
ccc(uint32_t cp)
{
    size_t lo = 0, hi = sizeof sp_ccc / sizeof sp_ccc[0];

    while (lo < hi) {
        size_t mid = (lo + hi) / 2;

        if (cp < sp_ccc[mid][0]) {
            hi = mid;
        } else if (cp > sp_ccc[mid][1]) {
            lo = mid + 1;
        } else {
            return (int)sp_ccc[mid][2];
        }
    }
    return 0;
}

/**
 * @brief @p cp's full compatibility decomposition (NFKD) put at @p out:
 *        a Hangul syllable's by rule, another's by table, or @p cp.
 * @return how many code points.
 */
static int
decompose(uint32_t cp, uint32_t *out)
{
    size_t lo = 0, hi = sizeof sp_dec / sizeof sp_dec[0];

    if (cp >= S_BASE && cp < S_BASE + S_COUNT) {
        uint32_t s = cp - S_BASE;

        out[0] = L_BASE + s / N_COUNT;
        out[1] = V_BASE + s % N_COUNT / T_COUNT;
        out[2] = T_BASE + s % T_COUNT;
        return s % T_COUNT ? 3 : 2;
    }
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        uint32_t key = sp_dec[mid] >> 5;

        if (cp < key) {
            hi = mid;
        } else if (cp > key) {
            lo = mid + 1;
        } else {
            int n = (int)(sp_dec[mid] & 31);

            memcpy(out, sp_to + sp_at[mid], (size_t)n * sizeof *out);
            return n;
        }
    }
    out[0] = cp;
    return 1;
}

/** @brief What starter @p a and @p b compose into (Hangul by rule), or 0
 *         when they do not. */
static uint32_t
compose(uint32_t a, uint32_t b)
{
    uint64_t key = (uint64_t)a << 21 | b;
    size_t lo = 0, hi = sizeof sp_pair / sizeof sp_pair[0];

    if (a >= L_BASE && a < L_BASE + L_COUNT && b >= V_BASE &&
        b < V_BASE + V_COUNT) {
        return S_BASE + ((a - L_BASE) * V_COUNT + (b - V_BASE)) * T_COUNT;
    }
    if (a >= S_BASE && a < S_BASE + S_COUNT && (a - S_BASE) % T_COUNT == 0 &&
        b > T_BASE && b < T_BASE + T_COUNT) {
        return a + (b - T_BASE);
    }
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;

        if (key < sp_pair[mid]) {
            hi = mid;
        } else if (key > sp_pair[mid]) {
            lo = mid + 1;
        } else {
            return sp_pair_to[mid];
        }
    }
    return 0;
}

/**
 * @brief Decomposed @p s (@p n long) composed canonically, in place: each
 *        character joined to the starter before it when nothing between
 *        blocks it (UAX 15).  @return its length now.
 */
static long
recompose(uint32_t *s, long n)
{
    long starter = -1, len = 0, i;
    int last = 0;

    for (i = 0; i < n; i++) {
        uint32_t c = s[i], made;
        int cc = ccc(c);

        if (starter >= 0 && (last < cc || (last == 0 && len - 1 == starter))
            && (made = compose(s[starter], c)) != 0) {
            s[starter] = made;
            continue;
        }
        if (cc == 0) {
            starter = len;
        }
        last = cc;
        s[len++] = c;
    }
    return len;
}

/**
 * @brief NFKC of the @p n code points @p cp into @p out (room for
 *        GROW_MAX each): decomposed, each starter's marks put in the
 *        order of their classes, then composed.  @return how many.
 */
static long
nfkc(const uint32_t *cp, long n, uint32_t *out)
{
    long len = 0, i, j;

    for (i = 0; i < n; i++) {
        len += decompose(cp[i], out + len);
    }
    for (i = 1; i < len; i++) {     /* stable insertion sort; starters stay */
        uint32_t c = out[i];
        int cc = ccc(c);

        for (j = i; cc != 0 && j > 0 && ccc(out[j - 1]) > cc; j--) {
            out[j] = out[j - 1];
        }
        out[j] = c;
    }
    return recompose(out, len);
}

/** @brief The code point UTF-8 has at *@p p, *@p p moved past it.
 *  @return it, or -1 for a sequence malformed, overlong, a surrogate
 *          or past U+10FFFF. */
static long
utf8_next(const unsigned char **p)
{
    static const uint32_t least[4] = {0, 0x80, 0x800, 0x10000};
    const unsigned char *s = *p;
    uint32_t c = s[0];
    int n = c < 0x80 ? 0 : c >= 0xC2 && c < 0xE0 ? 1 : c >= 0xE0 &&
            c < 0xF0 ? 2 : c >= 0xF0 && c < 0xF5 ? 3 : -1, i;

    if (n < 0) {
        return -1;
    }
    c &= n == 0 ? 0x7Fu : 0x3Fu >> n;
    for (i = 1; i <= n; i++) {
        if ((s[i] & 0xC0) != 0x80) {
            return -1;              /* a NUL stops here too */
        }
        c = c << 6 | (s[i] & 0x3Fu);
    }
    if (c < least[n] || c > 0x10FFFF || (c >= 0xD800 && c < 0xE000)) {
        return -1;
    }
    *p = s + n + 1;
    return (long)c;
}

/**
 * @brief UTF-8 @p in as code points into @p cp (IN_MAX at most), mapped
 *        as RFC 4013 2.1 says: B.1's to nothing, C.1.2's to a space.
 * @return how many, or -1 when it is not UTF-8 or too long.
 */
static long
mapped(const char *in, uint32_t *cp)
{
    const unsigned char *p = (const unsigned char *)in;
    long n = 0;

    while (*p != 0) {
        long c = utf8_next(&p);

        if (c < 0 || n == IN_MAX) {
            return -1;
        }
        if (!IN(sp_nothing, (uint32_t)c)) {
            cp[n++] = IN(sp_space, (uint32_t)c) ? 0x20u : (uint32_t)c;
        }
    }
    return n;
}

/** @brief Whether normalized @p s (@p n long) may stand: nothing RFC
 *         4013 2.3 prohibits, and right-to-left text alone and at both
 *         ends when there is any (RFC 3454 section 6). */
static int
allowed(const uint32_t *s, long n)
{
    int randal = 0, lcat = 0;
    long i;

    for (i = 0; i < n; i++) {
        if (IN(sp_prohibited, s[i])) {
            return 0;
        }
        randal |= IN(sp_randal, s[i]);
        lcat |= IN(sp_lcat, s[i]);
    }
    return !randal || (!lcat && IN(sp_randal, s[0]) &&
                       IN(sp_randal, s[n - 1]));
}

/** @brief @p s (@p n code points) as UTF-8 into @p out, NUL ended.
 *  @return its length, or -1 when @p cap is too small. */
static long
encode(const uint32_t *s, long n, char *out, size_t cap)
{
    size_t at = 0;
    long i;

    for (i = 0; i < n; i++) {
        uint32_t c = s[i];
        int k = c < 0x80 ? 1 : c < 0x800 ? 2 : c < 0x10000 ? 3 : 4, j;

        if (at + (size_t)k + 1 > cap) {
            return -1;
        }
        out[at++] = (char)(k == 1 ? c : ((0xFF00u >> k) & 0xFFu) |
                                        c >> (6 * (k - 1)));
        for (j = k - 2; j >= 0; j--) {
            out[at++] = (char)(0x80u | ((c >> (6 * j)) & 0x3Fu));
        }
    }
    if (at + 1 > cap) {
        return -1;
    }
    out[at] = '\0';
    return (long)at;
}

long
tiku_saslprep(const char *in, char *out, size_t cap)
{
    uint32_t *cp = malloc(IN_MAX * sizeof *cp), *norm = NULL;
    long n, len, rc = -1;

    if (cp != NULL && (n = mapped(in != NULL ? in : "", cp)) >= 0 &&
        (norm = malloc(((size_t)n * GROW_MAX + 1) * sizeof *norm)) != NULL) {
        len = nfkc(cp, n, norm);
        rc = allowed(norm, len) ? encode(norm, len, out, cap) : -1;
    }
    free(cp);
    free(norm);
    return rc;
}
