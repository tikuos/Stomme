/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_editdoc_history.c - transactions and their history: whole or
 * nothing, undo and redo, branches, typing joined into one step and where
 * it stops joining, the saved state, and a budget kept by whole steps.
 *
 * The upstream cases adapt Scintilla 5.6.7 test/unit/testCellBuffer.cxx
 * (CellBuffer, UndoHistory) and testDocument.cxx (DocumentUndo), Copyright
 * Neil Hodgson; see kits/editor/LICENSE.Scintilla and
 * kits/editor/UPSTREAM.md.
 *
 * SPDX-License-Identifier: Apache-2.0 AND LicenseRef-Scintilla
 */

#include <stdio.h>
#include <string.h>

#include "tiku_editdoc_private.h"

static int fails;

/** @brief Count a failure and say which contract row it belongs to. */
static void
check(int cond, const char *row, const char *what)
{
    if (!cond) {
        printf("  [FAIL] %-9s %s\n", row, what);
        fails++;
    }
}

/** @brief Whether the whole document reads back as @p s. */
static int
reads(const tiku_editdoc_t *doc, const char *s)
{
    static char buf[16384];
    size_t n = 0;
    tiku_edit_range_t all = { 0, tiku_editdoc_length(doc) };

    return tiku_editdoc_read(doc, all, buf, sizeof buf, &n) == TIKU_EDIT_OK &&
           n == strlen(s) && !memcmp(buf, s, n);
}

/** @brief A new document holding @p s, saved. */
static tiku_editdoc_t *
doc_of(const char *s, size_t history)
{
    tiku_editdoc_limits_t lim = { 0, history };
    tiku_editdoc_t *doc = NULL;

    check(tiku_editdoc_create(&lim, &doc) == TIKU_EDIT_OK, "DOC-001",
          "create");
    check(tiku_editdoc_load(doc, s, strlen(s)) == TIKU_EDIT_OK, "DOC-002",
          "load");
    return doc;
}

/** @brief Replace [s, e) by @p text as one transaction. */
static tiku_edit_status_t
put(tiku_editdoc_t *doc, int64_t s, int64_t e, const char *text,
    unsigned flags, tiku_edit_view_t view)
{
    tiku_edit_replace_t r = { { s, e }, text, strlen(text) };
    tiku_edit_transaction_t t;

    memset(&t, 0, sizeof t);
    t.edits = &r;
    t.count = 1;
    t.flags = flags;
    t.view = view;
    return tiku_editdoc_apply(doc, &t);
}

/** @brief How many undo steps the history holds, counted by undoing them
 *         all and redoing them again. */
static int
steps(tiku_editdoc_t *doc)
{
    int n = 0, i;

    while (tiku_editdoc_undo(doc) == TIKU_EDIT_OK)
        n++;
    for (i = 0; i < n; i++)
        tiku_editdoc_redo(doc);
    return n;
}

static const tiku_edit_view_t none = { 0, 0 };

/** @brief Single and multiple replacements, whole. */
static void
transactions(void)
{
    tiku_editdoc_t *doc = doc_of("abc", 0);
    uint64_t generation = tiku_editdoc_generation(doc);
    uint64_t state = tiku_editdoc_state(doc);
    tiku_edit_replace_t multi[] = {
        { { 0, 0 }, ">", 1 }, { { 1, 2 }, "XY", 2 }, { { 3, 3 }, "<", 1 }
    };
    tiku_edit_transaction_t t;

    check(put(doc, 1, 2, "X", 0, none) == TIKU_EDIT_OK && reads(doc, "aXc") &&
          tiku_editdoc_generation(doc) == generation + 1 &&
          tiku_editdoc_state(doc) != state && tiku_editdoc_modified(doc) &&
          tiku_editdoc_can_undo(doc), "TXN-001",
          "one replacement: text, generation, state, modified");
    tiku_editdoc_release(doc);

    doc = doc_of("abc", 0);
    memset(&t, 0, sizeof t);
    t.edits = multi;
    t.count = 3;
    check(tiku_editdoc_apply(doc, &t) == TIKU_EDIT_OK && reads(doc, ">aXYc<") &&
          steps(doc) == 1, "TXN-002",
          "three replacements in the coordinates before, one step");
    check(tiku_editdoc_undo(doc) == TIKU_EDIT_OK && reads(doc, "abc"),
          "TXN-002", "undone whole");
    tiku_editdoc_release(doc);
}

