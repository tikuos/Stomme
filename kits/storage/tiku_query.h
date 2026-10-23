/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_query.h - searching the namespace.
 *
 * A query is a predicate plus a scope: terms are (attribute, operator,
 * value), terms join with and/or, and the scope is a path-prefix test on
 * each result, outside the predicate.  A name search is a substring match
 * unless the text holds a '*', when the whole text is a pattern;
 * case-insensitivity rewrites the value into per-character classes ("txt"
 * as "[tT][xX][tT]"), so one matcher serves every operator.  A volume is a
 * namespace subtree and a node's descriptor stands in for a type registry,
 * so a search walks the namespace and needs no index.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_QUERY_H_
#define TIKU_QUERY_H_

#include "tiku_attr.h"
#include "tiku_model.h"

#include <stdint.h>

#define TIKU_QUERY_TERMS_MAX   6
#define TIKU_QUERY_SCOPES_MAX  4
#define TIKU_QUERY_VALUE_MAX  64
#define TIKU_QUERY_PRED_MAX  512

/** @brief Which field a term tests.  Saved records store it by number, so
 *         the order is fixed. */
typedef enum {
    TIKU_QF_NAME = 0,
    TIKU_QF_PATH,
    TIKU_QF_KIND,
    TIKU_QF_VALUE,
    TIKU_QF_SIZE,
    TIKU_QF_CAP,
    TIKU_QF_UNIT,        /* the descriptor's unit: bytes, Hz, s ...    */
    TIKU_QF_WRITABLE,
    /* The entry's type.  With IS and IS_NOT it is matched as a type, so a
     * supertype admits everything under it (Q-004). */
    TIKU_QF_TYPE,
    TIKU_QF_MODIFIED     /* seconds since epoch, with date syntax     */
} tiku_qfield_t;

/** @brief The operator vocabulary.  Saved records store it by number, so
 *         the order is fixed. */
typedef enum {
    TIKU_OP_CONTAINS = 0,
    TIKU_OP_IS,
    TIKU_OP_IS_NOT,
    TIKU_OP_BEGINS_WITH,
    TIKU_OP_ENDS_WITH,
    TIKU_OP_GREATER,
    TIKU_OP_LESS
} tiku_qop_t;

/** @brief How a term joins the one before it. */
typedef enum {
    TIKU_JOIN_AND = 0,
    TIKU_JOIN_OR
} tiku_qjoin_t;

/** @brief One term: a field, an operator and a value. */
typedef struct {
    tiku_qfield_t field;
    tiku_qop_t    op;
    char              value[TIKU_QUERY_VALUE_MAX];
    tiku_qjoin_t  join;      /* how it joins the previous term         */
    int               pattern_ready; /* formula already supplied a glob   */
} tiku_qterm_t;

/** @brief What the panel is asking. */
typedef enum {
    TIKU_QMODE_NAME = 0,
    TIKU_QMODE_ATTRIBUTE,
    TIKU_QMODE_FORMULA
} tiku_qmode_t;

/** @brief What a saved record is, which decides what opening it does. */
typedef enum {
    TIKU_QREC_QUERY = 0,     /* opening it runs it                    */
    TIKU_QREC_TEMPLATE       /* opening it opens the panel, filled in */
} tiku_qrec_t;

/**
 * @brief What a scope was when it was written.
 *
 * Mount points are reused, so the volume's identity is kept beside the path
 * and checked when the query runs: a scope whose volume is not the one
 * recorded is left out of the run, and a run with no scope left finds
 * nothing (Q-027).
 */
typedef struct {
    char     path[TIKU_PATH_MAX];
    uint64_t dev;                /* 0 when it was not recorded            */
    char     volume[TIKU_NAME_MAX];
} tiku_qscope_t;

/** @brief A query: its terms, its scopes and what kind of record it is. */
typedef struct {
    tiku_qmode_t mode;
    tiku_qterm_t term[TIKU_QUERY_TERMS_MAX];
    int              term_count;
    /* Scope: the subtrees a result may come from.  None means the whole
     * namespace, which is the "All disks" case. */
    char             scope[TIKU_QUERY_SCOPES_MAX][TIKU_PATH_MAX];
    int              scope_count;
    /* What each scope was, in the order of scope[]; a v1 record carries
     * the paths only. */
    tiku_qscope_t scope_was[TIKU_QUERY_SCOPES_MAX];
    int              max_results;
    /* The Trash is excluded unless include_trash is set.  Besides the
     * TRASH kind, trash_path marks the excluded subtree, so the children of
     * a scope inside the Trash are excluded too. */
    int              include_trash;
    char             trash_path[TIKU_PATH_MAX];
    /* Set when a formula did not parse: tiku_query_start() refuses the
     * query, whose empty predicate would match the whole namespace. */
    int              invalid;
    /* Set when a Modified term is relative ("2 days ago"); the query is
     * due again at its next minute, hour or day boundary (Q-009). */
    int              dynamic_date;
    /* What the record is, and whether it is temporary: a query the user
     * never named, which tiku_query_sweep() deletes once it is stale
     * (MA-053, AW-090). */
    tiku_qrec_t  kind;
    int              temporary;
    int64_t          saved_at;   /* seconds; 0 when never written         */
    /* The type the panel had selected, ANDed onto everything (Q-004). */
    char             type[TIKU_TYPE_MAX];
    /* Lines of a loaded record that could not be placed; non-zero means
     * the query is not the whole record. */
    int              dropped;
} tiku_query_t;

