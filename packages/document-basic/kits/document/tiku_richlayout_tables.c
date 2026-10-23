/*
 * TikuDesktop Document Kit.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_richlayout_tables.c - nested proportional-column table geometry.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richlayout_private.h"
#include <math.h>
#include <string.h>

/** @brief Restore the current block owner's horizontal content interval. */
static void
interval(tiku_richlayout_t *layout)
{
    tiku_rl_container_t *cell, *table;
    double inset;

    if (!layout->depth) {
        layout->content_x = 0;
        layout->content_width = layout->options.width;
        return;
    }
    cell = &layout->containers[layout->depth - 1];
    if (cell->node.kind != TIKU_RICH_BLOCK_CELL) return;
    table = &layout->containers[layout->depth - 2];
    inset = table->node.table.padding + table->node.table.border;
    layout->content_x = cell->x + inset;
    layout->content_width = cell->width - 2 * inset;
}

/** @brief Finalize a cell or table and propagate its measured height. */
static void
close_container(tiku_richlayout_t *layout)
{
    tiku_rl_container_t *current = &layout->containers[--layout->depth];
    tiku_rich_grid_box_t *box = &layout->grids[current->grid];
    tiku_rl_container_t *table;
    double inset;

    box->line_count = layout->nlines - box->line;
    if (current->node.kind == TIKU_RICH_BLOCK_TABLE) {
        layout->y = current->row_bottom;
        box->box.height = layout->y - box->box.y;
        memset(layout->numbers, 0, sizeof layout->numbers);
        memset(layout->kinds, 0, sizeof layout->kinds);
    } else {
        table = &layout->containers[layout->depth - 1];
        inset = table->node.table.padding + table->node.table.border;
        table->row_bottom = fmax(table->row_bottom, layout->y + inset);
        if (!(table->cells % table->node.table.columns)) {
            for (size_t i = 0; i < table->node.table.columns; i++)
                layout->grids[table->row_boxes[i]].box.height =
                    table->row_bottom - table->row_top;
            table->row_top = table->row_bottom;
        }
        layout->y = table->row_bottom;
    }
    interval(layout);
}

/** @brief Enter a table or cell and establish its first paragraph interval. */
static tiku_rich_status_t
open_container(tiku_richlayout_t *layout, const tiku_rich_block_t *node)
{
    tiku_rl_container_t *current, *table;
    tiku_rich_grid_box_t *box;
    size_t column;

    if (layout->depth == 16) return TIKU_RICH_LIMIT;
    if (!layout->containers) {
        layout->containers = tiku_rl_alloc(layout,
            16 * sizeof *layout->containers);
        if (!layout->containers) return layout->status;
    }
    if (!tiku_rl_grow(layout, (void **)&layout->grids, &layout->cgrids,
        layout->ngrids + 1, sizeof *layout->grids)) return layout->status;
    current = &layout->containers[layout->depth];
    memset(current, 0, sizeof *current);
    current->node = *node;
    current->grid = layout->ngrids++;
    box = &layout->grids[current->grid];
    *box = (tiku_rich_grid_box_t){.id = node->id, .kind = node->kind,
        .depth = (unsigned)layout->depth, .line = layout->nlines};
    if (node->kind == TIKU_RICH_BLOCK_TABLE) {
        current->x = layout->content_x;
        current->width = node->table.width ? node->table.width :
                         layout->content_width;
        current->width = fmax(current->width,
                              layout->minimums[layout->node - 1]);
        if (current->width > layout->content_width)
            layout->warnings |= TIKU_RICH_LAYOUT_OVERFLOW;
        current->row_top = current->row_bottom = layout->y;
    } else {
        table = &layout->containers[layout->depth - 1];
        column = table->cells++ % table->node.table.columns;
        table->row_boxes[column] = current->grid;
        current->width = table->width *
            tiku_rich_table_fraction(&table->node.table, (unsigned)column);
        current->x = table->x;
        for (unsigned i = 0; i < column; i++)
            current->x += table->width *
                tiku_rich_table_fraction(&table->node.table, i);
        layout->y = table->row_top;
        box->border = table->node.table.border;
        box->color = table->node.table.color;
        box->background = node->background;
    }
    box->box = (tiku_rich_rect_t){current->x, layout->y, current->width, 0};
    layout->depth++;
    interval(layout);
    if (node->kind == TIKU_RICH_BLOCK_CELL) {
        table = &layout->containers[layout->depth - 2];
        layout->y += table->node.table.padding + table->node.table.border;
        memset(layout->numbers, 0, sizeof layout->numbers);
        memset(layout->kinds, 0, sizeof layout->kinds);
    }
    return TIKU_RICH_OK;
}

tiku_rich_status_t
tiku_rl_containers(tiku_richlayout_t *layout, int finish)
{
    const tiku_rd_blocks_t *blocks = layout->source.root->blocks;
    const tiku_rich_block_t *node;
    tiku_rich_status_t status;

    if (!blocks) return TIKU_RICH_OK;
    if (finish) {
        while (layout->depth) close_container(layout);
        return TIKU_RICH_OK;
    }
    while (layout->node < blocks->count) {
        node = &blocks->nodes[layout->node++];
        while (layout->depth && node->parent !=
            layout->containers[layout->depth - 1].node.id)
            close_container(layout);
        if (node->kind == TIKU_RICH_BLOCK_PARAGRAPH)
            return node->id == layout->source.root->paragraphs[
                layout->paragraph]->value.id ? TIKU_RICH_OK :
                                              TIKU_RICH_INVALID;
        status = open_container(layout, node);
        if (status != TIKU_RICH_OK) return status;
    }
    return TIKU_RICH_INVALID;
}

size_t
tiku_richlayout_grids(const tiku_richlayout_t *layout)
{
    return layout && layout->status == TIKU_RICH_OK ? layout->ngrids : 0;
}

tiku_rich_status_t
tiku_richlayout_grid(const tiku_richlayout_t *layout, size_t index,
                     tiku_rich_grid_box_t *out)
{
    if (!out) return TIKU_RICH_INVALID;
    memset(out, 0, sizeof *out);
    if (!layout) return TIKU_RICH_INVALID;
    if (layout->status != TIKU_RICH_OK) return layout->status;
    if (index >= layout->ngrids) return TIKU_RICH_INVALID_RANGE;
    *out = layout->grids[index];
    return TIKU_RICH_OK;
}
