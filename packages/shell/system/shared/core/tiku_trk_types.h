/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_types.h - what the system knows about a type.
 *
 * The type database: a description, an icon and the extra columns each
 * type offers, beside the compiled-in type table, which cannot be added to.
 * It is written once on first run and read from a file afterwards, so a
 * type is added without a rebuild (AW-013, AW-015, CW-026).
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_TYPES_H_
#define TIKU_TRK_TYPES_H_

#include "tiku_model.h"
#include "tiku_trk_view_defs.h"

/** @brief Room for types, the columns of one type, and a description. */
#define TIKU_TRK_TYPES_MAX       40
#define TIKU_TRK_TYPE_COLS_MAX    6
#define TIKU_TRK_TYPE_DESC_MAX   48

/** @brief One type, as the database describes it. */
typedef struct {
    char type[TIKU_TYPE_MAX];
    char desc[TIKU_TRK_TYPE_DESC_MAX];   /* what a Kind column says      */
    char icon[TIKU_NAME_MAX];        /* the art it wears             */
    /* The columns this type is worth showing, by TIKU_TRK_COL_*.  A type
     * that offers none shows the generic columns every listing has. */
    int  col[TIKU_TRK_TYPE_COLS_MAX];
    int  ncol;
    /* Attribute columns, written in the file as "attr:<name>".  They are
     * kept by name: a view gives each its column id. */
    char attr[TIKU_TRK_TYPE_COLS_MAX][TIKU_TRK_ATTR_NAME_MAX];
    int  nattr;
    /* A document: opened by its handler even when it could be run,
     * written in the file as the word "document" among the columns. */
    int  document;
} tiku_trk_typerec_t;

/** @brief The type database. */
typedef struct tiku_trk_types {
    tiku_trk_typerec_t rec[TIKU_TRK_TYPES_MAX]; /* one per type read     */
    int                n;                        /* how many are filled   */
} tiku_trk_types_t;

/** @brief Start empty. */
void tiku_trk_types_init(tiku_trk_types_t *db);

/**
 * @brief Add or replace one type's record.
 *
 * @param cols Comma-separated column names ("size", "modified", ...),
 *             "attr:<name>" columns and the word "document", or NULL for
 *             none.
 * @return its index, or -1 when the database is full or @p type is empty.
 */
int tiku_trk_types_add(tiku_trk_types_t *db, const char *type,
                       const char *desc, const char *icon, const char *cols);

/**
 * @brief What the database says about @p type, or NULL.
 *
 * Exact first, then the longest supertype record, so "text/" answers for
 * every text type the database does not name (MA-017).
 */
const tiku_trk_typerec_t *tiku_trk_types_find(const tiku_trk_types_t *db,
                                              const char *type);

/** @brief The record naming @p type exactly, without supertype fallback. */
const tiku_trk_typerec_t *tiku_trk_types_exact(const tiku_trk_types_t *db,
                                               const char *type);

/** @brief The longest supertype record, ending in '/' or '-', that @p type
 *         begins with and is longer than; or NULL. */
const tiku_trk_typerec_t *tiku_trk_types_super(const tiku_trk_types_t *db,
                                               const char *type);

/**
 * @brief Read a database file into @p db, replacing records of the same
 *        type: one "type<TAB>description<TAB>icon<TAB>columns" per line.
 *
 * @return how many records were read, or -1 when it cannot be opened.
 */
int tiku_trk_types_load(tiku_trk_types_t *db, const char *path);

/** @brief Write @p db to @p path.  @return 0 when written, else -1. */
int tiku_trk_types_save(const tiku_trk_types_t *db, const char *path);

/**
 * @brief Install the system types, once (AW-013).
 *
 * Only when the file cannot be read, so a database the user has edited is
 * left alone.  Either way @p db holds the database afterwards.
 *
 * @return 1 when it installed, 0 when one was already there.
 */
int tiku_trk_types_install(tiku_trk_types_t *db, const char *path);

/** @brief The column id a name stands for, or -1. */
int tiku_trk_types_column(const char *name);

#endif /* TIKU_TRK_TYPES_H_ */
