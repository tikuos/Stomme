/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_richdoc_transaction.c - atomic publication of paragraph edit groups.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richdoc_private.h"

/** @brief Apply one edit to an unpublished root, sharing unchanged nodes. */
static tiku_rich_status_t
apply_one(tiku_richdoc_t *doc, tiku_rd_root_t *root,
            const tiku_rich_edit_t *edit, uint64_t *next_id,
            tiku_rd_map_t *map)
{
    tiku_rich_status_t status;
    tiku_rd_paragraph_t *node, *old;
    size_t index;

    if (edit->kind == TIKU_RICH_PAGE_SETUP) {
        tiku_rich_page_setup_t page;

        status = tiku_rich_page_setup(&edit->page, &page);
        if (status != TIKU_RICH_OK) return status;
        if (page.width == root->page.width &&
            page.height == root->page.height &&
            page.top == root->page.top && page.right == root->page.right &&
            page.bottom == root->page.bottom && page.left == root->page.left)
            return TIKU_RICH_NO_CHANGE;
        root->page = page;
        return TIKU_RICH_OK;
    }
    if (edit->kind == TIKU_RICH_TABLE_INSERT ||
        edit->kind == TIKU_RICH_TABLE_REMOVE)
        return tiku_rd_table_edit(doc, root, edit, next_id, map);
    if (edit->kind == TIKU_RICH_TABLE_GRID)
        return tiku_rd_grid_edit(doc, root, edit, next_id, map);
    if (edit->kind == TIKU_RICH_TABLE_SET)
        return tiku_rd_table_set(doc, root, edit);
    if (edit->kind == TIKU_RICH_CELL_SET)
        return tiku_rd_cell_set(doc, root, edit);
    if (edit->kind == TIKU_RICH_CELLS_SET)
        return tiku_rd_cells_set(doc, root, edit);
    if (edit->kind == TIKU_RICH_SPLIT || edit->kind == TIKU_RICH_JOIN) {
        status = tiku_rd_structure(doc, root, edit, next_id, map);
        return status == TIKU_RICH_OK ?
               tiku_rd_blocks_map(doc, root, map) : status;
    }
    if (edit->kind == TIKU_RICH_STYLE_DEFINE ||
        edit->kind == TIKU_RICH_STYLE_REMOVE)
        return tiku_rd_style_edit(doc, root, edit, next_id);
    if (edit->kind == TIKU_RICH_PARAGRAPH_FORMAT ||
        edit->kind == TIKU_RICH_PARAGRAPH_STYLE_SET ||
        edit->kind == TIKU_RICH_LIST_SET)
        node = tiku_rd_paragraph_format(doc, root, edit, &status);
    else
        node = tiku_rd_edit(doc, root, edit, &status);
    if (!node)
        return status;
    index = tiku_rd_find(root, edit->paragraph);
    old = root->paragraphs[index];
    root->bytes = root->bytes - old->value.length + node->value.length;
    root->runs = root->runs - old->value.run_count + node->value.run_count;
    root->paragraphs[index] = node;
    *map = (tiku_rd_map_t){.kind = edit->kind, .first = edit->paragraph,
        .at = edit->start, .removed = edit->end - edit->start,
        .inserted = edit->length};
    if (edit->kind == TIKU_RICH_IMAGE_INSERT ||
        edit->kind == TIKU_RICH_IMAGE_SET) {
        map->kind = TIKU_RICH_REPLACE;
        map->inserted = 3;
    }
    tiku_rd_paragraph_drop(old);
    return TIKU_RICH_OK;
}

tiku_rich_status_t
tiku_richdoc_apply(tiku_richdoc_t *doc,
                    const tiku_rich_transaction_t *change)
{
    return tiku_rd_apply(doc, change, NULL);
}

tiku_rich_status_t
tiku_rd_apply(tiku_richdoc_t *doc, const tiku_rich_transaction_t *change,
               const tiku_rd_map_t *range)
{
    tiku_rd_root_t *root;
    tiku_rd_history_t *entry;
    tiku_rd_map_t *maps = NULL;
    tiku_rich_status_t status = TIKU_RICH_OK;
    size_t i, map_count = 0;
    uint64_t next_id;
    int changed = 0;

    if (!doc || !change || (change->count && !change->edits))
        return TIKU_RICH_INVALID;
    if (doc->notifying)
        return TIKU_RICH_BUSY;
    if (doc->read_only)
        return TIKU_RICH_READ_ONLY;
    if (change->generation && change->generation != doc->generation)
        return TIKU_RICH_STALE;
    if (change->count > 1024 || doc->generation == UINT64_MAX)
        return TIKU_RICH_LIMIT;
    if (!change->count)
        return TIKU_RICH_NO_CHANGE;
    for (i = 0; i < change->count; i++)
        if ((change->edits[i].kind == TIKU_RICH_TABLE_INSERT ||
             change->edits[i].kind == TIKU_RICH_TABLE_REMOVE ||
             change->edits[i].kind == TIKU_RICH_TABLE_GRID) &&
            (change->count != 1 || range)) return TIKU_RICH_INVALID;
    root = tiku_rd_clone(doc, &status);
    if (!root)
        return status;
    maps = tiku_rd_alloc(doc->memory, change->count * sizeof *maps, &status);
    if (!maps)
        goto fail;
    next_id = doc->next_id;
    for (i = 0; i < change->count; i++) {
        status = apply_one(doc, root, &change->edits[i], &next_id,
                            &maps[map_count]);
        if (status == TIKU_RICH_OK) {
            changed = 1;
            if (change->edits[i].kind == TIKU_RICH_REPLACE ||
                change->edits[i].kind == TIKU_RICH_SPLIT ||
                change->edits[i].kind == TIKU_RICH_JOIN ||
                change->edits[i].kind == TIKU_RICH_IMAGE_INSERT ||
                change->edits[i].kind == TIKU_RICH_TABLE_INSERT ||
                change->edits[i].kind == TIKU_RICH_TABLE_REMOVE ||
                change->edits[i].kind == TIKU_RICH_TABLE_GRID)
                map_count++;
        } else if (status != TIKU_RICH_NO_CHANGE)
            goto fail;
    }
    if (!changed) {
        status = TIKU_RICH_NO_CHANGE;
        goto fail;
    }
    if (range) {
        maps[0] = *range;
        map_count = 1;
    }
    entry = tiku_rd_history_new(doc, root, maps, map_count, &status);
    if (!entry)
        goto fail;
    doc->next_id = next_id;
    tiku_rd_publish(doc, root, entry);
    return TIKU_RICH_OK;
fail:
    tiku_rd_free(maps);
    tiku_rd_root_drop(root);
    return status;
}
