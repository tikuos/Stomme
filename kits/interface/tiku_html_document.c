/*
 * TikuOS Interface Kit.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_html_document.c - owned document tables with bounded growth and reflow.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_html_document.h"
#include <stdlib.h>
#include <string.h>

/** @brief Supply the default budgets and allocator for omitted option fields. */
static tiku_html_options_t html_options(const tiku_html_options_t *options)
{
    tiku_html_options_t o = options ? *options : (tiku_html_options_t){0};
    if (!o.source_bytes) o.source_bytes = TIKU_HTML_SOURCE_MAX;
    if (o.source_bytes > INT32_MAX/4-4096) o.source_bytes = INT32_MAX/4-4096;
    if (!o.memory_bytes) o.memory_bytes = TIKU_HTML_MEMORY_MAX;
    if (!o.allocate) o.allocate = malloc;
    if (!o.zero) o.zero = calloc;
    if (!o.release) o.release = free;
    return o;
}

/** @brief Whether custom allocation callbacks form a complete matched set. */
static int html_allocator_valid(const tiku_html_options_t *o)
{
    return !o || (!(o->allocate || o->zero || o->release) || (o->allocate && o->zero && o->release));
}

void tiku_html_document_free(tiku_doc_t *doc, const tiku_html_options_t *options)
{
    tiku_html_options_t o = html_options(options);
    if (!doc) return;
    if (doc->storage) o.release(doc->storage);
    o.release(doc);
}

tiku_doc_t *tiku_html_document_load(const char *source, int width,
                                     const tiku_html_options_t *options)
{
    tiku_html_options_t o = html_options(options);
    if (!html_allocator_valid(options) || o.memory_bytes < sizeof(tiku_doc_t)) return NULL;
    if (!source) source = "";
    size_t n = strnlen(source,o.source_bytes+1);
    if (n > o.source_bytes || n > INT32_MAX/4-4096) return NULL;
    tiku_doc_limits_t l = {
        (int)(n * 2 + 4096), (int)(n / 64 + 256),
        (int)(n / 32 + 1024), (int)(n / 32 + 1024),
        (int)(n / 16 + 2048), (int)(n / 128 + 256),
        (int)(n / 256 + 64), (int)(n / 1024 + 32)
    };
    if (l.blocks > 4096) l.blocks = 4096;
    if (l.runs > 8192) l.runs = 8192;
    if (l.links > 1024) l.links = 1024;
    if (l.anchors > 1024) l.anchors = 1024;
    if (l.images > 256) l.images = 256;
    tiku_doc_t *doc = o.zero(1, sizeof *doc);
    if (!doc) return NULL;
    tiku_doc_init(doc);
    /* A small document is tried first in the kit's inline buffers. */
    int small = n < TIKU_DOC_TEXT_MAX / 2;
    for (int pass = 0; pass < 16; pass++) {
        if (!small) {
            size_t size = tiku_doc_storage_size(&l);
            if (!size || size > o.memory_bytes-sizeof *doc) break;
            void *memory = o.allocate(size);
            if (!memory) break;
            if (doc->storage) o.release(doc->storage);
            if (!tiku_doc_init_storage(doc, &l, memory, size)) {
                o.release(memory); doc->storage = NULL; break;
            }
        }
        if (o.parse) { if (!o.parse(doc,source,o.parse_ctx)) break; }
        else tiku_doc_read(doc,source);
        if (width > 0) (void)tiku_doc_layout(doc,width);
        unsigned full = doc->full & (TIKU_DOC_FULL_TEXT | TIKU_DOC_FULL_BLOCKS |
            TIKU_DOC_FULL_RUNS | TIKU_DOC_FULL_LINKS | TIKU_DOC_FULL_ANCHORS |
            TIKU_DOC_FULL_IMAGES | TIKU_DOC_FULL_LINES | TIKU_DOC_FULL_PIECES);
        if (!full) { doc->laid_width = TIKU_DOC_NONE; return doc; }
        if (small) { small = 0; continue; }
        if (full & TIKU_DOC_FULL_TEXT) l.text *= 2;
        if (full & TIKU_DOC_FULL_BLOCKS) l.blocks *= 2;
        if (full & TIKU_DOC_FULL_RUNS) l.runs *= 2;
        if (full & TIKU_DOC_FULL_LINKS) l.links *= 2;
        if (full & TIKU_DOC_FULL_ANCHORS) l.anchors *= 2;
        if (full & TIKU_DOC_FULL_IMAGES) l.images *= 2;
        if (full & TIKU_DOC_FULL_LINES) l.lines *= 2;
        if (full & TIKU_DOC_FULL_PIECES) l.pieces *= 2;
    }
    tiku_html_document_free(doc,&o);
    return NULL;
}

/** @brief A copy of @p old's text, blocks, runs, links, anchors and images
 *         in new storage, with twice the room in each of its line and
 *         piece tables that the last layout filled; @p old is unchanged.
 *  @return the copy, or NULL. */
static tiku_doc_t *html_grow_layout(const tiku_doc_t *old, const tiku_html_options_t *o)
{
    tiku_doc_limits_t limits = old->limits;
    if (old->full & TIKU_DOC_FULL_LINES) limits.lines *= 2;
    if (old->full & TIKU_DOC_FULL_PIECES) limits.pieces *= 2;
    size_t bytes = tiku_doc_storage_size(&limits);
    if (!bytes || o->memory_bytes < sizeof(tiku_doc_t) ||
        bytes > o->memory_bytes-sizeof(tiku_doc_t)) return NULL;
    tiku_doc_t *doc = o->zero(1, sizeof *doc);
    void *storage = o->allocate(bytes);
    if (!doc || !storage ||
        !tiku_doc_init_storage(doc, &limits, storage, bytes)) {
        if (doc) o->release(doc);
        if (storage) o->release(storage);
        return NULL;
    }
    /* Runs, headings, links and the selection are named by index into
     * these tables, and the copy keeps every index. */
#define COPY_TABLE(member, count) do { \
    doc->count = old->count; \
    memcpy(doc->member, old->member, \
           (size_t)old->count * sizeof *old->member); \
} while (0)
    COPY_TABLE(text, ntext); COPY_TABLE(block, nblock); COPY_TABLE(run, nrun);
    COPY_TABLE(link, nlink); COPY_TABLE(anchor, nanchor);
    COPY_TABLE(image, nimage);
#undef COPY_TABLE
    doc->title = old->title;
    doc->full = old->full;
    doc->notices = old->notices;
    doc->image_fn = old->image_fn; doc->image_ctx = old->image_ctx;
    return doc;
}

int tiku_html_document_reflow(tiku_doc_t **current, int width,
                                const tiku_html_options_t *options)
{
    if (!current || !*current || !html_allocator_valid(options)) return 0;
    tiku_html_options_t o = html_options(options);
    for (;;) {
        tiku_doc_t *doc = *current;
        (void)tiku_doc_layout(doc, width);
        if (!(doc->full & (TIKU_DOC_FULL_LINES | TIKU_DOC_FULL_PIECES))) return 1;
        tiku_doc_t *grown = html_grow_layout(doc,&o);
        /* Without room to grow, the page keeps its capacity notice. */
        if (!grown) return 0;
        *current = grown;
        tiku_html_document_free(doc,&o);
    }
}
