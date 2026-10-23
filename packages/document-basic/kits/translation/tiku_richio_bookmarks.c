/*
 * TikuDesktop Translation Kit.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_richio_bookmarks.c - bounded native bookmark persistence.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richio_private.h"
#include <stdlib.h>
#include <string.h>

void
tiku_rio_write_bookmarks(tiku_rio_writer_t *w,
    const tiku_richsnapshot_t *snapshot)
{
    tiku_rich_bookmark_t bookmark;
    tiku_rich_paragraph_t paragraph;
    size_t i, at, count = tiku_richsnapshot_bookmarks(snapshot);

    tiku_rio_literal(w, "<bookmarks>\n");
    for (i = 0; i < count && w->status == TIKU_RICHIO_OK; i++) {
        tiku_richsnapshot_bookmark(snapshot, i, &bookmark);
        for (at = 0; at < tiku_richsnapshot_paragraphs(snapshot); at++) {
            if (w->options.cancel && w->options.cancel(w->options.ctx)) {
                w->status = TIKU_RICHIO_CANCELLED;
                return;
            }
            tiku_richsnapshot_paragraph(snapshot, at, &paragraph);
            if (paragraph.id == bookmark.position.paragraph) break;
        }
        if (at == tiku_richsnapshot_paragraphs(snapshot)) {
            w->status = TIKU_RICHIO_DAMAGED;
            return;
        }
        tiku_rio_literal(w, "<bookmark name=\"");
        tiku_rio_escape(w, bookmark.name, strlen(bookmark.name));
        tiku_rio_literal(w, "\"");
        tiku_rio_uint(w, "paragraph", at);
        tiku_rio_uint(w, "byte", bookmark.position.byte);
        tiku_rio_uint(w, "after", bookmark.position.affinity == TIKU_RICH_AFTER);
        tiku_rio_literal(w, "/>\n");
    }
    tiku_rio_literal(w, "</bookmarks>\n");
}

void
tiku_rio_read_bookmarks(tiku_rio_reader_t *r, const tiku_xml_node_t *node)
{
    static const char *const attrs[] = {"name", "paragraph", "byte", "after"};
    const tiku_xml_node_t *n;
    size_t count = 0, i;
    tiku_rich_bookmark_source_t *b;

    if (!tiku_rio_element(r, node, "bookmarks", NULL, 0)) return;
    for (n = tiku_rio_next(r, node->child); n && r->status == TIKU_RICHIO_OK;
         n = tiku_rio_next(r, n->next)) {
        if (++count > TIKU_RICH_BOOKMARKS_MAX) {
            r->status = TIKU_RICHIO_LIMIT;
            return;
        }
        if (r->options.cancel && r->options.cancel(r->options.ctx)) {
            r->status = TIKU_RICHIO_CANCELLED;
            return;
        }
    }
    if (r->status != TIKU_RICHIO_OK) return;
    if (!count) { r->status = TIKU_RICHIO_DAMAGED; return; }
    if (count > TIKU_RICH_BOOKMARKS_MAX || count >
        (r->options.memory_bytes / 2 - r->used) / sizeof *b) {
        r->status = TIKU_RICHIO_LIMIT;
        return;
    }
    r->bookmarks = calloc(count, sizeof *b);
    if (!r->bookmarks) { r->status = TIKU_RICHIO_NO_MEMORY; return; }
    r->used += count * sizeof *b;
    r->source.bookmarks = r->bookmarks;
    r->source.bookmark_count = count;
    n = tiku_rio_next(r, node->child);
    for (i = 0; i < count && r->status == TIKU_RICHIO_OK; i++) {
        if (r->options.cancel && r->options.cancel(r->options.ctx)) {
            r->status = TIKU_RICHIO_CANCELLED;
            return;
        }
        if (!tiku_rio_element(r, n, "bookmark", attrs, 4)) return;
        b = &r->bookmarks[i];
        tiku_rio_string(r, tiku_xml_attr(n, "name"), b->name, sizeof b->name);
        b->paragraph = tiku_rio_number(r, n, "paragraph", r->source.paragraph_count - 1);
        b->byte = tiku_rio_number(r, n, "byte", SIZE_MAX);
        b->after = (unsigned)tiku_rio_number(r, n, "after", 1);
        if (tiku_rio_next(r, n->child)) r->status = TIKU_RICHIO_UNSUPPORTED;
        n = tiku_rio_next(r, n->next);
    }
}
