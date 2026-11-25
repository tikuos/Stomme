/*
 * TikuOS Interface Kit.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * test_surface_view.c - a view draws the pixels a surface of its own size
 * would, into its parent's rectangle and nowhere else, at scale 1 and 2.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_font.h"
#include "tiku_gfx.h"
#include "tiku_logo.h"
#include "tiku_painter.h"
#include "tiku_ui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(t) do{if(!(t)){fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#t);exit(1);}}while(0)
#define VW 110
#define VH 80

/** @brief Every kind of drawing a window does, on @p s from (0, 0). */
static void draw(tiku_surface_t *s, const tiku_surface_t *art)
{
    tiku_painter_t *p;

    tiku_fill(s, (tiku_rect_t){ 0, 0, VW, VH }, TIKU_RGB(40, 60, 80));
    tiku_fill_round(s, (tiku_rect_t){ 4, 4, 40, 20 }, 6, TIKU_CORNER_ALL, TIKU_RGB(200, 90, 30));
    tiku_frame(s, (tiku_rect_t){ 2, 30, 50, 20 }, TIKU_RGB(250, 250, 250));
    tiku_frame_round(s, (tiku_rect_t){ 56, 4, 40, 24 }, 5, TIKU_CORNER_ALL, TIKU_RGB(10, 200, 120));
    tiku_hline(s, 0, VH - 2, VW, TIKU_RGB(255, 0, 0));
    tiku_vline(s, VW - 2, 0, VH, TIKU_RGB(0, 0, 255));
    tiku_pixel(s, 60, 60, TIKU_RGB(1, 2, 3));
    tiku_bevel(s, (tiku_rect_t){ 60, 32, 30, 14 }, TIKU_RGB(220, 220, 220), TIKU_RGB(60, 60, 60));
    tiku_veil(s, (tiku_rect_t){ 10, 50, 40, 12 }, TIKU_RGB(255, 255, 0), 0.4f);
    tiku_shadow(s, (tiku_rect_t){ 70, 50, 24, 16 }, 6, 3, 0.8f);
    tiku_text(s, tiku_font_plain(), 6, 46, "View text", TIKU_RGB(240, 240, 200));
    tiku_ui_button(s, (tiku_rect_t){ 4, 56, 50, 18 }, "OK", 0);
    tiku_ui_gauge(s, (tiku_rect_t){ 58, 70, 40, 6 }, 0.6f);
    tiku_blit(s, 80, 8, art);
    tiku_blend(s, 30, 10, art, 0.5f);
    tiku_blend_scaled(s, (tiku_rect_t){ 40, 60, 24, 14 }, art, 1.0f);
    tiku_copy_bits(s, (tiku_rect_t){ 0, 0, 20, 10 }, 50, 66);
    tiku_invert_frame(s, (tiku_rect_t){ 3, 3, 30, 12 });
    tiku_logo_paint_with(s, (tiku_rect_t){ 90, 50, 18, 18 }, TIKU_LOGO_GROUND, NULL, 1.0f);
    p = tiku_painter_new(s);
    CHECK(p != NULL);
    tiku_painter_ellipse(p, 30.5, 40.5, 14, 9);
    {
        tiku_paint_t paint = tiku_paint_solid(0xC0306090u);
        tiku_painter_fill(p, &paint, TIKU_FILL_NONZERO);
    }
    tiku_painter_free(p);
    /* Clipped drawing stays inside the clip. */
    tiku_clip_set(s, (tiku_rect_t){ 20, 20, 30, 30 });
    tiku_fill(s, (tiku_rect_t){ 0, 0, VW, VH }, TIKU_RGB(9, 9, 9));
    tiku_clip_reset(s);
}

/** @brief Draw through a view at (@p x, @p y) of a parent and into a
 *         surface of the view's size, both at @p scale, and compare. */
