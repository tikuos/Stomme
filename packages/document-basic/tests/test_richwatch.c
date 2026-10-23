/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_richwatch.c - publication order, callback refusal and subscriptions.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richdoc_private.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "FAIL richwatch:%d: %s\n", __LINE__, #x); \
    exit(1); } } while (0)

/** @brief Callback expectations and handles used for reentrancy probes. */
typedef struct {
    tiku_richsession_t *session;
    tiku_richanchor_t *anchor;
    tiku_richwatch_t *watch;
    tiku_richfragment_t *fragment;
    tiku_rich_change_kind_t kind;
    uint64_t origin;
    size_t caret, calls;
    unsigned sequence;
} observer_t;

/** @brief Inspect committed state and attempt each public mutation family. */
static void
changed(void *ctx, tiku_richdoc_t *doc, const tiku_rich_change_t *change)
{
    observer_t *observer = ctx;
    tiku_richsnapshot_t *snapshot;
    tiku_richsession_t *session = (void *)1;
    tiku_richwatch_t *watch = (void *)1;
    tiku_richanchor_t *anchor = (void *)1;
    tiku_rich_pos_t first, last;
    tiku_richcmd_t cmd = {.kind = TIKU_RICHCMD_BOLD};
    tiku_rich_transaction_t transaction = {0};
    tiku_rich_source_t empty = {0};

    observer->calls++;
    CHECK(observer->sequence == 1);
    observer->sequence = 2;
    CHECK(change->kind == observer->kind && change->origin == observer->origin);
    CHECK(change->generation == doc->generation);
    CHECK(change->state == doc->root->state);
    CHECK(change->dirty == tiku_richdoc_dirty(doc));
    CHECK(tiku_richdoc_snapshot(doc, &snapshot) == TIKU_RICH_OK);
    CHECK(tiku_richsession_selection(observer->session, &first, &last) ==
          TIKU_RICH_OK);
    CHECK(tiku_richsnapshot_position(snapshot, first) == TIKU_RICH_OK);
    CHECK(tiku_richsnapshot_position(snapshot, last) == TIKU_RICH_OK);
    CHECK(first.byte == observer->caret && last.byte == observer->caret);
    CHECK(tiku_richdoc_apply(doc, &transaction) == TIKU_RICH_BUSY);
    CHECK(tiku_richdoc_import(doc, &empty, 0) == TIKU_RICH_BUSY);
    CHECK(tiku_richdoc_replace_range(doc, 0, first, last, "x", 1) ==
          TIKU_RICH_BUSY);
    CHECK(tiku_richdoc_format_range(doc, 0, first, last,
        &(tiku_rich_chars_t){0}) == TIKU_RICH_BUSY);
    CHECK(tiku_richdoc_paste_fragment(doc, 0, first, last,
                                      observer->fragment) == TIKU_RICH_BUSY);
    CHECK(tiku_richdoc_undo(doc) == TIKU_RICH_BUSY);
    CHECK(tiku_richdoc_redo(doc) == TIKU_RICH_BUSY);
    CHECK(tiku_richdoc_mark_saved(doc, snapshot) == TIKU_RICH_BUSY);
    CHECK(tiku_richsession_execute(observer->session, 0, &cmd) ==
          TIKU_RICH_BUSY);
    CHECK(tiku_richsession_select(observer->session, 0, first, last) ==
          TIKU_RICH_BUSY);
    CHECK(tiku_richsession_set_read_only(observer->session, 1) ==
          TIKU_RICH_BUSY);
    CHECK(tiku_richsession_destroy(observer->session) == TIKU_RICH_BUSY);
    CHECK(tiku_richsession_create(doc, &session) == TIKU_RICH_BUSY && !session);
    CHECK(tiku_richanchor_create(doc, first, &anchor) == TIKU_RICH_BUSY);
    CHECK(!anchor);
    CHECK(tiku_richanchor_set(observer->anchor, first) == TIKU_RICH_BUSY);
    CHECK(tiku_richwatch_create(doc, changed, ctx, &watch) == TIKU_RICH_BUSY);
    CHECK(!watch);
    CHECK(tiku_richwatch_destroy(observer->watch) == TIKU_RICH_BUSY);
    tiku_richdoc_release(doc);
    tiku_richanchor_release(observer->anchor);
    tiku_richdoc_clear_history(doc);
    tiku_richdoc_set_read_only(doc, !doc->read_only);
    tiku_richsnapshot_release(snapshot);
}

