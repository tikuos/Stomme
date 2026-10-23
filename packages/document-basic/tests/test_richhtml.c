/*
 * TikuDesktop Translation Kit tests.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_richhtml.c - HTML structure, escaping, bounds and cancellation.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richhtml.h"
#include "tiku_richmarkup.h"
#include <assert.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** @brief Build nested tables, list transitions and adversarial text/styles. */
static tiku_richdoc_t *
fixture(void)
{
    static const uint32_t pixels[] = {
        0xff0088aa, 0x80123456, 0x00112233, 0xffffffff};
    const tiku_rich_image_t image = {2, 2, pixels, 40, 30,
                                    "<photo & \"alt\">"};
    const char text[] = "A<&%\"'\té\nZ";
    tiku_rich_run_source_t run = {.length = sizeof text - 1,
        .link = "https://example.org/?a=1&b=%20\"q",
        .reset = TIKU_RICH_BOLD,
        .chars = {.present = TIKU_RICH_FONT | TIKU_RICH_SIZE |
            TIKU_RICH_FOREGROUND | TIKU_RICH_UNDERLINE,
            .font = "A\";}</style><script>", .size = 13.125,
            .foreground = 0x80123456, .underline = 1}};
    tiku_rich_run_source_t picture = {.length = 3, .image = &image};
    tiku_rich_style_source_t style = {.name = "Title",
        .chars = {.present = TIKU_RICH_BOLD, .bold = 1}};
    tiku_rich_paragraph_source_t p[13] = {
        {.text = text, .length = sizeof text - 1, .runs = &run, .run_count = 1,
         .style = 1, .paragraph = {.present = TIKU_RICH_INDENT |
            TIKU_RICH_PAGE_BREAK, .left = 18, .first = -9, .break_before = 1}},
        {.text = "four", .length = 4, .list = {TIKU_RICH_LIST_DECIMAL, 0, 4}},
        {.text = "child", .length = 5, .list = {TIKU_RICH_LIST_DECIMAL, 1, 0}},
        {.text = "five", .length = 4, .list = {TIKU_RICH_LIST_DECIMAL, 0, 0}},
        {.text = "bullet", .length = 6, .list = {TIKU_RICH_LIST_BULLET, 0, 0}},
        {.text = "one", .length = 3, .list = {TIKU_RICH_LIST_DECIMAL, 0, 0}},
        {.text = "plain", .length = 5},
        {.text = "deep", .length = 4, .list = {TIKU_RICH_LIST_DECIMAL, 8, 12}},
        {.chars = {.present = TIKU_RICH_SIZE, .size = 30}},
        {.text = "left", .length = 4}, {.text = "right", .length = 5},
        {.text = "\xef\xbf\xbc", .length = 3, .runs = &picture, .run_count = 1},
        {.text = "tail", .length = 4}
    };
    tiku_rich_block_source_t b[18] = {0};
    tiku_rich_source_t source = {.paragraphs = p, .paragraph_count = 13,
        .styles = &style, .style_count = 1, .blocks = b, .block_count = 18};
    tiku_richdoc_t *doc;

    for (size_t i = 0; i < 9; i++) b[i].paragraph = i;
    b[9] = (tiku_rich_block_source_t){.kind = TIKU_RICH_BLOCK_TABLE,
        .table = {1, 2, 240, 3, 1, 0xff112233, {1, 3}, 1}};
    b[10] = (tiku_rich_block_source_t){.kind = TIKU_RICH_BLOCK_CELL,
        .parent = 10, .background = 0x80445566};
    b[11] = (tiku_rich_block_source_t){.parent = 11, .paragraph = 9};
    b[12] = (tiku_rich_block_source_t){.kind = TIKU_RICH_BLOCK_CELL,
        .parent = 10};
    b[13] = (tiku_rich_block_source_t){.parent = 13, .paragraph = 10};
    b[14] = (tiku_rich_block_source_t){.kind = TIKU_RICH_BLOCK_TABLE,
        .parent = 13, .table = {1, 1, 0, 0, 0, 0, {0}, 0}};
    b[15] = (tiku_rich_block_source_t){.kind = TIKU_RICH_BLOCK_CELL,
        .parent = 15};
    b[16] = (tiku_rich_block_source_t){.parent = 16, .paragraph = 11};
    b[17] = (tiku_rich_block_source_t){.paragraph = 12};
    assert(tiku_richdoc_create(NULL, &doc) == TIKU_RICH_OK);
    assert(tiku_richdoc_import(doc, &source, 0) == TIKU_RICH_OK);
    return doc;
}

