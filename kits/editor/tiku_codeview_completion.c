/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_codeview_completion.c - rich lists of words, each with a label, a
 * text it puts in, a detail and a kind, and the tip of a function's
 * signature that follows the caret from argument to argument.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_codeview_private.h"

#include <stdlib.h>
#include <string.h>

/* The most words a rich list shows at once. */
#define ROWS_MOST 20

/** @brief A word offered while a list is put together: the host's item,
 *         its index among the host's items, and whether ASCII case is
 *         aside when it is sorted. */
typedef struct {
    const tiku_codeview_completion_item_t *item;
    size_t index;
    int fold;
} entry_t;

/** @brief An ASCII capital as its small letter, any other byte as it
 *         is. */
static unsigned
lower(unsigned c)
{
    return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c;
}

/** @brief The order of two entries for qsort(): by their labels' bytes,
 *         ASCII case aside when they say so, and then by their indices. */
static int
compare(const void *a, const void *b)
{
    const entry_t *x = a, *y = b;
    const unsigned char *p = (const unsigned char *)x->item->label;
    const unsigned char *q = (const unsigned char *)y->item->label;

    for (; *p != '\0' && *q != '\0'; p++, q++) {
        unsigned c = x->fold ? lower(*p) : *p;
        unsigned d = x->fold ? lower(*q) : *q;

        if (c != d)
            return c < d ? -1 : 1;
    }
    if (*p != *q)
        return *p != '\0' ? 1 : -1;
    return x->index < y->index ? -1 : x->index > y->index;
}

/** @brief Whether @p label starts with @p prefix, ASCII case aside with
 *         @p fold; every label does with a NULL prefix. */
static int
prefix_matches(const char *label, const char *prefix, int fold)
{
    while (prefix != NULL && *prefix != '\0') {
        unsigned a = (unsigned char)*label++;
        unsigned b = (unsigned char)*prefix++;

        if (fold ? lower(a) != lower(b) : a != b)
            return 0;
    }
    return 1;
}

/** @brief Whether @p s is one line of UTF-8, with no CR, LF or tab. */
static int
single_line(const char *s)
{
    return s != NULL && tiku_ed_utf8_valid(s, strlen(s)) &&
           strpbrk(s, "\r\n\t") == NULL;
}

/** @brief TIKU_EDIT_OK when @p list may be offered in @p v, else why not:
 *         its range must be on character boundaries, its flags and rows
 *         ones there are, its prefix one line of UTF-8, and its extra keys
 *         fewer than TIKU_CV_ACCEPT printable ASCII characters. */
static tiku_edit_status_t
list_ok(const tiku_codeview_t *v, const tiku_codeview_completion_t *list)
{
    const unsigned known = TIKU_COMPLETION_SORT | TIKU_COMPLETION_IGNORE_CASE;
    size_t i, n;

    if ((list->count > 0 && list->items == NULL) || list->rows > ROWS_MOST ||
        (list->flags & ~known) != 0)
        return TIKU_EDIT_INVALID;
    if (list->range.start > list->range.end ||
        !tiku_editdoc_is_boundary(v->doc, list->range.start) ||
        !tiku_editdoc_is_boundary(v->doc, list->range.end))
        return TIKU_EDIT_INVALID_RANGE;
    if (list->prefix != NULL && !single_line(list->prefix))
        return TIKU_EDIT_INVALID_TEXT;
    n = list->accept != NULL ? strlen(list->accept) : 0;
    if (n >= TIKU_CV_ACCEPT)
        return TIKU_EDIT_LIMIT;
    for (i = 0; i < n; i++)
        if ((unsigned char)list->accept[i] < ' ' ||
            (unsigned char)list->accept[i] > '~')
            return TIKU_EDIT_INVALID_TEXT;
    if (list->count > SIZE_MAX / sizeof(entry_t))
        return TIKU_EDIT_LIMIT;
    return TIKU_EDIT_OK;
}

