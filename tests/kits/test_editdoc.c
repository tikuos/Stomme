/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_editdoc.c - what a document holds and refuses: UTF-8 without NUL,
 * line breaks kept byte for byte, reads that never cut short, positions
 * and caret steps, read-only, load, snapshots.
 *
 * Some cases adapt Scintilla 5.6.7 test/unit/testDocument.cxx,
 * testCellBuffer.cxx and testUniConversion.cxx, Copyright Neil Hodgson;
 * see kits/editor/LICENSE.Scintilla and kits/editor/UPSTREAM.md.
 *
 * SPDX-License-Identifier: Apache-2.0 AND LicenseRef-Scintilla
 */

#include <stdio.h>
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

/** @brief Whether the whole document reads back as @p s. */
static int
reads(const tiku_editdoc_t *doc, const char *s)
{
    char buf[256];
    size_t n = 0;
    tiku_edit_range_t all = { 0, tiku_editdoc_length(doc) };

    return tiku_editdoc_read(doc, all, buf, sizeof buf, &n) == TIKU_EDIT_OK &&
           n == strlen(s) && !memcmp(buf, s, n) && buf[n] == 0;
}

/** @brief A new document holding @p s. */
static tiku_editdoc_t *
doc_of(const char *s)
{
    tiku_editdoc_t *doc = NULL;

    check(tiku_editdoc_create(NULL, &doc) == TIKU_EDIT_OK && doc, "DOC-001",
          "create");
    check(tiku_editdoc_load(doc, s, strlen(s)) == TIKU_EDIT_OK, "DOC-002",
          "load");
    return doc;
}

/** @brief The empty document: one empty line, unmodified. */
static void
empty(void)
{
    tiku_editdoc_t *doc = NULL;

    check(tiku_editdoc_create(NULL, &doc) == TIKU_EDIT_OK, "DOC-001",
          "create");
    check(tiku_editdoc_length(doc) == 0 && tiku_editdoc_lines(doc) == 1 &&
          tiku_editdoc_line_start(doc, 0) == 0 &&
          tiku_editdoc_line_start(doc, 1) == 0 &&
          tiku_editdoc_line_end(doc, 0) == 0 &&
          tiku_editdoc_line_of(doc, 0) == 0, "DOC-001", "one empty line");
    check(tiku_editdoc_generation(doc) == 1 && !tiku_editdoc_modified(doc) &&
          !tiku_editdoc_can_undo(doc) && !tiku_editdoc_can_redo(doc),
          "DOC-001", "generation 1, unmodified, no history");
    check(tiku_editdoc_create(NULL, NULL) == TIKU_EDIT_INVALID, "DOC-001",
          "NULL out refused");
    tiku_editdoc_release(doc);
}

/** @brief Load: exact bytes back; malformed UTF-8 and NUL refused with
 *         nothing changed. */
static void
text_policy(void)
{
    static const struct { const char *s; size_t n; } bad[] = {
        { "a\0b", 3 },                          /* NUL                   */
        { "\xc0\xaf", 2 },                      /* overlong /            */
        { "\xe0\x80\xaf", 3 },                  /* overlong, 3 bytes     */
        { "\xed\xa0\x80", 3 },                  /* surrogate D800        */
        { "\xf4\x90\x80\x80", 4 },              /* past U+10FFFF         */
        { "\xe2\x82", 2 },                      /* truncated             */
        { "\x80", 1 },                          /* stray continuation    */
        { "\xff", 1 }                           /* never a UTF-8 byte    */
    };
    tiku_editdoc_t *doc = doc_of("caf\xc3\xa9 \xf0\x9f\x98\x80\tform\ffeed");
    uint64_t generation = tiku_editdoc_generation(doc);
    size_t i;

    check(reads(doc, "caf\xc3\xa9 \xf0\x9f\x98\x80\tform\ffeed"), "DOC-002",
          "bytes read back exactly, tabs and control characters kept");
    for (i = 0; i < sizeof bad / sizeof *bad; i++) {
        check(tiku_editdoc_load(doc, bad[i].s, bad[i].n) ==
              TIKU_EDIT_INVALID_TEXT, "DOC-003", "malformed text refused");
        check(tiku_editdoc_generation(doc) == generation &&
              reads(doc, "caf\xc3\xa9 \xf0\x9f\x98\x80\tform\ffeed"),
              "DOC-003", "the document is unchanged");
    }
    tiku_editdoc_release(doc);
}

