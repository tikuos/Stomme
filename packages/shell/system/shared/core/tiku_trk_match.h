/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_match.h - the five ways Tracker matches a name.
 *
 * A port of TrackerString, Tracker's string-matching engine.  The Select
 * window matches names through it.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_MATCH_H_
#define TIKU_TRK_MATCH_H_

#include <stddef.h>

/** @brief The five rules, in the order the Select window offers them. */
typedef enum {
    TIKU_TRK_MATCH_NONE = 0,     /* matches nothing, as kNone does        */
    TIKU_TRK_MATCH_STARTS,
    TIKU_TRK_MATCH_ENDS,
    TIKU_TRK_MATCH_CONTAINS,
    TIKU_TRK_MATCH_GLOB,
    TIKU_TRK_MATCH_REGEX
} tiku_trk_match_t;

/**
 * @brief Whether @p text matches @p expr under @p how.
 *
 * @param case_sensitive 0 folds case, which is the Select window's default.
 * @return 1 on a match.
 */
int tiku_trk_match(const char *text, const char *expr, int case_sensitive,
                   tiku_trk_match_t how);

/**
 * @brief Whether a regular expression compiles, and why not.
 *
 * tiku_trk_match() finds no match for a bad expression; this names what is
 * wrong with it.
 *
 * @param err Receives the reason, by name.
 * @return 1 when the expression is usable.
 */
int tiku_trk_regex_check(const char *expr, char *err, size_t errmax);

#endif /* TIKU_TRK_MATCH_H_ */
