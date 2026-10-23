/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_richdoc_fragment.c - independently retained styled selection capture.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richdoc_private.h"

#include <string.h>

/** @brief Mark a used style and its complete validated ancestor chain. */
static void
mark_style(const tiku_rd_root_t *source, unsigned char *used, uint64_t id)
{
    size_t index;

    while (id) {
        index = tiku_rd_style_find(source, id);
        if (used[index])
            break;
        used[index] = 1;
        id = source->styles[index].value.parent;
    }
}

/** @brief Copy only styles referenced by captured paragraphs and runs. */
static tiku_rich_status_t
copy_styles(tiku_richfragment_t *fragment, const tiku_rd_root_t *source)
{
    tiku_rd_root_t *to = fragment->root;
    unsigned char *used;
    size_t i, j, count = 0;
    const tiku_rich_paragraph_t *p;
    tiku_rich_status_t status = TIKU_RICH_OK;

    if (!source->nstyles || fragment->empty)
        return TIKU_RICH_OK;
    used = tiku_rd_alloc(fragment->memory, source->nstyles, &status);
    if (!used)
        return status;
    for (i = 0; i < to->count; i++) {
        p = &to->paragraphs[i]->value;
        mark_style(source, used, p->style);
        for (j = 0; j < p->run_count; j++)
            mark_style(source, used, p->runs[j].style);
    }
    for (i = 0; i < source->nstyles; i++)
        count += used[i] != 0;
    if (count) {
        to->styles = tiku_rd_alloc(fragment->memory,
            count * sizeof *to->styles, &status);
        if (!to->styles)
            goto done;
    }
    for (i = 0; i < source->nstyles; i++) {
        if (!used[i])
            continue;
        to->styles[to->nstyles] = source->styles[i];
        to->styles[to->nstyles].value.name = to->styles[to->nstyles].name;
        to->nstyles++;
    }
done:
    tiku_rd_free(used);
    return status;
}

/** @brief Copy selected text/runs without retaining unselected paragraphs. */
static tiku_rich_status_t
copy_paragraphs(tiku_richfragment_t *fragment, const tiku_rd_root_t *source,
    size_t begin, size_t end, size_t first, size_t last)
{
    tiku_richdoc_t allocator = {.memory = fragment->memory};
    tiku_rd_root_t *root = fragment->root;
    const tiku_rich_paragraph_t *p;
    tiku_rd_paragraph_t *node;
    tiku_rich_status_t status = TIKU_RICH_OK;
    size_t i, start, finish;

    root->count = end - begin + 1;
    root->paragraphs = tiku_rd_alloc(fragment->memory,
        root->count * sizeof *root->paragraphs, &status);
    if (!root->paragraphs)
        return status;
    for (i = begin; i <= end; i++) {
        p = &source->paragraphs[i]->value;
        start = i == begin ? first : 0;
        finish = i == end ? last : p->length;
        node = tiku_rd_paragraph_new(&allocator, p, finish - start,
            tiku_rd_run_count(p, start, finish), &status);
        if (!node)
            return status;
        root->paragraphs[i - begin] = node;
        memcpy((char *)node->value.text, p->text + start, finish - start);
        tiku_rd_slice_runs(&node->value, p, start, finish, NULL);
        root->bytes += node->value.length;
        root->runs += node->value.run_count;
        if (fragment->empty) {
            node->value.style = 0;
            memset(&node->value.chars, 0, sizeof node->value.chars);
            memset(&node->value.paragraph, 0, sizeof node->value.paragraph);
            memset(&node->value.list, 0, sizeof node->value.list);
        }
    }
    return TIKU_RICH_OK;
}

tiku_rich_status_t
tiku_rd_fragment_create(const tiku_richsnapshot_t *snapshot,
    tiku_rich_pos_t first, tiku_rich_pos_t last, int keep_empty,
    tiku_richfragment_t **out)
{
    tiku_rich_status_t status;
    tiku_richfragment_t *fragment;
    size_t begin, end;
    int order;

    if (!out)
        return TIKU_RICH_INVALID;
    *out = NULL;
    if (!snapshot)
        return TIKU_RICH_INVALID;
    status = tiku_richsnapshot_compare(snapshot, first, last, &order);
    if (status != TIKU_RICH_OK)
        return status;
    if (order > 0)
        return TIKU_RICH_INVALID_RANGE;
    begin = tiku_rd_find(snapshot->root, first.paragraph);
    end = tiku_rd_find(snapshot->root, last.paragraph);
    if (!tiku_rd_same_container(snapshot->root, begin, end))
        return TIKU_RICH_UNSUPPORTED;
    fragment = tiku_rd_alloc(snapshot->memory, sizeof *fragment, &status);
    if (!fragment)
        return status;
    fragment->memory = snapshot->memory;
    fragment->generation = snapshot->generation;
    fragment->empty = order == 0 && !keep_empty;
    fragment->root = tiku_rd_alloc(snapshot->memory, sizeof *fragment->root,
                                   &status);
    if (!fragment->root)
        goto fail;
    atomic_init(&fragment->root->refs, 1);
    fragment->root->body = snapshot->root->body;
    fragment->root->page = snapshot->root->page;
    status = copy_paragraphs(fragment, snapshot->root,
        begin, end, first.byte, last.byte);
    if (status == TIKU_RICH_OK)
        status = copy_styles(fragment, snapshot->root);
    if (status != TIKU_RICH_OK)
        goto fail;
    *out = fragment;
    return TIKU_RICH_OK;
fail:
    tiku_richfragment_release(fragment);
    return status;
}

tiku_rich_status_t
tiku_richfragment_create(const tiku_richsnapshot_t *snapshot,
    tiku_rich_pos_t first, tiku_rich_pos_t last, tiku_richfragment_t **out)
{
    return tiku_rd_fragment_create(snapshot, first, last, 0, out);
}

void
tiku_richfragment_release(tiku_richfragment_t *fragment)
{
    if (!fragment)
        return;
    tiku_rd_root_drop(fragment->root);
    tiku_rd_free(fragment);
}

tiku_rich_status_t
tiku_richfragment_snapshot(const tiku_richfragment_t *fragment,
                            tiku_richsnapshot_t **out)
{
    tiku_richsnapshot_t *snapshot;
    tiku_rich_status_t status = TIKU_RICH_OK;

    if (!out)
        return TIKU_RICH_INVALID;
    *out = NULL;
    if (!fragment)
        return TIKU_RICH_INVALID;
    snapshot = tiku_rd_alloc(fragment->memory, sizeof *snapshot, &status);
    if (!snapshot)
        return status;
    snapshot->root = fragment->root;
    snapshot->memory = fragment->memory;
    snapshot->generation = fragment->generation;
    snapshot->fragment = 1;
    atomic_fetch_add_explicit(&snapshot->root->refs, 1, memory_order_relaxed);
    *out = snapshot;
    return TIKU_RICH_OK;
}
