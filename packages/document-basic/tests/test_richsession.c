/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_richsession.c - independent selections, typing formats and history.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richdoc_private.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "FAIL richsession:%d: %s\n", __LINE__, #x); \
    exit(1); } } while (0)

/** @brief Convert a separator-inclusive test offset into a live position. */
static tiku_rich_pos_t
position(tiku_richdoc_t *doc, size_t offset)
{
    size_t i;

    for (i = 0; i < doc->root->count; i++) {
        if (offset <= doc->root->paragraphs[i]->value.length)
            return (tiku_rich_pos_t){doc->root->body,
                doc->root->paragraphs[i]->value.id, offset, TIKU_RICH_AFTER};
        offset -= doc->root->paragraphs[i]->value.length + 1;
    }
    CHECK(0);
    return (tiku_rich_pos_t){0};
}

/** @brief Convert one valid live position to a flat test offset. */
static size_t
offset(tiku_richdoc_t *doc, tiku_rich_pos_t pos)
{
    tiku_richsnapshot_t snapshot = {.root = doc->root};
    size_t i, at = 0;

    CHECK(tiku_richsnapshot_position(&snapshot, pos) == TIKU_RICH_OK);
    for (i = 0; i < doc->root->count; i++) {
        if (doc->root->paragraphs[i]->value.id == pos.paragraph)
            return at + pos.byte;
        at += doc->root->paragraphs[i]->value.length + 1;
    }
    CHECK(0);
    return 0;
}

/** @brief Select in either direction using separator-inclusive offsets. */
static void
select_range(tiku_richsession_t *session, size_t a, size_t c)
{
    CHECK(tiku_richsession_select(session, 0, position(session->doc, a),
        position(session->doc, c)) == TIKU_RICH_OK);
}

/** @brief Verify both directional selection endpoints. */
static void
expect_selection(tiku_richsession_t *session, size_t a, size_t c)
{
    tiku_rich_pos_t anchor, caret;

    CHECK(tiku_richsession_selection(session, &anchor, &caret) == TIKU_RICH_OK);
    CHECK(offset(session->doc, anchor) == a);
    CHECK(offset(session->doc, caret) == c);
}

/** @brief Execute a no-parameter command. */
static tiku_rich_status_t
command(tiku_richsession_t *session, tiku_richcmd_kind_t kind)
{
    const tiku_richcmd_t cmd = {.kind = kind};

    return tiku_richsession_execute(session, 0, &cmd);
}

/** @brief Insert or replace plain text at this session's selection. */
static tiku_rich_status_t
type(tiku_richsession_t *session, const char *text)
{
    const tiku_richcmd_t cmd = {.kind = TIKU_RICHCMD_REPLACE_TEXT,
        .value.text = {text, strlen(text)}};

    return tiku_richsession_execute(session, 0, &cmd);
}

/** @brief Read flat text through an immutable snapshot interface. */
static void
expect_text(tiku_richdoc_t *doc, const char *text)
{
    tiku_richsnapshot_t snapshot = {.root = doc->root};
    char buffer[512];
    size_t length;

    CHECK(tiku_richsnapshot_text(&snapshot, buffer, sizeof buffer,
                                  &length) == TIKU_RICH_OK);
    CHECK(length == strlen(text) && !strcmp(buffer, text));
}

/** @brief Query the session's Bold state, including pending attributes. */
static tiku_richcmd_check_t
bold(tiku_richsession_t *session)
{
    tiku_richcmd_state_t state;

    CHECK(tiku_richsession_query(session, TIKU_RICHCMD_BOLD, &state) ==
          TIKU_RICH_OK);
    CHECK(state.enabled && state.status == TIKU_RICH_OK && !state.reason);
    return state.check;
}

