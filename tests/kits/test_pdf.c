/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_pdf.c - PDFs built byte by byte, and the ones on this machine.
 *
 * A one-page file with a classic table; the same with its table lying and
 * its Length wrong, so the reader has to find the objects itself; a file
 * with a cross-reference stream and an object stream, packed with a stored
 * DEFLATE block; every byte filter against known input; fonts read through
 * their encoding and through a ToUnicode map; the slips of careless writers;
 * a fax image as libtiff encodes one; layers on and off; annotations and
 * form fields that store no appearance, drawn as a viewer draws them; mesh
 * shadings; knockout groups.  Then, when TIKU_PDF_CORPUS names a folder,
 * every PDF in it is opened and its first page's text shown, which is how
 * the vendor documents are tried without being checked in.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#define _DEFAULT_SOURCE 1

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tiku_gfx.h"
#include "tiku_pdf.h"
#include "../../kits/translation/tiku_pdf_private.h"

static int fails;

static void
check(int cond, const char *what)
{
    printf("  [%s] PDF-001   %s\n", cond ? "PASS" : "FAIL", what);
    if (!cond) {
        fails++;
    }
}

/* ------------------------------------------------------------ building */

typedef struct {
    char *p;
    long  n, cap;
    long  off[64];
    int   nobj;
} pdfw_t;

static void
w(pdfw_t *b, const char *s, long n)
{
    if (b->n + n + 1 > b->cap) {
        b->cap = (b->n + n + 1) * 2;
        b->p = realloc(b->p, (size_t)b->cap);
    }
    memcpy(b->p + b->n, s, (size_t)n);
    b->n += n;
    b->p[b->n] = '\0';
}

static void
ws(pdfw_t *b, const char *s)
{
    w(b, s, (long)strlen(s));
}

/** @brief "N 0 obj" at the current offset, remembered for the table. */
static void
obj(pdfw_t *b, int num, const char *body)
{
    char head[32];

    b->off[num] = b->n;
    if (num >= b->nobj) {
        b->nobj = num + 1;
    }
    snprintf(head, sizeof head, "%d 0 obj\n", num);
    ws(b, head);
    ws(b, body);
    ws(b, "\nendobj\n");
}

/** @brief A stream object with the length it really has. */
static void
stream(pdfw_t *b, int num, const char *dict_inner, const char *data, long n)
{
    char head[512];

    b->off[num] = b->n;
    if (num >= b->nobj) {
        b->nobj = num + 1;
    }
    snprintf(head, sizeof head, "%d 0 obj\n<< %s /Length %ld >>\nstream\n",
             num, dict_inner, n);
    ws(b, head);
    w(b, data, n);
    ws(b, "\nendstream\nendobj\n");
}

/** @brief The table and trailer, with @p lie added to every offset. */
static void
xref(pdfw_t *b, int root, long lie)
{
    char line[64];
    long start = b->n;
    int i;

    snprintf(line, sizeof line, "xref\n0 %d\n", b->nobj);
    ws(b, line);
    ws(b, "0000000000 65535 f \n");
    for (i = 1; i < b->nobj; i++) {
        snprintf(line, sizeof line, "%010ld 00000 n \n", b->off[i] + lie);
        ws(b, line);
    }
    snprintf(line, sizeof line, "trailer\n<< /Size %d /Root %d 0 R >>\n"
             "startxref\n%ld\n%%%%EOF\n", b->nobj, root, start);
    ws(b, line);
}

/** @brief One page saying "Hello, PDF" with Helvetica, WinAnsi. */
static pdfw_t
simple_file(long lie, int wrong_length)
{
    pdfw_t b = {0};
    const char *content = "BT /F1 12 Tf 72 700 Td (Hello, PDF) Tj "
                          "0 -14 Td [(Sec) 20 (ond) -250 (line)] TJ ET";

    ws(&b, "%PDF-1.4\n%\xe2\xe3\xcf\xd3\n");
    obj(&b, 1, "<< /Type /Catalog /Pages 2 0 R >>");
    obj(&b, 2, "<< /Type /Pages /Kids [3 0 R] /Count 1 "
               "/MediaBox [0 0 300 400] >>");
    obj(&b, 3, "<< /Type /Page /Parent 2 0 R /Rotate 90 /Resources << /Font "
               "<< /F1 4 0 R >> >> /Contents 5 0 R >>");
    obj(&b, 4, "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica "
               "/Encoding /WinAnsiEncoding >>");
    if (wrong_length) {
        char head[64];

        b.off[5] = b.n;
        b.nobj = 6;
        snprintf(head, sizeof head, "5 0 obj\n<< /Length 999 >>\nstream\n");
        ws(&b, head);
        ws(&b, content);
        ws(&b, "\nendstream\nendobj\n");
    } else {
        stream(&b, 5, "", content, (long)strlen(content));
    }
    xref(&b, 1, lie);
    return b;
}

/** @brief A zlib stream holding @p n bytes stored, not compressed. */
static long
stored_zlib(const char *in, long n, char *out)
{
    long k = 0;

    out[k++] = 0x78;
    out[k++] = 0x01;
    out[k++] = 0x01;                /* last block, stored */
    out[k++] = (char)(n & 0xff);
    out[k++] = (char)((n >> 8) & 0xff);
    out[k++] = (char)(~n & 0xff);
    out[k++] = (char)((~n >> 8) & 0xff);
    memcpy(out + k, in, (size_t)n);
    k += n;
    out[k++] = 0;                   /* an Adler-32 nobody checks */
    out[k++] = 0;
    out[k++] = 0;
    out[k++] = 1;
    return k;
}

/**
 * @brief A PDF 1.5 file: catalog, pages and page packed in an object
 *        stream, located by a cross-reference stream.
 */
static pdfw_t
packed_file(void)
{
    pdfw_t b = {0};
    const char *objs = "<< /Type /Catalog /Pages 2 0 R >> "
                       "<< /Type /Pages /Kids [3 0 R] /Count 1 >> "
                       "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 100] "
                       "/Contents 5 0 R /Resources << /Font << /F1 << /Type "
                       "/Font /Subtype /TrueType /BaseFont /Arial /FirstChar "
                       "65 /Widths [667 667] /Encoding << /Differences [65 "
                       "/alpha /beta] >> >> >> >> >>";
    const char *content = "BT /F1 10 Tf 10 50 Td (AB) Tj ET";
    char header[64], packed[1024], z[1200], xr[256], dict[256];
    long n, zn, xrn = 0, i, xoff;
    long o1 = 0, o2, o3;

    o2 = (long)strlen("<< /Type /Catalog /Pages 2 0 R >> ");
    o3 = o2 + (long)strlen("<< /Type /Pages /Kids [3 0 R] /Count 1 >> ");
    snprintf(header, sizeof header, "1 %ld 2 %ld 3 %ld ", o1, o2, o3);
    n = (long)strlen(header);
    memcpy(packed, header, (size_t)n);
    memcpy(packed + n, objs, strlen(objs));
    zn = stored_zlib(packed, n + (long)strlen(objs), z);
    ws(&b, "%PDF-1.5\n");
    snprintf(dict, sizeof dict, "/Type /ObjStm /N 3 /First %ld /Filter "
             "/FlateDecode", n);
    stream(&b, 4, dict, z, zn);
    stream(&b, 5, "", content, (long)strlen(content));
    /* Entries: type 1 byte, field 2 four bytes, field 3 one byte. */
    xoff = b.n;
    for (i = 0; i < 7; i++) {
        int type = (i == 0) ? 0 : (i >= 1 && i <= 3) ? 2 : 1;
        long f2 = (i >= 1 && i <= 3) ? 4 : (i == 4 || i == 5) ? b.off[i]
                  : (i == 6) ? xoff : 0;
        long f3 = (i >= 1 && i <= 3) ? i - 1 : 0;

        xr[xrn++] = (char)type;
        xr[xrn++] = (char)((f2 >> 24) & 0xff);
        xr[xrn++] = (char)((f2 >> 16) & 0xff);
        xr[xrn++] = (char)((f2 >> 8) & 0xff);
        xr[xrn++] = (char)(f2 & 0xff);
        xr[xrn++] = (char)f3;
    }
    snprintf(dict, sizeof dict, "/Type /XRef /Size 7 /W [1 4 1] /Root 1 0 R");
    stream(&b, 6, dict, xr, xrn);
    snprintf(dict, sizeof dict, "startxref\n%ld\n%%%%EOF\n", xoff);
    ws(&b, dict);
    return b;
}

/* ------------------------------------------------------------- checks */

static tiku_pdf_t *
open_bytes(const pdfw_t *b, tiku_pdf_err_t *err)
{
    tiku_pdf_t *doc = NULL;

    *err = tiku_pdf_decode(b->p, (size_t)b->n, &doc);
    return doc;
}

static void
simple_test(void)
{
    pdfw_t b = simple_file(0, 0);
    tiku_pdf_err_t err;
    tiku_pdf_t *doc = open_bytes(&b, &err);
    double w = 0, h = 0;
    int rot = -1;
    char *text;

    check(err == TIKU_PDF_OK && doc != NULL, "a one-page file opens");
    if (doc == NULL) {
        free(b.p);
        return;
    }
    check(tiku_pdf_pages(doc) == 1 && !tiku_pdf_repaired(doc),
          "with one page, its table believed");
    check(tiku_pdf_page_size(doc, 0, &w, &h, &rot) == 0 && w == 300 &&
          h == 400 && rot == 90, "the box comes from the parent, Rotate 90");
    text = tiku_pdf_page_text(doc, 0);
    check(text != NULL && strcmp(text, "Hello, PDF\nSecond line") == 0,
          "its words, a kerned pair joined and a gap made a space");
    if (text != NULL && strcmp(text, "Hello, PDF\nSecond line") != 0) {
        printf("    got [%s]\n", text);
    }
    free(text);
    check(tiku_pdf_page_size(doc, 1, &w, &h, &rot) < 0, "page 1 is not there");
    tiku_pdf_close(doc);
    free(b.p);
}

static void
repair_test(void)
{
    pdfw_t b = simple_file(7, 1);
    tiku_pdf_err_t err;
    tiku_pdf_t *doc = open_bytes(&b, &err);
    char *text;

    check(err == TIKU_PDF_OK && doc != NULL && tiku_pdf_repaired(doc),
          "a table off by seven bytes is rebuilt from the file");
    text = doc ? tiku_pdf_page_text(doc, 0) : NULL;
    check(text != NULL && strncmp(text, "Hello, PDF", 10) == 0,
          "and a Length of 999 finds endstream instead");
    free(text);
    tiku_pdf_close(doc);
    free(b.p);
    b = simple_file(0, 0);
    memcpy(b.p + b.n - 12, "startxref\n99", 12);
    doc = open_bytes(&b, &err);
    check(err == TIKU_PDF_OK && doc != NULL && tiku_pdf_pages(doc) == 1,
          "a startxref pointing at nothing is rebuilt too");
    tiku_pdf_close(doc);
    free(b.p);
}

