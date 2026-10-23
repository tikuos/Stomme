/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_richview.c - per-view reflow, navigation, stale geometry and scrolling.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richview.h"
#include "tiku_richwatch.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "FAIL richview:%d: %s\n", __LINE__, #x); \
    exit(1); } } while (0)

/** @brief Supply one fixed-width grapheme without a native font dependency. */
static tiku_rich_status_t
shape(void *ctx, const tiku_rich_chars_t *chars, const char *text,
      size_t length, const uint8_t *boundaries, tiku_rich_shape_t *out)
{
    size_t end = 1;

    (void)ctx; (void)chars; (void)text;
    while (end < length && !boundaries[end]) end++;
    *out = (tiku_rich_shape_t){.bytes = end, .glyph_count = 1,
        .caret_count = 2, .advance = 6, .ascent = 9, .descent = 3,
        .glyphs = {{1, 1, 0, 0}}, .carets = {{0, 0}, {end, 6}}};
    return TIKU_RICH_OK;
}

/** @brief Import a paragraph fixture without inserting through editor code. */
static tiku_richdoc_t *
document(void)
{
    tiku_richdoc_t *doc;
    tiku_rich_paragraph_source_t p[2] = {
        {.text = "a\xcc\x81" "bcd efghijkl", .length = 15},
        {.text = "tail", .length = 4}
    };
    tiku_rich_source_t source = {.paragraphs = p, .paragraph_count = 2};

    CHECK(tiku_richdoc_create(NULL, &doc) == TIKU_RICH_OK);
    CHECK(tiku_richdoc_import(doc, &source, 0) == TIKU_RICH_OK);
    return doc;
}

/** @brief Create a view with bounded geometry and no platform state. */
static tiku_richview_t *
view_for(tiku_richdoc_t *doc, double width)
{
    tiku_richview_t *view;
    tiku_richlayout_options_t options = {.width = width,
        .provider = {.shape = shape}};

    CHECK(tiku_richview_create(doc, &options, &view) == TIKU_RICH_OK);
    CHECK(!tiku_richview_layout(view));
    return view;
}

/** @brief Drain scheduled reflow using independently bounded event-loop work. */
static void
finish(tiku_richview_t *view)
{
    tiku_rich_status_t status;
    unsigned steps = 0;

    do {
        status = tiku_richview_step(view, 1);
        CHECK(++steps < 10000);
    } while (status == TIKU_RICH_PENDING);
    CHECK(status == TIKU_RICH_OK);
    CHECK(tiku_richview_layout(view));
}

/** @brief Return a current caret while checking the public session query. */
static tiku_rich_pos_t
caret(tiku_richview_t *view)
{
    tiku_rich_pos_t anchor, caret;

    CHECK(tiku_richsession_selection(tiku_richview_session(view), &anchor,
                                    &caret) == TIKU_RICH_OK);
    return caret;
}

