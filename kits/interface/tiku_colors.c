/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_colors.c - the palette grid.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <string.h>

#include "tiku_colors.h"

#define R_OF(c) (int)(((c) >> 16) & 0xffu)
#define G_OF(c) (int)(((c) >> 8) & 0xffu)
#define B_OF(c) (int)((c) & 0xffu)

void
tiku_colors_init(tiku_colors_t *c, int cols)
{
    if (c == NULL) {
        return;
    }
    memset(c, 0, sizeof *c);
    c->cols = (cols > 0) ? cols : 8;
    c->current = -1;
}

int
tiku_colors_add(tiku_colors_t *c, tiku_rgb_t rgb)
{
    int at;

    if (c == NULL || c->count >= TIKU_COLORS_MAX) {
        return -1;
    }
    at = c->count;
    c->swatch[at] = rgb;
    c->count++;
    if (c->current < 0) {
        c->current = 0;
    }
    return at;
}

void
tiku_colors_default(tiku_colors_t *c)
{
    /*
     * Four rows of eight: dark hues, the same at full strength, softened,
     * and a light row, with a grey ramp from black to white down the
     * first column and along the last row.
     */
    static const tiku_rgb_t table[32] = {
        TIKU_RGB(0, 0, 0),       TIKU_RGB(128, 0, 0),
        TIKU_RGB(196, 84, 0),    TIKU_RGB(200, 160, 0),
        TIKU_RGB(0, 112, 48),    TIKU_RGB(0, 96, 144),
        TIKU_RGB(48, 48, 152),   TIKU_RGB(96, 32, 120),

        TIKU_RGB(64, 64, 64),    TIKU_RGB(208, 48, 48),
        TIKU_RGB(255, 140, 32),  TIKU_RGB(255, 203, 0),
        TIKU_RGB(48, 176, 96),   TIKU_RGB(51, 102, 152),
        TIKU_RGB(96, 96, 216),   TIKU_RGB(160, 80, 192),

        TIKU_RGB(128, 128, 128), TIKU_RGB(240, 128, 128),
        TIKU_RGB(255, 190, 128), TIKU_RGB(255, 232, 137),
        TIKU_RGB(144, 216, 168), TIKU_RGB(150, 190, 220),
        TIKU_RGB(176, 176, 240), TIKU_RGB(208, 160, 232),

        TIKU_RGB(192, 192, 192), TIKU_RGB(216, 216, 216),
        TIKU_RGB(232, 232, 232), TIKU_RGB(244, 244, 244),
        TIKU_RGB(255, 255, 255), TIKU_RGB(232, 232, 200),
        TIKU_RGB(200, 216, 232), TIKU_RGB(216, 200, 216)
    };
    int i;

    tiku_colors_init(c, 8);
    for (i = 0; i < 32; i++) {
        (void)tiku_colors_add(c, table[i]);
    }
}

int
tiku_colors_count(const tiku_colors_t *c)
{
    return (c != NULL) ? c->count : 0;
}

int
tiku_colors_current(const tiku_colors_t *c)
{
    return (c != NULL) ? c->current : -1;
}

tiku_rgb_t
tiku_colors_value(const tiku_colors_t *c)
{
    if (c == NULL || c->current < 0 || c->current >= c->count) {
        return TIKU_RGB(0, 0, 0);
    }
    return c->swatch[c->current];
}

int
tiku_colors_select(tiku_colors_t *c, int index)
{
    if (c == NULL || c->count == 0) {
        return -1;
    }
    if (index < 0) { index = 0; }
    if (index >= c->count) { index = c->count - 1; }
    c->current = index;
    return c->current;
}

int
tiku_colors_select_nearest(tiku_colors_t *c, tiku_rgb_t rgb)
{
    long best = -1;
    int at = -1, i;

    if (c == NULL || c->count == 0) {
        return -1;
    }
    for (i = 0; i < c->count; i++) {
        long dr = R_OF(c->swatch[i]) - R_OF(rgb);
        long dg = G_OF(c->swatch[i]) - G_OF(rgb);
        long db = B_OF(c->swatch[i]) - B_OF(rgb);
        long d = dr * dr + dg * dg + db * db;

        if (best < 0 || d < best) {
            best = d;
            at = i;
        }
    }
    c->current = at;
    return at;
}

tiku_rect_t
tiku_colors_rect(const tiku_colors_t *c, tiku_rect_t r, int index)
{
    tiku_rect_t sw = { r.x, r.y, 0, 0 };
    int cols, rows;

    if (c == NULL || c->count <= 0 || index < 0 || index >= c->count) {
        return sw;
    }
    cols = (c->cols > 0) ? c->cols : 1;
    rows = (c->count + cols - 1) / cols;
    sw.w = r.w / cols;
    sw.h = r.h / rows;
    sw.x = r.x + (index % cols) * sw.w;
    sw.y = r.y + (index / cols) * sw.h;
    return sw;
}

int
tiku_colors_hit(const tiku_colors_t *c, tiku_rect_t r, int x, int y)
{
    int i, n = tiku_colors_count(c);

    for (i = 0; i < n; i++) {
        tiku_rect_t sw = tiku_colors_rect(c, r, i);

        if (sw.w > 0 && sw.h > 0 &&
            x >= sw.x && x < sw.x + sw.w &&
            y >= sw.y && y < sw.y + sw.h) {
            return i;
        }
    }
    return -1;
}
