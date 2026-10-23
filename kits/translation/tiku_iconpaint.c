/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_iconpaint.c - icons, thumbnails and pictures composited onto an
 * opaque surface, tinted and inverted rubber bands, and haloed text; each
 * recorded on a recording surface's list as a command, or marked there as
 * a draw the list cannot carry.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_dl.h"
#include "tiku_iconpaint.h"
#include "tiku_hvif.h"
#include "tiku_icons.h"

#include <stdlib.h>

/** @brief Blend one premultiplied source pixel over an opaque destination. */
static tiku_rgb_t
over(uint32_t src, tiku_rgb_t dst)
{
    unsigned a = (src >> 24) & 0xFFu;
    unsigned inv = 255u - a;
    unsigned r, g, b;

    /* The source is premultiplied: dst * (1 - a) + src. */
    r = ((src >> 16) & 0xFFu) + (((dst >> 16) & 0xFFu) * inv + 127u) / 255u;
    g = ((src >> 8) & 0xFFu)  + (((dst >> 8) & 0xFFu) * inv + 127u) / 255u;
    b = (src & 0xFFu)         + ((dst & 0xFFu) * inv + 127u) / 255u;
    if (r > 255u) { r = 255u; }
    if (g > 255u) { g = 255u; }
    if (b > 255u) { b = 255u; }
    return TIKU_RGB(r, g, b);
}

/** @brief Composite a premultiplied @p size square bitmap, such as a cached
 *         washed render, with its top-left at (@p x, @p y). */
static int
paint_bmp(tiku_surface_t *s, const uint32_t *bmp, int x, int y,
          int size)
{
    int row, col;

    if (s == NULL || bmp == NULL || size <= 0) {
        return 0;
    }
    for (row = 0; row < size; row++) {
        int py = y + row;

        if (py < s->clip.y || py >= s->clip.y + s->clip.h ||
            py < 0 || py >= s->h) {
            continue;
        }
        for (col = 0; col < size; col++) {
            int px = x + col;
            uint32_t src = bmp[(size_t)row * (size_t)size + (size_t)col];

            if (px < s->clip.x || px >= s->clip.x + s->clip.w ||
                px < 0 || px >= s->w || ((src >> 24) & 0xFFu) == 0u) {
                continue;
            }
            tiku_pixel(s, px, py, over(src, tiku_peek(s, px, py)));
        }
    }
    return 1;
}

/**
 * @brief Composite icon @p name at @p size with its top-left at (@p x,
 *        @p y), washed toward @p wash by @p mix (0 to 1), and greyed when
 *        the theme is achromatic.
 * @return 1 when it was drawn, 0 for an unknown name or a failed render.
 */
static int
paint(tiku_surface_t *s, const char *name, int x, int y, int size,
      float mix, tiku_rgb_t wash)
{
    const uint32_t *bmp;
    int row, col;
    unsigned m;
    /* Asked once per icon; it cannot change while one is painted. */
    int mono = tiku_theme_achromatic();

    /* The public entries record the icon as a command; nothing here
     * marks the list as missing a draw. */
    if (s == NULL || name == NULL || size <= 0) {
        return 0;
    }
    bmp = tiku_icons_bitmap(name, size);
    if (bmp == NULL) {
        return 0;
    }
    if (mix < 0.0f) { mix = 0.0f; }
    if (mix > 1.0f) { mix = 1.0f; }
    m = (unsigned)(mix * 255.0f + 0.5f);

    for (row = 0; row < size; row++) {
        int py = y + row;

        if (py < s->clip.y || py >= s->clip.y + s->clip.h ||
            py < 0 || py >= s->h) {
            continue;
        }
        for (col = 0; col < size; col++) {
            int px = x + col;
            uint32_t src = bmp[(size_t)row * (size_t)size + (size_t)col];
            tiku_rgb_t d;

            if (px < s->clip.x || px >= s->clip.x + s->clip.w ||
                px < 0 || px >= s->w || ((src >> 24) & 0xFFu) == 0u) {
                continue;
            }
            d = over(src, tiku_peek(s, px, py));
            if (m != 0u) {
                unsigned r = ((d >> 16) & 0xFFu) * (255u - m) +
                             ((wash >> 16) & 0xFFu) * m;
                unsigned g = ((d >> 8) & 0xFFu) * (255u - m) +
                             ((wash >> 8) & 0xFFu) * m;
                unsigned b = (d & 0xFFu) * (255u - m) + (wash & 0xFFu) * m;
                d = TIKU_RGB(r / 255u, g / 255u, b / 255u);
            }
            if (mono) {
                d = tiku_grey(d);
            }
            tiku_pixel(s, px, py, d);
        }
    }
    return 1;
}

