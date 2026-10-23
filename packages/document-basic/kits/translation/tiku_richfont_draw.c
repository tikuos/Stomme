/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_richfont_draw.c - clipped native glyph drawing from shared layout.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richfont_private.h"

#include <math.h>
#include <string.h>

/** @brief Point-to-pixel mapping and bounded surface destination. */
typedef struct {
    tiku_richfont_t *fonts;
    tiku_surface_t *surface;
    double x, y, zoom;
    int scale;
} paint_t;

/** @brief Composite document ARGB ink with additional glyph coverage. */
static void
blend(uint32_t *pixel, uint32_t ink, unsigned coverage)
{
    unsigned a = (ink >> 24) * coverage / 255, b = 255 - a;
    unsigned red, green, blue;

    if (!a) return;
    red = (((ink >> 16) & 255) * a + ((*pixel >> 16) & 255) * b + 127) / 255;
    green = (((ink >> 8) & 255) * a + ((*pixel >> 8) & 255) * b + 127) / 255;
    blue = ((ink & 255) * a + (*pixel & 255) * b + 127) / 255;
    *pixel = (red << 16) | (green << 8) | blue;
}

/** @brief Scale an embedded picture into the clipped native pixel surface. */
static void
draw_image(const paint_t *paint, const tiku_rich_cluster_t *cluster)
{
    const tiku_rich_image_t *image = cluster->image;
    tiku_surface_t *s = paint->surface;
    double scale = paint->zoom * paint->scale;
    double left = (paint->x + cluster->box.x * paint->zoom) * paint->scale;
    double top = (paint->y + (cluster->baseline - image->height) *
                  paint->zoom) * paint->scale;
    double right = left + image->width * scale;
    double bottom = top + image->height * scale;
    int x0, x1, y0, y1;

    if (right <= s->clip.x * paint->scale ||
        bottom <= s->clip.y * paint->scale ||
        left >= (s->clip.x + s->clip.w) * paint->scale ||
        top >= (s->clip.y + s->clip.h) * paint->scale) return;
    x0 = (int)floor(fmax(left, s->clip.x * paint->scale));
    y0 = (int)floor(fmax(top, s->clip.y * paint->scale));
    x1 = (int)ceil(fmin(right, (s->clip.x + s->clip.w) * paint->scale));
    y1 = (int)ceil(fmin(bottom, (s->clip.y + s->clip.h) * paint->scale));
    for (int y = y0; y < y1; y++) {
        unsigned sy = (unsigned)fmax(0, (y + 0.5 - top) *
            image->pixel_height / (image->height * scale));

        if (sy >= image->pixel_height) sy = image->pixel_height - 1;
        for (int x = x0; x < x1; x++) {
            unsigned sx = (unsigned)fmax(0, (x + 0.5 - left) *
                image->pixel_width / (image->width * scale));

            if (sx >= image->pixel_width) sx = image->pixel_width - 1;
            blend(&s->px[(size_t)y * s->w * paint->scale + x],
                  image->pixels[(size_t)sy * image->pixel_width + sx], 255);
        }
    }
}

/** @brief Paint an alpha rectangle clipped before any coordinate conversion. */
static void
rectangle(const paint_t *paint, tiku_rich_rect_t box, uint32_t colour)
{
    tiku_surface_t *s = paint->surface;
    double left = paint->x + box.x * paint->zoom;
    double top = paint->y + box.y * paint->zoom;
    double right = left + box.width * paint->zoom;
    double bottom = top + box.height * paint->zoom;
    int x, y, x0, x1, y1;

    if (!(colour >> 24)) return;
    if (left < s->clip.x) left = s->clip.x;
    if (top < s->clip.y) top = s->clip.y;
    if (right > s->clip.x + s->clip.w) right = s->clip.x + s->clip.w;
    if (bottom > s->clip.y + s->clip.h) bottom = s->clip.y + s->clip.h;
    if (right <= left || bottom <= top) return;
    x0 = (int)floor(left * paint->scale);
    x1 = (int)ceil(right * paint->scale);
    y = (int)floor(top * paint->scale);
    y1 = (int)ceil(bottom * paint->scale);
    for (; y < y1; y++)
        for (x = x0; x < x1; x++)
            blend(&s->px[(size_t)y * s->w * paint->scale + x], colour, 255);
}

