/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_richdoc_paste.c - atomic styled-fragment replacement and position maps.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richdoc_private.h"

#include <string.h>

/** @brief Resources and endpoint indices for one unpublished paste. */
typedef struct {
    tiku_richdoc_t *doc;       /**< destination document */
    tiku_rd_root_t *root;      /**< owned candidate root */
    const tiku_rd_root_t *source; /**< borrowed fragment */
    uint64_t *styles;          /**< source-array to destination ID map */
    uint64_t next_id;          /**< unpublished next identity */
    tiku_rich_pos_t first, last; /**< destination selection */
    size_t begin, end;         /**< destination paragraph indices */
} paste_t;

/** @brief Translate a fragment-local style ID into its destination identity. */
static uint64_t
mapped_style(const paste_t *paste, uint64_t id)
{
    return id ? paste->styles[tiku_rd_style_find(paste->source, id)] : 0;
}

/** @brief Copy imported resource ownership into the destination allocator. */
static tiku_rich_status_t
append_imported(const paste_t *paste, tiku_rich_paragraph_t *out,
                tiku_rich_run_t run)
{
    tiku_rich_status_t status = TIKU_RICH_OK;
    const char *link = tiku_rd_link_new(paste->doc->memory, run.link, &status);
    const tiku_rich_image_t *image = NULL;

    if (status == TIKU_RICH_OK && run.image)
        image = tiku_rd_image_new(paste->doc->memory, run.image, NULL,
                                  &status);
    if (status == TIKU_RICH_OK) {
        run.link = link;
        run.image = image;
        tiku_rd_append_run(out, &run);
    }
    tiku_rd_image_drop(image);
    tiku_rd_link_drop(link);
    return status;
}

/** @brief Append a slice, preserving its character appearance and targets. */
static tiku_rich_status_t
append_slice(const paste_t *paste, tiku_rich_paragraph_t *out,
    const tiku_rich_paragraph_t *from, size_t first, size_t last, int imported)
{
    const tiku_rd_root_t *origin = imported ? paste->source : paste->root;
    tiku_rich_run_t run;
    tiku_rich_chars_t wanted, base;
    tiku_rich_para_t props;
    tiku_rich_status_t status;
    size_t i, at = 0, left, right;

    for (i = 0; i < from->run_count && at < last; i++) {
        run = from->runs[i];
        left = at > first ? at : first;
        right = at + run.length < last ? at + run.length : last;
        at += run.length;
        if (left >= right)
            continue;
        status = tiku_rd_resolve_value(origin, from, &run, &wanted, &props);
        if (status != TIKU_RICH_OK)
            return status;
        if (imported)
            run.style = mapped_style(paste, run.style);
        run.length = right - left;
        status = tiku_rd_resolve_value(paste->root, out, &run, &base, &props);
        if (status != TIKU_RICH_OK)
            return status;
        tiku_rd_preserve_chars(&run, &wanted, &base);
        if (imported && (run.link || run.image)) {
            status = append_imported(paste, out, run);
            if (status != TIKU_RICH_OK) return status;
        } else {
            tiku_rd_append_run(out, &run);
        }
    }
    return TIKU_RICH_OK;
}

/** @brief Construct one boundary or interior paragraph of the pasted range. */
static tiku_rd_paragraph_t *
paste_paragraph(paste_t *paste, size_t index, tiku_rich_status_t *status)
{
    const tiku_rich_paragraph_t *source =
        &paste->source->paragraphs[index]->value;
    const tiku_rich_paragraph_t *left =
        &paste->root->paragraphs[paste->begin]->value;
    const tiku_rich_paragraph_t *right =
        &paste->root->paragraphs[paste->end]->value;
    size_t prefix = index ? 0 : paste->first.byte;
    size_t suffix = index + 1 == paste->source->count ?
                   right->length - paste->last.byte : 0;
    tiku_rich_paragraph_t base = *source;
    tiku_rd_paragraph_t *node;
    size_t capacity = source->run_count;
    char *text;

    base.style = mapped_style(paste, base.style);
    if (prefix)
        base = *left;
    else if (suffix)
        base = *right;
    if (prefix)
        capacity += tiku_rd_run_count(left, 0, prefix);
    if (suffix)
        capacity += tiku_rd_run_count(right, paste->last.byte, right->length);
    if (index && !paste->next_id) {
        *status = TIKU_RICH_LIMIT;
        return NULL;
    }
    node = tiku_rd_paragraph_new(paste->doc, &base,
        prefix + source->length + suffix, capacity, status);
    if (!node)
        return NULL;
    node->value.id = index ? paste->next_id : left->id;
    if (index)
        paste->next_id = paste->next_id == UINT64_MAX ? 0 : paste->next_id + 1;
    text = (char *)node->value.text;
    memcpy(text, left->text, prefix);
    memcpy(text + prefix, source->text, source->length);
    memcpy(text + prefix + source->length, right->text + paste->last.byte,
           suffix);
    *status = append_slice(paste, &node->value, left, 0, prefix, 0);
    if (*status == TIKU_RICH_OK)
        *status = append_slice(paste, &node->value, source, 0,
                               source->length, 1);
    if (*status == TIKU_RICH_OK && suffix)
        *status = append_slice(paste, &node->value, right,
                               paste->last.byte, right->length, 0);
    if (*status != TIKU_RICH_OK) {
        tiku_rd_paragraph_drop(node);
        return NULL;
    }
    return node;
}

