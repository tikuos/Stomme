/*
 * TikuDesktop Translation Kit tests.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_richclip.c - fragment wire semantics, ownership and hostile input.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richclip.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

/** @brief Build a selection with inheritance, links and an owned picture. */
static tiku_richfragment_t *
fixture(void)
{
    const uint32_t pixels[] = {0x80010203, 0xffaabbcc};
    const tiku_rich_image_t image = {2, 1, pixels, 24, 12, "Picture <&>"};
    tiku_rich_style_source_t styles[] = {
        {.name = "Unselected secret"},
        {.name = "Body", .parent = 3,
         .paragraph = {.present = TIKU_RICH_ALIGNMENT, .align = TIKU_RICH_CENTER}},
        {.name = "Base", .chars = {.present = TIKU_RICH_SIZE, .size = 15}},
        {.name = "Emphasis", .kind = TIKU_RICH_CHARACTER_STYLE,
         .chars = {.present = TIKU_RICH_BOLD, .bold = 1}}
    };
    tiku_rich_run_source_t runs[] = {
        {.length = 4, .style = 4, .link = "https://example.test/?a=1&b=2"},
        {.length = 3, .image = &image}, {.length = 1}
    };
    tiku_rich_paragraph_source_t paragraphs[] = {
        {.text = "DO NOT COPY", .length = 11, .style = 1},
        {.text = "xB\xc3\xa9\xef\xbf\xbcy", .length = 8, .style = 2,
         .runs = runs, .run_count = 3},
        {.text = "OKtail", .length = 6, .style = 2,
         .list = {TIKU_RICH_LIST_BULLET, 2, 0}}
    };
    tiku_rich_source_t source = {.styles = styles, .style_count = 4,
        .paragraphs = paragraphs, .paragraph_count = 3};
    tiku_richdoc_t *doc;
    tiku_richsnapshot_t *snapshot;
    tiku_richfragment_t *fragment;
    tiku_rich_paragraph_t first, last;

    assert(tiku_richdoc_create(NULL, &doc) == TIKU_RICH_OK);
    assert(tiku_richdoc_import(doc, &source, 0) == TIKU_RICH_OK);
    assert(tiku_richdoc_snapshot(doc, &snapshot) == TIKU_RICH_OK);
    assert(tiku_richsnapshot_paragraph(snapshot, 1, &first) == TIKU_RICH_OK);
    assert(tiku_richsnapshot_paragraph(snapshot, 2, &last) == TIKU_RICH_OK);
    assert(tiku_richfragment_create(snapshot,
        (tiku_rich_pos_t){tiku_richsnapshot_body(snapshot),
            first.id, 1, TIKU_RICH_BEFORE},
        (tiku_rich_pos_t){tiku_richsnapshot_body(snapshot),
            last.id, 2, TIKU_RICH_AFTER}, &fragment) ==
        TIKU_RICH_OK);
    tiku_richsnapshot_release(snapshot);
    tiku_richdoc_release(doc);
    return fragment;
}

/** @brief Check actual text, style resolution and image pixels independently. */
static void
content(const tiku_richsnapshot_t *snapshot)
{
    tiku_rich_paragraph_t p;
    tiku_rich_chars_t chars;
    tiku_rich_para_t props;

    assert(tiku_richsnapshot_paragraphs(snapshot) == 2);
    assert(tiku_richsnapshot_styles(snapshot) == 3);
    assert(tiku_richsnapshot_paragraph(snapshot, 0, &p) == TIKU_RICH_OK);
    assert(p.length == 7 && !strcmp(p.text, "B\xc3\xa9\xef\xbf\xbcy"));
    assert(p.run_count == 3 && p.runs[0].length == 3);
    assert(p.runs[0].style && p.runs[0].link);
    assert(!strcmp(p.runs[0].link, "https://example.test/?a=1&b=2"));
    assert(p.runs[1].image && p.runs[1].length == 3);
    assert(p.runs[1].image->pixels[0] == 0x80010203);
    assert(p.runs[1].image->pixels[1] == 0xffaabbcc);
    assert(p.runs[1].image->pixel_width == 2);
    assert(p.runs[1].image->pixel_height == 1);
    assert(p.runs[1].image->width == 24 && p.runs[1].image->height == 12);
    assert(!strcmp(p.runs[1].image->alt, "Picture <&>"));
    assert(tiku_richsnapshot_resolve(snapshot, 0, 0, &chars, &props) ==
        TIKU_RICH_OK);
    assert(chars.bold && chars.size == 15 && props.align == TIKU_RICH_CENTER);
    assert(tiku_richsnapshot_paragraph(snapshot, 1, &p) == TIKU_RICH_OK);
    assert(p.length == 2 && !strcmp(p.text, "OK"));
    assert(p.list.kind == TIKU_RICH_LIST_BULLET && p.list.level == 2);
}

