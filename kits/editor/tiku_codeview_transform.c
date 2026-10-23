/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_codeview_transform.c - changes made to whole lines or to the
 * selection at once, each one undo step: blanks at lines' ends taken out,
 * indentation made of spaces or of tabs, ASCII case changed, lines sorted.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_codeview_private.h"

#include <stdlib.h>
#include <string.h>

/** @brief One line being sorted: its bytes, its line break left out, and
 *         its place before sorting. */
typedef struct {
    const char *text;
    size_t length;
    size_t order;
} line_t;

/** @brief The order of two lines for qsort(): by their bytes, a line
 *         before a longer one it starts, and then by their places, so the
 *         sort is stable. */
static int
compare(const void *a, const void *b)
{
    const line_t *x = a, *y = b;
    size_t n = x->length < y->length ? x->length : y->length;
    int c = memcmp(x->text, y->text, n);

    if (c != 0)
        return c;
    if (x->length != y->length)
        return x->length < y->length ? -1 : 1;
    return x->order < y->order ? -1 : x->order > y->order;
}

/** @brief Where the line starting at byte @p i of the @p n bytes of
 *         @p text ends, before its line break. */
static size_t
line_end(const char *text, size_t n, size_t i)
{
    while (i < n && text[i] != '\r' && text[i] != '\n')
        i++;
    return i;
}

/** @brief Past the line break at byte @p i of @p text, a CRLF as one;
 *         @p i itself at the end. */
static size_t
past_break(const char *text, size_t n, size_t i)
{
    if (i < n && text[i++] == '\r' && i < n && text[i] == '\n')
        i++;
    return i;
}

/** @brief The bytes @p op changes, from @p a to @p b, the main selection:
 *         the selection itself for a change of case, else the whole lines
 *         it touches, one it ends at the start of left out, or the whole
 *         document when it is empty. */
static void
op_range(tiku_codeview_t *v, tiku_codeview_transform_t op,
         tiku_edit_pos_t *a, tiku_edit_pos_t *b)
{
    int64_t last;

    if (op == TIKU_CODEVIEW_UPPER_ASCII || op == TIKU_CODEVIEW_LOWER_ASCII)
        return;
    if (*a == *b) {
        *a = 0;
        *b = tiku_editdoc_length(v->doc);
        return;
    }
    *a = tiku_editdoc_line_start(v->doc, tiku_editdoc_line_of(v->doc, *a));
    last = tiku_editdoc_line_of(v->doc, *b);
    if (*b != tiku_editdoc_line_start(v->doc, last))
        *b = last + 1 < tiku_editdoc_lines(v->doc) ?
             tiku_editdoc_line_start(v->doc, last + 1) :
             tiku_editdoc_length(v->doc);
}

/** @brief Change the case of the ASCII letters of @p n bytes of @p text
 *         into @p out; @return the bytes written. */
static size_t
change_case(const char *text, size_t n, char *out, int upper)
{
    size_t i;

    for (i = 0; i < n; i++) {
        unsigned char c = (unsigned char)text[i];

        if (upper && c >= 'a' && c <= 'z')
            c -= 'a' - 'A';
        else if (!upper && c >= 'A' && c <= 'Z')
            c += 'a' - 'A';
        out[i] = (char)c;
    }
    return n;
}

/** @brief The lines of @p n bytes of @p text, sorted, @p count of them in
 *         a new array; NULL with @p st set when there is no room. */
static line_t *
sorted_lines(const char *text, size_t n, size_t *count,
             tiku_edit_status_t *st)
{
    line_t *lines;
    size_t i, k;

    *count = 0;
    for (i = 0; i < n; i = past_break(text, n, line_end(text, n, i)))
        (*count)++;
    if (*count > SIZE_MAX / sizeof *lines) {
        *st = TIKU_EDIT_LIMIT;
        return NULL;
    }
    lines = tiku_ed_alloc(*count * sizeof *lines);
    if (lines == NULL) {
        *st = TIKU_EDIT_NO_MEMORY;
        return NULL;
    }
    for (i = 0, k = 0; i < n; k++) {
        size_t end = line_end(text, n, i);

        lines[k].text = text + i;
        lines[k].length = end - i;
        lines[k].order = k;
        i = past_break(text, n, end);
    }
    qsort(lines, *count, sizeof *lines, compare);
    return lines;
}

/** @brief Write an indentation of @p columns columns into @p out: spaces,
 *         or with @p tabs a tab for each @p tab columns and then spaces;
 *         @return the bytes written. */
static size_t
indentation(size_t columns, int tab, int tabs, char *out)
{
    size_t used = 0, t;

    if (tabs) {
        for (t = 0; t < columns / (size_t)tab; t++)
            out[used++] = '\t';
        columns %= (size_t)tab;
    }
    memset(out + used, ' ', columns);
    return used + columns;
}

/** @brief Write line @p k, the bytes of @p text from @p start to @p end,
 *         as @p op changes it into @p out, the sorted lines @p sorted
 *         giving its place; @return the bytes written. */
