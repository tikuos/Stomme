/*
 * TikuDesktop Translation Kit.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_richoutput_pdf.c - retained document glyphs embedded as vector PDF.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richoutput.h"
#include "tiku_richfont_private.h"
#include "tiku_pdf_writer.h"
#include <stdlib.h>
#include <string.h>

/** @brief One bounded export with lazy registration of retained font bytes. */
typedef struct {
    tiku_pdf_writer_t *writer; /**< owned until finish */
    tiku_richfont_t *fonts;    /**< borrowed matching provider */
    const tiku_richlayout_t *layout; /**< completed source geometry */
    tiku_richsnapshot_t *source; /**< immutable text for Unicode mapping */
    int font[32];             /**< writer ID per provider slot, or -1 */
    size_t glyphs;             /**< total emitted glyphs */
    size_t pixels;             /**< total embedded pixels, bounded to 16 Mi */
    size_t run, run_end;      /**< cursors within the current paragraph */
    tiku_rich_paragraph_t text; /**< current source paragraph */
    double x, y;              /**< page transform applied to source geometry */
    tiku_rich_status_t status; /**< sticky refusal */
} output_t;

/** @brief Decode a scalar from already validated model or marker UTF-8. */
static uint32_t
scalar(const char *text, size_t length, size_t *at)
{
    uint32_t cp;
    unsigned count;

    if (*at >= length) return 0;
    cp = (unsigned char)text[(*at)++];
    count = cp < 0x80 ? 0 : cp < 0xe0 ? 1 : cp < 0xf0 ? 2 : 3;
    if (count) cp &= (1u << (6 - count)) - 1;
    while (count-- && *at < length)
        cp = (cp << 6) | ((unsigned char)text[(*at)++] & 63);
    return cp;
}

/** @brief Register the exact font bytes retained by the native provider. */
static int
font_id(output_t *out, uint32_t id)
{
    const unsigned char *bytes;
    size_t length;

    if (!id || id > out->fonts->count) {
        out->status = TIKU_RICH_INVALID;
        return -1;
    }
    id--;
    if (out->font[id] >= 0) return out->font[id];
    bytes = tiku_ttf_source(out->fonts->face[id].ttf, &length);
    out->font[id] = tiku_pdf_writer_font_mem(out->writer, bytes, length,
                                             out->fonts->face[id].index);
    if (out->font[id] < 0) out->status = TIKU_RICH_NO_MEMORY;
    return out->font[id];
}

/** @brief Emit a colored rectangle at a source-relative position. */
static void
rectangle(output_t *out, tiku_rich_rect_t rect, uint32_t color)
{
    tiku_pdf_paint_t paint = {.fill = color};

    if (!(color >> 24) || rect.width <= 0 || rect.height <= 0) return;
    tiku_pdf_writer_rect(out->writer, out->x + rect.x, out->y + rect.y,
                         rect.width, rect.height);
    tiku_pdf_writer_paint(out->writer, &paint);
}

/** @brief Emit glyph IDs individually with their retained baseline offsets. */
static void
glyphs(output_t *out, size_t first, size_t count,
       const tiku_rich_chars_t *chars, double x, double baseline,
       const char *text, size_t length)
{
    tiku_rich_glyph_t glyph;
    tiku_pdf_glyph_t pdf;
    tiku_pdf_run_t run = {.size = chars->size, .argb = chars->foreground};
    size_t at = 0;

    for (size_t i = 0; i < count && out->status == TIKU_RICH_OK; i++) {
        if (++out->glyphs > 500000) { out->status = TIKU_RICH_LIMIT; break; }
        out->status = tiku_richlayout_glyph(out->layout, first + i, &glyph);
        if (out->status != TIKU_RICH_OK) break;
        run.font = font_id(out, glyph.font);
        if (run.font < 0) break;
        run.x = out->x + x + glyph.x;
        run.y = out->y + baseline + glyph.y;
        pdf = (tiku_pdf_glyph_t){glyph.glyph, scalar(text, length, &at), 0};
        tiku_pdf_writer_text(out->writer, &run, &pdf, 1);
    }
}

