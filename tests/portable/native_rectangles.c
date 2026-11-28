/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * native_rectangles.c - clipped fills and copies on strided, scaled views.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_gfx.h"
#include <string.h>

static int accept, fills, blits;

int tiku_gfx_native_fill(tiku_rgb_t *dst, unsigned stride, unsigned w,
                         unsigned h, tiku_rgb_t color)
{
    ++fills;
    if (!accept) return 0;
    for (unsigned y = 0; y < h; y++)
        for (unsigned x = 0; x < w; x++) dst[y * stride + x] = color;
    return 1;
}

int tiku_gfx_native_blit(tiku_rgb_t *dst, unsigned ds, const tiku_rgb_t *src,
                         unsigned ss, unsigned w, unsigned h)
{
    ++blits;
    if (!accept) return 0;
    for (unsigned y = 0; y < h; y++)
        memcpy(dst + y * ds, src + y * ss, w * sizeof *dst);
    return 1;
}

int main(void)
{
    tiku_rgb_t dst[8 * 17], src[8 * 13];
    tiku_surface_t d = {.px=dst + 1, .w=6, .h=4, .scale=2, .stride=17,
                        .clip={1, 1, 3, 2}};
    tiku_surface_t s = {.px=src + 1, .w=5, .h=4, .scale=2, .stride=13};
    for (unsigned i = 0; i < sizeof src / sizeof *src; i++) src[i] = i + 1;
    for (accept = 0; accept <= 1; ++accept) {
        memset(dst, 0, sizeof dst);
        tiku_fill(&d, (tiku_rect_t){-1, 0, 8, 4}, 0x44);
        for (unsigned y = 0; y < 8; y++)
            for (unsigned x = 0; x < 17; x++)
                if (dst[17*y+x] != (y >= 2 && y < 6 && x >= 3 && x < 9 ? 0x44u : 0u))
                    return 1;
        tiku_blit(&d, -1, 0, &s);
        for (unsigned y = 0; y < 8; y++)
            for (unsigned x = 0; x < 17; x++)
                if (dst[17*y+x] != (y >= 2 && y < 6 && x >= 3 && x < 9 ? src[13*y+x+2] : 0u))
                    return 2;
        tiku_blit(&d, 30, 0, &s);
        tiku_fill(&d, (tiku_rect_t){10, 10, 1, 1}, 0);
    }
    s.scale = 1;
    tiku_blit(&d, 0, 0, &s);
    return fills == 2 && blits == 2 ? 0 : 3;
}
