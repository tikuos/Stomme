/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_splash.c - painting the splash screen.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <string.h>

#include "tiku_font.h"
#include "tiku_logo.h"
#include "tiku_splash.h"
#include "tiku_ui.h"

/** @brief The artwork band's height: half the splash screen's. */
static int
band_h(int h)
{
    return h / 2;
}

/**
 * @brief The built-in artwork: the mark's palettes swept across the band
 *        from left to right, with the mark large right of centre.
 *
 * Each column runs from its palette's tint at the top to its accent at
 * the bottom.
 */
static void
band_paint(tiku_surface_t *s, int w, int h)
{
    int sets = tiku_logo_palette_count();
    int x, y;

    for (x = 1; x < w - 1; x++) {
        float along = (float)x / (float)(w > 1 ? w - 1 : 1);
        float seat = along * (float)(sets > 1 ? sets - 1 : 1);
        tiku_logo_palette_t p;

        tiku_logo_palette((int)seat, seat - (float)(int)seat, &p);
        for (y = 1; y < h; y++) {
            float down = (float)y / (float)h;
            int tr = (int)(p.tint >> 16) & 0xff;
            int tg = (int)(p.tint >> 8) & 0xff;
            int tb = (int)p.tint & 0xff;
            int ar = (int)(p.accent >> 16) & 0xff;
            int ag = (int)(p.accent >> 8) & 0xff;
            int ab = (int)p.accent & 0xff;

            tiku_pixel(s, x, y,
                TIKU_RGB(tr + (int)((ar - tr) * down),
                              tg + (int)((ag - tg) * down),
                              tb + (int)((ab - tb) * down)));
        }
    }
    {
        int mark = h - 14;
        tiku_rect_t at = { (w * 5) / 8, 8, mark, mark };

        tiku_logo_paint(s, at, 0u);
    }
    tiku_hline(s, 1, h, w - 2, tiku_tint(TIKU_C_PANEL, TIKU_DARKEN_2));
}

/**
 * @brief Draw @p art scaled to cover the band, centred, with what
 *        overflows it cropped.
 */
static void
art_paint(tiku_surface_t *s, int w, int h,
          const tiku_surface_t *art)
{
    int x, y;
    float scale, sw, sh;
    float off_x, off_y;

    if (art->w < 1 || art->h < 1) {
        return;
    }
    sw = (float)w / (float)art->w;
    sh = (float)h / (float)art->h;
    scale = (sw > sh) ? sw : sh;
    off_x = ((float)art->w - (float)w / scale) / 2.0f;
    off_y = ((float)art->h - (float)h / scale) / 2.0f;
    for (y = 1; y < h; y++) {
        for (x = 1; x < w - 1; x++) {
            int ax = (int)(off_x + (float)x / scale);
            int ay = (int)(off_y + (float)y / scale);

            if (ax < 0) { ax = 0; }
            if (ay < 0) { ay = 0; }
            if (ax >= art->w) { ax = art->w - 1; }
            if (ay >= art->h) { ay = art->h - 1; }
            tiku_pixel(s, x, y, art->px[ay * art->w + ax]);
        }
    }
    tiku_hline(s, 1, h, w - 2, tiku_tint(TIKU_C_PANEL, TIKU_DARKEN_2));
}

void
tiku_splash_paint(tiku_surface_t *s, const char *name,
                       const char *const *lines, int nlines,
                       const char *version, const char *status,
                       const tiku_surface_t *art)
{
    const tiku_font_t *big = tiku_font_at(26);
    const tiku_font_t *f = tiku_font_plain();
    tiku_rect_t all;
    int band, x, y, i;

    if (s == NULL || name == NULL) {
        return;
    }
    all = (tiku_rect_t){ 0, 0, s->w, s->h };
    band = band_h(s->h);
    x = 18;
    /* Document paper with a one-pixel frame. */
    tiku_fill(s, all, TIKU_C_DOC);
    tiku_frame(s, all, tiku_tint(TIKU_C_PANEL, TIKU_DARKEN_2));
    if (art != NULL) {
        art_paint(s, s->w, band, art);
    } else {
        band_paint(s, s->w, band);
    }
    /* The name, in a large face, at the left under the artwork. */
    y = band + 12;
    tiku_text(s, big, x, y + big->ascent, name, TIKU_C_TEXT);
    y += big->height + 6;
    /* The status, under the name. */
    if (status != NULL) {
        tiku_text(s, f, x, y + f->ascent, status,
                       tiku_tint(TIKU_C_PANEL, TIKU_DARKEN_2));
    }
    y += f->height + 10;
    /* The credit lines, as a block. */
    for (i = 0; i < nlines && i < TIKU_SPLASH_LINES; i++) {
        if (lines[i] == NULL) {
            break;
        }
        tiku_text(s, f, x, y + f->ascent, lines[i],
                       tiku_tint(TIKU_C_PANEL, TIKU_DARKEN_2));
        y += f->height + 2;
    }
    /* The corners: the version at the left, the mark at the right. */
    if (version != NULL) {
        tiku_text(s, f, x, s->h - 10 - f->height + f->ascent, version,
                       tiku_tint(TIKU_C_PANEL, TIKU_DARKEN_1));
    }
    tiku_logo_paint(s, (tiku_rect_t){ s->w - 34, s->h - 34, 22, 22 },
                         0u);
}

void
tiku_splash_license(tiku_surface_t *s)
{
    /* The boilerplate notice from the Apache License 2.0's appendix,
     * without its copyright placeholder; only the line breaks differ. */
    static const char *const notice[] = {
        "Licensed under the Apache License, Version 2.0 (the \"License\");",
        "you may not use this file except in compliance with the License. You may obtain a copy of",
        "the License at http://www.apache.org/licenses/LICENSE-2.0",
        "Unless required by applicable law or agreed to in writing, software distributed under the",
        "License is distributed on an \"AS IS\" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF",
        "ANY KIND, either express or implied. See the License for the specific language governing",
        "permissions and limitations under the License."
    };
    const tiku_font_t *small = tiku_font_at(10);
    /* Four pixels under the status line: the band, name and status
     * offsets repeat tiku_splash_paint()'s and must change with them. */
    int y = band_h(s->h) + 12 + tiku_font_at(26)->height + 6 +
            tiku_font_plain()->height + 4;
    size_t i;

    for (i = 0; i < sizeof notice / sizeof *notice; i++) {
        tiku_text(s, small, 18, y + small->ascent, notice[i],
                  TIKU_RGB(128, 128, 128));
        /* A pixel tighter than the face, so the seven lines end above the
         * card's bottom edge. */
        y += small->height - 1;
    }
}