/** @brief Composite one glyph bitmap at a native pixel baseline. */
static void
bitmap(const paint_t *paint, const tiku_ttf_glyph_t *glyph,
       double x, double y, uint32_t colour)
{
    tiku_surface_t *s = paint->surface;
    int gx, gy, left, right, top, bottom;

    x = x * paint->scale + glyph->ox;
    y = y * paint->scale + glyph->oy;
    if (!glyph->cover || x + glyph->w <= s->clip.x * paint->scale ||
        y + glyph->h <= s->clip.y * paint->scale ||
        x >= (s->clip.x + s->clip.w) * paint->scale ||
        y >= (s->clip.y + s->clip.h) * paint->scale) return;
    gx = (int)floor(x);
    gy = (int)floor(y);
    left = s->clip.x * paint->scale - gx;
    top = s->clip.y * paint->scale - gy;
    right = (s->clip.x + s->clip.w) * paint->scale - gx;
    bottom = (s->clip.y + s->clip.h) * paint->scale - gy;
    if (left < 0) left = 0;
    if (top < 0) top = 0;
    if (right > glyph->w) right = glyph->w;
    if (bottom > glyph->h) bottom = glyph->h;
    for (int row = top; row < bottom; row++)
        for (int col = left; col < right; col++)
            blend(&s->px[(size_t)(gy + row) * s->w * paint->scale + gx + col],
                  colour, glyph->cover[(size_t)row * glyph->w + col]);
}

/** @brief Draw one retained glyph without changing its advance. */
static tiku_rich_status_t
draw_glyph(const paint_t *paint, const tiku_rich_glyph_t *glyph,
           const tiku_rich_chars_t *chars, double x, double baseline)
{
    tiku_ttf_glyph_t raster;
    tiku_rich_status_t status;
    double size = chars->size * paint->zoom * paint->scale;

    if (size > 512) return TIKU_RICH_LIMIT;
    if (size < 1) size = 1;
    status = tiku_richfont_raster(paint->fonts, glyph->font, glyph->glyph,
                                  (int)(size + 0.5), &raster);
    if (status != TIKU_RICH_OK) return status;
    bitmap(paint, &raster, paint->x + (x + glyph->x) * paint->zoom,
           paint->y + (baseline + glyph->y) * paint->zoom, chars->foreground);
    tiku_ttf_free_glyph(&raster);
    return TIKU_RICH_OK;
}

/** @brief Paint a cluster's highlight, glyphs and text decorations. */
static tiku_rich_status_t
draw_cluster(const paint_t *paint, const tiku_richlayout_t *layout,
             const tiku_rich_cluster_t *cluster)
{
    size_t i;
    tiku_rich_glyph_t glyph;
    tiku_rich_rect_t stroke;
    tiku_rich_status_t status;

    rectangle(paint, cluster->box, cluster->chars.highlight);
    if (cluster->image) {
        draw_image(paint, cluster);
        return TIKU_RICH_OK;
    }
    for (i = 0; i < cluster->glyph_count; i++) {
        status = tiku_richlayout_glyph(layout, cluster->glyph + i, &glyph);
        if (status == TIKU_RICH_OK)
            status = draw_glyph(paint, &glyph, &cluster->chars,
                                cluster->box.x, cluster->baseline);
        if (status != TIKU_RICH_OK) return status;
    }
    stroke = (tiku_rich_rect_t){cluster->box.x, 0, cluster->box.width,
        cluster->chars.size / 16};
    if (stroke.height * paint->zoom * paint->scale < 1)
        stroke.height = 1 / (paint->zoom * paint->scale);
    if (cluster->chars.underline) {
        stroke.y = cluster->baseline + cluster->chars.size / 12;
        rectangle(paint, stroke, cluster->chars.foreground);
    }
    if (cluster->chars.strike) {
        stroke.y = cluster->baseline - cluster->chars.size / 3;
        rectangle(paint, stroke, cluster->chars.foreground);
    }
    return TIKU_RICH_OK;
}

/** @brief Paint a list label using the glyphs measured by layout. */
static tiku_rich_status_t
draw_marker(const paint_t *paint, const tiku_richlayout_t *layout,
            const tiku_rich_line_t *line)
{
    tiku_rich_glyph_t glyph;
    size_t i;
    tiku_rich_status_t status;

    for (i = 0; i < line->marker_count; i++) {
        status = tiku_richlayout_glyph(layout, line->marker_glyph + i, &glyph);
        if (status == TIKU_RICH_OK)
            status = draw_glyph(paint, &glyph, &line->marker_chars,
                                line->marker_x, line->baseline);
        if (status != TIKU_RICH_OK) return status;
    }
    return TIKU_RICH_OK;
}

/** @brief Draw visible clusters and the optional list label of one line. */
static tiku_rich_status_t
draw_line(const paint_t *paint, const tiku_richlayout_t *layout,
          const tiku_rich_line_t *line, size_t *remaining)
{
    tiku_rich_cluster_t cluster;
    tiku_rich_status_t status;
    size_t i;
    double y = paint->y + line->box.y * paint->zoom;

    if (y + line->box.height * paint->zoom <= paint->surface->clip.y ||
        y >= paint->surface->clip.y + paint->surface->clip.h)
        return TIKU_RICH_OK;
    for (i = 0; i < line->count; i++) {
        if (!*remaining) return TIKU_RICH_LIMIT;
        (*remaining)--;
        status = tiku_richlayout_cluster(layout, line->cluster + i, &cluster);
        if (status == TIKU_RICH_OK)
            status = draw_cluster(paint, layout, &cluster);
        if (status != TIKU_RICH_OK) return status;
    }
    return line->marker_count ? draw_marker(paint, layout, line) :
                               TIKU_RICH_OK;
}

