/*
 * TikuDesktop Document Kit.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_richpages_flow.h - incremental nested-row fragment state.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_RICHPAGES_FLOW_H_
#define TIKU_RICHPAGES_FLOW_H_
#include "tiku_richpages_private.h"

/** @brief Persistent cursor for a source cell or table; grid indices use +1. */
typedef struct {
    size_t parent, child, last, next;
    size_t cursor, nested;    /**< next line/row cell and next nested table */
    size_t cells, body, header_end; /**< table header collection */
    double source, header_height;
    const tiku_rich_table_t *props; /**< table properties, or owner's for a cell */
    int started;
} tiku_rp_cursor_t;

/** @brief One suspended row, cell or table placement within a page. */
typedef struct {
    unsigned kind, phase, column; /**< kind: 0 row, 1 cell, 2 table */
    size_t node, cursor;
    size_t piece_begin;       /**< discard an unplaced nested header group */
    size_t scan, group_end;   /**< incremental cell paragraph-keep scan */
    size_t boxes[64];          /**< this row's output cell-piece indices */
    double top, bottom, y, used;
    int done, progress;
} tiku_rp_frame_t;

/** @brief Source-indexed cursors and an explicit stack of at most eight tables. */
typedef struct tiku_rp_flow {
    tiku_rp_cursor_t *nodes;
    size_t build, parents[16]; /**< incremental source-grid indexing */
    tiku_rp_frame_t frames[24];
    unsigned depth;
    int done, progress;        /**< result of the most recently popped frame */
    double used;
} tiku_rp_flow_t;

/** @brief Push a row, cell or table frame within printable-area bounds. */
void tiku_rp_flow_push(tiku_richpages_t *pages, unsigned kind, size_t node,
                       double top, double bottom);
/** @brief Publish one frame's result to its parent. */
void tiku_rp_flow_pop(tiku_richpages_t *pages, int done,
                      double used, int progress);
/** @brief Append unchanged cell geometry and lines from one source interval. */
int tiku_rp_flow_range(tiku_richpages_t *pages, size_t first, size_t count,
                        double source, double target, unsigned depth);
/** @brief Advance one suspended row/cell/table frame. */
void tiku_rp_flow_frame(tiku_richpages_t *pages);
#endif
