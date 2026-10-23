/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_richdoc_range.c - atomic cross-paragraph selection and plain-text paste.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richdoc_private.h"

#include <string.h>

/** @brief Validated endpoints and normalized paste storage. */
typedef struct {
    size_t first, last;        /**< paragraph orders */
    tiku_rich_pos_t start, end; /**< validated source positions */
    char *text;               /**< owned normalized text */
    size_t length;            /**< normalized bytes */
    size_t breaks;            /**< paragraph delimiters in text */
} range_t;

/** @brief Check mutation permission, generation and ordered live endpoints. */
static tiku_rich_status_t
check_range(tiku_richdoc_t *doc, uint64_t generation,
              tiku_rich_pos_t first, tiku_rich_pos_t last, range_t *range)
{
    tiku_richsnapshot_t snapshot = {0};
    tiku_rich_status_t status;
    int order;

    if (!doc)
        return TIKU_RICH_INVALID;
    if (doc->notifying)
        return TIKU_RICH_BUSY;
    if (doc->read_only)
        return TIKU_RICH_READ_ONLY;
    if (generation && generation != doc->generation)
        return TIKU_RICH_STALE;
    if (doc->generation == UINT64_MAX)
        return TIKU_RICH_LIMIT;
    snapshot.root = doc->root;
    status = tiku_richsnapshot_compare(&snapshot, first, last, &order);
    if (status != TIKU_RICH_OK)
        return status;
    if (order > 0)
        return TIKU_RICH_INVALID_RANGE;
    range->first = tiku_rd_find(doc->root, first.paragraph);
    range->last = tiku_rd_find(doc->root, last.paragraph);
    range->start = first;
    range->end = last;
    return TIKU_RICH_OK;
}

/** @brief Normalize line endings in bounded storage and validate UTF-8. */
static tiku_rich_status_t
plain_text(tiku_richdoc_t *doc, const char *text, size_t length,
             range_t *range)
{
    tiku_rich_status_t status = TIKU_RICH_OK;
    size_t i;
    char ch;

    if (!text && length)
        return TIKU_RICH_INVALID;
    if (length == SIZE_MAX || length >= doc->limits.memory_bytes)
        return TIKU_RICH_LIMIT;
    range->text = tiku_rd_alloc(doc->memory, length + 1, &status);
    if (!range->text)
        return status;
    for (i = 0; i < length; i++) {
        ch = text[i];
        if (ch == '\r') {
            if (i + 1 < length && text[i + 1] == '\n')
                i++;
            ch = '\n';
        }
        range->text[range->length++] = ch;
        if (ch == '\n' && ++range->breaks > 511)
            return TIKU_RICH_LIMIT;
    }
    return tiku_rd_utf8(range->text, range->length, 1);
}

/** @brief Compose joins, replacement and splits using unpublished fresh IDs. */
static void
compose(tiku_richdoc_t *doc, const range_t *range, tiku_rich_edit_t *ops)
{
    size_t i, n = 0, start = 0, end = range->end.byte, at = range->start.byte;
    uint64_t current = range->start.paragraph, next = doc->next_id;

    for (i = range->first; i < range->last; i++) {
        end += doc->root->paragraphs[i]->value.length;
        ops[n++] = (tiku_rich_edit_t){.kind = TIKU_RICH_JOIN,
            .paragraph = current};
    }
    for (i = 0; i <= range->length; i++) {
        if (i != range->length && range->text[i] != '\n')
            continue;
        ops[n++] = (tiku_rich_edit_t){.kind = TIKU_RICH_REPLACE,
            .paragraph = current, .start = at, .end = end,
            .text = range->text + start, .length = i - start};
        at += i - start;
        if (i != range->length) {
            ops[n++] = (tiku_rich_edit_t){.kind = TIKU_RICH_SPLIT,
                .paragraph = current, .start = at, .end = at};
            current = next;
            next = next == UINT64_MAX ? 0 : next + 1;
            at = end = 0;
        }
        start = i + 1;
    }
}

