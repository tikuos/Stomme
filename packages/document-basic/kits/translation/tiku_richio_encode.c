/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_richio_encode.c - semantic snapshot serialization to schema-1 XML.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richio_private.h"
#include <stdlib.h>
#include <string.h>

/** @brief Sort style mappings by immutable model ID. */
static int
style_order(const void *a, const void *b)
{
    uint64_t x = ((const tiku_rio_style_t *)a)->id;
    uint64_t y = ((const tiku_rio_style_t *)b)->id;

    return (x > y) - (x < y);
}

/** @brief Map an ID to a source index; zero denotes an absent reference. */
static size_t
style_index(tiku_rio_writer_t *w, uint64_t id)
{
    tiku_rio_style_t key = {.id = id}, *found;

    if (!id) return 0;
    found = bsearch(&key, w->styles, w->count, sizeof key, style_order);
    if (!found) { w->status = TIKU_RICHIO_DAMAGED; return 0; }
    return found->index;
}

/** @brief Emit named styles without flattening their inheritance. */
static void
write_styles(tiku_rio_writer_t *w, const tiku_richsnapshot_t *snapshot)
{
    size_t i;
    tiku_rich_style_t style;

    tiku_rio_literal(w, "<styles>\n");
    for (i = 0; i < w->count && w->status == TIKU_RICHIO_OK; i++) {
        if (tiku_richsnapshot_style(snapshot, i, &style) != TIKU_RICH_OK) {
            w->status = TIKU_RICHIO_DAMAGED;
            return;
        }
        tiku_rio_literal(w, "<style");
        tiku_rio_uint(w, "id", i + 1);
        tiku_rio_uint(w, "kind", style.kind);
        tiku_rio_uint(w, "parent", style_index(w, style.parent));
        tiku_rio_literal(w, " name=\"");
        tiku_rio_escape(w, style.name, strlen(style.name));
        tiku_rio_literal(w, "\">");
        tiku_rio_write_chars(w, &style.chars);
        tiku_rio_write_para(w, &style.paragraph);
        tiku_rio_literal(w, "</style>\n");
    }
    tiku_rio_literal(w, "</styles>\n");
}

/** @brief Emit complete text and run boundaries for one paragraph. */
static void
write_paragraph(tiku_rio_writer_t *w, const tiku_rich_paragraph_t *p)
{
    size_t i;

    if (w->options.cancel && w->options.cancel(w->options.ctx)) {
        w->status = TIKU_RICHIO_CANCELLED;
        return;
    }
    tiku_rio_literal(w, "<p");
    tiku_rio_uint(w, "style", style_index(w, p->style));
    tiku_rio_uint(w, "list", p->list.kind);
    tiku_rio_uint(w, "level", p->list.level);
    tiku_rio_uint(w, "restart", p->list.restart);
    tiku_rio_literal(w, ">");
    tiku_rio_write_chars(w, &p->chars);
    tiku_rio_write_para(w, &p->paragraph);
    tiku_rio_literal(w, "<text>");
    tiku_rio_escape(w, p->text, p->length);
    tiku_rio_literal(w, "</text><runs>");
    for (i = 0; i < p->run_count && w->status == TIKU_RICHIO_OK; i++) {
        const tiku_rich_run_t *run = &p->runs[i];

        tiku_rio_literal(w, "<run");
        tiku_rio_uint(w, "bytes", run->length);
        tiku_rio_uint(w, "style", style_index(w, run->style));
        tiku_rio_uint(w, "reset", run->reset);
        tiku_rio_literal(w, " link=\"");
        if (run->link) tiku_rio_escape(w, run->link, strlen(run->link));
        tiku_rio_literal(w, "\">");
        tiku_rio_write_chars(w, &run->chars);
        if (run->image) tiku_rio_write_image(w, run->image);
        tiku_rio_literal(w, "</run>");
    }
    tiku_rio_literal(w, "</runs></p>\n");
}

/** @brief Detect an explicit paragraph-property group in paragraphs or styles. */
static int
has_property(const tiku_richsnapshot_t *snapshot, unsigned property)
{
    tiku_rich_paragraph_t paragraph;
    tiku_rich_style_t style;

    for (size_t i = 0; i < tiku_richsnapshot_styles(snapshot); i++) {
        tiku_richsnapshot_style(snapshot, i, &style);
        if (style.paragraph.present & property) return 1;
    }
    for (size_t i = 0; i < tiku_richsnapshot_paragraphs(snapshot); i++) {
        tiku_richsnapshot_paragraph(snapshot, i, &paragraph);
        if (paragraph.paragraph.present & property) return 1;
    }
    return 0;
}

