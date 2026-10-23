/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_codeview_find.c - Find, Replace and Replace All from a view: the
 * search starts from the view's own selection and leaves its match
 * selected and shown.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_codeview_private.h"
#include "tiku_edit_regex.h"

#include <string.h>

/** @brief Whether the empty selection at @p caret is the empty match of
 *         @p q that the last Find selected, with the document and the
 *         selection unchanged since. */
static int
found_empty(const tiku_codeview_t *v, const tiku_edit_query_t *q,
            tiku_edit_pos_t caret)
{
    const tiku_ed_view_t *s = tiku_ed_view_get(v->doc, v->view);

    return v->zero_pattern != NULL && q->text != NULL && s != NULL &&
           v->zero_generation == tiku_editdoc_generation(v->doc) &&
           v->zero_selection == s->selection_serial &&
           v->zero_position == caret && v->zero_flags == q->flags &&
           strlen(v->zero_pattern) == q->length &&
           memcmp(v->zero_pattern, q->text, q->length) == 0;
}

/** @brief Note the match Find selected at @p at, with the query's flags,
 *         the document's generation and the selection's serial; @p pattern
 *         is the query's text when the match is empty, else NULL. */
static void
remember(tiku_codeview_t *v, const tiku_edit_query_t *q, char *pattern,
         tiku_edit_pos_t at)
{
    tiku_ed_free(v->zero_pattern);
    v->zero_pattern = pattern;
    v->zero_position = at;
    v->zero_flags = q->flags;
    v->zero_generation = tiku_editdoc_generation(v->doc);
    v->zero_selection = tiku_ed_view_get(v->doc, v->view)->selection_serial;
}

/** @brief Find as tiku_codeview_find() does; a forward regex search from
 *         an empty selection starts a character on, past that empty
 *         match, when it is the one the last Find selected or with
 *         @p skip. */
static tiku_edit_status_t
find_next(tiku_codeview_t *view, const tiku_edit_query_t *q, int backward,
          int skip, tiku_codeview_found_t *out)
{
    tiku_edit_pos_t anchor, caret, from;
    tiku_edit_range_t match;
    tiku_edit_status_t st;
    int wrapped = 0, wrap = 0;
    char *pattern = NULL;

    if (out != NULL)
        memset(out, 0, sizeof *out);
    if (view == NULL)
        return TIKU_EDIT_INVALID;
    st = tiku_codeview_selection(view, &anchor, &caret);
    if (st != TIKU_EDIT_OK)
        return st;
    if (backward)
        from = anchor < caret ? anchor : caret;
    else
        from = anchor > caret ? anchor : caret;
    if (!backward && anchor == caret && q != NULL &&
        (q->flags & TIKU_EDIT_REGEX) &&
        (skip || found_empty(view, q, caret))) {
        if (from == tiku_editdoc_length(view->doc)) {
            from = 0;
            wrap = 1;
        } else {
            from = tiku_editdoc_next(view->doc, from);
        }
    }
    st = tiku_edit_find(view->doc, q, from, backward, &match, &wrapped);
    if (st != TIKU_EDIT_OK)
        return st;
    if ((q->flags & TIKU_EDIT_REGEX) && match.start == match.end) {
        pattern = tiku_ed_alloc(q->length + 1);
        if (pattern == NULL)
            return TIKU_EDIT_NO_MEMORY;
        memcpy(pattern, q->text, q->length);
        pattern[q->length] = '\0';
    }
    st = tiku_codeview_select(view, match.start, match.end);
    if (st != TIKU_EDIT_OK) {
        tiku_ed_free(pattern);
        return st;
    }
    remember(view, q, pattern, match.start);
    tiku_codeview_reveal(view);
    if (out != NULL) {
        out->found = 1;
        out->wrapped = wrapped || wrap;
    }
    return TIKU_EDIT_OK;
}

tiku_edit_status_t
tiku_codeview_find(tiku_codeview_t *view, const tiku_edit_query_t *q,
                   int backward, tiku_codeview_found_t *out)
{
    return find_next(view, q, backward, 0, out);
}

/** @brief Whether the main selection, @p selected, is one match of @p q
 *         to replace: for a regex its replacement, expanded from
 *         @p length bytes of @p with, goes in @p text, and an empty
 *         selection is one only when it is the empty match Find selected.
 *         0 with @p st set to the error when the pattern or the
 *         replacement cannot be used. */
static int
replaceable(tiku_codeview_t *v, const tiku_edit_query_t *q,
            tiku_edit_range_t selected, const char *with, size_t length,
            char **text, size_t *n, tiku_edit_status_t *st)
{
    *text = NULL;
    *n = 0;
    *st = TIKU_EDIT_OK;
    if (q == NULL || !(q->flags & TIKU_EDIT_REGEX))
        return tiku_edit_is_match(v->doc, q, selected);
    *st = tiku_ed_regex_expand(v->doc, q, selected, with, length, text, n);
    if (*st == TIKU_EDIT_NOTHING)
        *st = TIKU_EDIT_OK;
    else if (*st == TIKU_EDIT_OK && (selected.start != selected.end ||
                                     found_empty(v, q, selected.start)))
        return 1;
    tiku_ed_free(*text);
    *text = NULL;
    return 0;
}

tiku_edit_status_t
tiku_codeview_replace(tiku_codeview_t *view, const tiku_edit_query_t *q,
                      const char *with, size_t length,
                      tiku_codeview_found_t *out)
{
    tiku_edit_pos_t anchor, caret;
    tiku_edit_range_t selected;
    tiku_edit_status_t st;
    char *expanded;
    size_t n;
    int replaced = 0;

    if (out != NULL)
        memset(out, 0, sizeof *out);
    if (view == NULL)
        return TIKU_EDIT_INVALID;
    st = tiku_codeview_selection(view, &anchor, &caret);
    if (st != TIKU_EDIT_OK)
        return st;
    selected.start = anchor < caret ? anchor : caret;
    selected.end = anchor < caret ? caret : anchor;
    if (replaceable(view, q, selected, with, length, &expanded, &n, &st)) {
        if (expanded == NULL && with == NULL && length > 0)
            return TIKU_EDIT_INVALID;
        st = tiku_cv_replace(view, selected.start, selected.end,
                             expanded != NULL ? expanded : with,
                             expanded != NULL ? n : length, 0);
        tiku_ed_free(expanded);
        if (st != TIKU_EDIT_OK)
            return st;
        replaced = 1;
    } else if (st != TIKU_EDIT_OK) {
        return st;
    }
    st = find_next(view, q, 0, replaced && selected.start == selected.end,
                   out);
    if (out != NULL)
        out->replaced = (size_t)replaced;
    if (replaced && st == TIKU_EDIT_NOTHING)
        return TIKU_EDIT_OK;
    return st;
}

tiku_edit_status_t
tiku_codeview_replace_all(tiku_codeview_t *view, const tiku_edit_query_t *q,
                          const char *with, size_t length,
                          tiku_codeview_found_t *out)
{
    tiku_edit_status_t st;
    size_t count = 0;

    if (out != NULL)
        memset(out, 0, sizeof *out);
    if (view == NULL)
        return TIKU_EDIT_INVALID;
    st = tiku_edit_replace_all(view->doc, q, with, length, view->view,
                               &count);
    if (st == TIKU_EDIT_OK)
        tiku_codeview_reveal(view);
    if (out != NULL)
        out->replaced = count;
    return st;
}
