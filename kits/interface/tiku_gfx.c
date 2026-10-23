/*
 * KintsugiOS v0.01 kits -- the foundation KintsugiDesktop and KintsugiTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@kinstugi-os.org>
 *
 * tiku_gfx.c - surface primitives and the PNG writer.
 *
 * Every primitive clips against the surface's clip rectangle, so callers
 * draw in widget coordinates without testing bounds.  The PNG writer emits
 * stored (uncompressed) deflate blocks and needs no zlib.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_dl.h"
#include "tiku_gfx.h"

int
tiku_gfx_rec_enter(tiku_surface_t *s)
{
    if (s == NULL || s->record == NULL || s->record_depth > 0) {
        return 0;
    }
    s->record_depth++;
    return 1;
}

void
tiku_gfx_rec_miss(tiku_surface_t *s)
{
    if (s != NULL && s->record != NULL && s->record_depth == 0) {
        tiku_dl_miss(s->record);
    }
}

void
tiku_gfx_rec_leave(tiku_surface_t *s, int on)
{
    if (on) {
        s->record_depth--;
    }
}

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void
tiku_scale_pixels(tiku_rgb_t *destination,
                       int destination_width, int destination_height,
                       const tiku_rgb_t *source,
                       int source_width, int source_height)
{
    int scale, x, y, repeat;

    if (destination == NULL || source == NULL || destination_width <= 0 ||
        destination_height <= 0 || source_width <= 0 || source_height <= 0) {
        return;
    }
    scale = destination_width / source_width;
    if (scale > 1 && destination_width == source_width * scale &&
        destination_height == source_height * scale) {
        for (y = 0; y < source_height; y++) {
            tiku_rgb_t *row = destination +
                (size_t)y * scale * destination_width;

            for (x = 0; x < source_width; x++) {
                for (repeat = 0; repeat < scale; repeat++) {
                    row[x * scale + repeat] =
                        source[(size_t)y * source_width + x];
                }
            }
            for (repeat = 1; repeat < scale; repeat++) {
                memcpy(row + (size_t)repeat * destination_width, row,
                       (size_t)destination_width * sizeof *row);
            }
        }
        return;
    }
    if (destination_width == source_width &&
        destination_height == source_height) {
        memcpy(destination, source,
               (size_t)source_width * source_height * sizeof *source);
        return;
    }
    for (y = 0; y < destination_height; y++) {
        int source_y = (int)((int64_t)y * source_height /
                             destination_height);

        for (x = 0; x < destination_width; x++) {
            int source_x = (int)((int64_t)x * source_width /
                                 destination_width);

            destination[(size_t)y * destination_width + x] =
                source[(size_t)source_y * source_width + source_x];
        }
    }
}

/** @brief Native pixels per logical pixel; a zeroed surface reads as 1. */
static int
scale_of(const tiku_surface_t *s)
{
    return (s != NULL && s->scale > 1) ? s->scale : 1;
}

tiku_surface_t *
tiku_surface_new(int w, int h, tiku_rgb_t bg)
{
    tiku_surface_t *s;
    long i, n = (long)w * (long)h;   /* scale 1: native is logical */

    if (w <= 0 || h <= 0) {
        return NULL;
    }
    s = calloc(1, sizeof *s);
    if (s == NULL) {
        return NULL;
    }
    s->px = malloc((size_t)n * sizeof *s->px);
    if (s->px == NULL) {
        free(s);
        return NULL;
    }
    s->w = w;
    s->h = h;
    for (i = 0; i < n; i++) {
        s->px[i] = bg;
    }
    tiku_clip_reset(s);
    return s;
}

int
tiku_surface_resize(tiku_surface_t *s, int w, int h,
                         tiku_rgb_t bg)
{
    tiku_rgb_t *pixels;
    size_t i, count, sc;

    if (s == NULL || w <= 0 || h <= 0) {
        return -1;
    }
    sc = (size_t)scale_of(s);
    if ((size_t)w * sc > SIZE_MAX / sizeof *pixels / ((size_t)h * sc)) {
        return -1;
    }
    count = (size_t)w * sc * (size_t)h * sc;
    pixels = malloc(count * sizeof *pixels);
    if (pixels == NULL) {
        return -1;
    }
    for (i = 0; i < count; i++) {
        pixels[i] = bg;
    }
    free(s->px);
    s->px = pixels;
    s->w = w;
    s->h = h;
    s->damage_active = 0;
    tiku_clip_reset(s);
    return 0;
}

int
tiku_surface_rescale(tiku_surface_t *s, int scale,
                          tiku_rgb_t bg)
{
    int previous, result;

    if (s == NULL || scale < 1 || scale > 4) {
        return -1;
    }
    previous = s->scale;
    s->scale = scale;
    result = tiku_surface_resize(s, s->w, s->h, bg);
    if (result != 0) {
        s->scale = previous;
    }
    return result;
}

tiku_rgb_t
tiku_peek(const tiku_surface_t *s, int x, int y)
{
    int sc = scale_of(s);

    if (s == NULL || s->px == NULL || x < 0 || y < 0 ||
        x >= s->w || y >= s->h) {
        return 0;
    }
    return s->px[(long)y * sc * ((long)s->w * sc) + (long)x * sc];
}

void
tiku_surface_free(tiku_surface_t *s)
{
    if (s != NULL) {
        free(s->px);
        free(s);
    }
}

tiku_rect_t tiku_rect_union(tiku_rect_t a, tiku_rect_t b)
{
    if (a.w <= 0 || a.h <= 0) return b;
    if (b.w <= 0 || b.h <= 0) return a;
    int right = a.x+a.w > b.x+b.w ? a.x+a.w : b.x+b.w;
    int bottom = a.y+a.h > b.y+b.h ? a.y+a.h : b.y+b.h;
    if (b.x < a.x) a.x = b.x;
    if (b.y < a.y) a.y = b.y;
    a.w = right-a.x; a.h = bottom-a.y; return a;
}

void
tiku_clip_reset(tiku_surface_t *s)
{
    if (s != NULL && s->record != NULL && s->record_depth == 0) {
        (void)tiku_dl_clip_reset(s->record);
    }
    if (s != NULL) {
        s->clip.x = 0;
        s->clip.y = 0;
        s->clip.w = s->w;
        s->clip.h = s->h;
        if (s->damage_active) tiku_clip_set(s, s->damage);
    }
}

