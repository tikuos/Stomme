/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_editdoc_selections.c - a view's several selections: set and read
 * back in order, merged where they meet, moved by edits, set by
 * transactions, restored by undo and redo, each view's own; and random
 * sets and edits against a plain model of the rules.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tiku_editdoc.h"

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

/** @brief A new document holding @p s, with an attached view in @p v. */
static tiku_editdoc_t *
doc_of(const char *s, tiku_edit_view_t *v)
{
    tiku_editdoc_t *doc = NULL;

    if (tiku_editdoc_create(NULL, &doc) != TIKU_EDIT_OK ||
        tiku_editdoc_load(doc, s, strlen(s)) != TIKU_EDIT_OK ||
        tiku_editdoc_view_attach(doc, v) != TIKU_EDIT_OK) {
        printf("  [FAIL] setup     document\n");
        exit(1);
    }
    return doc;
}

/** @brief Set @p n selections given as anchor, caret pairs. */
static tiku_edit_status_t
set(tiku_editdoc_t *doc, tiku_edit_view_t v, const int64_t *pairs, size_t n,
    size_t main)
{
    tiku_edit_selection_t s[16];
    size_t i;

    for (i = 0; i < n; i++) {
        s[i].anchor = pairs[2 * i];
        s[i].caret = pairs[2 * i + 1];
    }
    return tiku_editdoc_view_select_many(doc, v, s, n, main);
}

/** @brief Whether the view's selections read back as the @p n anchor,
 *         caret pairs given, in order, the main one at @p main. */
static int
holds(tiku_editdoc_t *doc, tiku_edit_view_t v, const int64_t *pairs,
      size_t n, size_t main)
{
    tiku_edit_selection_t s[16];
    size_t i, at = 99, count = tiku_editdoc_view_selections(doc, v, s, 16,
                                                           &at);

    if (count != n || at != main)
        return 0;
    for (i = 0; i < n; i++)
        if (s[i].anchor != pairs[2 * i] || s[i].caret != pairs[2 * i + 1])
            return 0;
    return 1;
}

/** @brief Replace [s, e) by @p text, from no view. */
static tiku_edit_status_t
put(tiku_editdoc_t *doc, int64_t s, int64_t e, const char *text)
{
    tiku_edit_replace_t r = { { s, e }, text, strlen(text) };
    tiku_edit_transaction_t t;

    memset(&t, 0, sizeof t);
    t.edits = &r;
    t.count = 1;
    return tiku_editdoc_apply(doc, &t);
}

/** @brief Whether the document holds @p s. */
static int
reads(tiku_editdoc_t *doc, const char *s)
{
    char buf[128];
    size_t n = 0;
    tiku_edit_range_t all = { 0, tiku_editdoc_length(doc) };

    return tiku_editdoc_read(doc, all, buf, sizeof buf, &n) == TIKU_EDIT_OK &&
           n == strlen(s) && memcmp(buf, s, n) == 0;
}

/** @brief Type @p text at @p at from view @p v, the caret after it. */
static tiku_edit_status_t
type_one(tiku_editdoc_t *doc, tiku_edit_view_t v, int64_t at,
         const char *text)
{
    tiku_edit_replace_t r = { { at, at }, text, strlen(text) };
    tiku_edit_transaction_t t;

    memset(&t, 0, sizeof t);
    t.edits = &r;
    t.count = 1;
    t.view = v;
    t.select = 1;
    t.anchor = t.caret = at + (int64_t)strlen(text);
    t.flags = TIKU_EDIT_TYPING;
    return tiku_editdoc_apply(doc, &t);
}

/** @brief Selections set together, read back in order, the main one the
 *         one the one-selection calls use; refusals change nothing. */
