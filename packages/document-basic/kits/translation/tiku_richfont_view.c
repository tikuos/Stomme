/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_richfont_view.c - visible view drawing, selection and caret overlay.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richfont.h"
#include <math.h>

/** @brief Tint a document rectangle after clipping in logical pixels. */
static void
overlay(tiku_surface_t *s, tiku_rich_rect_t box, double x, double y,
        double zoom, uint32_t colour)
{
    double l = fmax(s->clip.x, x + box.x * zoom);
    double t = fmax(s->clip.y, y + box.y * zoom);
    double r = fmin(s->clip.x + s->clip.w,
                    x + (box.x + box.width) * zoom);
    double b = fmin(s->clip.y + s->clip.h,
                    y + (box.y + box.height) * zoom);
    unsigned a = colour >> 24, other = 255 - a;
    int scale = s->scale ? s->scale : 1;
    int ix, iy;

    if (r <= l || b <= t || !a) return;
    for (iy = (int)floor(t * scale); iy < (int)ceil(b * scale); iy++) {
        for (ix = (int)floor(l * scale); ix < (int)ceil(r * scale); ix++) {
            uint32_t *p = &s->px[(size_t)iy * s->w * scale + ix];
            unsigned red = (((colour >> 16) & 255) * a +
                            ((*p >> 16) & 255) * other + 127) / 255;
            unsigned green = (((colour >> 8) & 255) * a +
                              ((*p >> 8) & 255) * other + 127) / 255;
            unsigned blue = ((colour & 255) * a +
                             (*p & 255) * other + 127) / 255;

            *p = red << 16 | green << 8 | blue;
        }
    }
}

/** @brief Tint selected cell rows without tinting nested cells twice. */
static int
cell_overlay(tiku_richview_t *view, tiku_surface_t *surface,
             double x, double y, double zoom, uint32_t colour)
{
    tiku_rich_rect_t rows[256];
    size_t count;

    if (tiku_richview_cells_rectangles(view, rows, 256, &count) != TIKU_RICH_OK)
        return 0;
    for (size_t i = 0; i < count; i++)
        overlay(surface, rows[i], x, y, zoom, colour);
    return 1;
}

/** @brief Binary-search the first line intersecting the visible top. */
static size_t
first_visible(const tiku_richlayout_t *layout, double top)
{
    size_t lo = 0, hi = tiku_richlayout_lines(layout);
    tiku_rich_line_t line;

    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;

        tiku_richlayout_line(layout, mid, &line);
        if (line.box.y + line.box.height <= top) lo = mid + 1;
        else hi = mid;
    }
    return lo;
}

/** @brief Draw selected picture bounds and an aspect-preserving resize grip. */
static void
picture_outline(tiku_richview_t *view, tiku_surface_t *s,
                tiku_rich_rect_t box, double x, double y, double zoom)
{
    double pen = 1 / zoom;
    tiku_rich_rect_t edge = {box.x, box.y, box.width, pen};

    overlay(s, edge, x, y, zoom, 0xff2864a0);
    edge.y += box.height;
    overlay(s, edge, x, y, zoom, 0xff2864a0);
    edge = (tiku_rich_rect_t){box.x, box.y, pen, box.height};
    overlay(s, edge, x, y, zoom, 0xff2864a0);
    edge.x += box.width;
    overlay(s, edge, x, y, zoom, 0xff2864a0);
    if (!tiku_richsession_read_only(tiku_richview_session(view))) {
        edge = (tiku_rich_rect_t){box.x + box.width - 3 * pen,
            box.y + box.height - 3 * pen, 7 * pen, 7 * pen};
        overlay(s, edge, x, y, zoom, 0xff2864a0);
        edge.x += pen;
        edge.y += pen;
        edge.width -= 2 * pen;
        edge.height -= 2 * pen;
        overlay(s, edge, x, y, zoom, 0xffffffff);
    }
}