/** @brief Release an independently owned paragraph array. */
static void
drop_array(tiku_rd_paragraph_t **array, size_t count)
{
    size_t i;

    for (i = 0; i < count; i++)
        tiku_rd_paragraph_drop(array[i]);
    tiku_rd_free(array);
}

/** @brief Replace sibling paragraph blocks while preserving their owner. */
static tiku_rich_status_t
splice_blocks(paste_t *paste, tiku_rd_paragraph_t **array)
{
    tiku_rd_root_t *root = paste->root;
    tiku_rd_blocks_t *old = root->blocks, *next;
    tiku_rich_status_t status = TIKU_RICH_OK;
    size_t first, last, count;

    if (!old) return TIKU_RICH_OK;
    first = tiku_rd_block_find(root, root->paragraphs[paste->begin]->value.id);
    last = tiku_rd_block_find(root, root->paragraphs[paste->end]->value.id);
    count = old->count - (last - first + 1) + paste->source->count;
    next = tiku_rd_blocks_new(paste->doc, count, &status);
    if (!next) return status;
    memcpy(next->nodes, old->nodes, first * sizeof *next->nodes);
    for (size_t i = 0; i < paste->source->count; i++)
        next->nodes[first + i] = (tiku_rich_block_t){
            .kind = TIKU_RICH_BLOCK_PARAGRAPH,
            .parent = old->nodes[first].parent,
            .id = array[paste->begin + i]->value.id};
    memcpy(next->nodes + first + paste->source->count, old->nodes + last + 1,
           (old->count - last - 1) * sizeof *next->nodes);
    root->blocks = next;
    tiku_rd_blocks_drop(old);
    return TIKU_RICH_OK;
}

/** @brief Build a replacement array, retaining untouched paragraphs. */
static tiku_rich_status_t
splice(paste_t *paste)
{
    tiku_rd_root_t *root = paste->root;
    size_t retained = root->count - (paste->end - paste->begin + 1);
    size_t count, i, old, bytes = 0, runs = 0;
    tiku_rd_paragraph_t **array;
    tiku_rich_status_t status = TIKU_RICH_OK;

    if (retained > paste->doc->limits.paragraphs ||
        paste->source->count > paste->doc->limits.paragraphs - retained ||
        paste->source->count > SIZE_MAX / sizeof *array - retained)
        return TIKU_RICH_LIMIT;
    count = retained + paste->source->count;
    array = tiku_rd_alloc(paste->doc->memory, count * sizeof *array, &status);
    if (!array)
        return status;
    for (i = 0; i < count; i++) {
        if (i >= paste->begin && i - paste->begin < paste->source->count) {
            array[i] = paste_paragraph(paste, i - paste->begin, &status);
            if (!array[i])
                goto fail;
        } else {
            old = i < paste->begin ? i :
                i - paste->begin - paste->source->count + paste->end + 1;
            array[i] = root->paragraphs[old];
            atomic_fetch_add_explicit(&array[i]->refs, 1, memory_order_relaxed);
        }
        if (array[i]->value.run_count > paste->doc->limits.runs - runs ||
            array[i]->value.length > paste->doc->limits.text_bytes - bytes) {
            status = TIKU_RICH_LIMIT;
            goto fail;
        }
        runs += array[i]->value.run_count;
        bytes += array[i]->value.length;
    }
    status = splice_blocks(paste, array);
    if (status != TIKU_RICH_OK) goto fail;
    drop_array(root->paragraphs, root->count);
    root->paragraphs = array;
    root->count = count;
    root->bytes = bytes;
    root->runs = runs;
    return TIKU_RICH_OK;
fail:
    drop_array(array, count);
    return status;
}

