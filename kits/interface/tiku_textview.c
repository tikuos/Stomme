/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_textview.c - the document's text, caret, selection, journal and
 * scroll.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdlib.h>
#include <string.h>

#include "tiku_textview.h"
#include "tiku_text_input.h"

static void caret_clamp(tiku_textview_t *tv);

/** @brief The byte offset of the codepoint before byte @p at of @p s. */
static int text_previous(const char *s, int at)
{
    int start = at > 0 ? at - 1 : 0;
    while (start > 0 && at - start < 4 &&
           ((unsigned char)s[start] & 0xc0) == 0x80) start--;
    return start;
}
/**
 * @brief The byte offset after the codepoint at byte @p at of @p s, at
 *        most @p length.
 */
static int text_next(const char *s, int at, int length)
{
    int end = at < length ? at + 1 : length;
    while (end < length && end - at < 4 &&
           ((unsigned char)s[end] & 0xc0) == 0x80) end++;
    return end;
}

/** @brief Replace one line's text with @p len bytes of @p text. */
static void
line_set(tiku_textview_line_t *l, const char *text, int len)
{
    char *copy;

    if (len < 0) {
        len = 0;
    }
    copy = malloc((size_t)len + 1u);
    if (copy == NULL) {
        return;                 /* on failure the old text stays */
    }
    if (len > 0 && text != NULL) {
        memcpy(copy, text, (size_t)len);
    }
    copy[len] = '\0';
    free(l->text);
    l->text = copy;
    l->len = len;
}

/** @brief A NUL-closed copy of @p n bytes of @p src. */
static char *
dupn(const char *src, int n)
{
    char *p = malloc((size_t)n + 1u);

    if (p == NULL) {
        return NULL;
    }
    if (n > 0 && src != NULL) {
        memcpy(p, src, (size_t)n);
    }
    p[n] = '\0';
    return p;
}

/** @brief Where @p count bytes from line @p y, column @p x end, a line
 *         break counting as one byte; the text's end stops the walk. */
static void
span_end(const tiku_textview_t *tv, int y, int x, int count, int *ey, int *ex)
{
    while (count > 0) {
        int room = tv->line[y].len - x;

        if (count <= room) {
            x += count;
            count = 0;
        } else if (y + 1 < tv->nline) {
            count -= room + 1;
            y++;
            x = 0;
        } else {
            x = tv->line[y].len;
            count = 0;
        }
    }
    *ey = y;
    *ex = x;
}

/**
 * @brief Remove line @p y0 column @p x0 up to line @p y1 column @p x1 in
 *        one step, the caret left where the span began.
 *
 * The line that remains is built whole, so no line past the limit is
 * made on the way.  @return 0, changing nothing, when that line would
 * not fit or cannot be allocated.
 */
static int
span_delete(tiku_textview_t *tv, int y0, int x0, int y1, int x1)
{
    int tail = tv->line[y1].len - x1, gone = y1 - y0, i;
    char *joined;

    if (x0 + tail >= TIKU_TEXTVIEW_LINE_MAX) {
        return 0;
    }
    joined = malloc((size_t)(x0 + tail) + 1u);
    if (joined == NULL) {
        return 0;
    }
    memcpy(joined, tiku_textview_line(tv, y0), (size_t)x0);
    memcpy(joined + x0, tiku_textview_line(tv, y1) + x1, (size_t)tail);
    joined[x0 + tail] = '\0';
    for (i = y0; i <= y1; i++) {
        free(tv->line[i].text);
    }
    tv->line[y0].text = joined;
    tv->line[y0].len = x0 + tail;
    for (i = y1 + 1; i < tv->nline; i++) {
        tv->line[i - gone] = tv->line[i];
    }
    for (i = tv->nline - gone; i < tv->nline; i++) {
        memset(&tv->line[i], 0, sizeof tv->line[0]);
    }
    tv->nline -= gone;
    tv->cy = y0;
    tv->cx = x0;
    return 1;
}

/** @brief The line breaks in @p n bytes of @p text put at line @p y,
 *         column @p x, or -1 when a line they make, or the line count,
 *         would pass its limit. */
static int
span_breaks(const tiku_textview_t *tv, int y, int x, const char *text, int n)
{
    int breaks = 0, run = x, i;

    for (i = 0; i < n; i++) {
        if (text[i] != '\n') {
            run++;
        } else if (run >= TIKU_TEXTVIEW_LINE_MAX) {
            return -1;
        } else {
            breaks++;
            run = 0;
        }
    }
    if (run + tv->line[y].len - x >= TIKU_TEXTVIEW_LINE_MAX ||
        tv->nline + breaks > TIKU_TEXTVIEW_LINES_MAX) {
        return -1;
    }
    return breaks;
}