/** @brief Paint the clipped visible lines and their selection rectangles. */
static tiku_rich_status_t
paint_lines(tiku_richfont_t *fonts, tiku_richview_t *view,
    tiku_surface_t *s, tiku_rect_t viewport, uint32_t selection,
    tiku_rgb_t caret, int show_caret)
{
    const tiku_richlayout_t *layout = tiku_richview_layout(view);
    double zoom = tiku_richview_zoom(view);
    double y = viewport.y - tiku_richview_scroll_y(view) * zoom;
    size_t i = tiku_richlayout_grids(layout) ? 0 :
               first_visible(layout, (s->clip.y - y) / zoom);
    size_t first = i, count = tiku_richlayout_lines(layout);
    tiku_rich_pos_t from, to;
    tiku_rich_rect_t rect;
    tiku_rich_divider_t divider;
    tiku_rich_status_t status;

    for (; i < count; i++) {
        tiku_rich_line_t line;

        tiku_richlayout_line(layout, i, &line);
        if (!tiku_richlayout_grids(layout) &&
            y + line.box.y * zoom >= s->clip.y + s->clip.h) break;
    }
    count = i - first;
    status = tiku_richfont_draw(fonts, layout, s, viewport.x, y, zoom,
                                first, count);
    if (status != TIKU_RICH_OK) return status;
    if (cell_overlay(view, s, viewport.x, y, zoom, selection))
        return TIKU_RICH_OK;
    if (tiku_richview_divider(view, &divider) == TIKU_RICH_OK) {
        divider.line.width = 2 / zoom;
        overlay(s, divider.line, viewport.x, y, zoom, 0xff2864a0);
        return TIKU_RICH_OK;
    }
    status = tiku_richsession_range(tiku_richview_session(view), &from, &to);
    if (status != TIKU_RICH_OK) return status;
    if (tiku_richview_picture(view, &rect) == TIKU_RICH_OK) {
        picture_outline(view, s, rect, viewport.x, y, zoom);
        return TIKU_RICH_OK;
    }
    if (from.paragraph == to.paragraph && from.byte == to.byte) {
        if (!show_caret) return TIKU_RICH_OK;
        status = tiku_richlayout_caret(layout, to, &rect);
        rect.width = 1 / zoom;
        if (status == TIKU_RICH_OK)
            overlay(s, rect, viewport.x, y, zoom, 0xff000000u | caret);
        return status;
    }
    for (i = first; i < first + count; i++) {
        status = tiku_richlayout_selection(layout, from, to, i, &rect);
        if (status == TIKU_RICH_OK)
            overlay(s, rect, viewport.x, y, zoom, selection);
        else if (status != TIKU_RICH_NO_CHANGE) return status;
    }
    return TIKU_RICH_OK;
}

tiku_rich_status_t
tiku_richfont_draw_view(tiku_richfont_t *fonts, tiku_richview_t *view,
    tiku_surface_t *s, tiku_rect_t viewport, uint32_t selection,
    tiku_rgb_t caret, int show_caret)
{
    tiku_rect_t saved, clip;
    const tiku_richlayout_t *layout;
    tiku_rich_provider_t provider = tiku_richfont_provider(fonts);
    tiku_rich_status_t status;

    if (!fonts || !view || !s || viewport.x < 0 || viewport.y < 0 ||
        viewport.w <= 0 || viewport.h <= 0 || viewport.x > s->w ||
        viewport.y > s->h || viewport.w > s->w - viewport.x ||
        viewport.h > s->h - viewport.y) return TIKU_RICH_INVALID;
    layout = tiku_richview_layout(view);
    if (!layout) return tiku_richview_status(view);
    if (!tiku_richlayout_provider_is(layout, &provider))
        return TIKU_RICH_INVALID;
    /* A zero-line draw validates the surface before changing its clip. */
    status = tiku_richfont_draw(fonts, layout, s, 0, 0, 1, 0, 0);
    if (status != TIKU_RICH_OK) return status;
    saved = s->clip;
    clip.x = saved.x > viewport.x ? saved.x : viewport.x;
    clip.y = saved.y > viewport.y ? saved.y : viewport.y;
    clip.w = (saved.x + saved.w < viewport.x + viewport.w ?
              saved.x + saved.w : viewport.x + viewport.w) - clip.x;
    clip.h = (saved.y + saved.h < viewport.y + viewport.h ?
              saved.y + saved.h : viewport.y + viewport.h) - clip.y;
    if (clip.w <= 0 || clip.h <= 0) return TIKU_RICH_OK;
    tiku_clip_set(s, clip);
    status = paint_lines(fonts, view, s, viewport, selection, caret,
                         show_caret);
    tiku_clip_set(s, saved);
    return status;
}