int
tiku_icon_stream_play(tiku_surface_t *s, const void *hvif, size_t hlen,
                      int x, int y, int size, unsigned mix,
                      tiku_rgb_t wash)
{
    tiku_hvif_t *icon;
    uint32_t *bmp;
    int row, col;
    int mono = tiku_theme_achromatic();

    if (s == NULL || hvif == NULL || size <= 0 || size > 1024) {
        return 0;
    }
    icon = tiku_hvif_parse(hvif, hlen, NULL, 0);
    if (icon == NULL) {
        return 0;
    }
    bmp = (uint32_t *)malloc((size_t)size * (size_t)size * 4u);
    if (bmp == NULL || tiku_hvif_render(icon, bmp, size, size) != 0) {
        free(bmp);
        tiku_hvif_free(icon);
        return 0;
    }
    tiku_hvif_free(icon);
    if (mix > 255u) {
        mix = 255u;
    }
    for (row = 0; row < size; row++) {
        int py = y + row;

        if (py < s->clip.y || py >= s->clip.y + s->clip.h ||
            py < 0 || py >= s->h) {
            continue;
        }
        for (col = 0; col < size; col++) {
            int px = x + col;
            uint32_t src = bmp[(size_t)row * (size_t)size + (size_t)col];
            tiku_rgb_t d;

            if (px < s->clip.x || px >= s->clip.x + s->clip.w ||
                px < 0 || px >= s->w || ((src >> 24) & 0xFFu) == 0u) {
                continue;
            }
            d = over(src, tiku_peek(s, px, py));
            if (mix != 0u) {
                unsigned r = ((d >> 16) & 0xFFu) * (255u - mix) +
                             ((wash >> 16) & 0xFFu) * mix;
                unsigned g = ((d >> 8) & 0xFFu) * (255u - mix) +
                             ((wash >> 8) & 0xFFu) * mix;
                unsigned b = (d & 0xFFu) * (255u - mix) +
                             (wash & 0xFFu) * mix;

                d = TIKU_RGB(r / 255u, g / 255u, b / 255u);
            }
            if (mono) {
                d = tiku_grey(d);
            }
            tiku_pixel(s, px, py, d);
        }
    }
    free(bmp);
    return 1;
}

/**
 * @brief Record on @p s's list the icon an entry just painted.
 *
 * Called between rec_enter and rec_leave by the three icon entries.
 * Nothing is recorded when @p painted is 0.  tiku_dl_icon records or
 * marks a miss itself; a name with no blob behind it, which it cannot
 * see, is marked as a miss here.
 */
static void
record_icon(tiku_surface_t *s, const char *name, int x, int y, int size,
            unsigned mix, tiku_rgb_t wash, int painted)
{
    const tiku_icon_t *ic;

    if (!painted) {
        return;
    }
    ic = tiku_icons_find(name);
    if (ic == NULL) {
        tiku_dl_miss(s->record);
        return;
    }
    (void)tiku_dl_icon(s->record, ic->data, ic->len, x, y, size, mix,
                       wash);
}

int
tiku_icon_paint(tiku_surface_t *s, const char *name, int x, int y,
                    int size)
{
    int rec = tiku_gfx_rec_enter(s);
    int done = paint(s, name, x, y, size, 0.0f, 0);

    if (rec) {
        record_icon(s, name, x, y, size, 0u, 0u, done);
    }
    tiku_gfx_rec_leave(s, rec);
    return done;
}

int
tiku_icon_paint_dim(tiku_surface_t *s, const char *name, int x, int y,
                        int size, float mix, tiku_rgb_t wash)
{
    int rec = tiku_gfx_rec_enter(s);
    int done = paint(s, name, x, y, size, mix, wash);

    if (rec) {
        unsigned m;

        if (mix < 0.0f) { mix = 0.0f; }
        if (mix > 1.0f) { mix = 1.0f; }
        m = (unsigned)(mix * 255.0f + 0.5f);
        record_icon(s, name, x, y, size, m, wash, done);
    }
    tiku_gfx_rec_leave(s, rec);
    return done;
}