/** @brief Every refusal leaves text, generation, state and history. */
static void
refusals(void)
{
    static const struct {
        tiku_edit_replace_t a, b;
        int count;
        tiku_edit_status_t want;
        const char *why;
    } cases[] = {
        { { { 0, 2 }, "", 0 }, { { 1, 3 }, "", 0 }, 2,
          TIKU_EDIT_INVALID_RANGE, "overlapping ranges" },
        { { { 1, 1 }, "x", 1 }, { { 1, 1 }, "y", 1 }, 2,
          TIKU_EDIT_INVALID_RANGE, "two insertions at one point" },
        { { { 1, 1 }, "x", 1 }, { { 1, 2 }, "", 0 }, 2,
          TIKU_EDIT_INVALID_RANGE, "an insertion at a deletion's start" },
        { { { 2, 3 }, "", 0 }, { { 0, 1 }, "", 0 }, 2,
          TIKU_EDIT_INVALID_RANGE, "descending order" },
        { { { 2, 1 }, "", 0 }, { { 0, 0 }, "", 0 }, 1,
          TIKU_EDIT_INVALID_RANGE, "a reversed range" },
        { { { 4, 6 }, "", 0 }, { { 0, 0 }, "", 0 }, 1,
          TIKU_EDIT_INVALID_RANGE, "past the end" },
        { { { 4, 5 }, "", 0 }, { { 0, 0 }, "", 0 }, 1,
          TIKU_EDIT_INVALID_RANGE, "inside a character" },
        { { { 0, 0 }, "\xff", 1 }, { { 0, 0 }, "", 0 }, 1,
          TIKU_EDIT_INVALID_TEXT, "malformed text" },
        { { { 0, 0 }, "a\0", 2 }, { { 0, 0 }, "", 0 }, 1,
          TIKU_EDIT_INVALID_TEXT, "a NUL" },
        { { { 0, 0 }, NULL, 1 }, { { 0, 0 }, "", 0 }, 1,
          TIKU_EDIT_INVALID, "no text for a length" },
        { { { 0, 1 }, "ok", 2 }, { { 2, 3 }, "\xc3", 1 }, 2,
          TIKU_EDIT_INVALID_TEXT, "the second of two refused" }
    };
    tiku_editdoc_t *doc = doc_of("abc\xc3\xa9", 0);
    uint64_t generation = tiku_editdoc_generation(doc);
    uint64_t state = tiku_editdoc_state(doc);
    tiku_edit_transaction_t t;
    size_t i;

    for (i = 0; i < sizeof cases / sizeof *cases; i++) {
        tiku_edit_replace_t pair[2];

        pair[0] = cases[i].a;
        pair[1] = cases[i].b;
        memset(&t, 0, sizeof t);
        t.edits = pair;
        t.count = (size_t)cases[i].count;
        check(tiku_editdoc_apply(doc, &t) == cases[i].want, "TXN-003",
              cases[i].why);
        check(reads(doc, "abc\xc3\xa9") &&
              tiku_editdoc_generation(doc) == generation &&
              tiku_editdoc_state(doc) == state && !tiku_editdoc_can_undo(doc),
              "TXN-003", "nothing changed");
    }
    check(tiku_editdoc_apply(doc, NULL) == TIKU_EDIT_INVALID, "TXN-003",
          "no transaction");
    memset(&t, 0, sizeof t);
    t.count = 1;
    check(tiku_editdoc_apply(doc, &t) == TIKU_EDIT_INVALID, "TXN-003",
          "a count without edits");
    tiku_editdoc_release(doc);
}

/** @brief Adjacent ranges, no-ops, and the expected generation. */
static void
edges(void)
{
    tiku_editdoc_t *doc = doc_of("abcd", 0);
    tiku_edit_replace_t adjacent[] = {
        { { 0, 1 }, "A", 1 }, { { 1, 2 }, "B", 1 }
    };
    tiku_edit_replace_t noop[] = { { { 1, 1 }, "", 0 } };
    tiku_edit_replace_t mixed[] = {
        { { 0, 0 }, "", 0 }, { { 2, 3 }, "C", 1 }, { { 3, 3 }, "", 0 }
    };
    tiku_edit_transaction_t t;
    uint64_t generation;

    memset(&t, 0, sizeof t);
    t.edits = adjacent;
    t.count = 2;
    check(tiku_editdoc_apply(doc, &t) == TIKU_EDIT_OK && reads(doc, "ABcd") &&
          steps(doc) == 1, "TXN-004", "adjacent ranges are apart");
    generation = tiku_editdoc_generation(doc);
    t.edits = noop;
    t.count = 1;
    check(tiku_editdoc_apply(doc, &t) == TIKU_EDIT_OK &&
          tiku_editdoc_generation(doc) == generation && steps(doc) == 1,
          "TXN-005", "a no-op changes nothing and records nothing");
    t.edits = mixed;
    t.count = 3;
    check(tiku_editdoc_apply(doc, &t) == TIKU_EDIT_OK && reads(doc, "ABCd") &&
          steps(doc) == 2, "TXN-005", "no-ops among real edits are skipped");
    generation = tiku_editdoc_generation(doc);
    t.edits = adjacent;
    t.count = 1;
    t.generation = generation - 1;
    check(tiku_editdoc_apply(doc, &t) == TIKU_EDIT_STALE &&
          reads(doc, "ABCd"), "TXN-006", "a stale generation is refused");
    t.generation = generation;
    check(tiku_editdoc_apply(doc, &t) == TIKU_EDIT_OK, "TXN-006",
          "the current generation is accepted");
    tiku_editdoc_release(doc);
}

