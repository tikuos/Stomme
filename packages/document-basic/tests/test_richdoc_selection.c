/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_richdoc_selection.c - uniform, mixed and insertion property queries.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richdoc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "FAIL selection:%d: %s\n", __LINE__, #x); \
    exit(1); } } while (0)

/** @brief Resolve a public paragraph index and local byte into a position. */
static tiku_rich_pos_t
position(const tiku_richsnapshot_t *s, size_t index, size_t byte)
{
    tiku_rich_paragraph_t p;

    CHECK(tiku_richsnapshot_paragraph(s, index, &p) == TIKU_RICH_OK);
    return (tiku_rich_pos_t){tiku_richsnapshot_body(s), p.id, byte,
                             TIKU_RICH_AFTER};
}

/** @brief Build styled paragraphs with explicit false and absent properties. */
static tiku_richdoc_t *
fixture(void)
{
    const tiku_rich_style_source_t styles[3] = {
        {.name = "Body", .chars = {.present = TIKU_RICH_SIZE, .size = 12}},
        {.name = "Other", .parent = 1},
        {.name = "Strong", .kind = TIKU_RICH_CHARACTER_STYLE,
         .chars = {.present = TIKU_RICH_BOLD, .bold = 1}}
    };
    const tiku_rich_run_source_t runs[3] = {
        {.length = 2, .style = 3, .link = "#target"},
        {.length = 2, .chars = {.present = TIKU_RICH_BOLD}, .link = "#target"},
        {.length = 2}
    };
    const tiku_rich_paragraph_source_t p[3] = {
        {.text = "abcdef", .length = 6, .style = 1, .runs = runs,
         .run_count = 3, .list = {TIKU_RICH_LIST_DECIMAL, 0, 0},
         .paragraph = {.present = TIKU_RICH_ALIGNMENT | TIKU_RICH_SPACING,
                       .align = TIKU_RICH_CENTER, .before = 4}},
        {.text = "\xc3\xa9", .length = 2, .style = 2,
         .paragraph = {.present = TIKU_RICH_ALIGNMENT | TIKU_RICH_SPACING,
                       .align = TIKU_RICH_LEFT, .before = 4}},
        {.style = 1, .chars = {.present = TIKU_RICH_ITALIC, .italic = 1}}
    };
    const tiku_rich_source_t source = {.styles = styles, .style_count = 3,
        .paragraphs = p, .paragraph_count = 3};
    tiku_richdoc_t *doc;

    CHECK(tiku_richdoc_create(NULL, &doc) == TIKU_RICH_OK);
    CHECK(tiku_richdoc_import(doc, &source, 0) == TIKU_RICH_OK);
    return doc;
}

