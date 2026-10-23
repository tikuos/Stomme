/*
 * TikuDesktop Translation Kit tests.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_richmarkup.c - editable HTML/Markdown structure, limits and loss reports.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richmarkup.h"
#include "tiku_richhtml.h"
#include "tiku_richoutline.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** @brief Import HTML and retain a snapshot independently of its document. */
static tiku_richsnapshot_t *
html(const char *text, unsigned *notices)
{
    tiku_richdoc_t *doc = NULL;
    tiku_richsnapshot_t *snapshot = NULL;

    assert(tiku_richmarkup_read_html(text, strlen(text), NULL, &doc, notices) ==
           TIKU_RICHIO_OK);
    assert(!tiku_richdoc_dirty(doc));
    assert(tiku_richdoc_snapshot(doc, &snapshot) == TIKU_RICH_OK);
    tiku_richdoc_release(doc);
    return snapshot;
}

/** @brief Verify exact paragraph text without relying on C-string storage. */
static tiku_rich_paragraph_t
paragraph(tiku_richsnapshot_t *s, size_t index, const char *text)
{
    tiku_rich_paragraph_t p;

    assert(tiku_richsnapshot_paragraph(s, index, &p) == TIKU_RICH_OK);
    assert(p.length == strlen(text) && !memcmp(p.text, text, p.length));
    return p;
}

/** @brief Semantic formatting, entity decoding, whitespace and link filtering. */
static void
formatting(void)
{
    unsigned notices = 0;
    tiku_richsnapshot_t *s = html(
        "<!doctype html><HTML><head><title>Hidden</title></head><body>"
        "<h2>Heading &amp; &#x1f600;</h2><p>  Hello <b>bold</b> "
        "<i>world</i>!<br>next &nbsp; line</p>"
        "<p><a href='https://example.org/?a=1&amp;b=2'>link</a> "
        "<a href='javascript:alert(1)'>plain</a></p>"
        "<pre>a\r\n b\r c</pre><script>if (1 < 2) BAD</script></body></HTML>",
        &notices);
    tiku_rich_paragraph_t p;
    tiku_rich_chars_t chars;
    tiku_rich_para_t props;

    assert(tiku_richsnapshot_paragraphs(s) == 4);
    p = paragraph(s, 0, "Heading & \xf0\x9f\x98\x80");
    assert(p.style);
    assert(tiku_richsnapshot_resolve(s, 0, 0, &chars, &props) == TIKU_RICH_OK);
    assert(chars.bold && chars.size == 22 && props.keep_next);
    {
        unsigned char *output = NULL;
        size_t length = 0;
        unsigned losses;

        assert(tiku_richhtml_write(s, NULL, &output, &length, &losses) ==
               TIKU_RICHIO_OK);
        assert(strstr((char *)output, "<h2 style="));
        free(output);
    }
    p = paragraph(s, 1, "Hello bold world!\nnext \xc2\xa0 line");
    assert(p.run_count >= 4 && p.runs[1].chars.bold);
    p = paragraph(s, 2, "link plain");
    assert(!strcmp(p.runs[0].link, "https://example.org/?a=1&b=2"));
    assert(!p.runs[p.run_count-1].link);
    paragraph(s, 3, "a\n b\n c");
    assert(notices & TIKU_RICHMARKUP_CONTENT);
    tiku_richsnapshot_release(s);
}

/** @brief Nested editable tables, optional end tags, headers and lists. */
static void
structure(void)
{
    unsigned notices;
    tiku_richsnapshot_t *s = html(
        "<p>before<p>after<ul><li>one<li>two<ol start='4'><li>four"
        "<li>five</ol></ul><table><thead><tr><th>A<th>B</thead><tbody>"
        "<tr><td><table><tr><td>inner</td></tr></table><td>Z"
        "</tbody></table><p>end", &notices);
    tiku_rich_paragraph_t p;
    size_t tables = 0;

    assert(tiku_richsnapshot_paragraphs(s) == 11);
    paragraph(s, 0, "before"); paragraph(s, 1, "after");
    p = paragraph(s, 2, "one"); assert(p.list.kind == TIKU_RICH_LIST_BULLET);
    p = paragraph(s, 4, "four");
    assert(p.list.kind == TIKU_RICH_LIST_DECIMAL && p.list.level == 1 && p.list.restart == 4);
    p = paragraph(s, 5, "five"); assert(p.list.restart == 0);
    paragraph(s, 8, "inner"); paragraph(s, 10, "end");
    for (size_t i = 0; i < tiku_richsnapshot_blocks(s); i++) {
        tiku_rich_block_t block;

        assert(tiku_richsnapshot_block(s, i, &block) == TIKU_RICH_OK);
        if (block.kind == TIKU_RICH_BLOCK_TABLE) {
            if (!tables) assert(block.table.rows == 2 && block.table.columns == 2 &&
                                 block.table.header_rows == 1);
            else assert(block.table.rows == 1 && block.table.columns == 1);
            tables++;
        }
    }
    assert(tables == 2);
    tiku_richsnapshot_release(s);
}