/** @brief The selection a transaction sets, checked in the text after. */
static void
selection_after(void)
{
    tiku_editdoc_t *doc = doc_of("x\r", 0);
    tiku_edit_view_t view;
    tiku_edit_replace_t e = { { 0, 0 }, "\xc3\xa9", 2 };
    tiku_edit_replace_t lf = { { 4, 4 }, "\n", 1 };
    tiku_edit_transaction_t t;
    tiku_edit_pos_t anchor, caret;

    check(tiku_editdoc_view_attach(doc, &view) == TIKU_EDIT_OK, "TXN-007",
          "a view");
    memset(&t, 0, sizeof t);
    t.edits = &e;
    t.count = 1;
    t.view = view;
    t.select = 1;
    t.anchor = t.caret = 1;
    check(tiku_editdoc_apply(doc, &t) == TIKU_EDIT_INVALID_RANGE &&
          reads(doc, "x\r"), "TXN-007",
          "a caret inside the inserted character is refused");
    t.anchor = t.caret = 2;
    check(tiku_editdoc_apply(doc, &t) == TIKU_EDIT_OK &&
          tiku_editdoc_view_selection(doc, view, &anchor, &caret) ==
          TIKU_EDIT_OK && anchor == 2 && caret == 2, "TXN-007",
          "the selection set after the edit");
    t.edits = &lf;
    t.anchor = t.caret = 4;
    check(tiku_editdoc_apply(doc, &t) == TIKU_EDIT_INVALID_RANGE &&
          reads(doc, "\xc3\xa9x\r"), "TXN-007",
          "a caret between the CR and the LF the edit joins is refused");
    t.view = none;
    t.anchor = t.caret = 0;
    check(tiku_editdoc_apply(doc, &t) == TIKU_EDIT_INVALID, "TXN-007",
          "a selection needs a view");
    tiku_editdoc_release(doc);
}

/** @brief Undo, redo, the ends of the history and the lines with them. */
static void
undo_redo(void)
{
    tiku_editdoc_t *doc = doc_of("a\nb", 0);
    uint64_t generation;

    check(put(doc, 1, 2, "", 0, none) == TIKU_EDIT_OK && reads(doc, "ab") &&
          tiku_editdoc_lines(doc) == 1, "HIS-001", "an edit joining lines");
    check(put(doc, 2, 2, "\r\nc", 0, none) == TIKU_EDIT_OK &&
          reads(doc, "ab\r\nc") && tiku_editdoc_lines(doc) == 2, "HIS-001",
          "an edit adding a line");
    generation = tiku_editdoc_generation(doc);
    check(tiku_editdoc_undo(doc) == TIKU_EDIT_OK && reads(doc, "ab") &&
          tiku_editdoc_lines(doc) == 1 &&
          tiku_editdoc_generation(doc) == generation + 1, "HIS-001",
          "undo: text, lines and generation");
    check(tiku_editdoc_undo(doc) == TIKU_EDIT_OK && reads(doc, "a\nb") &&
          tiku_editdoc_lines(doc) == 2, "HIS-001", "undo to the start");
    check(tiku_editdoc_undo(doc) == TIKU_EDIT_NOTHING &&
          !tiku_editdoc_can_undo(doc) && tiku_editdoc_can_redo(doc),
          "HIS-001", "nothing further back");
    check(tiku_editdoc_redo(doc) == TIKU_EDIT_OK &&
          tiku_editdoc_redo(doc) == TIKU_EDIT_OK && reads(doc, "ab\r\nc") &&
          tiku_editdoc_redo(doc) == TIKU_EDIT_NOTHING, "HIS-001",
          "redo to the end, then nothing");
    tiku_editdoc_release(doc);
}

