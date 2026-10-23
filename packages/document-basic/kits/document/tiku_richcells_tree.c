/*
 * TikuDesktop Document Kit.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_richcells_tree.c - nested cell hierarchy copying and identity remapping.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richdoc_private.h"
#include <string.h>

/** @brief Count the destination cell and its enclosing table/cell nodes. */
static size_t
depth(const tiku_rd_root_t *root, uint64_t cell)
{
    size_t count = 0;

    while (cell != root->body) {
        size_t i = tiku_rd_block_find(root, cell);

        if (i == SIZE_MAX || ++count > 16) return 17;
        cell = root->blocks->nodes[i].parent;
    }
    return count;
}

tiku_rich_status_t
tiku_rd_cell_tree(tiku_richdoc_t *doc, tiku_rd_root_t *root,
    const tiku_rd_root_t *source, uint64_t from, uint64_t to,
    size_t paragraph, uint64_t *next)
{
    tiku_rd_blocks_t *old = root->blocks, *blocks;
    size_t a = tiku_rd_block_find(source, from);
    size_t b = tiku_rd_block_find(root, to);
    size_t end = tiku_rd_subtree_end(source, a);
    size_t tail = tiku_rd_subtree_end(root, b);
    size_t count = end - a - 1, base = depth(root, to), level = 1;
    uint64_t original[17] = {from}, mapped[17] = {to};
    tiku_rich_status_t status = TIKU_RICH_OK;

    blocks = tiku_rd_blocks_new(doc, old->count - (tail - b - 1) + count,
                                &status);
    if (!blocks) return status;
    memcpy(blocks->nodes, old->nodes, (b + 1) * sizeof *blocks->nodes);
    blocks->nodes[b].background = source->blocks->nodes[a].background;
    for (size_t i = 0; i < count; i++) {
        tiku_rich_block_t node = source->blocks->nodes[a + 1 + i];
        uint64_t id = node.id;

        while (level && original[level - 1] != node.parent) level--;
        if (!level) { status = TIKU_RICH_INVALID; goto fail; }
        node.parent = mapped[level - 1];
        if (node.kind == TIKU_RICH_BLOCK_PARAGRAPH) {
            node.id = root->paragraphs[paragraph++]->value.id;
        } else {
            if (base + level > 16 || !*next) {
                status = TIKU_RICH_LIMIT;
                goto fail;
            }
            node.id = *next;
            *next = *next == UINT64_MAX ? 0 : *next + 1;
            original[level] = id;
            mapped[level++] = node.id;
        }
        blocks->nodes[b + 1 + i] = node;
    }
    memcpy(blocks->nodes + b + 1 + count, old->nodes + tail,
           (old->count - tail) * sizeof *blocks->nodes);
    root->blocks = blocks;
    tiku_rd_blocks_drop(old);
    return TIKU_RICH_OK;
fail:
    tiku_rd_blocks_drop(blocks);
    return status;
}