/** @brief Paste through public APIs and verify one-step history and ownership. */
static void
paste(tiku_richfragment_t *fragment)
{
    tiku_richdoc_t *doc;
    tiku_richsnapshot_t *snapshot;
    tiku_rich_paragraph_t p;
    tiku_rich_pos_t at;

    assert(tiku_richdoc_create(NULL, &doc) == TIKU_RICH_OK);
    assert(tiku_richdoc_snapshot(doc, &snapshot) == TIKU_RICH_OK);
    assert(tiku_richsnapshot_paragraph(snapshot, 0, &p) == TIKU_RICH_OK);
    at = (tiku_rich_pos_t){tiku_richsnapshot_body(snapshot),
        p.id, 0, TIKU_RICH_BEFORE};
    tiku_richsnapshot_release(snapshot);
    assert(tiku_richdoc_paste_fragment(doc, 0, at, at, fragment) == TIKU_RICH_OK);
    tiku_richfragment_release(fragment);
    assert(tiku_richdoc_snapshot(doc, &snapshot) == TIKU_RICH_OK);
    content(snapshot);
    assert(tiku_richdoc_undo(doc) == TIKU_RICH_OK);
    assert(!tiku_richdoc_can_undo(doc));
    assert(tiku_richdoc_redo(doc) == TIKU_RICH_OK);
    tiku_richdoc_release(doc);
    content(snapshot);
    tiku_richsnapshot_release(snapshot);
}

/** @brief Require a specific refusal and a cleared output pointer. */
static void
refused(const void *data, size_t length, const tiku_richio_options_t *options,
    tiku_richio_status_t expected)
{
    tiku_richfragment_t *fragment = (void *)1;

    assert(tiku_richclip_read(data, length, options, &fragment) == expected);
    assert(!fragment);
}

/** @brief Frame independently supplied native XML for adversarial cases. */
static unsigned char *
frame(const unsigned char *xml, size_t size)
{
    unsigned char *data = malloc(size + 17);

    assert(data);
    memcpy(data, "TIKURF\0\1", 8);
    for (unsigned i = 0; i < 8; i++) data[8 + i] = (uint64_t)size >> (8 * i);
    memcpy(data + 16, xml, size);
    data[size + 16] = 0;
    return data;
}

/** @brief Reject altered schema tokens without changing the original bytes. */
static void
changed(const unsigned char *data, size_t length, const char *from,
    const char *to, tiku_richio_status_t expected)
{
    char *xml = malloc(length), *at;
    unsigned char *packet;
    size_t before, after, n;

    assert(xml);
    memcpy(xml, data + 16, length - 16);
    xml[length - 16] = 0;
    at = strstr(xml, from);
    assert(at);
    before = (size_t)(at - xml);
    after = before + strlen(from);
    n = length - 16 - strlen(from) + strlen(to);
    {
        char *copy = malloc(n + 1);

        assert(copy);
        memcpy(copy, xml, before);
        memcpy(copy + before, to, strlen(to));
        memcpy(copy + before + strlen(to), xml + after, length - 16 - after);
        packet = frame((unsigned char *)copy, n);
        free(copy);
    }
    refused(packet, n + 16, NULL, expected);
    free(packet);
    free(xml);
}

/** @brief Exercise all truncations, framing mismatches and hostile XML. */
static void
hostile(const unsigned char *data, size_t length)
{
    unsigned char *copy = malloc(length + 1);

    assert(copy);
    refused(NULL, length, NULL, TIKU_RICHIO_INVALID);
    refused(data, SIZE_MAX, NULL, TIKU_RICHIO_LIMIT);
    refused(data, TIKU_RICHCLIP_LIMIT + 1, NULL, TIKU_RICHIO_LIMIT);
    for (size_t i = 0; i < length; i++)
        refused(data, i, NULL, TIKU_RICHIO_DAMAGED);
    memcpy(copy, data, length);
    copy[length] = 0;
    refused(copy, length + 1, NULL, TIKU_RICHIO_DAMAGED);
    for (unsigned i = 0; i < 16; i++) {
        copy[i] ^= 0xff;
        refused(copy, length, NULL,
            i == 7 ? TIKU_RICHIO_UNSUPPORTED : TIKU_RICHIO_DAMAGED);
        copy[i] ^= 0xff;
    }
    memset(copy + 8, 0, 8);
    refused(copy, length, NULL, TIKU_RICHIO_DAMAGED);
    memcpy(copy, data, length);
    copy[length - 1] = 0;
    refused(copy, length, NULL, TIKU_RICHIO_DAMAGED);
    changed(data, length, "version=\"1\"", "version=\"9\"",
        TIKU_RICHIO_UNSUPPORTED);
    changed(data, length, "styles lists", "styles future",
        TIKU_RICHIO_UNSUPPORTED);
    changed(data, length, "https://example.test/?a=1&amp;b=2", "javascript:x",
        TIKU_RICHIO_DAMAGED);
    changed(data, length, "pixel-width=\"2\"", "pixel-width=\"3\"",
        TIKU_RICHIO_DAMAGED);
    changed(data, length, "<text>B", "<text>\xff",
        TIKU_RICHIO_DAMAGED);
    changed(data, length, "<body>",
        "<!DOCTYPE body [<!ENTITY x SYSTEM 'file:///etc/passwd'>]><body>",
        TIKU_RICHIO_UNSUPPORTED);
    free(copy);
}