static void
packed_test(void)
{
    pdfw_t b = packed_file();
    tiku_pdf_err_t err;
    tiku_pdf_t *doc = open_bytes(&b, &err);
    char *text;

    check(err == TIKU_PDF_OK && doc != NULL && !tiku_pdf_repaired(doc),
          "a cross-reference stream over an object stream opens");
    check(doc != NULL && tiku_pdf_pages(doc) == 1, "and finds the packed page");
    text = doc ? tiku_pdf_page_text(doc, 0) : NULL;
    check(text != NULL && strcmp(text, "\xce\xb1\xce\xb2") == 0,
          "Differences name the glyphs: A is alpha, B is beta");
    free(text);
    tiku_pdf_close(doc);
    free(b.p);
}

static void
not_pdf_test(void)
{
    tiku_pdf_t *doc = NULL;
    const char *junk = "GIF89a not a document at all";

    check(tiku_pdf_decode(junk, strlen(junk), &doc) == TIKU_PDF_NOT_PDF &&
          doc == NULL, "bytes without a header are not a PDF");
    check(tiku_pdf_decode("%PDF-1.4\n", 9, &doc) == TIKU_PDF_BAD &&
          doc == NULL, "a header with nothing behind it is bad, not open");
}

/** @brief One filter over a stream object, through the document. */
static char *
filtered(const char *filter, const char *data, long n, long *out_n)
{
    pdfw_t b = {0};
    char dict[128];
    tiku_pdf_t *doc = NULL;
    unsigned char *out = NULL;
    char *copy = NULL;

    ws(&b, "%PDF-1.4\n");
    obj(&b, 1, "<< /Type /Catalog /Pages 2 0 R >>");
    obj(&b, 2, "<< /Type /Pages /Kids [3 0 R] /Count 1 >>");
    obj(&b, 3, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 10 10] >>");
    snprintf(dict, sizeof dict, "/Filter /%s", filter);
    stream(&b, 4, dict, data, n);
    xref(&b, 1, 0);
    if (tiku_pdf_decode(b.p, (size_t)b.n, &doc) == TIKU_PDF_OK &&
        pdf_stream_data(doc, pdf_object(doc, 4), &out, out_n, NULL) == 0) {
        copy = malloc((size_t)*out_n + 1);
        memcpy(copy, out, (size_t)*out_n);
        copy[*out_n] = '\0';
        free(out);
    }
    tiku_pdf_close(doc);
    free(b.p);
    return copy;
}

static void
filter_test(void)
{
    long n = 0;
    char *s;
    /* "ABBBBBBBBBBBC" as run lengths: A alone, twelve Bs, C alone. */
    const char rl[] = "\x00" "A" "\xf5" "B" "\x00" "C" "\x80";
    /* LZW of "-----A---B" from the standard's own example. */
    const unsigned char lz[] = {0x80, 0x0B, 0x60, 0x50, 0x22, 0x0C, 0x0C,
                                0x85, 0x01};

    s = filtered("ASCIIHexDecode", "48656C6C 6F>", 12, &n);
    check(s != NULL && n == 5 && strcmp(s, "Hello") == 0, "hex pairs");
    free(s);
    s = filtered("ASCII85Decode", "87cURD]i,\"Ebo80~>", 17, &n);
    check(s != NULL && n == 12 && strcmp(s, "Hello World!") == 0,
          "base 85, the standard's example");
    free(s);
    s = filtered("ASCII85Decode", "88/~>", 5, &n);
    check(s != NULL && n == 2 && strcmp(s, "Hi") == 0,
          "base 85 with a short last group");
    free(s);
    s = filtered("RunLengthDecode", rl, (long)sizeof rl - 1, &n);
    check(s != NULL && n == 14 && strcmp(s, "ABBBBBBBBBBBBC") == 0,
          "run lengths");
    free(s);
    s = filtered("LZWDecode", (const char *)lz, (long)sizeof lz, &n);
    check(s != NULL && n == 10 && strcmp(s, "-----A---B") == 0,
          "LZW, the standard's example");
    free(s);
    {
        char z[64];
        long zn = stored_zlib("flate stored", 12, z);

        s = filtered("FlateDecode", z, zn, &n);
        check(s != NULL && n == 12 && strcmp(s, "flate stored") == 0,
              "Flate through a stored block, size unknown in advance");
        free(s);
    }
}

static void
glyph_test(void)
{
    check(pdf_glyph_unicode("fi") == 0xFB01 && pdf_glyph_unicode("A") == 'A' &&
          pdf_glyph_unicode("endash") == 0x2013 &&
          pdf_glyph_unicode("uni2264") == 0x2264 &&
          pdf_glyph_unicode("a.sc") == 'a' && pdf_glyph_unicode("g42") == 0,
          "glyph names: the list, one letter, uniXXXX, a suffix, a number");
}

/* ------------------------------------------------------------ drawing */

static uint32_t
pixel(const tiku_pdf_pixmap_t *pm, int x, int y)
{
    return (x >= 0 && y >= 0 && x < pm->w && y < pm->h)
           ? pm->px[(size_t)y * pm->w + x] & 0xffffffu : 0xdeadbeefu;
}

/** @brief Write @p pm as a PNG, for eyes; @return 0 when it was. */
static int
png_out(const tiku_pdf_pixmap_t *pm, const char *path)
{
    tiku_surface_t *s = tiku_surface_new(pm->w, pm->h, TIKU_C_DOC);
    int rc;

    if (s == NULL) {
        return -1;
    }
    memcpy(s->px, pm->px, (size_t)pm->w * pm->h * sizeof *pm->px);
    rc = tiku_surface_png(s, path);
    tiku_surface_free(s);
    return rc;
}

/**
 * @brief Shadings and patterns on a 300-point page: an axial pattern with
 *        one end extended, a radial shading by sh in a clip, a coloured
 *        and an uncoloured tiling, and a fill at half its alpha.
 */
static pdfw_t
shade_file(void)
{
    pdfw_t b = {0};
    const char *content =
        "/Pattern cs /Ax scn 0 200 100 100 re f "
        "q 150 150 100 100 re W n /Rd sh Q "
        "/Pattern cs /Tl scn 0 0 100 100 re f "
        "/PU cs 0 1 0 /Un scn 150 0 100 100 re f "
        "/Half gs 1 0 0 rg 0 110 100 60 re f";
    const char *cell = "0 0 0 rg 0 0 5 5 re f";
    const char *stencil = "1 0 0 rg 0 0 5 5 re f";

    ws(&b, "%PDF-1.4\n");
    obj(&b, 1, "<< /Type /Catalog /Pages 2 0 R >>");
    obj(&b, 2, "<< /Type /Pages /Kids [3 0 R] /Count 1 >>");
    obj(&b, 3, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 300 300] "
               "/Resources << /Pattern << /Ax 5 0 R /Tl 6 0 R /Un 7 0 R >> "
               "/Shading << /Rd 8 0 R >> /ExtGState << /Half << /ca 0.5 >> "
               ">> /ColorSpace << /PU [/Pattern /DeviceRGB] >> >> "
               "/Contents 4 0 R >>");
    stream(&b, 4, "", content, (long)strlen(content));
    obj(&b, 5, "<< /PatternType 2 /Shading << /ShadingType 2 /ColorSpace "
               "/DeviceRGB /Coords [20 0 80 0] /Extend [false true] "
               "/Function << /FunctionType 2 /Domain [0 1] /C0 [1 0 0] "
               "/C1 [0 0 1] /N 1 >> >> >>");
    stream(&b, 6, "/PatternType 1 /PaintType 1 /TilingType 1 /BBox [0 0 10 10]"
                  " /XStep 10 /YStep 10 /Resources << >>", cell,
           (long)strlen(cell));
    /* Uncoloured: the red it asks for is not its to choose. */
    stream(&b, 7, "/PatternType 1 /PaintType 2 /TilingType 1 /BBox [0 0 10 10]"
                  " /XStep 10 /YStep 10 /Resources << >>", stencil,
           (long)strlen(stencil));
    obj(&b, 8, "<< /ShadingType 3 /ColorSpace /DeviceRGB "
               "/Coords [200 200 0 200 200 40] /Function << /FunctionType 2 "
               "/Domain [0 1] /C0 [0 1 0] /C1 [0 0 1] /N 1 >> >>");
    xref(&b, 1, 0);
    return b;
}

/** @brief Whether @p c is within @p tol of @p r, @p g, @p b. */
static int
near(uint32_t c, int r, int g, int b, int tol)
{
    return abs((int)(c >> 16 & 255) - r) <= tol &&
           abs((int)(c >> 8 & 255) - g) <= tol &&
           abs((int)(c & 255) - b) <= tol;
}

static void
shade_test(void)
{
    pdfw_t b = shade_file();
    tiku_pdf_err_t err;
    tiku_pdf_t *doc = open_bytes(&b, &err);
    tiku_pdf_pixmap_t pm = {0, 0, NULL};

    check(doc != NULL && tiku_pdf_render(doc, 0, 1.0, 0, &pm) == 0 &&
          pm.w == 300, "a page of shadings and patterns renders");
    if (pm.px == NULL) {
        tiku_pdf_close(doc);
        free(b.p);
        return;
    }
    /* Axial along x 20..80, drawn in the pattern fill (device y 0..100). */
    check(pixel(&pm, 10, 50) == 0xffffffu,
          "an axial shading leaves what is before an end not extended");
    check(near(pixel(&pm, 50, 50), 127, 0, 128, 3),
          "and is half one colour, half the other, at its middle");
    check(pixel(&pm, 95, 50) == 0x0000ffu, "and carries the extended end on");
    /* Radial about user 200, 200 (device 200, 100), radius 0 to 40. */
    check(near(pixel(&pm, 200, 100), 0, 255, 0, 8) &&
          near(pixel(&pm, 230, 100), 0, 64, 191, 8),
          "a radial shading by sh, green at its centre to blue at 40");
    check(pixel(&pm, 246, 100) == 0xffffffu &&
          pixel(&pm, 140, 100) == 0xffffffu,
          "and nothing past its outer circle or outside the clip");
    /* The coloured cell: black 0..5 of every 10, both ways. */
    check(pixel(&pm, 2, 298) == 0 && pixel(&pm, 12, 288) == 0 &&
          pixel(&pm, 7, 298) == 0xffffffu && pixel(&pm, 2, 293) == 0xffffffu,
          "a coloured tiling repeats its cell");
    check(pixel(&pm, 152, 298) == 0x00ff00u &&
          pixel(&pm, 157, 298) == 0xffffffu,
          "an uncoloured one paints the colour scn gave it");
    check(near(pixel(&pm, 50, 160), 255, 127, 127, 2),
          "a fill at half its alpha lets half the paper through");
    tiku_pdf_pixmap_free(&pm);
    tiku_pdf_close(doc);
    free(b.p);
}

