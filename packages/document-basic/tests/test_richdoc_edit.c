/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_richdoc_edit.c - atomic edits, immutable history and saved states.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richdoc_private.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "FAIL richdoc edit:%d: %s\n", __LINE__, #x); \
    exit(1); } } while (0)

/** @brief Capture a snapshot with a checked result. */
static tiku_richsnapshot_t *
snapshot(tiku_richdoc_t *doc)
{
    tiku_richsnapshot_t *s;

    CHECK(tiku_richdoc_snapshot(doc, &s) == TIKU_RICH_OK);
    return s;
}

/** @brief Read the first paragraph's stable ID. */
static uint64_t
paragraph_id(tiku_richdoc_t *doc)
{
    tiku_richsnapshot_t *s = snapshot(doc);
    tiku_rich_paragraph_t p;

    CHECK(tiku_richsnapshot_paragraph(s, 0, &p) == TIKU_RICH_OK);
    tiku_richsnapshot_release(s);
    return p.id;
}

/** @brief Check the live plain-text result and its terminating NUL. */
static void
expect_text(tiku_richdoc_t *doc, const char *text)
{
    tiku_richsnapshot_t *s = snapshot(doc);
    char buffer[4096];
    size_t length;

    CHECK(tiku_richsnapshot_text(s, buffer, sizeof buffer, &length) ==
          TIKU_RICH_OK);
    CHECK(length == strlen(text) && !strcmp(buffer, text));
    tiku_richsnapshot_release(s);
}

/** @brief Apply one edit with a generation precondition. */
static tiku_rich_status_t
apply(tiku_richdoc_t *doc, tiku_rich_edit_t edit)
{
    tiku_rich_transaction_t tx = {tiku_richdoc_generation(doc), &edit, 1};

    return tiku_richdoc_apply(doc, &tx);
}

/** @brief Insert at an explicit byte offset without direct overrides. */
static void
insert(tiku_richdoc_t *doc, size_t at, const char *text)
{
    tiku_rich_edit_t edit = {
        .paragraph = paragraph_id(doc), .start = at, .end = at,
        .text = text, .length = strlen(text)
    };

    CHECK(apply(doc, edit) == TIKU_RICH_OK);
}

/** @brief Saves use state tokens, while undo/redo generations increase. */
static void
history_and_saves(void)
{
    tiku_richdoc_t *doc, *other;
    tiku_richsnapshot_t *saved, *old, *foreign;
    uint64_t state, generation;

    CHECK(tiku_richdoc_create(NULL, &doc) == TIKU_RICH_OK);
    CHECK(!tiku_richdoc_dirty(doc) && !tiku_richdoc_can_undo(doc));
    CHECK(tiku_richdoc_undo(doc) == TIKU_RICH_NO_CHANGE);
    old = snapshot(doc);
    insert(doc, 0, "first");
    saved = snapshot(doc);
    state = tiku_richsnapshot_state(saved);
    CHECK(tiku_richdoc_dirty(doc));
    insert(doc, 5, " second");
    CHECK(tiku_richdoc_mark_saved(doc, saved) == TIKU_RICH_OK);
    CHECK(tiku_richdoc_dirty(doc));
    generation = tiku_richdoc_generation(doc);
    CHECK(tiku_richdoc_undo(doc) == TIKU_RICH_OK);
    CHECK(!tiku_richdoc_dirty(doc));
    CHECK(tiku_richdoc_generation(doc) == generation + 1);
    CHECK(doc->root->state == state);
    expect_text(doc, "first");
    CHECK(tiku_richdoc_undo(doc) == TIKU_RICH_OK);
    CHECK(tiku_richdoc_dirty(doc));
    expect_text(doc, "");
    CHECK(tiku_richdoc_redo(doc) == TIKU_RICH_OK);
    CHECK(!tiku_richdoc_dirty(doc));
    insert(doc, 5, " branch");
    CHECK(!tiku_richdoc_can_redo(doc));
    CHECK(tiku_richdoc_redo(doc) == TIKU_RICH_NO_CHANGE);
    expect_text(doc, "first branch");
    CHECK(tiku_richsnapshot_state(old) == 1);
    CHECK(tiku_richdoc_create(NULL, &other) == TIKU_RICH_OK);
    foreign = snapshot(other);
    CHECK(tiku_richdoc_mark_saved(doc, foreign) == TIKU_RICH_INVALID);
    tiku_richsnapshot_release(foreign);
    tiku_richdoc_release(other);
    CHECK(tiku_richdoc_history_memory(doc) > 0);
    tiku_richdoc_clear_history(doc);
    CHECK(!tiku_richdoc_can_undo(doc));
    CHECK(!tiku_richdoc_history_memory(doc) && tiku_richdoc_dirty(doc));
    tiku_richdoc_release(doc);
    CHECK(tiku_richsnapshot_state(saved) == state);
    tiku_richsnapshot_release(saved);
    tiku_richsnapshot_release(old);
}

