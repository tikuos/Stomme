/*
 * TikuDesktop Translation Kit.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_richimport_private.h - budgeted staging for editable import adapters.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_RICHIMPORT_PRIVATE_H_
#define TIKU_RICHIMPORT_PRIVATE_H_
#include "tiku_richmarkup.h"
#include "tiku_richio_private.h"

/** @brief An allocation retained through checked document import. */
typedef struct tiku_rimp_owned {
    struct tiku_rimp_owned *next; /**< next retained string */
    char data[];                /**< NUL-terminated owned bytes */
} tiku_rimp_owned_t;

/** @brief One stable image description with owned decoded pixels. */
typedef struct tiku_rimp_image {
    struct tiku_rimp_image *next; /**< next retained picture */
    tiku_rich_image_t value;   /**< owned pixels and borrowed staging text */
} tiku_rimp_image_t;

/** @brief Relocation-independent paragraph staging. */
typedef struct {
    tiku_rich_paragraph_source_t value; /**< unresolved paragraph metadata */
    size_t text_at, run_at;     /**< offsets into relocatable staging arrays */
} tiku_rimp_para_t;

/** @brief Aggregate-bounded arrays and strings for one new document. */
typedef struct {
    tiku_richio_options_t options; /**< normalized limits and cancellation */
    tiku_richio_status_t status; /**< first conversion refusal */
    unsigned notices;          /**< accumulated conversion differences */
    size_t used;               /**< live staging heap bytes */
    char *text;                /**< concatenated paragraph text */
    size_t text_count, text_cap;
    tiku_rimp_para_t *paragraphs; /**< owned paragraph metadata */
    size_t paragraph_count, paragraph_cap;
    tiku_rich_run_source_t *runs; /**< owned runs, stable resource pointers */
    size_t run_count, run_cap;
    tiku_rich_block_source_t *blocks; /**< preorder source-index hierarchy */
    size_t block_count, block_cap;
    tiku_rimp_owned_t *owned;   /**< stable string allocations */
    tiku_rimp_image_t *images; /**< stable image records and decoded pixels */
    size_t current;             /**< open paragraph index + 1, or zero */
    size_t parent;              /**< one-based containing cell, zero at body */
    int headings;              /**< heading styles referenced by paragraphs */
} tiku_rimp_t;

/** @brief Poll cancellation and preserve the first conversion failure. */
int tiku_rimp_active(tiku_rimp_t *r);
/** @brief Reserve zeroed staging bytes under the aggregate import ceiling. */
void *tiku_rimp_allocate(tiku_rimp_t *r, size_t bytes);
/** @brief Retain a NUL-terminated copy, charged to the import budget. */
char *tiku_rimp_string(tiku_rimp_t *r, const char *text, size_t length);
/** @brief Append a hierarchy node; return its one-based index, zero on error. */
size_t tiku_rimp_block(tiku_rimp_t *r, tiku_rich_block_kind_t kind,
                       size_t parent);
/** @brief Start one paragraph with caller-supplied direct/default properties. */
void tiku_rimp_paragraph(tiku_rimp_t *r,
                          const tiku_rich_paragraph_source_t *properties);
/** @brief Append styled UTF-8 bytes to the current paragraph. */
void tiku_rimp_text(tiku_rimp_t *r, const char *text, size_t length,
                    const tiku_rich_chars_t *chars, const char *link);
/** @brief Append an image run, taking its pixels on success only.
 *  @note The alternate text must already belong to staging. Pixels are charged
 *        with their metadata; failed appends leave ownership with the caller. */
int tiku_rimp_picture(tiku_rimp_t *r, const tiku_rich_image_t *image,
                       const tiku_rich_chars_t *chars, const char *link);
/** @brief Import the staged tree, releasing no staging storage. */
tiku_richio_status_t tiku_rimp_finish(tiku_rimp_t *r, tiku_richdoc_t **out);
/** @brief Release every array and auxiliary string owned by staging. */
void tiku_rimp_dispose(tiku_rimp_t *r);
#endif
