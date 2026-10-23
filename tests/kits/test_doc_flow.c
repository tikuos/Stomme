/*
 * TikuOS Interface Kit.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * test_doc_flow.c - HTML text alignment, line spacing and whitespace.
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tiku_doc.h"
#include "tiku_doc_paint.h"
#include "tiku_font.h"

static tiku_doc_t doc;
static int failures;

/** @brief Record an assertion while retaining subsequent checks. */
static void check(int condition, const char *message)
{
    printf("[%s] flow %s\n",condition ? "PASS" : "FAIL",message);
    failures += !condition;
}

/** @brief Parse and lay out a page at the requested width. */
static void read_flow(const char *html, int width)
{
    tiku_doc_read(&doc,html); tiku_doc_layout(&doc,width);
}

/** @brief Supply one deterministic local image size without decoding pixels. */
static int image_size(void *ctx, const char *src, int *w, int *h)
{
    (void)ctx; (void)src; *w = 60; *h = 20; return 1;
}

/** @brief Copy visible text without run terminators. */
static void words(char *out, size_t cap)
{
    size_t at = 0;
    for (int i = 0; i < doc.nrun; i++) {
        size_t n = (size_t)doc.run[i].len;
        if (n >= cap-at) n = cap-at-1;
        memcpy(out+at,doc.text+doc.run[i].off,n); at += n;
    }
    out[at] = 0;
}

/** @brief Check inherited alignment and shared painting/hit geometry. */
static void alignment(void)
{
    read_flow("<div style='text-align:center'><p>Centered</p>"
              "<p style='text-align:right'>Right</p></div><p>Left</p>",300);
    check(doc.nline == 3 && doc.line[0].x == (300-doc.line[0].w)/2 &&
          doc.line[1].x == 300-doc.line[1].w && doc.line[2].x == 0,
          "center and right align inherit and restore at block boundaries");
    read_flow("<style>p{text-align:right!important}</style>"
              "<p style='text-align:center'>Priority</p>",300);
    check(doc.nline == 1 && doc.line[0].x == 300-doc.line[0].w,
          "alignment retains per-property importance");
    read_flow("<p style='text-align:center'><a href='#end'>Aligned link</a></p>",300);
    tiku_rect_t box = tiku_doc_piece_rect(&doc,0);
    check(box.x > 0 && tiku_doc_hit(&doc,box.x+box.w/2,box.y+box.h/2).kind ==
          TIKU_DOC_HIT_LINK, "aligned link hits use the shifted piece rectangle");
    read_flow("<blockquote style='text-align:end'>Indented words that wrap to several lines</blockquote>",120);
    int valid = doc.nline > 1;
    for (int i = 0; i < doc.nline; i++)
        valid &= doc.line[i].x+doc.line[i].w == 120;
    check(valid && tiku_doc_check(&doc), "right alignment uses remaining width after indentation");
    read_flow("<table style='text-align:center'><tr><td>A</td><td>BBBB</td></tr></table>",300);
    int x = doc.nline > 1 ? doc.line[1].x : 0;
    tiku_doc_layout(&doc,160);
    check(doc.nline == 2 && doc.line[1].x < x && tiku_doc_check(&doc),
          "aligned table cells retain independent geometry across reflow");
    tiku_doc_images(&doc,image_size,NULL);
    read_flow("<div style='text-align:center'><img src='local.bmp'></div>"
              "<div style='text-align:right'><img src='local.bmp'></div>",300);
    check(doc.nline == 2 && doc.line[0].x == 120 && doc.line[1].x == 240 &&
          tiku_doc_hit(&doc,250,doc.line[1].y+5).kind == TIKU_DOC_HIT_IMAGE,
          "alignment uses decoded image width and retains image hit geometry");
    tiku_doc_images(&doc,NULL,NULL);
}

