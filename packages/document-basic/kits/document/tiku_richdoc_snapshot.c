/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_richdoc_snapshot.c - immutable queries and position validation.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richdoc_private.h"

#include <string.h>

void
tiku_richsnapshot_release(tiku_richsnapshot_t *snapshot)
{
    if (snapshot) {
        tiku_rd_root_drop(snapshot->root);
        tiku_rd_free(snapshot);
    }
}

uint64_t
tiku_richsnapshot_generation(const tiku_richsnapshot_t *snapshot)
{
    return snapshot ? snapshot->generation : 0;
}

uint64_t
tiku_richsnapshot_body(const tiku_richsnapshot_t *snapshot)
{
    return snapshot ? snapshot->root->body : 0;
}

uint64_t
tiku_richsnapshot_state(const tiku_richsnapshot_t *snapshot)
{
    return snapshot ? snapshot->root->state : 0;
}

size_t
tiku_richsnapshot_paragraphs(const tiku_richsnapshot_t *snapshot)
{
    return snapshot ? snapshot->root->count : 0;
}

size_t
tiku_richsnapshot_styles(const tiku_richsnapshot_t *snapshot)
{
    return snapshot ? snapshot->root->nstyles : 0;
}

tiku_rich_status_t
tiku_richsnapshot_paragraph(const tiku_richsnapshot_t *snapshot, size_t index,
                             tiku_rich_paragraph_t *out)
{
    if (!out)
        return TIKU_RICH_INVALID;
    memset(out, 0, sizeof *out);
    if (!snapshot)
        return TIKU_RICH_INVALID;
    if (index >= snapshot->root->count)
        return TIKU_RICH_INVALID_RANGE;
    *out = snapshot->root->paragraphs[index]->value;
    return TIKU_RICH_OK;
}

tiku_rich_status_t
tiku_richsnapshot_style(const tiku_richsnapshot_t *snapshot, size_t index,
                         tiku_rich_style_t *out)
{
    if (!out)
        return TIKU_RICH_INVALID;
    memset(out, 0, sizeof *out);
    if (!snapshot)
        return TIKU_RICH_INVALID;
    if (index >= snapshot->root->nstyles)
        return TIKU_RICH_INVALID_RANGE;
    *out = snapshot->root->styles[index].value;
    return TIKU_RICH_OK;
}

tiku_rich_status_t
tiku_richsnapshot_position(const tiku_richsnapshot_t *snapshot,
                            tiku_rich_pos_t position)
{
    size_t index;
    const tiku_rich_paragraph_t *p;

    if (!snapshot)
        return TIKU_RICH_INVALID;
    if (position.body != snapshot->root->body ||
        (position.affinity != TIKU_RICH_BEFORE &&
         position.affinity != TIKU_RICH_AFTER))
        return TIKU_RICH_INVALID_RANGE;
    index = tiku_rd_find(snapshot->root, position.paragraph);
    if (index == SIZE_MAX)
        return TIKU_RICH_INVALID_RANGE;
    p = &snapshot->root->paragraphs[index]->value;
    return tiku_rd_boundary(p->text, p->length, position.byte)
           ? TIKU_RICH_OK : TIKU_RICH_INVALID_RANGE;
}

tiku_rich_status_t
tiku_richsnapshot_compare(const tiku_richsnapshot_t *snapshot,
                           tiku_rich_pos_t a, tiku_rich_pos_t b, int *order)
{
    tiku_rich_status_t status;
    size_t ai, bi;

    if (!order)
        return TIKU_RICH_INVALID;
    *order = 0;
    status = tiku_richsnapshot_position(snapshot, a);
    if (status != TIKU_RICH_OK)
        return status;
    status = tiku_richsnapshot_position(snapshot, b);
    if (status != TIKU_RICH_OK)
        return status;
    ai = tiku_rd_find(snapshot->root, a.paragraph);
    bi = tiku_rd_find(snapshot->root, b.paragraph);
    *order = ai < bi ? -1 : ai > bi ? 1 :
             a.byte < b.byte ? -1 : a.byte > b.byte ? 1 : 0;
    return TIKU_RICH_OK;
}

tiku_rich_status_t
tiku_richsnapshot_resolve(const tiku_richsnapshot_t *snapshot,
                           size_t paragraph, size_t run,
                           tiku_rich_chars_t *chars, tiku_rich_para_t *props)
{
    tiku_rich_status_t status;

    if (chars) memset(chars, 0, sizeof *chars);
    if (props) memset(props, 0, sizeof *props);
    if (!snapshot || !chars || !props)
        return TIKU_RICH_INVALID;
    status = tiku_rd_resolve(snapshot->root, paragraph, run, chars, props);
    if (status != TIKU_RICH_OK) {
        memset(chars, 0, sizeof *chars);
        memset(props, 0, sizeof *props);
    }
    return status;
}

tiku_rich_status_t
tiku_richsnapshot_text(const tiku_richsnapshot_t *snapshot, char *buffer,
                        size_t capacity, size_t *length)
{
    const tiku_rd_root_t *root;
    size_t total, i, at = 0;

    if (!length)
        return TIKU_RICH_INVALID;
    *length = 0;
    if (!snapshot || (!buffer && capacity))
        return TIKU_RICH_INVALID;
    root = snapshot->root;
    if (root->bytes > SIZE_MAX - root->count)
        return TIKU_RICH_LIMIT;
    total = root->bytes + root->count - 1;
    *length = total;
    if (!buffer && !capacity)
        return TIKU_RICH_OK;
    if (capacity <= total)
        return TIKU_RICH_LIMIT;
    for (i = 0; i < root->count; i++) {
        const tiku_rich_paragraph_t *p = &root->paragraphs[i]->value;

        if (i)
            buffer[at++] = '\n';
        memcpy(buffer + at, p->text, p->length);
        at += p->length;
    }
    buffer[at] = '\0';
    return TIKU_RICH_OK;
}
