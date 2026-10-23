/* Bounded UTF-8 commits, transport and editing regressions.
 * SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "tiku_text_input.h"
#include "tiku_field_event.h"
#include "tiku_textview.h"
#include "tiku_clipboard.h"

static void transport(void)
{
    const char *phrase = "Hello café 日本語 🌍";
    tiku_text_input_t send = {0}, receive = {0};
    tiku_input_event_t e, first;
    assert(sizeof(tiku_text_input_t) == TIKU_TEXT_INPUT_MAX + 4 * sizeof(int));
    assert(sizeof e.text == 8); /* Existing wire payload is unchanged. */
    assert(tiku_text_input_set(&send, phrase, strlen(phrase)));
    int count = 0, result = 0;
    while (tiku_text_input_next(&send, &e)) {
        if (!count) first = e;
        assert(!e.key && !e.modifiers);
        result = tiku_text_input_event(&receive, &e);
        assert(result == ((e.button & TIKU_TEXT_END) ? TIKU_TEXT_COMPLETE : TIKU_TEXT_PENDING));
        count++;
    }
    assert(count > 2 && result == TIKU_TEXT_COMPLETE && !strcmp(receive.text, phrase));
    assert(tiku_text_input_event(&receive, &e) == TIKU_TEXT_REJECTED); /* Duplicate tail. */
    assert(tiku_text_input_event(&receive, &first) == TIKU_TEXT_PENDING);
    e = first; e.button = 0; e.dx++;
    assert(tiku_text_input_event(&receive, &e) == TIKU_TEXT_REJECTED);
    assert(tiku_text_input_event(&receive, &first) == TIKU_TEXT_PENDING);
    e = (tiku_input_event_t){.type=TIKU_EVENT_DEACTIVATED};
    assert(tiku_text_input_event(&receive, &e) == TIKU_TEXT_IGNORED && !receive.active);
    e = first; e.button = TIKU_TEXT_END;
    assert(tiku_text_input_event(&receive, &e) == TIKU_TEXT_REJECTED);
    e = first; e.key = 'x';
    assert(tiku_text_input_event(&receive, &e) == TIKU_TEXT_REJECTED);
    e = first; memset(e.text, 'a', sizeof e.text);
    assert(tiku_text_input_event(&receive, &e) == TIKU_TEXT_REJECTED);
    const char *bad[] = {"\xc0\xaf", "\xed\xa0\x80", "\xf4\x90\x80\x80", "\xe2\x82", "\x1b", "\x7f"};
    for (size_t i = 0; i < sizeof bad / sizeof *bad; i++)
        assert(!tiku_text_input_set(&send, bad[i], strlen(bad[i])));
    assert(!tiku_text_input_set(&send, "a\0b", 3));
    char large[TIKU_TEXT_INPUT_MAX + 1]; memset(large, 'x', sizeof large);
    assert(!tiku_text_input_set(&send, large, TIKU_TEXT_INPUT_MAX));
    assert(tiku_text_input_set(&send, large, TIKU_TEXT_INPUT_MAX - 1));
    while (tiku_text_input_next(&send, &e)) result = tiku_text_input_event(&receive, &e);
    assert(result == TIKU_TEXT_COMPLETE && receive.length == TIKU_TEXT_INPUT_MAX - 1);
    assert(tiku_text_input_set(&receive, receive.text, receive.length)); /* Aliased source. */
    assert(tiku_text_input_set(&send, "", 0) && !tiku_text_input_next(&send, &e));
    puts("PASS: UTF-8 framing, bounds, malformed and interrupted commits");
}