/** @brief Resolved inline CSS is preserved; unsupported declarations are explicit. */
static void
css(void)
{
    unsigned notices;
    tiku_richsnapshot_t *s = html(
        "<p style='text-align:center;margin-left:24px;white-space:pre-wrap'>"
        " a  <span style='font-size:16pt;color:#1234;font-weight:700;"
        "font-family:&quot;\\41 rial&quot;;position:absolute'>B</span></p>", &notices);
    tiku_rich_paragraph_t p = paragraph(s, 0, " a  B");
    tiku_rich_chars_t c;
    tiku_rich_para_t props;

    assert(tiku_richsnapshot_resolve(s, 0, p.run_count-1, &c, &props) == TIKU_RICH_OK);
    assert(c.bold && c.size == 16 && c.foreground == 0x44112233);
    assert(!strcmp(c.font, "Arial"));
    assert(props.align == TIKU_RICH_CENTER && props.left == 18);
    assert(notices & TIKU_RICHMARKUP_STYLE);
    tiku_richsnapshot_release(s);
}

/** @brief HTML export/import retains text whitespace and empty paragraphs. */
static void
html_whitespace(void)
{
    unsigned notices;
    tiku_richsnapshot_t *s = html("<p></p><p style='white-space:pre-wrap'>  a  b\n c </p>"
        "<p><br></p><p><br>start</p><p><br><br></p>", &notices);
    unsigned char *data;
    size_t length;
    tiku_richdoc_t *doc;
    tiku_richsnapshot_t *copy;

    paragraph(s, 0, ""); paragraph(s, 1, "  a  b\n c ");
    paragraph(s, 2, ""); paragraph(s, 3, "\nstart"); paragraph(s, 4, "\n");
    assert(tiku_richhtml_write(s, NULL, &data, &length, &notices) == TIKU_RICHIO_OK);
    assert(tiku_richmarkup_read_html(data, length, NULL, &doc, &notices) == TIKU_RICHIO_OK);
    assert(tiku_richdoc_snapshot(doc, &copy) == TIKU_RICH_OK);
    assert(tiku_richsnapshot_paragraphs(copy) == 5);
    paragraph(copy, 0, ""); paragraph(copy, 1, "  a  b\n c ");
    paragraph(copy, 2, ""); paragraph(copy, 3, "\nstart"); paragraph(copy, 4, "\n");
    free(data);
    tiku_richsnapshot_release(s); tiku_richsnapshot_release(copy);
    tiku_richdoc_release(doc);
}

/** @brief Cancellation after a chosen number of parser or builder polls. */
static int
cancel(void *context)
{
    int *remaining = context;

    return --*remaining <= 0;
}

/** @brief Failed imports clear outputs, including malformed grids and limits. */
static void
failures(void)
{
    static const char *const bad[] = {
        "<table><tr><td colspan='2'>X</td></tr></table>",
        "<table><tr><td>A<td>B<tr><td>C</table>",
        "<p>&#0;</p>", "<p>&#x110000;</p>", "<p a='unterminated>",
        "<p a='1' a='2'>x</p>", "<!DOCTYPE x [<!ENTITY x 'bad'>]><p>&x;</p>"
    };
    tiku_richdoc_t *doc;
    unsigned notices;

    for (size_t i = 0; i < sizeof bad / sizeof *bad; i++) {
        doc = (void *)1; notices = 123;
        assert(tiku_richmarkup_read_html(bad[i], strlen(bad[i]), NULL,
               &doc, &notices) != TIKU_RICHIO_OK);
        assert(!doc && !notices);
    }
    for (int i = 1; i <= 10; i++) {
        int remaining = i;
        tiku_richio_options_t options = {.cancel = cancel, .ctx = &remaining};

        assert(tiku_richmarkup_read_html("<p>one</p><p>two</p>", 20,
               &options, &doc, &notices) == TIKU_RICHIO_CANCELLED);
        assert(!doc && !notices);
    }
    tiku_richio_options_t options = {.memory_bytes = 64};

    assert(tiku_richmarkup_read_html("<p>x</p>", 8, &options, &doc, &notices) == TIKU_RICHIO_LIMIT);
    options = (tiku_richio_options_t){.document.text_bytes = 2};
    assert(tiku_richmarkup_read_html("<p>abc</p>", 10, &options, &doc, &notices) == TIKU_RICHIO_LIMIT);
    assert(!doc);
}

