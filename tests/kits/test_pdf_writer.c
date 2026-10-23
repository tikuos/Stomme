/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_pdf_writer.c - the PDF writer, held to what the PDF kit reads back
 * from what it wrote: pages and their sizes, the title, text found where
 * it was drawn in faces cut to their glyphs, links and the outline, and
 * the pixels of fills, strokes, gradients, alpha and pictures -- a JPEG
 * passed through, pixels with a soft mask -- and made-up bold and slant.
 * Faces come from the machine; without any, the text rows are skipped.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "tiku_fontset.h"
#include "tiku_pdf.h"
#include "tiku_pdf_writer.h"
#include "tiku_ttf.h"

static int fails;
static tiku_fontset_face_t face;    /* a sans face this machine has     */
static tiku_ttf_t *ttf;             /* it, for measuring                */

/** @brief A 16 by 16 JPEG: red, green, blue and white quarters. */
static const char quad_jpg[] =
    "/9j/4AAQSkZJRgABAQAASABIAAD/wAARCAAQABADASIAAhEBAxEB/8QAHwAAAQUBAQEB"
    "AQEAAAAAAAAAAAECAwQFBgcICQoL/8QAtRAAAgEDAwIEAwUFBAQAAAF9AQIDAAQRBRIh"
    "MUEGE1FhByJxFDKBkaEII0KxwRVS0fAkM2JyggkKFhcYGRolJicoKSo0NTY3ODk6Q0RF"
    "RkdISUpTVFVWV1hZWmNkZWZnaGlqc3R1dnd4eXqDhIWGh4iJipKTlJWWl5iZmqKjpKWm"
    "p6ipqrKztLW2t7i5usLDxMXGx8jJytLT1NXW19jZ2uHi4+Tl5ufo6erx8vP09fb3+Pn6"
    "/8QAHwEAAwEBAQEBAQEBAQAAAAAAAAECAwQFBgcICQoL/8QAtREAAgECBAQDBAcFBAQA"
    "AQJ3AAECAxEEBSExBhJBUQdhcRMiMoEIFEKRobHBCSMzUvAVYnLRChYkNOEl8RcYGRom"
    "JygpKjU2Nzg5OkNERUZHSElKU1RVVldYWVpjZGVmZ2hpanN0dXZ3eHl6goOEhYaHiImK"
    "kpOUlZaXmJmaoqOkpaanqKmqsrO0tba3uLm6wsPExcbHyMnK0tPU1dbX2Nna4uPk5ebn"
    "6Onq8vP09fb3+Pn6/9sAQwABAQEBAQECAQECAwICAgMEAwMDAwQFBAQEBAQFBgUFBQUF"
    "BQYGBgYGBgYGBwcHBwcHCAgICAgJCQkJCQkJCQkJ/9sAQwEBAQECAgIEAgIECQYFBgkJ"
    "CQkJCQkJCQkJCQkJCQkJCQkJCQkJCQkJCQkJCQkJCQkJCQkJCQkJCQkJCQkJCQkJ/90A"
    "BAAB/9oADAMBAAIRAxEAPwD8X6/pQr+Dev8AcQo/aY/sM/8AiHP9if8AGUfWPrH1j/mD"
    "9ny+z9h/1FTvfn8rW630n9sF4lf8To/6u/uf7H/sf619r637b639X/u4b2fs/q39/n5/"
    "s8vvf//Z";

/** @brief One row of the report, PASS or FAIL; a FAIL is counted. */
static void
check(int cond, const char *row, const char *what)
{
    printf("  [%s] %-7s %s\n", cond ? "PASS" : "FAIL", row, what);
    if (!cond) {
        fails++;
    }
}

