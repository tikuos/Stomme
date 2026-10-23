/*
 * TikuDesktop Document Kit.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_richcells_private.h - bounded tabular text and paste composition.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_RICHCELLS_PRIVATE_H_
#define TIKU_RICHCELLS_PRIVATE_H_
#include "tiku_richdoc_private.h"

/** @brief One decoded text cell within owned tabular storage. */
typedef struct {
    size_t start, length;    /**< byte slice in the normalized text buffer */
} tiku_rd_textcell_t;

/** @brief Row-major rectangular text with normalized LF paragraph breaks. */
typedef struct {
    char *text;             /**< owned decoded UTF-8 bytes */
    tiku_rd_textcell_t *cells; /**< owned field offsets */
    size_t count;           /**< initialized fields */
    unsigned rows, columns;
} tiku_rd_tsv_t;

/** @brief Parse bounded quoted TSV; failure releases storage and clears out. */
tiku_rich_status_t tiku_rd_tsv_parse(tiku_rd_memory_t *memory,
    const char *text, size_t length, tiku_rd_tsv_t *out);
/** @brief Release owned decoded text and cell offsets. */
void tiku_rd_tsv_drop(tiku_rd_tsv_t *table);
#endif
