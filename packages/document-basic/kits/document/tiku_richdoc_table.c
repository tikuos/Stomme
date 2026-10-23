/*
 * TikuDesktop Document Kit.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_richdoc_table.c - transactional rectangular table insertion/removal.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richdoc_private.h"
#include <string.h>

/** @brief Consume an identity from a previously checked reservation. */
static uint64_t
identity(uint64_t *next)
{
    uint64_t result = *next;

    *next = *next == UINT64_MAX ? 0 : *next + 1;
    return result;
}

/** @brief Materialize the paragraph-only body's implicit structure. */
static tiku_rich_status_t
materialize(tiku_richdoc_t *doc, tiku_rd_root_t *root)
{
    tiku_rich_status_t status = TIKU_RICH_OK;

    if (root->blocks) return status;
    root->blocks = tiku_rd_blocks_new(doc, root->count, &status);
    if (!root->blocks) return status;
    for (size_t i = 0; i < root->count; i++)
        root->blocks->nodes[i] = (tiku_rich_block_t){
            .kind = TIKU_RICH_BLOCK_PARAGRAPH,
            .id = root->paragraphs[i]->value.id, .parent = root->body};
    return status;
}

/** @brief Count a block's enclosing table/cell nodes, excluding the body. */
static size_t
depth(const tiku_rd_root_t *root, size_t at)
{
    size_t result = 0;
    uint64_t parent = root->blocks->nodes[at].parent;

    while (parent != root->body) {
        at = tiku_rd_block_find(root, parent);
        parent = root->blocks->nodes[at].parent;
        result++;
    }
    return result;
}

/** @brief Allocate empty paragraphs after an existing paragraph. */
static tiku_rich_status_t
new_paragraphs(tiku_richdoc_t *doc, tiku_rd_root_t *root, size_t after,
               size_t count, uint64_t *next, tiku_rd_paragraph_t ***out)
{
    tiku_rich_paragraph_t empty = {0};
    tiku_rich_status_t status = TIKU_RICH_OK;
    tiku_rd_paragraph_t **array;

    *out = NULL;
    array = tiku_rd_alloc(doc->memory,
        (root->count + count) * sizeof *array, &status);
    if (!array) return status;
    for (size_t i = 0; i < count; i++) {
        empty.id = identity(next);
        array[after + 1 + i] =
            tiku_rd_paragraph_new(doc, &empty, 0, 0, &status);
        if (!array[after + 1 + i]) {
            for (size_t j = 0; j < i; j++)
                tiku_rd_paragraph_drop(array[after + 1 + j]);
            tiku_rd_free(array);
            return status;
        }
    }
    memcpy(array, root->paragraphs, (after + 1) * sizeof *array);
    memcpy(array + after + 1 + count, root->paragraphs + after + 1,
           (root->count - after - 1) * sizeof *array);
    *out = array;
    return status;
}

/** @brief Insert an empty grid with an optional trailing editable paragraph. */
static tiku_rich_status_t
insert(tiku_richdoc_t *doc, tiku_rd_root_t *root,
       const tiku_rich_edit_t *edit, uint64_t *next, tiku_rd_map_t *map,
       int following)
{
    tiku_rich_status_t status = tiku_rich_table_validate(&edit->table);
    size_t after = tiku_rd_find(root, edit->paragraph), at, cells, added;
    tiku_rd_blocks_t *blocks;
    tiku_rd_paragraph_t **paragraphs;
    uint64_t table, cell, parent;

    if (status != TIKU_RICH_OK) return status;
    if (after == SIZE_MAX || edit->start || edit->end)
        return TIKU_RICH_INVALID_RANGE;
    cells = (size_t)edit->table.rows * edit->table.columns;
    added = cells * 2 + 1 + following;
    if (!*next || added - 1 > UINT64_MAX - *next ||
        cells + following > doc->limits.paragraphs - root->count ||
        root->count > SIZE_MAX / sizeof *paragraphs - cells - following)
        return TIKU_RICH_LIMIT;
    status = materialize(doc, root);
    if (status != TIKU_RICH_OK) return status;
    at = tiku_rd_block_find(root, edit->paragraph);
    if (depth(root, at) >= 16) return TIKU_RICH_LIMIT;
    parent = root->blocks->nodes[at].parent;
    blocks = tiku_rd_blocks_new(doc, root->blocks->count + added, &status);
    if (!blocks) return status;
    status = new_paragraphs(doc, root, after, cells + following,
                             next, &paragraphs);
    if (status != TIKU_RICH_OK) { tiku_rd_blocks_drop(blocks); return status; }
    memcpy(blocks->nodes, root->blocks->nodes,
           (at + 1) * sizeof *blocks->nodes);
    memcpy(blocks->nodes + at + added + 1, root->blocks->nodes + at + 1,
           (root->blocks->count - at - 1) * sizeof *blocks->nodes);
    table = identity(next);
    blocks->nodes[++at] = (tiku_rich_block_t){
        .kind = TIKU_RICH_BLOCK_TABLE, .id = table, .parent = parent,
        .table = edit->table};
    for (size_t i = 0; i < cells; i++) {
        cell = identity(next);
        blocks->nodes[++at] = (tiku_rich_block_t){
            .kind = TIKU_RICH_BLOCK_CELL, .id = cell, .parent = table};
        blocks->nodes[++at] = (tiku_rich_block_t){
            .kind = TIKU_RICH_BLOCK_PARAGRAPH, .parent = cell,
            .id = paragraphs[after + 1 + i]->value.id};
    }
    if (following)
        blocks->nodes[++at] = (tiku_rich_block_t){
            .kind = TIKU_RICH_BLOCK_PARAGRAPH, .parent = parent,
            .id = paragraphs[after + 1 + cells]->value.id};
    *map = (tiku_rd_map_t){.kind = TIKU_RICH_TABLE_INSERT, .flat = 1,
        .at = root->paragraphs[after]->value.length,
        .inserted = cells + following};
    for (size_t i = 0; i < after; i++)
        map->at += root->paragraphs[i]->value.length + 1;
    tiku_rd_free(root->paragraphs);
    tiku_rd_blocks_drop(root->blocks);
    root->paragraphs = paragraphs;
    root->count += cells + following;
    root->blocks = blocks;
    return TIKU_RICH_OK;
}