/** @brief LF, CR, CRLF and mixtures: kept byte for byte, one break each. */
static void
line_endings(void)
{
    tiku_editdoc_t *doc = doc_of("a\r\nb\rc\nd\r\n\r\ne");

    check(reads(doc, "a\r\nb\rc\nd\r\n\r\ne") && tiku_editdoc_lines(doc) == 6,
          "DOC-004", "mixed endings kept, six lines");
    check(tiku_editdoc_line_end(doc, 0) == 1 &&
          tiku_editdoc_line_start(doc, 1) == 3 &&
          tiku_editdoc_line_end(doc, 1) == 4 &&
          tiku_editdoc_line_end(doc, 2) == 6 &&
          tiku_editdoc_line_end(doc, 3) == 8 &&
          tiku_editdoc_line_start(doc, 4) == 10 &&
          tiku_editdoc_line_end(doc, 4) == 10 &&
          tiku_editdoc_line_end(doc, 5) == 13, "DOC-004",
          "each line's text ends before its break");
    check(tiku_editdoc_line_of(doc, 1) == 0 &&
          tiku_editdoc_line_of(doc, 2) == 0 &&
          tiku_editdoc_line_of(doc, 3) == 1, "DOC-004",
          "the CR and LF of a CRLF belong to the line they end");
    tiku_editdoc_release(doc);

    doc = doc_of("x\n");
    check(tiku_editdoc_lines(doc) == 2 &&
          tiku_editdoc_line_start(doc, 1) == 2 &&
          tiku_editdoc_line_end(doc, 1) == 2 && reads(doc, "x\n"), "DOC-005",
          "a final break starts an empty line and adds no byte");
    tiku_editdoc_release(doc);

    doc = doc_of("a\xc2\x85" "b\xe2\x80\xa8" "c\xe2\x80\xa9" "d");
    check(tiku_editdoc_lines(doc) == 1, "DOC-006",
          "NEL, LS and PS are ordinary characters");
    tiku_editdoc_release(doc);
}

/** @brief The reader of each(): appends into a buffer and counts runs. */
typedef struct {
    char text[64];              /* what arrived                         */
    size_t n;                   /* bytes in text                        */
    int runs;                   /* calls made                           */
    tiku_editdoc_t *doc;        /* the document being read              */
    tiku_edit_status_t during;  /* what an edit inside the read got     */
} reader_t;

/** @brief Collect one run and try to edit from inside the read. */
static int
collect(void *ctx, const char *bytes, size_t n)
{
    reader_t *r = ctx;
    tiku_edit_replace_t e = { { 0, 0 }, "x", 1 };
    tiku_edit_transaction_t t = { &e, 1, { 0, 0 }, 0, 0, 0, 0, 0, NULL, 0,
                                  0 };

    memcpy(r->text + r->n, bytes, n);
    r->n += n;
    r->runs++;
    r->during = tiku_editdoc_apply(r->doc, &t);
    return 0;
}

