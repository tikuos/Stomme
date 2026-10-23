/*
 * TikuOS Interface Kit.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * test_doc_html.c - HTML token boundaries, structure and document fixtures.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TEST_SOURCE_ROOT
#define TIKU_TEST_SOURCE_ROOT "."
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tiku_doc.h"
#include "tiku_doc_paint.h"
#include "tiku_font.h"

static tiku_doc_t doc;
static int failures;

/** @brief Record an assertion and retain subsequent checks. */
static void check(int condition, const char *message)
{
    printf("[%s] html %s\n", condition ? "PASS" : "FAIL", message);
    failures += !condition;
}

/** @brief Join visible runs, separating blocks with a newline. */
static const char *text(void)
{
    static char result[32768];
    size_t at = 0;
    for (int i = 0; i < doc.nblock; i++) {
        const tiku_doc_block_t *b = &doc.block[i];
        for (int k = b->run0; k < b->run0 + b->nrun; k++) {
            const tiku_doc_run_t *run = &doc.run[k];
            if (at + run->len + 2 >= sizeof result) break;
            memcpy(result + at, doc.text + run->off, (size_t)run->len);
            at += run->len;
        }
        if (at + 2 < sizeof result) result[at++] = '\n';
    }
    result[at] = 0;
    return result;
}

/** @brief Read and lay out HTML with the default fixed capacities. */
static void read_html(const char *html)
{
    tiku_doc_read(&doc, html);
    tiku_doc_layout(&doc, 720);
}

/** @brief Verify HTML names, values and skipped-content boundaries. */
static void tokens(void)
{
    read_html("<P><STRONG>Bold</STRONG> <A HREF = 'next.html?a=1&gt;0'>Next</A></P>");
    check(doc.nblock == 1 && doc.nrun && doc.run[0].face == TIKU_MEASURE_BOLD,
          "mixed-case tags retain formatting");
    check(doc.nlink == 1 && !strcmp(tiku_doc_link_href(&doc, 0), "next.html?a=1>0"),
          "case-insensitive attributes decode their own values");
    read_html("<p title='a > b'><a\fHREF\t=\n\"page>one.html\">Visible</a></p>");
    check(!strcmp(text(), "Visible\n") && doc.nlink == 1 &&
          !strcmp(tiku_doc_link_href(&doc, 0), "page>one.html"),
          "quoted delimiters and HTML whitespace stay inside the tag");
    read_html("<a disabled data-x href=next.html>Link</a>");
    check(doc.nlink == 1 && !strcmp(tiku_doc_link_href(&doc, 0), "next.html"),
          "boolean attributes do not consume the following attribute");
    read_html("<HEAD><script>x</script>PRIVATE<title>Hidden</title></HEAD>"
              "<p>Shown</p><script>one</style>SECRET</ScRiPt><p>Tail</p>");
    check(!strcmp(text(), "Shown\nTail\n"),
          "head and raw content end at their matching closing names");
    read_html("<script>hidden</scripture>STILL_HIDDEN</script ><p>Visible</p>");
    check(!strcmp(text(), "Visible\n"), "raw closing names require a delimiter");
    read_html("<p>One<!-- > <p>hidden</p> -->Two</p><!doctype html><p>Three</p>");
    check(!strcmp(text(), "OneTwo\nThree\n"), "comments and declarations remain absent");
    read_html("<p>Before &amp; &#0; &#xD800; &#1114112; &unknown; After</p>");
    check(strstr(text(), "After") && strstr(text(), "&#0;") &&
          strstr(text(), "&#xD800;") && strstr(text(), "&unknown;"),
          "invalid numeric entities cannot insert string terminators");
    read_html("<pre><code>if (a &lt; b) { &amp;x; }\n  two  spaces</code></pre>");
    check(!strcmp(text(), "if (a < b) { &x; }\n  two  spaces\n"),
          "code retains literal entity characters and whitespace");
    read_html("<p>2 < 3; 4 <_literal; 6 <9</p>");
    check(!strcmp(text(), "2 < 3; 4 <_literal; 6 <9\n"),
          "literal less-than characters remain visible");
    char large[1600];
    memset(large, 'x', sizeof large);
    memcpy(large, "<a href='", 9);
    memcpy(large + 1209, "'>Long</a>", 11);
    large[1220] = 0;
    read_html(large);
    check(doc.nlink == 1 && strlen(tiku_doc_link_href(&doc, 0)) == 1200,
          "attribute values are not silently shortened to scratch size");
}