/** @brief Free the first @p k lines of @p made, then @p made; @return 0. */
static int
span_abandon(tiku_textview_line_t *made, int k)
{
    while (k-- > 0) {
        free(made[k].text);
    }
    free(made);
    return 0;
}

/**
 * @brief Insert @p n bytes of @p text, line breaks included, at line @p y
 *        column @p x in one step, the caret left after them.
 *
 * Every line the insertion makes is built before anything changes, so no
 * line past the limit is made on the way.  @return 0, changing nothing,
 * when a line or the line count would not fit or memory runs out.
 */
static int
span_insert(tiku_textview_t *tv, int y, int x, const char *text, int n)
{
    int breaks = span_breaks(tv, y, x, text, n), i, k = 0, start = 0, seg = 0;
    const char *old = tiku_textview_line(tv, y);  /* "" if not yet held */
    tiku_textview_line_t *made;

    if (breaks < 0 ||
        (made = calloc((size_t)breaks + 1u, sizeof *made)) == NULL) {
        return 0;
    }
    for (i = 0; k <= breaks; i++) {
        if (i < n && text[i] != '\n') {
            continue;
        }
        {
            int head = k == 0 ? x : 0;
            int end = k == breaks ? tv->line[y].len - x : 0;
            char *t;

            seg = i - start;
            t = malloc((size_t)(head + seg + end) + 1u);
            if (t == NULL) {
                return span_abandon(made, k);
            }
            memcpy(t, old, (size_t)head);
            memcpy(t + head, text + start, (size_t)seg);
            memcpy(t + head + seg, old + x, (size_t)end);
            t[head + seg + end] = '\0';
            made[k].text = t;
            made[k].len = head + seg + end;
            k++;
            start = i + 1;
        }
    }
    free(tv->line[y].text);
    for (i = tv->nline - 1; i > y; i--) {
        tv->line[i + breaks] = tv->line[i];
    }
    for (k = 0; k <= breaks; k++) {
        tv->line[y + k] = made[k];
    }
    free(made);
    tv->nline += breaks;
    tv->cy = y + breaks;
    tv->cx = breaks ? seg : x + seg;
    return 1;
}

/** @brief Empty the journal, freeing every entry's text. */
static void
journal_clear(tiku_textview_t *tv)
{
    int i;

    for (i = 0; i < tv->undo_n; i++) {
        free(tv->undo[i].text);
        tv->undo[i].text = NULL;
    }
    tv->undo_n = 0;
    tv->undo_at = 0;
    tv->saved_at = 0;
}

/**
 * @brief Write one edit into the journal, folding a keystroke run.
 *
 * A single character continuing the run above -- typed just after the
 * last, or rubbed out just before it -- grows that entry, so a word is
 * one step to undo.  Anything else, or a run that cannot grow, starts a
 * fresh entry, and the redo history above the present is let go the
 * moment a new edit lands.  Quiet while undo/redo is replaying.
 */
static void
record(tiku_textview_t *tv, int kind, int line, int col,
       const char *text, int len)
{
    struct tiku_textview_edit *top;
    int i;

    if (tv == NULL || !tv->recording) {
        return;
    }
    for (i = tv->undo_at; i < tv->undo_n; i++) {
        free(tv->undo[i].text);
        tv->undo[i].text = NULL;
    }
    tv->undo_n = tv->undo_at;

    if (len == 1 && text[0] != '\n' && tv->undo_n > 0) {
        top = &tv->undo[tv->undo_n - 1];
        if (top->coalesce && top->kind == kind && top->line == line) {
            if (kind == 1 && top->col + top->len == col) {
                char *g = dupn(top->text, top->len + 1);

                if (g != NULL) {
                    g[top->len] = text[0];
                    free(top->text);
                    top->text = g;
                    top->len++;
                    return;     /* typing forward: appended */
                }
            }
            if (kind == 2 && col == top->col) {
                char *g = dupn(top->text, top->len + 1);

                if (g != NULL) {
                    g[top->len] = text[0];
                    free(top->text);
                    top->text = g;
                    top->len++;
                    return;     /* forward delete at one place: appended */
                }
            }
            if (kind == 2 && col + 1 == top->col) {
                char *g = malloc((size_t)top->len + 2u);

                if (g != NULL) {
                    g[0] = text[0];
                    memcpy(g + 1, top->text, (size_t)top->len + 1u);
                    free(top->text);
                    top->text = g;
                    top->len++;
                    top->col = col;
                    return;     /* backspace run: prepended */
                }
            }
        }
    }

    if (tv->undo_n >= TIKU_TEXTVIEW_UNDO) {
        free(tv->undo[0].text);
        for (i = 1; i < tv->undo_n; i++) {
            tv->undo[i - 1] = tv->undo[i];
        }
        tv->undo_n--;
        tv->saved_at = (tv->saved_at > 0) ? tv->saved_at - 1 : -1;
    }
    top = &tv->undo[tv->undo_n];
    top->kind = kind;
    top->line = line;
    top->col = col;
    top->text = dupn(text, len);
    top->len = len;
    top->coalesce = (len == 1 && text[0] != '\n');
    top->group = tv->group_open;
    tv->undo_n++;
    tv->undo_at = tv->undo_n;
}

