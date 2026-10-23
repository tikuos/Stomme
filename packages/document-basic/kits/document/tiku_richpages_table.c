/*
 * TikuDesktop Document Kit.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_richpages_table.c - whole-row pagination and repeated table headers.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richpages_private.h"
#include <string.h>

void
tiku_rp_table_begin(tiku_richpages_t *pages,
                    const tiku_rich_grid_box_t *table)
{
    size_t at = tiku_rd_block_find(pages->layout->source.root, table->id);
    tiku_rich_status_t status = TIKU_RICH_OK;

    if (table->box.width > (double)pages->setup.width -
        pages->setup.left - pages->setup.right) {
        pages->status = TIKU_RICH_LIMIT;
        return;
    }
    if (!pages->table)
        pages->table = tiku_rd_alloc(pages->layout->source.memory,
                                     sizeof *pages->table, &status);
    if (!pages->table) { pages->status = status; return; }
    memset(pages->table, 0, sizeof *pages->table);
    pages->table->box = table;
    pages->table->props = pages->layout->source.root->blocks->nodes[at].table;
    pages->table->grid = (size_t)(table - pages->layout->grids) + 1;
    pages->table->check = table->line;
    pages->table->phase = 1;
}

/** @brief Accumulate one direct cell into its row's complete outer bounds. */
static void
collect(tiku_richpages_t *pages)
{
    tiku_rp_table_t *table = pages->table;
    const tiku_rich_grid_box_t *cell = &pages->layout->grids[table->grid++];
    tiku_rp_row_t *row;

    if (cell->kind != TIKU_RICH_BLOCK_CELL || cell->depth != 1) return;
    row = &table->rows[table->cells / table->props.columns];
    if (!(table->cells % table->props.columns)) {
        row->first = cell->line;
        row->grid = table->grid - 1;
        row->top = cell->box.y;
        row->height = cell->box.height;
    }
    row->count = cell->line + cell->line_count - row->first;
    if (++table->cells == table->props.rows * table->props.columns) {
        table->phase = 2;
        if (table->props.header_rows) {
            row = &table->rows[table->props.header_rows - 1];
            table->header_height = row->top + row->height - table->rows[0].top;
        }
    }
}

/** @brief Place a complete table or one row group with optional headers. */
static void
place(tiku_richpages_t *pages)
{
    tiku_rp_table_t *table = pages->table;
    unsigned last = table->row;
    const tiku_rp_row_t *row = &table->rows[table->row];
    const tiku_rp_row_t *end;
    tiku_rich_page_t *page;
    double bottom, height = (double)pages->setup.height -
                            pages->setup.top - pages->setup.bottom;
    double repeated = table->row ? table->header_height : 0;

    if (!table->row) {
        if (table->box->box.height <= height) last = table->props.rows - 1;
        else if (table->props.header_rows)
            last = table->props.header_rows < table->props.rows ?
                table->props.header_rows : table->props.rows - 1;
    }
    end = &table->rows[last];
    bottom = end->top + end->height;
    if (bottom - row->top + repeated > height) {
        if (!table->row && table->props.header_rows) {
            if (table->header_height >= height ||
                table->props.header_rows == table->props.rows) {
                pages->status = TIKU_RICH_LIMIT;
                return;
            }
            table->row = table->props.header_rows;
            pages->line = table->rows[table->row].first;
        }
        tiku_rp_flow_begin(pages);
        return;
    }
    if (!pages->count || pages->fresh_page || bottom -
        pages->pages[pages->count - 1].source_top > height) {
        if (!tiku_rp_new_page(pages)) return;
        page = &pages->pages[pages->count - 1];
        page->source_top = row->top - repeated;
        if (repeated) {
            const tiku_rp_row_t *header =
                &table->rows[table->props.header_rows - 1];

            page->header_first = table->rows[0].first;
            page->header_count = header->first + header->count -
                                  page->header_first;
            page->header_top = table->rows[0].top;
        }
    }
    page = &pages->pages[pages->count - 1];
    page->line_count += end->first + end->count - row->first;
    pages->line = end->first + end->count;
    pages->scan = pages->line;
    table->row = last + 1;
    if (table->row == table->props.rows) {
        table->phase = 0;
        if (pages->line == pages->layout->nlines) pages->status = TIKU_RICH_OK;
    }
}

void
tiku_rp_table_step(tiku_richpages_t *pages)
{
    tiku_rp_table_t *table = pages->table;

    if (table->phase == 4) tiku_rp_flow_step(pages);
    else if (table->phase == 1) collect(pages);
    else if (table->phase == 2) {
        const tiku_rich_line_t *line = &pages->layout->lines[table->check++];

        if (!line->first && tiku_rp_break_before(pages->layout,
                                                line->paragraph_index)) {
            pages->status = TIKU_RICH_UNSUPPORTED;
            return;
        }
        if (table->check == table->box->line + table->box->line_count)
            table->phase = 3;
    } else place(pages);
}