/** @brief Mixed properties retain uniform values without losing presence. */
static void
mixed_properties(void)
{
    tiku_richdoc_t *doc = fixture();
    tiku_richsnapshot_t *s;
    tiku_rich_selection_t state;
    tiku_rich_pos_t first, last;

    CHECK(tiku_richdoc_snapshot(doc, &s) == TIKU_RICH_OK);
    first = position(s, 0, 0);
    last = position(s, 0, 2);
    CHECK(tiku_richsnapshot_selection(s, first, last, &state) == TIKU_RICH_OK);
    CHECK(!state.empty && state.paragraphs == 1 && !state.mixed_chars);
    CHECK(state.chars.bold && state.chars.size == 12);
    CHECK(state.character_style && !strcmp(state.link, "#target"));
    last.byte = 4;
    CHECK(tiku_richsnapshot_selection(s, first, last, &state) == TIKU_RICH_OK);
    CHECK(state.mixed_chars == TIKU_RICH_BOLD && !state.chars.bold);
    CHECK(state.chars.present == TIKU_RICH_SIZE && state.chars.size == 12);
    CHECK(state.mixed == TIKU_RICH_MIXED_CHARACTER_STYLE);
    CHECK(!state.character_style && !strcmp(state.link, "#target"));
    first.byte = 2;
    CHECK(tiku_richsnapshot_selection(s, first, last, &state) == TIKU_RICH_OK);
    CHECK(!state.mixed_chars && (state.chars.present & TIKU_RICH_BOLD));
    CHECK(!state.chars.bold);
    last.byte = 6;
    CHECK(tiku_richsnapshot_selection(s, first, last, &state) == TIKU_RICH_OK);
    CHECK(state.mixed_chars == TIKU_RICH_BOLD);
    CHECK(state.mixed == TIKU_RICH_MIXED_LINK && !state.link);
    first.byte = 0;
    last = position(s, 1, 2);
    CHECK(tiku_richsnapshot_selection(s, first, last, &state) == TIKU_RICH_OK);
    CHECK(state.paragraphs == 2 && state.mixed_props == TIKU_RICH_ALIGNMENT);
    CHECK(state.props.present == TIKU_RICH_SPACING && state.props.before == 4);
    CHECK(state.mixed == (TIKU_RICH_MIXED_LINK | TIKU_RICH_MIXED_LIST |
          TIKU_RICH_MIXED_CHARACTER_STYLE | TIKU_RICH_MIXED_PARAGRAPH_STYLE));
    CHECK(!state.paragraph_style && state.list.kind == TIKU_RICH_LIST_NONE);
    last.byte = 0;
    CHECK(tiku_richsnapshot_selection(s, first, last, &state) == TIKU_RICH_OK);
    CHECK(state.paragraphs == 1 && !state.mixed_props);
    CHECK(state.props.align == TIKU_RICH_CENTER);
    first.byte = 6;
    CHECK(tiku_richsnapshot_selection(s, first, last, &state) == TIKU_RICH_OK);
    CHECK(!state.empty && state.chars.present == TIKU_RICH_SIZE);
    CHECK(!state.mixed_chars && !state.link);
    tiku_richsnapshot_release(s);
    tiku_richdoc_release(doc);
}

/** @brief Carets follow typing inheritance, including hyperlink boundaries. */
static void
carets_and_validation(void)
{
    tiku_richdoc_t *doc = fixture();
    tiku_richsnapshot_t *s;
    tiku_rich_selection_t state, zero = {0};
    tiku_rich_pos_t p, last;

    CHECK(tiku_richdoc_snapshot(doc, &s) == TIKU_RICH_OK);
    tiku_richdoc_set_read_only(doc, 1);
    tiku_richdoc_release(doc);
    p = position(s, 0, 0);
    CHECK(tiku_richsnapshot_selection(s, p, p, &state) == TIKU_RICH_OK);
    CHECK(state.empty && !state.link && state.chars.present == TIKU_RICH_SIZE);
    p.byte = 1;
    CHECK(tiku_richsnapshot_selection(s, p, p, &state) == TIKU_RICH_OK);
    CHECK(state.chars.bold && state.link && state.character_style);
    p.byte = 2;
    CHECK(tiku_richsnapshot_selection(s, p, p, &state) == TIKU_RICH_OK);
    CHECK(state.chars.bold && state.link && !state.mixed_chars);
    p.byte = 4;
    CHECK(tiku_richsnapshot_selection(s, p, p, &state) == TIKU_RICH_OK);
    CHECK(!state.chars.bold && !state.link && !state.mixed);
    p = position(s, 2, 0);
    CHECK(tiku_richsnapshot_selection(s, p, p, &state) == TIKU_RICH_OK);
    CHECK(state.empty && state.chars.italic && state.chars.size == 12);
    last = position(s, 1, 1);
    CHECK(tiku_richsnapshot_selection(s, last, last, &state) ==
          TIKU_RICH_INVALID_RANGE);
    CHECK(!memcmp(&state, &zero, sizeof zero));
    last.byte = 0;
    CHECK(tiku_richsnapshot_selection(s, p, last, &state) ==
          TIKU_RICH_INVALID_RANGE);
    CHECK(!memcmp(&state, &zero, sizeof zero));
    CHECK(tiku_richsnapshot_selection(NULL, p, p, &state) == TIKU_RICH_INVALID);
    CHECK(!memcmp(&state, &zero, sizeof zero));
    CHECK(tiku_richsnapshot_selection(s, p, p, NULL) == TIKU_RICH_INVALID);
    tiku_richsnapshot_release(s);
}

/** @brief Check mixed-format masks, half-open ranges and insertion defaults. */
int
main(void)
{
    mixed_properties();
    carets_and_validation();
    puts("PASS: rich document selection formatting, mixed values and carets");
    return 0;
}
