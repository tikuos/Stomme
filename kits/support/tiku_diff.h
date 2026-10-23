/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_diff.h - what changed between two sequences of lines.
 *
 * Myers' O(ND) difference, split at the middle snake so that memory stays
 * linear: two arrays of lines in, runs of kept, removed and added lines
 * out, and the unified text that diff -u writes and patch reads.  Lines
 * are numbered by their content first, so the search compares integers,
 * and a line found on one side only is set aside before it starts.  Past
 * a bounded number of steps a split settles for the furthest point either
 * search reached; the script is then correct but may not be the shortest,
 * and the diff's shortest is 0.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_DIFF_H_
#define TIKU_DIFF_H_

#include <stddef.h>
#include <stdint.h>

/** @brief One line, borrowed from the caller's text, its LF included
 *         when it has one: a last line without an LF differs from the
 *         same text with one. */
typedef struct {
    const char *text;
    size_t      len;
} tiku_diff_line_t;

/** @brief What a run of lines did. */
typedef enum {
    TIKU_DIFF_SAME = 0,     /* a[a..a+n) is b[b..b+n)            */
    TIKU_DIFF_DEL,          /* a[a..a+n) removed, before b[b]     */
    TIKU_DIFF_ADD           /* b[b..b+n) added, before a[a]       */
} tiku_diff_kind_t;

/** @brief A run: where it starts on each side, and how many lines. */
typedef struct {
    tiku_diff_kind_t kind;
    long a, b, n;
} tiku_diff_run_t;

/** @brief The runs that turn one side into the other, in order. */
typedef struct {
    tiku_diff_run_t *run;
    long nrun;
    long removed, added;    /* lines, over all the runs               */
    int  shortest;          /* 0 when a bound cut the search short    */
} tiku_diff_t;

#define TIKU_DIFF_SPACE 0x1     /* whitespace changes are not changes */
#define TIKU_DIFF_CR    0x2     /* a line's last CR is not part of it */
#define TIKU_DIFF_EXACT 0x4     /* no step bound: always the shortest */

/** @brief @p text cut into lines after each LF, pointing into it; @p lines
 *         is set to a malloc'd array of them.  @return how many, or -1
 *         out of memory. */
long tiku_diff_split(const char *text, size_t len, tiku_diff_line_t **lines);

/** @brief The runs that turn @p a into @p b, compared as @p flags say.
 *  @return 0, or -1 out of memory with @p out empty. */
int tiku_diff_lines(const tiku_diff_line_t *a, long na,
                    const tiku_diff_line_t *b, long nb, int flags,
                    tiku_diff_t *out);

/** @brief As tiku_diff_lines() for two arrays of numbers, such as a
 *         line's words once numbered; of the flags only TIKU_DIFF_EXACT
 *         applies.  @return 0, or -1 out of memory. */
int tiku_diff_ints(const uint32_t *a, long na, const uint32_t *b, long nb,
                   int flags, tiku_diff_t *out);

/** @brief Give back what a diff holds; @p d may be NULL, empty or
 *         zeroed. */
void tiku_diff_free(tiku_diff_t *d);

/**
 * @brief @p d as unified text: "---" and "+++" naming @p name_a and
 *        @p name_b, then hunks with @p context lines about each change,
 *        a last line without its LF marked as diff marks it.
 * @return a malloc'd, NUL-ended string, @p *len its length; "" when the
 *         two are the same; NULL out of memory.
 */
char *tiku_diff_unified(const tiku_diff_t *d, const tiku_diff_line_t *a,
                        const tiku_diff_line_t *b, const char *name_a,
                        const char *name_b, int context, size_t *len);

#endif /* TIKU_DIFF_H_ */