/** @brief Validate the live range and final text capacity before copying. */
static tiku_rich_status_t
check_paste(paste_t *paste, uint64_t generation)
{
    tiku_richdoc_t *doc = paste->doc;
    tiku_richsnapshot_t snapshot = {0};
    tiku_rich_status_t status;
    size_t i, removed = 0, kept;
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
    status = tiku_richsnapshot_compare(&snapshot, paste->first,
                                       paste->last, &order);
    if (status != TIKU_RICH_OK)
        return status;
    if (order > 0)
        return TIKU_RICH_INVALID_RANGE;
    paste->begin = tiku_rd_find(doc->root, paste->first.paragraph);
    paste->end = tiku_rd_find(doc->root, paste->last.paragraph);
    if (!tiku_rd_same_container(doc->root, paste->begin, paste->end))
        return TIKU_RICH_INVALID_RANGE;
    for (i = paste->begin; i <= paste->end; i++)
        removed += doc->root->paragraphs[i]->value.length;
    removed -= paste->first.byte;
    removed -= doc->root->paragraphs[paste->end]->value.length -
               paste->last.byte;
    kept = doc->root->bytes - removed;
    if (paste->source->bytes > doc->limits.text_bytes - kept ||
        paste->source->bytes >= SIZE_MAX - kept)
        return TIKU_RICH_LIMIT;
    return TIKU_RICH_OK;
}

/** @brief Prepare the whole replacement's separator-inclusive position map. */
static tiku_rd_map_t *
paste_map(const paste_t *paste, tiku_rich_status_t *status)
{
    tiku_rd_map_t *map;
    size_t i, end = paste->last.byte;

    map = tiku_rd_alloc(paste->doc->memory, sizeof *map, status);
    if (!map)
        return NULL;
    map->kind = TIKU_RICH_REPLACE;
    map->flat = 1;
    map->at = paste->first.byte;
    map->inserted = paste->source->bytes + paste->source->count - 1;
    for (i = 0; i < paste->begin; i++)
        map->at += paste->doc->root->paragraphs[i]->value.length + 1;
    for (i = 0; i < paste->end; i++)
        end += paste->doc->root->paragraphs[i]->value.length + 1;
    map->removed = end - map->at;
    return map;
}

tiku_rich_status_t
tiku_rd_paste_into(tiku_richdoc_t *doc, tiku_rd_root_t *root,
    tiku_rich_pos_t first, tiku_rich_pos_t last,
    const tiku_richfragment_t *fragment, uint64_t *next, tiku_rd_map_t *map)
{
    tiku_richdoc_t candidate = *doc;
    paste_t paste = {.doc = &candidate, .root = root,
        .source = fragment->root, .first = first, .last = last,
        .next_id = *next};
    tiku_rich_status_t status;
    tiku_rd_map_t *allocated;

    candidate.root = root;
    candidate.limits.text_bytes = SIZE_MAX;
    candidate.limits.paragraphs = SIZE_MAX;
    candidate.limits.runs = SIZE_MAX;
    paste.begin = tiku_rd_find(root, first.paragraph);
    paste.end = tiku_rd_find(root, last.paragraph);
    allocated = paste_map(&paste, &status);
    if (!allocated) return status;
    *map = *allocated;
    tiku_rd_free(allocated);
    status = tiku_rd_fragment_styles(&candidate, root, paste.source,
                                      &paste.next_id, &paste.styles);
    if (status == TIKU_RICH_OK) status = splice(&paste);
    tiku_rd_free(paste.styles);
    if (status == TIKU_RICH_OK) *next = paste.next_id;
    return status;
}

tiku_rich_status_t
tiku_richdoc_paste_fragment(tiku_richdoc_t *doc, uint64_t generation,
    tiku_rich_pos_t first, tiku_rich_pos_t last,
    const tiku_richfragment_t *fragment)
{
    paste_t paste = {.doc = doc, .first = first, .last = last};
    tiku_rd_map_t *map = NULL;
    tiku_rd_history_t *entry;
    tiku_rich_status_t status;

    if (!fragment)
        return TIKU_RICH_INVALID;
    if (fragment->empty)
        return tiku_richdoc_replace_range(doc, generation, first, last, "", 0);
    paste.source = fragment->root;
    status = check_paste(&paste, generation);
    if (status != TIKU_RICH_OK)
        return status;
    paste.next_id = doc->next_id;
    paste.root = tiku_rd_clone(doc, &status);
    if (!paste.root)
        return status;
    status = tiku_rd_fragment_styles(doc, paste.root, paste.source,
                                      &paste.next_id, &paste.styles);
    if (status == TIKU_RICH_OK)
        status = splice(&paste);
    if (status != TIKU_RICH_OK)
        goto fail;
    map = paste_map(&paste, &status);
    if (!map)
        goto fail;
    entry = tiku_rd_history_new(doc, paste.root, map, 1, &status);
    if (!entry)
        goto fail;
    doc->next_id = paste.next_id;
    tiku_rd_publish(doc, paste.root, entry);
    tiku_rd_free(paste.styles);
    return TIKU_RICH_OK;
fail:
    tiku_rd_free(map);
    tiku_rd_free(paste.styles);
    tiku_rd_root_drop(paste.root);
    return status;
}