int
tiku_icon_paint_selected(tiku_surface_t *s, const char *name, int x,
                             int y, int size)
{
    int rec = tiku_gfx_rec_enter(s);
    int done;

    /* Every channel multiplied by 168/255 with the alpha kept (IV-018),
     * which is a wash toward black at 87/255.  The washed render is cached
     * beside the plain one (IV-019); the per-pixel wash runs only when it
     * cannot be made. */
    {
        const uint32_t *w = tiku_icons_bitmap_washed(name, size,
            0x000000u, 87u);

        done = (w != NULL) ? paint_bmp(s, w, x, y, size)
                           : paint(s, name, x, y, size, 0.341f,
                                   TIKU_RGB(0, 0, 0));
    }
    /* Recorded whichever path drew it, the cached one included. */
    if (rec) {
        record_icon(s, name, x, y, size, 87u, 0u, done);
    }
    tiku_gfx_rec_leave(s, rec);
    return done;
}

/** @brief Blend a scaled thumbnail's straight-alpha samples over the surface. */
static void
thumb_blend(tiku_surface_t *s, const tiku_image_t *th, int x, int y, int size)
{
    tiku_gfx_rec_miss(s);
    for (int py = 0; py < size; py++) {
        long sy = (long)py * th->h / size;
        for (int px = 0; px < size; px++) {
            long sx = (long)px * th->w / size;
            uint32_t p = th->px[sy * th->w + sx];
            unsigned a = p >> 24;
            if (a) {
                unsigned r = (((p >> 16) & 255u) * a + 127u) / 255u;
                unsigned g = (((p >> 8) & 255u) * a + 127u) / 255u;
                unsigned b = ((p & 255u) * a + 127u) / 255u;
                p = (a << 24) | (r << 16) | (g << 8) | b;
                tiku_pixel(s, x + px, y + py, over(p, tiku_peek(s, x + px, y + py)));
            }
        }
    }
}

void
tiku_thumb_paint(tiku_surface_t *s, const tiku_image_t *th,
                     int x, int y, int size)
{
    uint32_t scaled[TIKU_DL_PICTURE_MAX];
    int py, px;

    if (s == NULL || th == NULL || th->px == NULL || size <= 0 ||
        th->w <= 0 || th->h <= 0) {
        return;
    }
    if ((long)size * (long)size > TIKU_DL_PICTURE_MAX) {
        thumb_blend(s, th, x, y, size);
        return;
    }
    /*
     * Scaled here and drawn with tiku_picture(), so a recording list
     * carries the scaled bitmap.  Its transparent margin goes with it, and
     * the row shows through the margin (IV-034).
     */
    for (py = 0; py < size; py++) {
        long sy = (long)py * th->h / size;

        for (px = 0; px < size; px++) {
            long sx = (long)px * th->w / size;

            scaled[py * size + px] = th->px[sy * th->w + sx];
            unsigned a = scaled[py * size + px] >> 24;
            if (a && a != 255) {
                thumb_blend(s, th, x, y, size);
                return;
            }
        }
    }
    tiku_picture(s, x, y, size, size, scaled);
}

int
tiku_icon_hit(const char *name, int size, int px, int py)
{
    const uint32_t *bm;

    if (name == NULL || size <= 0 || px < 0 || py < 0 ||
        px >= size || py >= size) {
        return 0;
    }
    bm = tiku_icons_bitmap(name, size);
    if (bm == NULL) {
        /* No art: the whole square answers, so the icon can still be
         * picked up. */
        return 1;
    }
    /* Any alpha counts, so the antialiased rim, most of a small icon, can
     * be grabbed. */
    return ((bm[(long)py * size + px] >> 24) != 0u);
}

void
tiku_paint_tint(tiku_surface_t *s, tiku_rect_t r,
                    tiku_rgb_t c, int a)
{
    tiku_gfx_rec_miss(s);   /* a blend has no display-list command */

    int x, y;
    int cr = (int)((c >> 16) & 0xffu), cg = (int)((c >> 8) & 0xffu);
    int cb = (int)(c & 0xffu);

    if (s == NULL || s->px == NULL || a <= 0) {
        return;
    }
    for (y = r.y; y < r.y + r.h; y++) {
        if (y < s->clip.y || y >= s->clip.y + s->clip.h ||
            y < 0 || y >= s->h) {
            continue;
        }
        for (x = r.x; x < r.x + r.w; x++) {
            tiku_rgb_t px;
            int pr, pg, pb;

            if (x < s->clip.x || x >= s->clip.x + s->clip.w ||
                x < 0 || x >= s->w) {
                continue;
            }
            px = tiku_peek(s, x, y);
            pr = (int)((px >> 16) & 0xffu);
            pg = (int)((px >> 8) & 0xffu);
            pb = (int)(px & 0xffu);
            pr = (pr * (255 - a) + cr * a) / 255;
            pg = (pg * (255 - a) + cg * a) / 255;
            pb = (pb * (255 - a) + cb * a) / 255;
            tiku_pixel(s, x, y, TIKU_RGB(pr, pg, pb));
        }
    }
}

