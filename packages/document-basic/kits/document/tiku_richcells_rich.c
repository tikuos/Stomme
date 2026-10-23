/*
 * TikuDesktop Document Kit.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_richcells_rich.c - atomic styled rectangular cell replacement.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richdoc_private.h"
#include <string.h>

/** @brief Paragraph bounds and identity of one direct table cell. */
typedef struct {
    size_t first, last;       /**< inclusive indices in the original root */
    uint64_t id;              /**< stable cell identity */
} cell_range_t;

/** @brief Collect direct cells and all their descendant paragraphs. */
static tiku_rich_status_t
ranges(const tiku_rd_root_t *root, const tiku_rich_cells_t *cells,
       cell_range_t *out)
{
    tiku_rd_cells_scan_t scan;
    const tiku_rich_block_t *node;
    size_t paragraph, count = 0;
    tiku_rich_status_t status = tiku_rd_cells_begin(root, cells, &scan);

    if (status != TIKU_RICH_OK) return status;
    while ((node = tiku_rd_cells_next(&scan, &paragraph))) {
        if (node->kind == TIKU_RICH_BLOCK_CELL && node->parent == cells->table) {
            out[count++] = (cell_range_t){SIZE_MAX, SIZE_MAX, node->id};
        } else if (paragraph != SIZE_MAX) {
            if (out[count - 1].first == SIZE_MAX)
                out[count - 1].first = paragraph;
            out[count - 1].last = paragraph;
        }
    }
    return TIKU_RICH_OK;
}

/** @brief Check final capacity after removing all replaced cell contents. */
static tiku_rich_status_t
capacity(const tiku_richdoc_t *doc, const tiku_rd_root_t *source,
         const cell_range_t *from, const cell_range_t *to, size_t count)
{
    size_t bytes = doc->root->bytes, runs = doc->root->runs;
    size_t paragraphs = doc->root->count;

    for (size_t i = 0; i < count; i++) {
        paragraphs -= to[i].last - to[i].first + 1;
        for (size_t j = to[i].first; j <= to[i].last; j++) {
            bytes -= doc->root->paragraphs[j]->value.length;
            runs -= doc->root->paragraphs[j]->value.run_count;
        }
    }
    for (size_t i = 0; i < count; i++) {
        size_t added = from[i].last - from[i].first + 1;

        if (added > doc->limits.paragraphs - paragraphs) return TIKU_RICH_LIMIT;
        paragraphs += added;
        for (size_t j = from[i].first; j <= from[i].last; j++) {
            const tiku_rich_paragraph_t *p = &source->paragraphs[j]->value;

            if (p->length > doc->limits.text_bytes - bytes ||
                p->run_count > doc->limits.runs - runs) return TIKU_RICH_LIMIT;
            bytes += p->length;
            runs += p->run_count;
        }
    }
    return TIKU_RICH_OK;
}

/** @brief Resolve a whole-cell range without including the following cell. */
static tiku_rich_pos_t
endpoint(const tiku_rd_root_t *root, cell_range_t cell, int end)
{
    const tiku_rich_paragraph_t *p =
        &root->paragraphs[end ? cell.last : cell.first]->value;

    return (tiku_rich_pos_t){root->body, p->id, end ? p->length : 0,
        end ? TIKU_RICH_AFTER : TIKU_RICH_BEFORE};
}

/** @brief Copy cells in reverse order, producing intermediate flat maps. */
static tiku_rich_status_t
replace_cells(tiku_richdoc_t *doc, tiku_rd_root_t *root,
    const tiku_richsnapshot_t *snapshot, const cell_range_t *from,
    const cell_range_t *to, size_t count, uint64_t *next, tiku_rd_map_t *maps)
{
    tiku_rich_status_t status;
    tiku_richfragment_t *fragment;
    tiku_rd_map_t ignored;
    tiku_rd_root_t flat = *snapshot->root;
    tiku_richsnapshot_t text = *snapshot;

    /* Paragraph transfer handles style/resource ownership; tree transfer
       restores descendants without exposing an intermediate flat document. */
    flat.blocks = NULL;
    text.root = &flat;

    for (size_t i = count; i-- > 0;) {
        tiku_rd_blocks_t *blocks = root->blocks;
        tiku_rich_pos_t first = endpoint(root, to[i], 0);
        tiku_rich_pos_t last = endpoint(root, to[i], 1);

        status = tiku_rd_fragment_create(&text,
            endpoint(snapshot->root, from[i], 0),
            endpoint(snapshot->root, from[i], 1), 1, &fragment);
        if (status != TIKU_RICH_OK) return status;
        root->blocks = NULL;
        status = tiku_rd_paste_into(doc, root, first, last,
            fragment, next, maps ? &maps[count - i - 1] : &ignored);
        root->blocks = blocks;
        tiku_richfragment_release(fragment);
        if (status != TIKU_RICH_OK) return status;
        status = tiku_rd_cell_tree(doc, root, snapshot->root,
            from[i].id, to[i].id, to[i].first, next);
        if (status != TIKU_RICH_OK) return status;
    }
    return TIKU_RICH_OK;
}

