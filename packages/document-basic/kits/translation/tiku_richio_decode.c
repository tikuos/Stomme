/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_richio_decode.c - checked schema tree to public model import data.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richio_private.h"
#include <stdlib.h>
#include <string.h>

/** @brief Check the schema and reject required features outside version 1. */
static int
schema(tiku_rio_reader_t *r, const tiku_xml_node_t *root)
{
    static const char *const attrs[] = {"version", "required"};
    static const char *const features[] = {"paragraphs", "styles",
        "lists", "links", "page", "page-breaks", "inline-images", "tables",
        "table-columns", "cell-shading", "table-headers", "image-references",
        "outline-levels", "bookmarks"};
    const char *value;
    size_t n, i;
    unsigned seen = 0;

    if (!tiku_rio_element(r, root, "tiku-richdoc", attrs, 2)) return 0;
    if (strcmp(tiku_xml_attr(root, "version"), "1")) {
        r->status = TIKU_RICHIO_UNSUPPORTED;
        return 0;
    }
    value = tiku_xml_attr(root, "required");
    while (*value) {
        value += strspn(value, " ");
        n = strcspn(value, " ");
        if (!n) break;
        for (i = 0; i < sizeof features / sizeof *features; i++)
            if (n == strlen(features[i]) && !memcmp(value, features[i], n))
                break;
        if (i == sizeof features / sizeof *features) {
            r->status = TIKU_RICHIO_UNSUPPORTED;
            return 0;
        }
        if (seen & (1u << i)) { r->status = TIKU_RICHIO_DAMAGED; return 0; }
        seen |= 1u << i;
        value += n;
    }
    if ((seen & 15) != 15) { r->status = TIKU_RICHIO_DAMAGED; return 0; }
    r->page_feature = !!(seen & 16);
    r->break_feature = !!(seen & 32);
    r->image_feature = !!(seen & 64);
    r->table_feature = !!(seen & 128);
    r->column_feature = !!(seen & 256);
    r->shading_feature = !!(seen & 512);
    r->header_feature = !!(seen & 1024);
    r->reference_feature = !!(seen & 2048);
    r->outline_feature = !!(seen & 4096);
    r->bookmark_feature = !!(seen & 8192);
    if (r->reference_feature && !r->image_feature) {
        r->status = TIKU_RICHIO_DAMAGED;
        return 0;
    }
    if ((r->column_feature || r->shading_feature || r->header_feature) &&
        !r->table_feature) {
        r->status = TIKU_RICHIO_DAMAGED;
        return 0;
    }
    return 1;
}

/** @brief Allocate zeroed staging within half of the scratch budget. */
static void *
reserve(tiku_rio_reader_t *r, size_t count, size_t size)
{
    void *p;

    if (r->status != TIKU_RICHIO_OK || !count) return NULL;
    if (count > (r->options.memory_bytes / 2 - r->used) / size) {
        r->status = TIKU_RICHIO_LIMIT;
        return NULL;
    }
    p = calloc(count, size);
    if (!p) r->status = TIKU_RICHIO_NO_MEMORY;
    else r->used += count * size;
    return p;
}

/** @brief Count direct child elements within a fixed schema ceiling. */
static size_t
children(tiku_rio_reader_t *r, const tiku_xml_node_t *parent, size_t limit)
{
    const tiku_xml_node_t *n = tiku_rio_next(r, parent->child);
    size_t count = 0;

    while (n && r->status == TIKU_RICHIO_OK) {
        if (count == limit) { r->status = TIKU_RICHIO_LIMIT; return 0; }
        count++;
        n = tiku_rio_next(r, n->next);
    }
    return count;
}

/** @brief Count text and link bytes and runs before allocating their arrays. */
static void
prepare(tiku_rio_reader_t *r, const tiku_xml_node_t *styles,
        const tiku_xml_node_t *body)
{
    const tiku_xml_node_t *p, *n, *runs, *text;
    size_t bytes = 0, count = 0;

    r->source.style_count = children(r, styles, 4096);
    r->source.paragraph_count = children(r, body, 250000);
    if (!r->source.paragraph_count && r->status == TIKU_RICHIO_OK)
        r->status = TIKU_RICHIO_DAMAGED;
    for (p = body->child; p && r->status == TIKU_RICHIO_OK; p = p->next) {
        if (!p->name) continue;
        text = tiku_xml_child(p, "text");
        runs = tiku_xml_child(p, "runs");
        if (!text || !runs) { r->status = TIKU_RICHIO_DAMAGED; break; }
        bytes++;
        for (n = text->child; n; n = n->next)
            bytes += strlen(n->text);
        count += children(r, runs, 1000000 - count);
        for (n = runs->child; n; n = n->next) {
            const char *link = tiku_xml_attr(n, "link");

            if (link) bytes += strlen(link) + 1;
            if (tiku_xml_child(n, "image")) r->nimages++;
        }
        if (bytes > r->options.memory_bytes / 2) {
            r->status = TIKU_RICHIO_LIMIT;
            break;
        }
    }
    r->styles = reserve(r, r->source.style_count, sizeof *r->styles);
    r->paragraphs = reserve(r, r->source.paragraph_count,
                            sizeof *r->paragraphs);
    r->runs = reserve(r, count, sizeof *r->runs);
    r->images = reserve(r, r->nimages, sizeof *r->images);
    r->text = reserve(r, bytes, 1);
    r->source.styles = r->styles;
    r->source.paragraphs = r->paragraphs;
}

