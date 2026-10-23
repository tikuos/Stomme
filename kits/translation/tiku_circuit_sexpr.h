/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_circuit_sexpr.h - the bounded syntax tree of a KiCad S-expression
 * file: one flat array of nodes in document order, each list knowing
 * where its subtree ends, shared by the interpreter's source files.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_CIRCUIT_SEXPR_H_
#define TIKU_CIRCUIT_SEXPR_H_

#include "tiku_circuit.h"

#define CIRCUIT_INPUT_MAX   (8u * 1024u * 1024u)    /* one document      */
#define CIRCUIT_TOKEN_MAX   262144u     /* nodes in one document         */
#define CIRCUIT_NEST_MAX    128u        /* lists within lists            */
#define CIRCUIT_TOKEN_BYTES 4096u       /* decoded bytes of one atom     */
#define CIRCUIT_NODES_FIRST 256u        /* nodes the array is first given */
#define SX_NONE             SIZE_MAX    /* no such node                  */

/** @brief An atom, or a list whose children follow it. */
typedef struct {
    const char *text;               /* NULL for a list; in tree.strings  */
    size_t end;                     /* first node after this subtree     */
    unsigned line, column;          /* 1-based source location           */
    int quoted;                     /* a string, not a bare atom         */
} circuit_sx_node_t;

/** @brief A parsed document. */
typedef struct {
    circuit_sx_node_t *node;        /* owned nodes, in preorder          */
    char *strings;                  /* owned decoded atoms               */
    size_t count, capacity;         /* nodes in use; nodes allocated     */
} circuit_sx_t;

/**
 * @brief Parse @p bytes of S-expression text into @p out.
 *
 * Strings are unescaped, and every atom must be strict UTF-8 with no
 * character below space but tab and newline; numbers stay text.  The
 * input size, node count, atom length and nesting are bounded.
 *
 * @param options  cancellation, or NULL
 * @param diag     the first failure and its position, or NULL
 * @return OK with @p out owning the tree, or why not with @p out
 *         untouched
 */
tiku_circuit_err_t circuit_sx_parse(const void *data, size_t bytes,
    const tiku_circuit_import_t *options, circuit_sx_t *out,
    tiku_circuit_diagnostic_t *diag);

/** @brief Let @p tree's nodes and strings go, leaving it empty. */
void circuit_sx_free(circuit_sx_t *tree);

/** @brief The name heading list @p list, or NULL for an atom or a bad
 *         index. */
const char *circuit_sx_head(const circuit_sx_t *tree, size_t list);

/** @brief The first list directly in @p list headed @p name, or SX_NONE.
 */
size_t circuit_sx_child(const circuit_sx_t *tree, size_t list,
                        const char *name);

/** @brief Child @p index after @p list's head, lists counted too, when it
 *         is an atom; SX_NONE when it is a list or absent. */
size_t circuit_sx_arg(const circuit_sx_t *tree, size_t list, size_t index);

/** @brief Unquoted atom @p atom as a plain decimal number: an optional
 *         sign, digits and a point, no exponent, whatever the locale.
 *  @return 1 with @p out set, or 0 */
int circuit_sx_number(const circuit_sx_t *tree, size_t atom, double *out);

#endif