/** @brief Pending formats are view-local, undoable with text and not dirty. */
static void
typing(void)
{
    tiku_richdoc_t *doc;
    tiku_richsession_t *a, *b;
    tiku_rich_selection_t format;
    uint64_t generation;

    CHECK(tiku_richdoc_create(NULL, &doc) == TIKU_RICH_OK);
    CHECK(tiku_richsession_create(doc, &a) == TIKU_RICH_OK);
    CHECK(tiku_richsession_create(doc, &b) == TIKU_RICH_OK);
    generation = doc->generation;
    CHECK(command(a, TIKU_RICHCMD_BOLD) == TIKU_RICH_OK);
    CHECK(bold(a) == TIKU_RICHCMD_CHECK_ON);
    CHECK(bold(b) == TIKU_RICHCMD_CHECK_UNSPECIFIED);
    CHECK(!tiku_richdoc_dirty(doc) && !tiku_richdoc_can_undo(doc));
    CHECK(doc->generation == generation);
    CHECK(type(a, "one\r\ntwo") == TIKU_RICH_OK);
    expect_text(doc, "one\ntwo");
    expect_selection(a, 7, 7);
    expect_selection(b, 7, 7);
    select_range(b, 0, 7);
    CHECK(tiku_richsession_format(b, &format) == TIKU_RICH_OK);
    CHECK(format.chars.bold && !format.mixed_chars);
    CHECK(command(a, TIKU_RICHCMD_BOLD) == TIKU_RICH_OK);
    CHECK(bold(a) == TIKU_RICHCMD_CHECK_OFF);
    CHECK(type(a, "!") == TIKU_RICH_OK);
    CHECK(command(b, TIKU_RICHCMD_UNDO) == TIKU_RICH_OK);
    expect_selection(a, 7, 7);
    CHECK(bold(a) == TIKU_RICHCMD_CHECK_OFF);
    CHECK(command(b, TIKU_RICHCMD_UNDO) == TIKU_RICH_OK);
    expect_text(doc, "");
    expect_selection(a, 0, 0);
    CHECK(bold(a) == TIKU_RICHCMD_CHECK_ON);
    CHECK(!tiku_richdoc_dirty(doc));
    CHECK(command(b, TIKU_RICHCMD_REDO) == TIKU_RICH_OK);
    expect_selection(a, 7, 7);
    CHECK(command(b, TIKU_RICHCMD_REDO) == TIKU_RICH_OK);
    expect_text(doc, "one\ntwo!");
    select_range(a, 0, 0);
    CHECK(bold(a) == TIKU_RICHCMD_CHECK_UNSPECIFIED);
    tiku_richdoc_release(doc);
    CHECK(type(a, "A") == TIKU_RICH_OK);
    CHECK(tiku_richsession_destroy(a) == TIKU_RICH_OK);
    CHECK(type(b, "B") == TIKU_RICH_OK);
    CHECK(tiku_richsession_destroy(b) == TIKU_RICH_OK);
}

/** @brief Restore reversed origin selections without moving another view. */
static void
history(void)
{
    tiku_richdoc_t *doc;
    tiku_richsession_t *a, *b, *late;
    tiku_rich_pos_t before;
    uint64_t id;

    CHECK(tiku_richdoc_create(NULL, &doc) == TIKU_RICH_OK);
    CHECK(tiku_richsession_create(doc, &a) == TIKU_RICH_OK);
    CHECK(tiku_richsession_create(doc, &b) == TIKU_RICH_OK);
    CHECK(type(a, "abcdef") == TIKU_RICH_OK);
    select_range(a, 4, 1);
    select_range(b, 6, 6);
    CHECK(type(a, "XY") == TIKU_RICH_OK);
    expect_text(doc, "aXYef");
    expect_selection(a, 3, 3);
    expect_selection(b, 5, 5);
    select_range(a, 0, 0);
    select_range(b, 1, 1);
    CHECK(command(b, TIKU_RICHCMD_UNDO) == TIKU_RICH_OK);
    expect_selection(a, 4, 1);
    expect_selection(b, 4, 4);
    CHECK(command(b, TIKU_RICHCMD_REDO) == TIKU_RICH_OK);
    expect_selection(a, 3, 3);
    expect_selection(b, 3, 3);
    select_range(a, 4, 1);
    CHECK(command(a, TIKU_RICHCMD_BOLD) == TIKU_RICH_OK);
    select_range(a, 0, 0);
    CHECK(command(b, TIKU_RICHCMD_UNDO) == TIKU_RICH_OK);
    expect_selection(a, 4, 1);
    id = tiku_richsession_id(a);
    CHECK(tiku_richsession_destroy(a) == TIKU_RICH_OK);
    CHECK(tiku_richsession_create(doc, &late) == TIKU_RICH_OK);
    CHECK(tiku_richsession_id(late) != id);
    CHECK(command(b, TIKU_RICHCMD_REDO) == TIKU_RICH_OK);
    expect_selection(late, 0, 0);
    before = position(doc, 0);
    before.affinity = TIKU_RICH_BEFORE;
    CHECK(tiku_richsession_select(late, 0, before, before) == TIKU_RICH_OK);
    select_range(b, 0, 0);
    CHECK(type(b, "!") == TIKU_RICH_OK);
    expect_selection(late, 0, 0);
    expect_selection(b, 1, 1);
    CHECK(tiku_richsession_destroy(late) == TIKU_RICH_OK);
    CHECK(tiku_richsession_destroy(b) == TIKU_RICH_OK);
    tiku_richdoc_release(doc);
}

