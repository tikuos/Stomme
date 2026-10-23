/*
 * KintsugiOS v0.01 kits -- the foundation KintsugiDesktop and KintsugiTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@kinstugi-os.org>
 *
 * tiku_pdf_writer_font.c - faces written into the file.
 *
 * A TrueType face goes in as a subset that keeps every glyph's number --
 * the glyphs drawn, and the pieces composite ones are built from, with
 * the rest left empty -- so a glyph's number is its CID and nothing is
 * renumbered.  A face with PostScript outlines goes in whole, as OpenType.
 * Each is a composite font: Identity-H, its widths, and a ToUnicode map
 * from each glyph to the character it stands for.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_pdf_writer_private.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define FONT_TABLES 64              /* tables a face may list            */
#define FONT_MAX    (256L << 20)    /* the largest file read             */
#define COMPOSITE   0x20            /* MORE_COMPONENTS                   */

/** @brief A table of a face: its tag, and where its bytes are. */
typedef struct {
    char   tag[5];
    size_t off, len;
} sf_table_t;

/* ------------------------------------------------------------ bytes   */

/** @brief A big-endian 16-bit number. */
static unsigned
be16(const unsigned char *p)
{
    return (unsigned)p[0] << 8 | p[1];
}

/** @brief A big-endian 32-bit number. */
static uint32_t
be32(const unsigned char *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 |
           (uint32_t)p[2] << 8 | p[3];
}

/** @brief @p v put big-endian in two bytes. */
static void
put16(unsigned char *p, unsigned v)
{
    p[0] = (unsigned char)(v >> 8);
    p[1] = (unsigned char)v;
}

/** @brief @p v put big-endian in four bytes. */
static void
put32(unsigned char *p, uint32_t v)
{
    p[0] = (unsigned char)(v >> 24);
    p[1] = (unsigned char)(v >> 16);
    p[2] = (unsigned char)(v >> 8);
    p[3] = (unsigned char)v;
}

/** @brief The whole file @p path.  @return its bytes, or NULL with
 *         *@p oom set when memory ran short. */
static unsigned char *
slurp(const char *path, size_t *len, int *oom)
{
    struct stat st;
    unsigned char *b;
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    size_t got = 0;

    if (fd < 0 || fstat(fd, &st) != 0 || st.st_size < 12 ||
        st.st_size > FONT_MAX) {
        if (fd >= 0) {
            close(fd);
        }
        return NULL;
    }
    if ((b = malloc((size_t)st.st_size)) == NULL) {
        *oom = 1;
        close(fd);
        return NULL;
    }
    while (got < (size_t)st.st_size) {
        ssize_t n = read(fd, b + got, (size_t)st.st_size - got);

        if (n <= 0) {
            break;
        }
        got += (size_t)n;
    }
    close(fd);
    if (got != (size_t)st.st_size) {
        free(b);
        return NULL;
    }
    *len = got;
    return b;
}

/** @brief Face @p index's tables in @p d, a collection's or the file's
 *         one.  @return how many, 0 when it is not a face. */
static int
tables(const unsigned char *d, size_t len, int index, sf_table_t *t)
{
    size_t dir = 0;
    unsigned n, i;

    if (len >= 16 && memcmp(d, "ttcf", 4) == 0) {
        if ((uint32_t)index >= be32(d + 8) || 16 + 4 * (size_t)index > len)
            return 0;
        dir = be32(d + 12 + 4 * index);
    }
    if (dir + 12 > len) {
        return 0;
    }
    n = be16(d + dir + 4);
    for (i = 0; i < n && i < FONT_TABLES && dir + 28 + 16 * i <= len; i++) {
        const unsigned char *r = d + dir + 12 + 16 * i;

        memcpy(t[i].tag, r, 4);
        t[i].tag[4] = '\0';
        t[i].off = be32(r + 8);
        t[i].len = be32(r + 12);
        if (t[i].off > len || t[i].len > len - t[i].off) {
            t[i].len = 0;
        }
    }
    return (int)i;
}

