/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_richdoc_list.c - list membership and nested decimal numbering.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richdoc_private.h"

#include <string.h>

tiku_rich_status_t
tiku_rd_list(const tiku_rich_list_t *in, tiku_rich_list_t *out)
{
    memset(out, 0, sizeof *out);
    if (in->kind == TIKU_RICH_LIST_NONE)
        return TIKU_RICH_OK;
    if ((in->kind != TIKU_RICH_LIST_BULLET &&
         in->kind != TIKU_RICH_LIST_DECIMAL) || in->level > 8 ||
        (in->kind == TIKU_RICH_LIST_BULLET && in->restart))
        return TIKU_RICH_INVALID_STYLE;
    *out = *in;
    return TIKU_RICH_OK;
}

int
tiku_rd_list_equal(const tiku_rich_list_t *a, const tiku_rich_list_t *b)
{
    return a->kind == b->kind && a->level == b->level &&
           a->restart == b->restart;
}

tiku_rich_status_t
tiku_richsnapshot_list(const tiku_richsnapshot_t *snapshot, size_t paragraph,
    tiku_rich_list_t *list, uint64_t *ordinal)
{
    uint64_t numbers[9] = {0};
    tiku_rich_list_kind_t kinds[9] = {0};
    const tiku_rich_list_t *item;
    size_t i, j, begin = 0;

    if (list)
        memset(list, 0, sizeof *list);
    if (ordinal)
        *ordinal = 0;
    if (!snapshot || !list || !ordinal)
        return TIKU_RICH_INVALID;
    if (paragraph >= snapshot->root->count)
        return TIKU_RICH_INVALID_RANGE;
    if (snapshot->root->blocks) {
        const tiku_rich_block_t *nodes = snapshot->root->blocks->nodes;
        size_t at = tiku_rd_block_find(snapshot->root,
            snapshot->root->paragraphs[paragraph]->value.id);

        begin = paragraph;
        while (at && nodes[at - 1].kind == TIKU_RICH_BLOCK_PARAGRAPH &&
               nodes[at - 1].parent == nodes[at].parent) { at--; begin--; }
    }
    for (i = begin; i <= paragraph; i++) {
        item = &snapshot->root->paragraphs[i]->value.list;
        if (item->kind == TIKU_RICH_LIST_NONE) {
            memset(numbers, 0, sizeof numbers);
            memset(kinds, 0, sizeof kinds);
            continue;
        }
        for (j = item->level + 1; j < 9; j++) {
            kinds[j] = TIKU_RICH_LIST_NONE;
            numbers[j] = 0;
        }
        if (kinds[item->level] != item->kind)
            numbers[item->level] = 0;
        kinds[item->level] = item->kind;
        if (item->kind == TIKU_RICH_LIST_DECIMAL) {
            if (item->restart)
                numbers[item->level] = item->restart;
            else {
                if (numbers[item->level] == UINT64_MAX)
                    return TIKU_RICH_LIMIT;
                numbers[item->level]++;
            }
        }
    }
    *list = snapshot->root->paragraphs[paragraph]->value.list;
    if (list->kind == TIKU_RICH_LIST_DECIMAL)
        *ordinal = numbers[list->level];
    return TIKU_RICH_OK;
}
