/*
 * TikuDesktop Document Kit.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_richdoc_blocks.c - immutable paragraph, table and cell hierarchy.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richdoc_private.h"
#include <math.h>
#include <string.h>

tiku_rich_status_t
tiku_rich_table_validate(const tiku_rich_table_t *table)
{
    if (!table || !table->rows || table->rows > 256 ||
        table->header_rows > table->rows ||
        !table->columns || table->columns > 64 ||
        table->rows * table->columns > 4096 ||
        !isfinite(table->width) || table->width < 0 ||
        (table->width > 0 && table->width < 1) || table->width > 14400 ||
        !isfinite(table->padding) || table->padding < 0 ||
        table->padding > 72 || !isfinite(table->border) ||
        table->border < 0 || table->border > 12)
        return TIKU_RICH_INVALID;
    for (unsigned i = 0; i < 64; i++) {
        float weight = table->weights[i];

        if (!isfinite(weight) || (i >= table->columns || !table->weights[0] ?
            weight != 0 : weight < 0.001f || weight > 1000))
            return TIKU_RICH_INVALID;
    }
    return TIKU_RICH_OK;
}

double
tiku_rich_table_fraction(const tiku_rich_table_t *table, unsigned column)
{
    double total = 0;

    if (tiku_rich_table_validate(table) != TIKU_RICH_OK ||
        column >= table->columns) return 0;
    if (!table->weights[0]) return 1.0 / table->columns;
    for (unsigned i = 0; i < table->columns; i++) total += table->weights[i];
    return table->weights[column] / total;
}

/** @brief Check a completed table/cell's direct-child count. */
static int
children_valid(const tiku_rich_block_source_t *node, size_t children)
{
    return node->kind == TIKU_RICH_BLOCK_TABLE ?
        children == (size_t)node->table.rows * node->table.columns :
        children != 0;
}

tiku_rich_status_t
tiku_rd_blocks_validate(const tiku_rich_source_t *source)
{
    size_t stack[17], children[17] = {0}, depth = 0, paragraph = 0;
    const tiku_rich_block_source_t *node, *parent;
    const tiku_rich_table_t empty = {0};

    if (!source->block_count) return TIKU_RICH_OK;
    if (!source->blocks || !source->paragraph_count) return TIKU_RICH_INVALID;
    if (source->block_count > 65536) return TIKU_RICH_LIMIT;
    for (size_t i = 0; i < source->block_count; i++) {
        node = &source->blocks[i];
        if (node->parent > i) return TIKU_RICH_INVALID;
        if (node->kind != TIKU_RICH_BLOCK_CELL && node->background)
            return TIKU_RICH_INVALID;
        while (depth && stack[depth - 1] != node->parent) {
            if (!children_valid(&source->blocks[stack[depth - 1] - 1],
                                children[depth - 1]))
                return TIKU_RICH_INVALID;
            depth--;
        }
        if (node->parent && !depth) return TIKU_RICH_INVALID;
        parent = depth ? &source->blocks[stack[depth - 1] - 1] : NULL;
        if ((node->kind == TIKU_RICH_BLOCK_CELL) !=
            (parent && parent->kind == TIKU_RICH_BLOCK_TABLE))
            return TIKU_RICH_INVALID;
        if (depth) children[depth - 1]++;
        if (node->kind == TIKU_RICH_BLOCK_PARAGRAPH) {
            if (node->paragraph != paragraph++) return TIKU_RICH_INVALID;
        } else if (node->kind != TIKU_RICH_BLOCK_TABLE &&
                   node->kind != TIKU_RICH_BLOCK_CELL)
            return TIKU_RICH_UNSUPPORTED;
        else if (node->paragraph) return TIKU_RICH_INVALID;
        if (node->kind == TIKU_RICH_BLOCK_TABLE) {
            if (tiku_rich_table_validate(&node->table) != TIKU_RICH_OK)
                return TIKU_RICH_INVALID;
        } else if (node->table.rows != empty.rows || node->table.columns ||
                   node->table.width || node->table.padding ||
                   node->table.border || node->table.color ||
                   node->table.header_rows)
            return TIKU_RICH_INVALID;
        if (node->kind != TIKU_RICH_BLOCK_TABLE)
            for (unsigned j = 0; j < 64; j++)
                if (node->table.weights[j]) return TIKU_RICH_INVALID;
        if (node->kind != TIKU_RICH_BLOCK_PARAGRAPH) {
            if (depth == 16) return TIKU_RICH_LIMIT;
            stack[depth] = i + 1;
            children[depth++] = 0;
        }
    }
    while (depth) {
        depth--;
        if (!children_valid(&source->blocks[stack[depth] - 1], children[depth]))
            return TIKU_RICH_INVALID;
    }
    return paragraph == source->paragraph_count ?
           TIKU_RICH_OK : TIKU_RICH_INVALID;
}

tiku_rd_blocks_t *
tiku_rd_blocks_new(tiku_richdoc_t *doc, size_t count,
                    tiku_rich_status_t *status)
{
    tiku_rd_blocks_t *blocks;

    if (count > 65536 || count >
        (SIZE_MAX - sizeof *blocks) / sizeof *blocks->nodes) {
        *status = TIKU_RICH_LIMIT;
        return NULL;
    }
    blocks = tiku_rd_alloc(doc->memory,
        sizeof *blocks + count * sizeof *blocks->nodes, status);
    if (blocks) { atomic_init(&blocks->refs, 1); blocks->count = count; }
    return blocks;
}

void
tiku_rd_blocks_drop(tiku_rd_blocks_t *blocks)
{
    if (blocks && atomic_fetch_sub_explicit(&blocks->refs, 1,
        memory_order_acq_rel) == 1) tiku_rd_free(blocks);
}