/** @brief The table tagged @p tag among @p t, or NULL. */
static const sf_table_t *
table(const sf_table_t *t, int n, const char *tag)
{
    int i;

    for (i = 0; i < n; i++) {
        if (memcmp(t[i].tag, tag, 4) == 0 && t[i].len > 0) {
            return &t[i];
        }
    }
    return NULL;
}

/* ----------------------------------------------------------- measure   */

/** @brief Face @p f's glyphs' advances from its hmtx, which holds @p nhm:
 *         the glyphs past them take the last one's. */
static void
advances(pw_font_t *f, const unsigned char *d, const sf_table_t *hmtx,
         int nhm)
{
    int i;

    for (i = 0; i < f->nglyph; i++) {
        int k = i < nhm ? i : nhm - 1;

        if ((size_t)k * 4 + 2 <= hmtx->len) {
            f->adv[i] = (unsigned short)be16(d + hmtx->off + 4 * (size_t)k);
        }
    }
}

/** @brief Copy an owned source or read a path-backed source for one operation. */
static unsigned char *
source(const pw_font_t *font, size_t *length, int *oom)
{
    unsigned char *copy;

    if (!font->data) return slurp(font->path, length, oom);
    copy = malloc(font->length);
    if (!copy) {
        *oom = 1;
        return NULL;
    }
    memcpy(copy, font->data, font->length);
    *length = font->length;
    return copy;
}

int
pw_font_load(pw_font_t *f)
{
    size_t len = 0;
    int bad = 0;
    unsigned char *d = source(f, &len, &bad);
    sf_table_t t[FONT_TABLES];
    int n = d ? tables(d, len, f->index, t) : 0, nhm;
    const sf_table_t *head = table(t, n, "head"), *hhea = table(t, n, "hhea");
    const sf_table_t *maxp = table(t, n, "maxp"), *hmtx = table(t, n, "hmtx");

    if (!head || !hhea || !maxp || !hmtx || head->len < 54 ||
        hhea->len < 36 || maxp->len < 6 ||
        (!table(t, n, "glyf") && !table(t, n, "CFF "))) {
        free(d);
        return bad ? -2 : -1;
    }
    f->upem = (int)be16(d + head->off + 18);
    f->nglyph = (int)be16(d + maxp->off + 4);
    nhm = (int)be16(d + hhea->off + 34);
    f->cff = table(t, n, "glyf") == NULL;
    f->adv = calloc((size_t)f->nglyph + 1, sizeof *f->adv);
    f->used = calloc((size_t)f->nglyph + 1, 1);
    f->uni = calloc((size_t)f->nglyph + 1, sizeof *f->uni);
    bad = f->upem < 16 || f->nglyph < 1 || nhm < 1 ? -1
          : !f->adv || !f->used || !f->uni ? -2 : 0;
    if (!bad) {
        advances(f, d, hmtx, nhm);
    }
    free(d);
    return bad;
}

void
pw_font_free(pw_font_t *f)
{
    free(f->adv);
    free(f->used);
    free(f->uni);
    free(f->data);
    f->data = NULL;
    f->length = 0;
    f->adv = NULL;
    f->used = NULL;
    f->uni = NULL;
}

/* ------------------------------------------------------------ subset   */

/** @brief Glyph @p g's bytes in glyf, through loca.  @return its length,
 *         with its start in @p at; 0 for an empty or broken one. */
static size_t
glyph_at(const unsigned char *d, const sf_table_t *loca,
         const sf_table_t *glyf, int long_loca, int g, size_t *at)
{
    size_t a, b, step = long_loca ? 4 : 2;

    if ((size_t)(g + 2) * step > loca->len) {
        return 0;
    }
    a = long_loca ? be32(d + loca->off + 4 * (size_t)g)
                  : (size_t)be16(d + loca->off + 2 * (size_t)g) * 2;
    b = long_loca ? be32(d + loca->off + 4 * (size_t)g + 4)
                  : (size_t)be16(d + loca->off + 2 * (size_t)g + 2) * 2;
    if (b <= a || b > glyf->len) {
        return 0;
    }
    *at = glyf->off + a;
    return b - a;
}

