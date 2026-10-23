/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_richio_native.c - semantic round trips and malformed native packages.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richio.h"
#include "tiku_zip_write.h"
#include <dirent.h>
#include <locale.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "FAIL richio:%d: %s\n", __LINE__, #x); \
    exit(1); } } while (0)

/** @brief Build forward-inherited styles and paragraph feature fixtures. */
static tiku_richdoc_t *
fixture(void)
{
    static const char text[] = "A<&\"% \t\ncaf\xc3\xa9 \xef\xbf\xbe";
    tiku_rich_style_source_t styles[4] = {
        {.name = "Child % <\xef\xbf\xbf", .parent = 2,
         .chars = {.present = TIKU_RICH_BOLD, .bold = 0}},
        {.name = "Body", .chars = {.present = 511, .bold = 1, .italic = 1,
         .underline = 1, .strike = 1, .size = 12.125f, .font = "Sans <\"%",
         .foreground = 0x80abcdef, .highlight = 0x30778899, .baseline = 1},
         .paragraph = {.present = 15, .align = TIKU_RICH_JUSTIFY,
         .left = 10.25f, .right = 1.125f, .first = -2.5f, .before = 7,
         .after = 9, .keep_next = 1, .keep_lines = 1}},
        {.name = "Emphasis", .kind = TIKU_RICH_CHARACTER_STYLE, .parent = 4,
         .chars = {.present = TIKU_RICH_ITALIC, .italic = 0}},
        {.name = "Emphasis base", .kind = TIKU_RICH_CHARACTER_STYLE,
         .chars = {.present = TIKU_RICH_BOLD, .bold = 1}}
    };
    tiku_rich_run_source_t runs[2] = {
        {.length = 1, .style = 3, .reset = TIKU_RICH_BOLD,
         .link = "https://example.test/?a=1&b=%20"},
        {.length = sizeof text - 2, .style = 4,
         .chars = {.present = TIKU_RICH_BASELINE, .baseline = -1},
         .reset = TIKU_RICH_FOREGROUND, .link = "#\xef\xbf\xbf"}
    };
    tiku_rich_paragraph_source_t p[5] = {
        {.text = text, .length = sizeof text - 1, .runs = runs,
         .run_count = 2, .style = 1,
         .chars = {.present = TIKU_RICH_FONT, .font = "A\xef\xbf\xbf%B"}},
        {.chars = {.present = TIKU_RICH_ITALIC, .italic = 0}},
        {.text = "one", .length = 3,
         .list = {TIKU_RICH_LIST_DECIMAL, 8, UINT32_MAX}},
        {.text = "two", .length = 3,
         .list = {TIKU_RICH_LIST_DECIMAL, 8, 0}},
        {.text = "three", .length = 5,
         .list = {TIKU_RICH_LIST_BULLET, 2, 0}}
    };
    tiku_rich_source_t source = {.styles = styles, .style_count = 4,
        .paragraphs = p, .paragraph_count = 5};
    tiku_richdoc_t *doc;

    CHECK(tiku_richdoc_create(NULL, &doc) == TIKU_RICH_OK);
    CHECK(tiku_richdoc_import(doc, &source, 0) == TIKU_RICH_OK);
    return doc;
}

/** @brief Encode a model through its immutable public snapshot API. */
static unsigned char *
encode(tiku_richdoc_t *doc, size_t *length)
{
    tiku_richsnapshot_t *snapshot;
    unsigned char *bytes;

    CHECK(tiku_richdoc_snapshot(doc, &snapshot) == TIKU_RICH_OK);
    CHECK(tiku_richio_write_xml(snapshot, NULL, &bytes, length) ==
          TIKU_RICHIO_OK);
    tiku_richsnapshot_release(snapshot);
    CHECK(bytes[*length] == 0);
    return bytes;
}

