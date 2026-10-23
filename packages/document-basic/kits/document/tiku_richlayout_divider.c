/*
 * TikuDesktop Document Kit.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_richlayout_divider.c - column-divider hits and content-width bounds.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richlayout_private.h"
#include <math.h>
#include <string.h>

/** @brief Bound a divider by cell content and valid column weights. */
static void
bounds(const tiku_richlayout_t *layout, size_t at, double width,
       tiku_rich_divider_t *out)
{
    const tiku_rd_blocks_t *blocks = layout->source.root->blocks;
    const tiku_rich_table_t *table = &blocks->nodes[at].table;
    double left = 1, right = 1, total = 0, weight, low, high;
    size_t ordinal = 0;

    for (size_t i = at + 1; i < blocks->count; i++) {
        if (blocks->nodes[i].parent != out->table) continue;
        if (ordinal % table->columns == out->column)
            left = fmax(left, layout->minimums[i]);
        if (ordinal % table->columns == out->column + 1)
            right = fmax(right, layout->minimums[i]);
        ordinal++;
    }
    left += 2 * (table->padding + table->border);
    right += 2 * (table->padding + table->border);
    for (unsigned i = 0; i < table->columns; i++)
        total += table->weights[0] ? table->weights[i] : 1;
    weight = table->weights[0] ?
        table->weights[out->column] + table->weights[out->column + 1] : 2;
    low = out->line.x - width * tiku_rich_table_fraction(table, out->column);
    high = low + width * weight / total;
    out->minimum = low + fmax(left, width * fmax(0.001, weight - 1000) / total);
    out->maximum = high -
        fmax(right, width * fmax(0.001, weight - 1000) / total);
}

tiku_rich_status_t
tiku_richlayout_divider(const tiku_richlayout_t *layout,
    double x, double y, double tolerance, tiku_rich_divider_t *out)
{
    const tiku_rd_blocks_t *blocks;
    size_t grid = 0, selected = SIZE_MAX;
    double width = 0, distance = INFINITY;
    unsigned depth = 0;

    if (!out) return TIKU_RICH_INVALID;
    memset(out, 0, sizeof *out);
    if (!layout || !isfinite(x) || !isfinite(y) || !isfinite(tolerance) ||
        tolerance < 0 || tolerance > 32) return TIKU_RICH_INVALID;
    if (layout->status != TIKU_RICH_OK) return layout->status;
    blocks = layout->source.root->blocks;
    if (!blocks) return TIKU_RICH_NOT_FOUND;
    for (size_t i = 0; i < blocks->count; i++) {
        const tiku_rich_block_t *node = &blocks->nodes[i];
        const tiku_rich_grid_box_t *box;
        double edge;

        if (node->kind == TIKU_RICH_BLOCK_PARAGRAPH) continue;
        box = &layout->grids[grid++];
        if (node->kind != TIKU_RICH_BLOCK_TABLE ||
            y < box->box.y || y >= box->box.y + box->box.height) continue;
        edge = box->box.x;
        for (unsigned c = 0; c + 1 < node->table.columns; c++) {
            double delta;

            edge += box->box.width * tiku_rich_table_fraction(&node->table, c);
            delta = fabs(edge - x);
            if (delta > tolerance || (selected != SIZE_MAX &&
                (box->depth < depth ||
                 (box->depth == depth && delta >= distance)))) continue;
            *out = (tiku_rich_divider_t){.table = node->id, .column = c,
                .line = {edge, box->box.y, 0, box->box.height}};
            selected = i;
            depth = box->depth;
            distance = delta;
            width = box->box.width;
        }
    }
    if (selected == SIZE_MAX) return TIKU_RICH_NOT_FOUND;
    bounds(layout, selected, width, out);
    if (out->minimum + 0.001 >= out->maximum) {
        memset(out, 0, sizeof *out);
        return TIKU_RICH_NOT_FOUND;
    }
    return TIKU_RICH_OK;
}