void
tiku_clip_set(tiku_surface_t *s, tiku_rect_t r)
{
    int x0, y0, x1, y1;

    if (s == NULL) {
        return;
    }
    if (s->record != NULL && s->record_depth == 0) {
        (void)tiku_dl_clip_set(s->record, r);
    }
    x0 = (r.x > 0) ? r.x : 0;
    y0 = (r.y > 0) ? r.y : 0;
    x1 = r.x + r.w;
    y1 = r.y + r.h;
    if (x1 > s->w) { x1 = s->w; }
    if (y1 > s->h) { y1 = s->h; }
    if (s->damage_active) {
        if (x0 < s->damage.x) x0 = s->damage.x;
        if (y0 < s->damage.y) y0 = s->damage.y;
        if (x1 > s->damage.x+s->damage.w) x1 = s->damage.x+s->damage.w;
        if (y1 > s->damage.y+s->damage.h) y1 = s->damage.y+s->damage.h;
    }
    s->clip.x = x0;
    s->clip.y = y0;
    s->clip.w = (x1 > x0) ? x1 - x0 : 0;
    s->clip.h = (y1 > y0) ? y1 - y0 : 0;
}

void tiku_clip_damage(tiku_surface_t *s, const tiku_rect_t *region)
{
    if (!s) return;
    s->damage_active = 0;
    tiku_clip_reset(s);
    if (region) {
        tiku_clip_set(s, *region);
        s->damage = s->clip;
        s->damage_active = 1;
    }
}

tiku_rgb_t
tiku_tint(tiku_rgb_t c, float tint)
{
    int i, out[3];
    int ch[3];

    ch[0] = (int)((c >> 16) & 0xFFu);
    ch[1] = (int)((c >> 8) & 0xFFu);
    ch[2] = (int)(c & 0xFFu);
    for (i = 0; i < 3; i++) {
        float v;
        if (tint < 1.0f) {
            v = 255.0f - (255.0f - (float)ch[i]) * tint;
        } else {
            v = (float)ch[i] * (2.0f - tint);
        }
        if (v < 0.0f)   { v = 0.0f; }
        if (v > 255.0f) { v = 255.0f; }
        out[i] = (int)(v + 0.5f);
    }
    return TIKU_RGB(out[0], out[1], out[2]);
}

tiku_rgb_t
tiku_dim(tiku_rgb_t ink, tiku_rgb_t ground)
{
    return TIKU_RGB(
        (((ink >> 16) & 0xFFu) + ((ground >> 16) & 0xFFu)) / 2u,
        (((ink >> 8) & 0xFFu) + ((ground >> 8) & 0xFFu)) / 2u,
        ((ink & 0xFFu) + (ground & 0xFFu)) / 2u);
}

/* Display-list ink values use these byte-sized numeric identifiers. */
_Static_assert(TIKU_INK_PLAIN == 0 && TIKU_INK_FAINT == 1 &&
               TIKU_INK_WORD == 2 && TIKU_INK_NUM == 3 &&
               TIKU_INK_STR == 4 && TIKU_INK_REMARK == 5 &&
               TIKU_INK_WRONG == 6 && TIKU_INK_PATH == 7 &&
               TIKU_INK_LINK == 8,
               "display-list ink identifiers must remain stable");

tiku_rgb_t
tiku_ink(tiku_ink_t which)
{
    switch (which) {
    case TIKU_INK_FAINT:
        /* Ordinary ink pulled halfway toward the ground it is read on, so
         * it recedes on every table. */
        return tiku_dim(TIKU_C_TEXT, TIKU_C_DOC);
    case TIKU_INK_WORD:   return TIKU_C_CODE_WORD;
    case TIKU_INK_NUM:    return TIKU_C_CODE_NUM;
    case TIKU_INK_STR:    return TIKU_C_CODE_STR;
    case TIKU_INK_REMARK: return TIKU_C_CODE_REM;
    case TIKU_INK_WRONG:  return TIKU_C_DANGER;
    case TIKU_INK_PATH:
        /* A string that is also an address: the string's ink met
         * halfway with the selection colour. */
        return tiku_dim(TIKU_C_CODE_STR, TIKU_C_SELECT);
    case TIKU_INK_LINK:
        /* The selection colour, which under a table with no hue is close
         * to the document's own ink. */
        return TIKU_C_SELECT;
    case TIKU_INK_PLAIN:
    default:
        /* An ink this build does not know reads as the document's own. */
        return TIKU_C_TEXT;
    }
}

void
tiku_pixel(tiku_surface_t *s, int x, int y, tiku_rgb_t c)
{
    /* Before the clip test, so a pixel clipped away still marks the list
     * not whole. */
    tiku_gfx_rec_miss(s);
    int sc, ry, rx;
    long stride;

    if (s == NULL || x < s->clip.x || y < s->clip.y ||
        x >= s->clip.x + s->clip.w || y >= s->clip.y + s->clip.h) {
        return;
    }
    sc = scale_of(s);
    stride = (long)s->w * sc;
    for (ry = 0; ry < sc; ry++) {
        tiku_rgb_t *row = s->px + ((long)y * sc + ry) * stride +
                               (long)x * sc;
        for (rx = 0; rx < sc; rx++) {
            row[rx] = c;
        }
    }
}

void
tiku_picture(tiku_surface_t *s, int x, int y, int w, int h,
                  const uint32_t *px)
{
    int py, sx;

    if (s == NULL || px == NULL || w < 1 || h < 1) {
        return;
    }
    {
        /*
         * Recorded at the outermost call only, like every other command:
         * a picture drawn inside a control that is already recording is
         * part of that control.  Entering also keeps the pixels below from
         * recording, so tiku_pixel() marks no miss for a picture that was
         * carried.
         */
        int rec = tiku_gfx_rec_enter(s);

        if (rec && !tiku_dl_picture(s->record, x, y, w, h, px)) {
            tiku_dl_miss(s->record);
        }
        for (py = 0; py < h; py++) {
            for (sx = 0; sx < w; sx++) {
                uint32_t v = px[(size_t)py * (size_t)w + sx];

                if ((v >> 24) != 0u) {
                    tiku_pixel(s, x + sx, y + py,
                        TIKU_RGB((v >> 16) & 0xffu, (v >> 8) & 0xffu,
                                      v & 0xffu));
                }
            }
        }
        tiku_gfx_rec_leave(s, rec);
    }
}