/** @brief Reads: exact or nothing; each() across the gap. */
static void
reading(void)
{
    tiku_editdoc_t *doc = doc_of("hello world");
    char buf[8] = "untouch";
    size_t n = 0;
    tiku_edit_range_t r = { 0, 11 }, bad = { 5, 3 }, out = { 0, 12 };
    tiku_edit_replace_t e = { { 5, 5 }, ",", 1 };
    tiku_edit_transaction_t t = { &e, 1, { 0, 0 }, 0, 0, 0, 0, 0, NULL, 0,
                                  0 };
    reader_t rd;

    check(tiku_editdoc_read(doc, r, buf, sizeof buf, &n) == TIKU_EDIT_SHORT &&
          n == 11 && !strcmp(buf, "untouch"), "DOC-007",
          "a short buffer is refused, the size told, nothing written");
    check(tiku_editdoc_read(doc, bad, buf, sizeof buf, &n) ==
          TIKU_EDIT_INVALID_RANGE &&
          tiku_editdoc_read(doc, out, buf, sizeof buf, &n) ==
          TIKU_EDIT_INVALID_RANGE, "DOC-007", "reversed and outside refused");
    check(tiku_editdoc_apply(doc, &t) == TIKU_EDIT_OK, "DOC-007",
          "an edit moves the gap into the middle");
    memset(&rd, 0, sizeof rd);
    rd.doc = doc;
    r.end = 12;
    check(tiku_editdoc_each(doc, r, collect, &rd) == TIKU_EDIT_OK &&
          rd.n == 12 && !memcmp(rd.text, "hello, world", 12) && rd.runs <= 2,
          "DOC-007", "each() hands the bytes over in order, in two runs");
    check(rd.during == TIKU_EDIT_BUSY && reads(doc, "hello, world"),
          "DOC-013", "no change while a read holds the bytes");
    check(tiku_editdoc_byte_at(doc, 0) == 'h' &&
          tiku_editdoc_byte_at(doc, 12) == -1 &&
          tiku_editdoc_byte_at(doc, -1) == -1, "DOC-014", "-1 outside");
    tiku_editdoc_release(doc);
}

/** @brief Size limits: a load past the limit is refused whole. */
static void
limits(void)
{
    tiku_editdoc_limits_t lim = { 8, 0 };
    tiku_editdoc_t *doc = NULL;
    tiku_edit_replace_t e = { { 0, 0 }, "12345", 5 };
    tiku_edit_transaction_t t = { &e, 1, { 0, 0 }, 0, 0, 0, 0, 0, NULL, 0,
                                  0 };

    check(tiku_editdoc_create(&lim, &doc) == TIKU_EDIT_OK, "DOC-008",
          "create with a limit");
    check(tiku_editdoc_load(doc, "12345678", 8) == TIKU_EDIT_OK &&
          tiku_editdoc_load(doc, "123456789", 9) == TIKU_EDIT_LIMIT &&
          reads(doc, "12345678"), "DOC-008",
          "a load past the limit is refused, the text kept");
    check(tiku_editdoc_apply(doc, &t) == TIKU_EDIT_LIMIT &&
          reads(doc, "12345678") && !tiku_editdoc_can_undo(doc), "DOC-008",
          "an edit past the limit is refused");
    e.range.end = 1;
    e.text = "ab";
    e.length = 2;
    check(tiku_editdoc_apply(doc, &t) == TIKU_EDIT_LIMIT &&
          reads(doc, "12345678"), "DOC-008",
          "an edit leaving one byte over the limit is refused");
    e.range.end = 2;
    check(tiku_editdoc_apply(doc, &t) == TIKU_EDIT_OK &&
          reads(doc, "ab345678"), "DOC-008",
          "an edit leaving the text exactly at the limit is taken");
    tiku_editdoc_release(doc);
}