void
tiku_textview_group_begin(tiku_textview_t *tv)
{
    if (tv != NULL) {
        tv->group_seq++;
        tv->group_open = tv->group_seq;
    }
}

void
tiku_textview_group_end(tiku_textview_t *tv)
{
    if (tv != NULL) {
        tv->group_open = 0;
    }
}

static int sel_delete(tiku_textview_t *tv);

void
tiku_textview_init(tiku_textview_t *tv)
{
    if (tv == NULL) {
        return;
    }
    memset(tv, 0, sizeof *tv);
    /* One empty line, never none: every operation may assume the caret
     * has a line to stand on.  Its text is NULL, which reads as empty, so
     * clearing or freeing a view allocates nothing. */
    tv->nline = 1;
    tv->recording = 1;          /* the journal listens from the start */
}

void
tiku_textview_free(tiku_textview_t *tv)
{
    int i;

    if (tv == NULL) {
        return;
    }
    for (i = 0; i < tv->nline; i++) {
        free(tv->line[i].text);
        tv->line[i].text = NULL;
        tv->line[i].len = 0;
    }
    tv->nline = 0;
    journal_clear(tv);
    tiku_textview_init(tv);
}

void
tiku_textview_set(tiku_textview_t *tv, const char *text)
{
    const char *p, *nl;

    if (tv == NULL) {
        return;
    }
    tiku_textview_free(tv);
    tv->nline = 0;
    p = (text != NULL) ? text : "";
    for (;;) {
        int len;

        nl = strchr(p, '\n');
        len = (nl != NULL) ? (int)(nl - p) : (int)strlen(p);
        if (len > TIKU_TEXTVIEW_LINE_MAX - 1) {
            len = TIKU_TEXTVIEW_LINE_MAX - 1;
        }
        if (tv->nline >= TIKU_TEXTVIEW_LINES_MAX) {
            break;
        }
        memset(&tv->line[tv->nline], 0, sizeof tv->line[0]);
        line_set(&tv->line[tv->nline], p, len);
        tv->nline++;
        if (nl == NULL) {
            break;
        }
        p = nl + 1;
        if (*p == '\0') {
            /* The newline that ends a file closes the last line and
             * starts no empty one. */
            break;
        }
    }
    if (tv->nline == 0) {
        line_set(&tv->line[0], "", 0);
        tv->nline = 1;
    }
    tv->cy = 0;
    tv->cx = 0;
    tv->selecting = 0;
    journal_clear(tv);          /* a fresh document has nothing to undo */
    tv->recording = 1;
    tv->top = 0;
}

int
tiku_textview_lines(const tiku_textview_t *tv)
{
    return (tv != NULL && tv->nline > 0) ? tv->nline : 0;
}

const char *
tiku_textview_line(const tiku_textview_t *tv, int i)
{
    if (tv == NULL || i < 0 || i >= tv->nline ||
        tv->line[i].text == NULL) {
        return "";
    }
    return tv->line[i].text;
}

int
tiku_textview_line_len(const tiku_textview_t *tv, int i)
{
    if (tv == NULL || i < 0 || i >= tv->nline) {
        return 0;
    }
    return tv->line[i].len;
}

int
tiku_textview_modified(const tiku_textview_t *tv)
{
    /* Compared with the mark the last save left, so undoing back to a
     * save reads as unmodified. */
    return (tv != NULL) ? (tv->undo_at != tv->saved_at) : 0;
}

void
tiku_textview_saved(tiku_textview_t *tv)
{
    if (tv != NULL) {
        tv->saved_at = tv->undo_at;
        /* The run is closed at a save, so the next keystroke starts a
         * new entry and moves the present off the mark; a keystroke
         * folded into the marked entry leaves the view unmodified. */
        if (tv->undo_n > 0) {
            tv->undo[tv->undo_n - 1].coalesce = 0;
        }
    }
}

void
tiku_textview_caret(const tiku_textview_t *tv, int *line, int *col)
{
    if (line != NULL) { *line = (tv != NULL) ? tv->cy : 0; }
    if (col != NULL)  { *col  = (tv != NULL) ? tv->cx : 0; }
}