void
tiku_blit(tiku_surface_t *dst, int x, int y,
               const tiku_surface_t *src)
{
    tiku_gfx_rec_miss(dst);  /* a surface copied in is a picture */

    int y0, y1, x0, x1, row;

    if (dst == NULL || src == NULL || dst->px == NULL || src->px == NULL) {
        return;
    }
    x0 = x;
    y0 = y;
    x1 = x + src->w;
    y1 = y + src->h;
    /* The clip alone: it is held inside the surface, so trimming to it
     * trims to the surface too. */
    if (x0 < dst->clip.x)               { x0 = dst->clip.x; }
    if (y0 < dst->clip.y)               { y0 = dst->clip.y; }
    if (x1 > dst->clip.x + dst->clip.w) { x1 = dst->clip.x + dst->clip.w; }
    if (y1 > dst->clip.y + dst->clip.h) { y1 = dst->clip.y + dst->clip.h; }
    if (x1 <= x0 || y1 <= y0) return;
    if (scale_of(dst) == scale_of(src)) {
        int sc = scale_of(dst), sub;
        long dstride = (long)dst->w * sc;
        long sstride = (long)src->w * sc;

        for (row = y0; row < y1; row++) {
            for (sub = 0; sub < sc; sub++) {
                memcpy(dst->px + ((long)row * sc + sub) * dstride +
                       (long)x0 * sc,
                       src->px + ((long)(row - y) * sc + sub) * sstride +
                       (long)(x0 - x) * sc,
                       (size_t)(x1 - x0) * sc * sizeof *dst->px);
            }
        }
    } else {
        /* Unequal scales: carry each source pixel over logically. */
        int col;
        int ss = scale_of(src);
        long sstride = (long)src->w * ss;

        for (row = y0; row < y1; row++) {
            for (col = x0; col < x1; col++) {
                tiku_pixel(dst, col, row,
                    src->px[(long)(row - y) * ss * sstride +
                            (long)(col - x) * ss]);
            }
        }
    }
}

void
tiku_copy_bits(tiku_surface_t *s, tiku_rect_t src, int dx,
                    int dy)
{
    tiku_gfx_rec_miss(s);   /* no command moves pixels */

    int y, x0, y0, x1, y1, w;

    if (s == NULL || (dx == 0 && dy == 0) || src.w <= 0 || src.h <= 0) {
        return;
    }
    /* Clipped on both ends: the source is read from the surface and the
     * destination is written into the clip, so a block that would run off
     * either is trimmed before a pixel moves.  The destination answers to
     * the clip as every other drawing does, so a list that slides its
     * rows does not carry them over its own border. */
    x0 = src.x;
    y0 = src.y;
    x1 = src.x + src.w;
    y1 = src.y + src.h;
    if (x0 < 0) { x0 = 0; }
    if (y0 < 0) { y0 = 0; }
    if (x1 > s->w) { x1 = s->w; }
    if (y1 > s->h) { y1 = s->h; }
    if (x0 + dx < s->clip.x) { x0 = s->clip.x - dx; }
    if (y0 + dy < s->clip.y) { y0 = s->clip.y - dy; }
    if (x1 + dx > s->clip.x + s->clip.w) { x1 = s->clip.x + s->clip.w - dx; }
    if (y1 + dy > s->clip.y + s->clip.h) { y1 = s->clip.y + s->clip.h - dy; }
    w = x1 - x0;
    if (w <= 0 || y1 <= y0) {
        return;
    }
    {
        int sc = scale_of(s), sub;
        long stride = (long)s->w * sc;

        if (dy > 0) {
            /* Downwards: the bottom scanline first, or the copy overwrites
             * the rows it has not read yet. */
            for (y = y1 - 1; y >= y0; y--) {
                for (sub = sc - 1; sub >= 0; sub--) {
                    memmove(s->px + ((long)(y + dy) * sc + sub) * stride +
                            (long)(x0 + dx) * sc,
                            s->px + ((long)y * sc + sub) * stride +
                            (long)x0 * sc,
                            (size_t)w * sc * sizeof *s->px);
                }
            }
        } else {
            for (y = y0; y < y1; y++) {
                for (sub = 0; sub < sc; sub++) {
                    memmove(s->px + ((long)(y + dy) * sc + sub) * stride +
                            (long)(x0 + dx) * sc,
                            s->px + ((long)y * sc + sub) * stride +
                            (long)x0 * sc,
                            (size_t)w * sc * sizeof *s->px);
                }
            }
        }
    }
}

/** @brief One pixel, @p c laid over it at @p a of 255. */
static void
blend_px(tiku_rgb_t *px, tiku_rgb_t c, unsigned a)
{
    unsigned inv = 255u - a;
    tiku_rgb_t p = *px;
    unsigned r = (((c >> 16) & 0xFFu) * a + ((p >> 16) & 0xFFu) * inv) / 255u;
    unsigned g = (((c >> 8) & 0xFFu) * a + ((p >> 8) & 0xFFu) * inv) / 255u;
    unsigned b = ((c & 0xFFu) * a + (p & 0xFFu) * inv) / 255u;

    *px = (tiku_rgb_t)((r << 16) | (g << 8) | b);
}

/**
 * @brief How much of a native pixel a rounded corner covers, 0 to 255.
 *
 * The corner is the quarter disc of radius @p r whose centre is at
 * (@p cx, @p cy); the pixel is the unit square at (@p x, @p y), sampled
 * four by four, in integer arithmetic: sixteen compares a pixel.
 */
static unsigned
corner_coverage(int x, int y, int cx, int cy, int r)
{
    int i, j, inside = 0;
    long rr = (long)r * r * 64;         /* radius in eighths, squared */

    for (j = 0; j < 4; j++) {
        for (i = 0; i < 4; i++) {
            long dx = (long)(x * 8 + 1 + i * 2) - cx * 8;
            long dy = (long)(y * 8 + 1 + j * 2) - cy * 8;

            if (dx * dx + dy * dy <= rr) { inside++; }
        }
    }
    return (unsigned)(inside * 255 / 16);
}

/** @brief Native pixel bounds with exclusive right and bottom edges. */
typedef struct { int x0, y0, x1, y1; } native_box_t;