/** @brief The glyphs composite glyphs drawn are made of, marked used too,
 *         however deep they nest.  @return 0, or -1 out of memory. */
static int
closure(const unsigned char *d, const sf_table_t *loca,
        const sf_table_t *glyf, int long_loca, pw_font_t *f)
{
    int *stack = malloc((size_t)f->nglyph * sizeof *stack), top = 0, g;

    if (stack == NULL) {
        return -1;
    }
    for (g = 0; g < f->nglyph; g++) {
        if (f->used[g]) {
            stack[top++] = g;
        }
    }
    while (top > 0) {
        size_t at = 0, n = glyph_at(d, loca, glyf, long_loca, stack[--top],
                                    &at), p = 10;
        unsigned flags = COMPOSITE;

        if (n < 10 || (short)be16(d + at) >= 0) {
            continue;
        }
        while ((flags & COMPOSITE) && p + 4 <= n) {
            unsigned kid = be16(d + at + p + 2);

            flags = be16(d + at + p);
            if (kid < (unsigned)f->nglyph && !f->used[kid]) {
                f->used[kid] = 1;
                stack[top++] = (int)kid;
            }
            p += 4 + (flags & 1 ? 4 : 2) + (flags & 8 ? 2 : flags & 0x40
                  ? 4 : flags & 0x80 ? 8 : 0);
        }
    }
    free(stack);
    return 0;
}

/** @brief The sum of @p n bytes as four-byte words, as sfnt checks them. */
static uint32_t
checksum(const unsigned char *p, size_t n)
{
    uint32_t sum = 0;
    size_t i;

    for (i = 0; i + 3 < n; i += 4) {
        sum += be32(p + i);
    }
    if (i < n) {
        unsigned char tail[4] = {0, 0, 0, 0};

        memcpy(tail, p + i, n - i);
        sum += be32(tail);
    }
    return sum;
}

/** @brief One table of a face being made: its tag and bytes. */
typedef struct {
    char                 tag[5];
    const unsigned char *p;
    size_t               n;
} sf_out_t;

/** @brief A face from its tables, sorted by tag, each on four bytes.
 *  @return its bytes, with their length in @p len. */
static unsigned char *
assemble(sf_out_t *t, int n, uint32_t version, size_t *len)
{
    size_t total = 12 + 16 * (size_t)n, at;
    unsigned char *b;
    int i, k, pow = 1, log = 0;

    for (i = 1; i < n; i++) {
        for (k = i; k > 0 && memcmp(t[k - 1].tag, t[k].tag, 4) > 0; k--) {
            sf_out_t s = t[k];
            t[k] = t[k - 1];
            t[k - 1] = s;
        }
    }
    for (i = 0; i < n; i++) {
        total += (t[i].n + 3) & ~(size_t)3;
    }
    if ((b = calloc(1, total)) == NULL) {
        return NULL;
    }
    while (pow * 2 <= n) {
        pow *= 2;
        log++;
    }
    put32(b, version);
    put16(b + 4, (unsigned)n);
    put16(b + 6, (unsigned)pow * 16);
    put16(b + 8, (unsigned)log);
    put16(b + 10, (unsigned)(n - pow) * 16);
    for (i = 0, at = 12 + 16 * (size_t)n; i < n; i++) {
        memcpy(b + 12 + 16 * i, t[i].tag, 4);
        memcpy(b + at, t[i].p, t[i].n);
        put32(b + 12 + 16 * i + 4, checksum(b + at, t[i].n));
        put32(b + 12 + 16 * i + 8, (uint32_t)at);
        put32(b + 12 + 16 * i + 12, (uint32_t)t[i].n);
        at += (t[i].n + 3) & ~(size_t)3;
    }
    *len = total;
    return b;
}