tiku_rich_status_t
tiku_rd_cells_fill(tiku_richdoc_t *doc, tiku_rd_root_t *root,
    const tiku_rich_cells_t *target, const tiku_richsnapshot_t *snapshot,
    const tiku_rich_cells_t *source, uint64_t *next)
{
    tiku_richdoc_t candidate = *doc;
    size_t count = (size_t)source->rows * source->columns;
    tiku_rich_status_t status;
    cell_range_t *from = tiku_rd_alloc(doc->memory,
        count * 2 * sizeof *from, &status);

    if (!from) return status;
    candidate.root = root;
    status = ranges(snapshot->root, source, from);
    if (status == TIKU_RICH_OK) status = ranges(root, target, from + count);
    if (status == TIKU_RICH_OK)
        status = capacity(&candidate, snapshot->root, from, from + count,
                            count);
    if (status == TIKU_RICH_OK)
        status = replace_cells(&candidate, root, snapshot, from, from + count,
                                count, next, NULL);
    tiku_rd_free(from);
    return status;
}

tiku_rich_status_t
tiku_richdoc_cells_paste(tiku_richdoc_t *doc, uint64_t generation,
    const tiku_rich_cells_t *target, const tiku_richsnapshot_t *snapshot,
    const tiku_rich_cells_t *source, tiku_rich_cells_t *out)
{
    tiku_rich_cells_t selected = target ? *target : (tiku_rich_cells_t){0};
    tiku_rich_cells_t copied = source ? *source : (tiku_rich_cells_t){0};
    tiku_rd_cells_scan_t scan;
    cell_range_t *from = NULL;
    tiku_rd_root_t *root = NULL;
    tiku_rd_map_t *maps = NULL;
    tiku_rd_history_t *entry;
    tiku_rich_status_t status;
    size_t count;
    uint64_t next;

    if (out) memset(out, 0, sizeof *out);
    if (!doc || !target || !snapshot || !source) return TIKU_RICH_INVALID;
    if (doc->notifying || doc->initiator) return TIKU_RICH_BUSY;
    if (doc->read_only) return TIKU_RICH_READ_ONLY;
    if (generation && generation != doc->generation) return TIKU_RICH_STALE;
    if (doc->generation == UINT64_MAX) return TIKU_RICH_LIMIT;
    status = tiku_rd_cells_begin(snapshot->root, &copied, &scan);
    if (status != TIKU_RICH_OK) return status;
    status = tiku_rd_cells_begin(doc->root, &selected, &scan);
    if (status != TIKU_RICH_OK) return status;
    if ((selected.rows != 1 || selected.columns != 1) &&
        (selected.rows != copied.rows || selected.columns != copied.columns))
        return TIKU_RICH_INVALID_RANGE;
    selected.rows = copied.rows;
    selected.columns = copied.columns;
    status = tiku_rd_cells_begin(doc->root, &selected, &scan);
    if (status != TIKU_RICH_OK) return status;
    count = (size_t)copied.rows * copied.columns;
    from = tiku_rd_alloc(doc->memory, count * 2 * sizeof *from, &status);
    if (!from) return status;
    status = ranges(snapshot->root, &copied, from);
    if (status == TIKU_RICH_OK)
        status = ranges(doc->root, &selected, from + count);
    if (status == TIKU_RICH_OK)
        status = capacity(doc, snapshot->root, from, from + count, count);
    if (status != TIKU_RICH_OK) goto done;
    root = tiku_rd_clone(doc, &status);
    if (!root) goto done;
    maps = tiku_rd_alloc(doc->memory, count * sizeof *maps, &status);
    if (!maps) goto done;
    next = doc->next_id;
    status = replace_cells(doc, root, snapshot, from, from + count,
                            count, &next, maps);
    if (status != TIKU_RICH_OK) goto done;
    entry = tiku_rd_history_new(doc, root, maps, count, &status);
    if (!entry) goto done;
    doc->next_id = next;
    tiku_rd_publish(doc, root, entry);
    root = NULL;
    maps = NULL;
    if (out) *out = selected;
done:
    tiku_rd_free(from);
    tiku_rd_free(maps);
    tiku_rd_root_drop(root);
    return status;
}