/** @brief Check line-height lengths, relative inheritance and empty lines. */
static void spacing(void)
{
    read_flow("<p style='line-height:40px'>First<br>Second</p>",300);
    check(doc.nline == 2 && doc.line[0].h == 40 && doc.line[1].h == 40 &&
          doc.line[1].y-doc.line[0].y == 40,
          "pixel line height controls each line's advance");
    read_flow("<div style='line-height:2'><p style='font-size:20px'>Large</p></div>",300);
    check(doc.nline == 1 && doc.line[0].h == 40,
          "unitless line height inherits as a factor of descendant font size");
    read_flow("<div style='font-size:20px;line-height:200%'>"
              "<p style='font-size:12px'>Small</p></div>",300);
    check(doc.nline == 1 && doc.line[0].h == 40,
          "percentage line height inherits its computed parent length");
    read_flow("<p style='font-size:14px;line-height:2em'>Em</p>"
              "<p style='font-size:20px;line-height:2rem'>Root</p>",300);
    check(doc.nline == 2 && doc.line[0].h == 28 && doc.line[1].h == 24,
          "em and rem line heights use the element and root font sizes");
    read_flow("<p style='line-height:40px'>Text <span style='line-height:normal'>inner</span></p>",300);
    check(doc.nline == 1 && doc.line[0].h == 40,
          "the paragraph line-height strut survives a smaller inline override");
    read_flow("<pre style='line-height:32px'>A\n\nB</pre>",300);
    check(doc.nline == 3 && doc.line[1].h == 32,
          "empty preformatted lines retain inherited line height");
    read_flow("<p style='line-height:2'>Resize</p>",300);
    tiku_font_set_size(16); tiku_doc_layout(&doc,300);
    check(doc.nline == 1 && doc.line[0].h == 32,
          "relative line height reflows with Reader text size");
    tiku_font_set_size(12);
    char html[96];
    snprintf(html,sizeof html,"<p style='line-height:%dpx'>Clamped</p>",tiku_font_plain()->height);
    read_flow(html,300);
    tiku_font_set_size(16); tiku_doc_layout(&doc,300);
    check(doc.nline == 1 && doc.line[0].h >= tiku_measure_height(doc.run[0].face) &&
          (tiku_doc_notices(&doc) & TIKU_DOC_FLOW_FALLBACK),
          "line heights below glyph metrics report a bounded fallback");
    tiku_font_set_size(12); tiku_doc_layout(&doc,300);
    check(!(tiku_doc_notices(&doc) & TIKU_DOC_FLOW_FALLBACK),
          "reflow clears line-height fallback when the requested height fits");
}

/** @brief Check preserved/collapsed spaces and bounded unwrapped lines. */
static void whitespace(void)
{
    char text[256];
    read_flow("<div style='white-space:pre-wrap'> A  B\nC  </div>",300);
    words(text,sizeof text);
    check(!strcmp(text," A  B\nC  ") && doc.nline == 2 &&
          doc.line[1].w == tiku_measure_width(doc.run[0].face,"C  "),
          "pre-wrap preserves leading, repeated and trailing whitespace");
    read_flow("<pre style='white-space:normal'> A  B\nC </pre>",300);
    words(text,sizeof text);
    check(!strcmp(text,"A B C") && doc.nline == 1,
          "normal whitespace overrides preformatted element defaults");
    read_flow("<div style='white-space:pre'>A\r\n\rB\tC</div>",300);
    words(text,sizeof text);
    check(!strcmp(text,"A\n\nB    C") && doc.nline == 3,
          "preserved carriage returns normalize and tabs expand to four spaces");
    read_flow("<p style='white-space:nowrap'>A lengthy line with spaces and words</p>",40);
    words(text,sizeof text);
    check(doc.nline == 1 && doc.line[0].w > 40 &&
          !strcmp(text,"A lengthy line with spaces and words") && tiku_doc_check(&doc),
          "nowrap retains one bounded line for viewport clipping");
    read_flow("<style>p{white-space:nowrap!important}</style>"
              "<p style='white-space:normal'>Many words on one line</p>",40);
    check(doc.nline == 1 && doc.line[0].w > 40,
          "whitespace importance survives stylesheet and inline merging");
    read_flow("<p style='white-space:pre'>LongUnbrokenWord\nSecond line</p>",40);
    check(doc.nline == 2 && !(doc.full & TIKU_DOC_NARROW),
          "pre retains explicit newlines without forced word splitting");
    read_flow("<p style='white-space:pre-wrap'>LongUnbrokenWord</p>",40);
    check(doc.nline > 1 && (doc.full & TIKU_DOC_NARROW),
          "pre-wrap still breaks long words to fit the content width");
    read_flow("<div style='white-space:pre-wrap'><p>A  B</p></div><p>C  D</p>",300);
    words(text,sizeof text);
    check(!strcmp(text,"A  BC D") && tiku_doc_check(&doc),
          "whitespace inheritance restores after its container closes");
    read_flow("<p style='text-align:justify;line-height:-2;white-space:break-spaces'>Kept</p>",300);
    words(text,sizeof text);
    check(!strcmp(text,"Kept") && (tiku_doc_notices(&doc) & TIKU_DOC_CSS_UNSUPPORTED),
          "unsupported flow values retain document text and report conversion");
}