/** @brief New glyf and loca holding only the glyphs used, every number
 *         kept.  @return 0, or -1 out of memory. */
static int
rebuild(const unsigned char *d, const sf_table_t *loca,
        const sf_table_t *glyf, int long_loca, const pw_font_t *f,
        unsigned char **nglyf, size_t *nglyf_len, unsigned char **nloca)
{
    size_t total = 0, at, n;
    int g;

    for (g = 0; g < f->nglyph; g++) {
        if (f->used[g] && (n = glyph_at(d, loca, glyf, long_loca, g, &at)))
            total += (n + 3) & ~(size_t)3;
    }
    *nglyf = malloc(total ? total : 4);
    *nloca = malloc(4 * ((size_t)f->nglyph + 1));
    if (*nglyf == NULL || *nloca == NULL) {
        return -1;
    }
    for (g = 0, total = 0; g < f->nglyph; g++) {
        put32(*nloca + 4 * (size_t)g, (uint32_t)total);
        if (f->used[g] && (n = glyph_at(d, loca, glyf, long_loca, g, &at))) {
            memcpy(*nglyf + total, d + at, n);
            memset(*nglyf + total + n, 0, ((n + 3) & ~(size_t)3) - n);
            total += (n + 3) & ~(size_t)3;
        }
    }
    put32(*nloca + 4 * (size_t)f->nglyph, (uint32_t)total);
    *nglyf_len = total;
    return 0;
}

/**
 * @brief Face @p f from its file's bytes as a file of its own: TrueType
 *        cut to the glyphs used, or every table of a PostScript one.
 * @return its bytes, with their length in @p out; NULL when it cannot be.
 */
static unsigned char *
program(const unsigned char *d, size_t len, pw_font_t *f, size_t *out)
{
    static const char *const keep[] = {"head", "hhea", "maxp", "hmtx",
                                       "cvt ", "fpgm", "prep", "OS/2"};
    sf_table_t t[FONT_TABLES];
    sf_out_t o[FONT_TABLES];
    const sf_table_t *head, *loca, *glyf, *k;
    unsigned char *nglyf = NULL, *nloca = NULL, *res = NULL, hd[54];
    size_t ng = 0;
    int n = tables(d, len, f->index, t), m = 0, i;

    if (f->cff) {
        for (i = 0; i < n; i++) {
            memcpy(o[m].tag, t[i].tag, 5);
            o[m].p = d + t[i].off;
            o[m++].n = t[i].len;
        }
        return assemble(o, m, 0x4f54544fu, out);    /* "OTTO" */
    }
    head = table(t, n, "head");
    loca = table(t, n, "loca");
    glyf = table(t, n, "glyf");
    if (!head || !loca || !glyf || head->len < 54) {
        return NULL;
    }
    f->used[0] = 1;
    if (closure(d, loca, glyf, (short)be16(d + head->off + 50) != 0,
                f) == 0 &&
        rebuild(d, loca, glyf, (short)be16(d + head->off + 50) != 0, f,
                &nglyf, &ng, &nloca) == 0) {
        memcpy(hd, d + head->off, 54);
        put32(hd + 8, 0);                   /* checkSumAdjustment */
        put16(hd + 50, 1);                  /* long offsets */
        for (i = 0; i < (int)(sizeof keep / sizeof keep[0]); i++) {
            if ((k = table(t, n, keep[i])) != NULL) {
                memcpy(o[m].tag, keep[i], 5);
                o[m].p = i == 0 ? hd : d + k->off;
                o[m++].n = i == 0 ? 54 : k->len;
            }
        }
        memcpy(o[m].tag, "loca", 5);
        o[m].p = nloca;
        o[m++].n = 4 * ((size_t)f->nglyph + 1);
        memcpy(o[m].tag, "glyf", 5);
        o[m].p = nglyf;
        o[m++].n = ng;
        res = assemble(o, m, 0x00010000u, out);
    }
    free(nglyf);
    free(nloca);
    return res;
}

