/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_office_util.c - what every office reader stands on: memory given
 * back all at once, arrays that grow while built, the document itself,
 * one copy of each look however many runs share it, numbers written as
 * lists and page footers write them, the symbol fonts' characters, and
 * the branch of an mc:AlternateContent to read.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_office_private.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define CHUNK 65536                 /* an arena grows by at least this   */

struct of_chunk {
    of_chunk_t *next;
    size_t      used, cap;
    unsigned char data[];
};

/* ------------------------------------------------------------- memory  */

void *
of_alloc(of_arena_t *a, size_t n)
{
    of_chunk_t *c = a->head;
    void *p;

    n = (n + 15) & ~(size_t)15;
    if (c == NULL || c->used + n > c->cap) {
        size_t cap = n > CHUNK ? n : CHUNK;

        if ((c = malloc(sizeof *c + cap)) == NULL) {
            a->bad = 1;
            return NULL;
        }
        c->next = a->head;
        c->used = 0;
        c->cap = cap;
        a->head = c;
    }
    p = c->data + c->used;
    c->used += n;
    memset(p, 0, n);
    return p;
}

char *
of_strndup(of_arena_t *a, const char *s, size_t n)
{
    char *d = of_alloc(a, n + 1);

    if (d != NULL) {
        memcpy(d, s, n);
        d[n] = '\0';
    }
    return d;
}

char *
of_strdup(of_arena_t *a, const char *s)
{
    return s != NULL ? of_strndup(a, s, strlen(s)) : NULL;
}

void
of_arena_free(of_arena_t *a)
{
    while (a->head != NULL) {
        of_chunk_t *next = a->head->next;

        free(a->head);
        a->head = next;
    }
}

void
of_vec_init(of_vec_t *v, size_t size)
{
    memset(v, 0, sizeof *v);
    v->size = size;
}

void *
of_vec_push(of_vec_t *v)
{
    unsigned char *slot;

    if (v->n == v->cap) {
        int cap = v->cap ? v->cap * 2 : 16;
        void *g = realloc(v->p, (size_t)cap * v->size);

        if (g == NULL) {
            return NULL;
        }
        v->p = g;
        v->cap = cap;
    }
    slot = (unsigned char *)v->p + (size_t)v->n++ * v->size;
    memset(slot, 0, v->size);
    return slot;
}

void *
of_vec_freeze(of_vec_t *v, of_arena_t *a, int *n)
{
    void *p = NULL;

    *n = v->n;
    if (v->n > 0 && (p = of_alloc(a, (size_t)v->n * v->size)) != NULL) {
        memcpy(p, v->p, (size_t)v->n * v->size);
    }
    if (p == NULL) {
        *n = 0;
    }
    v->n = 0;
    return p;
}

void
of_vec_free(of_vec_t *v)
{
    free(v->p);
    v->p = NULL;
    v->n = v->cap = 0;
}

/* ----------------------------------------------------------- document  */

of_doc_t *
of_doc_new(void)
{
    of_doc_t *d = calloc(1, sizeof *d);

    if (d != NULL) {
        d->tab = 36;
    }
    return d;
}

void
of_doc_free(of_doc_t *d)
{
    if (d == NULL) {
        return;
    }
    if (d->art_free != NULL) {
        d->art_free(d->art_ctx);
    }
    free(d->styles);
    of_arena_free(&d->arena);
    free(d);
}

/** @brief Whether two strings are the same, either maybe NULL. */
static int
same(const char *a, const char *b)
{
    return a == b || (a != NULL && b != NULL && strcmp(a, b) == 0);
}

/** @brief Whether two looks are the same look. */
static int
same_style(const of_style_t *a, const of_style_t *b)
{
    return same(a->font, b->font) && same(a->font_ea, b->font_ea) &&
           a->size == b->size && a->color == b->color && a->back == b->back
           && a->bold == b->bold && a->italic == b->italic &&
           a->caps == b->caps && a->smallcaps == b->smallcaps &&
           a->hidden == b->hidden && a->underline == b->underline &&
           a->strike == b->strike && a->vert == b->vert &&
           a->spacing == b->spacing && a->rise == b->rise &&
           a->scale == b->scale;
}

/** @brief A look's hash, over what tells looks apart most. */
static unsigned
style_hash(const of_style_t *s)
{
    unsigned h = 2166136261u;
    const char *f = s->font ? s->font : "";
    float size = s->size > 0 && s->size < 65536 ? s->size : 0;

    for (; *f; f++) {
        h = (h ^ (unsigned char)*f) * 16777619u;
    }
    h = (h ^ (unsigned)(size * 8)) * 16777619u;
    h = (h ^ s->color) * 16777619u;
    h = (h ^ (unsigned)(s->bold | s->italic << 1 | s->underline << 2 |
                        (unsigned char)s->vert << 6)) * 16777619u;
    return h;
}

