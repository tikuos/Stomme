/*
 * TikuDesktop Document Kit.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_richdoc_grid_map.c - grid cell lookup and structural position mapping.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richdoc_private.h"

size_t
tiku_rd_grid_cell(const tiku_rd_root_t *root, uint64_t table,
                  uint64_t paragraph)
{
    size_t at, owner, ordinal = 0;

    if (!root->blocks) return SIZE_MAX;
    at = tiku_rd_block_find(root, paragraph);
    owner = tiku_rd_block_find(root, table);
    if (at == SIZE_MAX || owner == SIZE_MAX ||
        root->blocks->nodes[owner].kind != TIKU_RICH_BLOCK_TABLE)
        return SIZE_MAX;
    while (root->blocks->nodes[at].parent != table) {
        at = tiku_rd_block_find(root, root->blocks->nodes[at].parent);
        if (at == SIZE_MAX) return SIZE_MAX;
    }
    for (size_t i = owner + 1; i < at; i++)
        if (root->blocks->nodes[i].parent == table) ordinal++;
    return ordinal;
}

uint64_t
tiku_rd_grid_paragraph(const tiku_rd_root_t *root, uint64_t table,
                       size_t ordinal)
{
    size_t at = tiku_rd_block_find(root, table);
    const tiku_rich_table_t *grid;

    if (!root->blocks || at == SIZE_MAX ||
        root->blocks->nodes[at].kind != TIKU_RICH_BLOCK_TABLE) return 0;
    grid = &root->blocks->nodes[at].table;
    if (ordinal >= (size_t)grid->rows * grid->columns) return 0;
    for (at++; at < root->blocks->count; at++) {
        if (root->blocks->nodes[at].parent != table) continue;
        if (ordinal) { ordinal--; continue; }
        while (++at < root->blocks->count)
            if (root->blocks->nodes[at].kind == TIKU_RICH_BLOCK_PARAGRAPH)
                return root->blocks->nodes[at].id;
    }
    return 0;
}

tiku_rich_pos_t
tiku_rd_grid_position(tiku_rich_pos_t position, uint64_t table,
    const tiku_rd_root_t *from, const tiku_rd_root_t *to)
{
    size_t before, after, ordinal, row, column;
    const tiku_rich_table_t *a, *b;

    if (tiku_rd_find(to, position.paragraph) != SIZE_MAX) return position;
    before = tiku_rd_block_find(from, table);
    after = tiku_rd_block_find(to, table);
    ordinal = tiku_rd_grid_cell(from, table, position.paragraph);
    a = &from->blocks->nodes[before].table;
    b = &to->blocks->nodes[after].table;
    row = ordinal / a->columns;
    column = ordinal % a->columns;
    if (row >= b->rows) row = b->rows - 1;
    if (column >= b->columns) column = b->columns - 1;
    position.paragraph = tiku_rd_grid_paragraph(to, table,
                                               row * b->columns + column);
    position.byte = 0;
    return position;
}