/** @brief The native pixel bounds of logical rectangle @p r at @p scale. */
static native_box_t
native_box(tiku_rect_t r, int scale)
{
    return (native_box_t){r.x * scale, r.y * scale,
                          (r.x + r.w) * scale, (r.y + r.h) * scale};
}

/**
 * @brief The corners of @p r, each blended by how much the disc covers.
 *
 * With @p ring set, what is drawn is the hairline: the difference
 * between the corner at @p radius and the corner one pixel inside it.
 * Native pixels throughout, so a scaled surface gets a finer curve.
 */
static void
round_corners(tiku_surface_t *s, tiku_rect_t r, int radius, unsigned corners,
              tiku_rgb_t c, int ring)
{
    int sc = scale_of(s);
    long stride = (long)s->w * sc;
    int rad = radius * sc;
    native_box_t box = native_box(r, sc), clip = native_box(s->clip, sc);
    int k, x, y;

    if (rad <= 0 || r.w < 2 * radius || r.h < 2 * radius) {
        return;
    }
    for (k = 0; k < 4; k++) {
        /* Each corner's square of pixels, and the disc's centre for it. */
        int left = (k & 1) ? box.x1 - rad : box.x0;

        if (!(corners & (1u << k))) { continue; }

        int top = (k & 2) ? box.y1 - rad : box.y0;
        int cx = (k & 1) ? box.x1 - rad : box.x0 + rad;
        int cy = (k & 2) ? box.y1 - rad : box.y0 + rad;

        for (y = top; y < top + rad; y++) {
            tiku_rgb_t *row;

            if (y < clip.y0 || y >= clip.y1) { continue; }
            row = s->px + (long)y * stride;
            for (x = left; x < left + rad; x++) {
                unsigned a;

                if (x < clip.x0 || x >= clip.x1) { continue; }
                a = corner_coverage(x, y, cx, cy, rad);
                if (ring) {
                    unsigned in = corner_coverage(x, y, cx, cy, rad - sc);

                    a = (a > in) ? a - in : 0u;
                }
                if (a >= 255u) { row[x] = c; }
                else if (a > 0u) { blend_px(&row[x], c, a); }
            }
        }
    }
}

tiku_surface_t *
tiku_grab(const tiku_surface_t *s, tiku_rect_t r)
{
    tiku_surface_t *out;
    int sc, y;
    long sstride, dstride;

    if (s == NULL || s->px == NULL) {
        return NULL;
    }
    /* Trimmed to the surface: a grab past the edge takes what is there. */
    if (r.x < 0) { r.w += r.x; r.x = 0; }
    if (r.y < 0) { r.h += r.y; r.y = 0; }
    if (r.x + r.w > s->w) { r.w = s->w - r.x; }
    if (r.y + r.h > s->h) { r.h = s->h - r.y; }
    if (r.w <= 0 || r.h <= 0) {
        return NULL;
    }
    out = tiku_surface_new(r.w, r.h, 0);
    if (out == NULL) {
        return NULL;
    }
    sc = scale_of(s);
    if (sc > 1 && tiku_surface_rescale(out, sc, 0) != 0) {
        tiku_surface_free(out);
        return NULL;
    }
    sstride = (long)s->w * sc;
    dstride = (long)r.w * sc;
    for (y = 0; y < r.h * sc; y++) {
        memcpy(out->px + (long)y * dstride,
               s->px + ((long)r.y * sc + y) * sstride + (long)r.x * sc,
               (size_t)dstride * sizeof *out->px);
    }
    return out;
}

void
tiku_blend(tiku_surface_t *dst, int x, int y, const tiku_surface_t *src,
           float amount)
{
    int x0, y0, x1, y1, row, col, sc;
    long dstride, sstride;
    unsigned a;

    tiku_gfx_rec_miss(dst);  /* a blend over what is there has no op */
    if (dst == NULL || src == NULL || dst->px == NULL || src->px == NULL ||
        scale_of(dst) != scale_of(src) || amount <= 0.0f) {
        return;
    }
    if (amount > 1.0f) { amount = 1.0f; }
    a = (unsigned)(amount * 255.0f + 0.5f);
    if (a == 0u) {
        return;
    }
    sc = scale_of(dst);
    x0 = (x > dst->clip.x) ? x : dst->clip.x;
    y0 = (y > dst->clip.y) ? y : dst->clip.y;
    x1 = x + src->w; y1 = y + src->h;
    if (x1 > dst->clip.x + dst->clip.w) { x1 = dst->clip.x + dst->clip.w; }
    if (y1 > dst->clip.y + dst->clip.h) { y1 = dst->clip.y + dst->clip.h; }
    if (x1 <= x0 || y1 <= y0) {
        return;
    }
    dstride = (long)dst->w * sc;
    sstride = (long)src->w * sc;
    for (row = y0 * sc; row < y1 * sc; row++) {
        tiku_rgb_t *out = dst->px + (long)row * dstride;
        const tiku_rgb_t *in = src->px + (long)(row - y * sc) * sstride
                               - (long)x * sc;

        for (col = x0 * sc; col < x1 * sc; col++) {
            if (a >= 255u) { out[col] = in[col]; }
            else { blend_px(&out[col], in[col], a); }
        }
    }
}

