/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_richdoc_model.c - document imports, snapshots and worker ownership.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richdoc.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "FAIL richdoc model:%d: %s\n", __LINE__, #x); \
    exit(1); } } while (0)

/** @brief Create a checked empty document. */
static tiku_richdoc_t *
create(void)
{
    tiku_richdoc_t *doc = NULL;

    CHECK(tiku_richdoc_create(NULL, &doc) == TIKU_RICH_OK);
    return doc;
}

/** @brief Capture a checked snapshot. */
static tiku_richsnapshot_t *
snapshot(tiku_richdoc_t *doc)
{
    tiku_richsnapshot_t *s = NULL;

    CHECK(tiku_richdoc_snapshot(doc, &s) == TIKU_RICH_OK);
    return s;
}

/** @brief Load one paragraph from an explicitly bounded byte string. */
static tiku_rich_status_t
load(tiku_richdoc_t *doc, const char *text, size_t length)
{
    const tiku_rich_paragraph_source_t p = {.text = text, .length = length};
    const tiku_rich_source_t source = {
        .paragraphs = &p, .paragraph_count = 1
    };

    return tiku_richdoc_import(doc, &source, 0);
}

/** @brief Check extracted bytes without relying on paragraph internals. */
static void
text_is(const tiku_richsnapshot_t *s, const char *text)
{
    char buffer[2048];
    size_t length = 0;

    CHECK(tiku_richsnapshot_text(s, NULL, 0, &length) == TIKU_RICH_OK);
    CHECK(length == strlen(text));
    CHECK(tiku_richsnapshot_text(s, buffer, sizeof buffer, &length) ==
          TIKU_RICH_OK);
    CHECK(!strcmp(buffer, text));
}

/** @brief Empty state, copied inputs, snapshot survival and pointer outputs. */
static void
lifetime(void)
{
    tiku_richdoc_t *doc = create();
    tiku_richsnapshot_t *empty = snapshot(doc), *loaded;
    tiku_rich_paragraph_t p;
    char input[] = "independent";
    size_t memory = tiku_richdoc_memory(doc), n = 12;
    char short_buffer[] = "unchanged";

    CHECK(tiku_richsnapshot_paragraphs(empty) == 1);
    CHECK(tiku_richsnapshot_styles(empty) == 0);
    CHECK(tiku_richsnapshot_paragraph(empty, 0, &p) == TIKU_RICH_OK);
    CHECK(p.id && !p.length && !p.run_count);
    CHECK(load(doc, input, strlen(input)) == TIKU_RICH_OK);
    loaded = snapshot(doc);
    memset(input, 'x', strlen(input));
    text_is(empty, "");
    text_is(loaded, "independent");
    CHECK(tiku_richsnapshot_generation(empty) == 1);
    CHECK(tiku_richsnapshot_generation(loaded) == 2);
    CHECK(tiku_richdoc_memory(doc) > memory);
    CHECK(tiku_richsnapshot_text(loaded, short_buffer, 4, &n) ==
          TIKU_RICH_LIMIT);
    CHECK(n == 11 && !strcmp(short_buffer, "unchanged"));
    CHECK(tiku_richsnapshot_paragraph(loaded, 9, &p) ==
          TIKU_RICH_INVALID_RANGE);
    CHECK(!p.text && !p.id);
    tiku_richdoc_retain(doc);
    tiku_richdoc_release(doc);
    tiku_richdoc_release(doc);
    text_is(empty, "");
    text_is(loaded, "independent");
    tiku_richsnapshot_release(empty);
    tiku_richsnapshot_release(loaded);
    tiku_richsnapshot_release(NULL);
    tiku_richdoc_release(NULL);
}

