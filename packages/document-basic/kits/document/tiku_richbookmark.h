/*
 * TikuDesktop Document Kit.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_richbookmark.h - persistent named positions and internal link targets.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_RICHBOOKMARK_H_
#define TIKU_RICHBOOKMARK_H_
#include "tiku_richdoc.h"

/** @brief A copied named point; its position belongs to the queried snapshot. */
typedef struct {
    char name[64];
    tiku_rich_pos_t position;
} tiku_rich_bookmark_t;

/** @brief Add a named point, or move the existing point with that exact name.
 *  @note Names are case-sensitive UTF-8, 1..63 bytes, without control bytes.
 *        Locations follow edits like anchors and participate in Undo/Redo.
 *        Zero generation accepts the current state. No callback on no-change. */
tiku_rich_status_t tiku_richdoc_bookmark_set(tiku_richdoc_t *doc,
    uint64_t generation, const char *name, tiku_rich_pos_t position);
/** @brief Remove a named point as one undoable edit; absent names return NOT_FOUND. */
tiku_rich_status_t tiku_richdoc_bookmark_remove(tiku_richdoc_t *doc,
    uint64_t generation, const char *name);
/** @brief Count named locations in stable insertion order; NULL yields zero. */
size_t tiku_richsnapshot_bookmarks(const tiku_richsnapshot_t *snapshot);
/** @brief Copy one bookmark by index; errors clear *out. */
tiku_rich_status_t tiku_richsnapshot_bookmark(const tiku_richsnapshot_t *snapshot,
    size_t index, tiku_rich_bookmark_t *out);
/** @brief Resolve an exact name without allocating; errors clear *out. */
tiku_rich_status_t tiku_richsnapshot_find_bookmark(
    const tiku_richsnapshot_t *snapshot, const char *name, tiku_rich_pos_t *out);
/** @brief Resolve a #name link after percent decoding, without external I/O.
 *  @note Names are not normalized. Missing targets return NOT_FOUND; external
 *        links return UNSUPPORTED. Errors clear *out. */
tiku_rich_status_t tiku_richsnapshot_resolve_link(
    const tiku_richsnapshot_t *snapshot, const char *target, tiku_rich_pos_t *out);
#endif
