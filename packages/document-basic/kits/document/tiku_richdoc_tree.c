/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_richdoc_tree.c - owned immutable paragraph and style construction.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richdoc_private.h"

#include <string.h>

void
tiku_rd_paragraph_drop(tiku_rd_paragraph_t *paragraph)
{
    size_t i;

    if (!paragraph || atomic_fetch_sub_explicit(&paragraph->refs, 1,
                                                memory_order_acq_rel) != 1)
        return;
    for (i = 0; i < paragraph->value.run_count; i++) {
        tiku_rd_link_drop(paragraph->value.runs[i].link);
        tiku_rd_image_drop(paragraph->value.runs[i].image);
    }
    tiku_rd_free((void *)paragraph->value.text);
    tiku_rd_free((void *)paragraph->value.runs);
    tiku_rd_free(paragraph);
}

void
tiku_rd_root_drop(tiku_rd_root_t *root)
{
    size_t i;

    if (!root || atomic_fetch_sub_explicit(&root->refs, 1,
                                           memory_order_acq_rel) != 1)
        return;
    if (root->paragraphs)
        for (i = 0; i < root->count; i++)
            tiku_rd_paragraph_drop(root->paragraphs[i]);
    tiku_rd_free(root->paragraphs);
    tiku_rd_free(root->styles);
    tiku_rd_blocks_drop(root->blocks);
    tiku_rd_bookmarks_drop(root->bookmarks);
    tiku_rd_free(root);
}

/** @brief Consume one checked ID, using zero to mark exhaustion. */
static uint64_t
take_id(uint64_t *next)
{
    uint64_t id = *next;

    *next = id == UINT64_MAX ? 0 : id + 1;
    return id;
}

/** @brief Convert a validated one-based source style index to its ID. */
static uint64_t
style_id(const tiku_rd_root_t *root, size_t index)
{
    return index ? root->styles[index - 1].value.id : 0;
}

/** @brief Copy named styles and translate inheritance indices to IDs. */
static int
build_styles(tiku_richdoc_t *doc, tiku_rd_root_t *root,
              const tiku_rich_source_t *source, uint64_t *next,
              tiku_rich_status_t *status)
{
    size_t i;
    tiku_rd_style_t *to;
    const tiku_rich_style_source_t *from;

    if (!source->style_count)
        return 1;
    root->styles = tiku_rd_alloc(doc->memory,
                          source->style_count * sizeof *root->styles, status);
    if (!root->styles)
        return 0;
    root->nstyles = source->style_count;
    for (i = 0; i < root->nstyles; i++)
        root->styles[i].value.id = take_id(next);
    for (i = 0; i < root->nstyles; i++) {
        to = &root->styles[i];
        from = &source->styles[i];
        memcpy(to->name, from->name, strlen(from->name) + 1);
        to->value.name = to->name;
        to->value.kind = from->kind;
        to->value.parent = style_id(root, from->parent);
        tiku_rd_chars(&from->chars, &to->value.chars);
        tiku_rd_props(&from->paragraph, &to->value.paragraph);
    }
    return 1;
}

/** @brief Build one validated paragraph and copy its text and runs. */
static tiku_rd_paragraph_t *
build_paragraph(tiku_richdoc_t *doc, const tiku_rd_root_t *root,
                 const tiku_rich_paragraph_source_t *source, uint64_t *next,
                 tiku_rich_status_t *status)
{
    tiku_rd_paragraph_t *node;
    tiku_rich_paragraph_t *p;
    tiku_rich_run_t *runs;
    char *text;
    size_t i, count = source->run_count;

    node = tiku_rd_alloc(doc->memory, sizeof *node, status);
    if (!node)
        return NULL;
    atomic_init(&node->refs, 1);
    p = &node->value;
    p->id = take_id(next);
    p->style = style_id(root, source->style);
    tiku_rd_chars(&source->chars, &p->chars);
    tiku_rd_props(&source->paragraph, &p->paragraph);
    tiku_rd_list(&source->list, &p->list);
    text = tiku_rd_alloc(doc->memory, source->length + 1, status);
    if (!text) {
        tiku_rd_paragraph_drop(node);
        return NULL;
    }
    p->text = text;
    p->length = source->length;
    if (source->length)
        memcpy(text, source->text, source->length);
    if (!count && source->length)
        count = 1;
    if (!count)
        return node;
    runs = tiku_rd_alloc(doc->memory, count * sizeof *runs, status);
    if (!runs) {
        tiku_rd_paragraph_drop(node);
        return NULL;
    }
    p->runs = runs;
    p->run_count = count;
    if (!source->run_count) {
        runs[0].length = source->length;
        return node;
    }
    for (i = 0; i < count; i++) {
        runs[i].length = source->runs[i].length;
        runs[i].style = style_id(root, source->runs[i].style);
        runs[i].reset = source->runs[i].reset;
        tiku_rd_chars(&source->runs[i].chars, &runs[i].chars);
        runs[i].link = tiku_rd_link_new(doc->memory, source->runs[i].link,
                                        status);
        if (*status == TIKU_RICH_OK && source->runs[i].image)
            runs[i].image = tiku_rd_image_new(doc->memory,
                source->runs[i].image, NULL, status);
        if (*status != TIKU_RICH_OK) {
            tiku_rd_paragraph_drop(node);
            return NULL;
        }
    }
    return node;
}

