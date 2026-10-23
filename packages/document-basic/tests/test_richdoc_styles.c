/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_richdoc_styles.c - inheritance, explicit overrides and invalid runs.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richdoc.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "FAIL richdoc styles:%d: %s\n", __LINE__, #x); \
    exit(1); } } while (0)

/** @brief A paragraph style chain and an independent character style. */
static void
inheritance(void)
{
    tiku_rich_style_source_t styles[3] = {
        {.name = "Base", .kind = TIKU_RICH_PARAGRAPH_STYLE,
         .chars = {.present = TIKU_RICH_BOLD | TIKU_RICH_FONT,
                   .bold = 1, .font = "Example"},
         .paragraph = {.present = TIKU_RICH_ALIGNMENT,
                       .align = TIKU_RICH_CENTER}},
        {.name = "Heading", .kind = TIKU_RICH_PARAGRAPH_STYLE, .parent = 1,
         .chars = {.present = TIKU_RICH_BOLD | TIKU_RICH_SIZE,
                   .bold = 0, .size = 24}},
        {.name = "Emphasis", .kind = TIKU_RICH_CHARACTER_STYLE,
         .chars = {.present = TIKU_RICH_ITALIC, .italic = 1}}
    };
    tiku_rich_run_source_t runs[2] = {
        {.length = 1, .style = 3},
        {.length = 2, .chars = {.present = TIKU_RICH_FOREGROUND,
                                .foreground = 0xff000000}}
    };
    tiku_rich_paragraph_source_t p = {
        .text = "A\xc3\xa9", .length = 3, .runs = runs, .run_count = 2,
        .style = 2
    };
    tiku_rich_source_t source = {.styles = styles, .style_count = 3,
        .paragraphs = &p, .paragraph_count = 1};
    tiku_richdoc_t *doc;
    tiku_richsnapshot_t *s;
    tiku_rich_chars_t chars;
    tiku_rich_para_t props;
    tiku_rich_style_t style, parent;

    CHECK(tiku_richdoc_create(NULL, &doc) == TIKU_RICH_OK);
    CHECK(tiku_richdoc_import(doc, &source, 0) == TIKU_RICH_OK);
    CHECK(tiku_richdoc_snapshot(doc, &s) == TIKU_RICH_OK);
    CHECK(tiku_richsnapshot_styles(s) == 3);
    CHECK(tiku_richsnapshot_style(s, 1, &style) == TIKU_RICH_OK);
    CHECK(tiku_richsnapshot_style(s, 0, &parent) == TIKU_RICH_OK);
    CHECK(style.parent == parent.id && !strcmp(style.name, "Heading"));
    CHECK(tiku_richsnapshot_resolve(s, 0, 0, &chars, &props) == TIKU_RICH_OK);
    CHECK((chars.present & TIKU_RICH_BOLD) && !chars.bold);
    CHECK(chars.italic && chars.size == 24 && !strcmp(chars.font, "Example"));
    CHECK(props.align == TIKU_RICH_CENTER);
    CHECK(tiku_richsnapshot_resolve(s, 0, 1, &chars, &props) == TIKU_RICH_OK);
    CHECK(!(chars.present & TIKU_RICH_ITALIC));
    CHECK(chars.foreground == 0xff000000);
    memset(styles, 0, sizeof styles);
    memset(runs, 0, sizeof runs);
    CHECK(!strcmp(style.name, "Heading"));
    CHECK(tiku_richsnapshot_resolve(s, 0, 0, &chars, &props) == TIKU_RICH_OK);
    CHECK(chars.italic && chars.size == 24);
    CHECK(tiku_richsnapshot_resolve(s, 0, 99, &chars, &props) ==
          TIKU_RICH_INVALID_RANGE);
    CHECK(!chars.present && !props.present);
    tiku_richdoc_release(doc);
    CHECK(!strcmp(style.name, "Heading"));
    tiku_richsnapshot_release(s);
}

/** @brief Cycles, wrong categories, duplicate names and invalid numbers. */
static void
invalid_styles(void)
{
    tiku_rich_style_source_t styles[2] = {
        {.name = "One"}, {.name = "Two", .parent = 1}
    };
    tiku_rich_source_t source = {.styles = styles, .style_count = 2};
    tiku_richdoc_t *doc;
    size_t memory;

    CHECK(tiku_richdoc_create(NULL, &doc) == TIKU_RICH_OK);
    memory = tiku_richdoc_memory(doc);
    styles[0].parent = 2;
    CHECK(tiku_richdoc_import(doc, &source, 0) == TIKU_RICH_INVALID_STYLE);
    styles[0].parent = 0;
    styles[1].parent = 3;
    CHECK(tiku_richdoc_import(doc, &source, 0) == TIKU_RICH_INVALID_STYLE);
    styles[1].parent = 1;
    styles[1].kind = TIKU_RICH_CHARACTER_STYLE;
    CHECK(tiku_richdoc_import(doc, &source, 0) == TIKU_RICH_INVALID_STYLE);
    styles[1].kind = TIKU_RICH_PARAGRAPH_STYLE;
    strcpy(styles[1].name, "One");
    CHECK(tiku_richdoc_import(doc, &source, 0) == TIKU_RICH_INVALID_STYLE);
    strcpy(styles[1].name, "Two");
    styles[0].chars.present = TIKU_RICH_SIZE;
    styles[0].chars.size = NAN;
    CHECK(tiku_richdoc_import(doc, &source, 0) == TIKU_RICH_INVALID_STYLE);
    styles[0].chars.size = INFINITY;
    CHECK(tiku_richdoc_import(doc, &source, 0) == TIKU_RICH_INVALID_STYLE);
    styles[0].chars.present = 1u << 20;
    CHECK(tiku_richdoc_import(doc, &source, 0) == TIKU_RICH_INVALID_STYLE);
    styles[0].chars.present = TIKU_RICH_BOLD;
    styles[0].chars.bold = 2;
    CHECK(tiku_richdoc_import(doc, &source, 0) == TIKU_RICH_INVALID_STYLE);
    styles[0].chars.present = TIKU_RICH_FONT;
    memset(styles[0].chars.font, 'x', sizeof styles[0].chars.font);
    CHECK(tiku_richdoc_import(doc, &source, 0) == TIKU_RICH_INVALID_STYLE);
    CHECK(tiku_richdoc_memory(doc) == memory);
    CHECK(tiku_richdoc_generation(doc) == 1);
    tiku_richdoc_release(doc);
}