static size_t
change_line(tiku_codeview_t *v, tiku_codeview_transform_t op,
            const line_t *sorted, size_t k, const char *text, size_t start,
            size_t end, char *out)
{
    size_t from = start, tail = end, columns = 0, wrote;

    if (op == TIKU_CODEVIEW_SORT_LINES) {
        memcpy(out, sorted[k].text, sorted[k].length);
        return sorted[k].length;
    }
    if (op == TIKU_CODEVIEW_TRIM_TRAILING) {
        while (tail > start && (text[tail - 1] == ' ' ||
                                text[tail - 1] == '\t'))
            tail--;
        memcpy(out, text + from, tail - from);
        return tail - from;
    }
    while (from < end && (text[from] == ' ' || text[from] == '\t'))
        columns += text[from++] == ' ' ? 1 : (size_t)v->tab_width -
                                             columns % v->tab_width;
    wrote = indentation(columns, v->tab_width,
                        op == TIKU_CODEVIEW_INDENT_TO_TABS, out);
    memcpy(out + wrote, text + from, tail - from);
    return wrote + tail - from;
}

/** @brief Write @p n bytes of @p text into @p out as @p op changes each
 *         line, keeping its line breaks; @return the bytes written. */
static size_t
change_lines(tiku_codeview_t *v, tiku_codeview_transform_t op,
             const line_t *sorted, const char *text, size_t n, char *out)
{
    size_t start = 0, used = 0, k;

    for (k = 0; start < n; k++) {
        size_t end = line_end(text, n, start);

        used += change_line(v, op, sorted, k, text, start, end, out + used);
        start = end;
        if (start < n) {
            out[used++] = text[start++];
            if (text[start - 1] == '\r' && start < n && text[start] == '\n')
                out[used++] = text[start++];
        }
    }
    return used;
}

/** @brief Write the @p n bytes of @p text as @p op changes them into
 *         @p out, and the bytes written in @p used. */
static tiku_edit_status_t
transformed(tiku_codeview_t *v, tiku_codeview_transform_t op,
            const char *text, size_t n, char *out, size_t *used)
{
    tiku_edit_status_t st = TIKU_EDIT_OK;
    line_t *lines = NULL;
    size_t count;

    if (op == TIKU_CODEVIEW_UPPER_ASCII || op == TIKU_CODEVIEW_LOWER_ASCII) {
        *used = change_case(text, n, out, op == TIKU_CODEVIEW_UPPER_ASCII);
        return TIKU_EDIT_OK;
    }
    if (op == TIKU_CODEVIEW_SORT_LINES) {
        lines = sorted_lines(text, n, &count, &st);
        if (lines == NULL)
            return st;
    }
    *used = change_lines(v, op, lines, text, n, out);
    tiku_ed_free(lines);
    return TIKU_EDIT_OK;
}

tiku_edit_status_t
tiku_codeview_transform(tiku_codeview_t *view,
                        tiku_codeview_transform_t operation)
{
    tiku_edit_range_t range;
    tiku_edit_status_t st;
    tiku_edit_pos_t a, b;
    size_t n, got, used = 0, factor;
    char *text, *out;

    if (view == NULL || (unsigned)operation > TIKU_CODEVIEW_SORT_LINES)
        return TIKU_EDIT_INVALID;
    if (tiku_editdoc_read_only(view->doc))
        return TIKU_EDIT_READ_ONLY;
    st = tiku_codeview_selection(view, &a, &b);
    if (st != TIKU_EDIT_OK)
        return st;
    if (a > b) {
        tiku_edit_pos_t swap = a;

        a = b;
        b = swap;
    }
    if (a == b && (operation == TIKU_CODEVIEW_UPPER_ASCII ||
                   operation == TIKU_CODEVIEW_LOWER_ASCII))
        return TIKU_EDIT_NOTHING;
    op_range(view, operation, &a, &b);
    n = (size_t)(b - a);
    if (n == 0)
        return TIKU_EDIT_NOTHING;
    factor = operation == TIKU_CODEVIEW_INDENT_TO_SPACES ?
             (size_t)view->tab_width : 1;
    if (n > (SIZE_MAX - 1) / factor)
        return TIKU_EDIT_LIMIT;
    text = tiku_ed_alloc(n + 1);
    out = tiku_ed_alloc(n * factor + 1);
    range.start = a;
    range.end = b;
    if (text == NULL || out == NULL)
        st = TIKU_EDIT_NO_MEMORY;
    else
        st = tiku_editdoc_read(view->doc, range, text, n + 1, &got);
    if (st == TIKU_EDIT_OK)
        st = transformed(view, operation, text, n, out, &used);
    if (st == TIKU_EDIT_OK) {
        if (used == n && memcmp(text, out, n) == 0)
            st = TIKU_EDIT_NOTHING;
        else
            st = tiku_cv_replace(view, a, b, out, used, 0);
    }
    tiku_ed_free(out);
    tiku_ed_free(text);
    return st;
}