void
tiku_blend_scaled(tiku_surface_t *dst, tiku_rect_t at,
                  const tiku_surface_t *src, float amount)
{
    native_box_t box;
    int row, col, sc, sw, sh;
    long dstride, sstride;
    unsigned a;

    tiku_gfx_rec_miss(dst);
    if (dst == NULL || src == NULL || dst->px == NULL || src->px == NULL ||
        at.w <= 0 || at.h <= 0 || amount <= 0.0f) {
        return;
    }
    if (amount > 1.0f) { amount = 1.0f; }
    a = (unsigned)(amount * 255.0f + 0.5f);
    if (a == 0u) {
        return;
    }
    sc = scale_of(dst);
    sw = src->w * scale_of(src);
    sh = src->h * scale_of(src);
    box.x0 = (at.x > dst->clip.x) ? at.x : dst->clip.x;
    box.y0 = (at.y > dst->clip.y) ? at.y : dst->clip.y;
    box.x1 = at.x + at.w; box.y1 = at.y + at.h;
    if (box.x1 > dst->clip.x + dst->clip.w) {
        box.x1 = dst->clip.x + dst->clip.w;
    }
    if (box.y1 > dst->clip.y + dst->clip.h) {
        box.y1 = dst->clip.y + dst->clip.h;
    }
    if (box.x1 <= box.x0 || box.y1 <= box.y0) {
        return;
    }
    dstride = (long)dst->w * sc;
    sstride = sw;
    /* Nearest source pixel for each native destination pixel, with no
     * filtering. */
    for (row = box.y0 * sc; row < box.y1 * sc; row++) {
        tiku_rgb_t *out = dst->px + (long)row * dstride;
        long sy = (long)(row - at.y * sc) * sh / ((long)at.h * sc);

        if (sy >= sh) { sy = sh - 1; }
        for (col = box.x0 * sc; col < box.x1 * sc; col++) {
            long sx = (long)(col - at.x * sc) * sw / ((long)at.w * sc);
            tiku_rgb_t p;

            if (sx >= sw) { sx = sw - 1; }
            p = src->px[sy * sstride + sx];
            if (a >= 255u) { out[col] = p; }
            else { blend_px(&out[col], p, a); }
        }
    }
}

void
tiku_fill_round(tiku_surface_t *s, tiku_rect_t r, int radius,
                unsigned corners, tiku_rgb_t c)
{
    int rec;

    if (s == NULL || r.w <= 0 || r.h <= 0) {
        return;
    }
    if (radius <= 0 || r.w < 2 * radius || r.h < 2 * radius ||
        (corners & TIKU_CORNER_ALL) == 0u) {
        tiku_fill(s, r, c);
        return;
    }
    rec = tiku_gfx_rec_enter(s);
    if (rec) {
        (void)tiku_dl_fill(s->record, r, c);   /* recorded as a fill */
    }
    /* The cross the corners leave, opaque; a corner kept square is
     * filled whole; the rest are laid by cover. */
    tiku_fill(s, (tiku_rect_t){ r.x + radius, r.y, r.w - 2 * radius, r.h },
              c);
    tiku_fill(s, (tiku_rect_t){ r.x, r.y + radius, radius, r.h - 2 * radius },
              c);
    tiku_fill(s, (tiku_rect_t){ r.x + r.w - radius, r.y + radius, radius,
                                r.h - 2 * radius }, c);
    if (!(corners & TIKU_CORNER_TL)) {
        tiku_fill(s, (tiku_rect_t){ r.x, r.y, radius, radius }, c);
    }
    if (!(corners & TIKU_CORNER_TR)) {
        tiku_fill(s, (tiku_rect_t){ r.x + r.w - radius, r.y, radius, radius },
                  c);
    }
    if (!(corners & TIKU_CORNER_BL)) {
        tiku_fill(s, (tiku_rect_t){ r.x, r.y + r.h - radius, radius, radius },
                  c);
    }
    if (!(corners & TIKU_CORNER_BR)) {
        tiku_fill(s, (tiku_rect_t){ r.x + r.w - radius, r.y + r.h - radius,
                                    radius, radius }, c);
    }
    round_corners(s, r, radius, corners, c, 0);
    tiku_gfx_rec_leave(s, rec);
}

void
tiku_frame_round(tiku_surface_t *s, tiku_rect_t r, int radius,
                 unsigned corners, tiku_rgb_t c)
{
    int rec, tl, tr, bl, br;

    if (s == NULL || r.w <= 0 || r.h <= 0) {
        return;
    }
    if (radius <= 0 || r.w < 2 * radius || r.h < 2 * radius ||
        (corners & TIKU_CORNER_ALL) == 0u) {
        tiku_frame(s, r, c);
        return;
    }
    rec = tiku_gfx_rec_enter(s);
    if (rec) {
        (void)tiku_dl_frame(s->record, r, c);
    }
    /* Each straight edge runs to the corner, or stops short of a curve. */
    tl = (corners & TIKU_CORNER_TL) ? radius : 0;
    tr = (corners & TIKU_CORNER_TR) ? radius : 0;
    bl = (corners & TIKU_CORNER_BL) ? radius : 0;
    br = (corners & TIKU_CORNER_BR) ? radius : 0;
    tiku_hline(s, r.x + tl, r.y, r.w - tl - tr, c);
    tiku_hline(s, r.x + bl, r.y + r.h - 1, r.w - bl - br, c);
    tiku_vline(s, r.x, r.y + tl, r.h - tl - bl, c);
    tiku_vline(s, r.x + r.w - 1, r.y + tr, r.h - tr - br, c);
    round_corners(s, r, radius, corners, c, 1);
    tiku_gfx_rec_leave(s, rec);
}

/** @brief One shadowed pixel: the alpha for its distance, then the blend. */
static void
shadow_px(tiku_rgb_t *px, long d2, const unsigned char *alpha)
{
    unsigned a = alpha[d2];

    if (a > 0u) { blend_px(px, 0u, a); }
}

/**
 * @brief Lay the shadow band of one strip of rows.
 *
 * Rows [y0, y1) of the band, columns [x0, x1) of it: the strip's every
 * pixel is outside the box, so only its distance decides its darkness.
 */
static void
shadow_strip(tiku_surface_t *s, long stride, int x0, int y0, int x1, int y1,
             int bx0, int by0, int bx1, int by1, long rad2,
             const unsigned char *alpha)
{
    int x, y;

    for (y = y0; y < y1; y++) {
        tiku_rgb_t *row = s->px + (long)y * stride;
        long dy = (y < by0) ? by0 - y : (y > by1) ? y - by1 : 0;
        long dy2 = dy * dy;

        if (dy2 >= rad2) { continue; }
        for (x = x0; x < x1; x++) {
            long dx = (x < bx0) ? bx0 - x : (x > bx1) ? x - bx1 : 0;
            long d2 = dy2 + dx * dx;

            if (d2 < rad2) { shadow_px(&row[x], d2, alpha); }
        }
    }
}

/**
 * @brief Fill @p alpha, indexed by squared native distance from a shadow's
 *        edge, for radius @p rad: @p strength of 255 at the edge, falling
 *        off as the square of the distance left, 0 at the radius.
 */