/** @brief Caret overrides, style assignment and clearing share resolution. */
static void
formatting(void)
{
    const tiku_rich_style_source_t style = {.name = "Emphasis",
        .kind = TIKU_RICH_CHARACTER_STYLE,
        .chars = {.present = TIKU_RICH_ITALIC, .italic = 1}};
    const tiku_rich_run_source_t run = {.length = 2,
        .chars = {.present = TIKU_RICH_BOLD, .bold = 1}, .link = "#note"};
    const tiku_rich_paragraph_source_t p = {.text = "ab", .length = 2,
        .chars = {.present = TIKU_RICH_SIZE, .size = 12},
        .runs = &run, .run_count = 1};
    const tiku_rich_source_t source = {.styles = &style, .style_count = 1,
        .paragraphs = &p, .paragraph_count = 1};
    tiku_richdoc_t *doc;
    tiku_richsession_t *session;
    tiku_richcmd_t cmd = {.kind = TIKU_RICHCMD_CHARACTER_STYLE};
    tiku_rich_selection_t state;

    CHECK(tiku_richdoc_create(NULL, &doc) == TIKU_RICH_OK);
    CHECK(tiku_richdoc_import(doc, &source, 0) == TIKU_RICH_OK);
    CHECK(tiku_richsession_create(doc, &session) == TIKU_RICH_OK);
    select_range(session, 1, 1);
    cmd.value.style = doc->root->styles[0].value.id;
    CHECK(tiku_richsession_execute(session, 0, &cmd) == TIKU_RICH_OK);
    CHECK(command(session, TIKU_RICHCMD_CLEAR_CHARACTER_OVERRIDES) ==
          TIKU_RICH_OK);
    CHECK(tiku_richsession_format(session, &state) == TIKU_RICH_OK);
    CHECK(!(state.chars.present & TIKU_RICH_BOLD));
    CHECK(state.chars.italic && state.chars.size == 12);
    CHECK(state.character_style == cmd.value.style && state.link);
    CHECK(type(session, "X") == TIKU_RICH_OK);
    select_range(session, 1, 2);
    CHECK(tiku_richsession_format(session, &state) == TIKU_RICH_OK);
    CHECK(state.chars.italic && !(state.chars.present & TIKU_RICH_BOLD));
    CHECK(state.link && !strcmp(state.link, "#note"));
    select_range(session, 3, 3);
    CHECK(type(session, "Z") == TIKU_RICH_OK);
    select_range(session, 3, 4);
    CHECK(tiku_richsession_format(session, &state) == TIKU_RICH_OK);
    CHECK(!state.link && !state.character_style && state.chars.bold);
    select_range(session, 0, 0);
    cmd.kind = TIKU_RICHCMD_CHARACTER_FORMAT;
    cmd.value.character.values = (tiku_rich_chars_t){
        .present = TIKU_RICH_SIZE | TIKU_RICH_UNDERLINE,
        .size = 18, .underline = 1};
    cmd.value.character.clear = 0;
    CHECK(tiku_richsession_execute(session, 0, &cmd) == TIKU_RICH_OK);
    CHECK(command(session, TIKU_RICHCMD_PARAGRAPH_BREAK) == TIKU_RICH_OK);
    CHECK(type(session, "Q") == TIKU_RICH_OK);
    CHECK(tiku_richsession_format(session, &state) == TIKU_RICH_OK);
    CHECK(state.chars.size == 18 && state.chars.underline);
    CHECK(command(session, TIKU_RICHCMD_UNDERLINE) == TIKU_RICH_OK);
    CHECK(command(session, TIKU_RICHCMD_STRIKE) == TIKU_RICH_OK);
    CHECK(command(session, TIKU_RICHCMD_ITALIC) == TIKU_RICH_OK);
    CHECK(tiku_richsession_format(session, &state) == TIKU_RICH_OK);
    CHECK(!state.chars.underline && state.chars.strike && state.chars.italic);
    CHECK(tiku_richdoc_import(doc, &source, 0) == TIKU_RICH_OK);
    expect_selection(session, 0, 0);
    CHECK(tiku_richsession_format(session, &state) == TIKU_RICH_OK);
    CHECK(state.chars.size == 12 && !state.chars.strike);
    CHECK(tiku_richsession_destroy(session) == TIKU_RICH_OK);
    tiku_richdoc_release(doc);
}