static void
setting(void)
{
    tiku_edit_view_t v, gone;
    tiku_editdoc_t *doc = doc_of("hello world\r\nsecond line", &v);
    static const int64_t three[] = { 8, 8, 2, 2, 5, 5 };
    static const int64_t sorted[] = { 2, 2, 5, 5, 8, 8 };
    static const int64_t crlf[] = { 1, 1, 12, 12 };
    static const int64_t outside[] = { 1, 1, 99, 99 };
    tiku_edit_selection_t one;
    tiku_edit_pos_t a = -1, c = -1;
    size_t at = 0;

    check(holds(doc, v, (const int64_t[]){ 0, 0 }, 1, 0), "SEL-001",
          "a new view has one caret at 0");
    check(set(doc, v, three, 3, 0) == TIKU_EDIT_OK &&
          holds(doc, v, sorted, 3, 2), "SEL-001",
          "three carets read back by their starts, the main one third");
    check(tiku_editdoc_view_selection(doc, v, &a, &c) == TIKU_EDIT_OK &&
          a == 8 && c == 8, "SEL-001",
          "the one-selection call reads the main one");
    check(tiku_editdoc_view_selections(doc, v, &one, 1, &at) == 3 &&
          one.anchor == 2 && at == 2 &&
          tiku_editdoc_view_selections(doc, v, NULL, 0, NULL) == 3,
          "SEL-001", "a short buffer takes what fits; the count is whole");
    check(set(doc, v, three, 0, 0) == TIKU_EDIT_INVALID &&
          set(doc, v, three, 3, 3) == TIKU_EDIT_INVALID &&
          tiku_editdoc_view_select_many(doc, v, NULL, 1, 0) ==
              TIKU_EDIT_INVALID &&
          set(doc, v, crlf, 2, 0) == TIKU_EDIT_INVALID_RANGE &&
          set(doc, v, outside, 2, 0) == TIKU_EDIT_INVALID_RANGE &&
          holds(doc, v, sorted, 3, 2), "SEL-001",
          "none, a main past them, an end between CR and LF or outside "
          "the text is refused, nothing changed");
    check(tiku_editdoc_view_select(doc, v, 3, 4) == TIKU_EDIT_OK &&
          holds(doc, v, (const int64_t[]){ 3, 4 }, 1, 0), "SEL-001",
          "setting one selection keeps it alone");
    tiku_editdoc_release(doc);
    doc = doc_of("", &v);
    check(type_one(doc, v, 0, "a") == TIKU_EDIT_OK &&
          set(doc, v, (const int64_t[]){ 1, 1 }, 1, 0) == TIKU_EDIT_OK &&
          type_one(doc, v, 1, "b") == TIKU_EDIT_OK &&
          tiku_editdoc_undo(doc) == TIKU_EDIT_OK && reads(doc, "a"), "SEL-001",
          "setting them ends the view's typing step");
    check(tiku_editdoc_view_attach(doc, &gone) == TIKU_EDIT_OK, "SEL-001",
          "a second view");
    tiku_editdoc_view_detach(doc, gone);
    check(set(doc, gone, three, 3, 0) == TIKU_EDIT_INVALID &&
          tiku_editdoc_view_selections(doc, gone, NULL, 0, NULL) == 0,
          "SEL-001", "a detached view's handle is dead");
    tiku_editdoc_release(doc);
}

/** @brief Selections that overlap become one, as do a caret and one it
 *         touches; two that only touch stay two; the main one stays
 *         main, facing its way. */