static void
shadow_alpha(unsigned char *alpha, int rad, float strength)
{
    unsigned edge = (unsigned)(strength * 255.0f + 0.5f);
    long rad2 = (long)rad * rad;

    for (long d2 = 0; d2 < rad2; d2++) {
        long t = rad2 - d2;
        alpha[d2] = (unsigned char)((long)edge * t / rad * t / rad / rad2);
    }
}

void
tiku_shadow(tiku_surface_t *s, tiku_rect_t r, int radius, int corner,
            float strength)
{
    /* The table holds a radius of 32 native pixels; rad is clamped to
     * it below. */
    unsigned char alpha[32 * 32 + 1];
    native_box_t box, band, cover, clip;
    int sc, rad, cr;
    long stride;

    tiku_gfx_rec_miss(s);   /* a blend over what is there has no op */
    if (s == NULL || r.w <= 0 || r.h <= 0 || radius <= 0 ||
        strength <= 0.0f) {
        return;
    }
    if (strength > 1.0f) { strength = 1.0f; }
    sc = scale_of(s);
    stride = (long)s->w * sc;
    rad = radius * sc;
    if (rad > 32) { rad = 32; }
    cr = (corner > 0) ? corner * sc : 0;
    shadow_alpha(alpha, rad, strength);
    /* The box the shadow falls from, native and dropped 2 logical pixels
     * (the light is a little above), and the band around it. */
    box.x0 = r.x * sc; box.y0 = r.y * sc + 2 * sc;
    box.x1 = (r.x + r.w) * sc - 1; box.y1 = (r.y + r.h) * sc - 1 + 2 * sc;
    band.x0 = box.x0 - rad; band.y0 = box.y0 - rad;
    band.x1 = box.x1 + rad + 1; band.y1 = box.y1 + rad + 1;
    clip = native_box(s->clip, sc);
    if (band.x0 < clip.x0) { band.x0 = clip.x0; }
    if (band.y0 < clip.y0) { band.y0 = clip.y0; }
    if (band.x1 > clip.x1) { band.x1 = clip.x1; }
    if (band.y1 > clip.y1) { band.y1 = clip.y1; }
    if (band.x0 >= band.x1 || band.y0 >= band.y1) { return; }
    /*
     * The window covers its own box, so nothing is laid under it: the
     * band is the strip above the box, the strip below, and the two
     * beside it.  Walking only those keeps a large window's shadow from
     * costing its whole area.
     */
    cover.x0 = r.x * sc; cover.y0 = r.y * sc;
    cover.x1 = (r.x + r.w) * sc; cover.y1 = (r.y + r.h) * sc;
    shadow_strip(s, stride, band.x0, band.y0, band.x1,
                 cover.y0 < band.y0 ? band.y0
                 : cover.y0 > band.y1 ? band.y1 : cover.y0,
                 box.x0, box.y0, box.x1, box.y1, ((long)rad * rad), alpha);
    shadow_strip(s, stride, band.x0,
                 cover.y1 < band.y0 ? band.y0
                 : cover.y1 > band.y1 ? band.y1 : cover.y1,
                 band.x1, band.y1,
                 box.x0, box.y0, box.x1, box.y1, ((long)rad * rad), alpha);
    {
        int sy0 = cover.y0 < band.y0 ? band.y0 : cover.y0;
        int sy1 = cover.y1 > band.y1 ? band.y1 : cover.y1;

        if (sy0 < sy1) {
            shadow_strip(s, stride, band.x0, sy0,
                         cover.x0 < band.x0 ? band.x0
                         : cover.x0 > band.x1 ? band.x1 : cover.x0, sy1,
                         box.x0, box.y0, box.x1, box.y1, ((long)rad * rad),
                         alpha);
            shadow_strip(s, stride,
                         cover.x1 < band.x0 ? band.x0
                         : cover.x1 > band.x1 ? band.x1 : cover.x1, sy0,
                         band.x1, sy1, box.x0, box.y0, box.x1, box.y1,
                         ((long)rad * rad), alpha);
        }
    }
    /*
     * Inside the box only a rounded corner's cut shows the desk, and
     * there the shadow reaches as it does past an edge: as dark as the
     * edge, for the little past the curve.  The rows within a corner of
     * the top or the bottom, the columns within one of a side, and
     * nothing else of the interior; a box narrower than two corners
     * takes the left and the top one's curve, as the frame draws it.
     */
    if (cr <= 0) { return; }
    {
        int qx0 = cover.x0 < band.x0 ? band.x0 : cover.x0;
        int qx1 = cover.x1 > band.x1 ? band.x1 : cover.x1;
        int qy0 = cover.y0 < band.y0 ? band.y0 : cover.y0;
        int qy1 = cover.y1 > band.y1 ? band.y1 : cover.y1;
        int x, y;

        for (y = qy0; y < qy1; y++) {
            tiku_rgb_t *row;
            long ey = (y < cover.y0 + cr) ? cover.y0 + cr
                    : (y >= cover.y1 - cr) ? cover.y1 - cr - 1 : -1;

            if (ey < 0) {
                if (cover.y1 - cr > cover.y0 + cr) { y = cover.y1 - cr - 1; }
                continue;
            }
            row = s->px + (long)y * stride;
            for (x = qx0; x < qx1; x++) {
                long ex = (x < cover.x0 + cr) ? cover.x0 + cr
                        : (x >= cover.x1 - cr) ? cover.x1 - cr - 1 : -1;
                long ccx, ccy;

                if (ex < 0) {
                    if (cover.x1 - cr > cover.x0 + cr) {
                        x = cover.x1 - cr - 1;
                    }
                    continue;
                }
                ccx = x - ex; ccy = y - ey;
                if (ccx * ccx + ccy * ccy > (long)cr * cr) {
                    shadow_px(&row[x], 0, alpha);
                }
            }
        }
    }
}

