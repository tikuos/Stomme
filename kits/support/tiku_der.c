/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_der.c - DER, and BER's indefinite lengths and cut strings, read
 * from X.690: every length checked against what holds it, nesting
 * bounded, dates read as the two kinds of time they are written in.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_der.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* How deep indefinite lengths, and strings cut in pieces, may nest. */
#define DER_DEPTH 32

/** @brief The element at @p p, @p depth deep in indefinite ones. */
static int
der_read(const unsigned char *p, size_t n, tiku_der_t *e, int depth)
{
    size_t at = 1, len = 0, start;
    tiku_der_t child;
    int k;

    if (n < 2 || depth > DER_DEPTH) {
        return -1;
    }
    e->cls = p[0] >> 6;
    e->cons = (p[0] >> 5) & 1;
    e->tag = p[0] & 31;
    if (e->tag == 31) {                 /* the number in base 128 */
        e->tag = 0;
        do {
            if (at >= n || e->tag > (0xffffffu >> 7)) return -1;
            e->tag = e->tag << 7 | (p[at] & 127);
        } while (p[at++] & 128);
    }
    if (at >= n) {
        return -1;
    }
    if (p[at] == 128) {                 /* indefinite: to end-of-contents */
        start = ++at;
        if (!e->cons) return -1;
        while (n - at < 2 || p[at] != 0 || p[at + 1] != 0) {
            if (der_read(p + at, n - at, &child, depth + 1) != 0) return -1;
            at += child.rawlen;
        }
        e->body = p + start;
        e->len = at - start;
        e->raw = p;
        e->rawlen = at + 2;
        return 0;
    }
    if (p[at] < 128) {
        len = p[at++];
    } else {
        for (k = p[at++] & 127; k > 0; k--) {
            if (at >= n || len > ((size_t)-1 >> 8)) return -1;
            len = len << 8 | p[at++];
        }
    }
    if (len > n - at) {
        return -1;
    }
    e->body = p + at;
    e->len = len;
    e->raw = p;
    e->rawlen = at + len;
    return 0;
}

int
tiku_der_read(const unsigned char *p, size_t n, tiku_der_t *e)
{
    tiku_der_t t;

    if (p == NULL || der_read(p, n, &t, 0) != 0) {
        memset(e, 0, sizeof *e);
        return -1;
    }
    *e = t;
    return 0;
}

void
tiku_der_enter(const tiku_der_t *e, tiku_der_cursor_t *c)
{
    c->p = e->body;
    c->n = e->len;
}

int
tiku_der_next(tiku_der_cursor_t *c, tiku_der_t *e)
{
    if (c->n == 0 || tiku_der_read(c->p, c->n, e) != 0) {
        memset(e, 0, sizeof *e);
        c->n = 0;
        return -1;
    }
    c->p += e->rawlen;
    c->n -= e->rawlen;
    return 0;
}

int
tiku_der_is(const tiku_der_t *e, unsigned tag)
{
    if (tag & 0x80) {
        return e->cls == TIKU_DER_CONTEXT && e->tag == (tag & 0x7f);
    }
    return e->cls == 0 && e->tag == tag;
}

int
tiku_der_take(tiku_der_cursor_t *c, unsigned tag, tiku_der_t *e)
{
    return tiku_der_next(c, e) == 0 && tiku_der_is(e, tag) ? 0 : -1;
}

/** @brief @p dotted as the contents of an OBJECT IDENTIFIER, into @p out.
 *         @return how many bytes, or 0 for what is not one. */
static size_t
oid_encode(const char *dotted, unsigned char *out, size_t cap)
{
    unsigned long arc[24], v;
    size_t at = 0;
    int n = 0, i, k;
    char *end;

    while (*dotted && n < 24) {
        arc[n++] = strtoul(dotted, &end, 10);
        if (end == dotted || (*end && *end != '.')) return 0;
        dotted = *end ? end + 1 : end;
    }
    if (n < 2 || arc[0] > 2) {
        return 0;
    }
    arc[1] += arc[0] * 40;
    for (i = 1; i < n; i++) {
        unsigned char b[10];

        for (v = arc[i], k = 0; k == 0 || v != 0; v >>= 7) {
            b[k++] = (unsigned char)(v & 127);
        }
        if (at + (size_t)k > cap) return 0;
        while (k-- > 0) {
            out[at++] = (unsigned char)(b[k] | (k ? 128 : 0));
        }
    }
    return at;
}

int
tiku_der_oid(const tiku_der_t *e, const char *dotted)
{
    unsigned char want[64];
    size_t n = oid_encode(dotted, want, sizeof want);

    return tiku_der_is(e, TIKU_DER_OID) && n != 0 && e->len == n &&
           memcmp(e->body, want, n) == 0;
}

