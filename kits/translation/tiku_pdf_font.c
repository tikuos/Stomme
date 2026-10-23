/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_pdf_font.c - a font as its text is read: which character each
 * code stands for, and how far it advances.
 *
 * A ToUnicode map says outright (ISO 32000 9.10.3); a simple font
 * without one says it through its encoding and the glyph names in it
 * (9.6.6); a composite font's codes go through its CMap to CIDs, which
 * its collection's table or its CMap's own scheme may name.  Widths come
 * from the font dictionary: per code, or per CID across and down.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_pdf_private.h"
#include "tiku_pdf_glyphs.inl"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CMAP_STEPS 200000           /* codes one map may set, in all      */

/** @brief Order two glyph names, for the table search. */
static int
name_cmp(const void *a, const void *b)
{
    return strcmp((const char *)a, ((const glyph_name_t *)b)->name);
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

uint32_t
pdf_glyph_unicode(const char *name)
{
    char base[64];
    const void *hit;
    size_t n;

    if (name == NULL || *name == '\0') {
        return 0;
    }
    n = strcspn(name, ".");        /* "a.sc" is an a in another shape */
    if (n == 0 || n >= sizeof base) {
        return 0;
    }
    memcpy(base, name, n);
    base[n] = '\0';
    if (n == 1) {
        return (unsigned char)base[0];
    }
    hit = bsearch(base, glyph_names, sizeof glyph_names / sizeof glyph_names[0],
                  sizeof glyph_names[0], name_cmp);
    if (hit != NULL) {
        return ((const glyph_name_t *)hit)->cp;
    }
    if ((n == 7 && strncmp(base, "uni", 3) == 0) ||
        (n >= 5 && n <= 7 && base[0] == 'u')) {
        uint32_t v = 0;
        size_t i = base[1] == 'n' ? 3 : 1;

        for (; i < n; i++) {
            int h = hexval(base[i]);

            if (h < 0) {
                return 0;
            }
            v = v * 16 + (uint32_t)h;
        }
        return v;
    }
    return 0;
}

/** @brief The codes of one of the three named encodings, as characters. */
static void
base_encoding(uint32_t *uni, const char *name)
{
    int c;

    memset(uni, 0, 256 * sizeof *uni);
    for (c = 32; c < 127; c++) {
        uni[c] = (uint32_t)c;
    }
    if (name != NULL && strcmp(name, "WinAnsiEncoding") == 0) {
        for (c = 128; c < 160; c++) {
            uni[c] = winansi_high[c - 128];
        }
        for (c = 160; c < 256; c++) {
            uni[c] = (uint32_t)c;
        }
        uni[160] = ' ';
        uni[173] = '-';
    } else if (name != NULL && strcmp(name, "MacRomanEncoding") == 0) {
        for (c = 128; c < 256; c++) {
            uni[c] = macroman_high[c - 128];
        }
    } else {
        uni[39] = 0x2019;
        uni[96] = 0x2018;
        for (c = 161; c < 252; c++) {
            uni[c] = standard_high[c - 161];
        }
    }
}

/** @brief /Differences: a code, then the names of the codes from it. */
static void
differences(tiku_pdf_t *doc, uint32_t *uni, pdf_obj_t *diff)
{
    long code = 0;
    int i;

    if (diff == NULL || diff->kind != PDF_ARRAY) {
        return;
    }
    for (i = 0; i < diff->u.a.n; i++) {
        pdf_obj_t *o = pdf_resolve(doc, diff->u.a.item[i]);

        if (o == NULL) {
            continue;
        }
        if (o->kind == PDF_INT || o->kind == PDF_REAL) {
            code = pdf_int(o, 0);
        } else if (o->kind == PDF_NAME && code >= 0 && code < 256) {
            uni[code++] = pdf_glyph_unicode((const char *)o->u.s.p);
        }
    }
}

/** @brief A big-endian code from a CMap string of one or two bytes. */
static long
cmap_code(const pdf_obj_t *s, int *bytes)
{
    if (s == NULL || s->kind != PDF_STRING || s->u.s.n < 1 || s->u.s.n > 4) {
        return -1;
    }
    *bytes = (int)s->u.s.n;
    if (s->u.s.n == 1) {
        return s->u.s.p[0];
    }
    if (s->u.s.n == 2) {
        return ((long)s->u.s.p[0] << 8) | s->u.s.p[1];
    }
    return -1;                      /* three and four bytes: not read */
}

/** @brief The first two characters of a UTF-16BE destination string. */
static void
cmap_dst(const pdf_obj_t *s, uint32_t *first, uint32_t *second)
{
    uint32_t cp[2] = {0, 0};
    size_t i, k = 0;

    *first = *second = 0;
    if (s == NULL || s->kind != PDF_STRING) {
        return;
    }
    for (i = 0; i + 1 < s->u.s.n && k < 2; i += 2) {
        uint32_t u = ((uint32_t)s->u.s.p[i] << 8) | s->u.s.p[i + 1];

        if (u >= 0xD800 && u < 0xDC00 && i + 3 < s->u.s.n) {
            uint32_t lo = ((uint32_t)s->u.s.p[i + 2] << 8) | s->u.s.p[i + 3];

            u = 0x10000 + ((u - 0xD800) << 10) + (lo - 0xDC00);
            i += 2;
        }
        cp[k++] = u;
    }
    if (s->u.s.n == 1) {
        cp[0] = s->u.s.p[0];        /* one byte: that byte's character */
    }
    *first = cp[0];
    *second = cp[1];
}

/** @brief Set code @p c of @p f to @p cp, within the font's range. */
static void
set_uni(pdf_tfont_t *f, long c, uint32_t cp, uint32_t cp2)
{
    if (c >= 0 && c < f->codes) {
        f->uni[c] = cp;
        f->uni2[c] = cp2;
    }
}

/**
 * @brief Read a ToUnicode CMap: bfchar pairs and bfrange runs.
 *
 * The map is PostScript-shaped but its tokens are PDF's, so the object
 * lexer reads it; only the operators between begin/end matter.
 */
static void
tounicode(tiku_pdf_t *doc, pdf_tfont_t *f, pdf_obj_t *stream)
{
    unsigned char *data = NULL;
    long n = 0, steps = 0;
    pdf_cur_t c;
    pdf_obj_t *st[4] = {NULL, NULL, NULL, NULL};
    int mode = 0, sp = 0;

    if (stream == NULL || stream->kind != PDF_STREAM ||
        pdf_stream_data(doc, stream, &data, &n, NULL) != 0) {
        return;
    }
    c.p = data;
    c.n = n;
    c.pos = 0;
    for (;;) {
        pdf_obj_t *o = pdf_parse(&c, 0);
        int bytes;

        if (o == NULL || steps > CMAP_STEPS) {
            pdf_free(o);
            break;
        }
        if (o->kind == PDF_OP) {
            const char *op = (const char *)o->u.s.p;

            if (strcmp(op, "beginbfchar") == 0) mode = 1;
            else if (strcmp(op, "beginbfrange") == 0) mode = 2;
            else if (strncmp(op, "end", 3) == 0) mode = 0;
            pdf_free(o);
            while (sp > 0) {
                pdf_free(st[--sp]);
            }
            continue;
        }
        if (sp == 4) {
            pdf_free(st[0]);
            memmove(st, st + 1, 3 * sizeof st[0]);
            sp = 3;
        }
        st[sp++] = o;
        if (mode == 1 && sp >= 2) {
            long code = cmap_code(st[0], &bytes);
            uint32_t a, b;

            cmap_dst(st[1], &a, &b);
            set_uni(f, code, a, b);
            steps++;
            pdf_free(st[0]);
            pdf_free(st[1]);
            sp = 0;
        } else if (mode == 2 && sp >= 3) {
            long lo = cmap_code(st[0], &bytes), hi = cmap_code(st[1], &bytes);
            long k;

            if (lo >= 0 && hi >= lo && hi - lo < 65536) {
                if (st[2]->kind == PDF_ARRAY) {
                    for (k = 0; k < st[2]->u.a.n && lo + k <= hi; k++) {
                        uint32_t a, b;

                        cmap_dst(st[2]->u.a.item[k], &a, &b);
                        set_uni(f, lo + k, a, b);
                    }
                } else {
                    uint32_t a, b;

                    cmap_dst(st[2], &a, &b);
                    for (k = 0; lo + k <= hi && steps++ < CMAP_STEPS; k++) {
                        set_uni(f, lo + k, a ? a + (uint32_t)k : 0, b);
                    }
                }
            }
            pdf_free(st[0]);
            pdf_free(st[1]);
            pdf_free(st[2]);
            sp = 0;
        }
    }
    free(data);
}

/** @brief Widths of a simple font: /FirstChar on, from /Widths. */
static void
simple_widths(tiku_pdf_t *doc, pdf_tfont_t *f, pdf_obj_t *font, double scale)
{
    pdf_obj_t *w = pdf_get(doc, font, "Widths");
    pdf_obj_t *desc = pdf_get(doc, font, "FontDescriptor");
    long first = pdf_int(pdf_get(doc, font, "FirstChar"), 0);
    int i;

    f->missing = pdf_number(pdf_get(doc, desc, "MissingWidth"), 0) * scale;
    if (w == NULL || w->kind != PDF_ARRAY) {
        f->missing = 0.5;           /* a standard face: its stand-in's */
        if (!f->type3) {
            (void)pdf_standin_widths(doc, font, f->uni, f->width);
        }
        return;
    }
    for (i = 0; i < w->u.a.n; i++) {
        pdf_obj_t *o = pdf_resolve(doc, w->u.a.item[i]);

        if (first + i >= 0 && first + i < 256 && o != NULL &&
            (o->kind == PDF_INT || o->kind == PDF_REAL)) {
            f->width[first + i] = pdf_number(o, 0) * scale;
        }
    }
}

/** @brief Widths of a CID font: /W runs, /DW for the rest. */
static void
cid_widths(tiku_pdf_t *doc, pdf_tfont_t *f, pdf_obj_t *cidfont)
{
    pdf_obj_t *w = pdf_get(doc, cidfont, "W");
    int i = 0;

    f->missing = pdf_number(pdf_get(doc, cidfont, "DW"), 1000) / 1000.0;
    if (w == NULL || w->kind != PDF_ARRAY) {
        return;
    }
    while (i < w->u.a.n) {
        pdf_obj_t *a = pdf_resolve(doc, w->u.a.item[i]), *b, *cw;
        long c, last, k;

        if (a == NULL || (a->kind != PDF_INT && a->kind != PDF_REAL) ||
            i + 1 >= w->u.a.n) {
            break;
        }
        c = pdf_int(a, 0);
        b = pdf_resolve(doc, w->u.a.item[i + 1]);
        if (b != NULL && b->kind == PDF_ARRAY) {
            for (k = 0; k < b->u.a.n; k++) {
                if (c + k >= 0 && c + k < f->codes) {
                    pdf_obj_t *wk = pdf_resolve(doc, b->u.a.item[k]);

                    f->width[c + k] = pdf_number(wk, 1000) / 1000.0;
                }
            }
            i += 2;
            continue;
        }
        if (i + 2 >= w->u.a.n) {
            break;
        }
        last = pdf_int(b, 0);
        cw = pdf_resolve(doc, w->u.a.item[i + 2]);
        for (k = c; k <= last && k < f->codes && k - c < 65536; k++) {
            if (k >= 0) {
                f->width[k] = pdf_number(cw, 1000) / 1000.0;
            }
        }
        i += 3;
    }
}

/** @brief Order W2's runs by their first CID. */
static int
vrun_cmp(const void *a, const void *b)
{
    const pdf_vrun_t *x = a, *y = b;

    return x->first < y->first ? -1 : x->first > y->first;
}

/** @brief A W2 run of @p first to @p last, from three numbers at @p v. */
static void
vrun_add(tiku_pdf_t *doc, pdf_tfont_t *f, long first, long last,
         pdf_obj_t **v, long *cap)
{
    pdf_vrun_t *r;

    if (f->nw2 == *cap) {
        long more = *cap ? *cap * 2 : 64;

        if (more > 65536 ||
            (r = realloc(f->w2, (size_t)more * sizeof *r)) == NULL) {
            return;
        }
        f->w2 = r;
        *cap = more;
    }
    r = &f->w2[f->nw2++];
    r->first = first;
    r->last = last;
    r->w1 = pdf_number(pdf_resolve(doc, v[0]), -1000) / 1000.0;
    r->vx = pdf_number(pdf_resolve(doc, v[1]), 500) / 1000.0;
    r->vy = pdf_number(pdf_resolve(doc, v[2]), 880) / 1000.0;
}

/** @brief Metrics of a CID font written down the page (ISO 32000
 *         9.7.4.3): /DW2 for all, /W2 runs for some, as W is. */
static void
cid_vmetrics(tiku_pdf_t *doc, pdf_tfont_t *f, pdf_obj_t *cidfont)
{
    pdf_obj_t *dw2 = pdf_get(doc, cidfont, "DW2");
    pdf_obj_t *w = pdf_get(doc, cidfont, "W2");
    long i = 0, cap = 0;

    if (dw2 != NULL && dw2->kind == PDF_ARRAY && dw2->u.a.n == 2) {
        f->dw2_vy = pdf_number(pdf_resolve(doc, dw2->u.a.item[0]), 880) /
                    1000.0;
        f->dw2_w1 = pdf_number(pdf_resolve(doc, dw2->u.a.item[1]), -1000) /
                    1000.0;
    }
    while (w != NULL && w->kind == PDF_ARRAY && i + 1 < w->u.a.n) {
        long c = pdf_int(pdf_resolve(doc, w->u.a.item[i]), -1), k;
        pdf_obj_t *b = pdf_resolve(doc, w->u.a.item[i + 1]);

        if (c < 0 || b == NULL) {
            break;
        }
        if (b->kind == PDF_ARRAY) {
            for (k = 0; k + 2 < b->u.a.n; k += 3) {
                vrun_add(doc, f, c + k / 3, c + k / 3, b->u.a.item + k,
                         &cap);
            }
            i += 2;
            continue;
        }
        if (i + 4 >= w->u.a.n) {
            break;
        }
        vrun_add(doc, f, c, pdf_int(b, c), w->u.a.item + i + 2, &cap);
        i += 5;
    }
    if (f->nw2 > 1) {
        qsort(f->w2, (size_t)f->nw2, sizeof *f->w2, vrun_cmp);
    }
}

/** @brief The collection a CID font's CIDs come from ("Adobe-Japan1"),
 *         into @p out; "" when they are the font's own (Identity). */
static void
collection(tiku_pdf_t *doc, pdf_obj_t *cidfont, char *out, size_t max)
{
    pdf_obj_t *info = pdf_get(doc, cidfont, "CIDSystemInfo");
    pdf_obj_t *reg = pdf_get(doc, info, "Registry");
    pdf_obj_t *ord = pdf_get(doc, info, "Ordering");

    out[0] = '\0';
    if (reg != NULL && ord != NULL && reg->kind == PDF_STRING &&
        ord->kind == PDF_STRING && reg->u.s.n < 16 && ord->u.s.n < 16) {
        snprintf(out, max, "%.*s-%.*s", (int)reg->u.s.n,
                 (const char *)reg->u.s.p, (int)ord->u.s.n,
                 (const char *)ord->u.s.p);
    }
}

/** @brief Whether @p cidfont carries its own program. */
static int
embedded(tiku_pdf_t *doc, pdf_obj_t *cidfont)
{
    pdf_obj_t *desc = pdf_get(doc, cidfont, "FontDescriptor");

    return pdf_get(doc, desc, "FontFile") != NULL ||
           pdf_get(doc, desc, "FontFile2") != NULL ||
           pdf_get(doc, desc, "FontFile3") != NULL;
}

/**
 * @brief A composite font: its CMap, its CIDs' widths across and down,
 *        and what they stand for.  When the CMap's codes are Unicode and
 *        its CIDs are not known here, glyphs go by character, as wide
 *        as the stand-in draws them.
 */
static void
composite(tiku_pdf_t *doc, pdf_tfont_t *f, pdf_obj_t *font)
{
    pdf_obj_t *kids = pdf_get(doc, font, "DescendantFonts");
    pdf_obj_t *cid = (kids != NULL && kids->kind == PDF_ARRAY &&
                      kids->u.a.n > 0)
                     ? pdf_resolve(doc, kids->u.a.item[0]) : NULL;
    char coll[40];
    long i;

    f->missing = 1.0;
    f->dw2_vy = 0.88;
    f->dw2_w1 = -1.0;
    f->cmap = pdf_cmap_load(doc, pdf_get(doc, font, "Encoding"));
    f->vertical = pdf_cmap_vertical(f->cmap);
    /* A Unicode scheme whose file is not here knows no CID for any code. */
    f->by_unicode = f->cmap != NULL && pdf_cmap_unicode_codes(f->cmap) &&
                    pdf_cmap_cid(f->cmap, 0x20, 2) < 0;
    if (cid != NULL) {
        collection(doc, cid, coll, sizeof coll);
        f->cid_uni = coll[0] ? pdf_cid_unicode(doc, coll) : NULL;
        cid_widths(doc, f, cid);
        if (f->vertical) {
            cid_vmetrics(doc, f, cid);
        }
    }
    if (f->by_unicode) {
        for (i = 0; i < f->codes; i++) {
            f->width[i] = -1;       /* W's CIDs are not these glyphs */
        }
        if (cid == NULL || !embedded(doc, cid)) {
            (void)pdf_standin_char_widths(doc, font, f->width);
        }
    }
    tounicode(doc, f, pdf_get(doc, font, "ToUnicode"));
}

void
pdf_tfont_free(pdf_tfont_t *f)
{
    if (f == NULL) {
        return;
    }
    pdf_cmap_free(f->cmap);
    free(f->w2);
    free(f->uni);
    free(f->uni2);
    free(f->width);
    free(f);
}

pdf_tfont_t *
pdf_tfont_load(tiku_pdf_t *doc, pdf_obj_t *font)
{
    pdf_tfont_t *f = calloc(1, sizeof *f);
    pdf_obj_t *sub = pdf_get(doc, font, "Subtype"), *enc;
    double scale = 1.0 / 1000.0;
    long i;

    if (f == NULL) {
        return NULL;
    }
    f->two_byte = pdf_is_name(sub, "Type0");
    f->type3 = pdf_is_name(sub, "Type3");
    f->codes = f->two_byte ? 65536 : 256;
    f->uni = calloc((size_t)f->codes, sizeof *f->uni);
    f->uni2 = calloc((size_t)f->codes, sizeof *f->uni2);
    f->width = malloc((size_t)f->codes * sizeof *f->width);
    if (f->uni == NULL || f->uni2 == NULL || f->width == NULL) {
        pdf_tfont_free(f);
        return NULL;
    }
    for (i = 0; i < f->codes; i++) {
        f->width[i] = -1;
    }
    if (f->two_byte) {
        composite(doc, f, font);
        return f;
    }
    if (f->type3) {
        pdf_obj_t *m = pdf_get(doc, font, "FontMatrix");

        scale = (m != NULL && m->kind == PDF_ARRAY && m->u.a.n == 6)
                ? pdf_number(pdf_resolve(doc, m->u.a.item[0]), 0.001) : 0.001;
    }
    enc = pdf_get(doc, font, "Encoding");
    if (enc != NULL && enc->kind == PDF_NAME) {
        base_encoding(f->uni, (const char *)enc->u.s.p);
    } else if (enc != NULL && enc->kind == PDF_DICT) {
        pdf_obj_t *base = pdf_get(doc, enc, "BaseEncoding");

        base_encoding(f->uni, base != NULL && base->kind == PDF_NAME
                              ? (const char *)base->u.s.p : NULL);
        differences(doc, f->uni, pdf_get(doc, enc, "Differences"));
    } else {
        base_encoding(f->uni, NULL);
    }
    simple_widths(doc, f, font, scale);
    tounicode(doc, f, pdf_get(doc, font, "ToUnicode"));
    return f;
}

/* ------------------------------------------------------------ showing  */

/** @brief Glyph @p cid's metrics down the page: W2's, or DW2 and half
 *         its width across. */
static void
vmetrics(const pdf_tfont_t *f, long cid, pdf_shown_t *g)
{
    long lo = 0, hi = f->nw2 - 1;

    g->w1 = f->dw2_w1;
    g->vx = g->w / 2;
    g->vy = f->dw2_vy;
    while (cid >= 0 && lo <= hi) {
        long mid = lo + (hi - lo) / 2;
        const pdf_vrun_t *r = &f->w2[mid];

        if (cid < r->first) {
            hi = mid - 1;
        } else if (cid > r->last) {
            lo = mid + 1;
        } else {
            g->w1 = r->w1;
            g->vx = r->vx;
            g->vy = r->vy;
            return;
        }
    }
}

/** @brief A simple font's glyph: one byte, its own name. */
static int
simple_next(const pdf_tfont_t *f, const unsigned char *s, pdf_shown_t *g)
{
    g->code = s[0];
    g->len = 1;
    g->key = s[0];
    g->word = s[0] == 32;
    g->w = f == NULL ? 0.5 : f->width[s[0]] >= 0 ? f->width[s[0]]
                                                 : f->missing;
    if (f != NULL) {
        g->uni = f->uni[s[0]];
        g->uni2 = f->uni2[s[0]];
    }
    return 1;
}

int
pdf_tfont_next(const pdf_tfont_t *f, const unsigned char *s, size_t n,
               pdf_shown_t *g)
{
    long cid = 0;
    uint32_t u = 0;

    memset(g, 0, sizeof *g);
    if (n == 0) {
        return 0;
    }
    if (f == NULL || !f->two_byte) {
        return simple_next(f, s, g);
    }
    if (f->cmap != NULL) {
        g->len = pdf_cmap_next(f->cmap, s, n, &g->code);
        cid = g->len ? pdf_cmap_cid(f->cmap, g->code, g->len) : 0;
        u = g->len ? pdf_cmap_unicode(f->cmap, g->code, g->len) : 0;
    } else if (n >= 2) {            /* no map read: two bytes, Identity */
        g->len = 2;
        g->code = (uint32_t)s[0] << 8 | s[1];
        cid = (long)g->code;
    }
    if (g->len == 0) {
        return 0;
    }
    g->word = g->len == 1 && g->code == 32;
    g->key = cid >= 0 ? cid : f->by_unicode && u < 65536 ? (long)u : 0;
    if (g->key >= f->codes) {
        g->key = 0;                 /* past the width table: .notdef */
    }
    g->w = f->width[g->key] >= 0 ? f->width[g->key] : f->missing;
    if (g->code < 65536 && f->uni[g->code] != 0) {
        g->uni = f->uni[g->code];   /* ToUnicode says, first */
        g->uni2 = f->uni2[g->code];
    } else if (u != 0) {
        g->uni = u;
    } else if (cid > 0 && f->cid_uni != NULL) {
        g->uni = f->cid_uni[cid];
    }
    if (f->vertical) {
        vmetrics(f, cid, g);
    }
    return g->len;
}

uint32_t *
pdf_tfont_glyph_unicode(const pdf_tfont_t *f)
{
    uint32_t *t;
    long c;

    if (f == NULL || !f->two_byte || f->by_unicode ||
        (t = calloc(65536, sizeof *t)) == NULL) {
        return NULL;
    }
    if (f->cid_uni != NULL) {
        memcpy(t, f->cid_uni, 65536 * sizeof *t);
    }
    /* What the file says of its codes, through the CMap to their CIDs. */
    for (c = 0; c < 65536; c++) {
        int len = f->cmap != NULL ? pdf_cmap_len(f->cmap, (uint32_t)c) : 2;
        long cid = f->cmap != NULL ? pdf_cmap_cid(f->cmap, (uint32_t)c, len)
                                   : c;
        uint32_t u = f->uni[c];

        if (u == 0 && f->cmap != NULL) {
            u = pdf_cmap_unicode(f->cmap, (uint32_t)c, len);
        }
        if (u != 0 && cid > 0 && cid < 65536) {
            t[cid] = u;
        }
    }
    return t;
}