/** @brief Describe the whole replacement's position mapping. */
static tiku_rd_map_t
range_map(tiku_richdoc_t *doc, const range_t *range)
{
    tiku_rd_map_t map = {.kind = TIKU_RICH_REPLACE, .flat = 1,
        .at = range->start.byte, .inserted = range->length};
    size_t i, end = range->end.byte;

    for (i = 0; i < range->first; i++)
        map.at += doc->root->paragraphs[i]->value.length + 1;
    for (i = 0; i < range->last; i++)
        end += doc->root->paragraphs[i]->value.length + 1;
    map.removed = end - map.at;
    return map;
}

tiku_rich_status_t
tiku_richdoc_replace_range(tiku_richdoc_t *doc, uint64_t generation,
    tiku_rich_pos_t first, tiku_rich_pos_t last,
    const char *text, size_t length)
{
    range_t range = {0};
    tiku_rich_transaction_t tx = {0};
    tiku_rich_edit_t *ops = NULL;
    tiku_rd_map_t map;
    tiku_rich_status_t status;

    status = check_range(doc, generation, first, last, &range);
    if (status != TIKU_RICH_OK)
        return status;
    if (range.last - range.first >= 1024)
        return TIKU_RICH_LIMIT;
    status = plain_text(doc, text, length, &range);
    if (status != TIKU_RICH_OK)
        goto done;
    tx.count = range.last - range.first + 1 + 2 * range.breaks;
    if (tx.count > 1024 || (range.breaks && (!doc->next_id ||
        range.breaks - 1 > UINT64_MAX - doc->next_id))) {
        status = TIKU_RICH_LIMIT;
        goto done;
    }
    ops = tiku_rd_alloc(doc->memory, tx.count * sizeof *ops, &status);
    if (!ops)
        goto done;
    compose(doc, &range, ops);
    tx.edits = ops;
    tx.generation = doc->generation;
    map = range_map(doc, &range);
    status = tiku_rd_apply(doc, &tx, &map);
done:
    tiku_rd_free(ops);
    tiku_rd_free(range.text);
    return status;
}

tiku_rich_status_t
tiku_richdoc_format_range(tiku_richdoc_t *doc, uint64_t generation,
    tiku_rich_pos_t first, tiku_rich_pos_t last, const tiku_rich_chars_t *chars)
{
    range_t range = {0};
    tiku_rich_transaction_t tx = {0};
    tiku_rich_edit_t *ops;
    tiku_rich_chars_t checked;
    tiku_rich_status_t status;
    size_t i;

    if (!chars)
        return TIKU_RICH_INVALID;
    status = check_range(doc, generation, first, last, &range);
    if (status != TIKU_RICH_OK)
        return status;
    status = tiku_rd_chars(chars, &checked);
    if (status != TIKU_RICH_OK)
        return status;
    if (range.first == range.last && first.byte == last.byte)
        return TIKU_RICH_NO_CHANGE;
    tx.count = range.last - range.first + 1;
    if (tx.count > 1024)
        return TIKU_RICH_LIMIT;
    ops = tiku_rd_alloc(doc->memory, tx.count * sizeof *ops, &status);
    if (!ops)
        return status;
    for (i = 0; i < tx.count; i++) {
        const tiku_rich_paragraph_t *p =
            &doc->root->paragraphs[range.first + i]->value;

        ops[i] = (tiku_rich_edit_t){.kind = TIKU_RICH_FORMAT,
            .paragraph = p->id, .start = i ? 0 : first.byte,
            .end = i + 1 == tx.count ? last.byte : p->length,
            .chars = checked};
    }
    tx.edits = ops;
    tx.generation = doc->generation;
    status = tiku_richdoc_apply(doc, &tx);
    tiku_rd_free(ops);
    return status;
}