/** @brief The table of looks twice as large.  @return 0, or -1. */
static int
styles_grow(of_doc_t *d)
{
    int cap = d->stylecap ? d->stylecap * 2 : 256, i;
    const of_style_t **t = calloc((size_t)cap, sizeof *t);

    if (t == NULL) {
        return -1;
    }
    for (i = 0; i < d->stylecap; i++) {
        if (d->styles[i] != NULL) {
            unsigned k = style_hash(d->styles[i]) & (unsigned)(cap - 1);

            while (t[k] != NULL) {
                k = (k + 1) & (unsigned)(cap - 1);
            }
            t[k] = d->styles[i];
        }
    }
    free(d->styles);
    d->styles = t;
    d->stylecap = cap;
    return 0;
}

const of_style_t *
of_style(of_doc_t *d, const of_style_t *s)
{
    of_style_t *copy;
    unsigned k;

    if ((d->nstyles + 1) * 2 > d->stylecap && styles_grow(d) != 0) {
        return NULL;
    }
    for (k = style_hash(s) & (unsigned)(d->stylecap - 1); d->styles[k];
         k = (k + 1) & (unsigned)(d->stylecap - 1)) {
        if (same_style(d->styles[k], s)) {
            return d->styles[k];
        }
    }
    if ((copy = of_alloc(&d->arena, sizeof *copy)) == NULL) {
        return NULL;
    }
    *copy = *s;
    copy->font = of_strdup(&d->arena, s->font);
    copy->font_ea = of_strdup(&d->arena, s->font_ea);
    d->styles[k] = copy;
    d->nstyles++;
    return copy;
}

void
of_style_default(of_style_t *s)
{
    memset(s, 0, sizeof *s);
    s->font = "Times New Roman";
    s->size = 12;
    s->scale = 1;
}

void
of_pstyle_default(of_pstyle_t *p)
{
    memset(p, 0, sizeof *p);
    p->align = OF_LEFT;
    p->line_rule = OF_LINE_AUTO;
    p->line = 1;
    p->outline = -1;
    p->widows = 1;
}

/* ------------------------------------------------------------ numbers  */

/** @brief @p n in Roman numerals, capitals when @p upper, into @p out. */
static void
roman(int n, int upper, char *out, size_t max)
{
    static const struct { int v; const char *s; } r[] = {
        {1000, "m"}, {900, "cm"}, {500, "d"}, {400, "cd"}, {100, "c"},
        {90, "xc"}, {50, "l"}, {40, "xl"}, {10, "x"}, {9, "ix"}, {5, "v"},
        {4, "iv"}, {1, "i"}
    };
    size_t k = 0, i;

    for (i = 0; i < sizeof r / sizeof r[0] && n > 0; i++) {
        while (n >= r[i].v && k + strlen(r[i].s) < max) {
            const char *p;

            for (p = r[i].s; *p; p++) {
                out[k++] = upper ? (char)(*p - 32) : *p;
            }
            n -= r[i].v;
        }
    }
    out[k] = '\0';
}

void
of_number(int n, int fmt, char *out, size_t max)
{
    if (max == 0) {
        return;
    }
    if ((fmt == OF_NUM_UROMAN || fmt == OF_NUM_LROMAN) && n > 0 &&
        n < 4000) {
        roman(n, fmt == OF_NUM_UROMAN, out, max);
    } else if ((fmt == OF_NUM_ULETTER || fmt == OF_NUM_LLETTER) && n > 0) {
        char c = (char)((fmt == OF_NUM_ULETTER ? 'A' : 'a') + (n - 1) % 26);
        int reps = (n - 1) / 26 + 1;
        size_t k;

        for (k = 0; k < (size_t)reps && k + 1 < max && k < 8; k++) {
            out[k] = c;             /* a, b ... z, aa, bb: as Word counts */
        }
        out[k] = '\0';
    } else {
        snprintf(out, max, "%d", n);
    }
}

/* ------------------------------------------------------------ symbols  */

/** @brief Whether @p hay holds @p needle, ASCII case aside. */
static int
has_ci(const char *hay, const char *needle)
{
    size_t n = strlen(needle);

    for (; hay != NULL && *hay; hay++) {
        if (strncasecmp(hay, needle, n) == 0) {
            return 1;
        }
    }
    return 0;
}