int
tiku_textview_top(const tiku_textview_t *tv)
{
    return (tv != NULL) ? tv->top : 0;
}

void
tiku_textview_place(tiku_textview_t *tv, int line, int col)
{
    if (tv == NULL || tv->nline <= 0) {
        return;
    }
    if (line < 0) { line = 0; }
    if (line >= tv->nline) { line = tv->nline - 1; }
    tv->cy = line;
    if (col < 0) { col = 0; }
    if (col > tv->line[line].len) { col = tv->line[line].len; }
    while (tv->recording && col > 0 && col < tv->line[line].len &&
           ((unsigned char)tv->line[line].text[col] & 0xc0) == 0x80) col--;
    tv->cx = col;
}

void
tiku_textview_select_begin(tiku_textview_t *tv)
{
    if (tv == NULL) {
        return;
    }
    if (!tv->selecting) {
        tv->ay = tv->cy;
        tv->ax = tv->cx;
        tv->selecting = 1;
    }
}

void
tiku_textview_select_none(tiku_textview_t *tv)
{
    if (tv != NULL) {
        tv->selecting = 0;
    }
}

void
tiku_textview_select_all(tiku_textview_t *tv)
{
    if (tv == NULL || tv->nline <= 0) {
        return;
    }
    tv->ay = 0;
    tv->ax = 0;
    tv->selecting = 1;
    tv->cy = tv->nline - 1;
    tv->cx = tv->line[tv->cy].len;
}

int
tiku_textview_selection(const tiku_textview_t *tv,
                        int *y0, int *x0, int *y1, int *x1)
{
    int ay, ax, by, bx;

    if (tv == NULL || !tv->selecting) {
        return 0;
    }
    if (tv->ay == tv->cy && tv->ax == tv->cx) {
        return 0;               /* an anchor at the caret is no span */
    }
    ay = tv->ay; ax = tv->ax;
    by = tv->cy; bx = tv->cx;
    if (ay > by || (ay == by && ax > bx)) {
        int ty = ay, tx = ax;

        ay = by; ax = bx;
        by = ty; bx = tx;
    }
    if (y0 != NULL) { *y0 = ay; }
    if (x0 != NULL) { *x0 = ax; }
    if (y1 != NULL) { *y1 = by; }
    if (x1 != NULL) { *x1 = bx; }
    return 1;
}

size_t
tiku_textview_selected(const tiku_textview_t *tv, char *out, size_t max)
{
    int y0, x0, y1, x1, line;
    size_t used = 0;

    if (out == NULL || max == 0u) {
        return 0;
    }
    out[0] = '\0';
    if (!tiku_textview_selection(tv, &y0, &x0, &y1, &x1)) {
        return 0;
    }
    for (line = y0; line <= y1; line++) {
        const char *text = tiku_textview_line(tv, line);
        int len = tv->line[line].len;
        int c0 = (line == y0) ? x0 : 0;
        int c1 = (line == y1) ? x1 : len;
        size_t n;

        if (c0 > len) { c0 = len; }
        if (c1 > len) { c1 = len; }
        n = (size_t)((c1 > c0) ? c1 - c0 : 0);
        /* Room for these characters, a newline unless this is the last
         * line, and the terminator. */
        if (used + n + (size_t)((line < y1) ? 1u : 0u) + 1u > max) {
            break;              /* the line's part whole or not at all */
        }
        memcpy(out + used, text + c0, n);
        used += n;
        if (line < y1) {
            out[used++] = '\n';
        }
    }
    out[used] = '\0';
    return used;
}

size_t
tiku_textview_selected_size(const tiku_textview_t *tv)
{
    int y0, x0, y1, x1, line;
    size_t size = 0;

    if (!tiku_textview_selection(tv, &y0, &x0, &y1, &x1)) {
        return 0;
    }
    for (line = y0; line <= y1; line++) {
        int len = tv->line[line].len;
        int c0 = (line == y0) ? x0 : 0;
        int c1 = (line == y1) ? x1 : len;

        if (c0 > len) { c0 = len; }
        if (c1 > len) { c1 = len; }
        size += (size_t)((c1 > c0) ? c1 - c0 : 0) + (line < y1 ? 1u : 0u);
    }
    return size;
}

