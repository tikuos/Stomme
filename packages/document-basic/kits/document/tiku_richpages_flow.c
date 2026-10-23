/*
 * TikuDesktop Document Kit.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_richpages_flow.c - charged page pieces and nested-cell traversal.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richpages_flow.h"
#include <string.h>

int
tiku_rp_piece_add(tiku_richpages_t *pages, tiku_rich_page_piece_t piece)
{
    tiku_rich_page_piece_t *next;
    size_t capacity;
    tiku_rich_status_t status = TIKU_RICH_OK;

    if (piece.kind == TIKU_RICH_PAGE_TEXT &&
        pages->pages[pages->count - 1].piece_count) {
        next = &pages->pieces[pages->npieces - 1];
        if (next->kind == piece.kind && next->source_top == piece.source_top &&
            next->first_line + next->line_count == piece.first_line) {
            next->line_count += piece.line_count;
            return 1;
        }
    }
    if (pages->npieces == 1000000) {
        pages->status = TIKU_RICH_LIMIT;
        return 0;
    }
    if (pages->npieces == pages->cpieces) {
        capacity = pages->cpieces ? pages->cpieces * 2 : 64;
        if (capacity > 1000000) capacity = 1000000;
        next = tiku_rd_alloc(pages->layout->source.memory,
                             capacity * sizeof *next, &status);
        if (!next) { pages->status = status; return 0; }
        if (pages->npieces)
            memcpy(next, pages->pieces, pages->npieces * sizeof *next);
        tiku_rd_free(pages->pieces);
        pages->pieces = next;
        pages->cpieces = capacity;
    }
    pages->pieces[pages->npieces++] = piece;
    pages->pages[pages->count - 1].piece_count++;
    return 1;
}

void
tiku_rp_flow_begin(tiku_richpages_t *pages)
{
    tiku_rich_status_t status = TIKU_RICH_OK;

    if (!pages->flow)
        pages->flow = tiku_rd_alloc(pages->layout->source.memory,
                                    sizeof *pages->flow, &status);
    if (pages->flow && !pages->flow->nodes)
        pages->flow->nodes = tiku_rd_alloc(pages->layout->source.memory,
            pages->layout->ngrids * sizeof *pages->flow->nodes, &status);
    if (status != TIKU_RICH_OK) { pages->status = status; return; }
    pages->table->phase = 4;
}

/** @brief Index one source grid and collect direct-cell header geometry. */
static void
index_grid(tiku_richpages_t *pages)
{
    tiku_rp_flow_t *flow = pages->flow;
    size_t index = flow->build++, owner, block;
    const tiku_rich_grid_box_t *box = &pages->layout->grids[index];
    tiku_rp_cursor_t *node = &flow->nodes[index], *parent;

    flow->parents[box->depth] = index + 1;
    owner = box->depth ? flow->parents[box->depth - 1] : 0;
    node->parent = owner;
    if (owner) {
        parent = &flow->nodes[owner - 1];
        if (parent->last) flow->nodes[parent->last - 1].next = index + 1;
        else parent->child = index + 1;
        parent->last = index + 1;
    } else parent = NULL;
    if (box->kind == TIKU_RICH_BLOCK_TABLE) {
        block = tiku_rd_block_find(pages->layout->source.root, box->id);
        node->props = &pages->layout->source.root->blocks->nodes[block].table;
    } else {
        node->props = parent->props;
        parent->cells++;
        if (parent->cells <= node->props->header_rows * node->props->columns) {
            parent->header_end = box->line + box->line_count;
            parent->header_height = box->box.y + box->box.height -
                                     pages->layout->grids[owner - 1].box.y;
        } else if (!parent->body) parent->body = index + 1;
    }
}

void
tiku_rp_flow_push(tiku_richpages_t *pages, unsigned kind, size_t node,
                  double top, double bottom)
{
    tiku_rp_flow_t *flow = pages->flow;

    if (flow->depth == 24) { pages->status = TIKU_RICH_LIMIT; return; }
    flow->frames[flow->depth++] = (tiku_rp_frame_t){
        .kind = kind, .node = node, .cursor = node, .top = top,
        .bottom = bottom, .y = top, .done = 1};
}

void
tiku_rp_flow_pop(tiku_richpages_t *pages, int done, double used, int progress)
{
    pages->flow->depth--;
    pages->flow->done = done;
    pages->flow->used = used;
    pages->flow->progress = progress;
}

int
tiku_rp_flow_range(tiku_richpages_t *pages, size_t first, size_t count,
                   double source, double target, unsigned depth)
{
    return tiku_rp_piece_add(pages, (tiku_rich_page_piece_t){
        .kind = TIKU_RICH_PAGE_RANGE, .first_line = first, .line_count = count,
        .source_top = source - target, .depth = depth});
}

/** @brief Start a continuation page with any complete outer table headers. */
static void
start_page(tiku_richpages_t *pages)
{
    tiku_rp_table_t *table = pages->table;
    tiku_rich_page_t *page;
    double header = table->header_height;

    if (!tiku_rp_new_page(pages)) return;
    page = &pages->pages[pages->count - 1];
    page->first_piece = pages->npieces;
    page->source_top = table->rows[table->row].top - header;
    if (table->props.header_rows) {
        const tiku_rp_row_t *end = &table->rows[table->props.header_rows - 1];

        page->header_first = table->rows[0].first;
        page->header_count = end->first + end->count - page->header_first;
        page->header_top = table->rows[0].top;
    }
    tiku_rp_flow_push(pages, 0, table->rows[table->row].grid + 1,
        header, (double)pages->setup.height - pages->setup.top - pages->setup.bottom);
}

void
tiku_rp_flow_step(tiku_richpages_t *pages)
{
    tiku_rp_flow_t *flow = pages->flow;
    tiku_rp_table_t *table = pages->table;
    const tiku_rp_row_t *row = &table->rows[table->row];

    if (flow->build < pages->layout->ngrids) { index_grid(pages); return; }
    if (!flow->depth) { start_page(pages); return; }
    tiku_rp_flow_frame(pages);
    if (flow->depth || pages->status != TIKU_RICH_PENDING) return;
    if (!flow->progress) { pages->status = TIKU_RICH_LIMIT; return; }
    if (!flow->done) return;
    pages->line = row->first + row->count;
    pages->scan = pages->line;
    pages->fresh_page = 1;
    table->phase = ++table->row == table->props.rows ? 0 : 3;
    if (pages->line == pages->layout->nlines) pages->status = TIKU_RICH_OK;
}

void
tiku_rp_flow_release(tiku_richpages_t *pages)
{
    if (!pages->flow) return;
    tiku_rd_free(pages->flow->nodes);
    tiku_rd_free(pages->flow);
}