/** @brief Decode required character and paragraph children in order. */
static const tiku_xml_node_t *
properties(tiku_rio_reader_t *r, const tiku_xml_node_t *node,
           tiku_rich_chars_t *chars, tiku_rich_para_t *paragraph)
{
    const tiku_xml_node_t *n = tiku_rio_next(r, node->child);

    tiku_rio_read_chars(r, n, chars);
    if (r->status != TIKU_RICHIO_OK) return NULL;
    n = tiku_rio_next(r, n->next);
    tiku_rio_read_para(r, n, paragraph);
    if (r->status != TIKU_RICHIO_OK) return NULL;
    return tiku_rio_next(r, n->next);
}

/** @brief Decode styles with consecutive unique IDs and source references. */
static void
read_styles(tiku_rio_reader_t *r, const tiku_xml_node_t *container)
{
    static const char *const attrs[] = {"id", "kind", "parent", "name"};
    const tiku_xml_node_t *n = tiku_rio_next(r, container->child);
    size_t i;

    for (i = 0; i < r->source.style_count && r->status == TIKU_RICHIO_OK;
         i++, n = tiku_rio_next(r, n->next)) {
        tiku_rich_style_source_t *s = &r->styles[i];

        if (!tiku_rio_element(r, n, "style", attrs, 4)) return;
        if (tiku_rio_number(r, n, "id", 4096) != i + 1) {
            r->status = TIKU_RICHIO_DAMAGED;
            return;
        }
        s->kind = (tiku_rich_style_kind_t)tiku_rio_number(r, n, "kind", 1);
        s->parent = tiku_rio_number(r, n, "parent", r->source.style_count);
        tiku_rio_string(r, tiku_xml_attr(n, "name"), s->name, sizeof s->name);
        if (properties(r, n, &s->chars, &s->paragraph))
            r->status = TIKU_RICHIO_UNSUPPORTED;
    }
}

/** @brief Join text nodes, decoding schema escapes into preallocated space. */
static size_t
read_text(tiku_rio_reader_t *r, const tiku_xml_node_t *n, char **at)
{
    size_t bytes = 0, count;

    if (!tiku_rio_element(r, n, "text", NULL, 0)) return 0;
    for (n = n->child; n && r->status == TIKU_RICHIO_OK; n = n->next) {
        if (n->name) { r->status = TIKU_RICHIO_UNSUPPORTED; break; }
        count = tiku_rio_string(r, n->text, *at + bytes,
                               strlen(n->text) + 1);
        bytes += count;
    }
    (*at)[bytes] = 0;
    *at += bytes + 1;
    return bytes;
}

/** @brief Decode run boundaries, style references, resets and link targets. */
static void
read_runs(tiku_rio_reader_t *r, const tiku_xml_node_t *n,
          tiku_rich_paragraph_source_t *p, size_t *run_at, char **text_at)
{
    static const char *const attrs[] = {"bytes", "style", "reset", "link"};
    size_t sum = 0;
    const tiku_xml_node_t *c;

    if (!tiku_rio_element(r, n, "runs", NULL, 0)) return;
    p->runs = r->runs ? r->runs + *run_at : NULL;
    for (n = tiku_rio_next(r, n->child); n && r->status == TIKU_RICHIO_OK;
         n = tiku_rio_next(r, n->next)) {
        tiku_rich_run_source_t *run = &r->runs[(*run_at)++];
        const char *link;
        size_t length;

        if (!tiku_rio_element(r, n, "run", attrs, 4)) return;
        run->length = tiku_rio_number(r, n, "bytes", p->length - sum);
        run->style = tiku_rio_number(r, n, "style", r->source.style_count);
        run->reset = (unsigned)tiku_rio_number(r, n, "reset", 511);
        sum += run->length;
        link = tiku_xml_attr(n, "link");
        run->link = *text_at;
        length = tiku_rio_string(r, link, *text_at, strlen(link) + 1);
        *text_at += length + 1;
        c = tiku_rio_next(r, n->child);
        tiku_rio_read_chars(r, c, &run->chars);
        if (r->status != TIKU_RICHIO_OK) return;
        c = tiku_rio_next(r, c->next);
        if (c) {
            run->image = tiku_rio_read_image(r, c);
            if (r->status != TIKU_RICHIO_OK) return;
            if (tiku_rio_next(r, c->next)) r->status = TIKU_RICHIO_UNSUPPORTED;
        }
        p->run_count++;
    }
    if (sum != p->length) r->status = TIKU_RICHIO_DAMAGED;
}