int tiku_textview_commit(tiku_textview_t *tv, const char *text)
{
    tiku_text_input_t checked;
    int y0, x0, y1, x1, ok;
    if (!tv || !text || !tv->nline ||
        !tiku_text_input_set(&checked, text, strlen(text))) return 0;
    if (!*text) return 1;
    if (!tiku_textview_selection(tv, &y0, &x0, &y1, &x1)) {
        y0 = y1 = tv->cy; x0 = x1 = tv->cx;
    }
    /* Stale or mid-codepoint ends are refused: the edit and its journal
     * entry are placed by byte. */
    if (y0 < 0 || y1 < y0 || y1 >= tv->nline || x0 < 0 || x1 < 0 ||
        x0 > tv->line[y0].len || x1 > tv->line[y1].len ||
        (x0 < tv->line[y0].len && ((unsigned char)tv->line[y0].text[x0] & 0xc0) == 0x80) ||
        (x1 < tv->line[y1].len && ((unsigned char)tv->line[y1].text[x1] & 0xc0) == 0x80)) return 0;
    int run = x0, lines = tv->nline - (y1 - y0);
    int tail = tv->line[y1].len - x1;
    /* Everything is measured before anything changes, so a refused commit
     * leaves the view as it was.  The line the selection's removal leaves
     * must fit, as undo rebuilds it, and so must every line the result
     * has. */
    if (x0 + tail >= TIKU_TEXTVIEW_LINE_MAX) return 0;
    for (const char *s = checked.text; *s; s++) {
        if (*s != '\n') {
            run++;
        } else if (run >= TIKU_TEXTVIEW_LINE_MAX ||
                   ++lines > TIKU_TEXTVIEW_LINES_MAX) {
            return 0;
        } else {
            run = 0;
        }
    }
    if (run + tail >= TIKU_TEXTVIEW_LINE_MAX) return 0;
    /* The text goes in whole, in one span_insert(), so each line is
     * measured with its own break in place. */
    tiku_textview_group_begin(tv);
    ok = sel_delete(tv) >= 0 &&
         span_insert(tv, tv->cy, tv->cx, checked.text, (int)checked.length);
    if (ok) {
        record(tv, 1, y0, x0, checked.text, (int)checked.length);
    }
    tiku_textview_group_end(tv);
    return ok;
}

/** @brief The bytes of the character at @p s: its first byte and the
 *         continuation bytes after it, four at most. */
static size_t
unit_length(const char *s)
{
    size_t k = 1;

    if ((unsigned char)s[0] >= 0x80u) {
        while (k < 4 && ((unsigned char)s[k] & 0xc0u) == 0x80u) {
            k++;
        }
    }
    return k;
}

/** @brief Put the @p k-byte character at @p s at the caret, whole or not
 *         at all; control bytes other than tab and CR, and DEL, never
 *         land.  @return 1 when it landed. */
static int
land_unit(tiku_textview_t *tv, const char *s, size_t k)
{
    unsigned char c = (unsigned char)s[0];
    size_t j;

    if (c == '\n') {
        int lines = tv->nline;

        tiku_textview_newline(tv);
        return tv->nline > lines;
    }
    if ((c < 32u && c != '\t' && c != '\r') || c == 127u ||
        tv->line[tv->cy].len + (int)k >= TIKU_TEXTVIEW_LINE_MAX) {
        return 0;
    }
    for (j = 0; j < k; j++) {
        tiku_textview_insert(tv, s[j]);
    }
    return 1;
}

void
tiku_textview_insert_text(tiku_textview_t *tv, const char *text)
{
    size_t i, k, n = 0;
    int ol, oc, was_rec;
    char *landed;

    /* A paste over a selection replaces it, and lands nothing when the
     * selection cannot go. */
    if (tv == NULL || text == NULL || tv->nline <= 0) {
        return;
    }
    caret_clamp(tv);
    if (sel_delete(tv) < 0) {
        return;
    }
    ol = tv->cy;
    oc = tv->cx;
    landed = malloc(strlen(text) + 1u);
    if (landed == NULL) {
        return;
    }
    /* The journal records exactly what landed, so undoing a paste takes
     * back just those characters and none of the ones it dropped. */
    was_rec = tv->recording;
    tv->recording = 0;          /* the run lands as one journal entry */
    for (i = 0; text[i] != '\0'; i += k) {
        k = unit_length(text + i);
        if (land_unit(tv, text + i, k)) {
            memcpy(landed + n, text + i, k);
            n += k;
        }
    }
    tv->recording = was_rec;
    if (n > 0) {
        record(tv, 1, ol, oc, landed, (int)n);
    }
    free(landed);
}

/** @brief Put journal entry @p e back into the text, inserting its text
 *         when @p insert is set and removing it otherwise, in one step. */
static void
replay(tiku_textview_t *tv, const struct tiku_textview_edit *e, int insert)
{
    int ey, ex;

    tiku_textview_place(tv, e->line, e->col);
    if (insert) {
        if (e->text != NULL) {  /* a copy that failed puts nothing back */
            (void)span_insert(tv, tv->cy, tv->cx, e->text, e->len);
        }
        return;
    }
    span_end(tv, tv->cy, tv->cx, e->len, &ey, &ex);
    (void)span_delete(tv, tv->cy, tv->cx, ey, ex);
}

