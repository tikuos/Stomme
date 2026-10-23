/*
 * TikuDesktop Document Kit.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_richpages.h - incremental single-section paragraph pagination.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_RICHPAGES_H_
#define TIKU_RICHPAGES_H_
#include "tiku_richlayout.h"

/** @brief Immutable snapshot pagination, advanced with bounded work. */
typedef struct tiku_richpages tiku_richpages_t;
/** @brief One page's source ranges and optional independently placed pieces. */
typedef struct {
    size_t first_line, line_count;
    double source_top;       /**< subtract from source y, then add top margin */
    size_t header_first, header_count; /**< repeated source lines, or zero */
    double header_top;       /**< separate source origin for repeated rows */
    size_t piece_count;      /**< tall-row pieces; line_count is zero on these pages */
    size_t first_piece;      /**< internal index; use richpages_piece to read */
} tiku_rich_page_t;
/** @brief A page piece containing lines, an unchanged range, or a cell box. */
typedef enum {
    TIKU_RICH_PAGE_TEXT,
    TIKU_RICH_PAGE_RANGE,     /**< lines and original nested cell geometry */
    TIKU_RICH_PAGE_CELL
} tiku_rich_page_piece_kind_t;
/** @brief One ordered paint operation in printable-area coordinates.
 *  @note TEXT paints lines without original cell boxes; RANGE includes boxes.
 *        Subtract source_top from line y and add the page's top margin.
 *        CELL uses its relocated box directly, adding left/top margins. */
typedef struct {
    tiku_rich_page_piece_kind_t kind;
    size_t first_line, line_count;
    double source_top;
    unsigned depth;          /**< RANGE omits cells with a smaller depth */
    tiku_rich_grid_box_t cell;
} tiku_rich_page_piece_t;
/** @brief A keep-lines/keep-next group exceeded the printable page height. */
#define TIKU_RICH_PAGE_KEEP_BROKEN 1u

/** @brief Create pagination using the snapshot's paper and margin properties.
 *  @note Layout width is replaced with the printable paper width. Other
 *        layout options, including the provider, are retained. page_limit=0
 *        selects 5000; larger limits are refused. Failure clears *out. */
tiku_rich_status_t tiku_richpages_create(const tiku_richsnapshot_t *snapshot,
    const tiku_richlayout_options_t *options, size_t page_limit,
    tiku_richpages_t **out);
/** @brief Advance layout, keep-group scanning or line placement.
 *  @note Keep groups fit whole when possible. Oversized groups split between
 *        lines with KEEP_BROKEN. Explicit paragraph page breaks override
 *        keep-next and do not create an initial blank page. A line taller
 *        than the printable height returns LIMIT. Small tables stay whole;
 *        larger tables split between rows, repeating configured headers.
 *        Tall rows split at cell line boundaries, including nested tables.
 *        Oversized lines/headers return LIMIT; cell page breaks UNSUPPORTED. */
tiku_rich_status_t tiku_richpages_step(tiku_richpages_t *pages, size_t work);
/** @brief Release a pending, completed or failed pagination and its layout. */
void tiku_richpages_release(tiku_richpages_t *pages);
/** @brief Completed page count; zero before success. */
size_t tiku_richpages_count(const tiku_richpages_t *pages);
/** @brief Sticky pagination status, or INVALID for NULL. */
tiku_rich_status_t tiku_richpages_status(const tiku_richpages_t *pages);
/** @brief Pagination-specific keep-rule diagnostics. */
unsigned tiku_richpages_warnings(const tiku_richpages_t *pages);
/** @brief Borrow the exact completed geometry used by these pages. */
const tiku_richlayout_t *tiku_richpages_layout(const tiku_richpages_t *pages);
/** @brief Copy a completed page; failure clears *out. */
tiku_rich_status_t tiku_richpages_page(const tiku_richpages_t *pages,
                                      size_t index, tiku_rich_page_t *out);
/** @brief Copy a completed page's piece by page-local index; errors clear out.
 *  @note Paint pieces in order after any repeated header. Paragraph lines and
 *        pictures are indivisible. Continuations repeat cell padding/borders.
 *        At most 1,000,000 pieces are retained, charged to the document. */
tiku_rich_status_t tiku_richpages_piece(const tiku_richpages_t *pages,
    size_t page, size_t index, tiku_rich_page_piece_t *out);
/** @brief Copy normalized paper properties, including during pagination. */
tiku_rich_status_t tiku_richpages_setup(const tiku_richpages_t *pages,
                                       tiku_rich_page_setup_t *out);
#endif
