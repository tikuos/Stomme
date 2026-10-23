/*
 * TikuDesktop Document Kit.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_richcells.c - rectangle validation, traversal and atomic cell edits.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richdoc_private.h"
#include <string.h>

tiku_rich_status_t
tiku_rd_cells_begin(const tiku_rd_root_t *root,
    const tiku_rich_cells_t *cells, tiku_rd_cells_scan_t *scan)
{
    size_t at;
    const tiku_rich_table_t *table;

    if (!root || !cells || !scan) return TIKU_RICH_INVALID;
    at = tiku_rd_block_find(root, cells->table);
    if (!root->blocks || at == SIZE_MAX ||
        root->blocks->nodes[at].kind != TIKU_RICH_BLOCK_TABLE)
        return TIKU_RICH_INVALID_RANGE;
    table = &root->blocks->nodes[at].table;
    if (!cells->rows || !cells->columns || cells->row >= table->rows ||
        cells->column >= table->columns ||
        cells->rows > table->rows - cells->row ||
        cells->columns > table->columns - cells->column)
        return TIKU_RICH_INVALID_RANGE;
    *scan = (tiku_rd_cells_scan_t){.root = root, .cells = *cells,
        .at = at + 1, .end = tiku_rd_subtree_end(root, at),
        .columns = table->columns};
    for (size_t i = 0; i < at; i++)
        scan->paragraph +=
            root->blocks->nodes[i].kind == TIKU_RICH_BLOCK_PARAGRAPH;
    return TIKU_RICH_OK;
}

const tiku_rich_block_t *
tiku_rd_cells_next(tiku_rd_cells_scan_t *scan, size_t *paragraph)
{
    while (scan->at < scan->end) {
        const tiku_rich_block_t *node =
            &scan->root->blocks->nodes[scan->at++];

        *paragraph = SIZE_MAX;
        if (node->parent == scan->cells.table) {
            unsigned row = (unsigned)(scan->ordinal / scan->columns);
            unsigned column = (unsigned)(scan->ordinal++ % scan->columns);

            scan->selected = row >= scan->cells.row &&
                row - scan->cells.row < scan->cells.rows &&
                column >= scan->cells.column &&
                column - scan->cells.column < scan->cells.columns;
        }
        if (node->kind == TIKU_RICH_BLOCK_PARAGRAPH)
            *paragraph = scan->paragraph++;
        if (scan->selected) return node;
    }
    return NULL;
}

tiku_rich_status_t
tiku_richsnapshot_cells(const tiku_richsnapshot_t *snapshot,
    uint64_t first, uint64_t last, tiku_rich_cells_t *out)
{
    const tiku_rd_root_t *root;
    size_t a, b, table;
    unsigned columns, r1, r2, c1, c2;

    if (!out) return TIKU_RICH_INVALID;
    memset(out, 0, sizeof *out);
    if (!snapshot) return TIKU_RICH_INVALID;
    root = snapshot->root;
    a = tiku_rd_block_find(root, first);
    b = tiku_rd_block_find(root, last);
    if (!root->blocks || a == SIZE_MAX || b == SIZE_MAX ||
        root->blocks->nodes[a].kind != TIKU_RICH_BLOCK_CELL ||
        root->blocks->nodes[b].kind != TIKU_RICH_BLOCK_CELL ||
        root->blocks->nodes[a].parent != root->blocks->nodes[b].parent)
        return TIKU_RICH_INVALID_RANGE;
    table = tiku_rd_block_find(root, root->blocks->nodes[a].parent);
    columns = root->blocks->nodes[table].table.columns;
    a = tiku_rd_grid_cell(root, root->blocks->nodes[table].id, first);
    b = tiku_rd_grid_cell(root, root->blocks->nodes[table].id, last);
    r1 = (unsigned)(a / columns); r2 = (unsigned)(b / columns);
    c1 = (unsigned)(a % columns); c2 = (unsigned)(b % columns);
    *out = (tiku_rich_cells_t){root->blocks->nodes[table].id,
        r1 < r2 ? r1 : r2, c1 < c2 ? c1 : c2,
        r1 < r2 ? r2 - r1 + 1 : r1 - r2 + 1,
        c1 < c2 ? c2 - c1 + 1 : c1 - c2 + 1};
    return TIKU_RICH_OK;
}

tiku_rich_status_t
tiku_richsnapshot_cell_at(const tiku_richsnapshot_t *snapshot,
    const tiku_rich_cells_t *cells, size_t index, tiku_rich_block_t *out)
{
    tiku_rd_cells_scan_t scan;
    const tiku_rich_block_t *node;
    tiku_rich_status_t status;
    size_t paragraph;

    if (!out) return TIKU_RICH_INVALID;
    memset(out, 0, sizeof *out);
    if (!snapshot) return TIKU_RICH_INVALID;
    status = tiku_rd_cells_begin(snapshot->root, cells, &scan);
    if (status != TIKU_RICH_OK) return status;
    while ((node = tiku_rd_cells_next(&scan, &paragraph))) {
        if (node->parent != cells->table) continue;
        if (index) { index--; continue; }
        *out = *node;
        return TIKU_RICH_OK;
    }
    return TIKU_RICH_INVALID_RANGE;
}

tiku_rich_status_t
tiku_rd_cells_set(tiku_richdoc_t *doc, tiku_rd_root_t *root,
                  const tiku_rich_edit_t *edit)
{
    tiku_rd_cells_scan_t scan;
    const tiku_rich_block_t *node;
    tiku_rd_blocks_t *blocks = NULL;
    size_t paragraph;
    tiku_rich_status_t status = tiku_rd_cells_begin(root, &edit->cells, &scan);

    if (status != TIKU_RICH_OK) return status;
    while ((node = tiku_rd_cells_next(&scan, &paragraph))) {
        if (node->parent != edit->cells.table ||
            node->background == edit->background) continue;
        if (!blocks) {
            blocks = tiku_rd_blocks_new(doc, root->blocks->count, &status);
            if (!blocks) return status;
            memcpy(blocks->nodes, root->blocks->nodes,
                   blocks->count * sizeof *blocks->nodes);
        }
        blocks->nodes[node - root->blocks->nodes].background = edit->background;
    }
    if (!blocks) return TIKU_RICH_NO_CHANGE;
    tiku_rd_blocks_drop(root->blocks);
    root->blocks = blocks;
    return TIKU_RICH_OK;
}

tiku_rich_status_t
tiku_richdoc_cells_clear(tiku_richdoc_t *doc, uint64_t generation,
                         const tiku_rich_cells_t *cells)
{
    tiku_rd_cells_scan_t scan;
    const tiku_rich_block_t *node;
    tiku_rich_edit_t *edits;
    tiku_rich_transaction_t tx;
    size_t count = 0, paragraph;
    tiku_rich_status_t status;

    if (!doc) return TIKU_RICH_INVALID;
    if (doc->notifying || doc->initiator) return TIKU_RICH_BUSY;
    if (doc->read_only) return TIKU_RICH_READ_ONLY;
    if (generation && generation != doc->generation) return TIKU_RICH_STALE;
    status = tiku_rd_cells_begin(doc->root, cells, &scan);
    if (status != TIKU_RICH_OK) return status;
    while ((node = tiku_rd_cells_next(&scan, &paragraph)))
        if (node->kind == TIKU_RICH_BLOCK_PARAGRAPH &&
            doc->root->paragraphs[paragraph]->value.length) count++;
    if (!count) return TIKU_RICH_NO_CHANGE;
    if (count > 1024) return TIKU_RICH_LIMIT;
    edits = tiku_rd_alloc(doc->memory, count * sizeof *edits, &status);
    if (!edits) return status;
    tx = (tiku_rich_transaction_t){generation, edits, count};
    tiku_rd_cells_begin(doc->root, cells, &scan);
    count = 0;
    while ((node = tiku_rd_cells_next(&scan, &paragraph))) {
        if (node->kind != TIKU_RICH_BLOCK_PARAGRAPH ||
            !doc->root->paragraphs[paragraph]->value.length) continue;
        edits[count++] = (tiku_rich_edit_t){.kind = TIKU_RICH_REPLACE,
            .paragraph = node->id,
            .end = doc->root->paragraphs[paragraph]->value.length};
    }
    status = tiku_richdoc_apply(doc, &tx);
    tiku_rd_free(edits);
    return status;
}
