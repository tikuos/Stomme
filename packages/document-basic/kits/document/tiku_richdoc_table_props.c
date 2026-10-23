/*
 * TikuDesktop Document Kit.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_richdoc_table_props.c - immutable table geometry and column weights.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richdoc_private.h"
#include <string.h>

tiku_rich_status_t
tiku_richsnapshot_cell(const tiku_richsnapshot_t *snapshot,
    uint64_t paragraph, tiku_rich_block_t *out)
{
    size_t at;

    if (!out) return TIKU_RICH_INVALID;
    memset(out, 0, sizeof *out);
    if (!snapshot) return TIKU_RICH_INVALID;
    if (tiku_rd_find(snapshot->root, paragraph) == SIZE_MAX)
        return TIKU_RICH_INVALID_RANGE;
    at = tiku_rd_block_find(snapshot->root,
        tiku_rd_container(snapshot->root, paragraph));
    if (!snapshot->root->blocks || at == SIZE_MAX ||
        snapshot->root->blocks->nodes[at].kind != TIKU_RICH_BLOCK_CELL)
        return TIKU_RICH_INVALID_RANGE;
    *out = snapshot->root->blocks->nodes[at];
    return TIKU_RICH_OK;
}

tiku_rich_status_t
tiku_rd_cell_set(tiku_richdoc_t *doc, tiku_rd_root_t *root,
                 const tiku_rich_edit_t *edit)
{
    size_t at = tiku_rd_block_find(root, edit->object);
    tiku_rd_blocks_t *blocks;
    tiku_rich_status_t status = TIKU_RICH_OK;

    if (!root->blocks || at == SIZE_MAX || edit->start || edit->end ||
        root->blocks->nodes[at].kind != TIKU_RICH_BLOCK_CELL)
        return TIKU_RICH_INVALID_RANGE;
    if (root->blocks->nodes[at].background == edit->background)
        return TIKU_RICH_NO_CHANGE;
    blocks = tiku_rd_blocks_new(doc, root->blocks->count, &status);
    if (!blocks) return status;
    memcpy(blocks->nodes, root->blocks->nodes,
           blocks->count * sizeof *blocks->nodes);
    blocks->nodes[at].background = edit->background;
    tiku_rd_blocks_drop(root->blocks);
    root->blocks = blocks;
    return TIKU_RICH_OK;
}

tiku_rich_status_t
tiku_richsnapshot_table(const tiku_richsnapshot_t *snapshot,
    uint64_t paragraph, tiku_rich_block_t *table, unsigned *column)
{
    uint64_t id;
    size_t at, cell;

    if (table) memset(table, 0, sizeof *table);
    if (column) *column = 0;
    if (!snapshot || !table || !column) return TIKU_RICH_INVALID;
    if (tiku_rd_find(snapshot->root, paragraph) == SIZE_MAX)
        return TIKU_RICH_INVALID_RANGE;
    id = tiku_rd_table_for(snapshot->root, paragraph);
    if (!id) return TIKU_RICH_INVALID_RANGE;
    at = tiku_rd_block_find(snapshot->root, id);
    cell = tiku_rd_grid_cell(snapshot->root, id, paragraph);
    if (cell == SIZE_MAX) return TIKU_RICH_INVALID_RANGE;
    *table = snapshot->root->blocks->nodes[at];
    *column = (unsigned)(cell % table->table.columns);
    return TIKU_RICH_OK;
}

tiku_rich_status_t
tiku_rd_table_set(tiku_richdoc_t *doc, tiku_rd_root_t *root,
                  const tiku_rich_edit_t *edit)
{
    size_t at = tiku_rd_block_find(root, edit->object);
    const tiku_rich_table_t *old;
    tiku_rd_blocks_t *blocks;
    tiku_rich_status_t status = tiku_rich_table_validate(&edit->table);
    int same;

    if (status != TIKU_RICH_OK) return status;
    if (!root->blocks || at == SIZE_MAX || edit->start || edit->end ||
        root->blocks->nodes[at].kind != TIKU_RICH_BLOCK_TABLE)
        return TIKU_RICH_INVALID_RANGE;
    old = &root->blocks->nodes[at].table;
    if (old->rows != edit->table.rows || old->columns != edit->table.columns)
        return TIKU_RICH_INVALID_RANGE;
    same = old->width == edit->table.width &&
        old->padding == edit->table.padding && old->border == edit->table.border
        && old->color == edit->table.color &&
        old->header_rows == edit->table.header_rows;
    for (unsigned i = 0; i < old->columns; i++)
        same &= old->weights[i] == edit->table.weights[i];
    if (same) return TIKU_RICH_NO_CHANGE;
    blocks = tiku_rd_blocks_new(doc, root->blocks->count, &status);
    if (!blocks) return status;
    memcpy(blocks->nodes, root->blocks->nodes,
           blocks->count * sizeof *blocks->nodes);
    blocks->nodes[at].table = edit->table;
    tiku_rd_blocks_drop(root->blocks);
    root->blocks = blocks;
    return TIKU_RICH_OK;
}

void
tiku_rd_table_columns(tiku_rich_table_t *table, unsigned at, int insert)
{
    unsigned size = table->columns;
    double total = 0;

    if (insert) {
        for (unsigned i = 0; i < size; i++) total += table->weights[i];
        for (unsigned i = size; i > at; i--)
            table->weights[i] = table->weights[i - 1];
        table->weights[at] = (float)(total / size);
        table->columns++;
    } else {
        for (unsigned i = at; i + 1 < size; i++)
            table->weights[i] = table->weights[i + 1];
        table->weights[--table->columns] = 0;
    }
}