/** @brief Verify blocks, list numbering and link destinations. */
static void structure(void)
{
    read_html("<header>Header</header><main><article><section id='section'>"
              "<div id='first'>First</div><div>Second</div></section></article></main>"
              "<footer>Footer</footer>");
    check(!strcmp(text(), "Header\nFirst\nSecond\nFooter\n"),
          "structural containers separate text blocks");
    check(tiku_doc_anchor_y(&doc, "section") == doc.block[1].y &&
          tiku_doc_anchor_y(&doc, "first") == doc.block[1].y,
          "container IDs target their first visible block");
    read_html("<p id='paragraph'>One <span id='inline'>two</span> three</p>"
              "<a id='target'></a><p>Four</p><img id='picture' src='x' alt='Photo'>");
    check(!strncmp(text(), "One two three\nFour\nPhoto\n", 25),
          "anchor attributes never become visible words");
    check(tiku_doc_anchor_y(&doc, "paragraph") == doc.block[0].y &&
          tiku_doc_anchor_y(&doc, "inline") == doc.block[0].y &&
          tiku_doc_anchor_y(&doc, "target") == doc.block[1].y &&
          tiku_doc_anchor_y(&doc, "picture") == doc.block[2].y,
          "paragraph, inline, empty anchor and image IDs resolve");
    read_html("<h1>One</h1><h2>Two</h2><h3>Three</h3>"
              "<h4 id='four'>Four</h4><h5>Five</h5><h6>Six</h6>");
    int headings = doc.nblock == 6;
    for (int i = 0; i < doc.nblock; i++)
        headings &= doc.block[i].kind != TIKU_DOC_BLOCK_P &&
                    doc.run[doc.block[i].run0].face != TIKU_MEASURE_PLAIN;
    check(headings && tiku_doc_anchor_y(&doc, "four") > 0,
          "six heading levels retain heading presentation and anchors");
    for (int i = 0; i < doc.nblock; i++)
        check(tiku_doc_heading_level(&doc.block[i]) == i + 1,
              "heading metadata preserves its source level");
    read_html("<h4>Only heading</h4>");
    check(tiku_doc_title(&doc) && !strcmp(tiku_doc_title(&doc), "Only heading"),
          "a document without H1 still has its first heading title");
    read_html("<dl><dt id='term'>Term</dt><dd>Explanation</dd>"
              "<dt>Other</dt><dd>Another explanation</dd></dl><p>Tail</p>");
    check(doc.nblock == 5 && doc.run[doc.block[0].run0].face == TIKU_MEASURE_BOLD &&
          doc.line[doc.block[1].line0].x > doc.line[doc.block[0].line0].x &&
          doc.run[doc.block[4].run0].face == TIKU_MEASURE_PLAIN,
          "definition terms are bold and descriptions are indented");
    read_html("<ol start='7'><li>Seven<li value='12'>Twelve<li>Thirteen</ol>"
              "<ol start='-2'><li>Negative<li>Negative one<li>Zero</ol>");
    check(doc.nblock == 6 && doc.block[0].index == 7 && doc.block[1].index == 12 &&
          doc.block[2].index == 13 && doc.block[3].index == -2 &&
          doc.block[4].index == -1 && doc.block[5].index == 0 &&
          !strncmp(doc.text + doc.run[doc.block[3].run0].off, "-2.", 3),
          "ordered-list start and item values include signed numbers");
    check(tiku_doc_check(&doc), "extended structure preserves document invariants");
    read_html("<dl><dt>Term<dd><p>First</p><p>Second</p>Third</dl><p>Tail</p>");
    check(doc.nblock == 5 && doc.block[1].kind == TIKU_DOC_BLOCK_DEFINITION &&
          doc.block[2].kind == TIKU_DOC_BLOCK_DEFINITION &&
          doc.block[3].kind == TIKU_DOC_BLOCK_DEFINITION &&
          doc.run[doc.block[4].run0].face == TIKU_MEASURE_PLAIN,
          "definition descriptions retain indentation across paragraphs");
    read_html("<blockquote>Outer<blockquote>Inner</blockquote>Outer again</blockquote>");
    check(doc.nblock == 3 && doc.block[2].kind == TIKU_DOC_BLOCK_QUOTE,
          "closing a nested quote retains the outer quote");
    read_html("<ol start='2147483647'><li>Last<li>Bounded</ol>"
              "<ol start='-2147483648'><li>First<li>Next</ol>");
    check(doc.nblock == 4 && doc.block[1].index == INT32_MAX &&
          doc.block[2].index == INT32_MIN && doc.block[3].index == INT32_MIN + 1 &&
          !strcmp(doc.text + doc.run[doc.block[2].run0].off, "-2147483648."),
          "list numbering remains defined at both integer limits");
    read_html("<table><tr><td>One</td><td colspan='2147483647'>Two</td></tr></table>");
    check((doc.full & TIKU_DOC_COLS_FOLDED) && tiku_doc_check(&doc),
          "large column spans fold without integer overflow");
}