int
tiku_der_oid_in(const tiku_der_t *e, const char *const *dotted, int n)
{
    int i;

    for (i = 0; i < n; i++) {
        if (dotted[i] != NULL && tiku_der_oid(e, dotted[i])) {
            return i;
        }
    }
    return -1;
}

/** @brief A string cut in pieces, joined: each piece's bytes onto @p out
 *         from @p *at, or only counted when @p out is NULL.
 *         @return 0, or -1 for a piece that is not a string. */
static int
octets_join(const tiku_der_t *e, unsigned char *out, size_t *at, int depth)
{
    tiku_der_cursor_t c;
    tiku_der_t piece;

    if (!e->cons) {
        if (out != NULL) memcpy(out + *at, e->body, e->len);
        *at += e->len;
        return 0;
    }
    if (depth > DER_DEPTH) {
        return -1;
    }
    tiku_der_enter(e, &c);
    while (c.n > 0) {
        if (tiku_der_take(&c, TIKU_DER_OCTETS, &piece) != 0 ||
            octets_join(&piece, out, at, depth + 1) != 0) {
            return -1;
        }
    }
    return 0;
}

long
tiku_der_octets(const tiku_der_t *e, unsigned char **out)
{
    size_t n = 0;

    *out = NULL;
    if (octets_join(e, NULL, &n, 0) != 0 || n > (size_t)0x7fffffff ||
        (*out = malloc(n + 1)) == NULL) {
        return -1;
    }
    n = 0;
    (void)octets_join(e, *out, &n, 0);
    return (long)n;
}

int
tiku_der_int(const tiku_der_t *e, long *v)
{
    size_t i;
    long x;

    if (!tiku_der_is(e, TIKU_DER_INTEGER) || e->len == 0 ||
        e->len > sizeof(long)) {
        return -1;
    }
    x = (e->body[0] & 0x80) ? -1 : 0;
    for (i = 0; i < e->len; i++) {
        x = (long)((unsigned long)x << 8 | e->body[i]);
    }
    *v = x;
    return 0;
}

int
tiku_der_uint(const tiku_der_t *e, const unsigned char **b, size_t *n)
{
    size_t skip = 0;

    if (!tiku_der_is(e, TIKU_DER_INTEGER) || e->len == 0 ||
        (e->body[0] & 0x80)) {
        return -1;
    }
    while (skip + 1 < e->len && e->body[skip] == 0) {
        skip++;
    }
    *b = e->body + skip;
    *n = e->len - skip;
    return 0;
}

/** @brief Code point @p cp as UTF-8 onto @p out at @p *at, if it fits;
 *         a control character as a space, a surrogate or a code point
 *         past U+10FFFF as U+FFFD. */
static void
put_utf8(char *out, size_t cap, size_t *at, unsigned long cp)
{
    unsigned char b[4];
    size_t k;

    if (cp < 32 || cp == 127 || (cp >= 0xd800 && cp <= 0xdfff) ||
        cp > 0x10ffff) {
        cp = cp < 32 || cp == 127 ? ' ' : 0xfffd;
    }
    if (cp < 0x80) {
        b[0] = (unsigned char)cp;
        k = 1;
    } else if (cp < 0x800) {
        b[0] = (unsigned char)(0xc0 | cp >> 6);
        b[1] = (unsigned char)(0x80 | (cp & 63));
        k = 2;
    } else if (cp < 0x10000) {
        b[0] = (unsigned char)(0xe0 | cp >> 12);
        b[1] = (unsigned char)(0x80 | ((cp >> 6) & 63));
        b[2] = (unsigned char)(0x80 | (cp & 63));
        k = 3;
    } else {
        b[0] = (unsigned char)(0xf0 | cp >> 18);
        b[1] = (unsigned char)(0x80 | ((cp >> 12) & 63));
        b[2] = (unsigned char)(0x80 | ((cp >> 6) & 63));
        b[3] = (unsigned char)(0x80 | (cp & 63));
        k = 4;
    }
    if (*at + k < cap) {
        memcpy(out + *at, b, k);
        *at += k;
    }
}