/* ----------------------------------------------------------- objects   */

/** @brief The face's PostScript name, as PDF names allow, into @p out;
 *         "Face" when it gives none. */
static void
ps_name(const unsigned char *d, size_t len, int index, char *out,
        size_t max)
{
    sf_table_t t[FONT_TABLES];
    const sf_table_t *nm = table(t, tables(d, len, index, t), "name");
    unsigned i, count = nm && nm->len >= 6 ? be16(d + nm->off + 2) : 0;
    size_t k = 0;

    for (i = 0; i < count && k == 0 && 18 + 12 * (size_t)i <= nm->len; i++) {
        const unsigned char *r = d + nm->off + 6 + 12 * i;
        size_t n = be16(r + 8), off = be16(d + nm->off + 4) + be16(r + 10);
        size_t j, step = be16(r) == 1 ? 1 : 2;

        if (be16(r + 6) != 6 || off + n > nm->len) {
            continue;
        }
        for (j = step - 1; j < n && k + 1 < max; j += step) {
            unsigned char c = d[nm->off + off + j];

            if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                (c >= '0' && c <= '9') || c == '-' || c == '_') {
                out[k++] = (char)c;
            }
        }
    }
    snprintf(out + k, max - k, "%s", k ? "" : "Face");
}

/** @brief The /W array: each used glyph's width, runs together. */
static void
widths(pw_buf_t *b, const pw_font_t *f)
{
    int g, open = 0;

    pw_printf(b, "/W [");
    for (g = 0; g < f->nglyph; g++) {
        if (!f->used[g]) {
            pw_printf(b, "%s", open ? "] " : "");
            open = 0;
            continue;
        }
        if (!open) {
            pw_printf(b, "%d [", g);
            open = 1;
        }
        pw_printf(b, "%ld ", (long)f->adv[g] * 1000 / f->upem);
    }
    pw_printf(b, "%s]", open ? "]" : "");
}

/** @brief The ToUnicode CMap: each used glyph's character, in blocks of
 *         a hundred as the format allows. */
static void
to_unicode(pw_buf_t *b, const pw_font_t *f)
{
    int g, left = 0, block = 0, size = 0;

    pw_printf(b, "/CIDInit /ProcSet findresource begin\n12 dict begin\n"
              "begincmap\n/CIDSystemInfo << /Registry (Adobe) /Ordering "
              "(UCS) /Supplement 0 >> def\n/CMapName /Adobe-Identity-UCS "
              "def\n/CMapType 2 def\n1 begincodespacerange\n<0000> <FFFF>\n"
              "endcodespacerange\n");
    for (g = 0; g < f->nglyph; g++) {
        left += f->used[g] && f->uni[g] != 0;
    }
    for (g = 0; g < f->nglyph; g++) {
        uint32_t u = f->uni[g];

        if (!f->used[g] || u == 0) {
            continue;
        }
        if (block == 0) {
            size = left < 100 ? left : 100;
            pw_printf(b, "%d beginbfchar\n", size);
        }
        if (u >= 0x10000) {
            pw_printf(b, "<%04X> <%04X%04X>\n", g, 0xd800 + ((u - 0x10000)
                      >> 10), 0xdc00 + ((u - 0x10000) & 0x3ff));
        } else {
            pw_printf(b, "<%04X> <%04X>\n", g, u);
        }
        left--;
        if (++block == size) {
            pw_printf(b, "endbfchar\n");
            block = 0;
        }
    }
    pw_printf(b, "endcmap\nCMapName currentdict /CMap defineresource pop\n"
              "end\nend\n");
}

/** @brief Six capitals naming the subset, from the glyphs it holds. */
static void
subset_tag(const pw_font_t *f, char *tag)
{
    uint32_t h = 2166136261u;
    int g, i;

    for (g = 0; g < f->nglyph; g++) {
        h = (h ^ (uint32_t)(f->used[g] ? g : 0)) * 16777619u;
    }
    for (i = 0; f->path[i]; i++) {
        h = (h ^ (unsigned char)f->path[i]) * 16777619u;
    }
    for (i = 0; i < 6; i++) {
        tag[i] = (char)('A' + h % 26);
        h /= 26;
    }
    tag[6] = '\0';
}