/** @brief Start an empty query over the whole namespace. */
void tiku_query_init(tiku_query_t *q);

/**
 * @brief Set the query from typed text, as by-name mode does.
 *
 * The '*' rule applies: with a star anywhere the text is a pattern matched
 * outright; without one it is a substring.  Empty text matches everything,
 * because the substring form becomes "*".
 */
void tiku_query_set_name(tiku_query_t *q, const char *text);

/** @brief Parse the formula grammar tiku_query_predicate() writes.
 *  @return 0, or -1 with the query left marked invalid. */
int tiku_query_set_formula(tiku_query_t *q, const char *formula);

/** @brief Append a term.  @return its index, or -1. */
int tiku_query_add_term(tiku_query_t *q, tiku_qfield_t field,
                            tiku_qop_t op, const char *value,
                            tiku_qjoin_t join);

/** @brief Restrict results to a subtree.  @return 1 when it was added. */
int tiku_query_add_scope(tiku_query_t *q, const char *path);

/** @brief Drop every scope, so the query covers the whole namespace. */
void tiku_query_clear_scope(tiku_query_t *q);

/** @brief Record the current identities of scopes that have none yet. */
void tiku_query_bind_scopes(tiku_query_t *q,
                                tiku_backend_t *backend);

/**
 * @brief Render the predicate the way the formula field shows it.
 *
 * A panel switched into formula mode is filled with this text.
 */
int tiku_query_predicate(const tiku_query_t *q, char *out,
                             size_t max);

/** @brief Whether a scope admits @p path, as a path-prefix test. */
int tiku_query_in_scope(const tiku_query_t *q, const char *path);

/** @brief Whether a node is outside Trash, or Trash was explicitly included. */
int tiku_query_allows_path(const tiku_query_t *q,
                               const tiku_model_t *m);

/** @brief Whether one node satisfies the predicate (scope not considered). */
int tiku_query_matches(const tiku_query_t *q,
                           const tiku_model_t *m,
                           const tiku_desc_t *d, const char *value);

/** @brief Match using an explicit wall-clock time (dynamic-date tests). */
int tiku_query_matches_at(const tiku_query_t *q,
                              const tiku_model_t *m,
                              const tiku_desc_t *d, const char *value,
                              int64_t now);

/** @brief Next wall-clock boundary a dynamic query should refresh at. */
int64_t tiku_query_next_refresh(const tiku_query_t *q, int64_t now);

/**
 * @brief Match @p name against @p pattern, with '*', '?' and [class].
 *
 * Every text operator matches through it.  A '[' with no ']' after it is
 * a literal bracket.
 */
int tiku_query_glob(const char *pattern, const char *name);

/**
 * @brief Build the case-insensitive pattern for @p value under @p op.
 *
 * Each letter becomes a two-member class and each space becomes '*', so
 * searching "foo bar" also finds "foo-bar"; @p op adds the leading or
 * trailing star a substring, prefix or suffix search needs.
 */
int tiku_query_pattern(const char *value, tiku_qop_t op, char *out,
                           size_t max);

/*---------------------------------------------------------------------------*/
/* Running one                                                               */
/*---------------------------------------------------------------------------*/

typedef struct tiku_qrun tiku_qrun_t;

/** @brief Begin a search.  Results arrive as the walk proceeds. */
tiku_qrun_t *tiku_query_start(tiku_backend_t *b,
                                      const tiku_query_t *q);

/** @brief End the search and free it and its results. */
void tiku_query_stop(tiku_qrun_t *r);

/**
 * @brief Advance the walk by at most @p budget directories.
 *
 * Each call returns after at most @p budget directories, so the caller
 * keeps drawing between steps.
 *
 * @return New results this step, or -1 when finished.
 */
int tiku_query_step(tiku_qrun_t *r, int budget);

/** @brief How many results the run holds; 0 for NULL. */
int tiku_query_count(const tiku_qrun_t *r);

/** @brief Result @p i, or NULL past the end. */
const tiku_model_t *tiku_query_at(const tiku_qrun_t *r, int i);

/** @brief How many directories have been visited, for a progress line. */
int tiku_query_visited(const tiku_qrun_t *r);

/** @brief Whether the walk has finished; 1 for NULL. */
int tiku_query_done(const tiku_qrun_t *r);

/** @brief Whether this live run searches the volume identified by @p dev. */
int tiku_query_targets_device(const tiku_qrun_t *r, uint64_t dev);

/** @brief Type whose results layout this run shares, or empty for any type. */
const char *tiku_query_search_type(const tiku_qrun_t *r);