/** @brief Markdown imports are editable and export owns its result after teardown. */
static void
markdown(void)
{
    const char *input = "# Title\n\nHello **bold** and [link](https://example.org).\n\n"
                        "- first\n- second\n\n![camera](https://example.org/a.png)\n";
    tiku_richdoc_t *doc = NULL;
    tiku_richsnapshot_t *s = NULL;
    unsigned notices;
    unsigned char *output = NULL;
    size_t length = 0;
    char report[256];

    assert(tiku_richmarkup_read_markdown(input, strlen(input), NULL, &doc, &notices) == TIKU_RICHIO_OK);
    assert(notices & TIKU_RICHMARKUP_SYNTAX);
    assert(notices & TIKU_RICHMARKUP_IMAGES);
    assert(tiku_richdoc_snapshot(doc, &s) == TIKU_RICH_OK);
    paragraph(s, 0, "Title");
    paragraph(s, 1, "Hello bold and link.");
    assert(tiku_richmarkup_write_markdown(s, NULL, &output, &length, &notices) == TIKU_RICHIO_OK);
    assert(length == strlen((char *)output));
    assert(strstr((char *)output, "# ") && strstr((char *)output, "**bold**"));
    assert(strstr((char *)output, "[link](https://example.org)"));
    tiku_richmarkup_report(notices, report, sizeof report);
    assert(strstr(report, "Page layout"));
    tiku_richdoc_release(doc);
    tiku_richsnapshot_release(s);
    free(output);
}

/** @brief Full heading levels, numbered starts and cancellation survive conversion. */
static void
markdown_structure(void)
{
    const char *input = "\xef\xbb\xbf###### Small heading\n\n7. Seven\n8. Eight\n\n"
                        "| A | B |\n| --- | --- |\n| C | D |\n";
    tiku_richdoc_t *doc;
    tiku_richsnapshot_t *s;
    unsigned notices;

    assert(tiku_richmarkup_read_markdown(input, strlen(input), NULL, &doc, &notices) == TIKU_RICHIO_OK);
    assert(notices == TIKU_RICHMARKUP_SYNTAX);
    assert(tiku_richdoc_snapshot(doc, &s) == TIKU_RICH_OK);
    tiku_rich_paragraph_t p = paragraph(s, 0, "Small heading");
    tiku_rich_style_t style;
    int found = 0;

    for (size_t i = 0; i < tiku_richsnapshot_styles(s); i++) {
        tiku_richsnapshot_style(s, i, &style);
        if (style.id == p.style) { assert(!strcmp(style.name, "Heading 6")); found = 1; }
    }
    assert(found);
    p = paragraph(s, 1, "Seven"); assert(p.list.restart == 7);
    p = paragraph(s, 2, "Eight"); assert(p.list.restart == 8);
    tiku_rich_block_t table;

    tiku_richsnapshot_block(s, 3, &table);
    assert(table.kind == TIKU_RICH_BLOCK_TABLE && table.table.header_rows == 1);
    tiku_richsnapshot_release(s); tiku_richdoc_release(doc);
    for (int i = 1; i <= 30; i++) {
        int remaining = i;
        tiku_richio_options_t options = {.cancel = cancel, .ctx = &remaining};

        assert(tiku_richmarkup_read_markdown(input, strlen(input), &options, &doc, &notices) == TIKU_RICHIO_CANCELLED);
        assert(!doc && !notices);
    }
}

/** @brief Headings survive native and markup files independently of style names. */
static void
headings(void)
{
    unsigned notices;
    tiku_richsnapshot_t *snapshot = html("<h1>One</h1><h6>Six</h6><p>Body</p>",
                                          &notices);
    unsigned char *xml = NULL, *markdown = NULL;
    size_t length, markdown_length;
    tiku_richdoc_t *copy = NULL;
    tiku_richsnapshot_t *loaded;
    tiku_richoutline_t *outline;
    tiku_rich_heading_t heading;

    assert(tiku_richio_write_xml(snapshot, NULL, &xml, &length) == TIKU_RICHIO_OK);
    assert(strstr((char *)xml, "outline-levels"));
    assert(strstr((char *)xml, "outline=\"6\""));
    assert(!strstr((char *)xml, "page-breaks"));
    assert(tiku_richio_read_xml(xml, length, NULL, &copy) == TIKU_RICHIO_OK);
    assert(tiku_richdoc_snapshot(copy, &loaded) == TIKU_RICH_OK);
    assert(tiku_richoutline_create(loaded, &outline) == TIKU_RICH_OK);
    assert(tiku_richoutline_step(outline, 10) == TIKU_RICH_OK);
    assert(tiku_richoutline_count(outline) == 2);
    assert(tiku_richoutline_get(outline, 1, &heading) == TIKU_RICH_OK && heading.level == 6);
    assert(tiku_richmarkup_write_markdown(loaded, NULL, &markdown,
                                        &markdown_length, &notices) == TIKU_RICHIO_OK);
    assert(strstr((char *)markdown, "###### **Six**"));
    free(markdown);
    tiku_richoutline_release(outline);
    tiku_richsnapshot_release(loaded);
    tiku_richdoc_release(copy);
    char *value = strstr((char *)xml, "outline=\"6\"");

    value[9] = '7';
    assert(tiku_richio_read_xml(xml, length, NULL, &copy) != TIKU_RICHIO_OK && !copy);
    value[9] = '6';
    value = strstr((char *)xml, "outline-levels");
    memset(value, ' ', strlen("outline-levels"));
    assert(tiku_richio_read_xml(xml, length, NULL, &copy) != TIKU_RICHIO_OK && !copy);
    free(xml);
    tiku_richsnapshot_release(snapshot);
}

