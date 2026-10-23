/*
 * TikuDesktop Document Kit.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_richdoc_history_memory.c - unique storage census and undo retention.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richdoc_private.h"
#include <assert.h>

/** @brief Traverse children only when a paragraph enters or leaves the set. */
static void
paragraph_account(const tiku_rd_paragraph_t *p, int add, size_t *bytes)
{
    size_t i;

    if (!tiku_rd_account(p, add, bytes)) return;
    tiku_rd_account(p->value.text, add, bytes);
    tiku_rd_account(p->value.runs, add, bytes);
    for (i = 0; i < p->value.run_count; i++) {
        tiku_rd_link_account(p->value.runs[i].link, add, bytes);
        tiku_rd_image_account(p->value.runs[i].image, add, bytes);
    }
}

/** @brief Add/remove a root and its reachable allocation graph. */
static void
root_account(const tiku_rd_root_t *root, int add, size_t *bytes)
{
    size_t i;

    if (!tiku_rd_account(root, add, bytes)) return;
    tiku_rd_account(root->paragraphs, add, bytes);
    tiku_rd_account(root->styles, add, bytes);
    tiku_rd_account(root->blocks, add, bytes);
    tiku_rd_account(root->bookmarks, add, bytes);
    for (i = 0; i < root->count; i++)
        paragraph_account(root->paragraphs[i], add, bytes);
}

/** @brief Cache a finalized root's unique bytes while the census is empty. */
static void
measure(tiku_rd_root_t *root)
{
    size_t bytes = 0;

    if (root->footprint) return;
    root_account(root, 1, &bytes);
    root->footprint = bytes;
    root_account(root, 0, &bytes);
    assert(!bytes);
}

/** @brief Add/remove history metadata without its other-state root. */
static void
record_account(const tiku_rd_history_t *entry, int add, size_t *bytes)
{
    tiku_rd_account(entry, add, bytes);
    tiku_rd_account(entry->maps, add, bytes);
    tiku_rd_account(entry->bindings, add, bytes);
}

/** @brief Record the newest undo prefix whose peak reserve fits the limit. */
static void
retain_prefix(tiku_richdoc_t *doc, tiku_rd_history_t *entry,
    tiku_rd_history_t *first, size_t minimum, size_t *bytes)
{
    tiku_rd_history_t *at;

    entry->discard = first;
    for (at = first; at; at = at->next) {
        root_account(at->root, 1, bytes);
        record_account(at, 1, bytes);
        if (at->root->footprint < minimum) minimum = at->root->footprint;
        if (*bytes - minimum > doc->limits.history_bytes) {
            root_account(at->root, 0, bytes);
            record_account(at, 0, bytes);
            break;
        }
        entry->planned_bytes = *bytes - minimum;
        entry->planned_total = *bytes;
        entry->discard = at->next;
    }
    for (at = first; at != entry->discard; at = at->next) {
        root_account(at->root, 0, bytes);
        record_account(at, 0, bytes);
    }
}

tiku_rich_status_t
tiku_rd_history_plan(tiku_richdoc_t *doc, tiku_rd_root_t *after,
    tiku_rd_history_t *entry)
{
    tiku_rd_root_t *before = entry->replace_top ? doc->undo->root : doc->root;
    tiku_rd_history_t *first = entry->replace_top ? doc->undo->next : doc->undo;
    size_t bytes = 0, minimum;
    tiku_rich_status_t status = TIKU_RICH_OK;

    measure(before);
    measure(after);
    root_account(before, 1, &bytes);
    root_account(after, 1, &bytes);
    record_account(entry, 1, &bytes);
    minimum = before->footprint < after->footprint ?
              before->footprint : after->footprint;
    entry->planned_bytes = bytes - minimum;
    entry->planned_total = bytes;
    if (entry->planned_bytes > doc->limits.history_bytes)
        status = TIKU_RICH_LIMIT;
    else
        retain_prefix(doc, entry, first, minimum, &bytes);
    record_account(entry, 0, &bytes);
    root_account(after, 0, &bytes);
    root_account(before, 0, &bytes);
    assert(!bytes);
    return status;
}