/** @brief Compare canonical output and inspect semantic fields. */
static void
same(tiku_richdoc_t *a, tiku_richdoc_t *b)
{
    size_t na, nb;
    unsigned char *xa = encode(a, &na), *xb = encode(b, &nb);
    tiku_richsnapshot_t *snapshot;
    tiku_rich_paragraph_t p;
    tiku_rich_style_t style;
    tiku_rich_list_t list;
    uint64_t ordinal;

    CHECK(na == nb && !memcmp(xa, xb, na));
    CHECK(!tiku_richdoc_dirty(b));
    CHECK(tiku_richdoc_snapshot(b, &snapshot) == TIKU_RICH_OK);
    CHECK(tiku_richsnapshot_paragraphs(snapshot) == 5);
    CHECK(tiku_richsnapshot_styles(snapshot) == 4);
    CHECK(tiku_richsnapshot_paragraph(snapshot, 0, &p) == TIKU_RICH_OK);
    CHECK(p.run_count == 2 && p.runs[0].reset == TIKU_RICH_BOLD);
    CHECK(p.runs[1].chars.baseline == -1);
    CHECK(!strcmp(p.runs[0].link, "https://example.test/?a=1&b=%20"));
    CHECK(!strcmp(p.text, "A<&\"% \t\ncaf\xc3\xa9 \xef\xbf\xbe"));
    CHECK(!strcmp(p.chars.font, "A\xef\xbf\xbf%B"));
    CHECK(tiku_richsnapshot_style(snapshot, 0, &style) == TIKU_RICH_OK);
    CHECK(!strcmp(style.name, "Child % <\xef\xbf\xbf"));
    CHECK(style.chars.present == TIKU_RICH_BOLD && !style.chars.bold);
    CHECK(tiku_richsnapshot_style(snapshot, 1, &style) == TIKU_RICH_OK);
    CHECK(style.chars.present == 511 && style.chars.size == 12.125f);
    CHECK(style.paragraph.first == -2.5f && style.paragraph.keep_lines);
    CHECK(tiku_richsnapshot_list(snapshot, 3, &list, &ordinal) == TIKU_RICH_OK);
    CHECK(ordinal == (uint64_t)UINT32_MAX + 1);
    tiku_richsnapshot_release(snapshot);
    free(xa);
    free(xb);
}

/** @brief Change one XML token without modifying the valid source fixture. */
static void
reject(const unsigned char *xml, size_t length, const char *from,
       const char *to, tiku_richio_status_t expected)
{
    const char *at = strstr((const char *)xml, from);
    size_t before, after, n;
    unsigned char *changed;
    tiku_richdoc_t *doc = (void *)1;
    tiku_richio_status_t status;

    CHECK(at);
    before = (size_t)(at - (const char *)xml);
    after = before + strlen(from);
    n = length - strlen(from) + strlen(to);
    changed = malloc(n + 1);
    CHECK(changed);
    memcpy(changed, xml, before);
    memcpy(changed + before, to, strlen(to));
    memcpy(changed + before + strlen(to), xml + after, length - after + 1);
    status = tiku_richio_read_xml(changed, n, NULL, &doc);
    if (status != expected)
        fprintf(stderr, "%s -> %s: expected %d, got %d\n",
                from, to, expected, status);
    CHECK(status == expected && !doc);
    free(changed);
}

