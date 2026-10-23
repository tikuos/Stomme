/*
 * TikuDesktop Document Kit.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_richruler.h - paragraph indentation geometry and checked ruler edits.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_RICHRULER_H_
#define TIKU_RICHRULER_H_
#include "tiku_richview.h"

/** @brief Ruler handles, also used as bit indices in mixed and enabled. */
typedef enum {
    TIKU_RICH_RULER_FIRST, /**< first-line start, independent of other lines */
    TIKU_RICH_RULER_LEFT,  /**< other-line start; carries first-line offset */
    TIKU_RICH_RULER_RIGHT  /**< right text boundary, independent of alignment */
} tiku_rich_ruler_part_t;

/** @brief Selection ruler in document points; copy unchanged as an edit token.
 *  @note Positions include list-label spacing, not alignment offsets.
 *        Mixed handles show the first selected paragraph's position.
 *        This describes continuous editor width, not printed page margins. */
typedef struct {
    double origin, width;
    double position[3], minimum[3], maximum[3];
    unsigned mixed, enabled; /**< one bit per handle; read-only disables edits */
    size_t paragraphs;
    uint64_t generation, session, anchor, caret; /**< capture identity */
    double viewport, zoom;   /**< capture transform; not editable properties */
    const tiku_richview_t *view; /**< borrowed identity; token dies with view */
} tiku_rich_ruler_t;

/** @brief Read ruler geometry for at most 1024 selected paragraphs.
 *  @note Requires completed current layout. Different containing cells or
 *        a cell rectangle return UNSUPPORTED. A selection ending at byte zero
 *        excludes that last paragraph. Failure clears *out. */
tiku_rich_status_t tiku_richruler_read(const tiku_richview_t *view,
                                      tiku_rich_ruler_t *out);
/** @brief Clamp one proposed handle position without allocating or editing.
 *  @note Use the unmodified read result as source on every preview.
 *        Bounds retain at least one point of text width in each paragraph.
 *        Output may alias source; failure clears output. */
tiku_rich_status_t tiku_richruler_preview(const tiku_rich_ruler_t *source,
    tiku_rich_ruler_part_t part, double x, tiku_rich_ruler_t *out);
/** @brief Apply one handle position to the captured selection as one undo step.
 *  @note Rechecks identity, generation, selection, geometry and permissions.
 *        Preserves other indentation components, list and style references.
 *        Pass the original read result, not the preview; failures change
 *        neither document nor selection. No pointer capture is retained. */
tiku_rich_status_t tiku_richruler_apply(tiku_richview_t *view,
    const tiku_rich_ruler_t *source, tiku_rich_ruler_part_t part, double x);

#endif