static void fields(void)
{
    tiku_field_t f;
    tiku_field_input_t input;
    tiku_text_input_t send;
    tiku_input_event_t e;
    tiku_rect_t rect = {0,0,240,24};
    const char *word = "café 日本語";
    tiku_field_start(&f, "original");
    tiku_field_input_init(&input);
    assert(tiku_text_input_set(&send, word, strlen(word)));
    while (tiku_text_input_next(&send, &e)) {
        assert(tiku_field_event(&f, &input, &e, rect, 1) == TIKU_FIELD_HANDLED);
        if (!(e.button & TIKU_TEXT_END)) assert(!strcmp(f.text, "original"));
    }
    assert(!strcmp(f.text, word) && input.nundo == 1);
    e = (tiku_input_event_t){.type=TIKU_EVENT_KEY_DOWN, .key='z', .modifiers=TIKU_MOD_CMD};
    tiku_field_event(&f, &input, &e, rect, 1);
    assert(!strcmp(f.text, "original"));
    assert(tiku_field_commit(&f, "é🌍") == TIKU_FIELD_HANDLED);
    assert(f.caret == 6);
    tiku_field_key(&f, TIKU_FIELD_LEFT, 0, 0); assert(f.caret == 2);
    tiku_field_key(&f, TIKU_FIELD_BACKSPACE, 0, 0); assert(!strcmp(f.text, "🌍"));
    tiku_field_key(&f, TIKU_FIELD_DELETE, 0, 0); assert(!*f.text);
    tiku_field_start(&f, "keep");
    char large[TIKU_FIELD_MAX + 1]; memset(large, 'a', sizeof large - 1); large[sizeof large - 1] = 0;
    assert(tiku_field_commit(&f, large) == TIKU_FIELD_REJECTED && !strcmp(f.text, "keep"));
    assert(f.sel_a == 0 && f.sel_b == 4);
    assert(tiku_field_commit(&f, "line\nbreak") == TIKU_FIELD_REJECTED);
    tiku_field_deny(&f, "/");
    assert(tiku_field_commit(&f, "a/b") == TIKU_FIELD_REJECTED);
    tiku_clipboard_set("café");
    assert(tiku_field_key(&f, TIKU_FIELD_PASTE, 0, 0) == TIKU_FIELD_HANDLED);
    assert(!strcmp(f.text, "café"));
    tiku_field_key(&f, TIKU_FIELD_SELECT_ALL, 0, 0);
    tiku_clipboard_set(large);
    assert(tiku_field_key(&f, TIKU_FIELD_PASTE, 0, 0) == TIKU_FIELD_REJECTED);
    assert(!strcmp(f.text, "café") && f.sel_a == 0 && f.sel_b == 5);
    tiku_clipboard_set("bad/name");
    assert(tiku_field_key(&f, TIKU_FIELD_PASTE, 0, 0) == TIKU_FIELD_REJECTED);
    assert(!strcmp(f.text, "café"));
    puts("PASS: atomic field commit, undo, UTF-8 navigation and rejection");
}