void
tiku_paint_band(tiku_surface_t *s, tiku_rect_t r,
                    int transparent)
{
    /*
     * No miss marked here: tiku_invert_frame and each of the five tints
     * mark their own.
     */
    if (s == NULL || r.w <= 0 || r.h <= 0) {
        return;
    }
    if (!transparent) {
        tiku_invert_frame(s, r);
        return;
    }
    {
        /* The edge is four one-pixel strips of the focus colour at
         * 128/255, the interior the selection colour at 90/255; the strips
         * do not overlap the interior. */
        tiku_rect_t in = { r.x + 1, r.y + 1, r.w - 2, r.h - 2 };
        tiku_rect_t e;

        e = (tiku_rect_t){ r.x, r.y, r.w, 1 };
        tiku_paint_tint(s, e, TIKU_C_FOCUS, 128);
        e = (tiku_rect_t){ r.x, r.y + r.h - 1, r.w, 1 };
        tiku_paint_tint(s, e, TIKU_C_FOCUS, 128);
        e = (tiku_rect_t){ r.x, r.y, 1, r.h };
        tiku_paint_tint(s, e, TIKU_C_FOCUS, 128);
        e = (tiku_rect_t){ r.x + r.w - 1, r.y, 1, r.h };
        tiku_paint_tint(s, e, TIKU_C_FOCUS, 128);
        if (in.w > 0 && in.h > 0) {
            tiku_paint_tint(s, in, TIKU_C_SELECT, 90);
        }
    }
}

void
tiku_paint_image(tiku_surface_t *s, const tiku_image_t *im,
                     tiku_rect_t dst, tiku_rect_t clip)
{
    int px, py;

    if (s == NULL || im == NULL || im->px == NULL || im->w <= 0 ||
        im->h <= 0 || dst.w <= 0 || dst.h <= 0) {
        return;
    }
    if (s != NULL && s->record != NULL && s->record_depth == 0) {
        tiku_dl_miss(s->record);        /* a picture has no command */
    }
    for (py = 0; py < dst.h; py++) {
        int y = dst.y + py;
        long sy = (long)py * im->h / dst.h;

        if (y < clip.y || y >= clip.y + clip.h) {
            continue;
        }
        for (px = 0; px < dst.w; px++) {
            int x = dst.x + px;
            long sx = (long)px * im->w / dst.w;
            uint32_t p;

            if (x < clip.x || x >= clip.x + clip.w) {
                continue;
            }
            p = im->px[sy * im->w + sx];
            /* Transparent pixels are skipped: what is behind shows through
             * a hole in the picture. */
            if ((p >> 24) != 0u) {
                tiku_rgb_t c = TIKU_RGB((p >> 16) & 0xffu,
                                             (p >> 8) & 0xffu, p & 0xffu);

                /* Greyed with everything else on an achromatic theme. */
                tiku_pixel(s, x, y,
                                tiku_theme_achromatic() ? tiku_grey(c) : c);
            }
        }
    }
}

void
tiku_paint_text_halo(tiku_surface_t *s, const tiku_font_t *f,
                         int x, int y, const char *text, tiku_rgb_t ink)
{
    static const int dx[] = { -1, 0, 1, -1, 1, -1, 0, 1 };
    static const int dy[] = { -1, -1, -1, 0, 0, 1, 1, 1 };
    int luminance = ((int)((ink >> 16) & 0xffu) * 299 +
                     (int)((ink >> 8) & 0xffu) * 587 +
                     (int)(ink & 0xffu) * 114) / 1000;
    tiku_rgb_t halo = (luminance > 127) ? TIKU_RGB(0, 0, 0)
                                             : TIKU_RGB(255, 255, 255);
    int i;

    if (s == NULL || f == NULL || text == NULL) {
        return;
    }
    for (i = 0; i < 8; i++) {
        tiku_text(s, f, x + dx[i], y + dy[i], text, halo);
    }
    tiku_text(s, f, x, y, text, ink);
}
