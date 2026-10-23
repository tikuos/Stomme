/*
 * TikuDesktop Translation Kit.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_richhtml.c - two-pass HTML export of immutable document structure.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richhtml_private.h"
#include <stdlib.h>
#include <string.h>

/** @brief One linear traversal of blocks and paragraphs. */
typedef struct {
    tiku_rio_writer_t sink;       /**< shared bounded counting/output sink */
    const tiku_richsnapshot_t *snapshot; /**< borrowed immutable input */
    size_t block, paragraph;     /**< next depth-first source indices */
    unsigned notices;            /**< conversion differences */
} html_t;

/** @brief Open list elements and numbering within one body or cell. */
typedef struct {
    unsigned depth;              /**< number of open list/item pairs */
    tiku_rich_list_kind_t kind[9]; /**< open list type at each level */
    uint64_t number[9];           /**< last emitted ordinal at each level */
} lists_t;

/** @brief Close deeper list items and containers, resetting their counters. */
static void
close_lists(tiku_rio_writer_t *w, lists_t *lists, unsigned depth)
{
    while (lists->depth > depth) {
        unsigned at = --lists->depth;

        tiku_rio_literal(w, lists->kind[at] == TIKU_RICH_LIST_DECIMAL ?
                         "</li></ol>\n" : "</li></ul>\n");
        lists->kind[at] = TIKU_RICH_LIST_NONE;
        lists->number[at] = 0;
    }
}

/** @brief Begin a real list item, adding unmarked parents for skipped levels. */
static void
list_item(tiku_rio_writer_t *w, lists_t *lists, tiku_rich_list_t item)
{
    unsigned depth = item.level + 1;

    if (!item.kind) { close_lists(w, lists, 0); return; }
    close_lists(w, lists, depth);
    if (lists->depth == depth && lists->kind[item.level] != item.kind)
        close_lists(w, lists, item.level);
    if (lists->depth == depth) tiku_rio_literal(w, "</li>");
    while (lists->depth < depth) {
        unsigned at = lists->depth++;

        lists->kind[at] = item.kind;
        tiku_rio_literal(w, item.kind == TIKU_RICH_LIST_DECIMAL ?
                         "<ol>" : "<ul>");
        if (lists->depth < depth)
            tiku_rio_literal(w, "<li style=\"list-style:none\">");
    }
    tiku_rio_literal(w, "<li");
    if (item.kind == TIKU_RICH_LIST_DECIMAL) {
        if (item.restart) lists->number[item.level] = item.restart;
        else lists->number[item.level]++;
        tiku_rio_uint(w, "value", (size_t)lists->number[item.level]);
    }
    tiku_rio_literal(w, ">");
}

/** @brief Poll cancellation at each structural step. */
static int
active(html_t *h)
{
    if (h->sink.status == TIKU_RICHIO_OK && h->sink.options.cancel &&
        h->sink.options.cancel(h->sink.options.ctx))
        h->sink.status = TIKU_RICHIO_CANCELLED;
    return h->sink.status == TIKU_RICHIO_OK;
}

static void body(html_t *h, uint64_t parent, unsigned depth);

/** @brief Emit one cell with table-local geometry and its child blocks. */
static void
cell(html_t *h, const tiku_rich_table_t *table, unsigned depth, int header)
{
    tiku_rich_block_t node;
    tiku_rio_writer_t *w = &h->sink;

    if (!active(h)) return;
    tiku_richsnapshot_block(h->snapshot, h->block++, &node);
    tiku_rio_literal(w, header ? "<th style=\"" : "<td style=\"");
    tiku_rio_literal(w, "padding:");
    tiku_rhtml_number(w, table->padding);
    tiku_rio_literal(w, "pt;border:");
    tiku_rhtml_number(w, table->border);
    tiku_rio_literal(w, "pt solid ");
    tiku_rhtml_color(w, table->color);
    tiku_rio_literal(w, ";background-color:");
    tiku_rhtml_color(w, node.background);
    tiku_rio_literal(w, ";\">");
    body(h, node.id, depth);
    tiku_rio_literal(w, header ? "</th>" : "</td>");
}

/** @brief Emit one table, its column weights and its repeatable header group. */
static void
table(html_t *h, const tiku_rich_table_t *t, unsigned depth)
{
    tiku_rio_writer_t *w = &h->sink;

    if (depth > 8) { w->status = TIKU_RICHIO_LIMIT; return; }
    tiku_rio_literal(w, "<table style=\"width:");
    if (t->width) {
        tiku_rhtml_number(w, t->width);
        tiku_rio_literal(w, "pt");
    } else tiku_rio_literal(w, "100%");
    tiku_rio_literal(w, "\"><colgroup>");
    for (unsigned column = 0; column < t->columns; column++) {
        tiku_rio_literal(w, "<col style=\"width:");
        tiku_rhtml_number(w, tiku_rich_table_fraction(t, column) * 100);
        tiku_rio_literal(w, "%\">");
    }
    tiku_rio_literal(w, t->header_rows ? "</colgroup><thead>" :
                                                  "</colgroup><tbody>");
    for (unsigned row = 0; row < t->rows && active(h); row++) {
        if (row && row == t->header_rows)
            tiku_rio_literal(w, "</thead><tbody>");
        tiku_rio_literal(w, "<tr>");
        for (unsigned col = 0; col < t->columns && active(h); col++)
            cell(h, t, depth, row < t->header_rows);
        tiku_rio_literal(w, "</tr>\n");
    }
    tiku_rio_literal(w, t->header_rows == t->rows ?
                     "</thead></table>\n" : "</tbody></table>\n");
}

