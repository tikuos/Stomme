/*
 * TikuDesktop Document Kit.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_richcmd_table.c - caret-relative grid operations and availability.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richcmd_private.h"

tiku_rich_status_t
tiku_rc_grid_edit(const tiku_rd_root_t *root, uint64_t paragraph,
    tiku_richcmd_kind_t kind, tiku_rich_edit_t *edit)
{
    uint64_t table = tiku_rd_table_for(root, paragraph);
    size_t ordinal = tiku_rd_grid_cell(root, table, paragraph), at;
    tiku_rich_table_t geometry;
    int column = kind >= TIKU_RICHCMD_COLUMN_BEFORE;
    int remove = kind == TIKU_RICHCMD_ROW_REMOVE ||
                 kind == TIKU_RICHCMD_COLUMN_REMOVE;
    unsigned size;

    if (!table || ordinal == SIZE_MAX) return TIKU_RICH_INVALID_RANGE;
    at = tiku_rd_block_find(root, table);
    geometry = root->blocks->nodes[at].table;
    size = column ? geometry.columns : geometry.rows;
    if (remove && size == 1) return TIKU_RICH_INVALID_RANGE;
    *edit = (tiku_rich_edit_t){.kind = TIKU_RICH_TABLE_GRID,
        .paragraph = paragraph, .object = table,
        .grid = {.operation = column ?
            (remove ? TIKU_RICH_COLUMN_REMOVE : TIKU_RICH_COLUMN_INSERT) :
            (remove ? TIKU_RICH_ROW_REMOVE : TIKU_RICH_ROW_INSERT),
            .index = column ? ordinal % geometry.columns :
                              ordinal / geometry.columns}};
    if (kind == TIKU_RICHCMD_ROW_AFTER || kind == TIKU_RICHCMD_COLUMN_AFTER)
        edit->grid.index++;
    if (column && !remove && size == 64) return TIKU_RICH_LIMIT;
    if (column) tiku_rd_table_columns(&geometry, edit->grid.index, !remove);
    else geometry.rows = size + (remove ? -1 : 1);
    return tiku_rich_table_validate(&geometry) == TIKU_RICH_OK ?
        TIKU_RICH_OK : TIKU_RICH_LIMIT;
}