/** @brief Modified follows the state, whichever way it is reached. */
static void
saved_state(void)
{
    tiku_editdoc_t *doc = doc_of("a", 0);
    uint64_t base = tiku_editdoc_state(doc), s1, s2, sa, sb;

    check(!tiku_editdoc_modified(doc) && put(doc, 1, 1, "b", 0, none) == 0 &&
          tiku_editdoc_modified(doc), "HIS-002", "an edit modifies");
    check(tiku_editdoc_undo(doc) == TIKU_EDIT_OK &&
          !tiku_editdoc_modified(doc) &&
          tiku_editdoc_state(doc) == base, "HIS-002",
          "undoing back to the saved state clears modified");
    check(tiku_editdoc_redo(doc) == TIKU_EDIT_OK && tiku_editdoc_modified(doc),
          "HIS-002", "redoing past it sets it again");
    s1 = tiku_editdoc_state(doc);
    tiku_editdoc_saved(doc, s1);
    check(!tiku_editdoc_modified(doc) && tiku_editdoc_undo(doc) == 0 &&
          tiku_editdoc_modified(doc) && tiku_editdoc_redo(doc) == 0 &&
          !tiku_editdoc_modified(doc), "HIS-002", "saved moves with the save");

    /* A save completion names the state its snapshot held. */
    check(put(doc, 2, 2, "c", 0, none) == TIKU_EDIT_OK, "HIS-010", "edit");
    s2 = tiku_editdoc_state(doc);
    check(put(doc, 3, 3, "d", 0, none) == TIKU_EDIT_OK, "HIS-010", "edit");
    tiku_editdoc_saved(doc, s2);
    check(tiku_editdoc_modified(doc) && tiku_editdoc_undo(doc) == 0 &&
          !tiku_editdoc_modified(doc) && tiku_editdoc_undo(doc) == 0 &&
          tiku_editdoc_modified(doc), "HIS-010",
          "a save of an older state leaves later edits modified");
    tiku_editdoc_release(doc);

    /* A branch drops the redo steps; their states never come back. */
    doc = doc_of("", 0);
    check(put(doc, 0, 0, "A", 0, none) == 0, "HIS-003", "A");
    sa = tiku_editdoc_state(doc);
    check(put(doc, 1, 1, "B", 0, none) == 0, "HIS-003", "B");
    sb = tiku_editdoc_state(doc);
    tiku_editdoc_saved(doc, sb);
    check(tiku_editdoc_undo(doc) == 0 && put(doc, 1, 1, "C", 0, none) == 0 &&
          !tiku_editdoc_can_redo(doc) && reads(doc, "AC"), "HIS-003",
          "a new edit after undo drops the redo branch");
    check(tiku_editdoc_state(doc) != sb && tiku_editdoc_state(doc) != sa &&
          tiku_editdoc_modified(doc) && tiku_editdoc_undo(doc) == 0 &&
          tiku_editdoc_state(doc) == sa && tiku_editdoc_modified(doc),
          "HIS-003", "the dropped state is never reached again");
    tiku_editdoc_release(doc);
}