static void compare(int scale, int x, int y)
{
    tiku_surface_t *parent = tiku_surface_new(240, 160, TIKU_RGB(7, 7, 7));
    tiku_surface_t *alone = tiku_surface_new(VW, VH, TIKU_RGB(7, 7, 7));
    tiku_surface_t *art = tiku_surface_new(16, 12, TIKU_RGB(90, 160, 210));
    tiku_surface_t *before;
    tiku_surface_t view;
    long ps, as;

    CHECK(parent && alone && art);
    CHECK(tiku_surface_rescale(parent, scale, TIKU_RGB(7, 7, 7)) == 0 &&
          tiku_surface_rescale(alone, scale, TIKU_RGB(7, 7, 7)) == 0 &&
          tiku_surface_rescale(art, scale, TIKU_RGB(90, 160, 210)) == 0);
    tiku_fill(art, (tiku_rect_t){ 3, 3, 6, 4 }, TIKU_RGB(255, 255, 255));
    for (int i = 0; i < 240 * scale * 160 * scale; i++)
        parent->px[i] = (tiku_rgb_t)(i * 2654435761u) & 0xFFFFFFu;
    before = tiku_grab(parent, (tiku_rect_t){ 0, 0, 240, 160 });
    CHECK(before != NULL);
    CHECK(tiku_surface_view(&view, parent, (tiku_rect_t){ x, y, VW, VH }) == 0);
    CHECK(view.w == VW && view.h == VH && tiku_surface_stride(&view) == 240L * scale);
    CHECK(view.clip.x == 0 && view.clip.y == 0 && view.clip.w == VW && view.clip.h == VH);
    /* The same starting pixels in both. */
    ps = tiku_surface_stride(parent);
    as = tiku_surface_stride(alone);
    for (int r = 0; r < VH * scale; r++)
        memcpy(alone->px + r * as, view.px + r * ps, (size_t)VW * scale * sizeof *alone->px);
    draw(&view, art);
    draw(alone, art);
    for (int r = 0; r < 160 * scale; r++)
        for (int c = 0; c < 240 * scale; c++) {
            int in = c >= x * scale && c < (x + VW) * scale && r >= y * scale && r < (y + VH) * scale;
            tiku_rgb_t got = parent->px[r * ps + c];
            tiku_rgb_t want = in ? alone->px[(r - y * scale) * as + c - x * scale]
                                 : before->px[r * ps + c];
            if (got != want) {
                fprintf(stderr, "scale %d at %d,%d (%s): %06x, want %06x\n", scale, c, r,
                        in ? "inside" : "outside", (unsigned)got, (unsigned)want);
                exit(1);
            }
        }
    CHECK(tiku_peek(&view, 60, 60) == tiku_peek(alone, 60, 60));
    {
        tiku_surface_t *a = tiku_grab(&view, (tiku_rect_t){ 5, 5, 30, 20 });
        tiku_surface_t *b = tiku_grab(alone, (tiku_rect_t){ 5, 5, 30, 20 });
        CHECK(a && b && !memcmp(a->px, b->px, (size_t)30 * scale * 20 * scale * sizeof *a->px));
        tiku_surface_free(a);
        tiku_surface_free(b);
    }
    tiku_surface_free(before);
    tiku_surface_free(art);
    tiku_surface_free(alone);
    tiku_surface_free(parent);
}

int main(void)
{
    tiku_surface_t *parent = tiku_surface_new(100, 60, 0);
    tiku_surface_t view;

    compare(1, 37, 23);
    compare(2, 37, 23);
    compare(1, 0, 0);

    /* The parent's clip and repaint region carry over, moved to the view's
     * corner; a rectangle not inside the parent is refused. */
    CHECK(parent != NULL);
    tiku_clip_set(parent, (tiku_rect_t){ 10, 10, 40, 30 });
    CHECK(tiku_surface_view(&view, parent, (tiku_rect_t){ 20, 5, 50, 40 }) == 0);
    CHECK(view.clip.x == 0 && view.clip.y == 5 && view.clip.w == 30 && view.clip.h == 30);
    tiku_clip_set(parent, (tiku_rect_t){ 80, 50, 10, 5 });
    CHECK(tiku_surface_view(&view, parent, (tiku_rect_t){ 0, 0, 30, 30 }) == 0 && view.clip.w == 0);
    CHECK(tiku_surface_view(&view, parent, (tiku_rect_t){ 90, 0, 20, 10 }) == -1);
    CHECK(tiku_surface_view(&view, parent, (tiku_rect_t){ 0, 0, 0, 10 }) == -1);
    tiku_surface_free(parent);
    puts("PASS surface view: drawing through a view matches a surface of its own and stays inside");
    return 0;
}