uint32_t
of_symbol(const char *font, uint32_t code)
{
    static const uint16_t sym[224] = {   /* Adobe Symbol, 0x20 on */
        0x0020, 0x0021, 0x2200, 0x0023, 0x2203, 0x0025, 0x0026, 0x220b,
        0x0028, 0x0029, 0x2217, 0x002b, 0x002c, 0x2212, 0x002e, 0x002f,
        0x0030, 0x0031, 0x0032, 0x0033, 0x0034, 0x0035, 0x0036, 0x0037,
        0x0038, 0x0039, 0x003a, 0x003b, 0x003c, 0x003d, 0x003e, 0x003f,
        0x2245, 0x0391, 0x0392, 0x03a7, 0x0394, 0x0395, 0x03a6, 0x0393,
        0x0397, 0x0399, 0x03d1, 0x039a, 0x039b, 0x039c, 0x039d, 0x039f,
        0x03a0, 0x0398, 0x03a1, 0x03a3, 0x03a4, 0x03a5, 0x03c2, 0x03a9,
        0x039e, 0x03a8, 0x0396, 0x005b, 0x2234, 0x005d, 0x22a5, 0x005f,
        0x203e, 0x03b1, 0x03b2, 0x03c7, 0x03b4, 0x03b5, 0x03c6, 0x03b3,
        0x03b7, 0x03b9, 0x03d5, 0x03ba, 0x03bb, 0x03bc, 0x03bd, 0x03bf,
        0x03c0, 0x03b8, 0x03c1, 0x03c3, 0x03c4, 0x03c5, 0x03d6, 0x03c9,
        0x03be, 0x03c8, 0x03b6, 0x007b, 0x007c, 0x007d, 0x223c, 0x0000,
        0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
        0x20ac, 0x03d2, 0x2032, 0x2264, 0x2044, 0x221e, 0x0192, 0x2663,
        0x2666, 0x2665, 0x2660, 0x2194, 0x2190, 0x2191, 0x2192, 0x2193,
        0x00b0, 0x00b1, 0x2033, 0x2265, 0x00d7, 0x221d, 0x2202, 0x2022,
        0x00f7, 0x2260, 0x2261, 0x2248, 0x2026, 0x23d0, 0x23af, 0x21b5,
        0x2135, 0x2111, 0x211c, 0x2118, 0x2297, 0x2295, 0x2205, 0x2229,
        0x222a, 0x2283, 0x2287, 0x2284, 0x2282, 0x2286, 0x2208, 0x2209,
        0x2220, 0x2207, 0x00ae, 0x00a9, 0x2122, 0x220f, 0x221a, 0x22c5,
        0x00ac, 0x2227, 0x2228, 0x21d4, 0x21d0, 0x21d1, 0x21d2, 0x21d3,
        0x25ca, 0x27e8, 0x00ae, 0x00a9, 0x2122, 0x2211, 0x239b, 0x239c,
        0x239d, 0x23a1, 0x23a2, 0x23a3, 0x23a7, 0x23a8, 0x23a9, 0x23aa,
        0x0000, 0x27e9, 0x222b, 0x2320, 0x23ae, 0x2321, 0x239e, 0x239f,
        0x23a0, 0x23a4, 0x23a5, 0x23a6, 0x23ab, 0x23ac, 0x23ad, 0x0000};
    static const struct { uint8_t c; uint16_t u; } wing[] = {
        {0x6c, 0x25cf}, {0x6e, 0x25a0}, {0x6f, 0x25a1}, {0x71, 0x2751},
        {0x75, 0x25c6}, {0x76, 0x2756}, {0xa7, 0x25aa}, {0xa8, 0x25fb},
        {0xd8, 0x27a2}, {0xfc, 0x2713}, {0xfb, 0x2717}, {0xfe, 0x2611},
        {0xa1, 0x25cb}, {0x9f, 0x2022}, {0x77, 0x2b25}, {0xe8, 0x2794},
        {0x4a, 0x263a}, {0x4c, 0x2639}, {0x46, 0x261e}, {0xf0, 0x21e8}};
    uint32_t c = code >= 0xf000 && code <= 0xf0ff ? code - 0xf000 : code;
    size_t i;

    if (font == NULL || c > 0xff) {
        return code;
    }
    if (has_ci(font, "wingdings")) {
        for (i = 0; i < sizeof wing / sizeof wing[0]; i++) {
            if (wing[i].c == c) {
                return wing[i].u;
            }
        }
        return 0x2022;              /* a bullet for the rest */
    }
    if (!has_ci(font, "symbol")) {
        return code;
    }
    return c >= 0x20 && sym[c - 0x20] != 0 ? sym[c - 0x20] : code;
}

/* ---------------------------------------------------------- choices  */

/** @brief Whether every prefix in @p req (a Requires list, spaces
 *         between) names a namespace read here. */
static int
understood(const char *req)
{
    static const char *const known[] = {"wps", "wpg", "wpc", "wp14", "w14",
                                        "w15", "w16se", "a14", "p14", "p15",
                                        "c14", "c16", "a16", "v"};
    size_t i, n;

    while (req != NULL && *req) {
        req += strspn(req, " ");
        n = strcspn(req, " ");
        for (i = 0; n > 0 && i < sizeof known / sizeof known[0]; i++) {
            if (strlen(known[i]) == n && strncmp(req, known[i], n) == 0) {
                break;
            }
        }
        if (n > 0 && i == sizeof known / sizeof known[0]) {
            return 0;
        }
        req += n;
    }
    return req != NULL;
}

const tiku_xml_node_t *
of_alternate(const tiku_xml_node_t *ac)
{
    const tiku_xml_node_t *c;

    for (c = tiku_xml_child(ac, "mc:Choice"); c != NULL;
         c = tiku_xml_next(c, "mc:Choice")) {
        if (understood(tiku_xml_attr(c, "Requires"))) {
            return c;
        }
    }
    return tiku_xml_child(ac, "mc:Fallback");
}
