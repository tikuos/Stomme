/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_type1.c - PostScript Type 1: eexec undone, the charstrings found,
 * and each one run into an outline.
 *
 * The format is the Adobe Type 1 Font Format book: a clear text head
 * (FontMatrix, Encoding), then eexec-encrypted PostScript holding the
 * Subrs and CharStrings. Charstrings are encrypted unless lenIV is -1. The
 * charstring machine is Type 1's: numbers, a dozen drawing operators,
 * subroutines, flex and hint replacement through "othersubrs", and
 * accents through seac.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_type1.h"
#include "tiku_cff_strings.inl"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define T1_MAX_GLYPHS  4096
#define T1_MAX_SUBRS   4096
#define T1_STACK       48
#define T1_PS_STACK    48
#define T1_MAX_DEPTH   16
#define T1_MAX_OPS     50000
#define T1_MAX_SIZE    (8u * 1024u * 1024u)

typedef struct {
    char          *name;
    unsigned char *code;
    size_t         len;
} t1_glyph_t;

struct tiku_t1 {
    t1_glyph_t     glyph[T1_MAX_GLYPHS];
    int            nglyph;
    unsigned char *subr[T1_MAX_SUBRS];
    size_t         subr_len[T1_MAX_SUBRS];
    int            nsubr;
    char          *encoding[256];   /* the font's own, or NULL         */
    float          upem;
};

/* --------------------------------------------------------------- bytes  */

/**
 * @brief Decrypt @p n bytes at @p p in place from key @p r; eexec and
 *        charstring decryption share one recurrence.
 */
static void
decrypt(unsigned char *p, size_t n, unsigned short r)
{
    size_t i;

    for (i = 0; i < n; i++) {
        unsigned char c = p[i];

        p[i] = (unsigned char)(c ^ (r >> 8));
        r = (unsigned short)((c + r) * 52845u + 22719u);
    }
}