/** @brief Draw one original or relocated cell's fill and borders. */
static void
draw_cell(const paint_t *paint, const tiku_rich_grid_box_t *grid)
{
    tiku_rich_rect_t edge;

    rectangle(paint, grid->box, grid->background);
    if (!grid->border || grid->box.height <= 0) return;
    edge = grid->box;
    edge.height = grid->border;
    rectangle(paint, edge, grid->color);
    edge.y += grid->box.height - grid->border;
    rectangle(paint, edge, grid->color);
    edge = grid->box;
    edge.width = grid->border;
    rectangle(paint, edge, grid->color);
    edge.x += grid->box.width - grid->border;
    rectangle(paint, edge, grid->color);
}

/** @brief Draw fills and borders for cells starting in this line range. */
static void
draw_grids(const paint_t *paint, const tiku_richlayout_t *layout,
           size_t first, size_t count, unsigned depth)
{
    tiku_rich_grid_box_t grid;

    for (size_t i = 0; i < tiku_richlayout_grids(layout); i++) {
        tiku_richlayout_grid(layout, i, &grid);
        if (grid.kind != TIKU_RICH_BLOCK_CELL || grid.depth < depth ||
            grid.line < first || grid.line - first >= count) continue;
        draw_cell(paint, &grid);
    }
}

/** @brief Validate a frame and paint lines with optional original cell geometry. */
static tiku_rich_status_t
draw(tiku_richfont_t *fonts, const tiku_richlayout_t *layout,
    tiku_surface_t *surface, double x, double y, double zoom,
    size_t first_line, size_t line_count, int grids, unsigned depth)
{
    paint_t paint = {fonts, surface, x, y, zoom, 1};
    tiku_rich_line_t line;
    tiku_rich_status_t status;
    size_t count, remaining = 65536;
    tiku_rich_provider_t provider = tiku_richfont_provider(fonts);

    if (!fonts || !layout || !surface || !surface->px || !isfinite(x) ||
        !isfinite(y) || !isfinite(zoom) || zoom < 0.1 || zoom > 8 ||
        surface->w < 1 || surface->h < 1 || surface->scale < 0 ||
        surface->scale > 4 || surface->clip.x < 0 || surface->clip.y < 0 ||
        surface->clip.w < 0 || surface->clip.h < 0 ||
        surface->clip.w > surface->w - surface->clip.x ||
        surface->clip.h > surface->h - surface->clip.y)
        return TIKU_RICH_INVALID;
    if (!tiku_richlayout_provider_is(layout, &provider))
        return TIKU_RICH_INVALID;
    status = tiku_richlayout_status(layout);
    if (status != TIKU_RICH_OK) return status;
    count = tiku_richlayout_lines(layout);
    if (first_line > count || line_count > count - first_line)
        return TIKU_RICH_INVALID_RANGE;
    paint.scale = surface->scale ? surface->scale : 1;
    tiku_gfx_rec_miss(surface);
    if (grids) draw_grids(&paint, layout, first_line, line_count, depth);
    while (line_count--) {
        status = tiku_richlayout_line(layout, first_line++, &line);
        if (status == TIKU_RICH_OK)
            status = draw_line(&paint, layout, &line, &remaining);
        if (status != TIKU_RICH_OK) return status;
    }
    return TIKU_RICH_OK;
}

tiku_rich_status_t
tiku_richfont_draw(tiku_richfont_t *fonts, const tiku_richlayout_t *layout,
    tiku_surface_t *surface, double x, double y, double zoom,
    size_t first_line, size_t line_count)
{
    return draw(fonts, layout, surface, x, y, zoom, first_line, line_count, 1, 0);
}

tiku_rich_status_t
tiku_rf_draw_piece(tiku_richfont_t *fonts, const tiku_richlayout_t *layout,
    tiku_surface_t *surface, double x, double y, double zoom,
    const tiku_rich_page_piece_t *piece)
{
    tiku_rich_status_t status;

    if (piece->kind != TIKU_RICH_PAGE_CELL)
        return draw(fonts, layout, surface, x, y - piece->source_top * zoom,
            zoom, piece->first_line, piece->line_count,
            piece->kind == TIKU_RICH_PAGE_RANGE, piece->depth);
    status = draw(fonts, layout, surface, x, y, zoom, 0, 0, 0, 0);
    if (status == TIKU_RICH_OK) {
        paint_t paint = {fonts, surface, x, y, zoom,
                         surface->scale ? surface->scale : 1};

        draw_cell(&paint, &piece->cell);
    }
    return status;
}