/**
 * @brief Fill and stroke spaces set by rg and RG, then 64 gray fills
 *        inside q...Q, then a green fill and stroke set by sc and SC in
 *        the spaces the restored state uses.
 */
static void
cs_keep_test(void)
{
    pdfw_t b = {0};
    char content[1024] = "0 0 1 rg 0 0 1 RG q ";
    tiku_pdf_err_t err;
    tiku_pdf_t *doc;
    tiku_pdf_pixmap_t pm = {0, 0, NULL};
    int i;

    for (i = 0; i < 64; i++) {
        strcat(content, "0.5 g ");
    }
    strcat(content, "Q 0 1 0 sc 0 1 0 SC 0 0 100 100 re f "
                    "20 w 150 50 m 250 50 l S");
    ws(&b, "%PDF-1.4\n");
    obj(&b, 1, "<< /Type /Catalog /Pages 2 0 R >>");
    obj(&b, 2, "<< /Type /Pages /Kids [3 0 R] /Count 1 >>");
    obj(&b, 3, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 300 300] "
               "/Contents 4 0 R >>");
    stream(&b, 4, "", content, (long)strlen(content));
    xref(&b, 1, 0);
    doc = open_bytes(&b, &err);
    check(doc != NULL && tiku_pdf_render(doc, 0, 1.0, 0, &pm) == 0 &&
          pixel(&pm, 50, 250) == 0x00ff00u &&
          pixel(&pm, 200, 250) == 0x00ff00u,
          "sc and SC after 64 colour spaces in q...Q use the restored ones");
    tiku_pdf_pixmap_free(&pm);
    tiku_pdf_close(doc);
    free(b.p);
}

/**
 * @brief Transparency and annotations on a 300-point page: Multiply and
 *        Difference, a luminosity and an alpha soft mask and their end, a
 *        group at half its alpha, and four annotations, two of them unseen.
 */
static pdfw_t
transp_file(void)
{
    pdfw_t b = {0};
    const char *content =
        "1 0 0 rg 0 250 40 40 re f q /M gs 0 0 1 rg 20 250 40 40 re f Q "
        "1 0 0 rg 100 250 40 40 re f q /D gs 1 1 1 rg 120 250 40 40 re f Q "
        "q /Lum gs 1 0 0 rg 0 150 100 50 re f /None gs 0 0 1 rg "
        "0 100 100 30 re f Q "
        "q /Al gs 1 0 0 rg 150 150 100 50 re f Q "
        "q /Half gs /Grp Do Q";
    const char *lum = "1 g 0 150 50 50 re f";
    const char *alpha = "q /Half gs 0 g 150 150 100 50 re f Q";
    const char *grp = "1 0 0 rg 150 20 60 60 re f 0 0 1 rg 180 20 60 60 re f";
    const char *green = "0 1 0 rg 0 0 10 10 re f";
    const char *red = "1 0 0 rg 0 0 10 10 re f";
    const char *blue = "0 0 1 rg 0 0 10 10 re f";

    ws(&b, "%PDF-1.4\n");
    obj(&b, 1, "<< /Type /Catalog /Pages 2 0 R >>");
    obj(&b, 2, "<< /Type /Pages /Kids [3 0 R] /Count 1 >>");
    obj(&b, 3, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 300 300] "
               "/Resources << /ExtGState << /M << /BM /Multiply >> "
               "/D << /BM [/Difference] >> /Half << /ca 0.5 >> "
               "/None << /SMask /None >> "
               "/Lum << /SMask << /S /Luminosity /G 10 0 R >> >> "
               "/Al << /SMask << /S /Alpha /G 11 0 R >> >> >> "
               "/XObject << /Grp 12 0 R >> >> /Contents 4 0 R "
               "/Annots [20 0 R 21 0 R 22 0 R 23 0 R] >>");
    stream(&b, 4, "", content, (long)strlen(content));
    stream(&b, 10, "/Type /XObject /Subtype /Form /BBox [0 0 300 300] "
                   "/Group << /S /Transparency >>", lum, (long)strlen(lum));
    stream(&b, 11, "/Type /XObject /Subtype /Form /BBox [0 0 300 300] "
                   "/Group << /S /Transparency >> /Resources << /ExtGState "
                   "<< /Half << /ca 0.5 >> >> >>", alpha, (long)strlen(alpha));
    stream(&b, 12, "/Type /XObject /Subtype /Form /BBox [0 0 300 300] "
                   "/Group << /S /Transparency >>", grp, (long)strlen(grp));
    obj(&b, 20, "<< /Type /Annot /Subtype /Square /Rect [20 20 70 40] "
                "/AP << /N 30 0 R >> >>");
    obj(&b, 21, "<< /Type /Annot /Subtype /Widget /Rect [80 20 120 40] "
                "/AP << /N << /On 31 0 R /Off 32 0 R >> >> /AS /Off >>");
    obj(&b, 22, "<< /Type /Annot /Subtype /Square /F 2 /Rect [20 50 60 70] "
                "/AP << /N 31 0 R >> >>");
    obj(&b, 23, "<< /Type /Annot /Subtype /Popup /Rect [80 50 120 70] "
                "/AP << /N 31 0 R >> >>");
    stream(&b, 30, "/Type /XObject /Subtype /Form /BBox [0 0 10 10]", green,
           (long)strlen(green));
    stream(&b, 31, "/Type /XObject /Subtype /Form /BBox [0 0 10 10]", red,
           (long)strlen(red));
    stream(&b, 32, "/Type /XObject /Subtype /Form /BBox [0 0 10 10]", blue,
           (long)strlen(blue));
    xref(&b, 1, 0);
    return b;
}

static void
transp_test(void)
{
    pdfw_t b = transp_file();
    tiku_pdf_err_t err;
    tiku_pdf_t *doc = open_bytes(&b, &err);
    tiku_pdf_pixmap_t pm = {0, 0, NULL};

    check(doc != NULL && tiku_pdf_render(doc, 0, 1.0, 0, &pm) == 0 &&
          pm.w == 300, "a page of transparency and annotations renders");
    if (pm.px == NULL) {
        tiku_pdf_close(doc);
        free(b.p);
        return;
    }
    check(pixel(&pm, 10, 30) == 0xff0000u && pixel(&pm, 30, 30) == 0 &&
          pixel(&pm, 50, 30) == 0x0000ffu,
          "Multiply: blue on red is black, blue on paper stays blue");
    check(pixel(&pm, 130, 30) == 0x00ffffu && pixel(&pm, 150, 30) == 0,
          "Difference: white on red is cyan, white on paper black");
    check(pixel(&pm, 25, 125) == 0xff0000u &&
          pixel(&pm, 75, 125) == 0xffffffu,
          "a luminosity mask lets the fill through where its group is white");
    check(pixel(&pm, 75, 185) == 0x0000ffu, "and SMask None takes it away");
    check(near(pixel(&pm, 200, 125), 255, 127, 127, 3),
          "an alpha mask from a half-alpha group lets half through");
    check(near(pixel(&pm, 165, 250), 255, 127, 127, 3) &&
          near(pixel(&pm, 225, 250), 127, 127, 255, 3),
          "a group at half its alpha shows each part at half");
    check(near(pixel(&pm, 195, 250), 127, 127, 255, 3),
          "and where its parts overlap, the top one at half: as one");
    check(pixel(&pm, 45, 270) == 0x00ff00u && pixel(&pm, 15, 270) == 0xffffffu,
          "an appearance fills its annotation's Rect, scaled from its BBox");
    check(pixel(&pm, 100, 270) == 0x0000ffu,
          "a widget shows the appearance of the state its AS names");
    check(pixel(&pm, 40, 240) == 0xffffffu && pixel(&pm, 100, 240) == 0xffffffu,
          "hidden annotations and popups are not drawn");
    tiku_pdf_pixmap_free(&pm);
    tiku_pdf_close(doc);
    free(b.p);
}

/**
 * @brief Knockout groups (ISO 32000 11.4.8) on a 300-point page, each a
 *        red and a blue square at half their alpha overlapping: a group
 *        knocking out, one isolated too, one over green, one that does
 *        not, one holding a group of its own, and one of opaque squares.
 */
static pdfw_t
knockout_file(void)
{
    static const struct { int num; const char *group, *body; } forms[6] = {
        {10, "/K true", "/Half gs 1 0 0 rg 0 250 40 40 re f "
                        "0 0 1 rg 20 250 40 40 re f"},
        {11, "/K true /I true", "/Half gs 1 0 0 rg 100 250 40 40 re f "
                                "0 0 1 rg 120 250 40 40 re f"},
        {12, "/K true", "/Half gs 1 0 0 rg 200 250 40 40 re f "
                        "0 0 1 rg 220 250 40 40 re f"},
        {13, "", "/Half gs 1 0 0 rg 0 190 40 40 re f "
                 "0 0 1 rg 20 190 40 40 re f"},
        {14, "/K true", "/In Do /Half gs 0 1 0 rg 135 190 40 40 re f"},
        {16, "/K true", "1 0 0 rg 200 190 40 40 re f "
                        "0 0 1 rg 220 190 40 40 re f"},
    };
    const char *content = "/K1 Do /K2 Do 0 1 0 rg 200 250 60 40 re f /K3 Do "
                          "/P1 Do /N1 Do /O1 Do";
    const char *inner = "/Half gs 1 0 0 rg 100 190 40 40 re f "
                        "0 0 1 rg 120 190 40 40 re f";
    const char *form = "/Type /XObject /Subtype /Form /BBox [0 0 300 300] "
                       "/Resources << /ExtGState << /Half << /ca 0.5 >> >> "
                       "/XObject << /In 15 0 R >> >> /Group << /S "
                       "/Transparency";
    pdfw_t b = {0};
    char dict[320];
    int i;

    ws(&b, "%PDF-1.4\n");
    obj(&b, 1, "<< /Type /Catalog /Pages 2 0 R >>");
    obj(&b, 2, "<< /Type /Pages /Kids [3 0 R] /Count 1 >>");
    obj(&b, 3, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 300 300] "
               "/Resources << /XObject << /K1 10 0 R /K2 11 0 R /K3 12 0 R "
               "/P1 13 0 R /N1 14 0 R /O1 16 0 R >> >> /Contents 4 0 R >>");
    stream(&b, 4, "", content, (long)strlen(content));
    for (i = 0; i < 6; i++) {
        snprintf(dict, sizeof dict, "%s %s >>", form, forms[i].group);
        stream(&b, forms[i].num, dict, forms[i].body,
               (long)strlen(forms[i].body));
    }
    snprintf(dict, sizeof dict, "%s >>", form);
    stream(&b, 15, dict, inner, (long)strlen(inner));
    xref(&b, 1, 0);
    return b;
}