/** @brief A hex digit's value, or -1. */
static int
hexval(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/** @brief PostScript white space, the NUL included. */
static int
is_ws(int c)
{
    return c == ' ' || c == '\n' || c == '\r' || c == '\t' || c == '\f' ||
           c == '\0';
}

/** @brief The first @p word at or after @p from, or -1. */
static long
find(const unsigned char *p, size_t n, size_t from, const char *word)
{
    size_t w = strlen(word), i;

    for (i = from; i + w <= n; i++) {
        if (memcmp(p + i, word, w) == 0) {
            return (long)i;
        }
    }
    return -1;
}

/** @brief PFB segments unwrapped into one run of bytes; @return the length. */
static size_t
unwrap_pfb(const unsigned char *src, size_t n, unsigned char *out)
{
    size_t i = 0, o = 0;

    while (i + 6 <= n && src[i] == 0x80 && src[i + 1] != 3) {
        size_t len = (size_t)src[i + 2] | (size_t)src[i + 3] << 8 |
                     (size_t)src[i + 4] << 16 | (size_t)src[i + 5] << 24;

        i += 6;
        if (len > n - i) {
            len = n - i;
        }
        memcpy(out + o, src + i, len);
        o += len;
        i += len;
    }
    return o;
}

/* ------------------------------------------------------------ the head  */

/** @brief The clear text's FontMatrix and Encoding. */
static void
read_head(tiku_t1_t *t, const unsigned char *p, size_t n)
{
    long at = find(p, n, 0, "/FontMatrix");

    t->upem = 1000.0f;
    if (at >= 0) {
        double a = 0;

        /* 0.001 is not exact in floating point, so the em, 1/a, is
         * rounded to whole units. */
        if (sscanf((const char *)p + at + 11, " [%lf", &a) == 1 && a > 0 &&
            1.0 / a >= 16.0 && 1.0 / a <= 16384.0) {
            t->upem = (float)(long)(1.0 / a + 0.5);
        }
    }
    at = find(p, n, 0, "/Encoding");
    if (at < 0) {
        return;
    }
    if (find(p, n, (size_t)at, "StandardEncoding") >= 0 &&
        (size_t)find(p, n, (size_t)at, "StandardEncoding") < (size_t)at + 40) {
        int c;

        for (c = 0; c < 256; c++) {
            if (cff_std_encoding[c] != 0) {
                t->encoding[c] = strdup(cff_std_string[cff_std_encoding[c]]);
            }
        }
        return;
    }
    /* "dup <code> /<name> put", until "readonly def" or "def". */
    for (;;) {
        long d = find(p, n, (size_t)at, "dup ");
        long stop = find(p, n, (size_t)at, " def");
        int code;
        char name[64];

        if (d < 0 || (stop >= 0 && d > stop)) {
            break;
        }
        if (sscanf((const char *)p + d, "dup %d /%63s put", &code, name) == 2 &&
            code >= 0 && code < 256) {
            free(t->encoding[code]);
            t->encoding[code] = strdup(name);
        }
        at = d + 4;
    }
}

/* ------------------------------------------------------------ the body  */

/** @brief Read a decimal integer; return LONG_MIN on invalid input or overflow. */
static long
integer(const unsigned char *p, size_t n, size_t *i)
{
    long v = 0, sign = 1;

    while (*i < n && is_ws(p[*i])) {
        (*i)++;
    }
    if (*i < n && (p[*i] == '-' || p[*i] == '+')) {
        sign = p[*i] == '-' ? -1 : 1;
        (*i)++;
    }
    if (*i >= n || p[*i] < '0' || p[*i] > '9') {
        return LONG_MIN;
    }
    while (*i < n && p[*i] >= '0' && p[*i] <= '9') {
        if (v > (LONG_MAX - (p[*i] - '0')) / 10) {
            return LONG_MIN;
        }
        v = v * 10 + (p[*i] - '0');
        (*i)++;
    }
    return v * sign;
}

/** @brief Past the RD token and the one space after it. */
static void
past_rd(const unsigned char *p, size_t n, size_t *i)
{
    while (*i < n && is_ws(p[*i])) {
        (*i)++;
    }
    while (*i < n && !is_ws(p[*i])) {
        (*i)++;
    }
    if (*i < n) {
        (*i)++;                     /* the single space before the bytes */
    }
}

/** @brief Copy a charstring; lenIV -1 is plaintext, otherwise decrypt and skip. */
static unsigned char *
charstring(const unsigned char *p, size_t len, int leniv, size_t *out_len)
{
    unsigned char *c;
    size_t prefix = leniv < 0 ? 0 : (size_t)leniv;

    if (prefix > len) {
        return NULL;
    }
    c = malloc(len ? len : 1);
    if (c == NULL) {
        return NULL;
    }
    memcpy(c, p, len);
    if (leniv >= 0) {
        decrypt(c, len, 4330);
    }
    *out_len = len - prefix;
    memmove(c, c + prefix, *out_len);
    return c;
}

/** @brief Subrs and CharStrings out of the decrypted private part. */
static void
read_body(tiku_t1_t *t, const unsigned char *p, size_t n)
{
    long at = find(p, n, 0, "/lenIV");
    int leniv = 4;
    size_t i;

    if (at >= 0) {
        long value;

        i = (size_t)at + 6;
        value = integer(p, n, &i);
        if (value >= -1 && value <= INT_MAX &&
            (i == n || is_ws(p[i]) || strchr("()<>[]{}/%", p[i]) != NULL)) {
            leniv = (int)value;
        }
    }
    at = find(p, n, 0, "/Subrs");
    if (at >= 0) {
        long cs = find(p, n, (size_t)at, "/CharStrings");

        i = (size_t)at + 6;
        while (t->nsubr < T1_MAX_SUBRS) {
            long d = find(p, n, i, "dup ");
            long idx, len;

            if (d < 0 || (cs >= 0 && d > cs)) {
                break;              /* the Subrs end where CharStrings begin */
            }
            i = (size_t)d + 4;
            idx = integer(p, n, &i);
            len = integer(p, n, &i);
            if (idx < 0 || len < 0 || idx >= T1_MAX_SUBRS) {
                break;
            }
            past_rd(p, n, &i);
            if (i + (size_t)len > n) {
                break;
            }
            free(t->subr[idx]);
            t->subr[idx] = charstring(p + i, (size_t)len, leniv,
                                      &t->subr_len[idx]);
            if (idx + 1 > t->nsubr) {
                t->nsubr = (int)idx + 1;
            }
            i += (size_t)len;
        }
    }
    at = find(p, n, 0, "/CharStrings");
    if (at < 0) {
        return;
    }
    i = (size_t)at + 12;
    at = find(p, n, i, "begin");
    if (at >= 0) {
        i = (size_t)at + 5;
    }
    while (t->nglyph < T1_MAX_GLYPHS) {
        t1_glyph_t *g;
        size_t start;
        long len;

        while (i < n && p[i] != '/') {
            if (p[i] == 'e' && i + 3 <= n && memcmp(p + i, "end", 3) == 0 &&
                (i + 3 == n || is_ws(p[i + 3]))) {
                return;
            }
            i++;
        }
        if (i >= n) {
            return;
        }
        start = ++i;
        while (i < n && !is_ws(p[i]) && p[i] != '{' && p[i] != '/') {
            i++;
        }
        g = &t->glyph[t->nglyph];
        g->name = malloc(i - start + 1);
        if (g->name == NULL) {
            return;
        }
        memcpy(g->name, p + start, i - start);
        g->name[i - start] = '\0';
        len = integer(p, n, &i);
        if (len < 0) {
            free(g->name);
            g->name = NULL;
            continue;
        }
        past_rd(p, n, &i);
        if (i + (size_t)len > n) {
            free(g->name);
            g->name = NULL;
            return;
        }
        g->code = charstring(p + i, (size_t)len, leniv, &g->len);
        i += (size_t)len;
        if (g->code != NULL) {
            t->nglyph++;
        } else {
            free(g->name);
            g->name = NULL;
        }
    }
}

tiku_t1_t *
tiku_t1_open(const unsigned char *data, size_t len)
{
    tiku_t1_t *t;
    unsigned char *buf, *body;
    size_t n, blen, k, i;
    long ee;

    if (data == NULL || len < 16 || len > T1_MAX_SIZE) {
        return NULL;
    }
    buf = malloc(len + 1);          /* and a NUL: the head is sscanf'd */
    t = calloc(1u, sizeof *t);
    if (buf == NULL || t == NULL) {
        free(buf);
        free(t);
        return NULL;
    }
    if (data[0] == 0x80) {
        n = unwrap_pfb(data, len, buf);
    } else {
        memcpy(buf, data, len);
        n = len;
    }
    buf[n] = '\0';
    ee = find(buf, n, 0, "eexec");
    if (ee < 0) {
        free(buf);
        free(t);
        return NULL;
    }
    read_head(t, buf, (size_t)ee);
    i = (size_t)ee + 5;
    while (i < n && is_ws(buf[i])) {
        i++;
    }
    body = buf + i;
    blen = n - i;
    /* The eexec section is hex text (the PFA form) when its first four
     * bytes are hex digits; it is then decoded to binary in place. */
    if (blen >= 4 && hexval(body[0]) >= 0 && hexval(body[1]) >= 0 &&
        hexval(body[2]) >= 0 && hexval(body[3]) >= 0) {
        size_t o = 0;
        int hi = -1;

        for (k = 0; k < blen; k++) {
            int v = hexval(body[k]);

            if (v < 0) {
                continue;
            }
            if (hi < 0) {
                hi = v;
            } else {
                body[o++] = (unsigned char)(hi * 16 + v);
                hi = -1;
            }
        }
        blen = o;
    }
    decrypt(body, blen, 55665);
    if (blen > 4) {
        read_body(t, body + 4, blen - 4);
    }
    free(buf);
    if (t->nglyph == 0) {
        tiku_t1_close(t);
        return NULL;
    }
    return t;
}

void
tiku_t1_close(tiku_t1_t *t)
{
    int i;

    if (t == NULL) {
        return;
    }
    for (i = 0; i < t->nglyph; i++) {
        free(t->glyph[i].name);
        free(t->glyph[i].code);
    }
    for (i = 0; i < T1_MAX_SUBRS; i++) {
        free(t->subr[i]);
    }
    for (i = 0; i < 256; i++) {
        free(t->encoding[i]);
    }
    free(t);
}

/** @brief The charstring called @p name, or NULL. */
static const t1_glyph_t *
glyph_named(const tiku_t1_t *t, const char *name)
{
    int i;

    for (i = 0; i < t->nglyph; i++) {
        if (strcmp(t->glyph[i].name, name) == 0) {
            return &t->glyph[i];
        }
    }
    return NULL;
}

int
tiku_t1_has(const tiku_t1_t *t, const char *name)
{
    return t != NULL && name != NULL && glyph_named(t, name) != NULL;
}

const char *
tiku_t1_encoded(const tiku_t1_t *t, unsigned code)
{
    return (t != NULL && code < 256u) ? t->encoding[code] : NULL;
}

float
tiku_t1_upem(const tiku_t1_t *t)
{
    return t != NULL ? t->upem : 1000.0f;
}

/* -------------------------------------------------------- the machine  */

typedef struct {
    const tiku_t1_t   *t;
    const tiku_hint_t *hint;
    tiku_path_t       *path;
    float  st[T1_STACK];
    int    sp;
    float  ps[T1_PS_STACK];         /* what othersubrs leave for pop    */
    int    psp;
    float  x, y, sbx, sby, width;
    float  ox, oy;                  /* where seac puts an accent's origin */
    int    flex;                    /* collecting flex points           */
    float  fx[16];
    int    nflex;
    int    open, ops, depth;
    int    ended;
} t1_run_t;

static int run_code(t1_run_t *r, const unsigned char *code, size_t len);

/** @brief A charstring number as an index or a count, or -1 when it is
 *         past any: casting a float beyond int's range is undefined. */
static int
t1_int(float v)
{
    return v >= -65536.0f && v <= 65536.0f ? (int)v : -1;
}

/** @brief Start a contour at (@p x, @p y), or gather a flex point. */
static void
t1_moveto(t1_run_t *r, float x, float y)
{
    if (r->flex) {
        if (r->nflex < 16) {
            r->fx[r->nflex++] = x;
            r->fx[r->nflex++] = y;
        }
        return;
    }
    if (r->open) {
        tiku_path_close(r->path);
    }
    tiku_path_move(r->path, tiku_hint_x(r->hint, x), tiku_hint_y(r->hint, y));
    r->open = 1;
}

/** @brief A line to (@p x, @p y), opening a contour if none is. */
static void
t1_lineto(t1_run_t *r, float x, float y)
{
    if (!r->open) {
        t1_moveto(r, r->x, r->y);
    }
    tiku_path_line(r->path, tiku_hint_x(r->hint, x), tiku_hint_y(r->hint, y));
}

/** @brief A cubic through (a, b) to (@p x, @p y), likewise. */
static void
t1_curveto(t1_run_t *r, float ax, float ay, float bx, float by, float x,
           float y)
{
    if (!r->open) {
        t1_moveto(r, r->x, r->y);
    }
    tiku_path_cubic(r->path, tiku_hint_x(r->hint, ax), tiku_hint_y(r->hint, ay),
                    tiku_hint_x(r->hint, bx), tiku_hint_y(r->hint, by),
                    tiku_hint_x(r->hint, x), tiku_hint_y(r->hint, y));
}

/** @brief seac: a base glyph with an accent placed over it. */
static void
seac(t1_run_t *r, float asb, float adx, float ady, int bchar, int achar)
{
    const t1_glyph_t *base, *accent;
    float sbx = r->sbx, width = r->width, x, y;

    if (bchar < 0 || bchar > 255 || achar < 0 || achar > 255 ||
        cff_std_encoding[bchar] == 0 || cff_std_encoding[achar] == 0 ||
        r->depth > 2) {
        return;
    }
    base = glyph_named(r->t, cff_std_string[cff_std_encoding[bchar]]);
    accent = glyph_named(r->t, cff_std_string[cff_std_encoding[achar]]);
    if (base == NULL || accent == NULL) {
        return;
    }
    x = r->x;
    y = r->y;
    r->depth++;
    r->sp = 0;
    r->ended = 0;
    r->ox = r->oy = 0;
    run_code(r, base->code, base->len);
    if (r->open) {
        tiku_path_close(r->path);
        r->open = 0;
    }
    r->sp = 0;
    r->ended = 0;
    /* The accent's hsbw starts it this far along, as FreeType and the
     * PostScript original place it: past the composite's sidebearing. */
    r->ox = sbx - asb + adx;
    r->oy = ady;
    run_code(r, accent->code, accent->len);
    r->ox = r->oy = 0;
    r->depth--;
    r->x = x;
    r->y = y;
    r->width = width;               /* the composite's advance, not theirs */
    r->ended = 1;
}

/** @brief callothersubr: flex, hint replacement, and the rest. */
static void
othersubr(t1_run_t *r, int which, int nargs)
{
    int i;

    if (which == 1) {
        r->flex = 1;
        r->nflex = 0;
        r->sp -= nargs;
    } else if (which == 0) {
        /* Seventeen arguments; the fourteen kept in fx are the reference
         * point and the two curves' six points: seven rmovetos. */
        r->flex = 0;
        if (r->nflex >= 14) {
            t1_curveto(r, r->fx[2], r->fx[3], r->fx[4], r->fx[5], r->fx[6],
                       r->fx[7]);
            t1_curveto(r, r->fx[8], r->fx[9], r->fx[10], r->fx[11],
                       r->fx[12], r->fx[13]);
            r->x = r->fx[12];
            r->y = r->fx[13];
        }
        r->sp -= nargs;
        r->psp = 0;
        r->ps[r->psp++] = r->y;
        r->ps[r->psp++] = r->x;
    } else if (which == 3) {
        r->sp -= nargs;
        r->psp = 0;
        r->ps[r->psp++] = 3;
    } else {
        /* Unknown: its arguments come back through pop, in order. */
        r->psp = 0;
        for (i = 0; i < nargs && r->sp > 0 && r->psp < T1_PS_STACK; i++) {
            r->ps[r->psp++] = r->st[--r->sp];
        }
    }
    if (r->sp < 0) {
        r->sp = 0;
    }
}

/** @brief One operator with the operands on the stack; @return 0 to stop. */
static int
op(t1_run_t *r, int v, const unsigned char *code, size_t len, size_t *i)
{
    float *s = r->st;
    int n = r->sp;

    switch (v) {
    case 13:                        /* hsbw */
        if (n >= 2) {
            r->sbx = s[0];
            r->width = s[1];
            r->x = r->ox + s[0];
            r->y = r->oy;
        }
        break;
    case 9:                         /* closepath */
        if (r->open) { tiku_path_close(r->path); r->open = 0; }
        break;
    case 1: case 3:                 /* hstem vstem */
        break;
    case 21:                        /* rmoveto */
        if (n >= 2) { r->x += s[n - 2]; r->y += s[n - 1]; }
        t1_moveto(r, r->x, r->y);
        break;
    case 22:                        /* hmoveto */
        if (n >= 1) r->x += s[n - 1];
        t1_moveto(r, r->x, r->y);
        break;
    case 4:                         /* vmoveto */
        if (n >= 1) r->y += s[n - 1];
        t1_moveto(r, r->x, r->y);
        break;
    case 5:                         /* rlineto */
        if (n >= 2) { r->x += s[0]; r->y += s[1]; t1_lineto(r, r->x, r->y); }
        break;
    case 6:                         /* hlineto */
        if (n >= 1) { r->x += s[0]; t1_lineto(r, r->x, r->y); }
        break;
    case 7:                         /* vlineto */
        if (n >= 1) { r->y += s[0]; t1_lineto(r, r->x, r->y); }
        break;
    case 8:                         /* rrcurveto */
        if (n >= 6) {
            float ax = r->x + s[0], ay = r->y + s[1];
            float bx = ax + s[2], by = ay + s[3];

            r->x = bx + s[4];
            r->y = by + s[5];
            t1_curveto(r, ax, ay, bx, by, r->x, r->y);
        }
        break;
    case 30:                        /* vhcurveto */
        if (n >= 4) {
            float ax = r->x, ay = r->y + s[0];
            float bx = ax + s[1], by = ay + s[2];

            r->x = bx + s[3];
            r->y = by;
            t1_curveto(r, ax, ay, bx, by, r->x, r->y);
        }
        break;
    case 31:                        /* hvcurveto */
        if (n >= 4) {
            float ax = r->x + s[0], ay = r->y;
            float bx = ax + s[1], by = ay + s[2];

            r->x = bx;
            r->y = by + s[3];
            t1_curveto(r, ax, ay, bx, by, r->x, r->y);
        }
        break;
    case 10: {                      /* callsubr */
        int k;

        if (n < 1) return 0;
        k = t1_int(s[--r->sp]);
        if (k < 0 || k >= r->t->nsubr || r->t->subr[k] == NULL ||
            r->depth >= T1_MAX_DEPTH) {
            return 0;
        }
        /* Its return ends the subroutine, not the glyph: go on unless it
         * ended the glyph (endchar, seac) or used up the steps. */
        r->depth++;
        (void)run_code(r, r->t->subr[k], r->t->subr_len[k]);
        r->depth--;
        return !r->ended && r->ops <= T1_MAX_OPS;
    }
    case 11:                        /* return */
        return 0;
    case 14:                        /* endchar */
        r->ended = 1;
        return 0;
    default:
        (void)code; (void)len; (void)i;
        break;
    }
    r->sp = 0;
    return 1;
}

/** @brief The escaped operators (12 x). */
static int
op_escape(t1_run_t *r, int v)
{
    float *s = r->st;
    int n = r->sp;

    switch (v) {
    case 0: case 1: case 2:         /* dotsection vstem3 hstem3 */
        r->sp = 0;
        break;
    case 6:                         /* seac */
        if (n >= 5) {
            seac(r, s[0], s[1], s[2], t1_int(s[3]), t1_int(s[4]));
        }
        r->sp = 0;
        return 0;
    case 7:                         /* sbw */
        if (n >= 4) {
            r->sbx = s[0]; r->sby = s[1]; r->width = s[2];
            r->x = r->ox + s[0]; r->y = r->oy + s[1];
        }
        r->sp = 0;
        break;
    case 12:                        /* div */
        if (n >= 2) {
            s[n - 2] = s[n - 1] != 0 ? s[n - 2] / s[n - 1] : 0;
            r->sp--;
        }
        break;
    case 16:                        /* callothersubr */
        if (n >= 2) {
            int which = t1_int(s[n - 1]), nargs = t1_int(s[n - 2]);

            r->sp -= 2;
            if (nargs < 0 || nargs > r->sp) nargs = r->sp;
            othersubr(r, which, nargs);
        }
        break;
    case 17:                        /* pop */
        if (r->psp > 0 && r->sp < T1_STACK) {
            s[r->sp++] = r->ps[--r->psp];
        } else if (r->sp < T1_STACK) {
            s[r->sp++] = 0;
        }
        break;
    case 33:                        /* setcurrentpoint */
        if (n >= 2) { r->x = s[0]; r->y = s[1]; }
        r->sp = 0;
        break;
    default:
        r->sp = 0;
        break;
    }
    return 1;
}

/** @brief Run @p code; @return 0 once the glyph or a subroutine ends. */
static int
run_code(t1_run_t *r, const unsigned char *code, size_t len)
{
    size_t i = 0;

    while (i < len) {
        int v = code[i++];

        if (++r->ops > T1_MAX_OPS) {
            return 0;
        }
        if (v >= 32) {
            float num;

            if (v <= 246) {
                num = (float)(v - 139);
            } else if (v <= 250) {
                if (i >= len) return 0;
                num = (float)((v - 247) * 256 + code[i++] + 108);
            } else if (v <= 254) {
                if (i >= len) return 0;
                num = (float)(-(v - 251) * 256 - code[i++] - 108);
            } else {
                long w;

                if (i + 4 > len) return 0;
                w = (long)((unsigned long)code[i] << 24 |
                           (unsigned long)code[i + 1] << 16 |
                           (unsigned long)code[i + 2] << 8 | code[i + 3]);
                if (w > 0x7fffffffL) w -= 0x100000000L;
                num = (float)w;
                i += 4;
            }
            if (r->sp < T1_STACK) {
                r->st[r->sp++] = num;
            }
            continue;
        }
        if (v == 12) {
            if (i >= len) return 0;
            if (!op_escape(r, code[i++])) {
                return 0;
            }
            continue;
        }
        if (!op(r, v, code, len, &i)) {
            return 0;
        }
        if (r->ended) {
            return 0;
        }
    }
    return 1;
}

int
tiku_t1_outline(const tiku_t1_t *t, const char *name, const tiku_hint_t *hint,
                tiku_path_t *path, float *adv)
{
    const t1_glyph_t *g;
    t1_run_t r;

    if (t == NULL || name == NULL || hint == NULL || path == NULL) {
        return 0;
    }
    g = glyph_named(t, name);
    if (g == NULL) {
        return 0;
    }
    memset(&r, 0, sizeof r);
    r.t = t;
    r.hint = hint;
    r.path = path;
    (void)run_code(&r, g->code, g->len);
    if (r.open) {
        tiku_path_close(path);
    }
    if (adv != NULL) {
        *adv = r.width;
    }
    return 1;
}
