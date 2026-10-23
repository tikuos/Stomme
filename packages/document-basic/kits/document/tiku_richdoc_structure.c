/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_richdoc_structure.c - paragraph split and formatting-preserving join.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richdoc_private.h"

#include <string.h>

/** @brief Copy a paragraph slice with its direct and named formatting. */
static tiku_rd_paragraph_t *
slice(tiku_richdoc_t *doc, const tiku_rich_paragraph_t *from,
        size_t start, size_t end, tiku_rich_status_t *status)
{
    tiku_rd_paragraph_t *node = tiku_rd_paragraph_new(doc, from,
        end - start, from->run_count, status);

    if (!node)
        return NULL;
    memcpy((char *)node->value.text, from->text + start, end - start);
    tiku_rd_slice_runs(&node->value, from, start, end, NULL);
    return node;
}

void
tiku_rd_preserve_chars(tiku_rich_run_t *run, const tiku_rich_chars_t *wanted,
                 const tiku_rich_chars_t *base)
{
    tiku_rich_chars_t a, b, patch = *wanted;
    unsigned bit, changed = 0;

    for (bit = 1; bit <= TIKU_RICH_BASELINE; bit <<= 1) {
        a = *wanted;
        b = *base;
        tiku_rd_chars_reset(&a, ~bit);
        tiku_rd_chars_reset(&b, ~bit);
        if (!tiku_rd_chars_equal(&a, &b))
            changed |= bit;
    }
    run->reset |= changed & ~wanted->present;
    tiku_rd_chars_reset(&patch, ~changed);
    tiku_rd_chars_overlay(&run->chars, &patch);
}

/** @brief Append runs while preserving their former paragraph appearance. */
static tiku_rich_status_t
join_runs(const tiku_rd_root_t *root, tiku_rich_paragraph_t *out,
            const tiku_rich_paragraph_t *right)
{
    tiku_rich_chars_t wanted, base;
    tiku_rich_para_t props;
    tiku_rich_run_t run;
    tiku_rich_status_t status;
    size_t i;

    for (i = 0; i < right->run_count; i++) {
        run = right->runs[i];
        status = tiku_rd_resolve_value(root, right, &run, &wanted, &props);
        if (status != TIKU_RICH_OK)
            return status;
        status = tiku_rd_resolve_value(root, out, &run, &base, &props);
        if (status != TIKU_RICH_OK)
            return status;
        tiku_rd_preserve_chars(&run, &wanted, &base);
        tiku_rd_append_run(out, &run);
    }
    return TIKU_RICH_OK;
}

/** @brief Split an unpublished paragraph and transfer its array ownership. */
static tiku_rich_status_t
split_paragraph(tiku_richdoc_t *doc, tiku_rd_root_t *root, size_t index,
                  size_t at, uint64_t *next_id, tiku_rd_map_t *map)
{
    const tiku_rich_paragraph_t *p = &root->paragraphs[index]->value;
    tiku_rd_paragraph_t *left = NULL, *right = NULL, **array;
    tiku_rich_status_t status = TIKU_RICH_OK;
    size_t runs;
    tiku_rich_chars_t chars;
    tiku_rich_para_t props;

    if (!tiku_rd_boundary(p->text, p->length, at))
        return TIKU_RICH_INVALID_RANGE;
    if (!*next_id || root->count >= doc->limits.paragraphs ||
        root->count >= SIZE_MAX / sizeof *array)
        return TIKU_RICH_LIMIT;
    left = slice(doc, p, 0, at, &status);
    if (!left)
        return status;
    right = slice(doc, p, at, p->length, &status);
    if (!right)
        goto fail;
    right->value.list.restart = 0;
    tiku_rd_resolve(root, index, 0, &chars, &props);
    if (props.break_before) {
        right->value.paragraph.present |= TIKU_RICH_PAGE_BREAK;
        right->value.paragraph.break_before = 0;
    }
    runs = left->value.run_count + right->value.run_count;
    if (runs > doc->limits.runs - (root->runs - p->run_count)) {
        status = TIKU_RICH_LIMIT;
        goto fail;
    }
    array = tiku_rd_alloc(doc->memory,
                           (root->count + 1) * sizeof *array, &status);
    if (!array)
        goto fail;
    right->value.id = *next_id;
    *map = (tiku_rd_map_t){.kind = TIKU_RICH_SPLIT,
        .first = p->id, .second = *next_id, .at = at};
    *next_id = *next_id == UINT64_MAX ? 0 : *next_id + 1;
    memcpy(array, root->paragraphs, index * sizeof *array);
    array[index] = left;
    array[index + 1] = right;
    memcpy(array + index + 2, root->paragraphs + index + 1,
           (root->count - index - 1) * sizeof *array);
    root->runs = root->runs - p->run_count + runs;
    tiku_rd_paragraph_drop(root->paragraphs[index]);
    tiku_rd_free(root->paragraphs);
    root->paragraphs = array;
    root->count++;
    return TIKU_RICH_OK;
fail:
    tiku_rd_paragraph_drop(left);
    tiku_rd_paragraph_drop(right);
    return status;
}