static void
knockout_test(void)
{
    pdfw_t b = knockout_file();
    tiku_pdf_err_t err;
    tiku_pdf_t *doc = open_bytes(&b, &err);
    tiku_pdf_pixmap_t pm = {0, 0, NULL};

    check(doc != NULL && tiku_pdf_render(doc, 0, 1.0, 0, &pm) == 0 &&
          pm.w == 300, "a page of knockout groups renders");
    if (pm.px == NULL) {
        tiku_pdf_close(doc);
        free(b.p);
        return;
    }
    check(near(pixel(&pm, 10, 30), 255, 127, 127, 3) &&
          near(pixel(&pm, 30, 30), 127, 127, 255, 3) &&
          near(pixel(&pm, 50, 30), 127, 127, 255, 3),
          "in a knockout group the blue at half hides the red it covers: "
          "blue at half on the paper");
    check(near(pixel(&pm, 110, 30), 255, 127, 127, 3) &&
          near(pixel(&pm, 130, 30), 127, 127, 255, 3),
          "and in an isolated one alike");
    check(near(pixel(&pm, 210, 30), 127, 127, 0, 3) &&
          near(pixel(&pm, 230, 30), 0, 127, 127, 3),
          "one over green lays the blue at half on the green alone");
    check(near(pixel(&pm, 30, 90), 127, 63, 191, 3),
          "a group that does not knock out shows the two together");
    check(near(pixel(&pm, 110, 90), 255, 127, 127, 3) &&
          near(pixel(&pm, 128, 90), 127, 63, 191, 3) &&
          near(pixel(&pm, 150, 90), 127, 255, 127, 3),
          "a group in a knockout group is one thing: its parts show "
          "together, and the green after hides the lot");
    check(pixel(&pm, 210, 90) == 0xff0000u && pixel(&pm, 230, 90) == 0x0000ffu,
          "opaque paints come out of a knockout group exactly");
    tiku_pdf_pixmap_free(&pm);
    tiku_pdf_close(doc);
    free(b.p);
}

#include "../fixtures/fax.inc"

/** @brief One page: a Group 4 fax as a one-bit grey image above, and as
 *         a stencil painted blue below, both at a pixel a sample. */
static pdfw_t
fax_file(void)
{
    pdfw_t b = {0};
    const char *draw = "q 150 0 0 24 0 24 cm /Im1 Do Q "
                       "q 0 0 1 rg 150 0 0 24 0 0 cm /Im2 Do Q";

    ws(&b, "%PDF-1.4\n");
    obj(&b, 1, "<< /Type /Catalog /Pages 2 0 R >>");
    obj(&b, 2, "<< /Type /Pages /Kids [3 0 R] /Count 1 >>");
    obj(&b, 3, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 150 48] "
               "/Resources << /XObject << /Im1 5 0 R /Im2 6 0 R >> >> "
               "/Contents 4 0 R >>");
    stream(&b, 4, "", draw, (long)strlen(draw));
    stream(&b, 5, "/Type /XObject /Subtype /Image /Width 150 /Height 24 "
                  "/ColorSpace /DeviceGray /BitsPerComponent 1 "
                  "/Filter /CCITTFaxDecode /DecodeParms << /K -1 "
                  "/Columns 150 >>", (const char *)fax_small_g4,
           (long)sizeof fax_small_g4);
    stream(&b, 6, "/Type /XObject /Subtype /Image /Width 150 /Height 24 "
                  "/ImageMask true /Filter /CCITTFaxDecode "
                  "/DecodeParms << /K -1 /Columns 150 >>",
           (const char *)fax_small_g4, (long)sizeof fax_small_g4);
    xref(&b, 1, 0);
    return b;
}

/** @brief The fax page drawn: every pixel of both as the pattern says. */
static void
fax_test(void)
{
    pdfw_t b = fax_file();
    tiku_pdf_err_t err;
    tiku_pdf_t *doc = open_bytes(&b, &err);
    tiku_pdf_pixmap_t pm = {0, 0, NULL};
    long grey = 0, stencil = 0;
    int x, y;

    check(doc != NULL && tiku_pdf_render(doc, 0, 1.0, 0, &pm) == 0 &&
          pm.w == FAX_SMALL_W && pm.h == 2 * FAX_SMALL_H,
          "a page of CCITT fax images renders");
    for (y = 0; pm.px != NULL && y < FAX_SMALL_H; y++) {
        for (x = 0; x < FAX_SMALL_W; x++) {
            int ink = fax_pixel(0, x, y);

            grey += pixel(&pm, x, y) != (ink ? 0x000000u : 0xffffffu);
            stencil += pixel(&pm, x, FAX_SMALL_H + y) !=
                       (ink ? 0x0000ffu : 0xffffffu);
        }
    }
    check(pm.px != NULL && grey == 0,
          "a Group 4 image draws as libtiff wrote it, black on white");
    check(pm.px != NULL && stencil == 0,
          "and as a stencil, the fill colour where it is black");
    tiku_pdf_pixmap_free(&pm);
    tiku_pdf_close(doc);
    free(b.p);
}

/**
 * @brief Layers: groups on and off by the default configuration, one
 *        off only when viewed (its AS usage), membership dictionaries by
 *        policy and by expression, and forms, an annotation and text in
 *        a layer that is off.
 */
static pdfw_t
oc_file(void)
{
    pdfw_t b = {0};
    const char *draw =
        "/OC /pA BDC 1 0 0 rg 5 35 30 30 re f EMC "
        "/OC /pB BDC 1 0 0 rg 41 35 30 30 re f EMC "
        "/OC /pC BDC 1 0 0 rg 77 35 30 30 re f EMC "
        "/OC /m1 BDC 1 0 0 rg 113 35 30 30 re f EMC "
        "/OC /m2 BDC 1 0 0 rg 149 35 30 30 re f EMC "
        "/OC /m3 BDC 1 0 0 rg 185 35 30 30 re f EMC "
        "/OC /m4 BDC 1 0 0 rg 221 35 30 30 re f EMC "
        "/OC /pB BDC /Span BMC 1 0 0 rg 257 35 30 30 re f EMC EMC "
        "0 1 0 rg 257 5 30 20 re f "
        "q 1 0 0 1 5 0 cm /FmB Do Q q 1 0 0 1 41 0 cm /FmN Do Q "
        "/OC /pB BDC BT /F1 10 Tf 5 80 Td (Hidden) Tj ET EMC "
        "BT /F1 10 Tf 100 80 Td (Shown) Tj ET";
    const char *fill_red = "1 0 0 rg 0 0 30 30 re f";
    const char *fill_green = "0 1 0 rg 0 0 30 30 re f";

    ws(&b, "%PDF-1.7\n");
    obj(&b, 1, "<< /Type /Catalog /Pages 2 0 R /OCProperties << "
               "/OCGs [10 0 R 11 0 R 12 0 R] /D << /OFF [11 0 R] "
               "/AS [<< /Event /View /Category [/View] /OCGs [12 0 R] >>] "
               ">> >> >>");
    obj(&b, 2, "<< /Type /Pages /Kids [3 0 R] /Count 1 >>");
    obj(&b, 3, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 300 100] "
               "/Contents 4 0 R /Annots [30 0 R] /Resources << /Font << "
               "/F1 << /Type /Font /Subtype /Type1 /BaseFont /Helvetica >> "
               ">> /XObject << /FmB 5 0 R /FmN 6 0 R >> /Properties << "
               "/pA 10 0 R /pB 11 0 R /pC 12 0 R /m1 20 0 R /m2 21 0 R "
               "/m3 22 0 R /m4 23 0 R >> >> >>");
    stream(&b, 4, "", draw, (long)strlen(draw));
    stream(&b, 5, "/Type /XObject /Subtype /Form /BBox [0 0 30 30] "
                  "/OC 11 0 R", fill_red, (long)strlen(fill_red));
    stream(&b, 6, "/Type /XObject /Subtype /Form /BBox [0 0 30 30]",
           fill_green, (long)strlen(fill_green));
    obj(&b, 10, "<< /Type /OCG /Name (A) >>");
    obj(&b, 11, "<< /Type /OCG /Name (B) >>");
    obj(&b, 12, "<< /Type /OCG /Name (C) /Usage << /View << /ViewState "
                "/OFF >> >> >>");
    obj(&b, 20, "<< /Type /OCMD /OCGs [10 0 R 11 0 R] /P /AllOn >>");
    obj(&b, 21, "<< /Type /OCMD /OCGs [10 0 R 11 0 R] /P /AnyOn >>");
    obj(&b, 22, "<< /Type /OCMD /VE [/Not 11 0 R] >>");
    obj(&b, 23, "<< /Type /OCMD /VE [/And 10 0 R 11 0 R] >>");
    obj(&b, 30, "<< /Type /Annot /Subtype /Square /Rect [200 0 230 30] "
                "/OC 11 0 R /AP << /N 31 0 R >> >>");
    stream(&b, 31, "/Type /XObject /Subtype /Form /BBox [0 0 30 30]",
           fill_red, (long)strlen(fill_red));
    xref(&b, 1, 0);
    return b;
}

