/*
 * TikuDesktop Document Kit.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_richcells_text.c - quoted tab-separated export of cell text.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richdoc_private.h"
#include <string.h>

/** @brief Append one byte or measure it without touching output. */
static int
emit(char *buffer, size_t *length, char byte)
{
    if (*length == SIZE_MAX - 1) return 0;
    if (buffer) buffer[*length] = byte;
    (*length)++;
    return 1;
}

/** @brief Encode cells; fallback flattens nested text within its outer field. */
static tiku_rich_status_t
encode(tiku_rd_cells_scan_t scan, char *buffer, size_t *length, int pictures)
{
    const tiku_rich_block_t *node;
    size_t paragraph, fields = 0, paragraphs = 0;

    *length = 0;
    while ((node = tiku_rd_cells_next(&scan, &paragraph))) {
        if (node->kind == TIKU_RICH_BLOCK_TABLE) {
            if (!pictures) return TIKU_RICH_UNSUPPORTED;
            continue;
        }
        if (node->kind == TIKU_RICH_BLOCK_CELL && node->parent != scan.cells.table)
            continue;
        if (node->kind == TIKU_RICH_BLOCK_CELL) {
            if (fields && (!emit(buffer, length, '"') ||
                !emit(buffer, length, fields % scan.cells.columns ? '\t' :
                                                                         '\n')))
                return TIKU_RICH_LIMIT;
            if (!emit(buffer, length, '"')) return TIKU_RICH_LIMIT;
            fields++;
            paragraphs = 0;
        } else {
            const tiku_rich_paragraph_t *p =
                &scan.root->paragraphs[paragraph]->value;

            for (size_t i = 0; i < p->run_count; i++)
                if (p->runs[i].image && !pictures) return TIKU_RICH_UNSUPPORTED;
            if (paragraphs++ && !emit(buffer, length, '\n'))
                return TIKU_RICH_LIMIT;
            for (size_t i = 0; i < p->length; i++) {
                if (pictures && p->length - i >= 3 &&
                    !memcmp(p->text + i, "\xef\xbf\xbc", 3)) {
                    const char *placeholder = "[Picture]";

                    for (; *placeholder; placeholder++)
                        if (!emit(buffer, length, *placeholder))
                            return TIKU_RICH_LIMIT;
                    i += 2;
                    continue;
                }
                if (p->text[i] == '"' && !emit(buffer, length, '"'))
                    return TIKU_RICH_LIMIT;
                if (!emit(buffer, length, p->text[i])) return TIKU_RICH_LIMIT;
            }
        }
    }
    return emit(buffer, length, '"') ? TIKU_RICH_OK : TIKU_RICH_LIMIT;
}

/** @brief Validate and export TSV, optionally substituting picture markers. */
static tiku_rich_status_t
export_text(const tiku_richsnapshot_t *snapshot,
    const tiku_rich_cells_t *cells, char *buffer, size_t capacity,
    size_t *length, int pictures)
{
    tiku_rd_cells_scan_t scan;
    tiku_rich_status_t status;

    if (!length) return TIKU_RICH_INVALID;
    *length = 0;
    if (!snapshot || (!buffer && capacity)) return TIKU_RICH_INVALID;
    status = tiku_rd_cells_begin(snapshot->root, cells, &scan);
    if (status != TIKU_RICH_OK) return status;
    status = encode(scan, NULL, length, pictures);
    if (status != TIKU_RICH_OK) { *length = 0; return status; }
    if (!buffer) return TIKU_RICH_OK;
    if (capacity <= *length) return TIKU_RICH_LIMIT;
    status = encode(scan, buffer, length, pictures);
    buffer[*length] = 0;
    return status;
}

tiku_rich_status_t
tiku_richsnapshot_cells_text(const tiku_richsnapshot_t *snapshot,
    const tiku_rich_cells_t *cells, char *buffer, size_t capacity,
    size_t *length)
{
    return export_text(snapshot, cells, buffer, capacity, length, 0);
}

tiku_rich_status_t
tiku_richsnapshot_cells_fallback(const tiku_richsnapshot_t *snapshot,
    const tiku_rich_cells_t *cells, char *buffer, size_t capacity,
    size_t *length)
{
    return export_text(snapshot, cells, buffer, capacity, length, 1);
}