/** @brief Typing joins into one step, and the things that stop it. */
static void
typing(void)
{
    tiku_editdoc_t *doc = doc_of("", 0);
    tiku_edit_view_t v1, v2;
    tiku_edit_replace_t paste = { { 1, 1 }, "xyz", 3 };
    tiku_edit_transaction_t t;

    /* UndoHistory/Coalesce: "ab" at 0 then "cd" at 2 are one step. */
    check(tiku_editdoc_view_attach(doc, &v1) == 0 &&
          tiku_editdoc_view_attach(doc, &v2) == 0, "HIS-004", "views");
    check(put(doc, 0, 0, "ab", TIKU_EDIT_TYPING, v1) == 0 &&
          put(doc, 2, 2, "cd", TIKU_EDIT_TYPING, v1) == 0 &&
          reads(doc, "abcd") && steps(doc) == 1, "UndoHistory/Coalesce",
          "adjacent typing is one step");
    check(tiku_editdoc_undo(doc) == 0 && reads(doc, "") &&
          !tiku_editdoc_modified(doc) && tiku_editdoc_redo(doc) == 0 &&
          reads(doc, "abcd") && tiku_editdoc_modified(doc),
          "UndoHistory/Coalesce", "undone and redone whole");
    tiku_editdoc_release(doc);

    /* Each boundary sits between two typings that would otherwise join;
     * steps() is counted only at the end, as it undoes and redoes. */
    doc = doc_of("", 0);
    tiku_editdoc_view_attach(doc, &v1);
    tiku_editdoc_view_attach(doc, &v2);
    put(doc, 0, 0, "a", TIKU_EDIT_TYPING, v1);            /* step 1 */
    put(doc, 0, 0, "b", TIKU_EDIT_TYPING, v1);            /* 2: elsewhere */
    put(doc, 1, 1, "c", TIKU_EDIT_TYPING, v1);            /* joins 2 */
    tiku_editdoc_seal(doc);
    put(doc, 2, 2, "d", TIKU_EDIT_TYPING, v1);            /* 3: sealed */
    put(doc, 3, 3, "e", TIKU_EDIT_TYPING, v1);            /* joins 3 */
    tiku_editdoc_view_select(doc, v1, 4, 4);
    put(doc, 4, 4, "f", TIKU_EDIT_TYPING, v1);            /* 4: caret moved */
    put(doc, 5, 5, "g", TIKU_EDIT_TYPING, v2);            /* 5: other view */
    put(doc, 6, 6, "h", TIKU_EDIT_TYPING, v2);            /* joins 5 */
    tiku_editdoc_saved(doc, tiku_editdoc_state(doc));
    put(doc, 7, 7, "i", TIKU_EDIT_TYPING, v2);            /* 6: after save */
    put(doc, 8, 8, "j", TIKU_EDIT_TYPING, v2);            /* joins 6 */
    memset(&t, 0, sizeof t);
    paste.range.start = paste.range.end = 9;
    t.edits = &paste;
    t.count = 1;
    t.view = v2;
    check(tiku_editdoc_apply(doc, &t) == 0, "HIS-005", "paste");  /* 7 */
    put(doc, 12, 12, "l", TIKU_EDIT_TYPING, v2);          /* 8: after paste */
    put(doc, 13, 13, "m", TIKU_EDIT_TYPING, v2);          /* joins 8 */
    check(reads(doc, "bcdefghijxyzlma") && steps(doc) == 8, "HIS-005",
          "elsewhere, seal, caret moved, other view, save and paste each "
          "start a step");
    check(tiku_editdoc_undo(doc) == 0 && tiku_editdoc_undo(doc) == 0 &&
          tiku_editdoc_modified(doc) && tiku_editdoc_undo(doc) == 0 &&
          !tiku_editdoc_modified(doc) && reads(doc, "bcdefgha"), "HIS-005",
          "three undos reach the saved state exactly");
    tiku_editdoc_release(doc);

    doc = doc_of("", 0);
    tiku_editdoc_view_attach(doc, &v1);
    put(doc, 0, 0, "a", TIKU_EDIT_TYPING, v1);
    check(tiku_editdoc_undo(doc) == 0 && tiku_editdoc_redo(doc) == 0 &&
          put(doc, 1, 1, "b", TIKU_EDIT_TYPING, v1) == 0 && steps(doc) == 2,
          "HIS-005", "undo and redo end a step");
    tiku_editdoc_release(doc);
}

/** @brief Backspace and Delete join; a character or a CRLF is one. */
static void
deleting(void)
{
    tiku_editdoc_t *doc = doc_of("abcd", 0);
    tiku_edit_view_t v;

    tiku_editdoc_view_attach(doc, &v);
    put(doc, 3, 4, "", TIKU_EDIT_TYPING, v);
    put(doc, 2, 3, "", TIKU_EDIT_TYPING, v);
    put(doc, 1, 2, "", TIKU_EDIT_TYPING, v);
    check(reads(doc, "a") && steps(doc) == 1 && tiku_editdoc_undo(doc) == 0 &&
          reads(doc, "abcd"), "HIS-006", "backspaces join");
    tiku_editdoc_release(doc);

    doc = doc_of("abcd", 0);
    tiku_editdoc_view_attach(doc, &v);
    put(doc, 1, 2, "", TIKU_EDIT_TYPING, v);
    put(doc, 1, 2, "", TIKU_EDIT_TYPING, v);
    check(reads(doc, "ad") && steps(doc) == 1 && tiku_editdoc_undo(doc) == 0 &&
          reads(doc, "abcd"), "HIS-006", "forward deletes join");
    tiku_editdoc_release(doc);

    doc = doc_of("a\r\nb\xc3\xa9", 0);
    tiku_editdoc_view_attach(doc, &v);
    put(doc, 4, 6, "", TIKU_EDIT_TYPING, v);
    put(doc, 3, 4, "", TIKU_EDIT_TYPING, v);
    put(doc, 1, 3, "", TIKU_EDIT_TYPING, v);
    check(reads(doc, "a") && steps(doc) == 1 && tiku_editdoc_undo(doc) == 0 &&
          reads(doc, "a\r\nb\xc3\xa9"), "HIS-006",
          "a two-byte character and a CRLF each delete as one");
    tiku_editdoc_release(doc);

    doc = doc_of("abcd", 0);
    tiku_editdoc_view_attach(doc, &v);
    put(doc, 2, 4, "", TIKU_EDIT_TYPING, v);
    put(doc, 1, 2, "", TIKU_EDIT_TYPING, v);
    put(doc, 1, 1, "x", TIKU_EDIT_TYPING, v);
    put(doc, 1, 2, "", TIKU_EDIT_TYPING, v);
    check(reads(doc, "a") && steps(doc) == 4, "HIS-006",
          "a selection deleted, typing, and deleting after typing are "
          "separate steps");
    tiku_editdoc_release(doc);
}