/** @brief Invalid UTF-8 and unsupported object markers never publish. */
static void
text_validation(void)
{
    static const struct { const char *text; size_t n; } bad[] = {
        {"x\0y", 3}, {"\xc0\x80", 2}, {"\xe0\x80\x80", 3},
        {"\xed\xa0\x80", 3}, {"\xf4\x90\x80\x80", 4},
        {"\xf0\x80\x80\x80", 4}, {"\xf0\x9f", 2}, {"\x80", 1},
        {"\xfe", 1}, {"\xff", 1}, {"\xe2\x28\xa1", 3},
        {"\r", 1}, {"\1", 1}
    };
    tiku_richdoc_t *doc = create();
    size_t i, memory = tiku_richdoc_memory(doc);
    tiku_richsnapshot_t *s;

    for (i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        CHECK(load(doc, bad[i].text, bad[i].n) == TIKU_RICH_INVALID_TEXT);
        CHECK(tiku_richdoc_generation(doc) == 1);
        CHECK(tiku_richdoc_memory(doc) == memory);
    }
    CHECK(load(doc, NULL, 1) == TIKU_RICH_INVALID);
    CHECK(load(doc, "\xef\xbf\xbc", 3) == TIKU_RICH_UNSUPPORTED);
    CHECK(load(doc, "\t\n\xc2\xa0\xf0\x9f\x98\x80", 8) == TIKU_RICH_OK);
    s = snapshot(doc);
    text_is(s, "\t\n\xc2\xa0\xf0\x9f\x98\x80");
    tiku_richsnapshot_release(s);
    tiku_richdoc_release(doc);
}

/** @brief Positions are bounded byte offsets attached to captured IDs. */
static void
positions(void)
{
    tiku_richdoc_t *doc = create();
    tiku_richsnapshot_t *a, *b;
    tiku_rich_paragraph_t p;
    tiku_rich_pos_t pos, end;
    int order = 99;

    CHECK(load(doc, "A\xc3\xa9", 3) == TIKU_RICH_OK);
    a = snapshot(doc);
    CHECK(tiku_richsnapshot_paragraph(a, 0, &p) == TIKU_RICH_OK);
    pos = (tiku_rich_pos_t){tiku_richsnapshot_body(a), p.id, 1,
                            TIKU_RICH_BEFORE};
    end = pos;
    end.byte = 3;
    CHECK(tiku_richsnapshot_position(a, pos) == TIKU_RICH_OK);
    CHECK(tiku_richsnapshot_compare(a, pos, end, &order) == TIKU_RICH_OK);
    CHECK(order == -1);
    pos.byte = 2;
    CHECK(tiku_richsnapshot_position(a, pos) == TIKU_RICH_INVALID_RANGE);
    pos.byte = 0;
    pos.affinity = (tiku_rich_affinity_t)5;
    CHECK(tiku_richsnapshot_position(a, pos) == TIKU_RICH_INVALID_RANGE);
    CHECK(load(doc, "new", 3) == TIKU_RICH_OK);
    b = snapshot(doc);
    CHECK(tiku_richsnapshot_position(a, end) == TIKU_RICH_OK);
    CHECK(tiku_richsnapshot_position(b, end) == TIKU_RICH_INVALID_RANGE);
    tiku_richsnapshot_release(a);
    tiku_richsnapshot_release(b);
    tiku_richdoc_release(doc);
}

/** @brief Failed size, generation and read-only checks preserve content. */
static void
limits(void)
{
    tiku_rich_limits_t config = {.text_bytes = 3, .paragraphs = 2, .runs = 1};
    tiku_richdoc_t *doc;
    tiku_rich_source_t source = {0};
    tiku_rich_paragraph_source_t p[3] = {{0}};
    tiku_rich_run_source_t runs[2] = {{.length = 1}, {.length = 1}};
    tiku_richsnapshot_t *s;
    size_t memory;

    CHECK(tiku_richdoc_create(&config, &doc) == TIKU_RICH_OK);
    CHECK(load(doc, "abc", 3) == TIKU_RICH_OK);
    memory = tiku_richdoc_memory(doc);
    CHECK(load(doc, "abcd", 4) == TIKU_RICH_LIMIT);
    CHECK(load(doc, "x", SIZE_MAX) == TIKU_RICH_LIMIT);
    CHECK(tiku_richdoc_import(doc, &source, 1) == TIKU_RICH_STALE);
    tiku_richdoc_set_read_only(doc, 1);
    CHECK(tiku_richdoc_read_only(doc));
    CHECK(load(doc, "", 0) == TIKU_RICH_READ_ONLY);
    tiku_richdoc_set_read_only(doc, 0);
    source.paragraphs = p;
    source.paragraph_count = 3;
    CHECK(tiku_richdoc_import(doc, &source, 0) == TIKU_RICH_LIMIT);
    p[0].text = "ab";
    p[0].length = 2;
    p[0].runs = runs;
    p[0].run_count = 2;
    source.paragraph_count = 1;
    CHECK(tiku_richdoc_import(doc, &source, 0) == TIKU_RICH_LIMIT);
    CHECK(tiku_richdoc_generation(doc) == 2);
    CHECK(tiku_richdoc_memory(doc) == memory);
    s = snapshot(doc);
    text_is(s, "abc");
    tiku_richsnapshot_release(s);
    tiku_richdoc_release(doc);
    config.memory_bytes = 1;
    CHECK(tiku_richdoc_create(&config, &doc) == TIKU_RICH_LIMIT && !doc);
}