/** @brief Emit one complete XML document through the counting/writing sink. */
static void
write_document(tiku_rio_writer_t *w, const tiku_richsnapshot_t *snapshot)
{
    size_t i, count = tiku_richsnapshot_paragraphs(snapshot);
    size_t runs = 0;
    tiku_rich_paragraph_t p;
    tiku_rich_page_setup_t page, defaults;
    const tiku_rich_page_setup_t zero = {0};
    int custom_page;

    w->image_at = 0;
    if (count > 250000) { w->status = TIKU_RICHIO_LIMIT; return; }
    tiku_richsnapshot_page(snapshot, &page);
    tiku_rich_page_setup(&zero, &defaults);
    custom_page = page.width != defaults.width ||
        page.height != defaults.height ||
        page.top != defaults.top || page.right != defaults.right ||
        page.bottom != defaults.bottom || page.left != defaults.left;
    tiku_rio_literal(w, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<tiku-richdoc version=\"1\" "
        "required=\"paragraphs styles lists links");
    w->break_feature = has_property(snapshot, TIKU_RICH_PAGE_BREAK);
    w->outline_feature = has_property(snapshot, TIKU_RICH_OUTLINE);
    if (custom_page) tiku_rio_literal(w, " page");
    if (w->break_feature) tiku_rio_literal(w, " page-breaks");
    if (w->outline_feature) tiku_rio_literal(w, " outline-levels");
    if (tiku_richsnapshot_bookmarks(snapshot)) tiku_rio_literal(w, " bookmarks");
    w->image_feature = w->image_count != 0;
    if (w->image_feature) tiku_rio_literal(w, " inline-images");
    if (w->reference_feature) tiku_rio_literal(w, " image-references");
    if (tiku_richsnapshot_blocks(snapshot) > count)
        tiku_rio_literal(w, " tables");
    if (tiku_rio_has_columns(snapshot)) tiku_rio_literal(w, " table-columns");
    if (tiku_rio_has_shading(snapshot)) tiku_rio_literal(w, " cell-shading");
    if (tiku_rio_has_headers(snapshot)) tiku_rio_literal(w, " table-headers");
    tiku_rio_literal(w, "\">\n");
    if (custom_page) tiku_rio_write_page(w, &page);
    write_styles(w, snapshot);
    tiku_rio_literal(w, "<body>\n");
    for (i = 0; i < count && w->status == TIKU_RICHIO_OK; i++) {
        if (tiku_richsnapshot_paragraph(snapshot, i, &p) != TIKU_RICH_OK) {
            w->status = TIKU_RICHIO_DAMAGED;
            break;
        }
        if (p.run_count > 1000000 - runs) {
            w->status = TIKU_RICHIO_LIMIT;
            break;
        }
        runs += p.run_count;
        write_paragraph(w, &p);
    }
    tiku_rio_literal(w, "</body>\n");
    if (tiku_richsnapshot_blocks(snapshot) > count)
        tiku_rio_write_blocks(w, snapshot);
    if (tiku_richsnapshot_bookmarks(snapshot)) tiku_rio_write_bookmarks(w, snapshot);
    tiku_rio_literal(w, "</tiku-richdoc>\n");
}

tiku_richio_status_t
tiku_richio_write_xml(const tiku_richsnapshot_t *snapshot,
    const tiku_richio_options_t *options, unsigned char **out, size_t *length)
{
    tiku_rio_writer_t w = {0};
    tiku_rich_style_t style;
    size_t i, needed;

    if (out) *out = NULL;
    if (length) *length = 0;
    if (!out || !length || !snapshot) return TIKU_RICHIO_INVALID;
    w.status = tiku_rio_options(options, &w.options);
    if (w.status != TIKU_RICHIO_OK) return w.status;
    w.count = tiku_richsnapshot_styles(snapshot);
    if (w.count > 4096 || w.count >
        w.options.memory_bytes / 2 / sizeof *w.styles)
        return TIKU_RICHIO_LIMIT;
    if (w.count) {
        w.styles = calloc(w.count, sizeof *w.styles);
        if (!w.styles) return TIKU_RICHIO_NO_MEMORY;
        for (i = 0; i < w.count; i++) {
            if (tiku_richsnapshot_style(snapshot, i, &style) != TIKU_RICH_OK) {
                free(w.styles);
                return TIKU_RICHIO_DAMAGED;
            }
            w.styles[i].id = style.id;
            w.styles[i].index = i + 1;
        }
        qsort(w.styles, w.count, sizeof *w.styles, style_order);
    }
    tiku_rio_prepare_images(&w, snapshot);
    if (w.status == TIKU_RICHIO_OK) write_document(&w, snapshot);
    needed = w.used;
    if (w.status == TIKU_RICHIO_OK) {
        w.data = malloc(needed + 1);
        if (!w.data) w.status = TIKU_RICHIO_NO_MEMORY;
        else {
            w.used = 0;
            write_document(&w, snapshot);
        }
    }
    free(w.styles);
    free(w.image_refs);
    if (w.status != TIKU_RICHIO_OK) free(w.data);
    else {
        w.data[w.used] = 0;
        *out = w.data;
        *length = w.used;
    }
    return w.status;
}