/** @brief Typing over a selection starts a step that later typing joins;
 *         undo and redo restore that view's selections. */
static void
over_selection(void)
{
    tiku_editdoc_t *doc = doc_of("hello", 0);
    tiku_edit_view_t v, other;
    tiku_edit_replace_t r = { { 0, 5 }, "J", 1 };
    tiku_edit_transaction_t t;
    tiku_edit_pos_t a, c;

    tiku_editdoc_view_attach(doc, &v);
    tiku_editdoc_view_attach(doc, &other);
    tiku_editdoc_view_select(doc, v, 0, 5);
    tiku_editdoc_view_select(doc, other, 5, 5);
    memset(&t, 0, sizeof t);
    t.edits = &r;
    t.count = 1;
    t.view = v;
    t.select = 1;
    t.anchor = t.caret = 1;
    t.flags = TIKU_EDIT_TYPING;
    check(tiku_editdoc_apply(doc, &t) == 0, "HIS-007", "typed over");
    r.range.start = r.range.end = 1;
    r.text = "o";
    t.anchor = t.caret = 2;
    check(tiku_editdoc_apply(doc, &t) == 0 && reads(doc, "Jo") &&
          steps(doc) == 1, "HIS-007", "later typing joins the step");
    check(tiku_editdoc_view_selection(doc, other, &a, &c) == 0 && a == 0 &&
          c == 0, "HIS-009", "another view's caret at the replaced end "
          "collapses to its start, as upstream's selections do");
    check(tiku_editdoc_undo(doc) == 0 && reads(doc, "hello") &&
          tiku_editdoc_view_selection(doc, v, &a, &c) == 0 && a == 0 && c == 5,
          "HIS-007", "undo restores the text and the selection before");
    check(tiku_editdoc_view_selection(doc, other, &a, &c) == 0 && a == 0 &&
          c == 0, "HIS-009", "the other view maps, not restores");
    check(tiku_editdoc_redo(doc) == 0 &&
          tiku_editdoc_view_selection(doc, v, &a, &c) == 0 && a == 2 && c == 2,
          "HIS-009", "redo restores the selection after");
    tiku_editdoc_view_detach(doc, v);
    check(tiku_editdoc_undo(doc) == 0 && reads(doc, "hello") &&
          tiku_editdoc_view_selection(doc, v, &a, &c) == TIKU_EDIT_INVALID,
          "HIS-009", "a detached view's steps still undo");
    tiku_editdoc_release(doc);
}

/** @brief The budget drops whole old steps and keeps the newest. */
static void
pruning(void)
{
    static char big[6000], chunk[400];
    tiku_editdoc_t *doc = doc_of("", 2048);
    int i;

    memset(chunk, 'x', sizeof chunk - 1);
    for (i = 0; i < 10; i++)
        check(put(doc, 0, 0, chunk, 0, none) == 0, "HIS-008", "edits");
    i = steps(doc);
    check(i >= 1 && i < 10, "HIS-008", "old steps dropped to the budget");
    while (tiku_editdoc_undo(doc) == 0)
        ;
    check(tiku_editdoc_length(doc) == (tiku_edit_pos_t)(10 - i) * 399 &&
          tiku_editdoc_modified(doc), "HIS-008",
          "undo stops at the oldest kept step, still modified");
    tiku_editdoc_release(doc);

    doc = doc_of("", 2048);
    memset(big, 'y', sizeof big - 1);
    check(put(doc, 0, 0, big, 0, none) == 0 && tiku_editdoc_can_undo(doc) &&
          tiku_editdoc_undo(doc) == 0 && tiku_editdoc_length(doc) == 0,
          "HIS-008", "a step bigger than the budget is still kept");
    tiku_editdoc_release(doc);
}

/** @brief testCellBuffer.cxx, CellBuffer: the undo side of its cases, each
 *         on a fresh document.  Upstream's returned change pointers and
 *         step counts have no counterpart; DeleteUndoHistory becomes a
 *         load, the one thing that clears history here. */