/** @brief Confirm newest-first delivery and busy command availability. */
static void
newest(void *ctx, tiku_richdoc_t *doc, const tiku_rich_change_t *change)
{
    observer_t *observer = ctx;
    tiku_richcmd_state_t state;
    tiku_richcmd_context_t context = {0};
    tiku_richcmd_t cmd = {.kind = TIKU_RICHCMD_UNDO};

    CHECK(!observer->sequence);
    observer->sequence = 1;
    CHECK(change->generation == tiku_richdoc_generation(doc));
    CHECK(tiku_richcmd_query(doc, &context, TIKU_RICHCMD_UNDO, &state) ==
          TIKU_RICH_OK && !state.enabled && state.status == TIKU_RICH_BUSY);
    CHECK(tiku_richcmd_execute(doc, &context, &cmd) == TIKU_RICH_BUSY);
    CHECK(tiku_richsession_query(observer->session, TIKU_RICHCMD_BOLD,
        &state) == TIKU_RICH_OK && !state.enabled &&
        state.status == TIKU_RICH_BUSY);
}

/** @brief Prepare callback assertions for the next expected notification. */
static void
expect(observer_t *observer, tiku_rich_change_kind_t kind,
       uint64_t origin, size_t caret)
{
    observer->kind = kind;
    observer->origin = origin;
    observer->caret = caret;
    observer->sequence = 0;
}

/** @brief Changes publish once; metadata callbacks do not increment content. */
static void
notifications(void)
{
    tiku_richdoc_t *doc;
    tiku_richwatch_t *watch;
    tiku_richsnapshot_t *snapshot;
    observer_t observer = {0};
    tiku_richcmd_t cmd = {.kind = TIKU_RICHCMD_REPLACE_TEXT,
        .value.text = {"abc", 3}};
    tiku_rich_pos_t pos;
    uint64_t generation, origin;
    size_t refs, used;

    CHECK(tiku_richdoc_create(NULL, &doc) == TIKU_RICH_OK);
    CHECK(tiku_richsession_create(doc, &observer.session) == TIKU_RICH_OK);
    CHECK(tiku_richsession_execute(observer.session, 0, &cmd) == TIKU_RICH_OK);
    pos = observer.session->caret->position;
    pos.byte = 0;
    CHECK(tiku_richdoc_snapshot(doc, &snapshot) == TIKU_RICH_OK);
    CHECK(tiku_richfragment_create(snapshot, pos,
        observer.session->caret->position, &observer.fragment) == TIKU_RICH_OK);
    tiku_richsnapshot_release(snapshot);
    CHECK(tiku_richanchor_create(doc, pos, &observer.anchor) == TIKU_RICH_OK);
    CHECK(tiku_richwatch_create(doc, changed, &observer, &observer.watch) ==
          TIKU_RICH_OK);
    CHECK(tiku_richwatch_create(doc, newest, &observer, &watch) == TIKU_RICH_OK);
    refs = doc->refs;
    origin = tiku_richsession_id(observer.session);
    expect(&observer, TIKU_RICH_CHANGE_EDIT, origin, 6);
    CHECK(tiku_richsession_execute(observer.session, 0, &cmd) == TIKU_RICH_OK);
    CHECK(observer.calls == 1 && observer.sequence == 2 && doc->refs == refs);
    CHECK(doc->undo && !doc->read_only && doc->anchor_count == 3);
    expect(&observer, TIKU_RICH_CHANGE_UNDO, origin, 3);
    CHECK(tiku_richdoc_undo(doc) == TIKU_RICH_OK);
    expect(&observer, TIKU_RICH_CHANGE_REDO, origin, 6);
    CHECK(tiku_richdoc_redo(doc) == TIKU_RICH_OK);
    CHECK(tiku_richdoc_snapshot(doc, &snapshot) == TIKU_RICH_OK);
    generation = doc->generation;
    expect(&observer, TIKU_RICH_CHANGE_SAVED, 0, 6);
    CHECK(tiku_richdoc_mark_saved(doc, snapshot) == TIKU_RICH_OK);
    CHECK(!tiku_richdoc_dirty(doc) && doc->generation == generation);
    CHECK(tiku_richdoc_mark_saved(doc, snapshot) == TIKU_RICH_OK);
    CHECK(observer.calls == 4);
    expect(&observer, TIKU_RICH_CHANGE_PERMISSION, 0, 6);
    tiku_richdoc_set_read_only(doc, 1);
    CHECK(doc->read_only && doc->generation == generation);
    CHECK(tiku_richsession_execute(observer.session, 0, &cmd) ==
          TIKU_RICH_READ_ONLY);
    CHECK(observer.calls == 5);
    tiku_richdoc_set_read_only(doc, 1);
    CHECK(observer.calls == 5);
    expect(&observer, TIKU_RICH_CHANGE_PERMISSION, 0, 6);
    tiku_richdoc_set_read_only(doc, 0);
    expect(&observer, TIKU_RICH_CHANGE_HISTORY, 0, 6);
    tiku_richdoc_clear_history(doc);
    CHECK(!doc->undo && !doc->redo && observer.calls == 7);
    tiku_richdoc_clear_history(doc);
    CHECK(observer.calls == 7);
    expect(&observer, TIKU_RICH_CHANGE_IMPORT, 0, 0);
    CHECK(tiku_richdoc_import(doc, &(tiku_rich_source_t){0}, 0) == TIKU_RICH_OK);
    CHECK(observer.calls == 8 && !tiku_richdoc_dirty(doc));
    used = tiku_richdoc_memory(doc);
    cmd.kind = TIKU_RICHCMD_BOLD;
    CHECK(tiku_richsession_execute(observer.session, 0, &cmd) == TIKU_RICH_OK);
    CHECK(observer.calls == 8 && tiku_richdoc_memory(doc) == used);
    CHECK(tiku_richwatch_destroy(watch) == TIKU_RICH_OK);
    CHECK(tiku_richwatch_destroy(observer.watch) == TIKU_RICH_OK);
    CHECK(!doc->watches && doc->refs == refs - 2);
    CHECK(tiku_richsession_destroy(observer.session) == TIKU_RICH_OK);
    tiku_richanchor_release(observer.anchor);
    tiku_richfragment_release(observer.fragment);
    tiku_richsnapshot_release(snapshot);
    tiku_richdoc_release(doc);
}