tiku_rd_root_t *
tiku_rd_build(tiku_richdoc_t *doc, const tiku_rich_source_t *source,
               uint64_t *next_id, tiku_rich_status_t *status)
{
    tiku_rd_root_t *root;
    const tiku_rich_paragraph_source_t empty = {0};
    size_t i, count = source->paragraph_count ? source->paragraph_count : 1;
    uint64_t required = count;

    if (source->style_count > UINT64_MAX - required) {
        *status = TIKU_RICH_LIMIT;
        return NULL;
    }
    required += source->style_count;
    if (!*next_id || required - 1 > UINT64_MAX - *next_id) {
        *status = TIKU_RICH_LIMIT;
        return NULL;
    }
    root = tiku_rd_alloc(doc->memory, sizeof *root, status);
    if (!root)
        return NULL;
    atomic_init(&root->refs, 1);
    root->body = 1;
    tiku_rich_page_setup(&source->page, &root->page);
    root->count = count;
    root->paragraphs = tiku_rd_alloc(doc->memory,
                                      count * sizeof *root->paragraphs,
                                      status);
    if (!root->paragraphs ||
        !build_styles(doc, root, source, next_id, status)) {
        tiku_rd_root_drop(root);
        return NULL;
    }
    for (i = 0; i < count; i++) {
        root->paragraphs[i] = build_paragraph(doc, root,
            source->paragraph_count ? &source->paragraphs[i] : &empty,
            next_id, status);
        if (!root->paragraphs[i]) {
            tiku_rd_root_drop(root);
            return NULL;
        }
        root->bytes += root->paragraphs[i]->value.length;
        root->runs += root->paragraphs[i]->value.run_count;
    }
    *status = tiku_rd_blocks_build(doc, root, source, next_id);
    if (*status == TIKU_RICH_OK)
        *status = tiku_rd_bookmarks_build(doc, root, source);
    if (*status != TIKU_RICH_OK) {
        tiku_rd_root_drop(root);
        return NULL;
    }
    return root;
}

size_t
tiku_rd_find(const tiku_rd_root_t *root, uint64_t id)
{
    size_t i;

    for (i = 0; i < root->count; i++)
        if (root->paragraphs[i]->value.id == id)
            return i;
    return SIZE_MAX;
}

tiku_rd_root_t *
tiku_rd_clone(tiku_richdoc_t *doc, tiku_rich_status_t *status)
{
    const tiku_rd_root_t *from = doc->root;
    tiku_rd_root_t *to;
    size_t i;

    to = tiku_rd_alloc(doc->memory, sizeof *to, status);
    if (!to)
        return NULL;
    atomic_init(&to->refs, 1);
    to->body = from->body;
    to->state = from->state;
    to->count = from->count;
    to->bytes = from->bytes;
    to->runs = from->runs;
    to->page = from->page;
    to->nstyles = from->nstyles;
    to->blocks = from->blocks;
    to->bookmarks = from->bookmarks;
    if (to->bookmarks)
        atomic_fetch_add_explicit(&to->bookmarks->refs, 1, memory_order_relaxed);
    if (to->blocks)
        atomic_fetch_add_explicit(&to->blocks->refs, 1, memory_order_relaxed);
    to->paragraphs = tiku_rd_alloc(doc->memory,
                                    to->count * sizeof *to->paragraphs,
                                    status);
    if (!to->paragraphs)
        goto fail;
    for (i = 0; i < to->count; i++) {
        to->paragraphs[i] = from->paragraphs[i];
        atomic_fetch_add_explicit(&to->paragraphs[i]->refs, 1,
                                   memory_order_relaxed);
    }
    if (to->nstyles) {
        to->styles = tiku_rd_alloc(doc->memory,
                                    to->nstyles * sizeof *to->styles, status);
        if (!to->styles)
            goto fail;
        memcpy(to->styles, from->styles, to->nstyles * sizeof *to->styles);
        for (i = 0; i < to->nstyles; i++)
            to->styles[i].value.name = to->styles[i].name;
    }
    return to;
fail:
    tiku_rd_root_drop(to);
    return NULL;
}