static void
upstream_cell_buffer(void)
{
    const char *name = "CellBuffer/UndoRedo";
    tiku_editdoc_t *doc = doc_of("", 0);

    check(put(doc, 0, 0, "Scintilla", 0, none) == 0 &&
          tiku_editdoc_can_undo(doc) && !tiku_editdoc_can_redo(doc),
          "CellBuffer/InsertOneLine", "undoable, nothing to redo");
    tiku_editdoc_release(doc);
    doc = doc_of("", 0);
    check(put(doc, 0, 0, "Two\nLines", 0, none) == 0 &&
          tiku_editdoc_can_undo(doc) && !tiku_editdoc_can_redo(doc),
          "CellBuffer/InsertTwoLines", "undoable, nothing to redo");
    tiku_editdoc_release(doc);

    doc = doc_of("", 0);
    check(put(doc, 0, 0, "Scintilla", 0, none) == 0 &&
          put(doc, 1, 3, "", 0, none) == 0 && reads(doc, "Sntilla") &&
          tiku_editdoc_can_undo(doc) && !tiku_editdoc_can_redo(doc), name,
          "an insertion, then ci deleted");
    check(tiku_editdoc_undo(doc) == 0 && reads(doc, "Scintilla") &&
          tiku_editdoc_can_undo(doc) && tiku_editdoc_can_redo(doc), name,
          "the deletion undone");
    check(tiku_editdoc_undo(doc) == 0 && reads(doc, "") &&
          !tiku_editdoc_can_undo(doc) && tiku_editdoc_can_redo(doc), name,
          "the insertion undone");
    check(tiku_editdoc_redo(doc) == 0 && reads(doc, "Scintilla") &&
          tiku_editdoc_can_undo(doc) && tiku_editdoc_can_redo(doc), name,
          "the insertion redone");
    check(tiku_editdoc_redo(doc) == 0 && reads(doc, "Sntilla") &&
          tiku_editdoc_can_undo(doc) && !tiku_editdoc_can_redo(doc), name,
          "the deletion redone");
    check(tiku_editdoc_load(doc, "Sntilla", 7) == 0 &&
          !tiku_editdoc_can_undo(doc) && !tiku_editdoc_can_redo(doc), name,
          "a load clears the history");
    tiku_editdoc_release(doc);
}

/** @brief testCellBuffer.cxx, UndoHistory: Basics, EnsureTruncationAfter-
 *         Undo and Grouping on documents.  The saved state stands in for
 *         the save point; a group of actions is one transaction. */
static void
upstream_undo_history(void)
{
    tiku_edit_replace_t group[] = {
        { { 0, 0 }, "ab", 2 }, { { 1, 2 }, "", 0 }, { { 3, 3 }, "cde", 3 }
    };
    tiku_editdoc_t *doc = doc_of("", 0);
    tiku_edit_transaction_t t;
    tiku_edit_view_t v;

    check(!tiku_editdoc_modified(doc) && !tiku_editdoc_can_undo(doc) &&
          !tiku_editdoc_can_redo(doc), "UndoHistory/Basics",
          "at the saved state, nothing to undo or redo");
    check(put(doc, 0, 0, "ab", 0, none) == 0 && tiku_editdoc_modified(doc) &&
          tiku_editdoc_can_undo(doc) && !tiku_editdoc_can_redo(doc),
          "UndoHistory/Basics", "an insertion leaves the saved state");
    check(put(doc, 0, 2, "", 0, none) == 0 && steps(doc) == 2,
          "UndoHistory/Basics", "a removal is a second step");
    check(tiku_editdoc_undo(doc) == 0 && reads(doc, "ab") &&
          tiku_editdoc_undo(doc) == 0 && reads(doc, "") &&
          !tiku_editdoc_modified(doc), "UndoHistory/Basics",
          "two undos reach the saved state");
    check(tiku_editdoc_redo(doc) == 0 && reads(doc, "ab") &&
          tiku_editdoc_redo(doc) == 0 && reads(doc, "") &&
          tiku_editdoc_modified(doc), "UndoHistory/Basics",
          "two redos leave it, though the text is the same");
    tiku_editdoc_release(doc);

    doc = doc_of("", 0);
    tiku_editdoc_view_attach(doc, &v);
    put(doc, 0, 0, "ab", TIKU_EDIT_TYPING, v);
    put(doc, 2, 2, "cd", TIKU_EDIT_TYPING, v);
    check(tiku_editdoc_undo(doc) == 0 && reads(doc, "") &&
          tiku_editdoc_can_redo(doc), "UndoHistory/EnsureTruncationAfterUndo",
          "undone and still redoable");
    check(put(doc, 0, 0, "12", TIKU_EDIT_TYPING, v) == 0 &&
          !tiku_editdoc_can_redo(doc) && steps(doc) == 1,
          "UndoHistory/EnsureTruncationAfterUndo",
          "the next edit drops what was undone");
    tiku_editdoc_release(doc);

    doc = doc_of("0123", 0);
    memset(&t, 0, sizeof t);
    t.edits = group;
    t.count = 3;
    check(tiku_editdoc_apply(doc, &t) == 0 && reads(doc, "ab02cde3") &&
          steps(doc) == 1, "UndoHistory/Grouping", "three edits, one step");
    check(tiku_editdoc_undo(doc) == 0 && reads(doc, "0123") &&
          !tiku_editdoc_modified(doc) && tiku_editdoc_redo(doc) == 0 &&
          reads(doc, "ab02cde3") && tiku_editdoc_modified(doc),
          "UndoHistory/Grouping", "undone and redone whole");
    tiku_editdoc_release(doc);
}