/** @brief Multi-operation errors preserve both earlier edits and redo. */
static void
atomicity(void)
{
    tiku_richdoc_t *doc;
    tiku_rich_edit_t edits[2] = {{0}, {0}};
    tiku_rich_transaction_t tx = {.edits = edits, .count = 2};
    size_t memory, history;
    uint64_t generation;

    CHECK(tiku_richdoc_create(NULL, &doc) == TIKU_RICH_OK);
    insert(doc, 0, "base");
    insert(doc, 4, " redo");
    CHECK(tiku_richdoc_undo(doc) == TIKU_RICH_OK);
    generation = tiku_richdoc_generation(doc);
    history = tiku_richdoc_history_memory(doc);
    memory = tiku_richdoc_memory(doc);
    edits[0] = (tiku_rich_edit_t){.paragraph = paragraph_id(doc),
        .start = 4, .end = 4, .text = "!", .length = 1};
    edits[1] = edits[0];
    edits[1].start = edits[1].end = 100;
    CHECK(tiku_richdoc_apply(doc, &tx) == TIKU_RICH_INVALID_RANGE);
    CHECK(tiku_richdoc_memory(doc) == memory);
    CHECK(tiku_richdoc_history_memory(doc) == history);
    CHECK(tiku_richdoc_generation(doc) == generation);
    CHECK(tiku_richdoc_can_redo(doc));
    expect_text(doc, "base");
    tx.generation = generation - 1;
    CHECK(tiku_richdoc_apply(doc, &tx) == TIKU_RICH_STALE);
    tx.generation = generation;
    tiku_richdoc_set_read_only(doc, 1);
    CHECK(tiku_richdoc_apply(doc, &tx) == TIKU_RICH_READ_ONLY);
    CHECK(tiku_richdoc_undo(doc) == TIKU_RICH_READ_ONLY);
    CHECK(tiku_richdoc_redo(doc) == TIKU_RICH_READ_ONLY);
    CHECK(!tiku_richdoc_can_undo(doc) && !tiku_richdoc_can_redo(doc));
    tiku_richdoc_set_read_only(doc, 0);
    edits[1].start = edits[1].end = 5;
    CHECK(tiku_richdoc_apply(doc, &tx) == TIKU_RICH_OK);
    CHECK(!tiku_richdoc_can_redo(doc));
    expect_text(doc, "base!!");
    CHECK(tiku_richdoc_undo(doc) == TIKU_RICH_OK);
    expect_text(doc, "base");
    CHECK(tiku_richdoc_redo(doc) == TIKU_RICH_OK);
    expect_text(doc, "base!!");
    edits[0].length = 0;
    tx.count = 1;
    tx.generation = 0;
    generation = tiku_richdoc_generation(doc);
    CHECK(tiku_richdoc_apply(doc, &tx) == TIKU_RICH_NO_CHANGE);
    CHECK(tiku_richdoc_generation(doc) == generation);
    doc->generation = UINT64_MAX;
    CHECK(tiku_richdoc_apply(doc, &tx) == TIKU_RICH_LIMIT);
    CHECK(tiku_richdoc_undo(doc) == TIKU_RICH_LIMIT);
    tiku_richdoc_release(doc);
}

