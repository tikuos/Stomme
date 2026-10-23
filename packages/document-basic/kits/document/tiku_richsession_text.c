/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_richsession_text.c - allocation-free selected plain-text export.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richdoc_private.h"
#include <string.h>

tiku_rich_status_t
tiku_richsession_range(const tiku_richsession_t *session,
    tiku_rich_pos_t *first, tiku_rich_pos_t *last)
{
    tiku_richcmd_context_t context;

    if (!session || !first || !last || first == last) return TIKU_RICH_INVALID;
    context = tiku_rs_context(session);
    *first = context.first;
    *last = context.last;
    return TIKU_RICH_OK;
}

tiku_rich_status_t
tiku_richsession_text(const tiku_richsession_t *session,
    char *buffer, size_t capacity, size_t *length)
{
    tiku_richcmd_context_t context;
    const tiku_rd_root_t *root;
    size_t first, last, i, total = 0;

    if (!length) return TIKU_RICH_INVALID;
    *length = 0;
    if (!session || (!buffer && capacity)) return TIKU_RICH_INVALID;
    context = tiku_rs_context(session);
    root = session->doc->root;
    first = tiku_rd_find(root, context.first.paragraph);
    last = tiku_rd_find(root, context.last.paragraph);
    for (i = first; i <= last; i++) {
        const tiku_rich_paragraph_t *p = &root->paragraphs[i]->value;
        size_t from = i == first ? context.first.byte : 0;
        size_t to = i == last ? context.last.byte : p->length;

        if (to - from > SIZE_MAX - total - (i != first))
            return TIKU_RICH_LIMIT;
        total += to - from + (i != first);
    }
    *length = total;
    if (!buffer && !capacity) return TIKU_RICH_OK;
    if (capacity <= total) return TIKU_RICH_LIMIT;
    total = 0;
    for (i = first; i <= last; i++) {
        const tiku_rich_paragraph_t *p = &root->paragraphs[i]->value;
        size_t from = i == first ? context.first.byte : 0;
        size_t to = i == last ? context.last.byte : p->length;

        if (i != first) buffer[total++] = '\n';
        memcpy(buffer + total, p->text + from, to - from);
        total += to - from;
    }
    buffer[total] = '\0';
    return TIKU_RICH_OK;
}
