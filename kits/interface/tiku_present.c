/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_present.c - a software frame copied into a native backing buffer
 * tile by tile, reporting the rectangles that changed.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_present.h"
#include <limits.h>
#include <string.h>

/**
 * @brief Update native tile @p r, copying whole rows when the physical
 *        sizes match, and return nonzero when it changed.
 */
static int tile(tiku_rgb_t *dst, int w, int h, int stride,
                const tiku_surface_t *src, tiku_rect_t r, int force)
{
    int sc = src->scale > 0 ? src->scale : 1;
    int sw = src->w*sc, sh = src->h*sc, changed = force;
    for (int y = r.y; y < r.y+r.h; y++) {
        tiku_rgb_t *out = dst+(size_t)y*stride+r.x;
        if ((w == sw && h == sh) ||
            (sc > 1 && sw >= w && sh >= h && sw-w < sc && sh-h < sc)) {
            const tiku_rgb_t *in = src->px+(size_t)y*sw+r.x;
            if (force || memcmp(out, in, (size_t)r.w*sizeof *out)) {
                memcpy(out, in, (size_t)r.w*sizeof *out); changed = 1;
            }
        } else {
            for (int x = r.x; x < r.x+r.w; x++) {
                /* Resampled as tiku_scale_pixels() does: logical row
                 * y * src->h / h, column x * src->w / w. */
                tiku_rgb_t px = src->px[((size_t)y*src->h/h)*sc*sw+
                                         ((size_t)x*src->w/w)*sc];
                if (force || out[x-r.x] != px) {
                    out[x-r.x] = px; changed = 1;
                }
            }
        }
    }
    return changed;
}

/** @brief Expand a nonempty rectangle to contain another. */
static tiku_rect_t united(tiku_rect_t a, tiku_rect_t b)
{
    int right = a.x+a.w > b.x+b.w ? a.x+a.w : b.x+b.w;
    int bottom = a.y+a.h > b.y+b.h ? a.y+a.h : b.y+b.h;
    if (b.x < a.x) a.x = b.x;
    if (b.y < a.y) a.y = b.y;
    a.w = right-a.x; a.h = bottom-a.y; return a;
}

int tiku_present_update(tiku_rgb_t *dst, int w, int h, int stride,
    const tiku_surface_t *src, int force, tiku_rect_t *rects, int cap)
{
    return tiku_present_update_in(dst, w, h, stride, src, force, NULL,
                                  rects, cap);
}

int tiku_present_update_in(tiku_rgb_t *dst, int w, int h, int stride,
    const tiku_surface_t *src, int force, const tiku_rect_t *look,
    tiku_rect_t *rects, int cap)
{
    if (!dst || !src || !src->px || !rects || cap < 1 || w < 1 || h < 1 ||
        stride < w || src->w < 1 || src->h < 1 ||
        src->scale < 0 || src->scale > 4 || src->w > INT_MAX/4 ||
        src->h > INT_MAX/4 || w > INT_MAX-64 || h > INT_MAX-32) return -1;
    if (force) {
        rects[0] = (tiku_rect_t){0,0,w,h};
        (void)tile(dst,w,h,stride,src,rects[0],1); return 1;
    }
    /* The tiles under the rectangle, in native pixels and rounded out
     * to whole tiles; the walk is the whole frame when the native size
     * is not the source's own scale, because the tiles then resample. */
    int sc = src->scale > 0 ? src->scale : 1;
    int x0 = 0, y0 = 0, x1 = w, y1 = h;
    if (look && (w == src->w*sc ||
                 (sc > 1 && src->w*sc >= w && src->w*sc-w < sc)) &&
        (h == src->h*sc || (sc > 1 && src->h*sc >= h && src->h*sc-h < sc))) {
        if (look->w <= 0 || look->h <= 0) return 0;
        x0 = look->x*sc/64*64; y0 = look->y*sc/32*32;
        x1 = (look->x+look->w)*sc; y1 = (look->y+look->h)*sc;
        if (x0 < 0) x0 = 0;
        if (y0 < 0) y0 = 0;
        if (x1 > w) x1 = w;
        if (y1 > h) y1 = h;
        if (x0 >= x1 || y0 >= y1) return 0;
    }
    int n = 0, overflow = 0;
    tiku_rect_t bounds = {0};
    for (int y = y0; y < y1; y += 32) {
        for (int x = x0; x < x1; x += 64) {
            tiku_rect_t r = {x,y,w-x < 64 ? w-x : 64,h-y < 32 ? h-y : 32};
            if (!tile(dst,w,h,stride,src,r,0)) continue;
            bounds = n ? united(bounds,r) : r;
            if (n && !overflow && rects[n-1].y == y &&
                rects[n-1].x+rects[n-1].w == x) rects[n-1].w += r.w;
            else if (n < cap) rects[n++] = r;
            else overflow = 1;
        }
    }
    if (overflow) { rects[0] = bounds; return 1; }
    return n;
}