/** @brief Runs cover the text exactly, without splitting UTF-8 characters. */
static void
runs_and_empty(void)
{
    tiku_richdoc_t *doc;
    tiku_rich_run_source_t runs[2] = {{.length = 1}, {.length = 1}};
    tiku_rich_paragraph_source_t p = {
        .text = "\xc3\xa9", .length = 2, .runs = runs, .run_count = 2
    };
    tiku_rich_source_t source = {.paragraphs = &p, .paragraph_count = 1};
    tiku_richsnapshot_t *s;
    tiku_rich_chars_t chars;
    tiku_rich_para_t props;

    CHECK(tiku_richdoc_create(NULL, &doc) == TIKU_RICH_OK);
    CHECK(tiku_richdoc_import(doc, &source, 0) == TIKU_RICH_INVALID_RANGE);
    p.text = "ab";
    runs[1].length = 0;
    CHECK(tiku_richdoc_import(doc, &source, 0) == TIKU_RICH_INVALID_RANGE);
    p.run_count = 1;
    CHECK(tiku_richdoc_import(doc, &source, 0) == TIKU_RICH_INVALID_RANGE);
    runs[0].length = SIZE_MAX;
    CHECK(tiku_richdoc_import(doc, &source, 0) == TIKU_RICH_INVALID_RANGE);
    runs[0].length = 2;
    runs[0].style = 1;
    CHECK(tiku_richdoc_import(doc, &source, 0) == TIKU_RICH_INVALID_STYLE);
    p.length = 0;
    p.run_count = 0;
    p.chars.present = TIKU_RICH_BOLD;
    p.chars.bold = 1;
    CHECK(tiku_richdoc_import(doc, &source, 0) == TIKU_RICH_OK);
    CHECK(tiku_richdoc_snapshot(doc, &s) == TIKU_RICH_OK);
    CHECK(tiku_richsnapshot_resolve(s, 0, 0, &chars, &props) == TIKU_RICH_OK);
    CHECK(chars.bold == 1);
    tiku_richsnapshot_release(s);
    tiku_richdoc_release(doc);
}

/** @brief Absent properties do not read poisoned values or font strings. */
static void
absent_properties(void)
{
    tiku_rich_paragraph_source_t p = {.text = "text", .length = 4};
    tiku_rich_source_t source = {.paragraphs = &p, .paragraph_count = 1};
    tiku_richdoc_t *doc;
    tiku_richsnapshot_t *s;
    tiku_rich_chars_t chars;
    tiku_rich_para_t props;

    memset(&p.chars, 0xff, sizeof p.chars);
    memset(&p.paragraph, 0xff, sizeof p.paragraph);
    p.chars.present = 0;
    p.paragraph.present = 0;
    CHECK(tiku_richdoc_create(NULL, &doc) == TIKU_RICH_OK);
    CHECK(tiku_richdoc_import(doc, &source, 0) == TIKU_RICH_OK);
    CHECK(tiku_richdoc_snapshot(doc, &s) == TIKU_RICH_OK);
    CHECK(tiku_richsnapshot_resolve(s, 0, 0, &chars, &props) == TIKU_RICH_OK);
    CHECK(!chars.present && !chars.bold && !chars.font[0] && !chars.size);
    CHECK(!props.present && !props.left);
    tiku_richsnapshot_release(s);
    tiku_richdoc_release(doc);
}

/** @brief The inheritance bound includes the leaf and its ancestors. */
static void
inheritance_depth(void)
{
    tiku_rich_style_source_t styles[65] = {0};
    tiku_rich_source_t source = {.styles = styles, .style_count = 64};
    tiku_richdoc_t *doc;
    size_t i;

    for (i = 0; i < 65; i++) {
        snprintf(styles[i].name, sizeof styles[i].name, "Style %zu", i);
        styles[i].parent = i;
    }
    CHECK(tiku_richdoc_create(NULL, &doc) == TIKU_RICH_OK);
    CHECK(tiku_richdoc_import(doc, &source, 0) == TIKU_RICH_OK);
    source.style_count = 65;
    CHECK(tiku_richdoc_import(doc, &source, 0) == TIKU_RICH_INVALID_STYLE);
    tiku_richdoc_release(doc);
}

/** @brief Run formatting and hostile-style checks. */
int
main(void)
{
    inheritance();
    invalid_styles();
    runs_and_empty();
    absent_properties();
    inheritance_depth();
    puts("PASS: rich document styles, explicit overrides and run validation");
    return 0;
}