/** @brief Whether @p item can be offered: a label of one line, not
 *         empty, a detail of one line or none, a text of UTF-8 or none,
 *         and a kind there is. */
static int
item_ok(const tiku_codeview_completion_item_t *item)
{
    return single_line(item->label) && item->label[0] != '\0' &&
           (item->detail == NULL || single_line(item->detail)) &&
           (item->text == NULL ||
            tiku_ed_utf8_valid(item->text, strlen(item->text))) &&
           (unsigned)item->kind <= TIKU_COMPLETION_KEYWORD;
}

/** @brief Put @p list's items whose labels start with its prefix in
 *         @p entries, @p count of them, with the bytes their labels and
 *         details take, NULs and all, in @p bytes; every item is checked,
 *         offered or not. */
static tiku_edit_status_t
gather(const tiku_codeview_completion_t *list, entry_t *entries,
       size_t *count, size_t *bytes)
{
    int fold = (list->flags & TIKU_COMPLETION_IGNORE_CASE) != 0;
    size_t i, n;

    *count = 0;
    *bytes = 0;
    for (i = 0; i < list->count; i++) {
        const tiku_codeview_completion_item_t *item = &list->items[i];

        if (!item_ok(item))
            return TIKU_EDIT_INVALID_TEXT;
        if (!prefix_matches(item->label, list->prefix, fold))
            continue;
        n = strlen(item->label) + 1 +
            (item->detail != NULL ? strlen(item->detail) : 0) + 1;
        if (n > SIZE_MAX - *bytes)
            return TIKU_EDIT_LIMIT;
        *bytes += n;
        entries[*count].item = item;
        entries[*count].index = i;
        entries[*count].fold = fold;
        (*count)++;
    }
    return TIKU_EDIT_OK;
}

/** @brief Copy @p count entries into @p copy: the items, their indices
 *         after them, and their labels and details after those; the text
 *         each puts in goes in @p words. */
static void
fill(tiku_codeview_completion_item_t *copy, const entry_t *entries,
     size_t count, const char **words)
{
    size_t *indices = (size_t *)(copy + count), i;
    char *text = (char *)(indices + count);

    for (i = 0; i < count; i++) {
        const tiku_codeview_completion_item_t *item = entries[i].item;
        const char *detail = item->detail != NULL ? item->detail : "";

        copy[i] = *item;
        copy[i].text = NULL;
        indices[i] = entries[i].index;
        copy[i].label = text;
        strcpy(text, item->label);
        text += strlen(text) + 1;
        copy[i].detail = text;
        strcpy(text, detail);
        text += strlen(text) + 1;
        words[i] = item->text != NULL ? item->text : item->label;
    }
}

/** @brief Show the @p count entries copied into @p copy as a list that
 *         inserts, @p words the texts they put in, and keep @p copy for
 *         painting it; @p copy is freed when the list is not shown. */
static tiku_edit_status_t
show(tiku_codeview_t *v, const tiku_codeview_completion_t *list,
     tiku_codeview_completion_item_t *copy, const char **words,
     size_t count)
{
    const size_t *indices = (const size_t *)(copy + count);
    tiku_codeview_list_t plain;
    tiku_edit_status_t st;
    size_t i;

    memset(&plain, 0, sizeof plain);
    plain.range = list->range;
    plain.items = words;
    plain.count = count;
    plain.inserts = 1;
    for (i = 0; i < count; i++)
        if (indices[i] == list->current)
            plain.current = i;
    st = tiku_codeview_list_show(v, &plain);
    if (st != TIKU_EDIT_OK) {
        tiku_ed_free(copy);
        return st;
    }
    v->completion_items = copy;
    v->completion_indices = (size_t *)(copy + count);
    v->completion_rows = list->rows;
    if (list->accept != NULL)
        strcpy(v->completion_accept, list->accept);
    return TIKU_EDIT_OK;
}