/** @brief Move by graphemes, extend/collapse selections and cross paragraphs. */
static void
navigation(void)
{
    tiku_richdoc_t *doc = document();
    tiku_richview_t *view = view_for(doc, 24);
    tiku_rich_pos_t anchor, last, previous;

    CHECK(tiku_richview_move(view, TIKU_RICH_MOVE_RIGHT, 0) ==
          TIKU_RICH_PENDING);
    finish(view);
    tiku_rich_pos_t source = caret(view), snapped;
    source.byte = 1;
    CHECK(tiku_richlayout_snap(tiku_richview_layout(view), source, 0, &snapped) == TIKU_RICH_OK);
    CHECK(snapped.byte == 0);
    CHECK(tiku_richlayout_snap(tiku_richview_layout(view), source, 1, &snapped) == TIKU_RICH_OK);
    CHECK(snapped.byte == 3);
    source.byte = 3;
    CHECK(tiku_richlayout_snap(tiku_richview_layout(view), source, 0, &snapped) == TIKU_RICH_OK);
    CHECK(snapped.byte == 3);
    source.byte = 2;
    CHECK(tiku_richlayout_snap(tiku_richview_layout(view), source, 1, &snapped) == TIKU_RICH_INVALID_RANGE);
    CHECK(!snapped.paragraph);
    CHECK(tiku_richview_move(view, TIKU_RICH_MOVE_LEFT, 0) ==
          TIKU_RICH_NO_CHANGE);
    CHECK(tiku_richview_move(view, TIKU_RICH_MOVE_RIGHT, 0) == TIKU_RICH_OK);
    CHECK(caret(view).byte == 3);
    CHECK(tiku_richview_move(view, TIKU_RICH_MOVE_RIGHT, 1) == TIKU_RICH_OK);
    CHECK(caret(view).byte == 4);
    CHECK(tiku_richview_move(view, TIKU_RICH_MOVE_LEFT, 0) == TIKU_RICH_OK);
    CHECK(caret(view).byte == 3);
    CHECK(tiku_richview_move(view, TIKU_RICH_MOVE_END, 0) == TIKU_RICH_OK);
    CHECK(caret(view).byte == 6);
    CHECK(tiku_richview_move(view, TIKU_RICH_MOVE_DOWN, 0) == TIKU_RICH_OK);
    CHECK(caret(view).byte == 7);
    CHECK(tiku_richview_move(view, TIKU_RICH_MOVE_DOWN, 0) == TIKU_RICH_OK);
    CHECK(caret(view).byte == 11);
    CHECK(tiku_richview_move(view, TIKU_RICH_MOVE_FINISH, 0) == TIKU_RICH_OK);
    last = caret(view);
    CHECK(last.byte == 4);
    CHECK(tiku_richview_move(view, TIKU_RICH_MOVE_RIGHT, 0) ==
          TIKU_RICH_NO_CHANGE);
    CHECK(tiku_richview_move(view, TIKU_RICH_MOVE_HOME, 0) == TIKU_RICH_OK);
    CHECK(tiku_richview_move(view, TIKU_RICH_MOVE_LEFT, 0) == TIKU_RICH_OK);
    previous = caret(view);
    CHECK(previous.paragraph != last.paragraph && previous.byte == 15);
    CHECK(tiku_richview_move(view, TIKU_RICH_MOVE_RIGHT, 0) == TIKU_RICH_OK);
    CHECK(caret(view).paragraph == last.paragraph && !caret(view).byte);
    CHECK(tiku_richview_move(view, TIKU_RICH_MOVE_START, 0) == TIKU_RICH_OK);
    anchor = caret(view);
    last = anchor;
    last.byte = 1;
    CHECK(tiku_richsession_select(tiku_richview_session(view), 0, last, last) ==
          TIKU_RICH_OK);
    CHECK(tiku_richview_move(view, TIKU_RICH_MOVE_RIGHT, 0) ==
          TIKU_RICH_INVALID_RANGE);
    CHECK(caret(view).byte == 1);
    CHECK(tiku_richsession_select(tiku_richview_session(view), 0,
                                  anchor, anchor) == TIKU_RICH_OK);
    CHECK(tiku_richview_viewport(view, 24, 12, 1) == TIKU_RICH_OK);
    CHECK(tiku_richview_move(view, TIKU_RICH_MOVE_FINISH, 0) == TIKU_RICH_OK);
    CHECK(tiku_richview_scroll_y(view) > 0);
    tiku_richdoc_release(doc);
    CHECK(tiku_richview_destroy(view) == TIKU_RICH_OK);
}

/** @brief Inspect a view before its older invalidation observer is called. */
static void
inspect_publication(void *context, tiku_richdoc_t *doc,
                    const tiku_rich_change_t *change)
{
    tiku_richview_t *view = context;

    (void)doc;
    if (change->kind > TIKU_RICH_CHANGE_IMPORT) return;
    CHECK(!tiku_richview_layout(view));
    CHECK(tiku_richview_status(view) == TIKU_RICH_PENDING);
    CHECK(tiku_richview_point(view, 0, 0, 0) == TIKU_RICH_BUSY);
    CHECK(tiku_richview_destroy(view) == TIKU_RICH_BUSY);
}