/** @brief Count cancellation polls and stop at a selected call. */
static int
cancel(void *ctx)
{
    size_t *state = ctx;

    return ++state[0] == state[1];
}

/** @brief Exercise cancellation at each poll and independent byte budgets. */
static void
limits(tiku_richfragment_t *source, const unsigned char *data, size_t length)
{
    tiku_richio_options_t options = {0};
    tiku_richfragment_t *fragment;
    tiku_richdoc_t *imported;
    unsigned char *encoded;
    size_t size, state[2] = {0}, total, codec_polls;

    options.xml_bytes = length - 17;
    refused(data, length, &options, TIKU_RICHIO_LIMIT);
    assert(tiku_richclip_write(source, &options, &encoded, &size) ==
        TIKU_RICHIO_LIMIT && !encoded && !size);
    options.xml_bytes++;
    assert(tiku_richclip_write(source, &options, &encoded, &size) ==
        TIKU_RICHIO_OK && size == length);
    free(encoded);
    options = (tiku_richio_options_t){.memory_bytes = 18};
    refused(data, length, &options, TIKU_RICHIO_LIMIT);
    options = (tiku_richio_options_t){.document.memory_bytes = 1};
    refused(data, length, &options, TIKU_RICHIO_LIMIT);
    options = (tiku_richio_options_t){.document.text_bytes = 8};
    refused(data, length, &options, TIKU_RICHIO_LIMIT);
    options = (tiku_richio_options_t){.cancel = cancel, .ctx = state};
    assert(tiku_richclip_write(source, &options, &encoded, &size) ==
        TIKU_RICHIO_OK);
    free(encoded);
    total = state[0];
    for (size_t i = 1; i <= total; i++) {
        state[0] = 0; state[1] = i;
        assert(tiku_richclip_write(source, &options, &encoded, &size) ==
            TIKU_RICHIO_CANCELLED && !encoded && !size);
    }
    state[0] = state[1] = 0;
    assert(tiku_richio_read_xml(data + 16, length - 16, &options, &imported) ==
        TIKU_RICHIO_OK);
    codec_polls = state[0];
    tiku_richdoc_release(imported);
    state[0] = 0;
    assert(tiku_richclip_read(data, length, &options, &fragment) ==
        TIKU_RICHIO_OK);
    tiku_richfragment_release(fragment);
    total = state[0];
    /* The envelope polls before decoding and on both sides of capture. */
    assert(total == codec_polls + 3);
    for (size_t i = 1; i <= total; i++) {
        state[0] = 0; state[1] = i;
        refused(data, length, &options, TIKU_RICHIO_CANCELLED);
    }
}

/** @brief Refuse a valid native table wrapped as a paragraph fragment. */
static void
table_refusal(void)
{
    tiku_richdoc_t *doc;
    tiku_richsnapshot_t *snapshot;
    tiku_rich_paragraph_t p;
    tiku_rich_edit_t edit = {.kind = TIKU_RICH_TABLE_INSERT,
        .table = {.rows = 1, .columns = 1}};
    tiku_rich_transaction_t tx = {0, &edit, 1};
    unsigned char *xml, *packet;
    size_t length;

    assert(tiku_richdoc_create(NULL, &doc) == TIKU_RICH_OK);
    assert(tiku_richdoc_snapshot(doc, &snapshot) == TIKU_RICH_OK);
    assert(tiku_richsnapshot_paragraph(snapshot, 0, &p) == TIKU_RICH_OK);
    edit.paragraph = p.id;
    tiku_richsnapshot_release(snapshot);
    assert(tiku_richdoc_apply(doc, &tx) == TIKU_RICH_OK);
    assert(tiku_richdoc_snapshot(doc, &snapshot) == TIKU_RICH_OK);
    assert(tiku_richio_write_xml(snapshot, NULL, &xml, &length) == TIKU_RICHIO_OK);
    packet = frame(xml, length);
    refused(packet, length + 16, NULL, TIKU_RICHIO_UNSUPPORTED);
    free(packet);
    free(xml);
    tiku_richsnapshot_release(snapshot);
    tiku_richdoc_release(doc);
}