void
tiku_veil(tiku_surface_t *s, tiku_rect_t r, tiku_rgb_t c, float amount)
{
    tiku_gfx_rec_miss(s);   /* a blend over what is already there has no op */

    int x, y, x0, y0, x1, y1, sc;
    long stride;
    unsigned a, inv, cr, cg, cb;

    if (s == NULL || amount <= 0.0f) {
        return;
    }
    if (amount > 1.0f) {
        amount = 1.0f;
    }
    /* Eight bits of it, so that the arithmetic below is integer: a veil
     * is laid over every pixel of a screen. */
    a = (unsigned)(amount * 255.0f + 0.5f);
    if (a == 0u) {
        return;
    }
    inv = 255u - a;
    cr = ((c >> 16) & 0xFFu) * a;
    cg = ((c >> 8) & 0xFFu) * a;
    cb = (c & 0xFFu) * a;

    x0 = (r.x > s->clip.x) ? r.x : s->clip.x;
    y0 = (r.y > s->clip.y) ? r.y : s->clip.y;
    x1 = r.x + r.w;
    y1 = r.y + r.h;
    if (x1 > s->clip.x + s->clip.w) { x1 = s->clip.x + s->clip.w; }
    if (y1 > s->clip.y + s->clip.h) { y1 = s->clip.y + s->clip.h; }
    sc = scale_of(s);
    stride = (long)s->w * sc;
    for (y = y0; y < y1; y++) {
        int ry;

        for (ry = 0; ry < sc; ry++) {
            tiku_rgb_t *row = s->px + ((long)y * sc + ry) * stride;

            for (x = x0 * sc; x < x1 * sc; x++) {
                tiku_rgb_t p = row[x];
                unsigned pr = (cr + ((p >> 16) & 0xFFu) * inv) / 255u;
                unsigned pg = (cg + ((p >> 8) & 0xFFu) * inv) / 255u;
                unsigned pb = (cb + (p & 0xFFu) * inv) / 255u;

                row[x] = (tiku_rgb_t)((pr << 16) | (pg << 8) | pb);
            }
        }
    }
}

void
tiku_fill(tiku_surface_t *s, tiku_rect_t r, tiku_rgb_t c)
{
    int x, y, x0, y0, x1, y1;
    int rec;

    if (s == NULL) {
        return;
    }
    rec = tiku_gfx_rec_enter(s);
    if (rec) {
        (void)tiku_dl_fill(s->record, r, c);
    }
    x0 = (r.x > s->clip.x) ? r.x : s->clip.x;
    y0 = (r.y > s->clip.y) ? r.y : s->clip.y;
    x1 = r.x + r.w;
    y1 = r.y + r.h;
    if (x1 > s->clip.x + s->clip.w) { x1 = s->clip.x + s->clip.w; }
    if (y1 > s->clip.y + s->clip.h) { y1 = s->clip.y + s->clip.h; }
    {
        int sc = scale_of(s);
        long stride = (long)s->w * sc;

        for (y = (long)y0 * sc; y < (long)y1 * sc; y++) {
            tiku_rgb_t *row = s->px + (long)y * stride;
            for (x = (long)x0 * sc; x < (long)x1 * sc; x++) {
                row[x] = c;
            }
        }
    }
    tiku_gfx_rec_leave(s, rec);
}

void
tiku_hline(tiku_surface_t *s, int x, int y, int len,
                tiku_rgb_t c)
{
    tiku_rect_t r = { x, y, len, 1 };
    int rec = tiku_gfx_rec_enter(s);

    if (rec) {
        (void)tiku_dl_hline(s->record, x, y, len, c);
    }
    tiku_fill(s, r, c);
    tiku_gfx_rec_leave(s, rec);
}

void
tiku_vline(tiku_surface_t *s, int x, int y, int len,
                tiku_rgb_t c)
{
    tiku_rect_t r = { x, y, 1, len };
    int rec = tiku_gfx_rec_enter(s);

    if (rec) {
        (void)tiku_dl_vline(s->record, x, y, len, c);
    }
    tiku_fill(s, r, c);
    tiku_gfx_rec_leave(s, rec);
}

void
tiku_frame(tiku_surface_t *s, tiku_rect_t r, tiku_rgb_t c)
{
    int rec;

    if (r.w <= 0 || r.h <= 0) {
        return;
    }
    rec = tiku_gfx_rec_enter(s);
    if (rec) {
        (void)tiku_dl_frame(s->record, r, c);
    }
    tiku_hline(s, r.x, r.y, r.w, c);
    tiku_hline(s, r.x, r.y + r.h - 1, r.w, c);
    tiku_vline(s, r.x, r.y, r.h, c);
    tiku_vline(s, r.x + r.w - 1, r.y, r.h, c);
    tiku_gfx_rec_leave(s, rec);
}

void
tiku_bevel(tiku_surface_t *s, tiku_rect_t r,
                tiku_rgb_t light, tiku_rgb_t shadow)
{
    int rec;

    if (r.w <= 0 || r.h <= 0) {
        return;
    }
    rec = tiku_gfx_rec_enter(s);
    if (rec) {
        (void)tiku_dl_bevel(s->record, r, light, shadow);
    }
    tiku_hline(s, r.x, r.y, r.w - 1, light);
    tiku_vline(s, r.x, r.y, r.h - 1, light);
    tiku_hline(s, r.x, r.y + r.h - 1, r.w, shadow);
    tiku_vline(s, r.x + r.w - 1, r.y, r.h, shadow);
    tiku_gfx_rec_leave(s, rec);
}

tiku_rect_t
tiku_inset(tiku_rect_t r, int n)
{
    tiku_rect_t o;

    o.x = r.x + n;
    o.y = r.y + n;
    o.w = r.w - 2 * n;
    o.h = r.h - 2 * n;
    if (o.w < 0) { o.w = 0; }
    if (o.h < 0) { o.h = 0; }
    return o;
}

/*---------------------------------------------------------------------------*/
/* PNG                                                                       */
/*---------------------------------------------------------------------------*/

/** @brief Run the PNG CRC-32 over @p n bytes of @p b from @p crc. */
static unsigned long
png_crc(const unsigned char *b, size_t n, unsigned long crc)
{
    static unsigned long tab[256];
    static int built;
    size_t i;

    if (!built) {
        unsigned long c;
        int k, j;
        for (k = 0; k < 256; k++) {
            c = (unsigned long)k;
            for (j = 0; j < 8; j++) {
                c = (c & 1u) ? (0xEDB88320uL ^ (c >> 1)) : (c >> 1);
            }
            tab[k] = c;
        }
        built = 1;
    }
    for (i = 0; i < n; i++) {
        crc = tab[(crc ^ b[i]) & 0xFFu] ^ (crc >> 8);
    }
    return crc;
}

