/*
 * TikuDesktop Document Kit.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_richpages.c - bounded paragraph keeps and source-line page mapping.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richpages_private.h"
#include <string.h>

int
tiku_rp_break_before(const tiku_richlayout_t *layout, size_t paragraph)
{
    tiku_rich_chars_t chars;
    tiku_rich_para_t props;

    return tiku_richsnapshot_resolve(&layout->source, paragraph, 0,
        &chars, &props) == TIKU_RICH_OK && props.break_before;
}

tiku_rich_status_t
tiku_richpages_create(const tiku_richsnapshot_t *snapshot,
    const tiku_richlayout_options_t *options, size_t page_limit,
    tiku_richpages_t **out)
{
    tiku_richpages_t *pages;
    tiku_richlayout_options_t settings;
    tiku_rich_status_t status = TIKU_RICH_OK;

    if (!out) return TIKU_RICH_INVALID;
    *out = NULL;
    if (!snapshot || !options || page_limit > 5000) return TIKU_RICH_INVALID;
    pages = tiku_rd_alloc(snapshot->memory, sizeof *pages, &status);
    if (!pages) return status;
    pages->limit = page_limit ? page_limit : 5000;
    pages->setup = snapshot->root->page;
    settings = *options;
    settings.width = (double)pages->setup.width -
                     pages->setup.left - pages->setup.right;
    status = tiku_richlayout_create(snapshot, &settings, &pages->layout);
    if (status == TIKU_RICH_OK) {
        pages->pages = tiku_rd_alloc(snapshot->memory,
            pages->limit * sizeof *pages->pages, &status);
        if (!pages->pages && status == TIKU_RICH_OK)
            status = TIKU_RICH_NO_MEMORY;
    }
    if (status != TIKU_RICH_OK) {
        tiku_richpages_release(pages);
        return status;
    }
    pages->status = TIKU_RICH_PENDING;
    *out = pages;
    return TIKU_RICH_OK;
}

int
tiku_rp_new_page(tiku_richpages_t *pages)
{
    if (pages->count == pages->limit) {
        pages->status = TIKU_RICH_LIMIT;
        return 0;
    }
    pages->pages[pages->count++] = (tiku_rich_page_t){
        .first_line = pages->line,
        .source_top = pages->layout->lines[pages->line].box.y};
    pages->fresh_page = 0;
    return 1;
}

/** @brief Locate a top-level table beginning at a source-line boundary. */
static const tiku_rich_grid_box_t *
table_at(const tiku_richlayout_t *layout, size_t line)
{
    for (size_t i = 0; i < layout->ngrids; i++)
        if (layout->grids[i].kind == TIKU_RICH_BLOCK_TABLE &&
            !layout->grids[i].depth && layout->grids[i].line == line)
            return &layout->grids[i];
    return NULL;
}

/** @brief Scan one line boundary, preserving paragraph keeps when feasible. */
static void
scan_group(tiku_richpages_t *pages)
{
    const tiku_richlayout_t *layout = pages->layout;
    tiku_rich_chars_t chars;
    size_t next = pages->scan + 1;
    int paragraph_end;
    double group_height, height = (double)pages->setup.height -
        pages->setup.top - pages->setup.bottom;

    pages->paragraph = layout->lines[pages->scan].paragraph_index;
    if (pages->scan == layout->paragraphs[pages->paragraph].line)
        tiku_richsnapshot_resolve(&layout->source, pages->paragraph, 0,
                                   &chars, &pages->props);
    paragraph_end = next == layout->nlines ||
        next == layout->paragraphs[pages->paragraph + 1].line;
    pages->scan = next;
    if (next < layout->nlines &&
        !table_at(layout, next) &&
        !(paragraph_end &&
          tiku_rp_break_before(layout, pages->paragraph + 1)) &&
        (paragraph_end ? pages->props.keep_next : pages->props.keep_lines))
        return;
    pages->group_end = next;
    pages->placing = 1;
    group_height = layout->lines[next - 1].box.y +
        layout->lines[next - 1].box.height - layout->lines[pages->line].box.y;
    if (group_height > height) pages->warnings |= TIKU_RICH_PAGE_KEEP_BROKEN;
    else if (pages->count && pages->pages[pages->count - 1].line_count &&
        layout->lines[next - 1].box.y + layout->lines[next - 1].box.height -
        pages->pages[pages->count - 1].source_top > height)
        tiku_rp_new_page(pages);
}

