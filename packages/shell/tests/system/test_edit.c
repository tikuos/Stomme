/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_edit.c - the in-place editor, and the Select window's persistence.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <string.h>

#include "tiku_clipboard.h"
#include "tiku_field.h"
#include "tiku_trk_select.h"

static int
ten_per_char(const char *text, int n, void *ctx)
{
    (void)text; (void)ctx;
    return n * 10;
}

static int fails;

static void
check(int cond, const char *row, const char *what)
{
    printf("  [%s] %-9s %s\n", cond ? "PASS" : "FAIL", row, what);
    if (!cond) { fails++; }
}

/** @brief Type a string one character at a time. */
static void
type(tiku_field_t *e, const char *s)
{
    while (*s != '\0') {
        (void)tiku_field_key(e, TIKU_FIELD_CHAR, 0, *s++);
    }
}

int
main(void)
{
    tiku_field_t e;
    tiku_trk_select_t w;
    int from, to, x, y;

    printf("in-place editing\n");

    tiku_field_start(&e, "readme.txt");
    check(e.active && e.len == 10, "MA-043", "the editor opens on the name");
    check(tiku_field_selection(&e, &from, &to) && from == 0 && to == 10,
          "PVS-014", "and opens with the whole name selected");

    type(&e, "n");
    check(strcmp(e.text, "n") == 0,
          "PVS-014", "so the first keystroke REPLACES rather than appends");

    tiku_field_start(&e, "readme.txt");
    (void)tiku_field_key(&e, TIKU_FIELD_END, 0, 0);
    check(!tiku_field_selection(&e, NULL, NULL) && e.caret == 10,
          "MA-043", "End drops the selection and goes to the end");
    type(&e, "!");
    check(strcmp(e.text, "readme.txt!") == 0,
          "MA-043", "typing then appends");

    (void)tiku_field_key(&e, TIKU_FIELD_BACKSPACE, 0, 0);
    check(strcmp(e.text, "readme.txt") == 0,
          "MA-043", "backspace removes one character");

    (void)tiku_field_key(&e, TIKU_FIELD_HOME, 0, 0);
    (void)tiku_field_key(&e, TIKU_FIELD_DELETE, 0, 0);
    check(strcmp(e.text, "eadme.txt") == 0,
          "MA-043", "delete removes forwards");

    (void)tiku_field_key(&e, TIKU_FIELD_RIGHT, 1, 0);
    (void)tiku_field_key(&e, TIKU_FIELD_RIGHT, 1, 0);
    check(tiku_field_selection(&e, &from, &to) && from == 0 && to == 2,
          "MA-043", "shift+arrow extends a selection from the anchor");
    (void)tiku_field_key(&e, TIKU_FIELD_LEFT, 1, 0);
    check(tiku_field_selection(&e, &from, &to) && from == 0 && to == 1,
          "MA-043", "and shrinks it back rather than starting a new one");

    /* Over a SELECTION, backspace deletes the selection and only the
     * selection -- found untested when a mutation of exactly that branch
     * sailed through the suite. */
    tiku_field_start(&e, "readme.txt");
    (void)tiku_field_key(&e, TIKU_FIELD_BACKSPACE, 0, 0);
    check(e.len == 0 && e.text[0] == '\0', "MA-043",
          "backspace over a selection removes the selection, not one "
          "character off its end");

    tiku_field_start(&e, "x");
    (void)tiku_field_key(&e, TIKU_FIELD_END, 0, 0);
    check(tiku_field_key(&e, TIKU_FIELD_COMMIT, 0, 0) ==
          TIKU_FIELD_COMMITTED && !e.active,
          "MA-043", "Return commits and closes");
    tiku_field_start(&e, "x");
    check(tiku_field_key(&e, TIKU_FIELD_CANCEL, 0, 0) ==
          TIKU_FIELD_CANCELLED && !e.active,
          "MA-043", "Escape abandons and closes");
    check(tiku_field_key(&e, TIKU_FIELD_CHAR, 0, 'q') ==
          TIKU_FIELD_IGNORED,
          "MA-043", "a closed editor takes no more keys");

    printf("\ntext clipboard routing\n");
    tiku_field_start(&e, "alpha");
    (void)tiku_field_key(&e, TIKU_FIELD_COPY, 0, 0);
    (void)tiku_field_key(&e, TIKU_FIELD_END, 0, 0);
    (void)tiku_field_key(&e, TIKU_FIELD_PASTE, 0, 0);
    check(strcmp(e.text, "alphaalpha") == 0, "PVK-039",
          "copy and paste stay inside the focused text editor");
    (void)tiku_field_key(&e, TIKU_FIELD_HOME, 0, 0);
    (void)tiku_field_key(&e, TIKU_FIELD_RIGHT, 1, 0);
    (void)tiku_field_key(&e, TIKU_FIELD_RIGHT, 1, 0);
    (void)tiku_field_key(&e, TIKU_FIELD_CUT, 0, 0);
    check(strcmp(e.text, "phaalpha") == 0, "PVK-039",
          "cut removes only the selected text and records it");
    tiku_clipboard_set("kept");
    (void)tiku_field_key(&e, TIKU_FIELD_END, 0, 0);
    (void)tiku_field_key(&e, TIKU_FIELD_COPY, 0, 0);
    (void)tiku_field_key(&e, TIKU_FIELD_CUT, 0, 0);
    check(strcmp(tiku_clipboard_text(), "kept") == 0 &&
          strcmp(e.text, "phaalpha") == 0, "EDR-CLIP-008",
          "copy and cut with nothing selected leave the clipboard");
    tiku_field_start(&e, e.text);
    check(strcmp(e.text, "phaalpha") == 0 && e.sel_a == 0 && e.sel_b == 8,
          "IDE-FIND-001", "a field restarted on its own text keeps it");

    printf("\ncharacters a name cannot contain (MA-046)\n");
    tiku_field_start(&e, "notes");
    tiku_field_deny(&e, "/");
    (void)tiku_field_key(&e, TIKU_FIELD_END, 0, 0);
    type(&e, "a/b");
    check(strcmp(e.text, "notesab") == 0, "MA-046",
          "'/' is swallowed as it is typed, not left in the field to be "
          "refused at commit");
    (void)tiku_field_key(&e, TIKU_FIELD_CHAR, 0, 0x01);
    check(strcmp(e.text, "notesab") == 0, "MA-046",
          "and so is a control character");
    tiku_field_start(&e, "x");
    (void)tiku_field_key(&e, TIKU_FIELD_END, 0, 0);
    type(&e, "/");
    check(strcmp(e.text, "x/") == 0, "MA-046",
          "a field with no deny set still takes everything printable");

    printf("\nread-only rename affordance (MA-048)\n");
    tiku_field_start(&e, "read-only name");
    tiku_field_set_editable(&e, 0);
    (void)tiku_field_key(&e, TIKU_FIELD_HOME, 0, 0);
    (void)tiku_field_key(&e, TIKU_FIELD_RIGHT, 1, 0);
    check(tiku_field_selection(&e, &from, &to) && from == 0 && to == 1,
          "MA-048", "a read-only editor remains selectable");
    check(tiku_field_key(&e, TIKU_FIELD_CHAR, 0, 'X') ==
              TIKU_FIELD_IGNORED && strcmp(e.text, "read-only name") == 0,
          "MA-048", "read-only text cannot be changed or pasted");

    printf("\nthe Select window\n");
    tiku_trk_select_init(&w);
    check(w.rule == 3 &&
          tiku_trk_select_rule(w.rule) == TIKU_TRK_MATCH_GLOB,
          "AW-080", "wildcard is the default rule");
    check(w.ignore_case == 1 && w.invert == 0,
          "AW-080", "ignore case on, invert off");
    check(tiku_trk_select_rule(0) == TIKU_TRK_MATCH_STARTS &&
          tiku_trk_select_rule(1) == TIKU_TRK_MATCH_ENDS &&
          tiku_trk_select_rule(2) == TIKU_TRK_MATCH_CONTAINS &&
          tiku_trk_select_rule(4) == TIKU_TRK_MATCH_REGEX,
          "Q-047", "the five rules are offered in Tracker's order");

    tiku_trk_select_show(&w, 300, 300, 320, 120, 1000, 700);
    type(&w.expr, "gpio*");
    w.ignore_case = 0;
    tiku_trk_select_hide(&w);
    check(!w.open, "AW-081", "closing hides it");
    tiku_trk_select_show(&w, 300, 300, 320, 120, 1000, 700);
    check(strcmp(w.expr.text, "gpio*") == 0 && w.ignore_case == 0,
          "AW-105", "and the expression and options are still there");

    tiku_trk_select_place(0, 0, 320, 120, 1000, 700, &x, &y);
    check(x == 20 && y == 20,
          "AW-081", "a pointer at the corner still leaves it 20px on screen");
    tiku_trk_select_place(1000, 700, 320, 120, 1000, 700, &x, &y);
    check(x == 1000 - 20 - 320 && y == 700 - 20 - 120,
          "AW-081", "and clamps at the far edge too");
    tiku_trk_select_place(500, 350, 320, 120, 1000, 700, &x, &y);
    check(x == 500 - 160 && y == 350 - 60,
          "AW-081", "otherwise it centres on the pointer");

    printf("\nthe editor grows, caps and slides (MA-049)\n");
    {
        tiku_field_t g;
        int w = 0, sc = 0;

        tiku_field_start(&g, "abcde");        /* 5 chars, 50 px */
        tiku_field_metrics(&g, 40, 200, ten_per_char, NULL, &w, &sc);
        check(w == 62 && sc == 0, "MA-049",
              "the box grows with the text and slides nothing while it "
              "fits");
        tiku_field_start(&g, "abcdefghijklmnopqrstuvwxyz");  /* 260 px */
        g.caret = g.len;
        tiku_field_metrics(&g, 40, 200, ten_per_char, NULL, &w, &sc);
        check(w == 200, "MA-049",
              "past the cap the box stays at the column's width");
        check(sc > 0 && sc <= 272 - 200, "MA-049",
              "and the text slides so the caret at the end stays visible");
        g.caret = 0;
        tiku_field_metrics(&g, 40, 200, ten_per_char, NULL, &w, &sc);
        check(sc == 0, "MA-049",
              "with the caret at the head nothing slides -- the slide "
              "follows the caret, not the length");
    }

    printf("\n%s\n", fails ? "FAILURES" : "editing and Select hold");
    return fails ? 1 : 0;
}