/** @brief Read-only, stale, malformed and exhausted requests preserve state. */
static void
refusals(void)
{
    tiku_richdoc_t *doc;
    tiku_richsession_t *session, *bad = (void *)1;
    tiku_richcmd_t cmd = {.kind = TIKU_RICHCMD_BOLD};
    tiku_richcmd_state_t query;
    tiku_rich_pos_t valid, invalid;
    tiku_rs_pending_t pending;
    tiku_rich_limits_t limits = {.anchors = 1};

    CHECK(tiku_richdoc_create(NULL, &doc) == TIKU_RICH_OK);
    CHECK(tiku_richsession_create(doc, &session) == TIKU_RICH_OK);
    CHECK(command(session, TIKU_RICHCMD_BOLD) == TIKU_RICH_OK);
    pending = session->pending;
    valid = position(doc, 0);
    invalid = valid;
    invalid.byte++;
    CHECK(tiku_richsession_select(session, 0, valid, invalid) ==
          TIKU_RICH_INVALID_RANGE);
    CHECK(tiku_richsession_select(session, doc->generation + 1, valid,
                                   valid) == TIKU_RICH_STALE);
    CHECK(tiku_richsession_execute(session, doc->generation + 1, &cmd) ==
          TIKU_RICH_STALE);
    CHECK(tiku_richsession_set_read_only(session, 1) == TIKU_RICH_OK);
    CHECK(tiku_richsession_read_only(session) && !doc->read_only);
    CHECK(type(session, "X") == TIKU_RICH_READ_ONLY);
    CHECK(command(session, TIKU_RICHCMD_BOLD) == TIKU_RICH_READ_ONLY);
    CHECK(tiku_richsession_set_read_only(session, 0) == TIKU_RICH_OK);
    tiku_richdoc_set_read_only(doc, 1);
    CHECK(command(session, TIKU_RICHCMD_BOLD) == TIKU_RICH_READ_ONLY);
    tiku_richdoc_set_read_only(doc, 0);
    cmd.kind = TIKU_RICHCMD_CHARACTER_FORMAT;
    cmd.value.character.clear = 512;
    CHECK(tiku_richsession_execute(session, 0, &cmd) ==
          TIKU_RICH_INVALID_STYLE);
    cmd.value.character.clear = 0;
    cmd.value.character.values.present = TIKU_RICH_SIZE;
    cmd.value.character.values.size = -1;
    CHECK(tiku_richsession_execute(session, 0, &cmd) ==
          TIKU_RICH_INVALID_STYLE);
    cmd.kind = TIKU_RICHCMD_CHARACTER_STYLE;
    cmd.value.style = UINT64_MAX;
    CHECK(tiku_richsession_execute(session, 0, &cmd) ==
          TIKU_RICH_INVALID_STYLE);
    CHECK(tiku_richsession_query(session, TIKU_RICHCMD_LINK_SET, &query) ==
          TIKU_RICH_OK && !query.enabled);
    session->caret->revision = UINT64_MAX;
    CHECK(tiku_richsession_select(session, 0, valid, valid) == TIKU_RICH_LIMIT);
    CHECK(!memcmp(&pending, &session->pending, sizeof pending));
    CHECK(session->anchor->revision == 0);
    CHECK(tiku_richsession_query(NULL, TIKU_RICHCMD_BOLD, &query) ==
          TIKU_RICH_INVALID);
    CHECK(!query.enabled && !query.generation);
    CHECK(tiku_richsession_create(NULL, &bad) == TIKU_RICH_INVALID && !bad);
    CHECK(!tiku_richsession_id(NULL) && !tiku_richsession_read_only(NULL));
    CHECK(tiku_richsession_destroy(session) == TIKU_RICH_OK);
    tiku_richdoc_release(doc);
    CHECK(tiku_richdoc_create(&limits, &doc) == TIKU_RICH_OK);
    CHECK(tiku_richsession_create(doc, &bad) == TIKU_RICH_LIMIT && !bad);
    CHECK(!doc->anchors && !doc->anchor_count && doc->refs == 1);
    tiku_richdoc_release(doc);
}

