/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_syntax.h - one line of a program, told apart into what its parts
 * mean.
 *
 * Given a line, this says which stretches are reserved words, numbers,
 * quoted text, remarks, paths and jump targets; it paints nothing and
 * hands back tiku_span_t runs for a painter to colour through the theme.
 * TikuOS BASIC is known from the interpreter's own tables and classified
 * line by line; C carries whether a line begins inside a block remark,
 * which tiku_syntax_spans_on() takes and returns.  doc/syntax.md describes
 * the design.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_SYNTAX_H_
#define TIKU_SYNTAX_H_

#include "tiku_gfx.h"

/** @brief The languages this build can tell apart. */
typedef enum {
    TIKU_SYNTAX_NONE = 0,   /**< prose: every byte is the document's ink */
    TIKU_SYNTAX_BASIC,      /**< TikuOS BASIC                            */
    TIKU_SYNTAX_C           /**< C, which carries a remark between lines */
} tiku_syntax_lang_t;

/**
 * @brief The BASIC reserved word at @p index, in sorted order, or NULL
 *        past the end.
 */
const char *tiku_syntax_basic_keyword(int index);

/**
 * @brief What one line of C leaves open for the next.
 *
 * Only an open block remark is carried; nesting and strings continued by
 * a backslash are not.
 */
typedef unsigned tiku_syntax_state_t;

#define TIKU_SYNTAX_OPEN   0u   /**< nothing carried in            */
#define TIKU_SYNTAX_BLOCK  1u   /**< a block remark is still open  */

/**
 * @brief Tell @p line apart into runs of meaning, into @p out.
 *
 * Runs are returned in order and cover the line from its first byte; a
 * caller paints them in order and gives what they do not reach the plain
 * ink.  Adjacent runs of the same ink are merged, so an ordinary line is
 * one run.
 *
 * @param max How many runs @p out can hold.  Past it the rest of the line
 *            is left to the plain ink.
 * @return the number of runs written; 0 for a NULL or empty line, and 0
 *         for TIKU_SYNTAX_NONE.
 */
int tiku_syntax_spans(tiku_syntax_lang_t lang, const char *line,
                      tiku_span_t *out, int max);

/**
 * @brief Tell @p line apart as tiku_syntax_spans() does, starting in the
 *        state @p in and reporting the state the line leaves.
 *
 * BASIC carries nothing, so for BASIC the answer is tiku_syntax_spans()'s.
 *
 * @param in    what the line before this one left open.
 * @param out_state where the state this line leaves is written, when
 *              given.  A caller painting a window walks from a line whose
 *              state it knows, such as the document's first, which is
 *              TIKU_SYNTAX_OPEN, and carries the answer forward.
 */
int tiku_syntax_spans_on(tiku_syntax_lang_t lang, const char *line,
                         tiku_syntax_state_t in,
                         tiku_syntax_state_t *out_state,
                         tiku_span_t *out, int max);

/** @brief One run of @p len bytes meaning @p ink, in line order. */
typedef void (*tiku_syntax_run_fn)(void *ctx, tiku_ink_t ink, int len);

/**
 * @brief Tell @p line apart as tiku_syntax_spans_on() does, handing each
 *        run to @p fn as it is completed.
 *
 * No run is dropped however many a line holds, and adjacent runs that
 * mean the same thing arrive merged.  @p fn may be NULL, for a caller
 * that wants only what the line leaves open.
 */
void tiku_syntax_runs(tiku_syntax_lang_t lang, const char *line,
                      tiku_syntax_state_t in,
                      tiku_syntax_state_t *out_state,
                      tiku_syntax_run_fn fn, void *ctx);

/**
 * @brief Whether @p name is a word TikuOS BASIC reserves.
 *
 * Case-insensitive, and the whole word must match: PRINTER is not PRINT.
 */
int tiku_syntax_basic_word(const char *name);

/**
 * @brief Whether a BASIC jump to the @p len bytes of @p target lands on
 *        @p line, NUL-terminated.
 *
 * A number names the line that starts with that number, by its value; a
 * name, the line whose head is that label, compared case-sensitively as
 * the interpreter compares it.
 */
int tiku_syntax_basic_names(const char *line, const char *target,
                            size_t len);

/**
 * @brief The language a file called @p path is written in, by its suffix.
 *
 * ".bas" is BASIC and ".c" and ".h" are C, in any case; everything else
 * is prose.  The contents are not looked at.
 */
tiku_syntax_lang_t tiku_syntax_of_path(const char *path);

#endif /* TIKU_SYNTAX_H_ */