tiku_edit_status_t
tiku_codeview_complete(tiku_codeview_t *view,
                       const tiku_codeview_completion_t *list)
{
    size_t each = sizeof(tiku_codeview_completion_item_t) + sizeof(size_t);
    tiku_codeview_completion_item_t *copy;
    entry_t *entries = NULL;
    const char **words;
    size_t count = 0, bytes = 0;
    tiku_edit_status_t st;

    if (view == NULL || list == NULL)
        return TIKU_EDIT_INVALID;
    st = list_ok(view, list);
    if (st != TIKU_EDIT_OK)
        return st;
    if (list->count > 0) {
        entries = tiku_ed_alloc(list->count * sizeof *entries);
        if (entries == NULL)
            return TIKU_EDIT_NO_MEMORY;
    }
    st = gather(list, entries, &count, &bytes);
    if (st == TIKU_EDIT_OK && count == 0) {
        tiku_codeview_list_hide(view);
        st = TIKU_EDIT_NOTHING;
    }
    if (st == TIKU_EDIT_OK && count > (SIZE_MAX - bytes) / each)
        st = TIKU_EDIT_LIMIT;
    if (st != TIKU_EDIT_OK) {
        tiku_ed_free(entries);
        return st;
    }
    if (list->flags & TIKU_COMPLETION_SORT)
        qsort(entries, count, sizeof *entries, compare);
    copy = tiku_ed_alloc(count * each + bytes);
    words = tiku_ed_alloc(count * sizeof *words);
    if (copy == NULL || words == NULL) {
        tiku_ed_free(copy);
        st = TIKU_EDIT_NO_MEMORY;
    } else {
        fill(copy, entries, count, words);
        st = show(view, list, copy, words, count);
    }
    tiku_ed_free(words);
    tiku_ed_free(entries);
    return st;
}

size_t
tiku_codeview_completion_selected(const tiku_codeview_t *view)
{
    if (view == NULL || view->completion_items == NULL ||
        view->list_current >= view->list_count)
        return SIZE_MAX;
    return view->completion_indices[view->list_current];
}

int
tiku_codeview_completion_result(tiku_codeview_t *view, size_t *index)
{
    if (view == NULL || !view->completion_accepted)
        return 0;
    if (index != NULL)
        *index = view->completion_result;
    view->completion_accepted = 0;
    return 1;
}

/** @brief Whether @p p is a character boundary of the @p n bytes of
 *         @p text, its end included. */
static int
boundary(const char *text, size_t n, tiku_edit_pos_t p)
{
    return p >= 0 && (uint64_t)p <= n &&
           ((uint64_t)p == n || ((unsigned char)text[p] & 0xc0) != 0x80);
}

tiku_edit_status_t
tiku_codeview_tip_highlight(tiku_codeview_t *view, tiku_edit_range_t range)
{
    size_t n;

    if (view == NULL || !view->tip_shown)
        return TIKU_EDIT_INVALID;
    n = strlen(view->tip_text);
    if (range.start > range.end || !boundary(view->tip_text, n, range.start) ||
        !boundary(view->tip_text, n, range.end))
        return TIKU_EDIT_INVALID_RANGE;
    view->tip_highlight = range;
    return TIKU_EDIT_OK;
}

/** @brief Whether @p count ranges are arguments of @p signature, @p n
 *         bytes: on its character boundaries, in order and apart. */
static int
arguments_ok(const char *signature, size_t n,
             const tiku_edit_range_t *arguments, size_t count)
{
    size_t i;

    for (i = 0; i < count; i++)
        if (arguments[i].start > arguments[i].end ||
            !boundary(signature, n, arguments[i].start) ||
            !boundary(signature, n, arguments[i].end) ||
            (i > 0 && arguments[i].start < arguments[i - 1].end))
            return 0;
    return 1;
}