/** @brief Count cancellation polls or refuse the selected poll. */
static int
cancel(void *context)
{
    size_t *counts = context;

    return ++counts[0] == counts[1];
}

/** @brief Refusals clear caller outputs and do not change the snapshot. */
static void
limits(const tiku_richsnapshot_t *snapshot, size_t bytes)
{
    size_t polls[2] = {0}, length, total;
    unsigned char *data;
    unsigned notices;
    tiku_richio_options_t options = {.xml_bytes = bytes - 1};

    assert(tiku_richhtml_write(snapshot, &options, &data, &length, &notices) ==
        TIKU_RICHIO_LIMIT);
    assert(!data && !length && !notices);
    options.xml_bytes++;
    assert(tiku_richhtml_write(snapshot, &options, &data, &length, &notices) ==
        TIKU_RICHIO_OK && length == bytes);
    free(data);
    options.memory_bytes = (bytes + 1) * 2 - 1;
    assert(tiku_richhtml_write(snapshot, &options, &data, &length, &notices) ==
        TIKU_RICHIO_LIMIT && !data && !length && !notices);
    options.memory_bytes++;
    options.cancel = cancel;
    options.ctx = polls;
    assert(tiku_richhtml_write(snapshot, &options, &data, &length, NULL) ==
        TIKU_RICHIO_OK);
    free(data);
    total = polls[0];
    for (size_t i = 1; i <= total; i++) {
        polls[0] = 0; polls[1] = i;
        data = (void *)1; length = notices = 123;
        assert(tiku_richhtml_write(snapshot, &options, &data, &length,
            &notices) == TIKU_RICHIO_CANCELLED);
        assert(!data && !length && !notices);
    }
    printf("HTML: %zu cancellation points PASS\n", total);
    assert(tiku_richhtml_write(NULL, NULL, &data, &length, &notices) ==
        TIKU_RICHIO_INVALID && !data && !length && !notices);
}

/** @brief Export differently shaped pictures spanning base64/block boundaries. */
static void
pictures(const char *path)
{
    static const unsigned widths[] = {1, 2, 3, 7, 1024, 1, 4095};
    static const unsigned heights[] = {1, 1, 1, 9, 1, 4096, 3};
    tiku_rich_paragraph_source_t p[7] = {0};
    tiku_rich_run_source_t runs[7] = {0};
    tiku_rich_image_t images[7];
    uint32_t *pixels = malloc(12285 * sizeof *pixels);
    tiku_richdoc_t *doc;
    tiku_richsnapshot_t *snapshot;
    tiku_rich_source_t source = {.paragraphs = p, .paragraph_count = 7};
    unsigned char *html;
    size_t length;
    unsigned notices;

    assert(pixels);
    for (size_t i = 0; i < 12285; i++) pixels[i] = (uint32_t)i * 0x739ab123u;
    for (size_t i = 0; i < 7; i++) {
        images[i] = (tiku_rich_image_t){widths[i], heights[i], pixels,
            80, 60, "Picture"};
        runs[i] = (tiku_rich_run_source_t){.length = 3, .image = &images[i],
                                         .link = "https://example.org/picture"};
        p[i] = (tiku_rich_paragraph_source_t){.text = "\xef\xbf\xbc",
            .length = 3, .runs = &runs[i], .run_count = 1};
    }
    assert(tiku_richdoc_create(NULL, &doc) == TIKU_RICH_OK);
    assert(tiku_richdoc_import(doc, &source, 0) == TIKU_RICH_OK);
    free(pixels);
    assert(tiku_richdoc_snapshot(doc, &snapshot) == TIKU_RICH_OK);
    tiku_richdoc_release(doc);
    assert(tiku_richhtml_write(snapshot, NULL, &html, &length, &notices) ==
           TIKU_RICHIO_OK && notices == TIKU_RICHHTML_PAGINATION);
    limits(snapshot, length);
    {
        tiku_richsnapshot_t *loaded;

        assert(tiku_richmarkup_read_html(html, length, NULL, &doc, &notices) ==
               TIKU_RICHIO_OK && !(notices & TIKU_RICHMARKUP_IMAGES));
        assert(tiku_richdoc_snapshot(doc, &loaded) == TIKU_RICH_OK);
        tiku_richdoc_release(doc);
        for (size_t i = 0; i < 7; i++) {
            tiku_rich_paragraph_t p;

            assert(tiku_richsnapshot_paragraph(loaded, i, &p) == TIKU_RICH_OK);
            assert(p.length == 3 && p.run_count == 1 && p.runs[0].image);
            const tiku_rich_image_t *image = p.runs[0].image;

            assert(image->pixel_width == widths[i] && image->pixel_height == heights[i]);
            assert(image->width == 80 && image->height == 60);
            for (size_t n = 0; n < (size_t)widths[i] * heights[i]; n++)
                assert(image->pixels[n] == (uint32_t)n * 0x739ab123u);
        }
        tiku_richsnapshot_release(loaded);
    }
    if (path) {
        FILE *file = fopen(path, "wb");

        assert(file && fwrite(html, 1, length, file) == length);
        assert(!fclose(file));
    }
    free(html);
    tiku_richsnapshot_release(snapshot);
}

