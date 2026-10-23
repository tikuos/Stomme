/* tiku_editor_search_feedback.c -- Find's live match count and outlines.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#include "tiku_editor_private.h"

/* The largest document live counting searches; Find Next and Find
 * Previous search any size. */
#define EDIT_LIVE_MAX (2u * 1024u * 1024u)

/** The Find field's text under the window's three checkboxes. */
static tiku_edit_query_t find_query(const edit_state_t *st)
{
    tiku_edit_query_t q = {st->field[0].text, strlen(st->field[0].text),
        (st->match_case ? TIKU_EDIT_MATCH_CASE : 0u) |
        (st->whole_word ? TIKU_EDIT_WHOLE_WORD : 0u) |
        (st->regex ? TIKU_EDIT_REGEX : 0u)};
    return q;
}

/** Drop the outlines and the count, so the next refresh searches anew. */
void tiku_editor_search_clear(edit_state_t *st)
{
    tiku_editdoc_indicators_clear(st->doc, 1u << EDIT_SEARCH_KIND);
    st->match_count = 0;
    st->match_truncated = 0;
    st->search_generation = 0;
    st->search_message[0] = '\0';
}

/** Find @p q's matches in the whole document and outline each one with
 *  text in it.  On failure no match is kept and the count line says
 *  why. */
static void collect(edit_state_t *st, const tiku_edit_query_t *q)
{
    tiku_edit_range_t all = {0, tiku_editdoc_length(st->doc)};
    tiku_edit_status_t status;
    const char *why = NULL;

    if (q->length == 0) {
        why = "Enter text or a pattern to find.";
    } else if ((uint64_t)all.end > EDIT_LIVE_MAX) {
        why = "Live counts are off above 2 MiB. "
              "Next/Previous still search the file.";
    } else {
        status = tiku_edit_collect(st->doc, q, all, st->matches,
                                   EDIT_SEARCH_MAX, &st->match_count,
                                   &st->match_truncated);
        if (status == TIKU_EDIT_BAD_PATTERN) {
            why = "Invalid regular expression. "
                  "Check brackets and parentheses.";
        } else if (status != TIKU_EDIT_OK) {
            why = "Search could not be completed.";
        }
    }
    for (size_t i = 0; why == NULL && i < st->match_count; i++) {
        if (st->matches[i].start < st->matches[i].end &&
            tiku_editdoc_indicator_add(st->doc, st->matches[i],
                                       EDIT_SEARCH_KIND, NULL) !=
                TIKU_EDIT_OK) {
            tiku_editdoc_indicators_clear(st->doc, 1u << EDIT_SEARCH_KIND);
            why = "Not enough memory to outline the matches.";
        }
    }
    if (why != NULL) {
        st->match_count = 0;
        st->match_truncated = 0;
        snprintf(st->search_message, sizeof st->search_message, "%s", why);
    }
}

/** Write the count line: which match the selection is, when it is one,
 *  else how many there are, saying so when more matched than are
 *  outlined. */
static void count_line(edit_state_t *st, size_t current)
{
    char *out = st->search_message;
    size_t cap = sizeof st->search_message;

    if (current && st->match_truncated) {
        snprintf(out, cap, "Match %zu of more than %d", current,
                 EDIT_SEARCH_MAX);
    } else if (current) {
        snprintf(out, cap, "Match %zu of %zu", current, st->match_count);
    } else if (st->match_truncated) {
        snprintf(out, cap, "More than %d matches; the first %d are "
                 "outlined.", EDIT_SEARCH_MAX, EDIT_SEARCH_MAX);
    } else {
        snprintf(out, cap, "%zu %s", st->match_count,
                 st->match_count == 1 ? "match" : "matches");
    }
}

/** Bring Find's count line and the document's outlines up to date.
 *  Matches are collected again only when the query, its options or the
 *  document changed; with Find closed the outlines are dropped. */
void tiku_editor_search_refresh(edit_state_t *st)
{
    tiku_edit_query_t q = find_query(st);
    uint64_t generation = tiku_editdoc_generation(st->doc);
    tiku_edit_pos_t a = 0, b = 0;
    size_t current = 0;

    if (st->finding != 1) {
        if (st->search_generation) {
            tiku_editor_search_clear(st);
        }
        return;
    }
    if (st->search_generation != generation ||
        st->search_flags != q.flags || strcmp(st->search_text, q.text)) {
        tiku_editor_search_clear(st);
        st->search_generation = generation;
        st->search_flags = q.flags;
        snprintf(st->search_text, sizeof st->search_text, "%s", q.text);
        collect(st, &q);
    }
    /* With no match the line already says why, or "0 matches". */
    if (st->search_message[0] && !st->match_count) {
        return;
    }
    (void)tiku_codeview_selection(st->view, &a, &b);
    if (a > b) {
        tiku_edit_pos_t t = a;
        a = b;
        b = t;
    }
    for (size_t i = 0; i < st->match_count && !current; i++) {
        if (st->matches[i].start == a && st->matches[i].end == b) {
            current = i + 1;
        }
    }
    count_line(st, current);
}

/** Select every match of Find's query, as one selection each, and give
 *  the document window the keys.  More than EDIT_SEARCH_MAX matches
 *  select nothing and say so. */
void tiku_editor_select_matches(edit_state_t *st)
{
    tiku_edit_query_t q = find_query(st);
    tiku_edit_range_t ranges[EDIT_SEARCH_MAX];
    tiku_edit_selection_t selections[EDIT_SEARCH_MAX];
    size_t count = 0;
    int truncated = 0;
    tiku_edit_status_t status;

    status = tiku_edit_collect(st->doc, &q,
                               (tiku_edit_range_t){0,
                                   tiku_editdoc_length(st->doc)},
                               ranges, EDIT_SEARCH_MAX, &count, &truncated);
    if (status != TIKU_EDIT_OK) {
        tiku_editor_refused(st, status);
        return;
    }
    if (truncated) {
        snprintf(st->note, sizeof st->note, "Too many matches to select "
                 "(maximum %d). Refine the search.", EDIT_SEARCH_MAX);
        return;
    }
    if (!count) {
        snprintf(st->note, sizeof st->note, "No matches.");
        return;
    }
    for (size_t i = 0; i < count; i++) {
        selections[i] = (tiku_edit_selection_t){.anchor = ranges[i].start,
                                                .caret = ranges[i].end};
    }
    status = tiku_codeview_select_many(st->view, selections, count, 0);
    if (status != TIKU_EDIT_OK) {
        tiku_editor_refused(st, status);
        return;
    }
    st->search_active = 0;
    if (st->services->raise_window) {
        st->services->raise_window(st->services->ctx, st->id);
    }
    if (count == 1) {
        snprintf(st->note, sizeof st->note,
                 "Selected 1 match. Type in the document to replace it.");
    } else {
        snprintf(st->note, sizeof st->note, "Selected %zu matches. "
                 "Type in the document to replace them.", count);
    }
}