/** @brief Join adjacent paragraphs while keeping the leading defaults. */
static tiku_rich_status_t
join_paragraphs(tiku_richdoc_t *doc, tiku_rd_root_t *root, size_t index,
                  tiku_rd_map_t *map)
{
    const tiku_rich_paragraph_t *left = &root->paragraphs[index]->value;
    const tiku_rich_paragraph_t *right;
    tiku_rd_paragraph_t *node;
    tiku_rich_status_t status = TIKU_RICH_OK;

    if (index + 1 == root->count)
        return TIKU_RICH_INVALID_RANGE;
    if (!tiku_rd_same_container(root, index, index + 1))
        return TIKU_RICH_INVALID_RANGE;
    right = &root->paragraphs[index + 1]->value;
    node = tiku_rd_paragraph_new(doc, left, left->length + right->length,
        left->run_count + right->run_count, &status);
    if (!node)
        return status;
    memcpy((char *)node->value.text, left->text, left->length);
    memcpy((char *)node->value.text + left->length, right->text, right->length);
    tiku_rd_slice_runs(&node->value, left, 0, left->length, NULL);
    status = join_runs(root, &node->value, right);
    if (status != TIKU_RICH_OK) {
        tiku_rd_paragraph_drop(node);
        return status;
    }
    *map = (tiku_rd_map_t){.kind = TIKU_RICH_JOIN, .first = left->id,
        .second = right->id, .at = left->length};
    root->runs = root->runs - left->run_count - right->run_count +
                 node->value.run_count;
    tiku_rd_paragraph_drop(root->paragraphs[index]);
    tiku_rd_paragraph_drop(root->paragraphs[index + 1]);
    root->paragraphs[index] = node;
    memmove(root->paragraphs + index + 1, root->paragraphs + index + 2,
            (root->count - index - 2) * sizeof *root->paragraphs);
    root->count--;
    root->paragraphs[root->count] = NULL;
    return TIKU_RICH_OK;
}

tiku_rich_status_t
tiku_rd_structure(tiku_richdoc_t *doc, tiku_rd_root_t *root,
    const tiku_rich_edit_t *edit, uint64_t *next_id, tiku_rd_map_t *map)
{
    size_t index = tiku_rd_find(root, edit->paragraph);

    if (index == SIZE_MAX)
        return TIKU_RICH_INVALID_RANGE;
    if (edit->kind == TIKU_RICH_SPLIT) {
        if (edit->start != edit->end)
            return TIKU_RICH_INVALID_RANGE;
        return split_paragraph(doc, root, index, edit->start, next_id, map);
    }
    if (edit->kind == TIKU_RICH_JOIN) {
        if (edit->start || edit->end)
            return TIKU_RICH_INVALID_RANGE;
        return join_paragraphs(doc, root, index, map);
    }
    return TIKU_RICH_UNSUPPORTED;
}
