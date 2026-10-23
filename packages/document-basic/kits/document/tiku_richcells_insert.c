/*
 * TikuDesktop Document Kit.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_richcells_insert.c - atomic rich table insertion at a text caret.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richdoc_private.h"
#include <string.h>

/** @brief Check mutation guards and a body/cell UTF-8 caret boundary. */
static tiku_rich_status_t
validate(tiku_richdoc_t *doc, uint64_t generation, tiku_rich_pos_t at,
    const tiku_richsnapshot_t *snapshot, const tiku_rich_cells_t *source)
{
    tiku_rd_cells_scan_t scan;
    tiku_richsnapshot_t current = {0};
    tiku_rich_status_t status;

    if (!doc || !snapshot || !source) return TIKU_RICH_INVALID;
    if (doc->notifying || doc->initiator) return TIKU_RICH_BUSY;
    if (doc->read_only) return TIKU_RICH_READ_ONLY;
    if (generation && generation != doc->generation) return TIKU_RICH_STALE;
    if (doc->generation == UINT64_MAX) return TIKU_RICH_LIMIT;
    current.root = doc->root;
    status = tiku_richsnapshot_position(&current, at);
    if (status != TIKU_RICH_OK) return status;
    return tiku_rd_cells_begin(snapshot->root, source, &scan);
}

/** @brief Split the destination and place an empty grid between its halves. */
static tiku_rich_status_t
prepare(tiku_richdoc_t *doc, tiku_rd_root_t *root, tiku_rich_pos_t at,
    tiku_rich_cells_t *cells, const tiku_richsnapshot_t *snapshot,
    const tiku_rich_cells_t *copied, uint64_t *next)
{
    tiku_rich_edit_t edit = {.kind = TIKU_RICH_SPLIT,
        .paragraph = at.paragraph, .start = at.byte, .end = at.byte};
    tiku_rd_map_t split;
    tiku_rich_status_t status = tiku_rd_structure(doc, root, &edit, next,
                                                  &split);
    size_t index = tiku_rd_block_find(snapshot->root, copied->table);
    tiku_rich_table_t original = snapshot->root->blocks->nodes[index].table;
    double fraction = 0;

    if (status == TIKU_RICH_OK) status = tiku_rd_blocks_map(doc, root, &split);
    if (status != TIKU_RICH_OK) return status;
    edit = (tiku_rich_edit_t){.kind = TIKU_RICH_TABLE_INSERT,
        .paragraph = at.paragraph, .table = original};
    edit.table.rows = copied->rows;
    edit.table.columns = copied->columns;
    memset(edit.table.weights, 0, sizeof edit.table.weights);
    for (unsigned i = 0; i < copied->columns; i++) {
        fraction += tiku_rich_table_fraction(&original, copied->column + i);
        edit.table.weights[i] = original.weights[copied->column + i];
    }
    if (original.width) {
        edit.table.width = (float)(original.width * fraction);
        if (edit.table.width < 1) edit.table.width = 1;
    }
    edit.table.header_rows = original.header_rows > copied->row ?
        original.header_rows - copied->row : 0;
    if (edit.table.header_rows > copied->rows)
        edit.table.header_rows = copied->rows;
    status = tiku_rd_table_between(doc, root, &edit, next);
    if (status == TIKU_RICH_OK)
        cells->table = root->blocks->nodes[
            tiku_rd_block_find(root, at.paragraph) + 1].id;
    return status;
}

tiku_rich_status_t
tiku_richdoc_cells_insert(tiku_richdoc_t *doc, uint64_t generation,
    tiku_rich_pos_t at, const tiku_richsnapshot_t *snapshot,
    const tiku_rich_cells_t *source, tiku_rich_cells_t *out)
{
    tiku_rich_cells_t copied = source ? *source : (tiku_rich_cells_t){0};
    tiku_rich_cells_t cells = {0, 0, 0, copied.rows, copied.columns};
    tiku_rd_root_t *root;
    tiku_rd_map_t *map = NULL;
    tiku_rd_history_t *entry;
    uint64_t next;
    tiku_rich_status_t status;

    if (out) memset(out, 0, sizeof *out);
    status = validate(doc, generation, at, snapshot, source ? &copied : NULL);
    if (status != TIKU_RICH_OK) return status;
    root = tiku_rd_clone(doc, &status);
    if (!root) return status;
    next = doc->next_id;
    status = prepare(doc, root, at, &cells, snapshot, &copied, &next);
    if (status == TIKU_RICH_OK)
        status = tiku_rd_cells_fill(doc, root, &cells, snapshot, &copied,
                                     &next);
    if (status != TIKU_RICH_OK) goto fail;
    map = tiku_rd_alloc(doc->memory, sizeof *map, &status);
    if (!map) goto fail;
    *map = (tiku_rd_map_t){.kind = TIKU_RICH_REPLACE, .flat = 1, .at = at.byte,
        .inserted = root->bytes - doc->root->bytes +
                    root->count - doc->root->count};
    for (size_t i = 0; doc->root->paragraphs[i]->value.id != at.paragraph; i++)
        map->at += doc->root->paragraphs[i]->value.length + 1;
    entry = tiku_rd_history_new(doc, root, map, 1, &status);
    if (!entry) goto fail;
    doc->next_id = next;
    tiku_rd_publish(doc, root, entry);
    if (out) *out = cells;
    return TIKU_RICH_OK;
fail:
    tiku_rd_free(map);
    tiku_rd_root_drop(root);
    return status;
}