/** @brief Base64 decoded, to free(). */
static unsigned char *
unbase64(const char *b64, size_t *n)
{
    static const char abc[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    unsigned char *p = malloc(strlen(b64) + 1);
    unsigned v = 0;
    int bits = 0;

    for (*n = 0; *b64 && *b64 != '='; b64++) {
        v = v << 6 | (unsigned)(strchr(abc, *b64) - abc);
        if ((bits += 6) >= 8) {
            bits -= 8;
            p[(*n)++] = (unsigned char)(v >> bits);
        }
    }
    return p;
}

/** @brief @p s drawn with the test's face at (@p x, @p y), @p size
 *         points, each glyph @p gap points further than the face says. */
static void
text(tiku_pdf_writer_t *w, int font, const char *s, double x, double y,
     double size, uint32_t argb, int bold, int italic, double gap)
{
    tiku_pdf_glyph_t g[128];
    tiku_pdf_run_t run = { font, size, argb, bold, italic, x, y };
    double at = 0;
    int n = 0;

    for (; *s && n < 128; s++, n++) {
        g[n].gid = tiku_ttf_glyph(ttf, (unsigned char)*s);
        g[n].cp = (unsigned char)*s;
        g[n].x = at;
        at += tiku_ttf_advance(ttf, g[n].gid) * size / tiku_ttf_upem(ttf) +
              gap;
    }
    tiku_pdf_writer_text(w, &run, g, n);
}

/** @brief The bytes finished, read back.  @return the document, or NULL. */
static tiku_pdf_t *
reread(tiku_pdf_writer_t *w, size_t *len)
{
    unsigned char *b = tiku_pdf_writer_finish(w, len);
    tiku_pdf_t *doc = NULL;

    if (b == NULL || tiku_pdf_decode(b, *len, &doc) != TIKU_PDF_OK) {
        doc = NULL;
    }
    free(b);
    return doc;
}

/** @brief Pixel (@p x, @p y) of page @p p drawn at one pixel a point. */
static uint32_t
pixel(tiku_pdf_t *doc, int p, int x, int y)
{
    tiku_pdf_pixmap_t pm;
    uint32_t c = 0;

    if (tiku_pdf_render(doc, p, 1.0, 0, &pm) == 0) {
        if (x >= 0 && y >= 0 && x < pm.w && y < pm.h) {
            c = pm.px[y * pm.w + x] & 0xffffff;
        }
        tiku_pdf_pixmap_free(&pm);
    }
    return c;
}

/** @brief Whether colour @p c is within @p tol of @p want, channel by
 *         channel. */
static int
near(uint32_t c, uint32_t want, int tol)
{
    int k;

    for (k = 0; k < 24; k += 8) {
        if (abs((int)((c >> k) & 0xff) - (int)((want >> k) & 0xff)) > tol)
            return 0;
    }
    return 1;
}

/** @brief Pages, sizes, the title, and a writer used wrongly. */
static void
page_tests(void)
{
    tiku_pdf_writer_t *w = tiku_pdf_writer_new();
    double pw = 0, ph = 0;
    int rot = 0;
    size_t len;
    tiku_pdf_t *doc;
    char *title;

    tiku_pdf_writer_info(w, "Caf\xc3\xa9 \xe2\x80\x94 \xf0\x9d\x90\x80",
                         "Tiku");
    check(tiku_pdf_writer_page(w, 595, 842) == 0 &&
          tiku_pdf_writer_page(w, 842, 595) == 1 &&
          tiku_pdf_writer_page(w, 0, 10) == -1 &&
          tiku_pdf_writer_pages(w) == 2, "PW-001",
          "pages numbered as made; one of no size refused");
    doc = reread(w, &len);
    title = doc ? tiku_pdf_title(doc) : NULL;
    check(doc != NULL && tiku_pdf_pages(doc) == 2 &&
          tiku_pdf_page_size(doc, 1, &pw, &ph, &rot) == 0 && pw == 842 &&
          ph == 595, "PW-002", "read back: two pages, the second landscape");
    check(title != NULL && strcmp(title, "Caf\xc3\xa9 \xe2\x80\x94 "
          "\xf0\x9d\x90\x80") == 0, "PW-003",
          "the title's every character, one beyond the BMP too");
    free(title);
    tiku_pdf_close(doc);
    w = tiku_pdf_writer_new();
    tiku_pdf_writer_rect(w, 0, 0, 10, 10);
    tiku_pdf_writer_save(w);
    tiku_pdf_writer_link(w, 0, 0, 5, 5, "x");
    check(tiku_pdf_writer_finish(w, &len) == NULL && len == 0 &&
          tiku_pdf_writer_finish(NULL, &len) == NULL, "PW-004",
          "no page: drawing is ignored and nothing is written");
}

/** @brief Fills, strokes, a gradient, alpha, and the path's ends. */
static void
paint_tests(void)
{
    tiku_pdf_writer_t *w = tiku_pdf_writer_new();
    tiku_pdf_paint_t fill = {0}, half = {0}, line = {0};
    tiku_pdf_stop_t st[2] = {{0, 0xffff0000}, {1, 0xff0000ff}};
    tiku_pdf_gradient_t g = {0, 20, 0, 220, 0, 0, 0, st, 2};
    size_t len;
    tiku_pdf_t *doc;

    tiku_pdf_writer_page(w, 300, 300);
    fill.fill = 0xff2050c0;
    tiku_pdf_writer_rect(w, 20, 20, 100, 50);
    tiku_pdf_writer_paint(w, &fill);
    half.fill = 0x80ff0000;
    tiku_pdf_writer_rect(w, 150, 20, 100, 50);
    tiku_pdf_writer_paint(w, &half);
    line.stroke = 0xff000000;
    line.width = 6;
    tiku_pdf_writer_move(w, 20, 100);
    tiku_pdf_writer_line(w, 280, 100);
    tiku_pdf_writer_paint(w, &line);
    tiku_pdf_writer_rect(w, 20, 200, 200, 40);
    tiku_pdf_writer_gradient(w, &g);
    doc = reread(w, &len);
    check(doc && near(pixel(doc, 0, 70, 45), 0x2050c0, 2) &&
          near(pixel(doc, 0, 10, 10), 0xffffff, 0), "PW-010",
          "a fill where it was put, y down; white elsewhere");
    check(doc && near(pixel(doc, 0, 200, 45), 0xff7f7f, 3), "PW-011",
          "half-transparent red over white is pink");
    check(doc && near(pixel(doc, 0, 150, 100), 0x000000, 8) &&
          near(pixel(doc, 0, 150, 106), 0xffffff, 8), "PW-012",
          "a stroke as wide as asked, centred on its line");
    check(doc && near(pixel(doc, 0, 22, 220), 0xff0000, 12) &&
          near(pixel(doc, 0, 218, 220), 0x0000ff, 12) &&
          near(pixel(doc, 0, 120, 220), 0x800080, 12), "PW-013",
          "a gradient from red to blue along its line, clipped to its path");
    tiku_pdf_close(doc);
}

/** @brief Pictures: a JPEG as it is, pixels with alpha, turned. */
static void
image_tests(void)
{
    tiku_pdf_writer_t *w = tiku_pdf_writer_new();
    uint32_t px[4] = {0xffff0000, 0x0000ff00, 0xff0000ff, 0x800000ff};
    size_t n, len;
    unsigned char *jpg = unbase64(quad_jpg, &n);
    int pw = 0, ph = 0, a, b;
    tiku_pdf_t *doc;

    tiku_pdf_writer_page(w, 200, 200);
    a = tiku_pdf_writer_image(w, jpg, n, &pw, &ph);
    b = tiku_pdf_writer_pixels(w, px, 2, 2);
    tiku_pdf_writer_draw(w, a, 10, 10, 80, 80);
    tiku_pdf_writer_draw(w, b, 110, 10, 80, 80);
    tiku_pdf_writer_save(w);
    tiku_pdf_writer_transform(w, -1, 0, 0, 1, 190, 110);
    tiku_pdf_writer_draw(w, a, 0, 0, 80, 80);
    tiku_pdf_writer_restore(w);
    check(a >= 0 && pw == 16 && ph == 16 && b >= 0 &&
          tiku_pdf_writer_image(w, "not a picture", 13, NULL, NULL) == -1,
          "PW-020", "a JPEG's size read from its header; nonsense refused");
    doc = reread(w, &len);
    check(doc && near(pixel(doc, 0, 25, 25), 0xff0000, 40) &&
          near(pixel(doc, 0, 75, 25), 0x00ff00, 40) &&
          near(pixel(doc, 0, 25, 75), 0x0000ff, 40) &&
          near(pixel(doc, 0, 75, 75), 0xffffff, 40), "PW-021",
          "the JPEG passed through, its quarters where they belong");
    check(doc && near(pixel(doc, 0, 130, 30), 0xff0000, 4) &&
          near(pixel(doc, 0, 170, 30), 0xffffff, 4) &&
          near(pixel(doc, 0, 170, 70), 0x7f7fff, 6), "PW-022",
          "pixels drawn with their alpha: clear shows white, half blue");
    check(doc && near(pixel(doc, 0, 175, 125), 0xff0000, 40) &&
          near(pixel(doc, 0, 125, 125), 0x00ff00, 40), "PW-023",
          "and through a transform: mirrored, red on the right");
    free(jpg);
    tiku_pdf_close(doc);
}

/** @brief Links out and within, and the outline. */
static void
nav_tests(void)
{
    tiku_pdf_writer_t *w = tiku_pdf_writer_new();
    tiku_pdf_link_t *l = NULL;
    tiku_pdf_outline_t *o = NULL;
    size_t len;
    tiku_pdf_t *doc;
    int nl = 0, no = 0;

    tiku_pdf_writer_page(w, 400, 400);
    tiku_pdf_writer_link(w, 10, 20, 100, 30, "https://example.org/a(b)");
    tiku_pdf_writer_jump(w, 10, 100, 50, 20, 1, 150);
    tiku_pdf_writer_mark(w, 0, "One", 0, 0);
    tiku_pdf_writer_mark(w, 2, "One.one", 0, 50);
    tiku_pdf_writer_mark(w, 1, "One.two", 0, 90);
    tiku_pdf_writer_page(w, 400, 400);
    tiku_pdf_writer_mark(w, 0, "Two", 1, 150);
    doc = reread(w, &len);
    nl = doc ? tiku_pdf_links(doc, 0, &l) : 0;
    check(nl == 2 && l[0].to.uri && strcmp(l[0].to.uri,
          "https://example.org/a(b)") == 0 && fabs(l[0].x0 - 10) < 0.5 &&
          fabs(l[0].y0 - 20) < 0.5 && fabs(l[0].y1 - 50) < 0.5, "PW-030",
          "a link out: its address, and its box, y down");
    check(nl == 2 && l[1].to.page == 1 && fabs(l[1].to.top - 150) < 1,
          "PW-031", "a link within: to the page and the height asked");
    no = doc ? tiku_pdf_outline(doc, &o) : 0;
    check(no == 4 && o[0].level == 0 && o[1].level == 1 && o[2].level == 1 &&
          o[3].level == 0 && strcmp(o[2].title, "One.two") == 0 &&
          o[0].below == 2 && o[3].to.page == 1 &&
          fabs(o[1].to.top - 50) < 1, "PW-032",
          "the outline's entries, a level that skips read as one deeper");
    tiku_pdf_links_free(l, nl);
    tiku_pdf_outline_free(o, no);
    tiku_pdf_close(doc);
}

/** @brief How much ink a page has in the box, and where its middle is. */
static long
ink(tiku_pdf_t *doc, int x0, int y0, int x1, int y1, double *cx_top,
    double *cx_bottom)
{
    tiku_pdf_pixmap_t pm;
    double st = 0, sb = 0, nt = 0, nb = 0;
    long sum = 0;
    int x, y;

    if (tiku_pdf_render(doc, 0, 2.0, 0, &pm) != 0) {
        return 0;
    }
    for (y = y0 * 2; y < y1 * 2 && y < pm.h; y++) {
        for (x = x0 * 2; x < x1 * 2 && x < pm.w; x++) {
            int v = 255 - (int)(pm.px[y * pm.w + x] & 0xff);

            sum += v;
            if (y < (y0 + y1)) {
                st += v * x;
                nt += v;
            } else {
                sb += v * x;
                nb += v;
            }
        }
    }
    *cx_top = nt > 0 ? st / nt : 0;
    *cx_bottom = nb > 0 ? sb / nb : 0;
    tiku_pdf_pixmap_free(&pm);
    return sum;
}

/** @brief Text: found and placed, a subset, bold and a slant made up. */
static void
text_tests(void)
{
    tiku_pdf_writer_t *w = tiku_pdf_writer_new();
    tiku_pdf_char_t *ch = NULL;
    double t1, b1, t2, b2, t3, b3;
    long n = 0, plain, bold, k;
    struct stat st;
    size_t len;
    tiku_pdf_t *doc;
    char *s;
    int f;

    tiku_pdf_writer_page(w, 400, 300);
    f = tiku_pdf_writer_font(w, face.path, face.index);
    text(w, f, "Hello (world)", 20, 40, 20, 0xff000000, 0, 0, 0);
    text(w, f, "Spaced", 20, 80, 20, 0xff000000, 0, 0, 1.5);
    text(w, f, "IIIIIIII", 20, 140, 30, 0xff000000, 0, 0, 0);
    text(w, f, "IIIIIIII", 20, 200, 30, 0xff000000, 1, 0, 0);
    text(w, f, "IIIIIIII", 20, 260, 30, 0xff000000, 0, 1, 0);
    check(f >= 0 && tiku_pdf_writer_font(w, face.path, face.index) == f &&
          tiku_pdf_writer_font(w, "/no/such/face.ttf", 0) == -1, "PW-040",
          "a face taken once; one that is not there refused");
    doc = reread(w, &len);
    s = doc ? tiku_pdf_page_text(doc, 0) : NULL;
    check(s != NULL && strstr(s, "Hello (world)") != NULL &&
          strstr(s, "Spaced") != NULL, "PW-041",
          "the text found again, through the glyphs' ToUnicode map");
    free(s);
    n = doc ? tiku_pdf_page_chars(doc, 0, &ch) : 0;
    for (k = 0; k < n && ch[k].cp != 'S'; k++) {}
    check(n > 20 && fabs(ch[0].x0 - 20) < 1 && ch[0].y1 > 30 &&
          ch[0].y0 < 40 && k + 1 < n && fabs(ch[k + 1].x0 - ch[k].x1 -
          1.5) < 0.3, "PW-042",
          "each character where it was drawn, spaced where it was spaced");
    check(stat(face.path, &st) == 0 && (long long)len < (long long)st.st_size
          / 2, "PW-043", "the face cut to its glyphs: far smaller than it");
    plain = doc ? ink(doc, 15, 110, 200, 150, &t1, &b1) : 0;
    bold = doc ? ink(doc, 15, 170, 200, 210, &t2, &b2) : 0;
    (void)ink(doc, 15, 230, 200, 270, &t3, &b3);
    check(plain > 0 && bold > plain * 11 / 10, "PW-044",
          "bold made up: its stems heavier than the plain face's");
    check(t3 - b3 > 4 && fabs(t1 - b1) < 2, "PW-045",
          "a slant made up: the tops lean right of the feet");
    free(ch);
    tiku_pdf_close(doc);
}

/** @brief Every group; the text ones with a face this machine has. */
int
main(void)
{
    printf("PDF writer: pages written, read back by the PDF kit\n");
    page_tests();
    paint_tests();
    image_tests();
    nav_tests();
    if (tiku_fontset_find("Arial", 400, 0, &face) == 0 &&
        (ttf = tiku_ttf_open_face(face.path, (unsigned)face.index)) != NULL) {
        text_tests();
        tiku_ttf_close(ttf);
    } else {
        printf("  [SKIP] no face on this machine to write text with\n");
    }
    printf("%s\n", fails ? "FAIL" : "PASS: pages, paint, pictures, links, "
           "outline and text written and read back");
    return fails ? 1 : 0;
}