/** @brief Exercise truncated inputs and repeated malformed tag candidates. */
static void malformed(void)
{
    const char *samples[] = {"<", "<!", "<!--", "<a", "<a href='unterminated>",
        "<p><strong>kept</p>tail", "<head><script>x</script></head><p>end</p>",
        "<a = = = href='next'>text</a>", "<p <p>text</p>", "&#x;", "<script/>",
        "<ol start='999999999999999999999'><li>x</ol>"};
    int valid = 1;
    for (size_t i = 0; i < sizeof samples / sizeof samples[0]; i++) {
        size_t len = strlen(samples[i]);
        for (size_t n = 0; n <= len; n++) {
            char *prefix = malloc(n + 1);
            if (!prefix) { valid = 0; break; }
            memcpy(prefix, samples[i], n); prefix[n] = 0;
            read_html(prefix);
            valid &= tiku_doc_check(&doc);
            free(prefix);
        }
    }
    check(valid, "every truncated sample terminates with consistent tables");
    char repeated[8193];
    for (int i = 0; i < 8192; i += 2) { repeated[i] = '<'; repeated[i+1] = 'a'; }
    repeated[8192] = 0;
    read_html(repeated);
    check(tiku_doc_check(&doc), "repeated malformed candidates remain bounded");
}

/** @brief Read fixture files and optionally capture their painted pages. */
static void fixtures(void)
{
    static const char *names[] = {"manual", "article", "report", "gallery", "table", "showcase"};
    const char *snapshots = getenv("TIKU_HTML_SNAPSHOTS");
    for (size_t i = 0; i < sizeof names / sizeof names[0]; i++) {
        char path[1024], source[8192];
        snprintf(path, sizeof path, TIKU_TEST_SOURCE_ROOT "/tests/fixtures/html/%s.html", names[i]);
        FILE *file = fopen(path, "rb");
        check(file != NULL, path);
        if (!file) continue;
        size_t n = fread(source, 1, sizeof source - 1, file);
        int ok = !ferror(file) && feof(file); fclose(file); source[n] = 0;
        check(ok, "fixture source fits its test buffer");
        read_html(source);
        check(strstr(text(), "TAIL_") != NULL && tiku_doc_check(&doc),
              "fixture keeps its tail and consistent layout");
        const int widths[] = {360, 720};
        for (int w = 0; w < 2; w++) {
            tiku_doc_layout(&doc, widths[w]);
            check(tiku_doc_check(&doc), "fixture reflow preserves layout invariants");
            for (int p = 0; p < doc.npiece; p++) {
                if (doc.run[doc.piece[p].run].link == TIKU_DOC_NONE) continue;
                tiku_rect_t r = tiku_doc_piece_rect(&doc, p);
                if (r.w > 0) check(tiku_doc_hit(&doc, r.x + r.w/2, r.y + r.h/2).kind ==
                                   TIKU_DOC_HIT_LINK, "fixture link hit follows its painted piece");
            }
            if (snapshots) {
                tiku_surface_t *s = tiku_surface_new(widths[w] + 32, 1000, TIKU_C_DOC);
                check(s != NULL, "snapshot surface allocated");
                if (!s) continue;
                tiku_doc_draw(s, &doc, (tiku_rect_t){16,16,widths[w],968}, NULL);
                snprintf(path, sizeof path, "%s/%s-%d.png", snapshots, names[i], widths[w]);
                check(tiku_surface_png(s, path) == 0, "fixture snapshot saved");
                tiku_surface_free(s);
            }
        }
    }
}

int main(void)
{
    tiku_font_set_size(12);
    tiku_doc_init(&doc);
    tokens(); structure(); malformed(); fixtures();
    printf("HTML: %d failure(s)\n", failures);
    return failures != 0;
}