/** @brief The descriptor, the CID font and the Type0 font over program
 *         object @p file, the ToUnicode map object @p uni. */
static void
font_dicts(tiku_pdf_writer_t *w, pw_font_t *f, const unsigned char *d,
           size_t len, int file, int uni)
{
    sf_table_t t[FONT_TABLES];
    int n = tables(d, len, f->index, t), k;
    const sf_table_t *head = table(t, n, "head"), *hhea = table(t, n, "hhea");
    int desc = pw_reserve(w), cid = pw_reserve(w);
    char name[80], tag[7];
    pw_buf_t b;

    ps_name(d, len, f->index, name, sizeof name);
    subset_tag(f, tag);
    pw_begin(w, desc);
    pw_printf(&w->out, "<< /Type /FontDescriptor /FontName /%s+%s /Flags 4 "
              "/FontBBox [", tag, name);
    for (k = 0; k < 4; k++) {
        pw_printf(&w->out, "%ld ", head ? (long)(short)be16(d + head->off +
                  36 + 2 * k) * 1000 / f->upem : 0L);
    }
    k = hhea && hhea->len >= 8 ? (short)be16(d + hhea->off + 4) : 800;
    pw_printf(&w->out, "] /ItalicAngle 0 /Ascent %ld /Descent %ld "
              "/CapHeight %ld /StemV 80 /FontFile%s %d 0 R >>\nendobj\n",
              (long)k * 1000 / f->upem, hhea && hhea->len >= 8 ?
              (long)(short)be16(d + hhea->off + 6) * 1000 / f->upem : -200L,
              (long)k * 700 / f->upem, f->cff ? "3" : "2", file);
    memset(&b, 0, sizeof b);
    widths(&b, f);
    pw_begin(w, cid);
    pw_printf(&w->out, "<< /Type /Font /Subtype /CIDFontType%d /BaseFont "
              "/%s+%s /CIDSystemInfo << /Registry (Adobe) /Ordering "
              "(Identity) /Supplement 0 >> /FontDescriptor %d 0 R /DW 1000 ",
              f->cff ? 0 : 2, tag, name, desc);
    pw_put(&w->out, b.p, b.n);
    w->out.bad |= b.bad;            /* widths left out */
    pw_printf(&w->out, "%s >>\nendobj\n", f->cff ? "" :
              " /CIDToGIDMap /Identity");
    free(b.p);
    pw_begin(w, f->obj);
    pw_printf(&w->out, "<< /Type /Font /Subtype /Type0 /BaseFont /%s+%s "
              "/Encoding /Identity-H /DescendantFonts [%d 0 R] /ToUnicode "
              "%d 0 R >>\nendobj\n", tag, name, cid, uni);
}

void
pw_font_write(tiku_pdf_writer_t *w, pw_font_t *f)
{
    size_t len = 0, plen = 0;
    int oom = 0;
    unsigned char *d = source(f, &len, &oom), *prog;
    int file = pw_reserve(w), uni = pw_reserve(w);
    pw_buf_t cmap;

    prog = d != NULL ? program(d, len, f, &plen) : NULL;
    if (prog == NULL || file == 0 || uni == 0) {
        w->out.bad = 1;             /* its file unreadable now, or no room */
        free(d);
        free(prog);
        return;
    }
    pw_stream(w, file, f->cff ? "/Subtype /OpenType " : "", prog, plen, 1);
    free(prog);
    memset(&cmap, 0, sizeof cmap);
    to_unicode(&cmap, f);
    pw_stream(w, uni, "", cmap.p, cmap.n, 1);
    w->out.bad |= cmap.bad;
    free(cmap.p);
    font_dicts(w, f, d, len, file, uni);
    free(d);
}