/** @brief The layers page drawn and read: what is off is neither. */
static void
oc_test(void)
{
    static const struct { int x, y, on; const char *what; } probe[] = {
        {20, 50, 1, "a group on shows"},
        {56, 50, 0, "a group the configuration sets off does not"},
        {92, 50, 0, "nor one whose usage is off when viewed"},
        {128, 50, 0, "AllOn of an on and an off group is off"},
        {164, 50, 1, "AnyOn of them is on"},
        {200, 50, 1, "an expression Not of the off group is on"},
        {236, 50, 0, "And of the on and the off group is off"},
        {272, 50, 0, "marked content inside an off layer is off"},
        {272, 85, 1, "and what follows its end shows again"},
        {20, 85, 0, "a form in an off layer is not drawn"},
        {56, 85, 1, "one in none is"},
        {215, 85, 0, "an annotation in an off layer is not drawn"},
    };
    pdfw_t b = oc_file();
    tiku_pdf_err_t err;
    tiku_pdf_t *doc = open_bytes(&b, &err);
    tiku_pdf_pixmap_t pm = {0, 0, NULL};
    char *text = doc != NULL ? tiku_pdf_page_text(doc, 0) : NULL;
    size_t i;

    check(doc != NULL && tiku_pdf_render(doc, 0, 1.0, 0, &pm) == 0,
          "a page of layers renders");
    for (i = 0; pm.px != NULL && i < sizeof probe / sizeof probe[0]; i++) {
        uint32_t want = probe[i].x == 272 && probe[i].y == 85 ? 0x00ff00u
                      : probe[i].x == 56 && probe[i].y == 85 ? 0x00ff00u
                      : 0xff0000u;

        check(pixel(&pm, probe[i].x, probe[i].y) ==
              (probe[i].on ? want : 0xffffffu), probe[i].what);
    }
    check(text != NULL && strstr(text, "Shown") != NULL &&
          strstr(text, "Hidden") == NULL,
          "text in an off layer is not read, the rest is");
    free(text);
    tiku_pdf_pixmap_free(&pm);
    tiku_pdf_close(doc);
    free(b.p);
}

/** @brief Annotations with no appearance stored, one of each kind a
 *         viewer draws anyway, over a line of text. */
static pdfw_t
annot_file(void)
{
    static const char *const annots[] = {
        "<< /Subtype /Square /Rect [20 200 80 260] /C [1 0 0] /IC [0 0 1] "
        "/BS << /W 3 >> >>",
        "<< /Subtype /Circle /Rect [100 200 160 260] /C [0 0.6 0] "
        "/BS << /W 2 >> >>",
        "<< /Subtype /Line /Rect [170 200 290 270] /L [180 210 280 260] "
        "/C [0 0 0] /IC [1 0 0] /LE [/OpenArrow /ClosedArrow] "
        "/BS << /W 2 >> >>",
        "<< /Subtype /Ink /Rect [290 200 380 260] /InkList [[300 210 320 "
        "250 340 210 360 250]] /C [0.5 0 0.5] /BS << /W 3 >> >>",
        "<< /Subtype /Highlight /Rect [18 145 150 165] /QuadPoints [18 165 "
        "150 165 18 145 150 145] /C [1 1 0] >>",
        "<< /Subtype /StrikeOut /Rect [18 105 95 125] /QuadPoints [18 105 "
        "95 105 95 125 18 125] /C [1 0 0] >>",
        "<< /Subtype /FreeText /Rect [20 20 180 80] /DA (0 0 1 rg /Helv 12 "
        "Tf) /Contents (A free text note that wraps across lines) >>",
        "<< /Subtype /Text /Rect [200 40 220 60] /C [1 1 0] >>",
        "<< /Subtype /Stamp /Rect [250 20 380 70] /Name /Approved >>",
    };
    const char *text = "BT /F1 14 Tf 20 150 Td (Highlighted words) Tj ET "
                       "BT /F1 14 Tf 20 110 Td (Struck out) Tj ET";
    char list[256];
    pdfw_t b = {0};
    size_t i;
    int at;

    ws(&b, "%PDF-1.7\n");
    obj(&b, 1, "<< /Type /Catalog /Pages 2 0 R >>");
    obj(&b, 2, "<< /Type /Pages /Kids [3 0 R] /Count 1 >>");
    at = snprintf(list, sizeof list, "<< /Type /Page /Parent 2 0 R "
                  "/MediaBox [0 0 400 300] /Contents 4 0 R /Resources << "
                  "/Font << /F1 5 0 R >> >> /Annots [");
    for (i = 0; i < sizeof annots / sizeof annots[0]; i++) {
        at += snprintf(list + at, sizeof list - (size_t)at, " %d 0 R",
                       (int)i + 10);
    }
    snprintf(list + at, sizeof list - (size_t)at, " ] >>");
    obj(&b, 3, list);
    stream(&b, 4, "", text, (long)strlen(text));
    obj(&b, 5, "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>");
    for (i = 0; i < sizeof annots / sizeof annots[0]; i++) {
        obj(&b, (int)i + 10, annots[i]);
    }
    xref(&b, 1, 0);
    return b;
}

/** @brief The annotations page drawn: each kind where and as it should
 *         be, and a round cap with no hole where it meets its line. */
static void
annot_test(void)
{
    pdfw_t b = annot_file();
    tiku_pdf_err_t err;
    tiku_pdf_t *doc = open_bytes(&b, &err);
    tiku_pdf_pixmap_t pm = {0, 0, NULL};
    int ok = doc != NULL && tiku_pdf_render(doc, 0, 1.0, 0, &pm) == 0;

    check(ok, "a page of annotations without appearances renders");
    if (!ok) {
        tiku_pdf_close(doc);
        free(b.p);
        return;
    }
    check(pixel(&pm, 50, 70) == 0x0000ffu && pixel(&pm, 20, 70) == 0xff0000u,
          "a square: IC within, C round it");
    check(near(pixel(&pm, 159, 70), 0, 153, 0, 40) &&
          pixel(&pm, 130, 70) == 0xffffffu,
          "a circle: its stroke, and no fill without IC");
    check(near(pixel(&pm, 274, 43), 255, 0, 0, 60),
          "a line's closed arrow is filled with IC");
    check(near(pixel(&pm, 310, 70), 128, 0, 128, 40),
          "an ink stroke is drawn in C");
    check(near(pixel(&pm, 360, 51), 128, 0, 128, 40),
          "and its round cap has no hole where it meets the line");
    check(pixel(&pm, 148, 153) == 0xffff00u,
          "a highlight is its colour where the paper shows");
    check(near(pixel(&pm, 60, 184), 255, 0, 0, 80) &&
          pixel(&pm, 60, 181) == 0xffffffu,
          "a strike-out crosses the middle of its words, and only that");
    check(pixel(&pm, 20, 250) == 0x0000ffu,
          "free text has a point's border in its text colour");
    check(near(pixel(&pm, 251, 255), 204, 0, 0, 20),
          "a stamp is framed in red");
    check(pixel(&pm, 210, 248) == 0xffff00u, "a note shows its icon");
    tiku_pdf_pixmap_free(&pm);
    tiku_pdf_close(doc);
    free(b.p);
}

/** @brief A form that needs its appearances made: a text field, a check
 *         box and a radio button on, and a field whose stored look is
 *         stale (red) against its value. */
static pdfw_t
form_file(void)
{
    const char *stale = "1 0 0 rg 0 0 160 22 re f";
    pdfw_t b = {0};

    ws(&b, "%PDF-1.7\n");
    obj(&b, 1, "<< /Type /Catalog /Pages 2 0 R /AcroForm << /Fields "
               "[10 0 R 11 0 R 12 0 R 13 0 R] /NeedAppearances true "
               "/DA (/Helv 0 Tf 0 g) >> >>");
    obj(&b, 2, "<< /Type /Pages /Kids [3 0 R] /Count 1 >>");
    obj(&b, 3, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 400 280] "
               "/Annots [10 0 R 11 0 R 12 0 R 13 0 R] >>");
    obj(&b, 10, "<< /Subtype /Widget /FT /Tx /T (name) /Rect [20 240 200 "
                "262] /V (John Smith) /DA (/Helv 12 Tf 0 0 1 rg) /MK << "
                "/BG [0.9 0.9 1] /BC [0 0 0] >> >>");
    obj(&b, 11, "<< /Subtype /Widget /FT /Btn /T (agree) /Rect [220 240 240 "
                "260] /V /Yes /AS /Yes /DA (/ZaDb 0 Tf 0 g) >>");
    obj(&b, 12, "<< /Subtype /Widget /FT /Btn /Ff 32768 /T (pick) /Rect "
                "[260 240 280 260] /V /1 /AS /1 /DA (0 g) >>");
    obj(&b, 13, "<< /Subtype /Widget /FT /Tx /T (stale) /Rect [220 110 380 "
                "132] /V (NEW VALUE) /DA (/Helv 12 Tf 0 g) /AP << /N 14 0 R "
                ">> >>");
    stream(&b, 14, "/Type /XObject /Subtype /Form /BBox [0 0 160 22]",
           stale, (long)strlen(stale));
    xref(&b, 1, 0);
    return b;
}

/** @brief How many pixels in the box (@p x0, @p y0)-(@p x1, @p y1) are
 *         darker than @p most on every channel. */
static int
dark(const tiku_pdf_pixmap_t *pm, int x0, int y0, int x1, int y1, int most)
{
    int x, y, n = 0;

    for (y = y0; y < y1; y++) {
        for (x = x0; x < x1; x++) {
            uint32_t c = pixel(pm, x, y);

            n += (int)(c >> 16 & 255) < most && (int)(c >> 8 & 255) < most &&
                 (int)(c & 255) < most;
        }
    }
    return n;
}

/** @brief The form drawn from its values. */
static void
form_test(void)
{
    pdfw_t b = form_file();
    tiku_pdf_err_t err;
    tiku_pdf_t *doc = open_bytes(&b, &err);
    tiku_pdf_pixmap_t pm = {0, 0, NULL};
    int ok = doc != NULL && tiku_pdf_render(doc, 0, 1.0, 0, &pm) == 0;
    int x, blue = 0;

    check(ok, "a form needing its appearances renders");
    if (!ok) {
        tiku_pdf_close(doc);
        free(b.p);
        return;
    }
    for (x = 22; x < 110; x++) {
        int y;

        for (y = 20; y < 38; y++) {
            uint32_t c = pixel(&pm, x, y);

            blue += (c & 255) > 200 && (c >> 16 & 255) < 120;
        }
    }
    check(near(pixel(&pm, 195, 36), 230, 230, 255, 2) && blue > 20,
          "a text field: MK's ground, and its value in DA's colour");
    check(dark(&pm, 220, 20, 240, 40, 100) > 8,
          "a check box that is on shows its tick");
    check(dark(&pm, 267, 27, 273, 33, 60) == 36,
          "a radio button that is on shows its dot");
    check(!near(pixel(&pm, 370, 158), 255, 0, 0, 60) &&
          dark(&pm, 222, 150, 380, 170, 100) > 20,
          "a stale look is drawn afresh from the value, as the form asks");
    tiku_pdf_pixmap_free(&pm);
    tiku_pdf_close(doc);
    free(b.p);
}

