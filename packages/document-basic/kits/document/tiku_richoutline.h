/*
 * TikuDesktop Document Kit.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_richoutline.h - snapshot-relative, incremental heading navigation.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_RICHOUTLINE_H_
#define TIKU_RICHOUTLINE_H_
#include "tiku_richdoc.h"

/** @brief An incremental heading index retaining immutable source content. */
typedef struct tiku_richoutline tiku_richoutline_t;

/** @brief A semantic heading, including headings within table cells.
 *  @note Text is borrowed through outline release and is not NUL-terminated.
 *        The position names the paragraph start in the captured generation. */
typedef struct {
    tiku_rich_pos_t position;
    size_t paragraph_index;
    unsigned level;              /**< 1..6, resolved through named styles */
    const char *text;
    size_t length;
} tiku_rich_heading_t;

/** @brief Create a heading scan, charged to the snapshot's allocation owner.
 *  @note The caller can release the snapshot after creation. Failure clears out.
 *        Outline objects are single-caller objects; release cancels a scan. */
tiku_rich_status_t tiku_richoutline_create(const tiku_richsnapshot_t *snapshot,
                                            tiku_richoutline_t **out);
/** @brief Inspect up to work paragraphs; zero inspects none.
 *  @return PENDING, OK on completion, or a latched allocation refusal. */
tiku_rich_status_t tiku_richoutline_step(tiku_richoutline_t *outline,
                                          size_t work);
/** @brief Read terminal or pending status; NULL returns INVALID. */
tiku_rich_status_t tiku_richoutline_status(const tiku_richoutline_t *outline);
/** @brief Source generation, or zero for NULL. */
uint64_t tiku_richoutline_generation(const tiku_richoutline_t *outline);
/** @brief Number of complete entries, including during an unfinished scan. */
size_t tiku_richoutline_count(const tiku_richoutline_t *outline);
/** @brief Copy an indexed entry; bad arguments clear a supplied output. */
tiku_rich_status_t tiku_richoutline_get(const tiku_richoutline_t *outline,
    size_t index, tiku_rich_heading_t *out);
/** @brief Find a heading strictly before or after a valid source position.
 *  @note forward is 0 or 1. Requires a complete index; no wrap at either end.
 *        Failure stores SIZE_MAX in index. Positions ignore caret affinity. */
tiku_rich_status_t tiku_richoutline_neighbor(const tiku_richoutline_t *outline,
    tiku_rich_pos_t position, int forward, size_t *index);
/** @brief Release the index and its source; NULL is accepted. */
void tiku_richoutline_release(tiku_richoutline_t *outline);
#endif