/** @brief Refuse version changes, malformed references and semantic loss. */
static void
malformed(const unsigned char *xml, size_t length)
{
    tiku_richdoc_t *doc;
    size_t i;

    reject(xml, length, "version=\"1\"", "version=\"2\"",
           TIKU_RICHIO_UNSUPPORTED);
    reject(xml, length, "styles lists", "styles future",
           TIKU_RICHIO_UNSUPPORTED);
    reject(xml, length, "styles lists", "styles lists tables",
           TIKU_RICHIO_DAMAGED);
    reject(xml, length, "<body>", "<body future=\"1\">",
           TIKU_RICHIO_UNSUPPORTED);
    reject(xml, length, "<body>", "<body xmlns=\"future\">",
           TIKU_RICHIO_UNSUPPORTED);
    reject(xml, length, "<body>", "<body><image/>", TIKU_RICHIO_DAMAGED);
    reject(xml, length, "id=\"2\"", "id=\"1\"", TIKU_RICHIO_DAMAGED);
    reject(xml, length, "parent=\"2\"", "parent=\"1\"", TIKU_RICHIO_DAMAGED);
    reject(xml, length, "style=\"3\"", "style=\"2\"", TIKU_RICHIO_DAMAGED);
    reject(xml, length, "bytes=\"1\"", "bytes=\"99999\"", TIKU_RICHIO_DAMAGED);
    reject(xml, length, "mask=\"511\"", "mask=\"512\"", TIKU_RICHIO_DAMAGED);
    reject(xml, length, "size-f32=\"41420000\"", "size-f32=\"7fc00000\"",
           TIKU_RICHIO_DAMAGED);
    reject(xml, length, "size-f32=\"41420000\"", "size-f32=\"7f800000\"",
           TIKU_RICHIO_DAMAGED);
    reject(xml, length, "bold=\"0\"", "bold=\"9\"", TIKU_RICHIO_DAMAGED);
    reject(xml, length, "baseline=\"1\"", "baseline=\"-1\"",
           TIKU_RICHIO_DAMAGED);
    reject(xml, length, "https://", "javascript:", TIKU_RICHIO_DAMAGED);
    reject(xml, length, "A&lt;", "&#0;&lt;", TIKU_RICHIO_DAMAGED);
    reject(xml, length, "A&lt;", "&#x110000;&lt;", TIKU_RICHIO_DAMAGED);
    reject(xml, length, "A&lt;", "&#xD800;&lt;", TIKU_RICHIO_DAMAGED);
    reject(xml, length, "A&lt;", "&unknown;&lt;", TIKU_RICHIO_DAMAGED);
    reject(xml, length, "A&lt;", "%00&lt;", TIKU_RICHIO_DAMAGED);
    reject(xml, length, "A&lt;", "%xy&lt;", TIKU_RICHIO_DAMAGED);
    reject(xml, length, "A&lt;", "\xc0\x80&lt;", TIKU_RICHIO_DAMAGED);
    reject(xml, length, "<body>", "<!DOCTYPE body><body>",
           TIKU_RICHIO_UNSUPPORTED);
    reject(xml, length, "UTF-8", "latin1", TIKU_RICHIO_UNSUPPORTED);
    for (i = 1; i + 1 < length; i++) {
        doc = (void *)1;
        CHECK(tiku_richio_read_xml(xml, i, NULL, &doc) != TIKU_RICHIO_OK);
        CHECK(!doc);
    }
}

/** @brief A cancellation counter fails one chosen poll. */
typedef struct { size_t calls, stop; } cancel_t;

/** @brief Stop at a selected conversion checkpoint. */
static int
cancel(void *ctx)
{
    cancel_t *state = ctx;

    return ++state->calls == state->stop;
}

/** @brief Cover limits and cancellation at every successful conversion poll. */
static void
limits_and_cancel(tiku_richdoc_t *source, const unsigned char *xml, size_t n)
{
    tiku_richio_options_t opt = {0};
    tiku_richdoc_t *doc;
    tiku_richsnapshot_t *snapshot;
    unsigned char *encoded;
    size_t length, count, i;
    cancel_t state = {0};

    opt.xml_bytes = n - 1;
    CHECK(tiku_richio_read_xml(xml, n, &opt, &doc) == TIKU_RICHIO_LIMIT);
    opt.xml_bytes = 0;
    opt.memory_bytes = 4096;
    CHECK(tiku_richio_read_xml(xml, n, &opt, &doc) == TIKU_RICHIO_LIMIT);
    opt.memory_bytes = 0;
    opt.document.paragraphs = 1;
    CHECK(tiku_richio_read_xml(xml, n, &opt, &doc) == TIKU_RICHIO_LIMIT);
    opt.document.paragraphs = 0;
    opt.cancel = cancel;
    opt.ctx = &state;
    CHECK(tiku_richio_read_xml(xml, n, &opt, &doc) == TIKU_RICHIO_OK);
    count = state.calls;
    tiku_richdoc_release(doc);
    for (i = 1; i <= count; i++) {
        state = (cancel_t){0, i};
        CHECK(tiku_richio_read_xml(xml, n, &opt, &doc) ==
              TIKU_RICHIO_CANCELLED);
        CHECK(!doc);
    }
    CHECK(tiku_richdoc_snapshot(source, &snapshot) == TIKU_RICH_OK);
    state = (cancel_t){0};
    CHECK(tiku_richio_write_xml(snapshot, &opt, &encoded, &length) ==
          TIKU_RICHIO_OK);
    count = state.calls;
    free(encoded);
    for (i = 1; i <= count; i++) {
        state = (cancel_t){0, i};
        CHECK(tiku_richio_write_xml(snapshot, &opt, &encoded, &length) ==
              TIKU_RICHIO_CANCELLED);
        CHECK(!encoded && !length);
    }
    opt.cancel = NULL;
    opt.xml_bytes = n - 1;
    CHECK(tiku_richio_write_xml(snapshot, &opt, &encoded, &length) ==
          TIKU_RICHIO_LIMIT);
    CHECK(!encoded && !length);
    tiku_richsnapshot_release(snapshot);
}

