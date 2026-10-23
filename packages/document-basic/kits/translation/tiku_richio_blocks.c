/*
 * TikuDesktop Translation Kit.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_richio_blocks.c - bounded structural table serialization.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richio_private.h"
#include <stdlib.h>
#include <string.h>

int
tiku_rio_has_headers(const tiku_richsnapshot_t *snapshot)
{
    tiku_rich_block_t node;

    for (size_t i = 0; i < tiku_richsnapshot_blocks(snapshot); i++) {
        tiku_richsnapshot_block(snapshot, i, &node);
        if (node.table.header_rows) return 1;
    }
    return 0;
}

int
tiku_rio_has_shading(const tiku_richsnapshot_t *snapshot)
{
    tiku_rich_block_t node;

    for (size_t i = 0; i < tiku_richsnapshot_blocks(snapshot); i++) {
        tiku_richsnapshot_block(snapshot, i, &node);
        if (node.background) return 1;
    }
    return 0;
}

int
tiku_rio_has_columns(const tiku_richsnapshot_t *snapshot)
{
    tiku_rich_block_t node;

    for (size_t i = 0; i < tiku_richsnapshot_blocks(snapshot); i++) {
        tiku_richsnapshot_block(snapshot, i, &node);
        if (node.kind == TIKU_RICH_BLOCK_TABLE && node.table.weights[0])
            return 1;
    }
    return 0;
}

void
tiku_rio_write_blocks(tiku_rio_writer_t *w,
                       const tiku_richsnapshot_t *snapshot)
{
    uint64_t ids[16];
    size_t indices[16], depth = 0, paragraph = 0;
    tiku_rich_block_t node;

    tiku_rio_literal(w, "<structure>\n");
    for (size_t i = 0; i < tiku_richsnapshot_blocks(snapshot) &&
         w->status == TIKU_RICHIO_OK; i++) {
        if (w->options.cancel && w->options.cancel(w->options.ctx)) {
            w->status = TIKU_RICHIO_CANCELLED;
            break;
        }
        tiku_richsnapshot_block(snapshot, i, &node);
        while (depth && ids[depth - 1] != node.parent) depth--;
        tiku_rio_literal(w, "<node");
        tiku_rio_uint(w, "kind", node.kind);
        tiku_rio_uint(w, "parent", depth ? indices[depth - 1] : 0);
        tiku_rio_uint(w, "paragraph",
            node.kind == TIKU_RICH_BLOCK_PARAGRAPH ? paragraph++ : 0);
        tiku_rio_uint(w, "rows", node.table.rows);
        tiku_rio_uint(w, "columns", node.table.columns);
        tiku_rio_write_float(w, "width-f32", node.table.width);
        tiku_rio_write_float(w, "padding-f32", node.table.padding);
        tiku_rio_write_float(w, "border-f32", node.table.border);
        tiku_rio_uint(w, "color", node.table.color);
        if (node.table.weights[0] || node.table.header_rows) {
            tiku_rio_literal(w, ">\n");
            for (unsigned j = 0; node.table.weights[0] &&
                 j < node.table.columns; j++) {
                tiku_rio_literal(w, "<column");
                tiku_rio_write_float(w, "weight-f32", node.table.weights[j]);
                tiku_rio_literal(w, "/>\n");
            }
            if (node.table.header_rows) {
                tiku_rio_literal(w, "<header");
                tiku_rio_uint(w, "rows", node.table.header_rows);
                tiku_rio_literal(w, "/>\n");
            }
            tiku_rio_literal(w, "</node>\n");
        } else if (node.background) {
            tiku_rio_literal(w, "><fill");
            tiku_rio_uint(w, "color", node.background);
            tiku_rio_literal(w, "/></node>\n");
        } else tiku_rio_literal(w, "/>\n");
        if (node.kind != TIKU_RICH_BLOCK_PARAGRAPH) {
            if (depth == 16) { w->status = TIKU_RICHIO_LIMIT; break; }
            ids[depth] = node.id;
            indices[depth++] = i + 1;
        }
    }
    tiku_rio_literal(w, "</structure>\n");
}

/** @brief Decode column weights followed by an optional header-row count. */
static void
read_columns(tiku_rio_reader_t *r, const tiku_xml_node_t *node,
             tiku_rich_block_source_t *to)
{
    static const char *const attrs[] = {"weight-f32"};
    static const char *const header_attrs[] = {"rows"};
    const tiku_xml_node_t *n = tiku_rio_next(r, node->child);
    unsigned count = 0;

    if (!n) return;
    if (to->kind != TIKU_RICH_BLOCK_TABLE) {
        r->status = TIKU_RICHIO_UNSUPPORTED;
        return;
    }
    while (n && r->status == TIKU_RICHIO_OK &&
           !strcmp(n->name, "column")) {
        if (!r->column_feature) {
            r->status = TIKU_RICHIO_UNSUPPORTED;
            return;
        }
        if (count >= to->table.columns) {
            r->status = TIKU_RICHIO_DAMAGED;
            return;
        }
        if (!tiku_rio_element(r, n, "column", attrs, 1)) return;
        to->table.weights[count] = tiku_rio_read_float(r, n, "weight-f32");
        if (!(to->table.weights[count++] > 0))
            r->status = TIKU_RICHIO_DAMAGED;
        if (tiku_rio_next(r, n->child)) r->status = TIKU_RICHIO_UNSUPPORTED;
        n = tiku_rio_next(r, n->next);
    }
    if (r->status == TIKU_RICHIO_OK && count && count != to->table.columns)
        r->status = TIKU_RICHIO_DAMAGED;
    if (!n || r->status != TIKU_RICHIO_OK) return;
    if (!r->header_feature) { r->status = TIKU_RICHIO_UNSUPPORTED; return; }
    if (!tiku_rio_element(r, n, "header", header_attrs, 1)) return;
    to->table.header_rows = (unsigned)tiku_rio_number(r, n, "rows",
                                                      to->table.rows);
    if (!to->table.header_rows) r->status = TIKU_RICHIO_DAMAGED;
    if (tiku_rio_next(r, n->child) || tiku_rio_next(r, n->next))
        r->status = TIKU_RICHIO_UNSUPPORTED;
}