/** @brief Bits packed as a mesh shading's stream packs them. */
typedef struct {
    char b[256];                    /* the bytes so far                 */
    int  n, bit;                    /* whole bytes, bits in the next    */
} bitw_t;

/** @brief @p v in @p k bits, the highest first. */
static void
bput(bitw_t *w, unsigned v, int k)
{
    while (k-- > 0) {
        if (w->bit == 0) {
            w->b[w->n] = 0;
        }
        w->b[w->n] |= (char)((v >> k & 1) << (7 - w->bit));
        if (++w->bit == 8) {
            w->bit = 0;
            w->n++;
        }
    }
}

/** @brief On to the next byte, as each vertex or patch starts one. */
static void
balign(bitw_t *w)
{
    if (w->bit != 0) {
        w->bit = 0;
        w->n++;
    }
}

/** @brief A vertex or patch point: 16-bit coordinates over 0..100, and
 *         @p nc 8-bit colour values over 0..1 (none when @p nc is 0). */
static void
bvertex(bitw_t *w, double x, double y, const double *c, int nc)
{
    int i;

    bput(w, (unsigned)(x / 100 * 65535 + 0.5), 16);
    bput(w, (unsigned)(y / 100 * 65535 + 0.5), 16);
    for (i = 0; i < nc; i++) {
        bput(w, (unsigned)(c[i] * 255 + 0.5), 8);
    }
}

/** @brief Three meshes shown by sh: two triangles going on from one
 *         another (type 4) above, a Coons patch (6) and a tensor-product
 *         patch (7) below, each colour at a corner. */
static pdfw_t
mesh_file(void)
{
    static const double red[3] = {1, 0, 0}, green[3] = {0, 1, 0};
    static const double blue[3] = {0, 0, 1}, yellow[3] = {1, 1, 0};
    static const double white[3] = {1, 1, 1};
    static const double coons[12][2] = {
        {5, 5}, {0, 20}, {10, 35}, {5, 50}, {20, 55}, {35, 45}, {50, 50},
        {55, 35}, {45, 20}, {50, 5}, {35, 0}, {20, 10}
    };
    static const double tensor[16][2] = {
        {5, 5}, {5, 35}, {5, 65}, {5, 95}, {35, 95}, {65, 95}, {95, 95},
        {95, 65}, {95, 35}, {95, 5}, {65, 5}, {35, 5}, {20, 20}, {35, 65},
        {80, 80}, {65, 35}
    };
    const double *corner[4] = {red, green, blue, yellow};
    const char *draw = "q 1 0 0 1 5 115 cm /S4 sh Q q 1 0 0 1 5 5 cm /S6 sh "
                       "Q q 1 0 0 1 115 5 cm /S7 sh Q";
    const char *dict = "/ColorSpace /DeviceRGB /BitsPerCoordinate 16 "
                       "/BitsPerComponent 8 /BitsPerFlag 8 /Decode [0 100 0 "
                       "100 0 1 0 1 0 1]";
    char head[256];
    bitw_t t4 = {{0}, 0, 0}, t6 = {{0}, 0, 0}, t7 = {{0}, 0, 0};
    pdfw_t b = {0};
    int i;

    bput(&t4, 0, 8); bvertex(&t4, 5, 5, red, 3); balign(&t4);
    bput(&t4, 0, 8); bvertex(&t4, 95, 5, green, 3); balign(&t4);
    bput(&t4, 0, 8); bvertex(&t4, 50, 95, blue, 3); balign(&t4);
    bput(&t4, 1, 8); bvertex(&t4, 95, 95, yellow, 3); balign(&t4);
    bput(&t6, 0, 8);
    bput(&t7, 0, 8);
    for (i = 0; i < 12; i++) {
        bvertex(&t6, coons[i][0], coons[i][1], NULL, 0);
    }
    for (i = 0; i < 16; i++) {
        bvertex(&t7, tensor[i][0], tensor[i][1], NULL, 0);
    }
    for (i = 0; i < 4 * 3; i++) {   /* the corners' colours: c00 to c30 */
        bput(&t6, (unsigned)(corner[i / 3][i % 3] * 255), 8);
    }
    corner[3] = white;
    for (i = 0; i < 4 * 3; i++) {
        bput(&t7, (unsigned)(corner[i / 3][i % 3] * 255), 8);
    }
    balign(&t6);
    balign(&t7);
    ws(&b, "%PDF-1.7\n");
    obj(&b, 1, "<< /Type /Catalog /Pages 2 0 R >>");
    obj(&b, 2, "<< /Type /Pages /Kids [3 0 R] /Count 1 >>");
    obj(&b, 3, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 220 220] "
               "/Contents 4 0 R /Resources << /Shading << /S4 10 0 R "
               "/S6 11 0 R /S7 12 0 R >> >> >>");
    stream(&b, 4, "", draw, (long)strlen(draw));
    snprintf(head, sizeof head, "/ShadingType 4 %s", dict);
    stream(&b, 10, head, t4.b, t4.n);
    snprintf(head, sizeof head, "/ShadingType 6 %s", dict);
    stream(&b, 11, head, t6.b, t6.n);
    snprintf(head, sizeof head, "/ShadingType 7 %s", dict);
    stream(&b, 12, head, t7.b, t7.n);
    xref(&b, 1, 0);
    return b;
}

/** @brief The meshes drawn: each corner's colour at its corner, blended
 *         between, and nothing where no triangle is. */
static void
mesh_test(void)
{
    pdfw_t b = mesh_file();
    tiku_pdf_err_t err;
    tiku_pdf_t *doc = open_bytes(&b, &err);
    tiku_pdf_pixmap_t pm = {0, 0, NULL};
    int ok = doc != NULL && tiku_pdf_render(doc, 0, 1.0, 0, &pm) == 0;

    check(ok, "a page of mesh shadings renders");
    if (!ok) {
        tiku_pdf_close(doc);
        free(b.p);
        return;
    }
    check(near(pixel(&pm, 13, 97), 255, 0, 0, 40) &&
          near(pixel(&pm, 95, 97), 0, 255, 0, 50) &&
          near(pixel(&pm, 97, 15), 255, 255, 0, 50),
          "free triangles: red, green and, going on from them, yellow");
    check(pixel(&pm, 15, 20) == 0xffffffu,
          "and nothing where no triangle reaches");
    check(near(pixel(&pm, 14, 206), 255, 0, 0, 50) &&
          near(pixel(&pm, 51, 207), 255, 255, 0, 60) &&
          near(pixel(&pm, 35, 185), 128, 128, 64, 40),
          "a Coons patch: its corners' colours, and between them a mix");
    check(near(pixel(&pm, 122, 208), 255, 0, 0, 40) &&
          near(pixel(&pm, 208, 122), 0, 0, 255, 50) &&
          near(pixel(&pm, 208, 208), 255, 255, 255, 30),
          "a tensor-product patch: its corners' colours");
    tiku_pdf_pixmap_free(&pm);
    tiku_pdf_close(doc);
    free(b.p);
}

/**
 * @brief Five pages a careless writer makes: a MediaBox of nothing; a
 *        form whose stray Q would undo its caller's clip; a Separation
 *        whose alternate is no space; an Indexed table over five
 *        colorants; those five colorants painted with themselves.
 */
static pdfw_t
damage_file(void)
{
    pdfw_t b = {0};
    const char *clip = "q 0 0 100 100 re W n /Fm Do 1 0 0 rg 0 0 300 300 re "
                       "f Q BT /F1 12 Tf 10 200 Td (After) Tj ET";
    const char *sep = "/CS0 cs 0.5 sc 0 0 300 300 re f";
    const char *idx = "/CS1 cs 1 sc 0 0 300 300 re f";
    const char *inks = "/CS2 cs 0 0 0 0 0.8 sc 0 0 300 300 re f";
    const char *fn = "{ exch pop exch pop exch pop exch pop }";

    ws(&b, "%PDF-1.4\n");
    obj(&b, 1, "<< /Type /Catalog /Pages 2 0 R >>");
    obj(&b, 2, "<< /Type /Pages /Kids [3 0 R 4 0 R 5 0 R 6 0 R 13 0 R] "
               "/Count 5 "
               "/MediaBox [0 0 300 300] /Resources << /Font << /F1 << "
               "/Type /Font /Subtype /Type1 /BaseFont /Helvetica >> >> "
               "/XObject << /Fm 10 0 R >> /ColorSpace << "
               "/CS0 [/Separation /Spot /NoSuchSpace 11 0 R] "
               "/CS1 [/Indexed [/DeviceN [/A /B /C /D /E] /DeviceGray "
               "12 0 R] 1 <000000000000000000CC>] "
               "/CS2 [/DeviceN [/A /B /C /D /E] /DeviceGray 12 0 R] >> >> >>");
    obj(&b, 3, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 0 0] >>");
    obj(&b, 4, "<< /Type /Page /Parent 2 0 R /Contents 7 0 R >>");
    obj(&b, 5, "<< /Type /Page /Parent 2 0 R /Contents 8 0 R >>");
    obj(&b, 6, "<< /Type /Page /Parent 2 0 R /Contents 9 0 R >>");
    stream(&b, 7, "", clip, (long)strlen(clip));
    stream(&b, 8, "", sep, (long)strlen(sep));
    stream(&b, 9, "", idx, (long)strlen(idx));
    stream(&b, 10, "/Type /XObject /Subtype /Form /BBox [0 0 300 300]",
           "Q Q", 3);
    obj(&b, 11, "<< /FunctionType 2 /Domain [0 1] /C0 [1] /C1 [0] /N 1 >>");
    stream(&b, 12, "/FunctionType 4 /Domain [0 1 0 1 0 1 0 1 0 1] "
                   "/Range [0 1]", fn, (long)strlen(fn));
    obj(&b, 13, "<< /Type /Page /Parent 2 0 R /Contents 14 0 R >>");
    stream(&b, 14, "", inks, (long)strlen(inks));
    xref(&b, 1, 0);
    return b;
}