/** @brief Emit direct child blocks in order, bounded to eight nested tables. */
static void
body(html_t *h, uint64_t parent, unsigned depth)
{
    tiku_rich_block_t node;
    tiku_rich_paragraph_t paragraph;
    lists_t lists = {0};

    while (h->block < tiku_richsnapshot_blocks(h->snapshot) && active(h)) {
        tiku_richsnapshot_block(h->snapshot, h->block, &node);
        if (node.parent != parent) break;
        h->block++;
        if (node.kind == TIKU_RICH_BLOCK_PARAGRAPH) {
            tiku_richsnapshot_paragraph(h->snapshot, h->paragraph, &paragraph);
            list_item(&h->sink, &lists, paragraph.list);
            tiku_rhtml_paragraph(&h->sink, h->snapshot, h->paragraph++);
        } else if (node.kind == TIKU_RICH_BLOCK_TABLE) {
            close_lists(&h->sink, &lists, 0);
            table(h, &node.table, depth + 1);
        } else h->sink.status = TIKU_RICHIO_DAMAGED;
    }
    close_lists(&h->sink, &lists, 0);
}

/** @brief Emit print hints and a complete, resource-free HTML document. */
static void
document(html_t *h)
{
    tiku_rio_writer_t *w = &h->sink;
    tiku_rich_page_setup_t page;

    h->block = h->paragraph = 0;
    if (!active(h)) return;
    tiku_richsnapshot_page(h->snapshot, &page);
    tiku_rio_literal(w, "<!doctype html>\n<html><head><meta charset=\"utf-8\">"
        "<title>Document</title><style>\n"
        "body{font:12pt sans-serif;color:#000;background:#fff}"
        "p{white-space:pre-wrap;overflow-wrap:anywhere;margin:0;min-height:1em}"
        "ul,ol{margin-top:0;margin-bottom:0;padding-left:24pt}"
        "table{border-collapse:collapse;table-layout:fixed}"
        "td,th{vertical-align:top;font-weight:normal;text-align:left}"
        "thead{display:table-header-group}tr{break-inside:avoid}"
        "@page{size:");
    tiku_rhtml_number(w, page.width);
    tiku_rio_literal(w, "pt ");
    tiku_rhtml_number(w, page.height);
    tiku_rio_literal(w, "pt;margin:");
    const float margins[] = {page.top, page.right, page.bottom, page.left};

    for (size_t i = 0; i < 4; i++) {
        tiku_rhtml_number(w, margins[i]);
        tiku_rio_literal(w, "pt ");
    }
    tiku_rio_literal(w, ";}\n</style></head><body>\n");
    body(h, tiku_richsnapshot_body(h->snapshot), 0);
    tiku_rio_literal(w, "</body></html>\n");
    if (w->status == TIKU_RICHIO_OK &&
        (h->block != tiku_richsnapshot_blocks(h->snapshot) ||
         h->paragraph != tiku_richsnapshot_paragraphs(h->snapshot)))
        w->status = TIKU_RICHIO_DAMAGED;
}

tiku_richio_status_t
tiku_richhtml_write(const tiku_richsnapshot_t *snapshot,
    const tiku_richio_options_t *options, unsigned char **out, size_t *length,
    unsigned *notices)
{
    html_t h = {.snapshot = snapshot, .notices = TIKU_RICHHTML_PAGINATION};
    size_t needed;

    if (out) *out = NULL;
    if (length) *length = 0;
    if (notices) *notices = 0;
    if (!out || !length || !snapshot) return TIKU_RICHIO_INVALID;
    h.sink.status = tiku_rio_options(options, &h.sink.options);
    if (h.sink.status != TIKU_RICHIO_OK) return h.sink.status;
    if (tiku_richsnapshot_styles(snapshot)) h.notices |= TIKU_RICHHTML_STYLES;
    if (tiku_richsnapshot_bookmarks(snapshot)) h.notices |= TIKU_RICHHTML_BOOKMARKS;
    document(&h);
    needed = h.sink.used;
    if (h.sink.status != TIKU_RICHIO_OK) return h.sink.status;
    h.sink.data = malloc(needed + 1);
    if (!h.sink.data) return TIKU_RICHIO_NO_MEMORY;
    h.sink.used = 0;
    document(&h);
    if (h.sink.status != TIKU_RICHIO_OK) {
        free(h.sink.data);
        return h.sink.status;
    }
    h.sink.data[h.sink.used] = 0;
    *out = h.sink.data;
    *length = h.sink.used;
    if (notices) *notices = h.notices;
    return TIKU_RICHIO_OK;
}