/** @brief Write a test-owned package without invoking native conversion. */
static void
package(const char *path, const tiku_zip_entry_t *entries, size_t count)
{
    FILE *out = fopen(path, "wb");

    CHECK(out && tiku_zip_write(out, entries, count) == TIKU_ARCHIVE_OK);
    CHECK(!fclose(out));
}

/** @brief Save/read a package and refuse damaged headers and extra parts. */
static void
files(tiku_richdoc_t *source, const unsigned char *xml, size_t length)
{
    char dir[] = "/tmp/tiku-richio-XXXXXX", path[256], link[256];
    tiku_richsnapshot_t *snapshot;
    tiku_richdoc_t *loaded;
    tiku_zip_entry_t entries[2] = {{"document.xml", xml, length},
        {"media/picture.bin", "x", 1}};
    FILE *f;
    tiku_richio_options_t opt = {0};
    struct stat st;
    cancel_t state = {0};
    size_t i, polls;

    CHECK(mkdtemp(dir));
    snprintf(path, sizeof path, "%s/document.tkrd", dir);
    snprintf(link, sizeof link, "%s/alias.tkrd", dir);
    CHECK(tiku_richdoc_snapshot(source, &snapshot) == TIKU_RICH_OK);
    CHECK(tiku_richio_save(path, snapshot, NULL) == TIKU_RICHIO_OK);
    CHECK(!chmod(path, 0640));
    CHECK(tiku_richio_load(path, NULL, &loaded) == TIKU_RICHIO_OK);
    same(source, loaded);
    tiku_richdoc_release(loaded);
    CHECK(!symlink(path, link));
    CHECK(tiku_richio_save(link, snapshot, NULL) == TIKU_RICHIO_IO);
    opt.cancel = cancel;
    opt.ctx = &state;
    CHECK(tiku_richio_save(path, snapshot, &opt) == TIKU_RICHIO_OK);
    polls = state.calls;
    for (i = 1; i <= polls; i++) {
        package(path, entries + 1, 1);
        state = (cancel_t){0, i};
        CHECK(tiku_richio_save(path, snapshot, &opt) == TIKU_RICHIO_CANCELLED);
        CHECK(tiku_richio_load(path, NULL, &loaded) == TIKU_RICHIO_DAMAGED);
    }
    CHECK(!stat(path, &st) && (st.st_mode & 0777) == 0640);
    package(path, entries, 2);
    CHECK(tiku_richio_load(path, NULL, &loaded) == TIKU_RICHIO_UNSUPPORTED);
    package(path, entries, 1);
    f = fopen(path, "r+b");
    CHECK(f && !fseek(f, 30, SEEK_SET) && fputc('D', f) != EOF && !fclose(f));
    CHECK(tiku_richio_load(path, NULL, &loaded) == TIKU_RICHIO_DAMAGED);
    package(path, entries, 1);
    f = fopen(path, "r+b");
    CHECK(f && !fseek(f, 60, SEEK_SET) && fputc('!', f) != EOF && !fclose(f));
    CHECK(tiku_richio_load(path, NULL, &loaded) == TIKU_RICHIO_DAMAGED);
    CHECK(!unlink(path));
    CHECK(tiku_richio_load(path, NULL, &loaded) == TIKU_RICHIO_IO && !loaded);
    CHECK(!unlink(link));
    CHECK(!rmdir(dir));
    tiku_richsnapshot_release(snapshot);
}