/** @brief Transfer bytes from an exited encoder process into a fresh model. */
static void
process_transfer(void)
{
    int pipefd[2], status;
    pid_t pid;
    unsigned char *data = malloc(TIKU_RICHCLIP_LIMIT);
    size_t length = 0;
    ssize_t got;
    tiku_richfragment_t *fragment;

    assert(data && !pipe(pipefd));
    pid = fork();
    assert(pid >= 0);
    if (!pid) {
        unsigned char *bytes;
        size_t count, sent = 0;
        tiku_richfragment_t *source = fixture();

        close(pipefd[0]);
        assert(tiku_richclip_write(source, NULL, &bytes, &count) == TIKU_RICHIO_OK);
        tiku_richfragment_release(source);
        while (sent < count) {
            ssize_t n = write(pipefd[1], bytes + sent, count - sent);

            assert(n > 0);
            sent += (size_t)n;
        }
        free(bytes);
        close(pipefd[1]);
        free(data);
        _exit(0);
    }
    close(pipefd[1]);
    while ((got = read(pipefd[0], data + length,
                        TIKU_RICHCLIP_LIMIT - length)) > 0)
        length += (size_t)got;
    assert(got == 0);
    close(pipefd[0]);
    assert(waitpid(pid, &status, 0) == pid && WIFEXITED(status) &&
        WEXITSTATUS(status) == 0);
    assert(tiku_richclip_read(data, length, NULL, &fragment) == TIKU_RICHIO_OK);
    memset(data, 0xa5, length);
    free(data);
    paste(fragment);
}

/** @brief Empty selections and paragraph separators retain their edit meaning. */
static void
empty_ranges(void)
{
    tiku_richdoc_t *doc;
    tiku_rich_paragraph_source_t p[2] = {{0}, {0}};
    tiku_rich_source_t source = {.paragraphs = p, .paragraph_count = 2};
    tiku_richsnapshot_t *snapshot, *result;
    tiku_richfragment_t *fragment, *decoded;
    tiku_rich_paragraph_t first, last;
    unsigned char *data;
    size_t length;

    assert(tiku_richdoc_create(NULL, &doc) == TIKU_RICH_OK);
    assert(tiku_richdoc_import(doc, &source, 0) == TIKU_RICH_OK);
    assert(tiku_richdoc_snapshot(doc, &snapshot) == TIKU_RICH_OK);
    tiku_richsnapshot_paragraph(snapshot, 0, &first);
    for (unsigned i = 0; i < 2; i++) {
        tiku_richsnapshot_paragraph(snapshot, i, &last);
        assert(tiku_richfragment_create(snapshot,
            (tiku_rich_pos_t){tiku_richsnapshot_body(snapshot), first.id, 0, 0},
            (tiku_rich_pos_t){tiku_richsnapshot_body(snapshot), last.id, 0, 0},
            &fragment) == TIKU_RICH_OK);
        assert(tiku_richclip_write(fragment, NULL, &data, &length) == TIKU_RICHIO_OK);
        tiku_richfragment_release(fragment);
        assert(tiku_richclip_read(data, length, NULL, &decoded) == TIKU_RICHIO_OK);
        free(data);
        assert(tiku_richfragment_snapshot(decoded, &result) == TIKU_RICH_OK);
        assert(tiku_richsnapshot_paragraphs(result) == i + 1);
        tiku_richsnapshot_release(result);
        tiku_richfragment_release(decoded);
    }
    tiku_richsnapshot_release(snapshot);
    tiku_richdoc_release(doc);
}

/** @brief Run round trips, independent expectations and hostile-input checks. */
int
main(void)
{
    tiku_richfragment_t *source = fixture(), *decoded;
    tiku_richsnapshot_t *snapshot;
    unsigned char *bytes, *again;
    size_t length, second;

    assert(tiku_richclip_write(NULL, NULL, &bytes, &length) ==
        TIKU_RICHIO_INVALID && !bytes && !length);
    assert(tiku_richclip_write(source, NULL, &bytes, &length) == TIKU_RICHIO_OK);
    assert(tiku_richclip_read(bytes, length, NULL, &decoded) == TIKU_RICHIO_OK);
    assert(tiku_richfragment_snapshot(decoded, &snapshot) == TIKU_RICH_OK);
    content(snapshot);
    tiku_richsnapshot_release(snapshot);
    assert(tiku_richclip_write(decoded, NULL, &again, &second) == TIKU_RICHIO_OK);
    assert(second == length && !memcmp(bytes, again, length));
    free(again);
    paste(decoded);
    hostile(bytes, length);
    limits(source, bytes, length);
    tiku_richfragment_release(source);
    free(bytes);
    table_refusal();
    empty_ranges();
    process_transfer();
    puts("richclip: PASS");
    return 0;
}
