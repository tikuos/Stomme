/*
 * TikuDesktop Document Kit.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_richdoc_grid.c - atomic row and column edits preserving cell ownership.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richdoc_private.h"

/** @brief Source cell boundaries in the block and paragraph arrays. */
typedef struct {
    size_t block, paragraph;
} cell_t;

/** @brief Owned candidate arrays and borrowed source for one grid edit. */
typedef struct {
    tiku_richdoc_t *doc;
    tiku_rd_root_t *root;       /**< unpublished source root */
    tiku_rd_blocks_t *blocks;  /**< candidate hierarchy */
    tiku_rd_paragraph_t **paragraphs; /**< independently retained leaves */
    cell_t *cells;             /**< source boundaries, including sentinel */
    size_t nodes, count, bytes, runs; /**< populated candidate sizes */
    size_t table, end;         /**< source block boundaries */
    size_t added, removed_nodes, removed_paragraphs;
    int columns, inserting;
    tiku_rich_table_t geometry;
    const tiku_rich_edit_t *edit;
    uint64_t *next;            /**< unpublished identity cursor */
} grid_t;

/** @brief Validate the axis and retain the table's other geometry values. */
static tiku_rich_status_t
dimensions(grid_t *g)
{
    unsigned size;

    if (!g->root->blocks || g->table == SIZE_MAX ||
        g->root->blocks->nodes[g->table].kind != TIKU_RICH_BLOCK_TABLE ||
        g->edit->start || g->edit->end ||
        g->edit->grid.operation > TIKU_RICH_COLUMN_REMOVE ||
        g->edit->grid.operation < TIKU_RICH_ROW_INSERT)
        return TIKU_RICH_INVALID_RANGE;
    g->columns = g->edit->grid.operation >= TIKU_RICH_COLUMN_INSERT;
    g->inserting = g->edit->grid.operation == TIKU_RICH_ROW_INSERT ||
                   g->edit->grid.operation == TIKU_RICH_COLUMN_INSERT;
    g->geometry = g->root->blocks->nodes[g->table].table;
    size = g->columns ? g->geometry.columns : g->geometry.rows;
    if (g->edit->grid.index > size ||
        (!g->inserting && (size == 1 || g->edit->grid.index == size)))
        return TIKU_RICH_INVALID_RANGE;
    if (g->columns && g->inserting && size == 64) return TIKU_RICH_LIMIT;
    if (g->columns)
        tiku_rd_table_columns(&g->geometry, g->edit->grid.index, g->inserting);
    else {
        g->geometry.rows = size + (g->inserting ? 1 : -1);
        if (g->edit->grid.index < g->geometry.header_rows)
            g->geometry.header_rows += g->inserting ? 1 : -1;
    }
    if (tiku_rich_table_validate(&g->geometry) != TIKU_RICH_OK)
        return TIKU_RICH_LIMIT;
    return TIKU_RICH_OK;
}

/** @brief Index direct cells and count the nodes removed with one axis. */
static tiku_rich_status_t
index_cells(grid_t *g)
{
    const tiku_rich_table_t *old = &g->root->blocks->nodes[g->table].table;
    size_t count = (size_t)old->rows * old->columns, cell = 0, paragraph = 0;
    tiku_rich_status_t status = TIKU_RICH_OK;

    g->cells = tiku_rd_alloc(g->doc->memory,
                             (count + 1) * sizeof *g->cells, &status);
    if (!g->cells) return status;
    g->end = tiku_rd_subtree_end(g->root, g->table);
    for (size_t i = 0; i < g->end; i++) {
        const tiku_rich_block_t *node = &g->root->blocks->nodes[i];

        if (node->parent == g->edit->object)
            g->cells[cell++] = (cell_t){i, paragraph};
        if (node->kind == TIKU_RICH_BLOCK_PARAGRAPH) paragraph++;
    }
    g->cells[count] = (cell_t){g->end, paragraph};
    if (g->inserting) g->added = g->columns ? old->rows : old->columns;
    else for (size_t i = 0; i < count; i++) {
        size_t axis = g->columns ? i % old->columns : i / old->columns;

        if (axis != g->edit->grid.index) continue;
        g->removed_nodes += g->cells[i + 1].block - g->cells[i].block;
        g->removed_paragraphs +=
            g->cells[i + 1].paragraph - g->cells[i].paragraph;
    }
    return TIKU_RICH_OK;
}

/** @brief Allocate candidate arrays after count and identity checks. */
static tiku_rich_status_t
reserve(grid_t *g)
{
    size_t count = g->root->count - g->removed_paragraphs + g->added;
    tiku_rich_status_t status = TIKU_RICH_OK;

    if (count > g->doc->limits.paragraphs ||
        count > SIZE_MAX / sizeof *g->paragraphs ||
        (g->added && (!*g->next || 2 * g->added - 1 > UINT64_MAX - *g->next)))
        return TIKU_RICH_LIMIT;
    g->blocks = tiku_rd_blocks_new(g->doc, g->root->blocks->count -
        g->removed_nodes + 2 * g->added, &status);
    if (!g->blocks) return status;
    g->paragraphs = tiku_rd_alloc(g->doc->memory,
                                  count * sizeof *g->paragraphs, &status);
    return status;
}

