/*
 * TikuDesktop Document Kit.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_richcells_paste.c - atomic multi-cell text replacement with undo.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richcells_private.h"
#include <string.h>

/** @brief Counted edit sequence with unpublished paragraph identities. */
typedef struct {
    tiku_rich_edit_t *edits;  /**< NULL measures the required edit count */
    size_t count;           /**< at most 1024 transaction primitives */
    uint64_t next;          /**< next identity consumed by a split */
} paste_edits_t;

/** @brief Append or count a primitive, refusing the transaction ceiling. */
static tiku_rich_status_t
append(paste_edits_t *plan, tiku_rich_edit_t edit)
{
    if (plan->count == 1024) return TIKU_RICH_LIMIT;
    if (plan->edits) plan->edits[plan->count] = edit;
    plan->count++;
    return TIKU_RICH_OK;
}

/** @brief Join cleared paragraphs and fill one cell from decoded text. */
static tiku_rich_status_t
fill(paste_edits_t *plan, uint64_t id, size_t paragraphs,
     const char *text, size_t length)
{
    size_t start = 0;
    tiku_rich_status_t status;

    for (size_t i = 1; i < paragraphs; i++) {
        status = append(plan, (tiku_rich_edit_t){
            .kind = TIKU_RICH_JOIN, .paragraph = id});
        if (status != TIKU_RICH_OK) return status;
    }
    for (size_t i = 0; i <= length; i++) {
        if (i != length && text[i] != '\n') continue;
        if (i != start) {
            status = append(plan, (tiku_rich_edit_t){
                .kind = TIKU_RICH_REPLACE, .paragraph = id,
                .text = text + start, .length = i - start});
            if (status != TIKU_RICH_OK) return status;
        }
        if (i != length) {
            if (!plan->next) return TIKU_RICH_LIMIT;
            status = append(plan, (tiku_rich_edit_t){.kind = TIKU_RICH_SPLIT,
                .paragraph = id, .start = i - start, .end = i - start});
            if (status != TIKU_RICH_OK) return status;
            id = plan->next;
            plan->next = plan->next == UINT64_MAX ? 0 : plan->next + 1;
        }
        start = i + 1;
    }
    return TIKU_RICH_OK;
}

/** @brief Clear target text, rejecting cells with nested tables. */
static tiku_rich_status_t
clear_plan(const tiku_rd_root_t *root, const tiku_rich_cells_t *cells,
           paste_edits_t *plan)
{
    tiku_rd_cells_scan_t scan;
    const tiku_rich_block_t *node;
    size_t paragraph;
    tiku_rich_status_t status = tiku_rd_cells_begin(root, cells, &scan);

    if (status != TIKU_RICH_OK) return status;
    while ((node = tiku_rd_cells_next(&scan, &paragraph))) {
        if (node->kind == TIKU_RICH_BLOCK_TABLE) return TIKU_RICH_UNSUPPORTED;
        if (paragraph == SIZE_MAX || !root->paragraphs[paragraph]->value.length)
            continue;
        status = append(plan, (tiku_rich_edit_t){.kind = TIKU_RICH_REPLACE,
            .paragraph = node->id,
            .end = root->paragraphs[paragraph]->value.length});
        if (status != TIKU_RICH_OK) return status;
    }
    return TIKU_RICH_OK;
}

/** @brief Compose a complete replacement without publishing partial results. */
static tiku_rich_status_t
compose(const tiku_rd_root_t *root, const tiku_rich_cells_t *cells,
        const tiku_rd_tsv_t *text, paste_edits_t *plan)
{
    tiku_rd_cells_scan_t scan;
    const tiku_rich_block_t *node;
    size_t paragraph, paragraphs = 0, ordinal = 0;
    uint64_t first = 0;
    tiku_rich_status_t status = clear_plan(root, cells, plan);

    if (status != TIKU_RICH_OK) return status;
    tiku_rd_cells_begin(root, cells, &scan);
    do {
        node = tiku_rd_cells_next(&scan, &paragraph);
        if ((!node || node->parent == cells->table) && paragraphs) {
            const tiku_rd_textcell_t *field = &text->cells[ordinal++];

            status = fill(plan, first, paragraphs,
                text->text + field->start, field->length);
            if (status != TIKU_RICH_OK) return status;
            paragraphs = 0;
        }
        if (node && node->kind == TIKU_RICH_BLOCK_PARAGRAPH) {
            if (!paragraphs) first = node->id;
            paragraphs++;
        }
    } while (node);
    return TIKU_RICH_OK;
}

/** @brief Match a selection or expand a one-cell origin within its table. */
static tiku_rich_status_t
target(const tiku_rd_root_t *root, tiku_rich_cells_t *cells,
       const tiku_rd_tsv_t *text)
{
    tiku_rd_cells_scan_t scan;
    tiku_rich_status_t status = tiku_rd_cells_begin(root, cells, &scan);

    if (status != TIKU_RICH_OK) return status;
    if (cells->rows != 1 || cells->columns != 1) {
        if (cells->rows != text->rows || cells->columns != text->columns)
            return TIKU_RICH_INVALID_RANGE;
    }
    cells->rows = text->rows;
    cells->columns = text->columns;
    return tiku_rd_cells_begin(root, cells, &scan);
}

tiku_rich_status_t
tiku_richdoc_cells_paste_text(tiku_richdoc_t *doc, uint64_t generation,
    const tiku_rich_cells_t *cells, const char *text, size_t length,
    tiku_rich_cells_t *out)
{
    tiku_rich_cells_t selected = cells ? *cells : (tiku_rich_cells_t){0};
    tiku_rd_tsv_t decoded = {0};
    paste_edits_t plan = {0};
    tiku_rich_transaction_t tx;
    tiku_rich_status_t status;

    if (out) memset(out, 0, sizeof *out);
    if (!doc || !cells) return TIKU_RICH_INVALID;
    if (doc->notifying || doc->initiator) return TIKU_RICH_BUSY;
    if (doc->read_only) return TIKU_RICH_READ_ONLY;
    if (generation && generation != doc->generation) return TIKU_RICH_STALE;
    status = tiku_rd_tsv_parse(doc->memory, text, length, &decoded);
    if (status == TIKU_RICH_OK) status = target(doc->root, &selected, &decoded);
    plan.next = doc->next_id;
    if (status == TIKU_RICH_OK)
        status = compose(doc->root, &selected, &decoded, &plan);
    if (status != TIKU_RICH_OK) goto done;
    if (plan.count) {
        plan.edits = tiku_rd_alloc(doc->memory,
            plan.count * sizeof *plan.edits, &status);
        if (!plan.edits) goto done;
        plan.count = 0; plan.next = doc->next_id;
        status = compose(doc->root, &selected, &decoded, &plan);
        if (status != TIKU_RICH_OK) goto done;
    }
    tx = (tiku_rich_transaction_t){generation, plan.edits, plan.count};
    status = tiku_richdoc_apply(doc, &tx);
    if (out && (status == TIKU_RICH_OK || status == TIKU_RICH_NO_CHANGE))
        *out = selected;
done:
    tiku_rd_free(plan.edits);
    tiku_rd_tsv_drop(&decoded);
    return status;
}