/** @brief Character boundaries and caret steps. */
static void
positions(void)
{
    /* a é CRLF 😀 LF b */
    tiku_editdoc_t *doc = doc_of("a\xc3\xa9\r\n\xf0\x9f\x98\x80\nb");
    tiku_edit_pos_t len = tiku_editdoc_length(doc);

    check(len == 11, "DOC-009", "eleven bytes");
    check(tiku_editdoc_is_boundary(doc, 1) &&
          !tiku_editdoc_is_boundary(doc, 2) &&
          tiku_editdoc_is_boundary(doc, 3) &&
          !tiku_editdoc_is_boundary(doc, 6) &&
          !tiku_editdoc_is_boundary(doc, -1) &&
          !tiku_editdoc_is_boundary(doc, 12) &&
          tiku_editdoc_is_boundary(doc, 11),
          "DOC-009", "no boundary inside a character, 0..length only");
    check(tiku_editdoc_is_boundary(doc, 4) && !tiku_editdoc_is_caret(doc, 4) &&
          tiku_editdoc_is_caret(doc, 3) && tiku_editdoc_is_caret(doc, 5),
          "DOC-009", "between CR and LF is a boundary but no caret");
    check(tiku_editdoc_next(doc, 0) == 1 && tiku_editdoc_next(doc, 1) == 3 &&
          tiku_editdoc_next(doc, 3) == 5 && tiku_editdoc_next(doc, 4) == 5 &&
          tiku_editdoc_next(doc, 5) == 9 && tiku_editdoc_next(doc, 11) == 11 &&
          tiku_editdoc_next(doc, -3) == 0, "DOC-009",
          "next: a character, a CRLF as one, clamped");
    check(tiku_editdoc_prev(doc, 11) == 10 && tiku_editdoc_prev(doc, 9) == 5 &&
          tiku_editdoc_prev(doc, 5) == 3 && tiku_editdoc_prev(doc, 4) == 3 &&
          tiku_editdoc_prev(doc, 3) == 1 && tiku_editdoc_prev(doc, 0) == 0 &&
          tiku_editdoc_prev(doc, 99) == 11, "DOC-009",
          "prev: a character, a CRLF as one, clamped");
    check(tiku_editdoc_line_start(doc, 3) == 11 &&
          tiku_editdoc_line_start(doc, 4) == -1 &&
          tiku_editdoc_line_start(doc, -1) == -1 &&
          tiku_editdoc_line_of(doc, 11) == 2 &&
          tiku_editdoc_line_of(doc, 12) == -1 &&
          tiku_editdoc_line_end(doc, 3) == -1, "DOC-015",
          "line queries at and past the ends");
    tiku_editdoc_release(doc);
}

/** @brief Read-only refuses every change and no read. */
static void
read_only(void)
{
    tiku_editdoc_t *doc = doc_of("abc");
    tiku_edit_replace_t e = { { 1, 2 }, "X", 1 };
    tiku_edit_transaction_t t = { &e, 1, { 0, 0 }, 0, 0, 0, 0, 0, NULL, 0,
                                  0 };

    check(tiku_editdoc_apply(doc, &t) == TIKU_EDIT_OK, "DOC-010", "an edit");
    tiku_editdoc_set_read_only(doc, 1);
    check(tiku_editdoc_read_only(doc) &&
          tiku_editdoc_apply(doc, &t) == TIKU_EDIT_READ_ONLY &&
          tiku_editdoc_undo(doc) == TIKU_EDIT_READ_ONLY &&
          tiku_editdoc_load(doc, "z", 1) == TIKU_EDIT_READ_ONLY &&
          reads(doc, "aXc"), "DOC-010",
          "edit, undo and load refused; reads still answer");
    tiku_editdoc_set_read_only(doc, 0);
    check(tiku_editdoc_undo(doc) == TIKU_EDIT_OK &&
          tiku_editdoc_redo(doc) == TIKU_EDIT_OK &&
          tiku_editdoc_undo(doc) == TIKU_EDIT_OK && reads(doc, "abc"),
          "DOC-010", "writable again: undo and redo work");
    tiku_editdoc_set_read_only(doc, 1);
    check(tiku_editdoc_redo(doc) == TIKU_EDIT_READ_ONLY, "DOC-010",
          "redo refused too");
    tiku_editdoc_release(doc);
}

