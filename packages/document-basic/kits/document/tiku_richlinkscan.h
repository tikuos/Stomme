/*
 * TikuDesktop Document Kit.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_richlinkscan.h - incremental navigation between hyperlink occurrences.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_RICHLINKSCAN_H_
#define TIKU_RICHLINKSCAN_H_
#include "tiku_richdoc.h"

/** @brief A single-caller navigation scan retaining an immutable source. */
typedef struct tiku_richlinkscan tiku_richlinkscan_t;
/** @brief One contiguous link occurrence within a paragraph.
 *  @note Positions are UTF-8 byte boundaries, not necessarily grapheme
 *        boundaries. Target is borrowed until the scan is released. */
typedef struct {
    tiku_rich_pos_t first, last;
    const char *target;
    uint64_t generation;
} tiku_rich_link_match_t;

/** @brief Find a link starting at/after position, or strictly before it.
 *  @note forward is 0 or 1. Adjacent runs with the same target form one
 *        occurrence; paragraphs and unlinked runs separate occurrences.
 *        No wrap or target activation occurs. Allocation is source-charged.
 *        Failure clears *out. The caller may release the source snapshot. */
tiku_rich_status_t tiku_richlinkscan_create(const tiku_richsnapshot_t *source,
    tiku_rich_pos_t position, int forward, tiku_richlinkscan_t **out);
/** @brief Inspect at most work runs or paragraph boundaries; zero does no work.
 *  @return PENDING, OK with a match, NOT_FOUND at an end, or INVALID for NULL. */
tiku_rich_status_t tiku_richlinkscan_step(tiku_richlinkscan_t *scan, size_t work);
/** @brief Read the completed match, clearing *out on pending or refusal. */
tiku_rich_status_t tiku_richlinkscan_result(const tiku_richlinkscan_t *scan,
                                          tiku_rich_link_match_t *out);
/** @brief Release the scan and its retained source; NULL is accepted. */
void tiku_richlinkscan_release(tiku_richlinkscan_t *scan);
#endif