/** @brief Formatting respects UTF-8, explicit false, inheritance and merge. */
static void
formatting(void)
{
    tiku_richdoc_t *doc;
    tiku_richsnapshot_t *s;
    tiku_rich_paragraph_t p;
    tiku_rich_edit_t edit = {.kind = TIKU_RICH_FORMAT, .start = 1, .end = 3,
        .chars = {.present = TIKU_RICH_BOLD, .bold = 1}};

    CHECK(tiku_richdoc_create(NULL, &doc) == TIKU_RICH_OK);
    insert(doc, 0, "A\xc3\xa9Z");
    edit.paragraph = paragraph_id(doc);
    CHECK(apply(doc, edit) == TIKU_RICH_OK);
    s = snapshot(doc);
    CHECK(tiku_richsnapshot_paragraph(s, 0, &p) == TIKU_RICH_OK);
    CHECK(p.run_count == 3 && p.runs[1].length == 2 && p.runs[1].chars.bold);
    edit.start = 2;
    CHECK(apply(doc, edit) == TIKU_RICH_INVALID_RANGE);
    edit.start = 1;
    edit.chars.bold = 0;
    CHECK(apply(doc, edit) == TIKU_RICH_OK);
    CHECK(p.runs[1].chars.bold);
    tiku_richsnapshot_release(s);
    s = snapshot(doc);
    CHECK(tiku_richsnapshot_paragraph(s, 0, &p) == TIKU_RICH_OK);
    CHECK(!p.runs[1].chars.bold && p.runs[1].chars.present == TIKU_RICH_BOLD);
    tiku_richsnapshot_release(s);
    edit.start = 0;
    edit.end = 4;
    CHECK(apply(doc, edit) == TIKU_RICH_OK);
    CHECK(apply(doc, edit) == TIKU_RICH_NO_CHANGE);
    s = snapshot(doc);
    CHECK(tiku_richsnapshot_paragraph(s, 0, &p) == TIKU_RICH_OK);
    CHECK(p.run_count == 1 && p.runs[0].length == 4);
    tiku_richsnapshot_release(s);
    edit.chars.bold = 1;
    CHECK(apply(doc, edit) == TIKU_RICH_OK);
    insert(doc, 4, "!");
    insert(doc, 0, "?");
    s = snapshot(doc);
    CHECK(tiku_richsnapshot_paragraph(s, 0, &p) == TIKU_RICH_OK);
    CHECK(p.run_count == 2 && !p.runs[0].chars.present);
    CHECK(p.runs[1].length == 5 && p.runs[1].chars.bold);
    tiku_richsnapshot_release(s);
    edit.kind = TIKU_RICH_REPLACE;
    edit.start = 0;
    edit.end = 6;
    edit.chars.present = 0;
    CHECK(apply(doc, edit) == TIKU_RICH_OK);
    expect_text(doc, "");
    CHECK(paragraph_id(doc) == edit.paragraph);
    CHECK(tiku_richdoc_undo(doc) == TIKU_RICH_OK);
    expect_text(doc, "?A\xc3\xa9Z!");
    tiku_richdoc_release(doc);
}

/** @brief History limits evict whole groups and refuse unrecordable edits. */
static void
history_budget(void)
{
    tiku_richdoc_t *doc;
    tiku_rich_limits_t limits = {.history_bytes = 1};
    tiku_rich_edit_t edit = {.text = "a", .length = 1};
    size_t charge, bytes;
    unsigned i, undos = 0;

    CHECK(tiku_richdoc_create(&limits, &doc) == TIKU_RICH_OK);
    edit.paragraph = paragraph_id(doc);
    bytes = tiku_richdoc_memory(doc);
    CHECK(apply(doc, edit) == TIKU_RICH_LIMIT);
    CHECK(tiku_richdoc_memory(doc) == bytes && !tiku_richdoc_can_undo(doc));
    tiku_richdoc_release(doc);
    CHECK(tiku_richdoc_create(NULL, &doc) == TIKU_RICH_OK);
    insert(doc, 0, "a");
    charge = tiku_richdoc_history_memory(doc);
    tiku_richdoc_release(doc);
    limits.history_bytes = charge * 3;
    CHECK(tiku_richdoc_create(&limits, &doc) == TIKU_RICH_OK);
    for (i = 0; i < 12; i++) {
        insert(doc, i, "a");
        CHECK(tiku_richdoc_history_memory(doc) <= limits.history_bytes);
    }
    while (tiku_richdoc_undo(doc) == TIKU_RICH_OK)
        undos++;
    CHECK(undos > 0 && undos <= 3);
    CHECK(tiku_richdoc_redo(doc) == TIKU_RICH_OK);
    tiku_richdoc_release(doc);
}

/** @brief Deterministic pseudo-random generator for independent string edits. */
static unsigned
random_step(unsigned *state)
{
    *state = *state * 1664525u + 1013904223u;
    return *state;
}

/** @brief Compare generated edits and their undo/redo with a flat oracle. */
static void
generated_edits(void)
{
    tiku_richdoc_t *doc;
    tiku_rich_edit_t edit = {0};
    char oracle[1024] = "", before[1024], added[4];
    size_t length = 0, i;
    unsigned seed = 42, step;
    tiku_rich_status_t status;

    CHECK(tiku_richdoc_create(NULL, &doc) == TIKU_RICH_OK);
    edit.paragraph = paragraph_id(doc);
    for (step = 0; step < 3000; step++) {
        memcpy(before, oracle, length + 1);
        edit.start = random_step(&seed) % (length + 1);
        edit.end = edit.start + random_step(&seed) % (length - edit.start + 1);
        edit.length = random_step(&seed) % 4;
        for (i = 0; i < edit.length; i++)
            added[i] = (char)('a' + random_step(&seed) % 26);
        edit.text = added;
        CHECK(length + edit.length < sizeof oracle);
        memmove(oracle + edit.start + edit.length, oracle + edit.end,
                length - edit.end + 1);
        memcpy(oracle + edit.start, added, edit.length);
        length = length - (edit.end - edit.start) + edit.length;
        status = apply(doc, edit);
        CHECK(status == TIKU_RICH_OK || status == TIKU_RICH_NO_CHANGE);
        expect_text(doc, oracle);
        if (status == TIKU_RICH_OK) {
            CHECK(tiku_richdoc_undo(doc) == TIKU_RICH_OK);
            expect_text(doc, before);
            CHECK(tiku_richdoc_redo(doc) == TIKU_RICH_OK);
            expect_text(doc, oracle);
        } else {
            CHECK(!strcmp(before, oracle));
        }
    }
    tiku_richdoc_release(doc);
}