/** @brief Check output and optionally write an independent-parser fixture. */
int
main(int argc, char **argv)
{
    tiku_richdoc_t *doc = fixture();
    tiku_richsnapshot_t *snapshot;
    unsigned char *data, *other;
    size_t length, n;
    unsigned notices;

    assert(tiku_richdoc_snapshot(doc, &snapshot) == TIKU_RICH_OK);
    tiku_richdoc_release(doc);
    assert(tiku_richhtml_write(snapshot, NULL, &data, &length, &notices) ==
        TIKU_RICHIO_OK);
    assert(strlen((char *)data) == length);
    assert(notices == (TIKU_RICHHTML_PAGINATION | TIKU_RICHHTML_STYLES));
    assert(strstr((char *)data, "A&lt;&amp;%&quot;&#39;\té\nZ"));
    assert(!strstr((char *)data, "<script>"));
    assert(strstr((char *)data, "a=1&amp;b=%20&quot;q"));
    assert(strstr((char *)data, "font-weight:normal;"));
    assert(strstr((char *)data, "font-size:13.125pt;"));
    assert(strstr((char *)data, "#12345680"));
    assert(strstr((char *)data, "\\22 \\3b \\7d \\3c "));
    assert(strstr((char *)data, "text-indent:-9.000pt;break-before:page;"));
    assert(strstr((char *)data, "<li value=\"4\">"));
    assert(strstr((char *)data, "<li value=\"5\">"));
    assert(strstr((char *)data, "<li value=\"12\">"));
    assert(strstr((char *)data, "width:25.000%"));
    assert(strstr((char *)data, "width:75.000%"));
    assert(strstr((char *)data, "<thead>"));
    assert(strstr((char *)data, "<tbody>"));
    assert(strstr((char *)data, "background-color:#44556680"));
    assert(strstr((char *)data, "alt=\"&lt;photo &amp; &quot;alt&quot;&gt;\""));
    assert(strstr((char *)data, "width:40.000pt;height:30.000pt"));
    assert(strstr((char *)data, "src=\"data:image/png;base64,"));
    assert(!strstr((char *)data, "[Image:"));
    assert(strstr((char *)data, "font-size:30.000pt;"));
    limits(snapshot, length);
    setlocale(LC_NUMERIC, "de_DE.UTF-8");
    assert(tiku_richhtml_write(snapshot, NULL, &other, &n, NULL) ==
        TIKU_RICHIO_OK && n == length && !memcmp(data, other, n));
    free(other);
    if (argc >= 2) {
        FILE *file = fopen(argv[1], "wb");

        assert(file && fwrite(data, 1, length, file) == length);
        assert(!fclose(file));
    }
    free(data);
    tiku_richsnapshot_release(snapshot);
    pictures(argc >= 3 ? argv[2] : NULL);
    puts("HTML: escaping, styles, lists, nested tables, notices and limits PASS");
    return 0;
}