int
tiku_textview_undo(tiku_textview_t *tv)
{
    struct tiku_textview_edit *e;

    if (tv == NULL || tv->undo_at <= 0) {
        return 0;
    }
    {
        int g = tv->undo[tv->undo_at - 1].group;

        tv->recording = 0;
        tv->selecting = 0;
        do {
            e = &tv->undo[tv->undo_at - 1];
            replay(tv, e, e->kind == 2);
            tv->undo_at--;
        } while (g != 0 && tv->undo_at > 0 &&
                 tv->undo[tv->undo_at - 1].group == g);
        tv->recording = 1;
    }
    /* The run below is closed, so a new edit after an undo starts an
     * entry of its own. */
    if (tv->undo_at > 0) {
        tv->undo[tv->undo_at - 1].coalesce = 0;
    }
    return 1;
}

int
tiku_textview_redo(tiku_textview_t *tv)
{
    struct tiku_textview_edit *e;

    if (tv == NULL || tv->undo_at >= tv->undo_n) {
        return 0;
    }
    {
        int g = tv->undo[tv->undo_at].group;

        tv->recording = 0;
        tv->selecting = 0;
        do {
            e = &tv->undo[tv->undo_at];
            replay(tv, e, e->kind == 1);
            tv->undo_at++;
        } while (g != 0 && tv->undo_at < tv->undo_n &&
                 tv->undo[tv->undo_at].group == g);
        tv->recording = 1;
    }
    if (tv->undo_at > 0) {
        tv->undo[tv->undo_at - 1].coalesce = 0;
    }
    return 1;
}

int
tiku_textview_can_undo(const tiku_textview_t *tv)
{
    return (tv != NULL && tv->undo_at > 0);
}

int
tiku_textview_can_redo(const tiku_textview_t *tv)
{
    return (tv != NULL && tv->undo_at < tv->undo_n);
}

/**
 * @brief Bring the caret back inside the buffer, onto a codepoint.
 *
 * A caret column past its line's end gives an edit that reads `len - cx`
 * a negative count, which becomes a huge size_t and copies off the end of
 * the line.  Every edit clamps first so that cannot happen.
 */
static void
caret_clamp(tiku_textview_t *tv)
{
    if (tv->cy < 0) {
        tv->cy = 0;
    }
    if (tv->cy >= tv->nline) {
        tv->cy = tv->nline - 1;
    }
    if (tv->cx < 0) {
        tv->cx = 0;
    }
    if (tv->cy >= 0 && tv->cx > tv->line[tv->cy].len) {
        tv->cx = tv->line[tv->cy].len;
    }
    while (tv->recording && tv->cy >= 0 && tv->cx > 0 &&
           tv->cx < tv->line[tv->cy].len &&
           ((unsigned char)tv->line[tv->cy].text[tv->cx] & 0xc0) == 0x80) tv->cx--;
}

/**
 * @brief Remove the selected span in one step, caret left where it began.
 *
 * The span is recorded whole, so a single undo restores it.  An anchor at
 * the caret encloses nothing and is dropped, so the edit's own text does
 * not become a selection.  @return 1 when the span went, 0 when there
 * was none, -1 when the joined line would not fit or memory ran out, and
 * nothing changed.
 */
static int
sel_delete(tiku_textview_t *tv)
{
    int y0, x0, y1, x1, count, used = 0, line;
    char *span;

    if (!tiku_textview_selection(tv, &y0, &x0, &y1, &x1)) {
        tv->selecting = 0;
        return 0;
    }
    /* A selection endpoint can point past its line after the buffer
     * shrank under it; bring both ends back inside first. */
    if (y1 >= tv->nline) {
        y1 = tv->nline - 1;
        x1 = tv->line[y1].len;
    }
    x0 = x0 > tv->line[y0].len ? tv->line[y0].len : x0;
    x1 = x1 > tv->line[y1].len ? tv->line[y1].len : x1;
    count = y1 - y0;                    /* the line breaks it crosses */
    for (line = y0; line <= y1; line++) {
        count += (line == y1 ? x1 : tv->line[line].len) -
                 (line == y0 ? x0 : 0);
    }
    span = malloc((size_t)count + 1u);
    if (span == NULL) {
        return -1;
    }
    for (line = y0; line <= y1; line++) {
        int c0 = line == y0 ? x0 : 0;
        int c1 = line == y1 ? x1 : tv->line[line].len;

        memcpy(span + used, tv->line[line].text + c0, (size_t)(c1 - c0));
        used += c1 - c0;
        if (line < y1) {
            span[used++] = '\n';
        }
    }
    span[used] = '\0';
    if (!span_delete(tv, y0, x0, y1, x1)) {
        free(span);
        return -1;
    }
    tv->selecting = 0;
    record(tv, 2, y0, x0, span, used);
    free(span);
    return 1;
}