/** @brief Count notifications without retaining event data. */
static void
count(void *ctx, tiku_richdoc_t *doc, const tiku_rich_change_t *change)
{
    unsigned *calls = ctx;

    CHECK(change->generation == tiku_richdoc_generation(doc));
    (*calls)++;
}

/** @brief Bound registration, retain the document and refuse invalid input. */
static void
lifetime(void)
{
    tiku_richdoc_t *doc;
    tiku_richwatch_t *watches[1024], *bad = (void *)1;
    size_t i;
    unsigned calls = 0;

    CHECK(tiku_richdoc_create(NULL, &doc) == TIKU_RICH_OK);
    CHECK(tiku_richwatch_create(doc, NULL, NULL, &bad) ==
          TIKU_RICH_INVALID && !bad);
    CHECK(tiku_richwatch_create(NULL, count, &calls, &bad) ==
          TIKU_RICH_INVALID && !bad);
    CHECK(tiku_richwatch_create(doc, count, &calls, NULL) == TIKU_RICH_INVALID);
    for (i = 0; i < 1024; i++)
        CHECK(tiku_richwatch_create(doc, count, &calls, &watches[i]) ==
              TIKU_RICH_OK);
    CHECK(tiku_richwatch_create(doc, count, &calls, &bad) ==
          TIKU_RICH_LIMIT && !bad);
    tiku_richdoc_release(doc);
    tiku_richdoc_set_read_only(doc, 1);
    CHECK(calls == 1024);
    for (i = 0; i < 1024; i++)
        CHECK(tiku_richwatch_destroy(watches[i]) == TIKU_RICH_OK);
    CHECK(tiku_richwatch_destroy(NULL) == TIKU_RICH_OK);
}

/** @brief Run guarded publication and callback ownership checks. */
int
main(void)
{
    notifications();
    lifetime();
    puts("PASS: rich notifications, publication order and reentrancy guards");
    return 0;
}
