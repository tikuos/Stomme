/*
 * TikuDesktop Document Kit.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_richpages_frames.c - resumable row, cell and nested-table placement.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richpages_flow.h"
#include <math.h>

/** @brief Advance one column and give each cell fragment the same row height. */
static void
row_step(tiku_richpages_t *pages, tiku_rp_frame_t *frame)
{
    tiku_rp_flow_t *flow = pages->flow;
    unsigned columns = flow->nodes[frame->node - 1].props->columns;
    tiku_rich_page_piece_t piece = {.kind = TIKU_RICH_PAGE_CELL};

    if (frame->phase) {
        frame->used = fmax(frame->used, flow->used);
        frame->done &= flow->done;
        frame->progress |= flow->progress;
        frame->cursor = flow->nodes[frame->cursor - 1].next;
        frame->column++;
        frame->phase = 0;
    }
    if (frame->column == columns) {
        for (unsigned i = 0; i < columns; i++)
            pages->pieces[frame->boxes[i]].cell.box.height = frame->used;
        tiku_rp_flow_pop(pages, frame->done, frame->used, frame->progress);
        return;
    }
    piece.cell = pages->layout->grids[frame->cursor - 1];
    piece.cell.box.y = frame->top;
    piece.cell.box.height = 0;
    frame->boxes[frame->column] = pages->npieces;
    if (!tiku_rp_piece_add(pages, piece)) return;
    frame->phase = 1;
    tiku_rp_flow_push(pages, 1, frame->cursor, frame->top, frame->bottom);
}

/** @brief Finish a cell fragment, reserving its bottom border and padding. */
static void
cell_pop(tiku_richpages_t *pages, tiku_rp_frame_t *frame, int done)
{
    const tiku_rich_table_t *props = pages->flow->nodes[frame->node - 1].props;
    double used = frame->progress ? frame->y - frame->top +
                                  props->padding + props->border : 0;

    tiku_rp_flow_pop(pages, done, used, frame->progress);
}

/** @brief Advance a cell's single source line, preserving its complete glyphs. */
static void
cell_line(tiku_richpages_t *pages, tiku_rp_frame_t *frame,
          tiku_rp_cursor_t *node)
{
    const tiku_rich_line_t *line = &pages->layout->lines[node->cursor];
    double inset = node->props->padding + node->props->border;
    double gap = fmax(0, line->box.y - node->source);
    double room = frame->bottom - inset - frame->y;
    tiku_rich_page_piece_t piece = {.kind = TIKU_RICH_PAGE_TEXT,
        .first_line = node->cursor, .line_count = 1};

    /* Source-coordinate subtraction can leave sub-pixel rounding residues. */
    if (gap + line->box.height > room + 1e-8 && !frame->progress) gap = 0;
    if (gap + line->box.height > room + 1e-8) {
        cell_pop(pages, frame, 0);
        return;
    }
    piece.source_top = line->box.y - frame->y - gap;
    if (!tiku_rp_piece_add(pages, piece)) return;
    frame->y += gap + line->box.height;
    frame->progress = 1;
    node->source = line->box.y + line->box.height;
    node->cursor++;
}

/** @brief Scan one line of a cell-local keep group before placing its content. */
static void
cell_group(tiku_richpages_t *pages, tiku_rp_frame_t *frame,
           const tiku_rp_cursor_t *node)
{
    const tiku_richlayout_t *layout = pages->layout;
    const tiku_rich_grid_box_t *cell = &layout->grids[frame->node - 1];
    const tiku_rich_line_t *last = &layout->lines[frame->scan++];
    const tiku_rich_line_t *first = &layout->lines[node->cursor];
    tiku_rich_chars_t chars;
    tiku_rich_para_t props;
    double inset = node->props->padding + node->props->border;
    double height, room = frame->bottom - inset - frame->y;

    tiku_richsnapshot_resolve(&layout->source, last->paragraph_index, 0, &chars, &props);
    if (frame->scan < cell->line + cell->line_count &&
        (!node->nested || frame->scan < layout->grids[node->nested - 1].line) &&
        (layout->lines[frame->scan].paragraph == last->paragraph ?
            props.keep_lines : props.keep_next)) return;
    height = last->box.y + last->box.height - first->box.y;
    if (height > frame->bottom - frame->top - 2 * inset + 1e-8) {
        if (frame->scan - node->cursor > 1)
            pages->warnings |= TIKU_RICH_PAGE_KEEP_BROKEN;
    } else if (frame->progress && height + fmax(0, first->box.y - node->source) >
               room + 1e-8) {
        cell_pop(pages, frame, 0);
        return;
    }
    frame->group_end = frame->scan;
    frame->phase = 1;
}

