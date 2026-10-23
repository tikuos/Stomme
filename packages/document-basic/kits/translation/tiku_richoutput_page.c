/*
 * TikuDesktop Translation Kit.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_richoutput_page.c - paper preview from completed pagination.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richoutput.h"
#include "tiku_richfont_private.h"
#include <math.h>

tiku_rich_status_t
tiku_richoutput_page(tiku_richfont_t *fonts, const tiku_richpages_t *pages,
    size_t page, tiku_surface_t *surface, double x, double y, double zoom)
{
    tiku_rich_page_setup_t setup;
    tiku_rich_page_t span;
    tiku_rect_t clip, paper;
    tiku_rich_status_t status;
    double left, top, right, bottom;

    if (!surface || !fonts || !isfinite(x) || !isfinite(y) ||
        !isfinite(zoom) || zoom < 0.1 || zoom > 8)
        return TIKU_RICH_INVALID;
    status = tiku_richpages_page(pages, page, &span);
    if (status != TIKU_RICH_OK) return status;
    status = tiku_richfont_draw(fonts, tiku_richpages_layout(pages),
                                surface, x, y, zoom, 0, 0);
    if (status != TIKU_RICH_OK) return status;
    tiku_richpages_setup(pages, &setup);
    clip = surface->clip;
    left = fmax(clip.x, x);
    top = fmax(clip.y, y);
    right = fmin(clip.x + clip.w, x + setup.width * zoom);
    bottom = fmin(clip.y + clip.h, y + setup.height * zoom);
    if (right <= left || bottom <= top) return TIKU_RICH_OK;
    paper = (tiku_rect_t){(int)left, (int)top,
                          (int)ceil(right) - (int)left,
                          (int)ceil(bottom) - (int)top};
    tiku_clip_set(surface, paper);
    tiku_fill(surface, paper, 0xffffff);
    if (span.header_count)
        status = tiku_richfont_draw(fonts, tiku_richpages_layout(pages),
            surface,
            x + setup.left * zoom, y + (setup.top - span.header_top) * zoom,
            zoom, span.header_first, span.header_count);
    if (status == TIKU_RICH_OK)
        status = tiku_richfont_draw(fonts, tiku_richpages_layout(pages),
            surface, x + setup.left * zoom,
            y + (setup.top - span.source_top) * zoom,
            zoom, span.first_line, span.line_count);
    for (size_t i = 0; i < span.piece_count && status == TIKU_RICH_OK; i++) {
        tiku_rich_page_piece_t piece;

        status = tiku_richpages_piece(pages, page, i, &piece);
        if (status == TIKU_RICH_OK)
            status = tiku_rf_draw_piece(fonts, tiku_richpages_layout(pages),
                surface, x + setup.left * zoom, y + setup.top * zoom,
                zoom, &piece);
    }
    tiku_clip_set(surface, clip);
    return status;
}