/** @brief Paste adopts source formatting and collapses before notification. */
static void
fragments(void)
{
    tiku_richdoc_t *doc;
    tiku_richsession_t *session;
    tiku_richsnapshot_t *snapshot;
    tiku_richfragment_t *fragment;
    tiku_richcmd_t cmd = {.kind = TIKU_RICHCMD_PASTE_FRAGMENT};

    CHECK(tiku_richdoc_create(NULL, &doc) == TIKU_RICH_OK);
    CHECK(tiku_richsession_create(doc, &session) == TIKU_RICH_OK);
    CHECK(type(session, "one\ntwo") == TIKU_RICH_OK);
    CHECK(tiku_richdoc_snapshot(doc, &snapshot) == TIKU_RICH_OK);
    CHECK(tiku_richfragment_create(snapshot, position(doc, 1),
        position(doc, 5), &fragment) == TIKU_RICH_OK);
    select_range(session, 7, 7);
    CHECK(command(session, TIKU_RICHCMD_BOLD) == TIKU_RICH_OK);
    cmd.value.fragment = fragment;
    CHECK(tiku_richsession_execute(session, 0, &cmd) == TIKU_RICH_OK);
    expect_text(doc, "one\ntwone\nt");
    expect_selection(session, 11, 11);
    CHECK(bold(session) == TIKU_RICHCMD_CHECK_UNSPECIFIED);
    CHECK(command(session, TIKU_RICHCMD_UNDO) == TIKU_RICH_OK);
    CHECK(bold(session) == TIKU_RICHCMD_CHECK_ON);
    expect_selection(session, 7, 7);
    tiku_richfragment_release(fragment);
    tiku_richsnapshot_release(snapshot);
    CHECK(tiku_richsession_destroy(session) == TIKU_RICH_OK);
    tiku_richdoc_release(doc);
}

/** @brief Programmatic edits bypass pending formats and prune lost styles. */
static void
programmatic(void)
{
    const tiku_rich_style_source_t style = {.name = "Hint",
        .kind = TIKU_RICH_CHARACTER_STYLE,
        .chars = {.present = TIKU_RICH_ITALIC, .italic = 1}};
    const tiku_rich_source_t source = {.styles = &style, .style_count = 1,
        .paragraphs = NULL, .paragraph_count = 0};
    tiku_richdoc_t *doc;
    tiku_richsession_t *session;
    tiku_richcmd_t cmd = {.kind = TIKU_RICHCMD_CHARACTER_STYLE};
    tiku_rich_edit_t edit = {.kind = TIKU_RICH_STYLE_REMOVE};
    tiku_rich_transaction_t tx = {.edits = &edit, .count = 1};
    tiku_rich_pos_t pos;
    tiku_rich_selection_t format;

    CHECK(tiku_richdoc_create(NULL, &doc) == TIKU_RICH_OK);
    CHECK(tiku_richdoc_import(doc, &source, 0) == TIKU_RICH_OK);
    CHECK(tiku_richsession_create(doc, &session) == TIKU_RICH_OK);
    cmd.value.style = edit.style = doc->root->styles[0].value.id;
    CHECK(tiku_richsession_execute(session, 0, &cmd) == TIKU_RICH_OK);
    CHECK(command(session, TIKU_RICHCMD_BOLD) == TIKU_RICH_OK);
    pos = position(doc, 0);
    CHECK(tiku_richdoc_replace_range(doc, 0, pos, pos, "\xc3\xa9", 2) ==
          TIKU_RICH_OK);
    CHECK(!doc->root->paragraphs[0]->value.runs[0].chars.bold);
    CHECK(!doc->root->paragraphs[0]->value.runs[0].style);
    expect_selection(session, 2, 2);
    CHECK(tiku_richdoc_apply(doc, &tx) == TIKU_RICH_OK);
    CHECK(tiku_richsession_format(session, &format) == TIKU_RICH_OK);
    CHECK(!format.character_style && !format.chars.italic && format.chars.bold);
    CHECK(tiku_richdoc_undo(doc) == TIKU_RICH_OK);
    CHECK(tiku_richsession_format(session, &format) == TIKU_RICH_OK);
    CHECK(!format.character_style && format.chars.bold);
    pos.byte = 1;
    CHECK(tiku_richsession_select(session, 0, pos, pos) ==
          TIKU_RICH_INVALID_RANGE);
    expect_selection(session, 2, 2);
    CHECK(bold(session) == TIKU_RICHCMD_CHECK_ON);
    select_range(session, 2, 0);
    CHECK(type(session, "\xc3\xa9") == TIKU_RICH_NO_CHANGE);
    expect_selection(session, 2, 0);
    CHECK(tiku_richsession_destroy(session) == TIKU_RICH_OK);
    tiku_richdoc_release(doc);
}

