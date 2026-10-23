/*
 * TikuDesktop Document Kit.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_richpages_private.h - incremental paragraph and table pagination.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_RICHPAGES_PRIVATE_H_
#define TIKU_RICHPAGES_PRIVATE_H_
#include "tiku_richpages.h"
#include "tiku_richlayout_private.h"

/** @brief One complete table row in the continuous source layout. */
typedef struct {
    size_t first, count;       /**< consecutive lines including nested cells */
    size_t grid;              /**< first direct cell in this row */
    double top, height;        /**< full outer row bounds */
} tiku_rp_row_t;

/** @brief Scratch state for one top-level table, reused by its pagination. */
typedef struct {
    tiku_rp_row_t rows[256];   /**< bounded row geometry */
    const tiku_rich_grid_box_t *box; /**< borrowed source table */
    tiku_rich_table_t props;   /**< source dimensions and header count */
    size_t grid, cells, check; /**< incremental grid and line checks */
    unsigned row;            /**< next row to place */
    unsigned phase;          /**< 0 inactive, 1 grids, 2 checks, 3 rows, 4 fragments */
    double header_height;     /**< space occupied by repeated leading rows */
} tiku_rp_table_t;

/** @brief Pagination state retaining its layout and charged page array. */
struct tiku_richpages {
    tiku_richlayout_t *layout; /**< owned source geometry */
    tiku_rich_page_setup_t setup; /**< normalized source paper */
    tiku_rich_page_t *pages;  /**< fixed, bounded output capacity */
    size_t count, limit;     /**< used pages and ceiling */
    size_t line, scan, paragraph; /**< placement and keep-scan cursors */
    size_t placed_paragraph;  /**< paragraph containing the next placed line */
    size_t group_end;         /**< exclusive last line in scanned keep group */
    tiku_rich_para_t props;   /**< cached paragraph keep properties */
    tiku_rich_status_t status; /**< sticky job state */
    unsigned warnings;       /**< broken oversized keep groups */
    int placing;             /**< place the scanned group before the next */
    tiku_rp_table_t *table;   /**< lazily allocated row scratch */
    struct tiku_rp_flow *flow; /**< lazily allocated recursive cell cursors */
    tiku_rich_page_piece_t *pieces; /**< ordered, charged fragment records */
    size_t npieces, cpieces; /**< fragment count and allocation capacity */
    int fresh_page;          /**< following content starts after a split row */
};

/** @brief Resolve a paragraph's explicit or inherited page break. */
int tiku_rp_break_before(const tiku_richlayout_t *layout, size_t paragraph);
/** @brief Start one nonempty page at the next source line. */
int tiku_rp_new_page(tiku_richpages_t *pages);
/** @brief Begin incremental table checks, charging scratch to the document. */
void tiku_rp_table_begin(tiku_richpages_t *pages,
                        const tiku_rich_grid_box_t *table);
/** @brief Check one grid/line or place one row group. */
void tiku_rp_table_step(tiku_richpages_t *pages);
/** @brief Start splitting the current outer row on dedicated continuation pages. */
void tiku_rp_flow_begin(tiku_richpages_t *pages);
/** @brief Advance one grid, line or bounded cell-frame operation. */
void tiku_rp_flow_step(tiku_richpages_t *pages);
/** @brief Release pending fragment cursors without releasing their layout. */
void tiku_rp_flow_release(tiku_richpages_t *pages);
/** @brief Append one charged paint piece, setting sticky failure on refusal. */
int tiku_rp_piece_add(tiku_richpages_t *pages, tiku_rich_page_piece_t piece);
#endif