int
tiku_der_string(const tiku_der_t *e, char *out, size_t cap)
{
    size_t at = 0, i, w;
    unsigned long cp;

    if (cap == 0) return -1;
    out[0] = 0;
    if (e->cls != 0 || e->cons) return -1;
    w = e->tag == TIKU_DER_BMP ? 2
        : e->tag == TIKU_DER_UNIVERSAL_STRING ? 4 : 1;
    if (e->tag != 12 && e->tag != 18 && e->tag != 19 && e->tag != 20 &&
        e->tag != 22 && e->tag != 26 && w == 1) {
        return -1;
    }
    for (i = 0; i + w <= e->len; i += w) {
        const unsigned char *b = e->body + i;

        if (w == 2) {
            cp = (unsigned long)b[0] << 8 | b[1];
            if (cp >= 0xd800 && cp < 0xdc00 && i + 4 <= e->len &&
                (b[2] & 0xfc) == 0xdc) {
                cp = 0x10000 + ((cp - 0xd800) << 10) +
                     (((unsigned long)b[2] << 8 | b[3]) - 0xdc00);
                i += 2;
            }
        } else if (w == 4) {
            cp = (unsigned long)b[0] << 24 | (unsigned long)b[1] << 16 |
                 (unsigned long)b[2] << 8 | b[3];
        } else if (e->tag == TIKU_DER_UTF8 && b[0] >= 0x80) {
            size_t more = b[0] >= 0xf0 ? 3 : b[0] >= 0xe0 ? 2 : 1, k;

            cp = b[0] & (0x3f >> more);
            for (k = 1; k <= more && i + k < e->len &&
                        (b[k] & 0xc0) == 0x80; k++) {
                cp = cp << 6 | (b[k] & 63);
            }
            if (k <= more || b[0] < 0xc2 || b[0] > 0xf4) cp = 0xfffd;
            i += k - 1;
        } else {
            cp = b[0];                  /* ASCII, or Teletex as Latin-1 */
        }
        put_utf8(out, cap, &at, cp);
    }
    out[at] = 0;
    return 0;
}

/** @brief Days from 1970-01-01 to @p y-@p m-@p d (proleptic Gregorian). */
static int64_t
days_from_civil(int64_t y, int m, int d)
{
    int64_t era, yoe, doy, doe;

    y -= m <= 2;
    era = (y >= 0 ? y : y - 399) / 400;
    yoe = y - era * 400;
    doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

/** @brief @p k digits at @p s as a number, or -1 when they are not. */
static int
digits(const unsigned char *s, int k)
{
    int v = 0;

    while (k-- > 0) {
        if (*s < '0' || *s > '9') return -1;
        v = v * 10 + (*s++ - '0');
    }
    return v;
}

int
tiku_der_time(const tiku_der_t *e, int64_t *t)
{
    const unsigned char *s = e->body;
    size_t n = e->len, at;
    int f[6] = { 0, 1, 1, 0, 0, 0 }, i, off = 0, yl;

    if (e->cls != 0 || (e->tag != TIKU_DER_UTC_TIME &&
                        e->tag != TIKU_DER_GEN_TIME)) {
        return -1;
    }
    yl = e->tag == TIKU_DER_UTC_TIME ? 2 : 4;
    if (n < (size_t)yl + 6 || (f[0] = digits(s, yl)) < 0) {
        return -1;
    }
    if (yl == 2) f[0] += f[0] < 50 ? 2000 : 1900;
    for (i = 1, at = (size_t)yl; i < 6 && at + 2 <= n &&
                                s[at] >= '0' && s[at] <= '9'; i++, at += 2) {
        if ((f[i] = digits(s + at, 2)) < 0) return -1;
    }
    if (i < 4 || f[1] < 1 || f[1] > 12 || f[2] < 1 || f[2] > 31 ||
        f[3] > 23 || f[4] > 59 || f[5] > 60) {
        return -1;
    }
    if (at < n && (s[at] == '.' || s[at] == ',')) {
        for (at++; at < n && s[at] >= '0' && s[at] <= '9'; at++) {}
    }
    if (at + 5 <= n && (s[at] == '+' || s[at] == '-')) {
        int hh = digits(s + at + 1, 2), mm = digits(s + at + 3, 2);

        if (hh < 0 || mm < 0) return -1;
        off = (hh * 60 + mm) * 60 * (s[at] == '+' ? 1 : -1);
    }
    *t = days_from_civil(f[0], f[1], f[2]) * 86400 + f[3] * 3600 +
         f[4] * 60 + f[5] - off;
    return 0;
}

/** @brief Day @p z after 1970-01-01 as its year, month and day. */
static void
civil_from_days(int64_t z, int64_t *y, int *m, int *d)
{
    int64_t day = z + 719468;       /* counted from 0000-03-01 */
    int64_t era = (day >= 0 ? day : day - 146096) / 146097;
    int64_t doe = day - era * 146097;
    int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    int64_t mp = (5 * doy + 2) / 153;

    *d = (int)(doy - (153 * mp + 2) / 5 + 1);
    *m = (int)(mp < 10 ? mp + 3 : mp - 9);
    *y = yoe + era * 400 + (*m <= 2);
}

void
tiku_der_time_text(int64_t t, char *out, size_t cap)
{
    int64_t z = (t >= 0 ? t : t - 86399) / 86400, secs = t - z * 86400, y;
    int m, d;

    civil_from_days(z, &y, &m, &d);
    snprintf(out, cap, "%04lld-%02d-%02d %02d:%02d:%02d UTC", (long long)y, m,
             d, (int)(secs / 3600), (int)(secs / 60 % 60), (int)(secs % 60));
}