/** @brief A snapshot is an owned copy with the generation and state. */
static void
snapshots(void)
{
    tiku_editdoc_t *doc = doc_of("one\ntwo");
    tiku_edit_snapshot_t snap;
    tiku_edit_replace_t e = { { 0, 3 }, "1", 1 };
    tiku_edit_transaction_t t = { &e, 1, { 0, 0 }, 0, 0, 0, 0, 0, NULL, 0,
                                  0 };

    check(tiku_editdoc_snapshot(doc, &snap) == TIKU_EDIT_OK &&
          snap.length == 7 && !strcmp(snap.text, "one\ntwo") &&
          snap.generation == tiku_editdoc_generation(doc) &&
          snap.state == tiku_editdoc_state(doc), "DOC-011",
          "text, NUL, generation and state");
    check(tiku_editdoc_apply(doc, &t) == TIKU_EDIT_OK &&
          !strcmp(snap.text, "one\ntwo") &&
          snap.generation != tiku_editdoc_generation(doc), "DOC-011",
          "later edits leave the copy alone");
    tiku_edit_snapshot_free(&snap);
    check(snap.text == NULL, "DOC-011", "freed");
    tiku_editdoc_release(doc);
}

/** @brief Load replaces, clears the history and counts as saved; anchors
 *         and selections map as for one replacement of everything. */
static void
loading(void)
{
    tiku_editdoc_t *doc = doc_of("abc");
    tiku_edit_anchor_t left, right;
    tiku_edit_view_t view;
    tiku_edit_pos_t pos, anchor, caret;
    tiku_edit_replace_t e = { { 3, 3 }, "d", 1 };
    tiku_edit_transaction_t t = { &e, 1, { 0, 0 }, 0, 0, 0, 0, 0, NULL, 0,
                                  0 };
    uint64_t generation;

    check(tiku_editdoc_apply(doc, &t) == TIKU_EDIT_OK &&
          tiku_editdoc_modified(doc), "DOC-012", "edited");
    check(tiku_editdoc_anchor_create(doc, 2, TIKU_EDIT_LEFT, &left) ==
          TIKU_EDIT_OK &&
          tiku_editdoc_anchor_create(doc, 2, TIKU_EDIT_RIGHT, &right) ==
          TIKU_EDIT_OK &&
          tiku_editdoc_view_attach(doc, &view) == TIKU_EDIT_OK &&
          tiku_editdoc_view_select(doc, view, 1, 3) == TIKU_EDIT_OK,
          "DOC-012", "anchors and a selection");
    generation = tiku_editdoc_generation(doc);
    check(tiku_editdoc_load(doc, "xyz\n", 4) == TIKU_EDIT_OK &&
          reads(doc, "xyz\n") && !tiku_editdoc_modified(doc) &&
          !tiku_editdoc_can_undo(doc) &&
          tiku_editdoc_generation(doc) == generation + 1, "DOC-012",
          "replaced, history cleared, saved, generation raised");
    check(tiku_editdoc_anchor_pos(doc, left, &pos) == TIKU_EDIT_OK && pos == 0,
          "DOC-012", "a left anchor goes to the start");
    check(tiku_editdoc_anchor_pos(doc, right, &pos) == TIKU_EDIT_OK && pos == 4,
          "DOC-012", "a right anchor goes to the end");
    check(tiku_editdoc_view_selection(doc, view, &anchor, &caret) ==
          TIKU_EDIT_OK && anchor == 0 && caret == 0, "DOC-012",
          "a selection collapses to the start");
    tiku_editdoc_release(doc);
}

/** @brief Replace [s, e) of @p doc by @p text as one transaction. */
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

/** @brief Document/InsertOneLine and CellBuffer/ReadOnly, each on a fresh
 *         document.  Upstream's LineStartPosition has no counterpart. */