/** @brief Place one source line or refuse a line that cannot fit on a page. */
static void
place_line(tiku_richpages_t *pages)
{
    const tiku_rich_line_t *line = &pages->layout->lines[pages->line];
    double height = (double)pages->setup.height -
                     pages->setup.top - pages->setup.bottom;

    if (line->box.height > height) {
        pages->status = TIKU_RICH_LIMIT;
        return;
    }
    pages->placed_paragraph = line->paragraph_index;
    if (pages->line ==
        pages->layout->paragraphs[pages->placed_paragraph].line &&
        tiku_rp_break_before(pages->layout, pages->placed_paragraph) &&
        pages->count && pages->pages[pages->count - 1].line_count &&
        !tiku_rp_new_page(pages)) return;
    if ((!pages->count || pages->fresh_page) && !tiku_rp_new_page(pages)) return;
    if (line->box.y + line->box.height -
        pages->pages[pages->count - 1].source_top > height &&
        !tiku_rp_new_page(pages)) return;
    pages->pages[pages->count - 1].line_count++;
    pages->line++;
    if (pages->line == pages->layout->nlines) pages->status = TIKU_RICH_OK;
    else if (pages->line == pages->group_end) pages->placing = 0;
}

tiku_rich_status_t
tiku_richpages_step(tiku_richpages_t *pages, size_t work)
{
    tiku_rich_status_t status;

    if (!pages || !work) return TIKU_RICH_INVALID;
    if (pages->status != TIKU_RICH_PENDING) return pages->status;
    if (tiku_richlayout_status(pages->layout) == TIKU_RICH_PENDING) {
        status = tiku_richlayout_step(pages->layout, work);
        if (status != TIKU_RICH_OK && status != TIKU_RICH_PENDING)
            pages->status = status;
        return pages->status;
    }
    while (work-- && pages->status == TIKU_RICH_PENDING) {
        const tiku_rich_grid_box_t *table;

        if (pages->table && pages->table->phase) {
            tiku_rp_table_step(pages);
            continue;
        }
        table = table_at(pages->layout, pages->line);
        if (!pages->placing && table) tiku_rp_table_begin(pages, table);
        else if (pages->placing) place_line(pages);
        else scan_group(pages);
    }
    return pages->status;
}

void
tiku_richpages_release(tiku_richpages_t *pages)
{
    if (!pages) return;
    tiku_rp_flow_release(pages);
    tiku_richlayout_release(pages->layout);
    tiku_rd_free(pages->table);
    tiku_rd_free(pages->pages);
    tiku_rd_free(pages->pieces);
    tiku_rd_free(pages);
}

size_t
tiku_richpages_count(const tiku_richpages_t *pages)
{
    return pages && pages->status == TIKU_RICH_OK ? pages->count : 0;
}

tiku_rich_status_t
tiku_richpages_status(const tiku_richpages_t *pages)
{
    return pages ? pages->status : TIKU_RICH_INVALID;
}

unsigned
tiku_richpages_warnings(const tiku_richpages_t *pages)
{
    return pages ? pages->warnings : 0;
}

const tiku_richlayout_t *
tiku_richpages_layout(const tiku_richpages_t *pages)
{
    return pages && pages->status == TIKU_RICH_OK ? pages->layout : NULL;
}

tiku_rich_status_t
tiku_richpages_page(const tiku_richpages_t *pages, size_t index,
                    tiku_rich_page_t *out)
{
    if (!out) return TIKU_RICH_INVALID;
    memset(out, 0, sizeof *out);
    if (!pages) return TIKU_RICH_INVALID;
    if (pages->status != TIKU_RICH_OK) return pages->status;
    if (index >= pages->count) return TIKU_RICH_INVALID_RANGE;
    *out = pages->pages[index];
    return TIKU_RICH_OK;
}

tiku_rich_status_t
tiku_richpages_setup(const tiku_richpages_t *pages, tiku_rich_page_setup_t *out)
{
    if (!pages || !out) return TIKU_RICH_INVALID;
    *out = pages->setup;
    return TIKU_RICH_OK;
}

tiku_rich_status_t
tiku_richpages_piece(const tiku_richpages_t *pages, size_t page,
                     size_t index, tiku_rich_page_piece_t *out)
{
    tiku_rich_page_t span;
    tiku_rich_status_t status;

    if (!out) return TIKU_RICH_INVALID;
    memset(out, 0, sizeof *out);
    status = tiku_richpages_page(pages, page, &span);
    if (status != TIKU_RICH_OK) return status;
    if (index >= span.piece_count) return TIKU_RICH_INVALID_RANGE;
    *out = pages->pieces[span.first_piece + index];
    return TIKU_RICH_OK;
}