/** @brief Copies preserve styles and share unchanged paragraph payloads. */
static void
shared_nodes(void)
{
    tiku_rich_style_source_t style = {.name = "Heading",
        .chars = {.present = TIKU_RICH_BOLD, .bold = 1}};
    tiku_rich_paragraph_source_t p[2] = {
        {.text = "one", .length = 3, .style = 1},
        {.text = "two", .length = 3}
    };
    tiku_rich_source_t source = {.styles = &style, .style_count = 1,
        .paragraphs = p, .paragraph_count = 2};
    tiku_richdoc_t *doc;
    tiku_richsnapshot_t *old, *current;
    tiku_rich_chars_t chars;
    tiku_rich_para_t props;

    CHECK(tiku_richdoc_create(NULL, &doc) == TIKU_RICH_OK);
    CHECK(tiku_richdoc_import(doc, &source, 0) == TIKU_RICH_OK);
    old = snapshot(doc);
    insert(doc, 3, "!");
    current = snapshot(doc);
    CHECK(old->root->paragraphs[0] != current->root->paragraphs[0]);
    CHECK(old->root->paragraphs[1] == current->root->paragraphs[1]);
    tiku_richsnapshot_release(old);
    tiku_richdoc_clear_history(doc);
    CHECK(!strcmp(current->root->styles[0].value.name, "Heading"));
    CHECK(tiku_richsnapshot_resolve(current, 0, 0, &chars, &props) ==
          TIKU_RICH_OK && chars.bold);
    CHECK(tiku_richdoc_import(doc, &source, 0) == TIKU_RICH_OK);
    CHECK(!tiku_richdoc_dirty(doc) && !tiku_richdoc_can_undo(doc));
    expect_text(doc, "one\ntwo");
    tiku_richdoc_release(doc);
    CHECK(!strcmp(current->root->paragraphs[0]->value.text, "one!"));
    tiku_richsnapshot_release(current);
}

/** @brief Invalid edits and run/text limits leave content and history alone. */
static void
edit_limits(void)
{
    tiku_rich_limits_t limits = {.text_bytes = 8, .runs = 1};
    tiku_richdoc_t *doc;
    tiku_rich_edit_t edit = {.kind = TIKU_RICH_FORMAT, .start = 1, .end = 2,
        .chars = {.present = TIKU_RICH_BOLD, .bold = 1}};
    size_t before;

    CHECK(tiku_richdoc_create(&limits, &doc) == TIKU_RICH_OK);
    insert(doc, 0, "abc");
    edit.paragraph = paragraph_id(doc);
    before = tiku_richdoc_memory(doc);
    CHECK(apply(doc, edit) == TIKU_RICH_LIMIT);
    CHECK(tiku_richdoc_memory(doc) == before);
    edit.kind = TIKU_RICH_REPLACE;
    edit.chars.present = 0;
    edit.text = "1234567";
    edit.length = 7;
    CHECK(apply(doc, edit) == TIKU_RICH_LIMIT);
    edit.length = SIZE_MAX;
    CHECK(apply(doc, edit) == TIKU_RICH_LIMIT);
    edit.text = "\xff";
    edit.length = 1;
    CHECK(apply(doc, edit) == TIKU_RICH_INVALID_TEXT);
    edit.text = "\xef\xbf\xbc";
    edit.length = 3;
    CHECK(apply(doc, edit) == TIKU_RICH_UNSUPPORTED);
    edit.text = "\r";
    edit.length = 1;
    CHECK(apply(doc, edit) == TIKU_RICH_INVALID_TEXT);
    edit.kind = (tiku_rich_edit_kind_t)99;
    CHECK(apply(doc, edit) == TIKU_RICH_UNSUPPORTED);
    CHECK(tiku_richdoc_memory(doc) == before);
    expect_text(doc, "abc");
    tiku_richdoc_release(doc);
}

/** @brief Run transaction, formatting, history and generated-edit checks. */
int
main(void)
{
    history_and_saves();
    atomicity();
    formatting();
    history_budget();
    generated_edits();
    shared_nodes();
    edit_limits();
    puts("PASS: rich document editing, history, 3000 generated edits");
    return 0;
}
