/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_edit_regex.h - the search tiku_edit_search.c hands a query with
 * TIKU_EDIT_REGEX to: matches found, gathered and replaced line by line
 * with the host's POSIX regular expressions.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_EDIT_REGEX_H_
#define TIKU_EDIT_REGEX_H_

#include "tiku_edit_search.h"

/* tiku_edit_collect() for a regex query, its other arguments checked. */
tiku_edit_status_t tiku_ed_regex_collect(tiku_editdoc_t *doc,
                                         const tiku_edit_query_t *q,
                                         tiku_edit_range_t scope,
                                         tiku_edit_range_t *matches,
                                         size_t capacity, size_t *count,
                                         int *truncated);
/* tiku_edit_find() for a regex query. */
tiku_edit_status_t tiku_ed_regex_find(tiku_editdoc_t *doc,
                                      const tiku_edit_query_t *q,
                                      tiku_edit_pos_t from, int backward,
                                      tiku_edit_range_t *match,
                                      int *wrapped);
/* The replacement for a match that is exactly @p range: @p length bytes of
 * @p with, the match's groups put in, in @p out, a new allocation of @p n
 * bytes and a NUL.  TIKU_EDIT_NOTHING, @p out not set, for no match. */
tiku_edit_status_t tiku_ed_regex_expand(tiku_editdoc_t *doc,
                                        const tiku_edit_query_t *q,
                                        tiku_edit_range_t range,
                                        const char *with, size_t length,
                                        char **out, size_t *n);
/* tiku_edit_replace_all() for a regex query, each match's groups put into
 * its replacement. */
tiku_edit_status_t tiku_ed_regex_replace_all(tiku_editdoc_t *doc,
                                             const tiku_edit_query_t *q,
                                             const char *with,
                                             size_t length,
                                             tiku_edit_view_t view,
                                             size_t *count);

#endif /* TIKU_EDIT_REGEX_H_ */
