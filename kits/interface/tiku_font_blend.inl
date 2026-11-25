/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_font_blend.inl - shared baked-font text operations.
 * SPDX-License-Identifier: Apache-2.0
 */
/** @brief Blend @p c over the native pixel at (x,y), coverage @p a. */
static void
blend(tiku_surface_t *s, int sc, int x, int y, tiku_rgb_t c,
      unsigned a)
{
    tiku_rgb_t d;
    unsigned dr, dg, db, sr, sg, sb;
    long stride = tiku_surface_stride(s);

    if (a == 0u ||
        x < s->clip.x * sc || y < s->clip.y * sc ||
        x >= (s->clip.x + s->clip.w) * sc ||
        y >= (s->clip.y + s->clip.h) * sc) {
        return;
    }
    if (a >= 255u) {
        s->px[(long)y * stride + x] = c;
        return;
    }
    d = s->px[(long)y * stride + x];
    dr = (d >> 16) & 0xFFu; dg = (d >> 8) & 0xFFu; db = d & 0xFFu;
    sr = (c >> 16) & 0xFFu; sg = (c >> 8) & 0xFFu; sb = c & 0xFFu;
    dr = (sr * a + dr * (255u - a)) / 255u;
    dg = (sg * a + dg * (255u - a)) / 255u;
    db = (sb * a + db * (255u - a)) / 255u;
    s->px[(long)y * stride + x] = TIKU_RGB(dr, dg, db);
}