/*---------------------------------------------------------------------------*/
/* Live queries                                                              */
/*---------------------------------------------------------------------------*/

/**
 * @brief Why a result left the set.
 *
 * A node that no longer matches only loses its row; a node that is gone
 * may also take away what the window is about.  The reason is found when
 * the result leaves, by asking whether the node still exists.
 */
typedef enum {
    TIKU_QGONE_UNMATCHED = 0,   /* still there, no longer qualifies   */
    TIKU_QGONE_VANISHED         /* the node itself has gone           */
} tiku_qgone_t;

/** @brief What changed between two runs. */
typedef struct {
    int              added;
    int              removed;
    int              vanished;      /* of the removed, how many were gone */
} tiku_qdiff_t;

/**
 * @brief Re-run the query and report what changed.
 *
 * Results found again keep their place in the set.
 *
 * @note The run is the live query: a caller that stops it after the first
 *       fill has nothing left to refresh.
 * @return 0, or -1 on failure.
 */
int tiku_query_refresh(tiku_qrun_t *r, tiku_qdiff_t *out);

/**
 * @brief Begin a refresh without walking it: snapshot, then restart.
 *
 * This call, tiku_query_step() and tiku_query_refresh_end() are
 * tiku_query_refresh() in steps, for a caller that draws between them.
 * The snapshot lives on the run in between.
 *
 * @note Show the results only after the end: mid-walk the set is half
 *       rebuilt.
 * @return 0 when a refresh is under way, -1 when one already was.
 */
int tiku_query_refresh_begin(tiku_qrun_t *r);

/** @brief Whether a refresh has begun and not yet been ended. */
int tiku_query_refreshing(const tiku_qrun_t *r);

/**
 * @brief End it, once tiku_query_done() says the walk finished.
 * @return 0, or -1 when no refresh was under way.
 */
int tiku_query_refresh_end(tiku_qrun_t *r, tiku_qdiff_t *out);

/** @brief Whether result @p i arrived in the most recent refresh. */
int tiku_query_is_new(const tiku_qrun_t *r, int i);

/*---------------------------------------------------------------------------*/
/* Saved queries                                                             */
/*---------------------------------------------------------------------------*/

/** @brief Where a saved query is kept, and under what attribute. */
#define TIKU_QUERY_STORE_NODE "\x01queries"
#define TIKU_QUERY_ATTR       "_trk/query"
#define TIKU_QUERY_INDEX_ATTR "_trk/query_index"

struct tiku_store;

/** @brief The query as its saved text form.  Length, or -1. */
int tiku_query_serialise(const tiku_query_t *q, char *buf,
                             size_t max);

/**
 * @brief Write @p q under @p name, and add the name to the head of the
 *        saved-query index when the index does not list it yet.
 *
 * The record goes to the state store under the query's name.
 *
 * @return 0, or -1 when the store refused.
 */
int tiku_query_save(const tiku_query_t *q, const char *name,
                        struct tiku_store *store);

/** @brief Read back a saved query.  @return 0, or -1 when absent. */
int tiku_query_load(tiku_query_t *q, const char *name,
                        struct tiku_store *store);

/** @brief How many saved records the index is read into at once. */
#define TIKU_QUERY_LIST_MAX 32

/**
 * @brief Names of the saved queries, newest first.
 *
 * @return How many were written to @p names.
 */
int tiku_query_list(struct tiku_store *store, char names[][128],
                        int max);

/** @brief Forget a saved query: its record and its line in the index.
 *  @return 0, or -1 when the index cannot be read or written. */
int tiku_query_forget(const char *name, struct tiku_store *store);

/**
 * @brief The saved records of one kind, newest first (Q-031, Q-032).
 *
 * The panel's two menus, the templates and the queries that were run,
 * each read the index through this, in the index's newest-first order.
 *
 * @return how many were written to @p names.
 */
int tiku_query_list_kind(struct tiku_store *store,
                             tiku_qrec_t kind, char names[][128],
                             int max);

/** @brief How old a temporary query may get before the sweep takes it. */
#define TIKU_QUERY_STALE_SEC  (7 * 24 * 60 * 60)

/** @brief How long after a start the sweep holds off (AW-090). */
#define TIKU_QUERY_SWEEP_WAIT (30 * 60)

/**
 * @brief Delete the temporary records nobody is looking at (Q-037, AW-090).
 *
 * Three things spare a record: being a template or a named query, being
 * younger than TIKU_QUERY_STALE_SEC, or being shown in a window now.
 *
 * @param now      Seconds, the same clock saved_at is stamped from.
 * @param up_for   Seconds since this Tracker started; the sweep does not
 *                 run before TIKU_QUERY_SWEEP_WAIT.
 * @param open     Names currently shown in a window; may be NULL.
 * @param nopen    How many names @p open holds.
 * @return how many were deleted, or -1 when it did not run.
 */
int tiku_query_sweep(struct tiku_store *store, int64_t now,
                         int64_t up_for, const char *const *open, int nopen);

#endif /* TIKU_QUERY_H_ */