/** @brief Exported embedded PNGs import as owned images with display metadata. */
static void
pictures(void)
{
    const uint32_t pixels[] = {0xffff0000, 0x8000ff00, 0xff0000ff, 0};
    tiku_rich_image_t image = {2, 2, pixels, 48, 27, "Camera & image"};
    tiku_rich_run_source_t runs[] = {{.length = 1},
        {.length = 3, .image = &image, .link = "https://example.org"}, {.length = 1}};
    tiku_rich_paragraph_source_t paragraph_source = {
        .text = "A\xef\xbf\xbc" "B", .length = 5, .runs = runs, .run_count = 3};
    tiku_rich_source_t source = {.paragraphs = &paragraph_source, .paragraph_count = 1};
    tiku_richdoc_t *doc, *copy;
    tiku_richsnapshot_t *snapshot, *loaded;
    unsigned char *data;
    size_t length;
    unsigned notices;

    assert(tiku_richdoc_create(NULL, &doc) == TIKU_RICH_OK);
    assert(tiku_richdoc_import(doc, &source, 0) == TIKU_RICH_OK);
    assert(tiku_richdoc_snapshot(doc, &snapshot) == TIKU_RICH_OK);
    assert(tiku_richhtml_write(snapshot, NULL, &data, &length, &notices) == TIKU_RICHIO_OK);
    assert(tiku_richmarkup_read_html(data, length, NULL, &copy, &notices) == TIKU_RICHIO_OK);
    assert(!(notices & TIKU_RICHMARKUP_IMAGES));
    {
        char *encoded = strstr((char *)data, "data:image/png;base64,") + 22;
        char saved = *encoded;

        *encoded = '!';
        tiku_richdoc_t *bad = (void *)1;

        assert(tiku_richmarkup_read_html(data, length, NULL, &bad, &notices) ==
               TIKU_RICHIO_DAMAGED && !bad && !notices);
        *encoded = saved;
        char *end = strchr(encoded, '"');
        const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        char padding = end[-2];
        const char *code = strchr(alphabet, padding);

        assert(end[-1] == '=' && code && ((code - alphabet) & 3) == 0);
        end[-2] = code[1];
        assert(tiku_richmarkup_read_html(data, length, NULL, &bad, &notices) ==
               TIKU_RICHIO_DAMAGED && !bad && !notices);
        end[-2] = padding;
        for (int budget = 1; budget <= 60; budget++) {
            int remaining = budget;
            tiku_richio_options_t options = {.cancel = cancel, .ctx = &remaining};

            assert(tiku_richmarkup_read_html(data, length, &options, &bad, &notices) ==
                   TIKU_RICHIO_CANCELLED && !bad && !notices);
        }
    }
    free(data);
    tiku_richsnapshot_release(snapshot); tiku_richdoc_release(doc);
    assert(tiku_richdoc_snapshot(copy, &loaded) == TIKU_RICH_OK);
    tiku_rich_paragraph_t p = paragraph(loaded, 0, "A\xef\xbf\xbc" "B");

    assert(p.run_count == 3 && p.runs[1].image);
    assert(p.runs[1].image->pixel_width == 2 && p.runs[1].image->pixel_height == 2);
    assert(p.runs[1].image->width == 48 && p.runs[1].image->height == 27);
    assert(!strcmp(p.runs[1].image->alt, image.alt));
    assert(!strcmp(p.runs[1].link, runs[1].link));
    assert(!memcmp(p.runs[1].image->pixels, pixels, sizeof pixels));
    tiku_richsnapshot_release(loaded); tiku_richdoc_release(copy);
}

/** @brief Run markup import, conversion and refusal regressions. */
int
main(void)
{
    formatting(); structure(); css(); html_whitespace(); failures(); markdown();
    markdown_structure();
    headings();
    pictures();
    puts("richmarkup: formatting, structure, CSS, limits and Markdown passed");
    return 0;
}