/** @brief Read a single declared cell fill and reject extra children. */
static void
read_fill(tiku_rio_reader_t *r, const tiku_xml_node_t *node,
          tiku_rich_block_source_t *to)
{
    static const char *const attrs[] = {"color"};
    const tiku_xml_node_t *n = tiku_rio_next(r, node->child);

    if (!n) return;
    if (!r->shading_feature) { r->status = TIKU_RICHIO_UNSUPPORTED; return; }
    if (!tiku_rio_element(r, n, "fill", attrs, 1)) return;
    to->background = (uint32_t)tiku_rio_number(r, n, "color", UINT32_MAX);
    if (tiku_rio_next(r, n->child) || tiku_rio_next(r, n->next))
        r->status = TIKU_RICHIO_UNSUPPORTED;
}

void
tiku_rio_read_blocks(tiku_rio_reader_t *r, const tiku_xml_node_t *node)
{
    static const char *const attrs[] = {"kind", "parent", "paragraph",
        "rows", "columns", "width-f32", "padding-f32", "border-f32", "color"};
    const tiku_xml_node_t *n;
    size_t count = 0;
    tiku_rich_block_source_t *to;

    if (!tiku_rio_element(r, node, "structure", NULL, 0)) return;
    for (n = tiku_rio_next(r, node->child); n && r->status == TIKU_RICHIO_OK;
         n = tiku_rio_next(r, n->next)) {
        if (count++ == 65536) { r->status = TIKU_RICHIO_LIMIT; return; }
    }
    if (r->status != TIKU_RICHIO_OK) return;
    if (!count) { r->status = TIKU_RICHIO_DAMAGED; return; }
    if (count > (r->options.memory_bytes / 2 - r->used) / sizeof *to) {
        r->status = TIKU_RICHIO_LIMIT;
        return;
    }
    r->blocks = calloc(count, sizeof *r->blocks);
    if (!r->blocks) { r->status = TIKU_RICHIO_NO_MEMORY; return; }
    r->used += count * sizeof *r->blocks;
    r->source.blocks = r->blocks;
    r->source.block_count = count;
    n = tiku_rio_next(r, node->child);
    for (size_t i = 0; i < count && r->status == TIKU_RICHIO_OK; i++) {
        if (r->options.cancel && r->options.cancel(r->options.ctx)) {
            r->status = TIKU_RICHIO_CANCELLED;
            return;
        }
        if (!tiku_rio_element(r, n, "node", attrs, 9)) return;
        to = &r->blocks[i];
        to->kind = (tiku_rich_block_kind_t)tiku_rio_number(r, n, "kind", 2);
        to->parent = tiku_rio_number(r, n, "parent", i);
        to->paragraph = tiku_rio_number(r, n, "paragraph",
                                        r->source.paragraph_count);
        to->table.rows = (unsigned)tiku_rio_number(r, n, "rows", 256);
        to->table.columns = (unsigned)tiku_rio_number(r, n, "columns", 64);
        to->table.width = tiku_rio_read_float(r, n, "width-f32");
        to->table.padding = tiku_rio_read_float(r, n, "padding-f32");
        to->table.border = tiku_rio_read_float(r, n, "border-f32");
        to->table.color = (uint32_t)tiku_rio_number(r, n, "color", UINT32_MAX);
        if (to->kind == TIKU_RICH_BLOCK_CELL) read_fill(r, n, to);
        else read_columns(r, n, to);
        n = tiku_rio_next(r, n->next);
    }
}