/** @brief Deterministic input generation independent of model mappings. */
static unsigned
random_value(void)
{
    static uint32_t seed = 1977;

    seed = seed * 1664525u + 1013904223u;
    return seed;
}

/** @brief Map an after-affinity flat offset through text replacement. */
static size_t
mapped(size_t value, size_t first, size_t last, size_t inserted)
{
    if (value < first)
        return value;
    if (value > last)
        return value - (last - first) + inserted;
    return first + inserted;
}

/** @brief Compare three sessions against flat-text selection arithmetic. */
static void
generated(void)
{
    tiku_richdoc_t *doc;
    tiku_richsession_t *sessions[3];
    size_t before[3][2], after[3][2], first, last, i, n, origin;
    char original[] = "abcd\nefgh\nijkl", expected[64];
    const char *insert;

    CHECK(tiku_richdoc_create(NULL, &doc) == TIKU_RICH_OK);
    for (i = 0; i < 3; i++)
        CHECK(tiku_richsession_create(doc, &sessions[i]) == TIKU_RICH_OK);
    CHECK(type(sessions[0], original) == TIKU_RICH_OK);
    for (n = 0; n < 1000; n++) {
        origin = random_value() % 3;
        for (i = 0; i < 3; i++) {
            before[i][0] = random_value() % sizeof original;
            before[i][1] = random_value() % sizeof original;
            select_range(sessions[i], before[i][0], before[i][1]);
        }
        first = before[origin][0] < before[origin][1] ?
                before[origin][0] : before[origin][1];
        last = before[origin][0] > before[origin][1] ?
               before[origin][0] : before[origin][1];
        insert = random_value() & 1 ? "X" : "Y\nZ";
        memcpy(expected, original, first);
        memcpy(expected + first, insert, strlen(insert));
        strcpy(expected + first + strlen(insert), original + last);
        CHECK(type(sessions[origin], insert) == TIKU_RICH_OK);
        expect_text(doc, expected);
        for (i = 0; i < 3; i++) {
            after[i][0] = mapped(before[i][0], first, last, strlen(insert));
            after[i][1] = mapped(before[i][1], first, last, strlen(insert));
            if (i == origin)
                after[i][0] = after[i][1] = first + strlen(insert);
            expect_selection(sessions[i], after[i][0], after[i][1]);
        }
        select_range(sessions[origin], 0, 0);
        CHECK(command(sessions[(origin + 1) % 3], TIKU_RICHCMD_UNDO) ==
              TIKU_RICH_OK);
        expect_text(doc, original);
        for (i = 0; i < 3; i++)
            expect_selection(sessions[i], before[i][0], before[i][1]);
        CHECK(command(sessions[(origin + 2) % 3], TIKU_RICHCMD_REDO) ==
              TIKU_RICH_OK);
        expect_text(doc, expected);
        for (i = 0; i < 3; i++)
            expect_selection(sessions[i], after[i][0], after[i][1]);
        CHECK(tiku_richdoc_undo(doc) == TIKU_RICH_OK);
        tiku_richdoc_clear_history(doc);
    }
    for (i = 0; i < 3; i++)
        CHECK(tiku_richsession_destroy(sessions[i]) == TIKU_RICH_OK);
    tiku_richdoc_release(doc);
}

/** @brief Run independent-view and generated selection-history checks. */
int
main(void)
{
    typing();
    history();
    formatting();
    refusals();
    fragments();
    programmatic();
    generated();
    puts("PASS: rich sessions, typing state and 1000 three-view history cases");
    return 0;
}