/** @brief Write @p v big-endian at @p p. */
static void
png_be32(unsigned char *p, unsigned long v)
{
    p[0] = (unsigned char)(v >> 24);
    p[1] = (unsigned char)(v >> 16);
    p[2] = (unsigned char)(v >> 8);
    p[3] = (unsigned char)v;
}

/** @brief Write one chunk: length, @p tag, @p n bytes of @p data, CRC.
 *  @return 0, or -1 when a write fails. */
static int
png_chunk(FILE *f, const char *tag, const unsigned char *data, size_t n)
{
    unsigned char hdr[8];
    unsigned char crcbuf[4];
    unsigned long crc;

    png_be32(hdr, (unsigned long)n);
    memcpy(hdr + 4, tag, 4);
    if (fwrite(hdr, 1, 8, f) != 8u) {
        return -1;
    }
    if (n > 0u && fwrite(data, 1, n, f) != n) {
        return -1;
    }
    crc = png_crc((const unsigned char *)tag, 4u, 0xFFFFFFFFuL);
    crc = png_crc(data, n, crc) ^ 0xFFFFFFFFuL;
    png_be32(crcbuf, crc);
    return (fwrite(crcbuf, 1, 4, f) == 4u) ? 0 : -1;
}

/** @brief Serialize native RGB pixels as unfiltered PNG rows. */
static void
png_rows(const tiku_surface_t *s, unsigned char *raw, int nw, int nh)
{
    size_t off;
    int y;
    off = 0;
    for (y = 0; y < nh; y++) {
        int x;
        raw[off++] = 0;                       /* filter: none */
        for (x = 0; x < nw; x++) {
            tiku_rgb_t c = s->px[(long)y * nw + x];
            raw[off++] = (unsigned char)((c >> 16) & 0xFFu);
            raw[off++] = (unsigned char)((c >> 8) & 0xFFu);
            raw[off++] = (unsigned char)(c & 0xFFu);
        }
    }
}

/** @brief Write a stored-deflate zlib stream and its Adler checksum. */
static size_t
png_store(const unsigned char *raw, size_t rawlen, unsigned char *z)
{
    unsigned long a = 1, b = 0;
    size_t i, off;
    for (i = 0; i < rawlen; i++) {
        a = (a + raw[i]) % 65521uL;
        b = (b + a) % 65521uL;
    }
    off = 0;
    z[off++] = 0x78;                          /* zlib: deflate, 32K window */
    z[off++] = 0x01;
    for (i = 0; i < rawlen; ) {
        size_t n = rawlen - i;
        int last;
        if (n > 65535u) { n = 65535u; }
        last = (i + n >= rawlen);
        z[off++] = (unsigned char)(last ? 1 : 0);
        z[off++] = (unsigned char)(n & 0xFFu);
        z[off++] = (unsigned char)((n >> 8) & 0xFFu);
        z[off++] = (unsigned char)(~n & 0xFFu);
        z[off++] = (unsigned char)((~n >> 8) & 0xFFu);
        memcpy(z + off, raw + i, n);
        off += n;
        i += n;
    }
    png_be32(z + off, (b << 16) | a);
    off += 4u;
    return off;
}

int
tiku_surface_png(const tiku_surface_t *s, const char *path)
{
    FILE *f;
    unsigned char ihdr[13];
    unsigned char *raw, *z;
    size_t rawlen, zlen, off;
    int nw, nh, rc = -1;

    if (s == NULL || path == NULL) {
        return -1;
    }
    f = fopen(path, "wb");
    if (f == NULL) {
        return -1;
    }
    {
        int sc = scale_of(s);
        nw = s->w * sc;
        nh = s->h * sc;
    }
    rawlen = (size_t)nh * (1u + (size_t)nw * 3u);
    raw = malloc(rawlen);
    /* Stored deflate: 5 bytes of block header per 65535, plus zlib framing. */
    zlen = 2u + rawlen + 5u * (rawlen / 65535u + 1u) + 4u;
    z = malloc(zlen);
    if (raw == NULL || z == NULL) {
        goto done;
    }
    png_rows(s, raw, nw, nh);
    off = png_store(raw, rawlen, z);
    if (fwrite("\x89PNG\r\n\x1a\n", 1, 8, f) != 8u) {
        goto done;
    }
    png_be32(ihdr, (unsigned long)nw);
    png_be32(ihdr + 4, (unsigned long)nh);
    ihdr[8] = 8;                              /* bit depth  */
    ihdr[9] = 2;                              /* truecolour */
    ihdr[10] = 0;
    ihdr[11] = 0;
    ihdr[12] = 0;
    if (png_chunk(f, "IHDR", ihdr, sizeof ihdr) != 0 ||
        png_chunk(f, "IDAT", z, off) != 0 ||
        png_chunk(f, "IEND", NULL, 0u) != 0) {
        goto done;
    }
    rc = 0;
done:
    free(raw);
    free(z);
    (void)fclose(f);
    return rc;
}

/** @brief One inverted pixel, honouring the clip. */
static void
invert_pixel(tiku_surface_t *s, int x, int y)
{
    int sc, ry, rx;
    long stride;

    if (s == NULL || x < s->clip.x || x >= s->clip.x + s->clip.w ||
        y < s->clip.y || y >= s->clip.y + s->clip.h ||
        x < 0 || x >= s->w || y < 0 || y >= s->h) {
        return;
    }
    sc = scale_of(s);
    stride = (long)s->w * sc;
    for (ry = 0; ry < sc; ry++) {
        tiku_rgb_t *row = s->px + ((long)y * sc + ry) * stride +
                               (long)x * sc;
        for (rx = 0; rx < sc; rx++) {
            row[rx] = (tiku_rgb_t)(~row[rx] & 0x00FFFFFFu);
        }
    }
}

void
tiku_invert_frame(tiku_surface_t *s, tiku_rect_t r)
{
    tiku_gfx_rec_miss(s);   /* no command inverts */

    int i;

    if (s == NULL || r.w <= 0 || r.h <= 0) {
        return;
    }
    for (i = 0; i < r.w; i++) {
        invert_pixel(s, r.x + i, r.y);
        invert_pixel(s, r.x + i, r.y + r.h - 1);
    }
    for (i = 1; i < r.h - 1; i++) {
        invert_pixel(s, r.x, r.y + i);
        invert_pixel(s, r.x + r.w - 1, r.y + i);
    }
}