/** @brief Decode the main text body using preallocated import arrays. */
static void
read_body(tiku_rio_reader_t *r, const tiku_xml_node_t *container)
{
    static const char *const attrs[] = {"style", "list", "level", "restart"};
    const tiku_xml_node_t *n = tiku_rio_next(r, container->child), *c;
    size_t i, run_at = 0;
    char *text_at = r->text;

    for (i = 0; i < r->source.paragraph_count && r->status == TIKU_RICHIO_OK;
         i++, n = tiku_rio_next(r, n->next)) {
        tiku_rich_paragraph_source_t *p = &r->paragraphs[i];

        if (!tiku_rio_element(r, n, "p", attrs, 4)) return;
        p->style = tiku_rio_number(r, n, "style", r->source.style_count);
        p->list.kind = (tiku_rich_list_kind_t)
            tiku_rio_number(r, n, "list", 2);
        p->list.level = (unsigned)tiku_rio_number(r, n, "level", 8);
        p->list.restart = (uint32_t)tiku_rio_number(r, n, "restart",
                                                  UINT32_MAX);
        if (p->list.kind == TIKU_RICH_LIST_NONE &&
            (p->list.level || p->list.restart))
            r->status = TIKU_RICHIO_DAMAGED;
        c = properties(r, n, &p->chars, &p->paragraph);
        if (r->status != TIKU_RICHIO_OK) return;
        p->text = text_at;
        p->length = read_text(r, c, &text_at);
        if (r->status != TIKU_RICHIO_OK) return;
        c = tiku_rio_next(r, c->next);
        read_runs(r, c, p, &run_at, &text_at);
        if (r->status != TIKU_RICHIO_OK) return;
        if (tiku_rio_next(r, c->next)) r->status = TIKU_RICHIO_UNSUPPORTED;
    }
}

tiku_richio_status_t
tiku_rio_decode(tiku_rio_reader_t *r, const tiku_xml_node_t *root,
    tiku_richdoc_t **out)
{
    const tiku_xml_node_t *styles, *body, *structure;
    tiku_richdoc_t *doc = NULL;

    if (!schema(r, root)) return r->status;
    styles = tiku_rio_next(r, root->child);
    if (r->page_feature) {
        tiku_rio_read_page(r, styles);
        if (r->status != TIKU_RICHIO_OK) return r->status;
        styles = tiku_rio_next(r, styles->next);
    }
    if (!tiku_rio_element(r, styles, "styles", NULL, 0)) return r->status;
    body = tiku_rio_next(r, styles->next);
    if (!tiku_rio_element(r, body, "body", NULL, 0)) return r->status;
    structure = tiku_rio_next(r, body->next);
    prepare(r, styles, body);
    if (r->status == TIKU_RICHIO_OK) read_styles(r, styles);
    if (r->status == TIKU_RICHIO_OK) read_body(r, body);
    if (r->status == TIKU_RICHIO_OK && r->table_feature) {
        tiku_rio_read_blocks(r, structure);
        if (r->status == TIKU_RICHIO_OK)
            structure = tiku_rio_next(r, structure->next);
    }
    if (r->status == TIKU_RICHIO_OK && r->bookmark_feature) {
        tiku_rio_read_bookmarks(r, structure);
        if (r->status == TIKU_RICHIO_OK)
            structure = tiku_rio_next(r, structure->next);
    }
    if (r->status == TIKU_RICHIO_OK && structure)
        r->status = TIKU_RICHIO_UNSUPPORTED;
    if (r->status == TIKU_RICHIO_OK)
        r->status = tiku_rio_model(tiku_richdoc_create(&r->options.document,
                                                     &doc));
    if (r->status == TIKU_RICHIO_OK)
        r->status = tiku_rio_model(tiku_richdoc_import(doc, &r->source, 0));
    free(r->styles);
    free(r->paragraphs);
    free(r->runs);
    free(r->text);
    free(r->blocks);
    free(r->bookmarks);
    for (size_t i = 0; i < r->image_at; i++)
        if (!r->images[i].borrowed) free((void *)r->images[i].image.pixels);
    free(r->images);
    if (r->status != TIKU_RICHIO_OK) tiku_richdoc_release(doc);
    else *out = doc;
    return r->status;
}