/** @brief Embed straight-alpha picture pixels at the retained dimensions. */
static void
picture(output_t *out, const tiku_rich_cluster_t *c)
{
    size_t count = (size_t)c->image->pixel_width * c->image->pixel_height;
    int id;

    if (count > (16u << 20) - out->pixels) {
        out->status = TIKU_RICH_LIMIT;
        return;
    }
    out->pixels += count;
    id = tiku_pdf_writer_pixels(out->writer, c->image->pixels,
        (int)c->image->pixel_width, (int)c->image->pixel_height);
    if (id < 0) { out->status = TIKU_RICH_NO_MEMORY; return; }
    tiku_pdf_writer_draw(out->writer, id, out->x + c->box.x,
        out->y + c->baseline - c->image->height,
        c->image->width, c->image->height);
}

/** @brief Paint cluster content, decorations and external links. */
static void
cluster(output_t *out, const tiku_rich_cluster_t *c)
{
    tiku_rich_rect_t stroke = {c->box.x, 0, c->box.width,
                               c->chars.size / 16};
    tiku_rich_rect_t box = c->box;
    const char *link = NULL;

    rectangle(out, c->box, c->chars.highlight);
    if (c->image) {
        picture(out, c);
        box.y = c->baseline - c->image->height;
        box.height = c->image->height;
    } else
        glyphs(out, c->glyph, c->glyph_count, &c->chars, c->box.x, c->baseline,
               out->text.text + c->first, c->last - c->first);
    if (out->status != TIKU_RICH_OK) return;
    if (!c->image && c->chars.underline) {
        stroke.y = c->baseline + c->chars.size / 12;
        rectangle(out, stroke, c->chars.foreground);
    }
    if (!c->image && c->chars.strike) {
        stroke.y = c->baseline - c->chars.size / 3;
        rectangle(out, stroke, c->chars.foreground);
    }
    while (out->run < out->text.run_count && out->run_end <= c->first) {
        if (out->run_end) out->run++;
        if (out->run < out->text.run_count)
            out->run_end += out->text.runs[out->run].length;
    }
    if (out->run < out->text.run_count) link = out->text.runs[out->run].link;
    if (link && *link != '#')
        tiku_pdf_writer_link(out->writer, out->x + box.x,
            out->y + box.y, box.width, box.height, link);
}

/** @brief Emit a source line, including repeated header lines. */
static void
line(output_t *out, size_t index)
{
    tiku_rich_line_t line;
    tiku_rich_cluster_t c;

    out->status = tiku_richlayout_line(out->layout, index, &line);
    if (out->status != TIKU_RICH_OK) return;
    if (line.paragraph != out->text.id || line.first == 0) {
        out->status = tiku_richsnapshot_paragraph(out->source,
            line.paragraph_index, &out->text);
        out->run = out->run_end = 0;
        if (out->status != TIKU_RICH_OK) return;
    }
    for (size_t i = 0; i < line.count && out->status == TIKU_RICH_OK; i++) {
        out->status = tiku_richlayout_cluster(out->layout,
                                              line.cluster + i, &c);
        if (out->status == TIKU_RICH_OK) cluster(out, &c);
    }
    glyphs(out, line.marker_glyph, line.marker_count, &line.marker_chars,
           line.marker_x, line.baseline, line.marker, strlen(line.marker));
}

/** @brief Emit one source or relocated cell's fill and borders. */
static void
cell(output_t *out, const tiku_rich_grid_box_t *grid)
{
    tiku_rich_rect_t edge;

    rectangle(out, grid->box, grid->background);
    if (!grid->border || grid->box.height <= 0) return;
    edge = grid->box;
    edge.height = grid->border;
    rectangle(out, edge, grid->color);
    edge.y += grid->box.height - grid->border;
    rectangle(out, edge, grid->color);
    edge = grid->box;
    edge.width = grid->border;
    rectangle(out, edge, grid->color);
    edge.x += grid->box.width - grid->border;
    rectangle(out, edge, grid->color);
}

/** @brief Emit cell fills and borders from shared retained rectangles. */
static void
grids(output_t *out, const tiku_rich_page_t *page, unsigned depth)
{
    tiku_rich_grid_box_t grid;

    for (size_t i = 0; i < tiku_richlayout_grids(out->layout); i++) {
        tiku_richlayout_grid(out->layout, i, &grid);
        if (grid.kind != TIKU_RICH_BLOCK_CELL || grid.depth < depth ||
            grid.line < page->first_line ||
            grid.line - page->first_line >= page->line_count) continue;
        cell(out, &grid);
    }
}

static void span(output_t *out, const tiku_rich_page_t *page,
                 int (*cancel)(void *), void *context, unsigned depth);