void
tiku_textview_insert(tiku_textview_t *tv, char ch)
{
    char buf[TIKU_TEXTVIEW_LINE_MAX];
    tiku_textview_line_t *l;

    if (tv == NULL || tv->nline <= 0) {
        return;
    }
    caret_clamp(tv);
    /* Typing replaces what is selected, and types nothing when the
     * selection cannot go. */
    if (sel_delete(tv) < 0) {
        return;
    }
    l = &tv->line[tv->cy];
    if (l->len + 1 >= TIKU_TEXTVIEW_LINE_MAX) {
        return;                 /* a full line takes nothing */
    }
    {
        int ol = tv->cy, oc = tv->cx;

        const char *source = tiku_textview_line(tv, tv->cy);
        memcpy(buf, source, (size_t)tv->cx);
        buf[tv->cx] = ch;
        memcpy(buf + tv->cx + 1, source + tv->cx,
               (size_t)(l->len - tv->cx));
        line_set(l, buf, l->len + 1);
        tv->cx++;
        record(tv, 1, ol, oc, &ch, 1);
    }
}

void
tiku_textview_newline(tiku_textview_t *tv)
{
    tiku_textview_line_t *l;
    int tail, i;

    if (tv == NULL || tv->nline <= 0) {
        return;
    }
    caret_clamp(tv);
    /* A return over a selection replaces it, and breaks nothing when the
     * selection cannot go. */
    if (sel_delete(tv) < 0) {
        return;
    }
    if (tv->nline + 1 > TIKU_TEXTVIEW_LINES_MAX) {
        return;
    }
    l = &tv->line[tv->cy];
    tail = l->len - tv->cx;
    for (i = tv->nline; i > tv->cy + 1; i--) {
        tv->line[i] = tv->line[i - 1];
    }
    {
        int ol = tv->cy, oc = tv->cx;

        memset(&tv->line[tv->cy + 1], 0, sizeof tv->line[0]);
        line_set(&tv->line[tv->cy + 1], tiku_textview_line(tv, tv->cy) + tv->cx, tail);
        line_set(l, l->text, tv->cx);
        tv->nline++;
        tv->cy++;
        tv->cx = 0;
        record(tv, 1, ol, oc, "\n", 1);
    }
}

/** @brief Fold line @p at into the one before it, caret at the seam.
 *         @return 0, changing nothing, when the joined line would not fit. */
static int
join_previous(tiku_textview_t *tv, int at)
{
    tiku_textview_line_t *prev = &tv->line[at - 1];
    tiku_textview_line_t *cur = &tv->line[at];
    char buf[TIKU_TEXTVIEW_LINE_MAX];
    int seam = prev->len;
    int i;

    if (seam + cur->len >= TIKU_TEXTVIEW_LINE_MAX) {
        return 0;
    }
    memcpy(buf, tiku_textview_line(tv, at - 1), (size_t)seam);
    memcpy(buf + seam, tiku_textview_line(tv, at), (size_t)cur->len);
    line_set(prev, buf, seam + cur->len);
    free(cur->text);
    for (i = at; i < tv->nline - 1; i++) {
        tv->line[i] = tv->line[i + 1];
    }
    memset(&tv->line[tv->nline - 1], 0, sizeof tv->line[0]);
    tv->nline--;
    tv->cy = at - 1;
    tv->cx = seam;
    return 1;
}

void
tiku_textview_backspace(tiku_textview_t *tv)
{
    char buf[TIKU_TEXTVIEW_LINE_MAX];
    tiku_textview_line_t *l;

    if (tv == NULL || tv->nline <= 0) {
        return;
    }
    caret_clamp(tv);
    if (sel_delete(tv)) {
        return;                 /* the selection was the thing to remove */
    }
    l = &tv->line[tv->cy];
    if (tv->cx > 0) {
        int gc = tv->recording ? text_previous(l->text, tv->cx) : tv->cx - 1;
        int gl = tv->cy, n = tv->cx - gc;
        char gone[4];
        memcpy(gone, l->text + gc, (size_t)n);

        memcpy(buf, l->text, (size_t)gc);
        memcpy(buf + gc, l->text + tv->cx,
               (size_t)(l->len - tv->cx));
        line_set(l, buf, l->len - n);
        tv->cx = gc;
        record(tv, 2, gl, gc, gone, n);
    } else if (tv->cy > 0) {
        int gl = tv->cy - 1, gc = tv->line[tv->cy - 1].len;

        if (join_previous(tv, tv->cy)) {
            record(tv, 2, gl, gc, "\n", 1);
        }
    }
}