static void
merging(void)
{
    tiku_edit_view_t v;
    tiku_editdoc_t *doc = doc_of("0123456789abcdef", &v);

    check(set(doc, v, (const int64_t[]){ 1, 5, 3, 8 }, 2, 1) ==
              TIKU_EDIT_OK &&
          holds(doc, v, (const int64_t[]){ 1, 8 }, 1, 0), "SEL-002",
          "two that overlap become one");
    check(set(doc, v, (const int64_t[]){ 1, 3, 3, 3 }, 2, 0) == 0 &&
          holds(doc, v, (const int64_t[]){ 1, 3 }, 1, 0) &&
          set(doc, v, (const int64_t[]){ 3, 3, 3, 6 }, 2, 1) == 0 &&
          holds(doc, v, (const int64_t[]){ 3, 6 }, 1, 0), "SEL-002",
          "a caret at either end of a selection joins it");
    check(set(doc, v, (const int64_t[]){ 4, 4, 4, 4, 9, 9 }, 3, 2) == 0 &&
          holds(doc, v, (const int64_t[]){ 4, 4, 9, 9 }, 2, 1), "SEL-002",
          "two carets at one place are one");
    check(set(doc, v, (const int64_t[]){ 1, 3, 3, 5 }, 2, 0) == 0 &&
          holds(doc, v, (const int64_t[]){ 1, 3, 3, 5 }, 2, 0), "SEL-002",
          "two selections that only touch stay two");
    check(set(doc, v, (const int64_t[]){ 1, 9, 3, 4 }, 2, 1) == 0 &&
          holds(doc, v, (const int64_t[]){ 1, 9 }, 1, 0), "SEL-002",
          "one inside another is taken in");
    check(set(doc, v, (const int64_t[]){ 1, 4, 8, 3 }, 2, 1) == 0 &&
          holds(doc, v, (const int64_t[]){ 8, 1 }, 1, 0), "SEL-002",
          "the merged one faces the main one's way");
    check(set(doc, v, (const int64_t[]){ 6, 3, 3, 3 }, 2, 1) == 0 &&
          holds(doc, v, (const int64_t[]){ 6, 3 }, 1, 0), "SEL-002",
          "a main caret taken in faces the other's way");
    check(set(doc, v, (const int64_t[]){ 12, 12, 1, 4, 3, 6, 5, 9 }, 4, 0) ==
              0 &&
          holds(doc, v, (const int64_t[]){ 1, 9, 12, 12 }, 2, 1), "SEL-002",
          "a chain of overlaps is one, the main one after it");
    check(set(doc, v, (const int64_t[]){ 1, 3, 3, 3, 3, 5 }, 3, 1) == 0 &&
          holds(doc, v, (const int64_t[]){ 1, 3, 3, 5 }, 2, 0), "SEL-002",
          "a main caret touching two joins the earlier");
    tiku_editdoc_release(doc);
}

/** @brief Every selection moves through an edit as the main one does;
 *         edits that bring them together merge them. */
static void
editing(void)
{
    tiku_edit_view_t v;
    tiku_editdoc_t *doc = doc_of("abcdefghij", &v);

    (void)set(doc, v, (const int64_t[]){ 2, 2, 5, 5, 8, 8 }, 3, 1);
    check(put(doc, 0, 0, "XY") == 0 &&
          holds(doc, v, (const int64_t[]){ 4, 4, 7, 7, 10, 10 }, 3, 1),
          "SEL-003", "an insertion before them moves them all");
    check(put(doc, 3, 9, "") == 0 &&
          holds(doc, v, (const int64_t[]){ 3, 3, 4, 4 }, 2, 0), "SEL-003",
          "a deletion over two carets brings them to one, the main one");
    (void)set(doc, v, (const int64_t[]){ 1, 1, 3, 5 }, 2, 0);
    check(put(doc, 3, 3, "--") == 0 && put(doc, 1, 1, "+") == 0 &&
          holds(doc, v, (const int64_t[]){ 1, 1, 6, 8 }, 2, 0), "SEL-003",
          "text put at a selection's start moves it; a caret stays before "
          "text put at it");
    check(put(doc, 1, 6, "") == 0 &&
          holds(doc, v, (const int64_t[]){ 1, 3 }, 1, 0), "SEL-003",
          "an edit that brings a caret to a selection's start merges them");
    tiku_editdoc_release(doc);
    doc = doc_of("ab\rX\ncd", &v);
    (void)set(doc, v, (const int64_t[]){ 0, 0, 4, 4 }, 2, 0);
    check(put(doc, 3, 4, "") == 0 &&
          holds(doc, v, (const int64_t[]){ 0, 0, 2, 2 }, 2, 0), "SEL-003",
          "one left between a CR and its LF goes back to the CR");
    tiku_editdoc_release(doc);
}

/** @brief Type @p text at each of @p n positions from view @p v, the
 *         carets after the text, the main one at @p main. */