/** @brief Emit independent tall-row cells and source ranges in paint order. */
static void
pieces(output_t *out, const tiku_richpages_t *pages, size_t page,
       int (*cancel)(void *), void *context)
{
    tiku_rich_page_t info;
    tiku_rich_page_piece_t piece;
    tiku_rich_page_setup_t setup;

    tiku_richpages_setup(pages, &setup);
    tiku_richpages_page(pages, page, &info);
    for (size_t i = 0; i < info.piece_count && out->status == TIKU_RICH_OK; i++) {
        if (cancel && cancel(context)) { out->status = TIKU_RICH_CANCELLED; return; }
        tiku_richpages_piece(pages, page, i, &piece);
        out->y = setup.top - piece.source_top;
        if (piece.kind == TIKU_RICH_PAGE_CELL) {
            out->y = setup.top;
            cell(out, &piece.cell);
        } else if (piece.kind == TIKU_RICH_PAGE_RANGE) {
            tiku_rich_page_t range = {.first_line = piece.first_line,
                                      .line_count = piece.line_count};

            span(out, &range, cancel, context, piece.depth);
        } else for (size_t n = 0; n < piece.line_count &&
                    out->status == TIKU_RICH_OK; n++) {
            if (cancel && cancel(context)) {
                out->status = TIKU_RICH_CANCELLED;
                return;
            }
            line(out, piece.first_line + n);
        }
    }
}

/** @brief Emit one source range with cancellation between lines. */
static void
span(output_t *out, const tiku_rich_page_t *page,
     int (*cancel)(void *), void *context, unsigned depth)
{
    grids(out, page, depth);
    for (size_t i = 0; i < page->line_count &&
        out->status == TIKU_RICH_OK; i++) {
        if (cancel && cancel(context)) {
            out->status = TIKU_RICH_CANCELLED;
            return;
        }
        line(out, page->first_line + i);
    }
}

tiku_rich_status_t
tiku_richoutput_pdf(tiku_richfont_t *fonts, const tiku_richpages_t *pages,
    int (*cancel)(void *), void *context,
    unsigned char **result, size_t *length)
{
    output_t out = {.fonts = fonts, .status = TIKU_RICH_OK};
    tiku_rich_page_setup_t setup;
    tiku_rich_page_t page;
    tiku_rich_provider_t provider = tiku_richfont_provider(fonts);

    if (!result || !length) return TIKU_RICH_INVALID;
    *result = NULL;
    *length = 0;
    if (!fonts || !pages) return TIKU_RICH_INVALID;
    out.layout = tiku_richpages_layout(pages);
    if (!out.layout) return tiku_richpages_status(pages);
    if (!tiku_richlayout_provider_is(out.layout, &provider))
        return TIKU_RICH_INVALID;
    if (tiku_richlayout_clusters(out.layout) > 250000) return TIKU_RICH_LIMIT;
    out.status = tiku_richlayout_snapshot(out.layout, &out.source);
    if (out.status != TIKU_RICH_OK) return out.status;
    out.writer = tiku_pdf_writer_new();
    if (!out.writer) { tiku_richsnapshot_release(out.source);
                       return TIKU_RICH_NO_MEMORY; }
    for (size_t i = 0; i < 32; i++) out.font[i] = -1;
    tiku_richpages_setup(pages, &setup);
    for (size_t p = 0; p < tiku_richpages_count(pages) &&
        out.status == TIKU_RICH_OK; p++) {
        tiku_richpages_page(pages, p, &page);
        if (tiku_pdf_writer_page(out.writer, setup.width, setup.height) < 0) {
            out.status = TIKU_RICH_NO_MEMORY;
            break;
        }
        out.x = setup.left;
        if (page.header_count) {
            tiku_rich_page_t header = {.first_line = page.header_first,
                .line_count = page.header_count};

            out.y = setup.top - page.header_top;
            span(&out, &header, cancel, context, 0);
        }
        out.y = setup.top - page.source_top;
        if (out.status == TIKU_RICH_OK) span(&out, &page, cancel, context, 0);
        if (out.status == TIKU_RICH_OK) pieces(&out, pages, p, cancel, context);
    }
    if (out.status == TIKU_RICH_OK) {
        *result = tiku_pdf_writer_finish(out.writer, length);
        if (!*result) out.status = TIKU_RICH_NO_MEMORY;
        else if (*length > (64u << 20)) {
            free(*result);
            *result = NULL;
            *length = 0;
            out.status = TIKU_RICH_LIMIT;
        }
    } else tiku_pdf_writer_free(out.writer);
    tiku_richsnapshot_release(out.source);
    return out.status;
}