/** @brief Copy a contiguous subtree range and retain its paragraph leaves. */
static void
copy(grid_t *g, size_t first, size_t end, size_t paragraph)
{
    for (size_t i = first; i < end; i++) {
        g->blocks->nodes[g->nodes++] = g->root->blocks->nodes[i];
        if (g->root->blocks->nodes[i].kind == TIKU_RICH_BLOCK_PARAGRAPH) {
            tiku_rd_paragraph_t *p = g->root->paragraphs[paragraph++];

            atomic_fetch_add_explicit(&p->refs, 1, memory_order_relaxed);
            g->paragraphs[g->count++] = p;
            g->bytes += p->value.length;
            g->runs += p->value.run_count;
        }
    }
}

/** @brief Allocate one empty paragraph and emit its new cell and leaf nodes. */
static tiku_rich_status_t
empty_cell(grid_t *g)
{
    tiku_rich_paragraph_t value = {.id = *g->next};
    tiku_rich_status_t status = TIKU_RICH_OK;
    tiku_rd_paragraph_t *p = tiku_rd_paragraph_new(g->doc, &value, 0, 0,
                                                  &status);
    uint64_t cell;

    if (!p) return status;
    cell = ++*g->next;
    *g->next = cell == UINT64_MAX ? 0 : cell + 1;
    g->paragraphs[g->count++] = p;
    g->blocks->nodes[g->nodes++] = (tiku_rich_block_t){
        .kind = TIKU_RICH_BLOCK_CELL, .id = cell, .parent = g->edit->object};
    g->blocks->nodes[g->nodes++] = (tiku_rich_block_t){
        .kind = TIKU_RICH_BLOCK_PARAGRAPH, .id = value.id, .parent = cell};
    return TIKU_RICH_OK;
}

/** @brief Assemble output cells, sharing retained subtrees outside the edit. */
static tiku_rich_status_t
assemble(grid_t *g)
{
    const tiku_rich_table_t *old = &g->root->blocks->nodes[g->table].table;
    size_t axis, source, row, column;
    tiku_rich_status_t status;

    copy(g, 0, g->table + 1, 0);
    g->blocks->nodes[g->table].table = g->geometry;
    for (size_t i = 0; i < (size_t)g->geometry.rows * g->geometry.columns;
         i++) {
        row = i / g->geometry.columns;
        column = i % g->geometry.columns;
        axis = g->columns ? column : row;
        if (g->inserting && axis == g->edit->grid.index) {
            status = empty_cell(g);
            if (status != TIKU_RICH_OK) return status;
            continue;
        }
        if (axis >= g->edit->grid.index) {
            if (g->columns) column += g->inserting ? -1 : 1;
            else row += g->inserting ? -1 : 1;
        }
        source = row * old->columns + column;
        copy(g, g->cells[source].block, g->cells[source + 1].block,
             g->cells[source].paragraph);
    }
    copy(g, g->end, g->root->blocks->count,
         g->cells[(size_t)old->rows * old->columns].paragraph);
    return TIKU_RICH_OK;
}

/** @brief Publish candidate arrays into the private transaction root. */
static void
adopt(grid_t *g, tiku_rd_map_t *map)
{
    const tiku_rich_table_t *old = &g->root->blocks->nodes[g->table].table;
    size_t origin = tiku_rd_grid_cell(g->root, g->edit->object,
                                      g->edit->paragraph);
    size_t row = origin == SIZE_MAX ? 0 : origin / old->columns;
    size_t column = origin == SIZE_MAX ? 0 : origin % old->columns;

    if (g->columns) column = g->edit->grid.index;
    else row = g->edit->grid.index;
    if (row >= g->geometry.rows) row = g->geometry.rows - 1;
    if (column >= g->geometry.columns) column = g->geometry.columns - 1;
    for (size_t i = 0; i < g->root->count; i++)
        tiku_rd_paragraph_drop(g->root->paragraphs[i]);
    tiku_rd_free(g->root->paragraphs);
    tiku_rd_blocks_drop(g->root->blocks);
    g->root->paragraphs = g->paragraphs;
    g->root->blocks = g->blocks;
    g->root->count = g->count;
    g->root->bytes = g->bytes;
    g->root->runs = g->runs;
    *map = (tiku_rd_map_t){.kind = TIKU_RICH_TABLE_GRID,
        .first = g->edit->object,
        .second = tiku_rd_grid_paragraph(g->root, g->edit->object,
                                         row * g->geometry.columns + column)};
    g->paragraphs = NULL;
    g->blocks = NULL;
    g->count = 0;
}

tiku_rich_status_t
tiku_rd_grid_edit(tiku_richdoc_t *doc, tiku_rd_root_t *root,
    const tiku_rich_edit_t *edit, uint64_t *next, tiku_rd_map_t *map)
{
    grid_t g = {.doc = doc, .root = root, .edit = edit, .next = next,
        .table = tiku_rd_block_find(root, edit->object)};
    tiku_rich_status_t status = dimensions(&g);

    if (status == TIKU_RICH_OK) status = index_cells(&g);
    if (status == TIKU_RICH_OK) status = reserve(&g);
    if (status == TIKU_RICH_OK) status = assemble(&g);
    if (status == TIKU_RICH_OK) adopt(&g, map);
    for (size_t i = 0; i < g.count; i++)
        tiku_rd_paragraph_drop(g.paragraphs[i]);
    tiku_rd_free(g.paragraphs);
    tiku_rd_blocks_drop(g.blocks);
    tiku_rd_free(g.cells);
    return status;
}