tiku_edit_status_t
tiku_codeview_signature_show(tiku_codeview_t *view, tiku_edit_pos_t open,
                             const char *signature,
                             const tiku_edit_range_t *arguments,
                             size_t count)
{
    tiku_edit_range_t *copy;
    tiku_edit_status_t st;
    size_t n;

    if (view == NULL || signature == NULL || count == 0 ||
        arguments == NULL)
        return TIKU_EDIT_INVALID;
    if (count > SIZE_MAX / sizeof *arguments)
        return TIKU_EDIT_LIMIT;
    if (tiku_editdoc_byte_at(view->doc, open) != '(')
        return TIKU_EDIT_INVALID_RANGE;
    n = strlen(signature);
    if (!tiku_ed_utf8_valid(signature, n))
        return TIKU_EDIT_INVALID_TEXT;
    if (!arguments_ok(signature, n, arguments, count))
        return TIKU_EDIT_INVALID_RANGE;
    copy = tiku_ed_alloc(count * sizeof *copy);
    if (copy == NULL)
        return TIKU_EDIT_NO_MEMORY;
    memcpy(copy, arguments, count * sizeof *copy);
    st = tiku_codeview_tip_show(view, open, signature);
    if (st != TIKU_EDIT_OK) {
        tiku_ed_free(copy);
        return st;
    }
    view->tip_arguments = copy;
    view->tip_argument_count = count;
    tiku_cv_signature_follow(view);
    return TIKU_EDIT_OK;
}

size_t
tiku_codeview_signature_argument(const tiku_codeview_t *view)
{
    if (view == NULL || !view->tip_shown || view->tip_arguments == NULL)
        return SIZE_MAX;
    return view->tip_argument;
}

/** @brief A walk through a call's arguments, a byte at a time. */
typedef struct {
    int depth;                  /* brackets open inside the call        */
    int quote;                  /* the quote a string is in, 0 none     */
    int escaped;                /* the byte before was a backslash      */
    size_t argument;            /* the argument reached                 */
} call_walk_t;

/** @brief Take byte @p c of the call's code into @p w; 0 when it closes
 *         the call. */
static int
call_step(call_walk_t *w, int c)
{
    if (w->quote != 0) {
        if (w->escaped)
            w->escaped = 0;
        else if (c == '\\')
            w->escaped = 1;
        else if (c == w->quote)
            w->quote = 0;
    } else if (c == '"' || c == '\'') {
        w->quote = c;
    } else if (c == '(' || c == '[' || c == '{') {
        w->depth++;
    } else if (c == ')' || c == ']' || c == '}') {
        if (w->depth == 0)
            return 0;
        w->depth--;
    } else if (c == ',' && w->depth == 0) {
        w->argument++;
    }
    return 1;
}

void
tiku_cv_signature_follow(tiku_codeview_t *v)
{
    tiku_edit_pos_t open, anchor, caret, p;
    tiku_cv_inks_t inks;
    int64_t last_line = -1;
    call_walk_t w;

    if (v->tip_arguments == NULL)
        return;
    if (tiku_editdoc_anchor_pos(v->doc, v->tip_at, &open) != TIKU_EDIT_OK ||
        tiku_codeview_selection(v, &anchor, &caret) != TIKU_EDIT_OK ||
        caret <= open || caret - open > TIKU_CODEVIEW_BRACE_SCAN ||
        tiku_editdoc_byte_at(v->doc, open) != '(') {
        tiku_codeview_tip_hide(v);
        return;
    }
    memset(&w, 0, sizeof w);
    memset(&inks, 0, sizeof inks);
    for (p = open + 1; p < caret; p++) {
        int64_t line = tiku_editdoc_line_of(v->doc, p);

        if (line != last_line) {
            inks = tiku_cv_inks(v, line, tiku_editdoc_line_end(v->doc, line));
            last_line = line;
        }
        if (!tiku_cv_code_at(&inks, p - tiku_editdoc_line_start(v->doc, line)))
            continue;
        if (!call_step(&w, tiku_editdoc_byte_at(v->doc, p))) {
            tiku_codeview_tip_hide(v);
            return;
        }
    }
    v->tip_argument = w.argument;
    if (w.argument < v->tip_argument_count) {
        v->tip_highlight = v->tip_arguments[w.argument];
    } else {
        v->tip_highlight.start = 0;
        v->tip_highlight.end = 0;
    }
}