/** @brief Import long text independently of bounded interactive transactions. */
static void
plain_text(void)
{
    tiku_richdoc_t *doc = NULL;
    tiku_richsnapshot_t *snapshot;
    tiku_rich_paragraph_t p;
    tiku_richio_options_t options = {0};
    char text[12003];

    memcpy(text, "\xef\xbb\xbf", 3);
    for (size_t i = 0; i < 2000; i++)
        memcpy(text + 3 + i * 6, "a\r\nb\r\n", 6);
    CHECK(tiku_richio_read_text(text, sizeof text, NULL, &doc) ==
          TIKU_RICHIO_OK);
    CHECK(!tiku_richdoc_dirty(doc));
    CHECK(tiku_richdoc_snapshot(doc, &snapshot) == TIKU_RICH_OK);
    CHECK(tiku_richsnapshot_paragraphs(snapshot) == 4001);
    CHECK(tiku_richsnapshot_paragraph(snapshot, 0, &p) == TIKU_RICH_OK);
    CHECK(p.length == 1 && *p.text == 'a');
    CHECK(tiku_richsnapshot_paragraph(snapshot, 4000, &p) == TIKU_RICH_OK);
    CHECK(!p.length);
    tiku_richsnapshot_release(snapshot);
    tiku_richdoc_release(doc);
    options.document.paragraphs = 4000;
    CHECK(tiku_richio_read_text(text, sizeof text, &options, &doc) ==
          TIKU_RICHIO_LIMIT && !doc);
    options.document.paragraphs = 0;
    options.memory_bytes = 100;
    CHECK(tiku_richio_read_text(text, sizeof text, &options, &doc) ==
          TIKU_RICHIO_LIMIT && !doc);
    CHECK(tiku_richio_read_text("a\0b", 3, NULL, &doc) ==
          TIKU_RICHIO_DAMAGED && !doc);
    CHECK(tiku_richio_read_text("\xc0\x80", 2, NULL, &doc) ==
          TIKU_RICHIO_DAMAGED && !doc);
    CHECK(tiku_richio_read_text(NULL, 0, NULL, &doc) == TIKU_RICHIO_OK);
    tiku_richdoc_release(doc);
}

/** @brief Run native semantic, refusal and atomic-file fixtures. */
int
main(void)
{
    tiku_richdoc_t *doc = fixture(), *loaded;
    unsigned char *xml;
    size_t length;
    const char *locales[] = {"C", "de_DE.UTF-8", "fr_FR.UTF-8", "C.UTF-8"};
    size_t i;

    xml = encode(doc, &length);
    CHECK(strstr((char *)xml, "%EF%BF%BE") && strstr((char *)xml, "%25"));
    for (i = 0; i < sizeof locales / sizeof locales[0]; i++) {
        if (!setlocale(LC_NUMERIC, locales[i])) continue;
        CHECK(tiku_richio_read_xml(xml, length, NULL, &loaded) ==
              TIKU_RICHIO_OK);
        same(doc, loaded);
        tiku_richdoc_release(loaded);
    }
    CHECK(setlocale(LC_NUMERIC, "C"));
    malformed(xml, length);
    limits_and_cancel(doc, xml, length);
    files(doc, xml, length);
    plain_text();
    free(xml);
    tiku_richdoc_release(doc);
    puts("Rich native I/O: exact styles/text/lists/links, schema refusals, "
         "truncation, cancellation, budgets and atomic files PASS");
    return 0;
}