/** @brief Edits invalidate both views; pending jobs never supply click geometry. */
static void
multiple_views(void)
{
    tiku_richdoc_t *doc = document();
    tiku_richview_t *a = view_for(doc, 24), *b = view_for(doc, 48);
    tiku_richwatch_t *watch;
    tiku_rich_pos_t pos;
    tiku_richcmd_t undo = {.kind = TIKU_RICHCMD_UNDO};

    finish(a); finish(b);
    CHECK(tiku_richlayout_lines(tiku_richview_layout(a)) >
          tiku_richlayout_lines(tiku_richview_layout(b)));
    CHECK(tiku_richwatch_create(doc, inspect_publication, b, &watch) ==
          TIKU_RICH_OK);
    CHECK(tiku_richsession_type(tiku_richview_session(a), 0, "X", 1) ==
          TIKU_RICH_OK);
    CHECK(!tiku_richview_layout(a) && !tiku_richview_layout(b));
    CHECK(tiku_richview_point(b, 0, 0, 0) == TIKU_RICH_PENDING);
    CHECK(tiku_richview_step(b, 3) == TIKU_RICH_PENDING);
    CHECK(tiku_richsession_type(tiku_richview_session(a), 0, "Y", 1) ==
          TIKU_RICH_OK);
    finish(a); finish(b);
    CHECK(tiku_richlayout_generation(tiku_richview_layout(b)) ==
          tiku_richdoc_generation(doc));
    CHECK(caret(a).byte == 2);
    CHECK(tiku_richsession_execute(tiku_richview_session(a), 0, &undo) ==
          TIKU_RICH_OK);
    finish(a); finish(b);
    CHECK(!caret(a).byte);
    CHECK(tiku_richview_viewport(b, 96, 24, 2) == TIKU_RICH_OK);
    finish(b);
    CHECK(tiku_richview_zoom(b) == 2);
    CHECK(tiku_richview_point(b, 12, 0, 0) == TIKU_RICH_OK);
    pos = caret(b);
    CHECK(pos.byte == 3);
    CHECK(tiku_richview_point(b, 24, 0, 1) == TIKU_RICH_OK);
    CHECK(caret(b).byte == 4);
    CHECK(!caret(a).byte);
    CHECK(tiku_richsession_set_read_only(tiku_richview_session(b), 1) ==
          TIKU_RICH_OK);
    CHECK(tiku_richsession_type(tiku_richview_session(b), 0, "!", 1) ==
          TIKU_RICH_READ_ONLY);
    CHECK(tiku_richview_layout(b));
    CHECK(tiku_richview_move(b, TIKU_RICH_MOVE_RIGHT, 0) == TIKU_RICH_OK);
    CHECK(tiku_richview_scroll(b, 1e10) == TIKU_RICH_OK);
    CHECK(tiku_richview_scroll_y(b) < 100);
    CHECK(tiku_richview_scroll(b, NAN) == TIKU_RICH_INVALID);
    tiku_richwatch_destroy(watch);
    tiku_richview_destroy(a);
    tiku_richview_destroy(b);
    tiku_richdoc_release(doc);
}

/** @brief Compare repeated resize/zoom layouts with independent fresh jobs. */
static void
resizing(void)
{
    tiku_richdoc_t *doc = document();
    tiku_richview_t *view = view_for(doc, 60);
    tiku_richsnapshot_t *snapshot;
    tiku_richlayout_options_t options = {.provider = {.shape = shape}};
    tiku_richlayout_t *oracle;
    tiku_rich_line_t a, b;
    size_t i, j;
    double width, zoom;

    CHECK(tiku_richdoc_snapshot(doc, &snapshot) == TIKU_RICH_OK);
    for (i = 0; i < 1000; i++) {
        width = 12 + i % 97;
        zoom = 0.5 + (i % 8) * 0.25;
        CHECK(tiku_richview_viewport(view, width, 12 + i % 41, zoom) ==
              TIKU_RICH_OK);
        finish(view);
        options.width = width / zoom;
        CHECK(tiku_richlayout_create(snapshot, &options, &oracle) ==
              TIKU_RICH_OK);
        CHECK(tiku_richlayout_step(oracle, 10000) == TIKU_RICH_OK);
        CHECK(tiku_richlayout_lines(oracle) ==
              tiku_richlayout_lines(tiku_richview_layout(view)));
        for (j = 0; j < tiku_richlayout_lines(oracle); j++) {
            tiku_richlayout_line(oracle, j, &a);
            tiku_richlayout_line(tiku_richview_layout(view), j, &b);
            CHECK(!memcmp(&a, &b, sizeof a));
        }
        tiku_richlayout_release(oracle);
    }
    tiku_richsnapshot_release(snapshot);
    tiku_richview_destroy(view);
    tiku_richdoc_release(doc);
}

/** @brief Navigation at the end of a document also ends a typing group. */
static void
typing_boundary(void)
{
    tiku_richdoc_t *doc = document();
    tiku_richview_t *view = view_for(doc, 100);
    tiku_richsession_t *session = tiku_richview_session(view);
    tiku_richcmd_t undo = {.kind = TIKU_RICHCMD_UNDO};

    finish(view);
    CHECK(tiku_richview_move(view, TIKU_RICH_MOVE_FINISH, 0) == TIKU_RICH_OK);
    CHECK(tiku_richsession_type(session, 0, "X", 1) == TIKU_RICH_OK);
    finish(view);
    CHECK(tiku_richview_move(view, TIKU_RICH_MOVE_RIGHT, 0) ==
          TIKU_RICH_NO_CHANGE);
    CHECK(tiku_richsession_type(session, 0, "Y", 1) == TIKU_RICH_OK);
    CHECK(tiku_richsession_execute(session, 0, &undo) == TIKU_RICH_OK);
    CHECK(caret(view).byte == 5);
    tiku_richview_destroy(view);
    tiku_richdoc_release(doc);
}

/** @brief Run navigation, independent view and full-reflow equivalence tests. */
int
main(void)
{
    navigation();
    multiple_views();
    resizing();
    typing_boundary();
    puts("PASS richview (navigation, multi-view, 1,000 resize/zoom oracles)");
    return 0;
}