static void
upstream_document(void)
{
    tiku_editdoc_t *doc = doc_of("");

    check(put(doc, 0, 0, "Scintilla") == TIKU_EDIT_OK &&
          tiku_editdoc_length(doc) == 9 && tiku_editdoc_lines(doc) == 1 &&
          tiku_editdoc_line_start(doc, 0) == 0 &&
          tiku_editdoc_line_of(doc, 0) == 0 &&
          tiku_editdoc_line_start(doc, 1) == 9 &&
          tiku_editdoc_line_of(doc, 9) == 0 && tiku_editdoc_can_undo(doc) &&
          !tiku_editdoc_can_redo(doc), "Document/InsertOneLine",
          "one line of 9, undoable");
    tiku_editdoc_release(doc);

    doc = doc_of("");
    check(!tiku_editdoc_read_only(doc), "CellBuffer/ReadOnly", "writable");
    tiku_editdoc_set_read_only(doc, 1);
    check(tiku_editdoc_read_only(doc) &&
          put(doc, 0, 0, "Scintilla") == TIKU_EDIT_READ_ONLY &&
          tiku_editdoc_length(doc) == 0, "CellBuffer/ReadOnly",
          "an insertion into a read-only document is refused");
    tiku_editdoc_release(doc);
}

/** @brief testUniConversion.cxx, UTF8Classify, adapted from classifying
 *         the first character to accepting or refusing the whole text.
 *         Upstream flags U+FDD0, U+FFFE, U+FFFF and U+1FFFF, which are
 *         noncharacters, and Tiku keeps them: they are well-formed UTF-8. */
static void
utf8_classify(void)
{
    static const struct { const char *s, *section; int ok; } cases[] = {
        { "a", "Simple ASCII", 1 },
        { "\xF5", "Invalid Too large lead", 0 },
        { "\xC0\x80", "Overlong", 0 },
        { "\x80", "single trail byte", 0 },
        { "\xD0", "2 byte lead, string less than 2 long", 0 },
        { "\xEF", "3 byte lead, string less than 3 long", 0 },
        { "\xF0", "4 byte lead, string less than 4 long", 0 },
        { "\xD0q", "2 byte lead trail is invalid", 0 },
        { "\xE2qq", "3 byte lead invalid trails", 0 },
        { "\xF0xyz", "4 byte bad trails", 0 },
        { "\xD0\x80", "2 byte valid character", 1 },
        { "\xE0\x80\xAF", "3 byte lead, overlong", 0 },
        { "\xED\xA0\x80", "3 byte lead, surrogate", 0 },
        { "\xEF\xBF\xBE", "FFFE non-character", 1 },
        { "\xEF\xBF\xBF", "FFFF non-character", 1 },
        { "\xEF\xB7\x90", "FDD0 non-character", 1 },
        { "\xE2\x82\xAC", "3 byte valid character", 1 },
        { "\xF0\x9F\xBF\xBF", "1FFFF non-character", 1 },
        { "\xF4\x90\x80\x80", "1 Greater than max Unicode 110000", 0 },
        { "\xF0\x80\x80\x80", "4 byte overlong", 0 },
        { "\xF0\x9F\x8C\x90", "4 byte valid character", 1 },
        { "\xE2\x82q", "3 byte lead invalid 2nd trail", 0 },
        { "\xF0\x9Fq\x9F", "4 byte lead invalid 2nd trail", 0 },
        { "\xF0\x9F\x9Fq", "4 byte lead invalid 3rd trail", 0 }
    };
    size_t i;

    for (i = 0; i < sizeof cases / sizeof *cases; i++) {
        tiku_editdoc_t *doc = doc_of("");
        tiku_edit_status_t st = put(doc, 0, 0, cases[i].s);

        check(cases[i].ok ? st == TIKU_EDIT_OK && reads(doc, cases[i].s)
                          : st == TIKU_EDIT_INVALID_TEXT && reads(doc, ""),
              "UTF8Classify", cases[i].section);
        tiku_editdoc_release(doc);
    }
}

int
main(void)
{
    empty();
    text_policy();
    line_endings();
    reading();
    limits();
    positions();
    read_only();
    snapshots();
    loading();
    upstream_document();
    utf8_classify();
    printf("%s\n", fails ? "FAIL: editor document" :
           "PASS: editor document -- UTF-8 and NUL refusal, line endings, "
           "reads, limits, positions, read-only, snapshots, load");
    return fails != 0;
}