/** @brief The careless writer's pages, read and drawn as it meant them. */
static void
damage_test(void)
{
    pdfw_t b = damage_file();
    tiku_pdf_err_t err;
    tiku_pdf_t *doc = open_bytes(&b, &err);
    tiku_pdf_pixmap_t pm = {0, 0, NULL};
    char *text = doc != NULL ? tiku_pdf_page_text(doc, 1) : NULL;
    double pw = 0, ph = 0;
    int rot = 0;

    check(doc != NULL && tiku_pdf_page_size(doc, 0, &pw, &ph, &rot) == 0 &&
          pw == 612 && ph == 792,
          "a MediaBox of nothing leaves the page Letter, not nothing");
    check(doc != NULL && tiku_pdf_render(doc, 1, 1.0, 0, &pm) == 0 &&
          pixel(&pm, 50, 250) == 0xff0000u &&
          pixel(&pm, 150, 150) == 0xffffffu,
          "a form's stray Q does not undo its caller's clip");
    tiku_pdf_pixmap_free(&pm);
    check(text != NULL && strstr(text, "After") != NULL,
          "nor lose the text that follows it");
    free(text);
    check(doc != NULL && tiku_pdf_render(doc, 2, 1.0, 0, &pm) == 0 &&
          pixel(&pm, 150, 150) == 0x808080u,
          "a Separation whose alternate is no space paints its tint grey");
    tiku_pdf_pixmap_free(&pm);
    check(doc != NULL && tiku_pdf_render(doc, 3, 1.0, 0, &pm) == 0 &&
          pixel(&pm, 150, 150) == 0xccccccu,
          "an Indexed table over five colorants reads all five");
    tiku_pdf_pixmap_free(&pm);
    check(doc != NULL && tiku_pdf_render(doc, 4, 1.0, 0, &pm) == 0 &&
          pixel(&pm, 150, 150) == 0xccccccu,
          "and those five colorants, set and painted, are all read");
    tiku_pdf_pixmap_free(&pm);
    tiku_pdf_close(doc);
    free(b.p);
}

/**
 * @brief Three pages, an outline over them and links between them: an
 *        entry and a link by name (the Names tree and the older Dests
 *        dictionary), one straight to a page, one to an address.
 */
static pdfw_t
nav_file(void)
{
    pdfw_t b = {0};
    const char *text = "BT /F1 20 Tf 100 700 Td (Hi) Tj ET "
                       "BT /F1 20 Tf 100 600 Td (Yo) Tj ET";

    ws(&b, "%PDF-1.4\n");
    obj(&b, 1, "<< /Type /Catalog /Pages 2 0 R /Outlines 10 0 R "
               "/Dests << /old [4 0 R /FitH 500] >> "
               "/Names << /Dests 20 0 R >> >>");
    obj(&b, 2, "<< /Type /Pages /Kids [3 0 R 4 0 R 5 0 R] /Count 3 "
               "/MediaBox [0 0 612 792] /Resources << /Font << /F1 7 0 R "
               ">> >> >>");
    obj(&b, 3, "<< /Type /Page /Parent 2 0 R /Contents 6 0 R "
               "/Annots [8 0 R 9 0 R 13 0 R] >>");
    obj(&b, 4, "<< /Type /Page /Parent 2 0 R >>");
    obj(&b, 5, "<< /Type /Page /Parent 2 0 R /Rotate 90 >>");
    stream(&b, 6, "", text, (long)strlen(text));
    /* Widths from H (72): H 500, Y 600, i 250, o 550, the rest 0. */
    obj(&b, 7, "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica "
               "/FirstChar 72 /LastChar 111 /Widths [500 0 0 0 0 0 0 0 0 0 0 0 "
               "0 0 0 0 0 600 0 0 0 0 0 0 0 0 0 0 "
               "0 0 0 0 0 250 0 0 0 0 0 550] >>");
    obj(&b, 8, "<< /Type /Annot /Subtype /Link /Rect [100 700 200 720] "
               "/Dest (sec2) >>");
    obj(&b, 9, "<< /Type /Annot /Subtype /Link /Rect [300 50 400 70] "
               "/A << /S /URI /URI (https://tiku-os.org/) >> >>");
    obj(&b, 13, "<< /Type /Annot /Subtype /Link /Rect [0 0 10 10] "
                "/A << /S /Named /N /NextPage >> >>");
    obj(&b, 10, "<< /Type /Outlines /First 11 0 R /Last 12 0 R >>");
    obj(&b, 11, "<< /Title (One) /Parent 10 0 R /Next 12 0 R /First 14 0 R "
                "/Last 14 0 R /Count 1 /Dest [3 0 R /Fit] >>");
    obj(&b, 14, "<< /Title <FEFF0054007700F8> /Parent 11 0 R "
                "/Dest [4 0 R /XYZ 0 700 0] >>");
    obj(&b, 12, "<< /Title (Three\x8d) /Parent 10 0 R /Prev 11 0 R "
                "/A << /S /GoTo /D /old >> >>");
    obj(&b, 20, "<< /Kids [21 0 R 22 0 R] >>");
    obj(&b, 21, "<< /Limits [(a) (m)] /Names [(abc) [3 0 R /Fit]] >>");
    obj(&b, 22, "<< /Limits [(n) (z)] /Names [(sec2) [5 0 R /XYZ 72 792 0]"
                " (zz) [3 0 R /Fit]] >>");
    xref(&b, 1, 0);
    return b;
}

static void
nav_test(void)
{
    pdfw_t b = nav_file();
    tiku_pdf_err_t err;
    tiku_pdf_t *doc = open_bytes(&b, &err);
    tiku_pdf_outline_t *o = NULL;
    tiku_pdf_link_t *l = NULL;
    tiku_pdf_char_t *c = NULL;
    int n = 0, k = 0;
    long nc = 0;

    check(doc != NULL && tiku_pdf_pages(doc) == 3, "three pages, linked");
    if (doc == NULL) {
        free(b.p);
        return;
    }
    n = tiku_pdf_outline(doc, &o);
    check(n == 3 && o[0].level == 0 && o[0].below == 1 && o[0].open &&
          o[1].level == 1 && o[2].level == 0 && o[2].below == 0,
          "the outline, depth first, with what lies under each entry");
    check(n == 3 && !strcmp(o[0].title, "One") &&
          !strcmp(o[1].title, "Tw\xc3\xb8") &&
          !strcmp(o[2].title, "Three\xe2\x80\x9c"),
          "titles in UTF-16 and PDFDocEncoding come out as UTF-8");
    /* XYZ 0 700 on a 792-point page: 92 points down from the top. */
    check(n == 3 && o[0].to.page == 0 && o[0].to.top == 0 &&
          o[1].to.page == 1 && o[1].to.top > 91.9 && o[1].to.top < 92.1,
          "an entry leads to its page and the point on it");
    check(n == 3 && o[2].to.page == 1 && o[2].to.top > 291.9 &&
          o[2].to.top < 292.1, "a GoTo by a name the Dests dictionary holds");
    tiku_pdf_outline_free(o, n);

    k = tiku_pdf_links(doc, 0, &l);
    check(k == 3, "three links on the first page");
    /* Rect 100 700 200 720 is 72..92 down the page as drawn. */
    check(k == 3 && l[0].x0 == 100 && l[0].x1 == 200 && l[0].y0 == 72 &&
          l[0].y1 == 92, "a link's area, on the page as drawn");
    /* sec2 by the Names tree: the third page, turned; x 72 is 72 down. */
    check(k == 3 && l[0].to.page == 2 && l[0].to.top > 71.9 &&
          l[0].to.top < 72.1, "a name found through the tree's Limits");
    check(k == 3 && l[1].to.page == -1 && l[1].to.uri != NULL &&
          !strcmp(l[1].to.uri, "https://tiku-os.org/"), "an address");
    check(k == 3 && l[2].to.page == 1, "a Named step to the next page");
    tiku_pdf_links_free(l, k);
    check(tiku_pdf_links(doc, 1, &l) == 0 && l == NULL,
          "a page without links has none");

    /* "Hi" at 100, 700 in 20 points: H is 500 wide, i 250. */
    nc = tiku_pdf_page_chars(doc, 0, &c);
    check(nc == 5 && c[0].cp == 'H' && c[1].cp == 'i' && c[2].cp == '\n' &&
          c[3].cp == 'Y' && c[4].cp == 'o', "the characters, in order");
    check(nc == 5 && c[0].x0 == 100 && c[0].x1 == 110 && c[1].x0 == 110 &&
          c[1].x1 == 115 && c[0].y0 == 76 && c[0].y1 == 96,
          "each where it stands: the em from below the baseline");
    check(nc == 5 && c[2].x0 == c[2].x1, "a made-up break has no width");
    free(c);
    tiku_pdf_close(doc);
    free(b.p);
}

/** @brief A page with a red square, a blue stroke, a clipped green fill
 *        and a form, on a Rotate 90 page. */
static pdfw_t
drawn_file(void)
{
    pdfw_t b = {0};
    const char *content =
        "1 0 0 rg 0 0 50 50 re f "
        "0 0 1 RG 4 w 100 100 m 200 100 l S "
        "q 150 150 40 40 re W n 0 1 0 rg 100 100 200 200 re f Q "
        "0.5 g q 1 0 0 1 250 350 cm /Fx Do Q "
        "q 40 0 0 20 100 300 cm /Im Do Q "
        "1 0 1 rg q 20 0 0 20 200 300 cm /St Do Q "
        "q 20 0 0 20 100 200 cm BI /W 1 /H 1 /CS /G /BPC 8 ID \x40 EI Q "
        "BT /T3 20 Tf 200 200 Td 0 0.5 0 rg (a) Tj 3 Tr (a) Tj ET "
        "0 0 0 rg 300 400 m 300 400 l f";
    const char *fx = "0 0 1 rg 0 0 20 20 re f";
    /* Two by one: a yellow sample then a cyan one. */
    const char im[] = "\xff\xff\x00\x00\xff\xff";
    /* Four by two, one bit: the left half painted (0), the right not. */
    const char st[] = "\x30\x30";
    /* A Type 3 glyph filling its whole em. */
    const char glyph[] = "1000 0 d0 0 0 1000 1000 re f";

    ws(&b, "%PDF-1.4\n");
    obj(&b, 1, "<< /Type /Catalog /Pages 2 0 R >>");
    obj(&b, 2, "<< /Type /Pages /Kids [3 0 R] /Count 1 >>");
    obj(&b, 3, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 300 400] "
               "/Rotate 90 /Resources << /XObject << /Fx 5 0 R /Im 6 0 R "
               "/St 7 0 R >> /Font << /T3 8 0 R >> >> /Contents 4 0 R >>");
    stream(&b, 4, "", content, (long)strlen(content));
    stream(&b, 5, "/Type /XObject /Subtype /Form /BBox [0 0 20 20]", fx,
           (long)strlen(fx));
    stream(&b, 6, "/Type /XObject /Subtype /Image /Width 2 /Height 1 "
                  "/ColorSpace /DeviceRGB /BitsPerComponent 8", im, 6);
    stream(&b, 7, "/Type /XObject /Subtype /Image /Width 4 /Height 2 "
                  "/ImageMask true", st, 2);
    obj(&b, 8, "<< /Type /Font /Subtype /Type3 /FontBBox [0 0 1000 1000] "
               "/FontMatrix [0.001 0 0 0.001 0 0] /CharProcs << /box 9 0 R >> "
               "/Encoding << /Type /Encoding /Differences [97 /box] >> "
               "/FirstChar 97 /LastChar 97 /Widths [1000] >>");
    stream(&b, 9, "", glyph, (long)strlen(glyph));
    xref(&b, 1, 0);
    return b;
}