static tiku_edit_status_t
type_at(tiku_editdoc_t *doc, tiku_edit_view_t v, const int64_t *at,
        size_t n, size_t main, const char *text)
{
    tiku_edit_replace_t r[8];
    tiku_edit_selection_t after[8];
    tiku_edit_transaction_t t;
    int64_t len = (int64_t)strlen(text);
    size_t i;

    for (i = 0; i < n; i++) {
        r[i].range.start = r[i].range.end = at[i];
        r[i].text = text;
        r[i].length = (size_t)len;
        after[i].anchor = after[i].caret = at[i] + (int64_t)(i + 1) * len;
    }
    memset(&t, 0, sizeof t);
    t.edits = r;
    t.count = n;
    t.view = v;
    t.select = 1;
    t.selections = after;
    t.nselections = n;
    t.main = main;
    t.flags = TIKU_EDIT_TYPING;
    return tiku_editdoc_apply(doc, &t);
}

/** @brief A transaction sets the selections after it, merged, or else
 *         moves them; a bad one is refused whole. */
static void
transactions(void)
{
    tiku_edit_view_t v;
    tiku_editdoc_t *doc = doc_of("one two three", &v);
    static const int64_t at[] = { 3, 7 };
    tiku_edit_replace_t r = { { 0, 0 }, "#", 1 };
    tiku_edit_selection_t bad[2] = { { 1, 1 }, { 40, 40 } };
    tiku_edit_selection_t same[2] = { { 2, 2 }, { 2, 2 } };
    tiku_edit_transaction_t t;

    (void)set(doc, v, (const int64_t[]){ 3, 3, 7, 7 }, 2, 1);
    check(type_at(doc, v, at, 2, 1, "!") == TIKU_EDIT_OK &&
          reads(doc, "one! two! three") &&
          holds(doc, v, (const int64_t[]){ 4, 4, 9, 9 }, 2, 1), "SEL-004",
          "typing at two carets is one transaction setting both after it");
    memset(&t, 0, sizeof t);
    t.edits = &r;
    t.count = 1;
    t.view = v;
    t.select = 1;
    t.selections = bad;
    t.nselections = 2;
    check(tiku_editdoc_apply(doc, &t) == TIKU_EDIT_INVALID_RANGE &&
          reads(doc, "one! two! three") &&
          holds(doc, v, (const int64_t[]){ 4, 4, 9, 9 }, 2, 1), "SEL-004",
          "a selection after it outside the text refuses it whole");
    t.main = 2;
    check(tiku_editdoc_apply(doc, &t) == TIKU_EDIT_INVALID, "SEL-004",
          "and so does a main one past them");
    t.selections = NULL;
    t.main = 0;
    check(tiku_editdoc_apply(doc, &t) == TIKU_EDIT_INVALID, "SEL-004",
          "or none given");
    t.selections = same;
    check(tiku_editdoc_apply(doc, &t) == TIKU_EDIT_OK &&
          holds(doc, v, (const int64_t[]){ 2, 2 }, 1, 0), "SEL-004",
          "the selections after it merge");
    (void)set(doc, v, (const int64_t[]){ 1, 1, 6, 6 }, 2, 0);
    t.select = 0;
    t.nselections = 0;
    check(tiku_editdoc_apply(doc, &t) == TIKU_EDIT_OK &&
          holds(doc, v, (const int64_t[]){ 2, 2, 7, 7 }, 2, 0), "SEL-004",
          "without them, the edit moves the view's selections");
    t.select = 1;
    t.anchor = t.caret = 0;
    check(tiku_editdoc_apply(doc, &t) == TIKU_EDIT_OK &&
          holds(doc, v, (const int64_t[]){ 0, 0 }, 1, 0), "SEL-004",
          "with one anchor and caret, the others go");
    t.count = 0;
    t.selections = bad;
    t.nselections = 1;
    bad[0].anchor = 2;
    check(tiku_editdoc_apply(doc, &t) == TIKU_EDIT_OK &&
          holds(doc, v, (const int64_t[]){ 2, 1 }, 1, 0), "SEL-004",
          "a transaction of no edits only sets them");
    tiku_editdoc_release(doc);
}

/** @brief Delete the character before each of @p n positions from view
 *         @p v, as Backspace at a caret there does, the carets after it. */