void
tiku_textview_delete(tiku_textview_t *tv)
{
    char buf[TIKU_TEXTVIEW_LINE_MAX];
    tiku_textview_line_t *l;

    if (tv == NULL || tv->nline <= 0) {
        return;
    }
    caret_clamp(tv);
    if (sel_delete(tv)) {
        return;
    }
    l = &tv->line[tv->cy];
    if (tv->cx < l->len) {
        int end = tv->recording ? text_next(l->text, tv->cx, l->len) : tv->cx + 1;
        int n = end - tv->cx;
        char gone[4];
        memcpy(gone, l->text + tv->cx, (size_t)n);
        int gl = tv->cy, gc = tv->cx;

        memcpy(buf, l->text, (size_t)tv->cx);
        memcpy(buf + tv->cx, l->text + end, (size_t)(l->len - end));
        line_set(l, buf, l->len - n);
        record(tv, 2, gl, gc, gone, n);
    } else if (tv->cy + 1 < tv->nline) {
        /* Delete at a line's end is the join backspace makes at the next
         * line's head, and leaves the caret where it was. */
        int gl = tv->cy, gc = tv->cx;

        if (join_previous(tv, tv->cy + 1)) {
            record(tv, 2, gl, gc, "\n", 1);
        }
    }
}

void
tiku_textview_move(tiku_textview_t *tv, tiku_textview_move_t how)
{
    if (tv == NULL || tv->nline <= 0) {
        return;
    }
    switch (how) {
    case TIKU_TEXTVIEW_LEFT:
        if (tv->cx > 0) {
            tv->cx = text_previous(tv->line[tv->cy].text, tv->cx);
        } else if (tv->cy > 0) {
            tv->cy--;
            tv->cx = tv->line[tv->cy].len;
        }
        break;
    case TIKU_TEXTVIEW_RIGHT:
        if (tv->cx < tv->line[tv->cy].len) {
            tv->cx = text_next(tv->line[tv->cy].text, tv->cx, tv->line[tv->cy].len);
        } else if (tv->cy + 1 < tv->nline) {
            tv->cy++;
            tv->cx = 0;
        }
        break;
    case TIKU_TEXTVIEW_UP:
        if (tv->cy > 0) {
            tv->cy--;
        }
        break;
    case TIKU_TEXTVIEW_DOWN:
        if (tv->cy + 1 < tv->nline) {
            tv->cy++;
        }
        break;
    case TIKU_TEXTVIEW_HOME:
        tv->cx = 0;
        return;
    case TIKU_TEXTVIEW_END:
        tv->cx = tv->line[tv->cy].len;
        return;
    default:
        return;
    }
    /* Up and down keep the column where they can; on a shorter line the
     * caret goes to its end. */
    if (tv->cx > tv->line[tv->cy].len) {
        tv->cx = tv->line[tv->cy].len;
    }
    caret_clamp(tv);
}

int
tiku_textview_reveal(tiku_textview_t *tv, int rows)
{
    int was;

    if (tv == NULL || rows <= 0) {
        return 0;
    }
    was = tv->top;
    if (tv->cy < tv->top) {
        tv->top = tv->cy;
    }
    if (tv->cy >= tv->top + rows) {
        tv->top = tv->cy - rows + 1;
    }
    if (tv->top < 0) {
        tv->top = 0;
    }
    return tv->top != was;
}

int
tiku_textview_scroll_to(tiku_textview_t *tv, int top, int rows)
{
    int was, most;

    if (tv == NULL) {
        return 0;
    }
    was = tv->top;
    /* Never past the last screenful. */
    most = (rows > 0 && tv->nline > rows) ? tv->nline - rows : 0;
    if (top < 0) { top = 0; }
    if (top > most) { top = most; }
    tv->top = top;
    return tv->top != was;
}

size_t
tiku_textview_text(const tiku_textview_t *tv, char *out, size_t max)
{
    size_t need = 0;
    int i;

    if (tv == NULL) {
        return 0;
    }
    for (i = 0; i < tv->nline; i++) {
        size_t len = (size_t)tv->line[i].len;

        if (out != NULL && need < max && max - need > 1u) {
            size_t copy = len < max - need - 1u ? len : max - need - 1u;

            memcpy(out + need, tiku_textview_line(tv, i), copy);
            if (copy == len && max - need - copy > 1u) {
                out[need + copy] = '\n';
            }
        }
        need += len + 1u;
    }
    if (out != NULL && max > 0) {
        out[(need < max) ? need : max - 1u] = '\0';
    }
    return need;
}