/**
 * @brief The view turned a quarter, a half and three quarters (and a
 *        quarter back): each is the unturned page, every pixel where the
 *        turn takes it; tiku_pdf_turn_point() takes it back; a turn
 *        other than a quarter's is refused.
 */
static void
turn_test(tiku_pdf_t *doc, const tiku_pdf_pixmap_t *pm)
{
    static const int turns[] = { 90, 180, 270, -90 };
    tiku_pdf_pixmap_t tp = {0, 0, NULL};
    size_t i;
    int w = 0, h = 0;

    for (i = 0; i < sizeof turns / sizeof turns[0]; i++) {
        int t = turns[i], side = t % 180 != 0, x, y;
        long same = 0, back = 0;

        if (tiku_pdf_render(doc, 0, 1.0, t, &tp) != 0 ||
            tp.w != (side ? pm->h : pm->w) || tp.h != (side ? pm->w : pm->h)) {
            check(0, "a turned page renders at its turned size");
            tiku_pdf_pixmap_free(&tp);
            continue;
        }
        for (y = 0; y < pm->h; y++) {
            for (x = 0; x < pm->w; x++) {
                double tx = x + 0.5, ty = y + 0.5, bx, by;

                tiku_pdf_turn_point(pm->w, pm->h, t, 0, &tx, &ty);
                bx = tx;
                by = ty;
                tiku_pdf_turn_point(pm->w, pm->h, t, 1, &bx, &by);
                back += bx == x + 0.5 && by == y + 0.5;
                same += pixel(pm, x, y) == pixel(&tp, (int)tx, (int)ty);
            }
        }
        check(same == (long)pm->w * pm->h && back == same,
              "a turned page is the page's pixels, where the turn puts "
              "them, and turned back they are where they were");
        tiku_pdf_pixmap_free(&tp);
    }
    check(tiku_pdf_render_size(doc, 0, 1.0, 45, &w, &h) != 0 &&
          tiku_pdf_render(doc, 0, 1.0, 45, &tp) != 0 && tp.px == NULL,
          "a turn of 45 degrees is refused");
}

static void
render_test(void)
{
    pdfw_t b = drawn_file();
    tiku_pdf_err_t err;
    tiku_pdf_t *doc = open_bytes(&b, &err);
    tiku_pdf_pixmap_t pm = {0, 0, NULL}, band = {0, 0, NULL};
    int w = 0, h = 0;

    check(doc != NULL && tiku_pdf_render_size(doc, 0, 1.0, 0, &w, &h) == 0 &&
          w == 400 && h == 300, "a 300 by 400 page turned 90 is 400 by 300");
    check(doc != NULL && tiku_pdf_render(doc, 0, 1.0, 0, &pm) == 0 &&
          pm.w == 400 && pm.h == 300, "and renders at that size");
    if (pm.px == NULL) {
        tiku_pdf_close(doc);
        free(b.p);
        return;
    }
    /* Turned clockwise: the page's bottom left is now the top left. */
    check(pixel(&pm, 10, 10) == 0xff0000u, "the red square, at the top left");
    check(pixel(&pm, 390, 290) == 0xffffffu, "paper where nothing was drawn");
    /* Turned clockwise, user (x, y) lands at device (y, x).  The stroke
     * ran along y=100 from x=100 to 200, 4 wide: now down x=100. */
    check(pixel(&pm, 100, 150) == 0x0000ffu && pixel(&pm, 96, 150) != 0x0000ffu,
          "a 4-point stroke, blue, where the turn put it");
    /* Green only inside the 40-point clip square (150..190). */
    check(pixel(&pm, 170, 170) == 0x00ff00u &&
          pixel(&pm, 120, 120) == 0xffffffu,
          "a fill stops at its clip");
    /* The form: blue 20-point square at user 250..270, 350..370. */
    check(pixel(&pm, 360, 260) == 0x0000ffu,
          "a form draws where its matrix puts it");
    /* The image: user 100..140 by 300..320 is device 300..320 by
     * 100..140; its first sample (yellow) is the left half in user
     * space, so the top in device space. */
    check(pixel(&pm, 310, 105) == 0xffff00u &&
          pixel(&pm, 310, 135) == 0x00ffffu,
          "an image's samples land where the matrix turns them");
    /* The stencil: left half of user 200..220 painted magenta. */
    check(pixel(&pm, 305, 205) == 0xff00ffu &&
          pixel(&pm, 305, 215) == 0xffffffu,
          "a stencil paints the fill colour through its bits");
    check(pixel(&pm, 210, 110) == 0x404040u, "an inline image draws too");
    /* Type 3 text: the em square at user 200..220 by 200..220, filled
     * dark green; the second glyph, invisible, would be at 220..240. */
    check(pixel(&pm, 210, 210) == 0x008000u &&
          pixel(&pm, 210, 230) == 0xffffffu,
          "a Type 3 glyph runs its procedure; render mode 3 draws none");
    check(tiku_pdf_render_rect(doc, 0, 1.0, 0, 0, 150, 400, 20, &band) == 0 &&
          band.h == 20 && pixel(&band, 100, 0) == 0x0000ffu &&
          pixel(&band, 170, 19) == 0x00ff00u,
          "a band of rows shows the same ink");
    tiku_pdf_pixmap_free(&band);
    turn_test(doc, &pm);
    tiku_pdf_pixmap_free(&pm);
    tiku_pdf_close(doc);
    free(b.p);
}

/** @brief Every PDF in the folder TIKU_PDF_CORPUS names, opened and read. */
static void
corpus_test(void)
{
    const char *dir = getenv("TIKU_PDF_CORPUS");
    DIR *d;
    struct dirent *e;
    int opened = 0, tried = 0;

    if (dir == NULL || (d = opendir(dir)) == NULL) {
        puts("SKIP: TIKU_PDF_CORPUS names no folder of PDFs");
        return;
    }
    while ((e = readdir(d)) != NULL) {
        char path[1024], *text, *title;
        tiku_pdf_t *doc = NULL;
        tiku_pdf_err_t err;
        size_t len = strlen(e->d_name);
        int pages;

        if (len < 4 || strcmp(e->d_name + len - 4, ".pdf") != 0) {
            continue;
        }
        snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
        tried++;
        err = tiku_pdf_open(path, &doc);
        if (err != TIKU_PDF_OK) {
            printf("  %s: %s\n", e->d_name, err == TIKU_PDF_UNSUPPORTED
                   ? "unsupported (encrypted)" : "did not open");
            continue;
        }
        opened++;
        pages = tiku_pdf_pages(doc);
        title = tiku_pdf_title(doc);
        text = tiku_pdf_page_text(doc, pages > 1 ? 1 : 0);
        printf("  %s: %d pages%s, title %s\n    %.160s\n", e->d_name, pages,
               tiku_pdf_repaired(doc) ? " (repaired)" : "",
               title ? title : "(none)", text ? text : "(no text)");
        check(pages > 0 && text != NULL, e->d_name);
        free(text);
        free(title);
        if (getenv("TIKU_PDF_PNG_DIR") != NULL) {
            /* The first pages drawn, for eyes: not a check, a look. */
            tiku_pdf_pixmap_t pm;
            char png[1200];
            int k;

            for (k = 0; k < pages && k < 3; k++) {
                if (tiku_pdf_render(doc, k, 1.5, 0, &pm) == 0) {
                    snprintf(png, sizeof png, "%s/%.*s-%d.png",
                             getenv("TIKU_PDF_PNG_DIR"), (int)len - 4,
                             e->d_name, k);
                    (void)png_out(&pm, png);
                    tiku_pdf_pixmap_free(&pm);
                }
            }
        }
        tiku_pdf_close(doc);
    }
    closedir(d);
    printf("  corpus: %d of %d opened\n", opened, tried);
}

/** @brief PDFDocEncoding spacing marks survive text-string decoding. */
static void
spacing_marks_test(void)
{
    unsigned char bytes[] = {0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c,
                              0x1d, 0x1e, 0x1f, 0x20, 0x80, 0xa0};
    static const char expected[] = " \xcb\x98\xcb\x87\xcb\x86\xcb\x99"
        "\xcb\x9d\xcb\x9b\xcb\x9a\xcb\x9c \xe2\x80\xa2\xe2\x82\xac";
    pdf_obj_t value = {.kind = PDF_STRING};
    unsigned char encoded[sizeof bytes];
    char *text;

    value.u.s.p = bytes;
    value.u.s.n = sizeof bytes;
    text = pdf_text_string(&value);
    check(text && !strcmp(text, expected),
          "D-093: spacing marks and adjacent PDFDocEncoding bytes decode");
    check(text && pdf_docenc(text, encoded, sizeof encoded) == sizeof bytes &&
          !memcmp(encoded + 1, bytes + 1, sizeof bytes - 1),
          "D-093: spacing marks round-trip through PDFDocEncoding");
    free(text);
}

int
main(void)
{
    printf("PDFs built byte by byte\n");
    simple_test();
    repair_test();
    packed_test();
    not_pdf_test();
    filter_test();
    glyph_test();
    render_test();
    shade_test();
    cs_keep_test();
    transp_test();
    knockout_test();
    nav_test();
    spacing_marks_test();
    fax_test();
    oc_test();
    annot_test();
    form_test();
    mesh_test();
    damage_test();
    corpus_test();
    if (fails) {
        printf("%d FAILED\n", fails);
        return 1;
    }
    puts("PASS: objects, tables rebuilt, object streams, filters, text");
    return 0;
}