/** @brief Whether @p v's selection is an empty caret at @p pos. */
static int
caret_at(tiku_editdoc_t *doc, tiku_edit_view_t v, tiku_edit_pos_t pos)
{
    tiku_edit_pos_t a = -1, c = -1;

    return tiku_editdoc_view_selection(doc, v, &a, &c) == 0 && a == pos &&
           c == pos;
}

/** @brief Type deletions of [s, s+1) from @p v, one per entry of @p at. */
static void
type_deletes(tiku_editdoc_t *doc, tiku_edit_view_t v, const int *at, int n)
{
    int i;

    for (i = 0; i < n; i++)
        put(doc, at[i], at[i] + 1, "", TIKU_EDIT_TYPING, v);
}

/** @brief testDocument.cxx, DocumentUndo: joined deletions and insertions
 *         come back in one undo that empties the history.  Upstream reports
 *         where the reinserted text ends and moves the caret there; here
 *         undo puts back the selection the view had before the step, so
 *         the caret expected is where the typing began. */
static void
upstream_document_undo(void)
{
    static const int forwards[] = { 1, 1, 1 }, backwards[] = { 5, 4, 3 };
    static const int both[] = { 5, 4, 4, 3 };
    tiku_edit_replace_t grouped[] = {
        { { 1, 3 }, "1", 1 }, { { 5, 7 }, "", 0 }
    };
    tiku_edit_transaction_t t;
    tiku_editdoc_t *doc;
    tiku_edit_view_t v;
    int i;

    for (i = 0; i < 5; i++) {
        static const char *const name[] = {
            "DocumentUndo/CheckDeleteForwards",
            "DocumentUndo/CheckDeleteBackwards",
            "DocumentUndo/CheckBothWays", "DocumentUndo/CheckInsert",
            "DocumentUndo/CheckGrouped"
        };
        static const int caret[] = { 1, 6, 5, 1, 1 };

        doc = doc_of("Scintilla", 0);
        tiku_editdoc_view_attach(doc, &v);
        tiku_editdoc_view_select(doc, v, caret[i], caret[i]);
        memset(&t, 0, sizeof t);
        t.edits = grouped;
        t.count = 2;
        t.view = v;
        if (i == 0)
            type_deletes(doc, v, forwards, 3);
        else if (i == 1)
            type_deletes(doc, v, backwards, 3);
        else if (i == 2)
            type_deletes(doc, v, both, 4);
        else if (i == 3)
            check(put(doc, 1, 1, "1", TIKU_EDIT_TYPING, v) == 0 &&
                  put(doc, 2, 2, "2", TIKU_EDIT_TYPING, v) == 0 &&
                  put(doc, 3, 3, "3", TIKU_EDIT_TYPING, v) == 0 &&
                  reads(doc, "S123cintilla"), name[i], "typed");
        else
            check(tiku_editdoc_apply(doc, &t) == 0 && reads(doc, "S1ntla"),
                  name[i], "the group as one transaction");
        check(tiku_editdoc_undo(doc) == 0 && reads(doc, "Scintilla") &&
              !tiku_editdoc_can_undo(doc) && tiku_editdoc_can_redo(doc),
              name[i], "one undo restores it all");
        check(caret_at(doc, v, caret[i]), name[i],
              "the caret goes back to where the step began");
        tiku_editdoc_release(doc);
    }
}

int
main(void)
{
    unsigned long before = tiku_ed_test_commit_allocations();

    transactions();
    refusals();
    edges();
    selection_after();
    undo_redo();
    saved_state();
    typing();
    deleting();
    over_selection();
    pruning();
    upstream_cell_buffer();
    upstream_undo_history();
    upstream_document_undo();
    check(tiku_ed_test_commit_allocations() == before, "TXN-NOALLOC",
          "no commit allocated");
    printf("%s\n", fails ? "FAIL: editor history" :
           "PASS: editor history -- transactions whole or refused, undo, "
           "redo, branches, typing steps, saved state, budget");
    return fails != 0;
}