/** @brief A deterministic independent sequence for generated documents. */
static unsigned
random_next(unsigned *seed)
{
    *seed = *seed * 1664525u + 1013904223u;
    return *seed;
}

/** @brief Generated paragraphs compare with independently concatenated text. */
static void
generated(void)
{
    tiku_richdoc_t *doc = create();
    tiku_rich_paragraph_source_t p[12];
    char text[12][64], expected[1024], previous[1024] = "";
    tiku_richsnapshot_t *old = snapshot(doc), *now;
    tiku_rich_source_t source = {.paragraphs = p};
    unsigned seed = 74591, step;
    size_t i, k, at;

    for (step = 0; step < 2000; step++) {
        memset(p, 0, sizeof p);
        source.paragraph_count = random_next(&seed) % 12 + 1;
        at = 0;
        for (i = 0; i < source.paragraph_count; i++) {
            p[i].length = random_next(&seed) % 63;
            p[i].text = text[i];
            if (i)
                expected[at++] = '\n';
            for (k = 0; k < p[i].length; k++) {
                text[i][k] = (char)('a' + random_next(&seed) % 26);
                expected[at++] = text[i][k];
            }
        }
        expected[at] = '\0';
        CHECK(tiku_richdoc_import(doc, &source,
              tiku_richdoc_generation(doc)) == TIKU_RICH_OK);
        now = snapshot(doc);
        text_is(now, expected);
        text_is(old, previous);
        strcpy(previous, expected);
        tiku_richsnapshot_release(old);
        old = now;
    }
    tiku_richsnapshot_release(old);
    tiku_richdoc_release(doc);
}

/** @brief A snapshot handed to a worker with no live-document dependency. */
typedef struct {
    tiku_richsnapshot_t *snapshot;
    int bad;
} worker_t;

/** @brief Read and release immutable content while the owner imports. */
static void *
read_worker(void *arg)
{
    worker_t *w = arg;
    unsigned i;
    char text[16];
    size_t length;

    for (i = 0; i < 20000; i++)
        if (tiku_richsnapshot_text(w->snapshot, text, sizeof text, &length) !=
            TIKU_RICH_OK || length != 6 || strcmp(text, "worker"))
            w->bad = 1;
    tiku_richsnapshot_release(w->snapshot);
    return NULL;
}

/** @brief Separate snapshots can finish after the live document is closed. */
static void
threads(void)
{
    tiku_richdoc_t *doc = create();
    pthread_t threads[4];
    worker_t work[4] = {{0}};
    size_t i;

    CHECK(load(doc, "worker", 6) == TIKU_RICH_OK);
    for (i = 0; i < 4; i++) {
        work[i].snapshot = snapshot(doc);
        CHECK(!pthread_create(&threads[i], NULL, read_worker, &work[i]));
    }
    for (i = 0; i < 100; i++)
        CHECK(load(doc, "current", 7) == TIKU_RICH_OK);
    tiku_richdoc_release(doc);
    for (i = 0; i < 4; i++) {
        CHECK(!pthread_join(threads[i], NULL));
        CHECK(!work[i].bad);
    }
}

/** @brief Run model, validation and cross-thread lifetime checks. */
int
main(void)
{
    lifetime();
    text_validation();
    positions();
    limits();
    generated();
    threads();
    puts("PASS: rich document model, 2000 generated imports, worker lifetime");
    return 0;
}
