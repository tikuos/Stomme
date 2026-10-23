/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_edit_search.h - literal and regex search in an editor document.
 *
 * A match lies within one line.  Each line is read from its start, left
 * to right, and a match is taken whole before the next is looked for, so
 * matches never overlap and the same ones are found whichever way a
 * search goes.  Without TIKU_EDIT_MATCH_CASE a literal search folds only
 * ASCII letters, and a regular expression folds as its locale does.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_EDIT_SEARCH_H_
#define TIKU_EDIT_SEARCH_H_

#include <stddef.h>

#include "tiku_editdoc.h"

/* Letters match only in the case they are written in. */
#define TIKU_EDIT_MATCH_CASE 0x1u
/* A match is neither preceded nor followed by a word byte: an ASCII
 * letter, a digit, '_' or any byte past ASCII. */
#define TIKU_EDIT_WHOLE_WORD 0x2u
/* The query is a POSIX extended regular expression, matched within each
 * line in a UTF-8 locale of the kit's own; the process's locale is left
 * as it is.  An empty match is found once, and the next search starts a
 * character past it.  A replacement puts in group n for $n, n one digit,
 * or for ${n}, n up to 31, a dollar for $$, and a line feed, CR, tab or
 * backslash for \n, \r, \t or \\.  PCRE syntax, such as \d, lookaround
 * and lazy repeats, is not understood, and a search runs to its end
 * before it returns. */
#define TIKU_EDIT_REGEX 0x4u

/** @brief What to look for: valid UTF-8, not empty, with no NUL, CR or
 *         LF; anything else is refused with TIKU_EDIT_INVALID_TEXT. */
typedef struct {
    const char *text;
    size_t length;
    unsigned flags;             /* TIKU_EDIT_MATCH_CASE, _WHOLE_WORD,
                                   _REGEX                               */
} tiku_edit_query_t;

/**
 * @brief The matches lying wholly inside @p scope, in order: at most
 *        @p capacity of them in @p matches, how many in @p count, and
 *        @p truncated set when there are more.
 *
 * Matches are found as tiku_edit_find() finds them, in one pass, and the
 * document and its selections are left as they are.  After an error
 * @p count is 0 and what @p matches holds is not to be read.
 */
tiku_edit_status_t tiku_edit_collect(tiku_editdoc_t *doc,
                                     const tiku_edit_query_t *query,
                                     tiku_edit_range_t scope,
                                     tiku_edit_range_t *matches,
                                     size_t capacity, size_t *count,
                                     int *truncated);

/**
 * @brief The first match starting at or after @p from, or with
 *        @p backward the last one starting before it.
 *
 * With none that way the search starts again from the other end, and
 * @p wrapped (when given) is set.  TIKU_EDIT_NOTHING when there is no
 * match anywhere.
 */
tiku_edit_status_t tiku_edit_find(tiku_editdoc_t *doc,
                                  const tiku_edit_query_t *q,
                                  tiku_edit_pos_t from, int backward,
                                  tiku_edit_range_t *match, int *wrapped);

/** @brief Whether @p range holds exactly one match of @p q. */
int tiku_edit_is_match(tiku_editdoc_t *doc, const tiku_edit_query_t *q,
                       tiku_edit_range_t range);

/**
 * @brief Replace every match with @p length bytes of @p with, as one
 *        undo step that names @p view as its origin.
 *
 * @p count, when given, takes the number replaced.  TIKU_EDIT_NOTHING
 * when there is no match; any failure changes nothing.
 */
tiku_edit_status_t tiku_edit_replace_all(tiku_editdoc_t *doc,
                                         const tiku_edit_query_t *q,
                                         const char *with, size_t length,
                                         tiku_edit_view_t view,
                                         size_t *count);

#endif /* TIKU_EDIT_SEARCH_H_ */