/** @brief Place one cell line or suspend it while a nested table advances. */
static void
cell_step(tiku_richpages_t *pages, tiku_rp_frame_t *frame)
{
    tiku_rp_flow_t *flow = pages->flow;
    tiku_rp_cursor_t *node = &flow->nodes[frame->node - 1];
    const tiku_rich_grid_box_t *box = &pages->layout->grids[frame->node - 1];
    const tiku_rich_grid_box_t *child;
    double inset = node->props->padding + node->props->border, gap;

    if (!frame->phase) {
        if (!node->started) {
            node->cursor = box->line;
            node->nested = node->child;
            node->source = box->box.y + inset;
            node->started = 1;
        }
        frame->y += inset;
        frame->phase = 1;
    }
    if (frame->phase == 2) {
        frame->y += flow->used;
        frame->progress |= flow->progress;
        if (!flow->done) { cell_pop(pages, frame, 0); return; }
        child = &pages->layout->grids[node->nested - 1];
        node->cursor = child->line + child->line_count;
        node->source = child->box.y + child->box.height;
        node->nested = flow->nodes[node->nested - 1].next;
        frame->phase = 1;
    }
    if (frame->phase == 3) { cell_group(pages, frame, node); return; }
    if (node->cursor == box->line + box->line_count) {
        cell_pop(pages, frame, 1);
        return;
    }
    child = node->nested ? &pages->layout->grids[node->nested - 1] : NULL;
    if (!child || node->cursor != child->line) {
        if (node->cursor < frame->group_end) cell_line(pages, frame, node);
        else { frame->scan = node->cursor; frame->phase = 3; }
        return;
    }
    gap = fmax(0, child->box.y - node->source);
    if (!frame->progress && gap >= frame->bottom - inset - frame->y) gap = 0;
    frame->y += gap;
    node->source = child->box.y;
    frame->phase = 2;
    tiku_rp_flow_push(pages, 2, node->nested, frame->y, frame->bottom - inset);
}

/** @brief Return the sibling immediately following a table row. */
static size_t
after_row(const tiku_rp_flow_t *flow, size_t first, unsigned columns)
{
    while (columns--) first = flow->nodes[first - 1].next;
    return first;
}

/** @brief Place complete nested headers before starting or resuming body rows. */
static int
table_header(tiku_richpages_t *pages, tiku_rp_frame_t *frame,
             tiku_rp_cursor_t *node)
{
    const tiku_rich_grid_box_t *box = &pages->layout->grids[frame->node - 1];

    frame->piece_begin = pages->npieces;
    if (!node->started) { node->cursor = node->child; node->started = 1; }
    if (!node->cursor) { tiku_rp_flow_pop(pages, 1, 0, 0); return 0; }
    if (node->props->header_rows) {
        if (node->header_height > frame->bottom - frame->top) {
            tiku_rp_flow_pop(pages, 0, 0, 0);
            return 0;
        }
        if (!tiku_rp_flow_range(pages, box->line, node->header_end - box->line,
                               box->box.y, frame->y, box->depth + 1)) return 0;
        frame->y += node->header_height;
        if (node->cursor == node->child) {
            node->cursor = node->body;
            frame->progress = !node->body;
        }
    }
    frame->phase = 1;
    return 1;
}

/** @brief Place a nested row whole when possible, otherwise descend into cells. */
static void
table_row(tiku_richpages_t *pages, tiku_rp_frame_t *frame,
          tiku_rp_cursor_t *node)
{
    tiku_rp_flow_t *flow = pages->flow;
    const tiku_rich_grid_box_t *row = &pages->layout->grids[node->cursor - 1];
    size_t after = after_row(flow, node->cursor, node->props->columns);
    const tiku_rich_grid_box_t *table = &pages->layout->grids[frame->node - 1];
    size_t end = after ? pages->layout->grids[after - 1].line :
                        table->line + table->line_count;

    if (!flow->nodes[node->cursor - 1].started &&
        row->box.height <= frame->bottom - frame->y) {
        if (!tiku_rp_flow_range(pages, row->line, end - row->line,
                               row->box.y, frame->y, row->depth)) return;
        frame->y += row->box.height;
        frame->progress = 1;
        node->cursor = after;
        return;
    }
    frame->phase = 2;
    tiku_rp_flow_push(pages, 0, node->cursor, frame->y, frame->bottom);
}

/** @brief Resume a nested table, repeating complete headers on continuation. */
static void
table_step(tiku_richpages_t *pages, tiku_rp_frame_t *frame)
{
    tiku_rp_flow_t *flow = pages->flow;
    tiku_rp_cursor_t *node = &flow->nodes[frame->node - 1];

    if (!frame->phase && !table_header(pages, frame, node)) return;
    if (frame->phase == 2) {
        frame->y += flow->used;
        frame->progress |= flow->progress;
        if (!flow->done) {
            if (!frame->progress) {
                pages->pages[pages->count - 1].piece_count -=
                    pages->npieces - frame->piece_begin;
                pages->npieces = frame->piece_begin;
                frame->y = frame->top;
            }
            tiku_rp_flow_pop(pages, 0, frame->y - frame->top, frame->progress);
            return;
        }
        node->cursor = after_row(flow, node->cursor, node->props->columns);
        frame->phase = 1;
    }
    if (!node->cursor) {
        tiku_rp_flow_pop(pages, 1, frame->y - frame->top, frame->progress);
        return;
    }
    table_row(pages, frame, node);
}

void
tiku_rp_flow_frame(tiku_richpages_t *pages)
{
    tiku_rp_frame_t *frame = &pages->flow->frames[pages->flow->depth - 1];

    if (frame->kind == 0) row_step(pages, frame);
    else if (frame->kind == 1) cell_step(pages, frame);
    else table_step(pages, frame);
}