tiku_rich_status_t
tiku_rd_blocks_build(tiku_richdoc_t *doc, tiku_rd_root_t *root,
                    const tiku_rich_source_t *source, uint64_t *next)
{
    tiku_rich_status_t status = TIKU_RICH_OK;
    size_t extra;
    const tiku_rich_block_source_t *from;
    tiku_rich_block_t *to;

    if (!source->block_count) return TIKU_RICH_OK;
    extra = source->block_count - source->paragraph_count;
    if (extra && (!*next || extra - 1 > UINT64_MAX - *next))
        return TIKU_RICH_LIMIT;
    root->blocks = tiku_rd_blocks_new(doc, source->block_count, &status);
    if (!root->blocks) return status;
    for (size_t i = 0; i < source->block_count; i++) {
        from = &source->blocks[i];
        to = &root->blocks->nodes[i];
        to->kind = from->kind;
        to->table = from->table;
        to->background = from->background;
        to->parent = from->parent ?
            root->blocks->nodes[from->parent - 1].id : root->body;
        if (from->kind == TIKU_RICH_BLOCK_PARAGRAPH)
            to->id = root->paragraphs[from->paragraph]->value.id;
        else { to->id = *next; *next = *next == UINT64_MAX ? 0 : *next + 1; }
    }
    return TIKU_RICH_OK;
}

size_t
tiku_rd_block_find(const tiku_rd_root_t *root, uint64_t id)
{
    if (!root->blocks) return tiku_rd_find(root, id);
    for (size_t i = 0; i < root->blocks->count; i++)
        if (root->blocks->nodes[i].id == id) return i;
    return SIZE_MAX;
}

uint64_t
tiku_rd_container(const tiku_rd_root_t *root, uint64_t id)
{
    size_t index = tiku_rd_block_find(root, id);

    if (index == SIZE_MAX) return 0;
    return root->blocks ? root->blocks->nodes[index].parent : root->body;
}

int
tiku_rd_same_container(const tiku_rd_root_t *root, size_t first, size_t last)
{
    size_t a, b;

    if (first > last || last >= root->count) return 0;
    if (!root->blocks) return 1;
    a = tiku_rd_block_find(root, root->paragraphs[first]->value.id);
    b = tiku_rd_block_find(root, root->paragraphs[last]->value.id);
    return a != SIZE_MAX && b != SIZE_MAX && b - a == last - first &&
        root->blocks->nodes[a].parent == root->blocks->nodes[b].parent;
}

uint64_t
tiku_rd_table_for(const tiku_rd_root_t *root, uint64_t paragraph)
{
    uint64_t cell = tiku_rd_container(root, paragraph);
    size_t index = tiku_rd_block_find(root, cell);

    return root->blocks && index != SIZE_MAX &&
           root->blocks->nodes[index].kind == TIKU_RICH_BLOCK_CELL ?
           root->blocks->nodes[index].parent : 0;
}

size_t
tiku_richsnapshot_blocks(const tiku_richsnapshot_t *snapshot)
{
    if (!snapshot) return 0;
    return snapshot->root->blocks ? snapshot->root->blocks->count :
                                   snapshot->root->count;
}

tiku_rich_status_t
tiku_richsnapshot_block(const tiku_richsnapshot_t *snapshot, size_t index,
                       tiku_rich_block_t *out)
{
    if (!out) return TIKU_RICH_INVALID;
    memset(out, 0, sizeof *out);
    if (!snapshot) return TIKU_RICH_INVALID;
    if (index >= tiku_richsnapshot_blocks(snapshot))
        return TIKU_RICH_INVALID_RANGE;
    if (snapshot->root->blocks) *out = snapshot->root->blocks->nodes[index];
    else *out = (tiku_rich_block_t){.kind = TIKU_RICH_BLOCK_PARAGRAPH,
        .id = snapshot->root->paragraphs[index]->value.id,
        .parent = snapshot->root->body};
    return TIKU_RICH_OK;
}

uint64_t
tiku_richsnapshot_container(const tiku_richsnapshot_t *snapshot,
                           uint64_t paragraph)
{
    return snapshot && tiku_rd_find(snapshot->root, paragraph) != SIZE_MAX ?
           tiku_rd_container(snapshot->root, paragraph) : 0;
}

tiku_rich_status_t
tiku_rd_blocks_map(tiku_richdoc_t *doc, tiku_rd_root_t *root,
                   const tiku_rd_map_t *map)
{
    tiku_rd_blocks_t *old = root->blocks, *next;
    tiku_rich_status_t status = TIKU_RICH_OK;
    size_t at;
    int split = map->kind == TIKU_RICH_SPLIT;

    if (!old) return TIKU_RICH_OK;
    at = tiku_rd_block_find(root, split ? map->first : map->second);
    if (at == SIZE_MAX) return TIKU_RICH_INVALID;
    if (split) at++;
    next = tiku_rd_blocks_new(doc, split ? old->count + 1 : old->count - 1,
                              &status);
    if (!next) return status;
    memcpy(next->nodes, old->nodes, at * sizeof *next->nodes);
    if (split) {
        next->nodes[at] = old->nodes[at - 1];
        next->nodes[at].id = map->second;
        memcpy(next->nodes + at + 1, old->nodes + at,
               (old->count - at) * sizeof *next->nodes);
    } else memcpy(next->nodes + at, old->nodes + at + 1,
                  (old->count - at - 1) * sizeof *next->nodes);
    root->blocks = next;
    tiku_rd_blocks_drop(old);
    return TIKU_RICH_OK;
}
