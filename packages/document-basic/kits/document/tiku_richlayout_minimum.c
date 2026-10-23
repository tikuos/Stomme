/*
 * TikuDesktop Document Kit.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_richlayout_minimum.c - bounded table and embedded-picture width scan.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richlayout_private.h"
#include <math.h>

/** @brief Close one measured container and propagate its minimum width. */
static tiku_rich_status_t
close_width(tiku_richlayout_t *layout)
{
    size_t at = layout->min_stack[--layout->min_depth];
    const tiku_rich_block_t *node = &layout->source.root->blocks->nodes[at];
    double width = fmax(1, layout->minimums[at]);

    if (node->kind == TIKU_RICH_BLOCK_TABLE)
        width = fmax(node->table.width, width);
    if (width > 1000000) return TIKU_RICH_LIMIT;
    layout->minimums[at] = width;
    if (layout->min_depth) {
        at = layout->min_stack[layout->min_depth - 1];
        if (node->kind == TIKU_RICH_BLOCK_CELL) {
            const tiku_rich_table_t *table =
                &layout->source.root->blocks->nodes[at].table;
            unsigned column = layout->min_cells[layout->min_depth - 1]++ %
                              table->columns;

            width = (width + 2 * (table->padding + table->border)) /
                    tiku_rich_table_fraction(table, column);
        }
        layout->minimums[at] = fmax(layout->minimums[at], width);
    }
    return TIKU_RICH_OK;
}

tiku_rich_status_t
tiku_rl_minimum(tiku_richlayout_t *layout)
{
    const tiku_rd_blocks_t *blocks = layout->source.root->blocks;
    const tiku_rich_block_t *node;
    const tiku_rich_paragraph_t *paragraph;
    tiku_rich_status_t status;

    if (layout->min_node == blocks->count) {
        if (layout->min_depth) return close_width(layout);
        layout->phase = RL_PREPARE;
        return TIKU_RICH_OK;
    }
    node = &blocks->nodes[layout->min_node];
    if (layout->min_depth && node->parent !=
        blocks->nodes[layout->min_stack[layout->min_depth - 1]].id) {
        status = close_width(layout);
        return status;
    }
    if (node->kind != TIKU_RICH_BLOCK_PARAGRAPH) {
        if (layout->min_depth == 16) return TIKU_RICH_LIMIT;
        layout->min_cells[layout->min_depth] = 0;
        layout->min_stack[layout->min_depth++] = layout->min_node++;
        return TIKU_RICH_OK;
    }
    paragraph = &layout->source.root->paragraphs[layout->min_paragraph]->value;
    if (layout->min_run < paragraph->run_count) {
        const tiku_rich_image_t *image =
            paragraph->runs[layout->min_run++].image;

        if (image) layout->minimums[layout->min_node] =
            fmax(layout->minimums[layout->min_node], image->width);
        return TIKU_RICH_OK;
    }
    if (layout->min_depth) {
        size_t parent = layout->min_stack[layout->min_depth - 1];

        layout->minimums[parent] = fmax(layout->minimums[parent],
            fmax(1, layout->minimums[layout->min_node]));
    }
    layout->min_node++;
    layout->min_paragraph++;
    layout->min_run = 0;
    return TIKU_RICH_OK;
}