static tiku_edit_status_t
back_at(tiku_editdoc_t *doc, tiku_edit_view_t v, const int64_t *at, size_t n,
        size_t main)
{
    tiku_edit_replace_t r[8];
    tiku_edit_selection_t after[8];
    tiku_edit_transaction_t t;
    size_t i;

    for (i = 0; i < n; i++) {
        r[i].range.start = at[i] - 1;
        r[i].range.end = at[i];
        r[i].text = NULL;
        r[i].length = 0;
        after[i].anchor = after[i].caret = at[i] - 1 - (int64_t)i;
    }
    memset(&t, 0, sizeof t);
    t.edits = r;
    t.count = n;
    t.view = v;
    t.select = 1;
    t.selections = after;
    t.nselections = n;
    t.main = main;
    t.flags = TIKU_EDIT_TYPING;
    return tiku_editdoc_apply(doc, &t);
}

/** @brief Undo gives back the selections before a step, redo those after;
 *         typing on at every caret joins one step, as at one caret. */
static void
history(void)
{
    tiku_edit_view_t v;
    tiku_editdoc_t *doc = doc_of("one two three", &v);
    static const int64_t at[] = { 3, 7 }, again[] = { 4, 9 };
    static const int64_t apart[] = { 5, 12 }, words[] = { 3, 7 };
    static const int64_t more[] = { 2, 5 }, one[] = { 1 }, aside[] = { 2, 4 };
    tiku_edit_selection_t two[2] = { { 2, 2 }, { 0, 0 } };
    tiku_edit_replace_t r = { { 0, 0 }, "#", 1 };
    tiku_edit_transaction_t t;

    (void)set(doc, v, (const int64_t[]){ 3, 3, 7, 7 }, 2, 1);
    (void)type_at(doc, v, at, 2, 1, "!");
    (void)type_at(doc, v, again, 2, 1, "?");
    check(reads(doc, "one!? two!? three") &&
          tiku_editdoc_undo(doc) == TIKU_EDIT_OK &&
          reads(doc, "one two three") &&
          holds(doc, v, (const int64_t[]){ 3, 3, 7, 7 }, 2, 1), "SEL-005",
          "typing on at every caret joins one step, undone with every "
          "selection it began with");
    check(tiku_editdoc_redo(doc) == TIKU_EDIT_OK &&
          reads(doc, "one!? two!? three") &&
          holds(doc, v, (const int64_t[]){ 5, 5, 11, 11 }, 2, 1), "SEL-005",
          "redo gives back the text and every selection after it");
    check(type_at(doc, v, apart, 2, 1, "-") == TIKU_EDIT_OK &&
          reads(doc, "one!?- two!? -three") &&
          tiku_editdoc_undo(doc) == TIKU_EDIT_OK &&
          reads(doc, "one!? two!? three"), "SEL-005",
          "typing not going on at every caret starts its own step");
    memset(&t, 0, sizeof t);
    t.edits = &r;
    t.count = 1;
    t.view = v;
    check(tiku_editdoc_apply(doc, &t) == TIKU_EDIT_OK &&
          holds(doc, v, (const int64_t[]){ 6, 6, 12, 12 }, 2, 1) &&
          tiku_editdoc_undo(doc) == TIKU_EDIT_OK &&
          tiku_editdoc_redo(doc) == TIKU_EDIT_OK &&
          holds(doc, v, (const int64_t[]){ 6, 6, 12, 12 }, 2, 1), "SEL-005",
          "redo gives back the ones a step only moved");
    tiku_editdoc_release(doc);

    doc = doc_of("abc def", &v);
    (void)set(doc, v, (const int64_t[]){ 3, 3, 7, 7 }, 2, 0);
    (void)back_at(doc, v, words, 2, 0);
    (void)back_at(doc, v, more, 2, 0);
    check(reads(doc, "a d") && tiku_editdoc_undo(doc) == TIKU_EDIT_OK &&
          reads(doc, "abc def") &&
          holds(doc, v, (const int64_t[]){ 3, 3, 7, 7 }, 2, 0), "SEL-005",
          "Backspace on at every caret joins one step too");
    (void)tiku_editdoc_redo(doc);
    check(back_at(doc, v, one, 1, 0) == TIKU_EDIT_OK && reads(doc, " d") &&
          tiku_editdoc_undo(doc) == TIKU_EDIT_OK && reads(doc, "a d"),
          "SEL-005", "at fewer carets it starts its own step");
    tiku_editdoc_release(doc);
    doc = doc_of("abc def", &v);
    (void)set(doc, v, (const int64_t[]){ 3, 3, 7, 7 }, 2, 0);
    (void)back_at(doc, v, words, 2, 0);
    check(back_at(doc, v, aside, 2, 0) == TIKU_EDIT_OK && reads(doc, "a e") &&
          tiku_editdoc_undo(doc) == TIKU_EDIT_OK && reads(doc, "ab de"),
          "SEL-005", "and so does one not going on at every caret");
    tiku_editdoc_release(doc);

    doc = doc_of("", &v);
    memset(&t, 0, sizeof t);
    t.edits = &r;
    t.count = 1;
    t.view = v;
    t.select = 1;
    t.flags = TIKU_EDIT_TYPING;
    r.range.start = r.range.end = 0;
    t.anchor = t.caret = 1;
    (void)tiku_editdoc_apply(doc, &t);
    r.range.start = r.range.end = 1;
    t.selections = two;
    t.nselections = 2;
    check(tiku_editdoc_apply(doc, &t) == TIKU_EDIT_OK && reads(doc, "##") &&
          tiku_editdoc_undo(doc) == TIKU_EDIT_OK && reads(doc, "") &&
          holds(doc, v, (const int64_t[]){ 0, 0 }, 1, 0) &&
          tiku_editdoc_redo(doc) == TIKU_EDIT_OK &&
          holds(doc, v, (const int64_t[]){ 0, 0, 2, 2 }, 2, 1), "SEL-005",
          "a step joining one that kept one selection gives that back on "
          "undo and its own on redo");
    tiku_editdoc_release(doc);
}