/** @brief Compare aligned, spaced text pixels with an independently placed face. */
static void pixels(void)
{
    read_flow("<p style='font-size:14px;text-align:center;line-height:40px'>Pixels</p>",300);
    const tiku_font_t *font = tiku_font_at(14);
    tiku_surface_t *actual = tiku_surface_new(320,80,TIKU_C_DOC);
    tiku_surface_t *expected = tiku_surface_new(320,80,TIKU_C_DOC);
    check(actual && expected,"flow comparison surfaces allocated");
    if (actual && expected) {
        tiku_doc_draw(actual,&doc,(tiku_rect_t){10,10,300,60},NULL);
        int x = 10+(300-tiku_text_width(font,"Pixels"))/2;
        int baseline = 10+font->ascent+(40-font->height)/2;
        tiku_text(expected,font,x,baseline,"Pixels",tiku_doc_ink(TIKU_DOC_INK_PLAIN));
        check(!memcmp(actual->px,expected->px,(size_t)320*80*sizeof *actual->px),
              "aligned text paints at the expected centered baseline");
        const char *path = getenv("TIKU_FLOW_SNAPSHOT");
        if (path) check(!tiku_surface_png(actual,path),"flow snapshot written");
    }
    tiku_surface_free(actual); tiku_surface_free(expected);
}

/** @brief Exercise every prefix of styled input without readable sentinel bytes. */
static void truncated(void)
{
    const char source[] = "<style>.x{white-space:pre-wrap!important;line-height:1.5}</style>"
        "<div class='x' style='text-align:right'><p>A  B\r\nC</p>"
        "<span style='line-height:200%'>D</span></div>";
    int valid = 1;
    for (size_t n = 0; n < sizeof source; n++) {
        char *prefix = malloc(n+1);
        if (!prefix) { valid = 0; break; }
        memcpy(prefix,source,n); prefix[n] = 0;
        read_flow(prefix,43); valid &= tiku_doc_check(&doc);
        free(prefix);
    }
    check(valid,"truncated flow declarations and nested elements retain valid tables");
}

/** @brief Bound tall-document coordinates while retaining the parsed source. */
static void tall(void)
{
    const int count = 525000;
    tiku_doc_limits_t limits = {count+256,4,4,count+4,4,1,1,1};
    size_t bytes = tiku_doc_storage_size(&limits);
    void *storage = malloc(bytes);
    char *source = malloc((size_t)count+128);
    check(storage && source,"tall flow fixture storage allocated");
    if (storage && source) {
        int n = sprintf(source,"<p style='font-size:512px;line-height:64;white-space:pre'>A");
        memset(source+n,'\n',(size_t)count); strcpy(source+n+count,"B</p>");
        check(tiku_doc_init_storage(&doc,&limits,storage,bytes),"tall flow tables initialized");
        tiku_doc_read(&doc,source); tiku_doc_layout(&doc,300);
        const char *what = NULL; int cap = 0;
        check(doc.height > 0 && doc.height < INT32_MAX/4 &&
              doc.nline < count && tiku_doc_check(&doc) &&
              tiku_doc_full_first(&doc,&what,&cap) && what && !strcmp(what,"layout height"),
              "oversized line spacing stops at a reported coordinate bound");
        check(doc.nrun && doc.run[0].len == count+2 &&
              doc.text[doc.run[0].off+count+1] == 'B',
              "height-limited layout retains text beyond its visible cutoff");
        check(doc.nblock && doc.block[doc.nblock-1].kind == TIKU_DOC_BLOCK_NOTICE &&
              doc.block[doc.nblock-1].nline == 1,
              "the coordinate cutoff retains a visible capacity notice");
        n = sprintf(source,"<p style='font-size:1rem;line-height:64;white-space:pre'>A");
        memset(source+n,'\n',(size_t)count); strcpy(source+n+count,"B</p>");
        tiku_font_set_size(16);
        tiku_doc_read(&doc,source); tiku_doc_layout(&doc,300);
        int limited = tiku_doc_full_first(&doc,&what,&cap) && what && !strcmp(what,"layout height");
        tiku_font_set_size(12); tiku_doc_layout(&doc,300);
        check(limited && !tiku_doc_full_first(&doc,NULL,NULL) &&
              doc.nline == count+1 && tiku_doc_check(&doc),
              "smaller relative text reflows past the previous height cutoff");
        tiku_doc_init(&doc);
    }
    free(storage); free(source);
}

int main(void)
{
    tiku_font_set_size(12); tiku_font_set_family(0); tiku_doc_init(&doc);
    alignment(); spacing(); whitespace(); pixels(); truncated(); tall();
    printf("Flow: %d failure(s)\n",failures);
    return failures != 0;
}