tiku_rich_status_t
tiku_rd_table_between(tiku_richdoc_t *doc, tiku_rd_root_t *root,
    const tiku_rich_edit_t *edit, uint64_t *next)
{
    tiku_rd_map_t map;

    return insert(doc, root, edit, next, &map, 0);
}

size_t
tiku_rd_subtree_end(const tiku_rd_root_t *root, size_t first)
{
    uint64_t stack[17];
    size_t count = 1, at = first + 1;
    const tiku_rich_block_t *node;

    stack[0] = root->blocks->nodes[first].id;
    for (; at < root->blocks->count; at++) {
        node = &root->blocks->nodes[at];
        while (count && stack[count - 1] != node->parent) count--;
        if (!count) break;
        if (node->kind != TIKU_RICH_BLOCK_PARAGRAPH)
            stack[count++] = node->id;
    }
    return at;
}

/** @brief Remove a complete table subtree without joining surviving text. */
static tiku_rich_status_t
remove_table(tiku_richdoc_t *doc, tiku_rd_root_t *root,
             const tiku_rich_edit_t *edit, uint64_t *next, tiku_rd_map_t *map)
{
    size_t at = tiku_rd_block_find(root, edit->object), end, begin = SIZE_MAX;
    size_t count = 0, bytes = 0, runs = 0;
    tiku_rd_blocks_t *blocks;
    tiku_rich_status_t status = TIKU_RICH_OK;
    tiku_rd_paragraph_t *empty = NULL;
    int replacement;

    if (!root->blocks || at == SIZE_MAX || edit->start || edit->end ||
        root->blocks->nodes[at].kind != TIKU_RICH_BLOCK_TABLE)
        return TIKU_RICH_INVALID_RANGE;
    end = tiku_rd_subtree_end(root, at);
    for (size_t i = at + 1; i < end; i++)
        if (root->blocks->nodes[i].kind == TIKU_RICH_BLOCK_PARAGRAPH) {
            if (!count) begin = tiku_rd_find(root, root->blocks->nodes[i].id);
            bytes += root->paragraphs[begin + count]->value.length;
            runs += root->paragraphs[begin + count++]->value.run_count;
        }
    replacement = count == root->count ||
        (at && root->blocks->nodes[at - 1].kind == TIKU_RICH_BLOCK_CELL &&
         (end == root->blocks->count || root->blocks->nodes[end].parent !=
          root->blocks->nodes[at].parent));
    if (replacement) {
        tiku_rich_paragraph_t value = {0};

        if (!*next) return TIKU_RICH_LIMIT;
        value.id = identity(next);
        empty = tiku_rd_paragraph_new(doc, &value, 0, 0, &status);
        if (!empty) return status;
    }
    blocks = tiku_rd_blocks_new(doc,
        root->blocks->count - (end - at) + replacement, &status);
    if (!blocks) { tiku_rd_paragraph_drop(empty); return status; }
    memcpy(blocks->nodes, root->blocks->nodes, at * sizeof *blocks->nodes);
    if (replacement) blocks->nodes[at] = (tiku_rich_block_t){
        .kind = TIKU_RICH_BLOCK_PARAGRAPH, .id = empty->value.id,
        .parent = root->blocks->nodes[at].parent};
    memcpy(blocks->nodes + at + replacement, root->blocks->nodes + end,
           (root->blocks->count - end) * sizeof *blocks->nodes);
    *map = (tiku_rd_map_t){.kind = TIKU_RICH_REPLACE, .flat = 1,
        .removed = bytes + count - replacement};
    for (size_t i = 0; i < begin; i++)
        map->at += root->paragraphs[i]->value.length + 1;
    if (begin && !replacement) map->at--;
    for (size_t i = 0; i < count; i++)
        tiku_rd_paragraph_drop(root->paragraphs[begin + i]);
    memmove(root->paragraphs + begin + replacement,
            root->paragraphs + begin + count,
            (root->count - begin - count) * sizeof *root->paragraphs);
    if (replacement) root->paragraphs[begin] = empty;
    root->count = root->count - count + replacement;
    root->bytes -= bytes;
    root->runs -= runs;
    tiku_rd_blocks_drop(root->blocks);
    root->blocks = blocks;
    return TIKU_RICH_OK;
}

tiku_rich_status_t
tiku_rd_table_edit(tiku_richdoc_t *doc, tiku_rd_root_t *root,
                   const tiku_rich_edit_t *edit, uint64_t *next,
                   tiku_rd_map_t *map)
{
    return edit->kind == TIKU_RICH_TABLE_INSERT ?
        insert(doc, root, edit, next, map, 1) :
        remove_table(doc, root, edit, next, map);
}