/** @brief Each view keeps its own selections; a new view in a freed slot
 *         starts with one caret. */
static void
views(void)
{
    tiku_edit_view_t a, b, c;
    tiku_editdoc_t *doc = doc_of("abcdef", &a);

    check(tiku_editdoc_view_attach(doc, &b) == TIKU_EDIT_OK &&
          set(doc, a, (const int64_t[]){ 1, 1, 4, 4 }, 2, 0) == 0 &&
          holds(doc, b, (const int64_t[]){ 0, 0 }, 1, 0), "SEL-006",
          "selections set in one view leave the other alone");
    check(put(doc, 0, 0, "x") == 0 &&
          holds(doc, a, (const int64_t[]){ 2, 2, 5, 5 }, 2, 0) &&
          holds(doc, b, (const int64_t[]){ 0, 0 }, 1, 0), "SEL-006",
          "an edit moves each view's own");
    tiku_editdoc_view_detach(doc, a);
    check(tiku_editdoc_view_attach(doc, &c) == TIKU_EDIT_OK &&
          c.slot == a.slot &&
          holds(doc, c, (const int64_t[]){ 0, 0 }, 1, 0), "SEL-006",
          "a view attached in a detached one's slot has one caret at 0");
    tiku_editdoc_release(doc);
}

/* The model: selections as plain numbers, merged and moved by the rules
 * written out again. */
typedef struct {
    int64_t anchor, caret;
    int main;
} msel_t;

static int64_t
mlo(const msel_t *s)
{
    return s->anchor < s->caret ? s->anchor : s->caret;
}

static int64_t
mhi(const msel_t *s)
{
    return s->anchor < s->caret ? s->caret : s->anchor;
}

/** @brief Sort by start, then end; merge each into the one before when
 *         they overlap or a caret touches; the main one's way wins. */