static void documents(void)
{
    tiku_textview_t tv;
    tiku_textview_init(&tv);
    assert(!tv.line[0].text && !*tiku_textview_line(&tv, 0));
    tiku_textview_newline(&tv);
    tiku_textview_backspace(&tv);
    assert(tv.nline == 1 && !*tiku_textview_line(&tv, 0));
    tiku_textview_set(&tv, "original\ntext");
    tiku_textview_select_all(&tv);
    assert(tiku_textview_commit(&tv, "é🌍\n日本語"));
    assert(!strcmp(tiku_textview_line(&tv, 0), "é🌍"));
    assert(tiku_textview_undo(&tv) && !strcmp(tiku_textview_line(&tv, 0), "original"));
    assert(!strcmp(tiku_textview_line(&tv, 1), "text"));
    assert(tiku_textview_redo(&tv) && !strcmp(tiku_textview_line(&tv, 1), "日本語"));
    tiku_textview_place(&tv, 0, 5); assert(tv.cx == 2); /* Inside globe. */
    tiku_textview_delete(&tv); assert(!strcmp(tiku_textview_line(&tv, 0), "é"));
    assert(tiku_textview_undo(&tv) && !strcmp(tiku_textview_line(&tv, 0), "é🌍"));
    tiku_textview_place(&tv, 0, 6);
    tiku_textview_backspace(&tv); assert(!strcmp(tiku_textview_line(&tv, 0), "é"));
    assert(tiku_textview_undo(&tv) && !strcmp(tiku_textview_line(&tv, 0), "é🌍"));
    tiku_textview_place(&tv, 0, 0);
    tiku_textview_move(&tv, TIKU_TEXTVIEW_RIGHT); assert(tv.cx == 2);
    tiku_textview_move(&tv, TIKU_TEXTVIEW_RIGHT); assert(tv.cx == 6);
    tiku_textview_move(&tv, TIKU_TEXTVIEW_LEFT); assert(tv.cx == 2);
    tiku_textview_move(&tv, TIKU_TEXTVIEW_DOWN); assert(tv.cy == 1 && tv.cx == 0);
    char full[TIKU_TEXTVIEW_LINE_MAX]; memset(full, 'x', sizeof full - 1); full[sizeof full - 1] = 0;
    tiku_textview_set(&tv, full);
    tiku_textview_place(&tv, 0, 1); tiku_textview_select_begin(&tv); tiku_textview_place(&tv, 0, 2);
    assert(!tiku_textview_commit(&tv, "é"));
    assert(!strcmp(tiku_textview_line(&tv, 0), full) && tv.selecting && !tiku_textview_modified(&tv));
    assert(tiku_textview_commit(&tv, "y"));
    assert(tiku_textview_undo(&tv) && !strcmp(tiku_textview_line(&tv, 0), full));
    char two[2 * TIKU_TEXTVIEW_LINE_MAX + 2];
    snprintf(two, sizeof two, "%s\n%s", full, full);
    tiku_textview_set(&tv, two);
    tiku_textview_place(&tv, 0, 900); tiku_textview_select_begin(&tv);
    tiku_textview_place(&tv, 1, 900);
    /* Every line the result has fits, and so does the one the selection's
     * removal leaves, so the commit lands and undoes whole. */
    assert(tiku_textview_commit(&tv, "é\n"));
    assert(tv.nline == 2 && tiku_textview_line_len(&tv, 0) == 902 &&
           tiku_textview_line_len(&tv, 1) == 123);
    assert(tiku_textview_undo(&tv) && tv.nline == 2);
    assert(!strcmp(tiku_textview_line(&tv, 0), full) && !tiku_textview_modified(&tv));
    /* Two adjacent new lines that would not fit as one are no bar. */
    tiku_textview_set(&tv, full);
    tiku_textview_place(&tv, 0, 500);
    assert(tiku_textview_commit(&tv, "\nBB"));
    assert(tv.nline == 2 && tiku_textview_line_len(&tv, 0) == 500 &&
           tiku_textview_line_len(&tv, 1) == 525);
    assert(tiku_textview_undo(&tv) && tv.nline == 1);
    assert(!strcmp(tiku_textview_line(&tv, 0), full) && !tiku_textview_modified(&tv));
    assert(tiku_textview_redo(&tv) && tiku_textview_line_len(&tv, 1) == 525);
    /* The line the selection's removal leaves must still fit, as undo
     * rebuilds it: a selection of 900 bytes kept on each side is refused. */
    char wide[2 * TIKU_TEXTVIEW_LINE_MAX + 2];
    memset(wide, 'a', 1000); wide[1000] = '\n';
    memset(wide + 1001, 'b', 1000); wide[2001] = 0;
    tiku_textview_set(&tv, wide);
    tiku_textview_place(&tv, 0, 900); tiku_textview_select_begin(&tv);
    tiku_textview_place(&tv, 1, 100);
    assert(!tiku_textview_commit(&tv, "\n"));
    assert(tv.nline == 2 && tv.selecting && !tiku_textview_modified(&tv));
    /* One that does fit still lands, and undoes whole. */
    tiku_textview_set(&tv, "short line");
    tiku_textview_place(&tv, 0, 5);
    assert(tiku_textview_commit(&tv, "\nBB"));
    assert(tv.nline == 2 && !strcmp(tiku_textview_line(&tv, 1), "BB line"));
    assert(tiku_textview_undo(&tv));
    assert(tv.nline == 1 && !strcmp(tiku_textview_line(&tv, 0), "short line"));
    /* Undo takes back a commit whose last line would not fit beside the
     * caret's column, and redo puts it back. */
    char head[601], lines3[603];
    memset(head, 'a', 600); head[600] = 0;
    lines3[0] = '\n'; memset(lines3 + 1, 'b', 100); lines3[101] = '\n';
    memset(lines3 + 102, 'c', 500); lines3[602] = 0;
    tiku_textview_set(&tv, head);
    tiku_textview_place(&tv, 0, 600);
    assert(tiku_textview_commit(&tv, lines3) && tv.nline == 3);
    assert(tiku_textview_undo(&tv) && tv.nline == 1);
    assert(!strcmp(tiku_textview_line(&tv, 0), head));
    assert(!tiku_textview_modified(&tv));
    assert(tiku_textview_redo(&tv) && tv.nline == 3);
    assert(tiku_textview_line_len(&tv, 0) == 600 &&
           tiku_textview_line_len(&tv, 1) == 100 &&
           tiku_textview_line_len(&tv, 2) == 500);
    tiku_textview_free(&tv);
    assert(!tv.line[0].text && tv.nline == 1);
    assert(tiku_textview_commit(&tv, "é")); /* Freed model remains reusable. */
    tiku_textview_free(&tv);
    puts("PASS: document UTF-8 commit, grouped undo/redo, deletion and line bounds");
}

int main(void)
{
    char buffer[8] = "é";
    assert(tiku_text_input_append(buffer, sizeof buffer, "🌍"));
    assert(!tiku_text_input_append(buffer, sizeof buffer, "é"));
    assert(!strcmp(buffer, "é🌍"));
    tiku_text_input_backspace(buffer, sizeof buffer);
    assert(!strcmp(buffer, "é"));
    assert(!tiku_text_input_append(buffer, sizeof buffer, "\n"));
    assert(!tiku_text_input_append(buffer, sizeof buffer, "\xff"));
    assert(!tiku_text_input_append(buffer, 1, "a")); /* Unterminated within bound. */
    tiku_text_input_backspace(buffer, sizeof buffer);
    assert(!*buffer);
    assert(tiku_text_input_append(buffer, sizeof buffer, "1234567"));
    assert(!tiku_text_input_append(buffer, sizeof buffer, "8"));
    transport(); fields(); documents();
    return 0;
}