static size_t
model_merge(msel_t *s, size_t n)
{
    size_t i, j, k = 0;

    for (i = 1; i < n; i++)
        for (j = i; j > 0 && (mlo(&s[j]) < mlo(&s[j - 1]) ||
                              (mlo(&s[j]) == mlo(&s[j - 1]) &&
                               mhi(&s[j]) < mhi(&s[j - 1]))); j--) {
            msel_t t = s[j];

            s[j] = s[j - 1];
            s[j - 1] = t;
        }
    for (i = 0; i < n; i++) {
        msel_t *c = k ? &s[k - 1] : NULL;
        int empty = c && (c->anchor == c->caret ||
                          s[i].anchor == s[i].caret);

        if (c && (empty ? mlo(&s[i]) <= mhi(c) : mlo(&s[i]) < mhi(c))) {
            int64_t lo = mlo(c), hi = mhi(&s[i]) > mhi(c) ? mhi(&s[i])
                                                          : mhi(c);
            const msel_t *way = s[i].main || c->anchor == c->caret ? &s[i]
                                                                   : c;
            int back;

            if (way->anchor == way->caret)
                way = way == c ? &s[i] : c;
            back = way->caret < way->anchor;
            c->anchor = back ? hi : lo;
            c->caret = back ? lo : hi;
            c->main |= s[i].main;
        } else {
            s[k++] = s[i];
        }
    }
    return k;
}

/** @brief A selection end through [s, e) replaced by n bytes, as the
 *         upstream rule moves it. */
static void
model_map(msel_t *m, int64_t s, int64_t e, int64_t n)
{
    int64_t *p[2] = { &m->anchor, &m->caret };
    int starts[2], i;

    for (i = 0; i < 2; i++)
        if (*p[i] > s)
            *p[i] = *p[i] > e ? *p[i] - (e - s) : s;
    starts[0] = m->anchor < m->caret;
    starts[1] = m->caret < m->anchor;
    for (i = 0; i < 2; i++)
        if (*p[i] > s || (*p[i] == s && starts[i]))
            *p[i] += n;
}

/** @brief Whether the view's selections are the model's. */
static int
model_holds(tiku_editdoc_t *doc, tiku_edit_view_t v, const msel_t *m,
            size_t n)
{
    tiku_edit_selection_t s[16];
    size_t i, at = 99;

    if (tiku_editdoc_view_selections(doc, v, s, 16, &at) != n)
        return 0;
    for (i = 0; i < n; i++)
        if (s[i].anchor != m[i].anchor || s[i].caret != m[i].caret ||
            (at == i) != m[i].main)
            return 0;
    return 1;
}

/** @brief Random sets and random edits of ASCII text against the model. */
static void
model(void)
{
    tiku_edit_view_t v;
    tiku_editdoc_t *doc = doc_of("", &v);
    msel_t m[16];
    tiku_edit_selection_t s[16];
    size_t n = 0, i, main;
    int round, ok = 1, sets = 0, edits = 0;

    srand(7);
    for (round = 0; round < 4000 && ok; round++) {
        int64_t len = tiku_editdoc_length(doc);

        if (round % 5 == 0 || n == 0) {
            n = 1 + (size_t)(rand() % 6);
            main = (size_t)rand() % n;
            for (i = 0; i < n; i++) {
                s[i].anchor = len ? rand() % (len + 1) : 0;
                s[i].caret = rand() % 3 == 0 ? s[i].anchor
                             : len ? rand() % (len + 1) : 0;
                m[i].anchor = s[i].anchor;
                m[i].caret = s[i].caret;
                m[i].main = i == main;
            }
            ok = tiku_editdoc_view_select_many(doc, v, s, n, main) ==
                 TIKU_EDIT_OK;
            n = model_merge(m, n);
            sets++;
        } else {
            int64_t a = len ? rand() % (len + 1) : 0;
            int64_t b = a + (len - a ? rand() % (len - a + 1) : 0);
            const char *text = rand() % 2 ? "" : rand() % 2 ? "ab" : "q";

            if (len > 60)
                b = len;
            ok = put(doc, a, b, text) == TIKU_EDIT_OK;
            for (i = 0; i < n; i++)
                model_map(&m[i], a, b, (int64_t)strlen(text));
            n = model_merge(m, n);
            edits++;
        }
        ok = ok && model_holds(doc, v, m, n);
    }
    check(ok && sets > 500 && edits > 2000, "SEL-002",
          "random sets and edits agree with the model");
    tiku_editdoc_release(doc);
}

int
main(void)
{
    setting();
    merging();
    editing();
    transactions();
    history();
    views();
    model();
    printf("%s\n", fails ? "FAIL: editor selections" :
           "PASS: editor selections -- set and read in order, merged, "
           "moved by edits, set by transactions, restored by undo, per "
           "view, against a model");
    return fails != 0;
}
