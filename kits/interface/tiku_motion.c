/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_motion.c - effect timing, the icon outline and the popup fade.
 *
 * Every frame is worked out from the time the caller passes in; nothing
 * here sleeps, keeps a timer or allocates beyond the fade's one capture.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_motion.h"

/* How far outside the icon the outline's dark ring starts. */
#define OUTLINE_FIRST 2

void
tiku_motion_start(tiku_motion_t *m, int64_t now, int64_t duration)
{
    if (m == NULL) {
        return;
    }
    m->since = now;
    m->duration = duration;
    m->active = now > 0 && duration > 0;
}

float
tiku_motion_progress(tiku_motion_t *m, int64_t now)
{
    if (m == NULL || !m->active) {
        return 1.0f;
    }
    if (now <= 0 || now < m->since || now - m->since >= m->duration) {
        m->active = 0;
        return 1.0f;
    }
    return (float)(now - m->since) / (float)m->duration;
}

float
tiku_motion_ease_out(float t)
{
    float u;

    if (t <= 0.0f) {
        return 0.0f;
    }
    if (t >= 1.0f) {
        return 1.0f;
    }
    u = 1.0f - t;
    return 1.0f - u * u * u;
}

tiku_rect_t
tiku_motion_outline_bounds(tiku_rect_t r)
{
    return (tiku_rect_t){ r.x - TIKU_MOTION_ICON_MARGIN,
                          r.y - TIKU_MOTION_ICON_MARGIN,
                          r.w + 2 * TIKU_MOTION_ICON_MARGIN,
                          r.h + 2 * TIKU_MOTION_ICON_MARGIN };
}

/** @brief A one-pixel ring just inside @p r, laid in @p ink at @p alpha. */
static void
outline(tiku_surface_t *s, tiku_rect_t r, tiku_rgb_t ink, float alpha)
{
    tiku_veil(s, (tiku_rect_t){ r.x, r.y, r.w, 1 }, ink, alpha);
    tiku_veil(s, (tiku_rect_t){ r.x, r.y + r.h - 1, r.w, 1 }, ink, alpha);
    tiku_veil(s, (tiku_rect_t){ r.x, r.y + 1, 1, r.h - 2 }, ink, alpha);
    tiku_veil(s, (tiku_rect_t){ r.x + r.w - 1, r.y + 1, 1, r.h - 2 },
              ink, alpha);
}

void
tiku_motion_outline(tiku_surface_t *s, tiku_rect_t r, float t)
{
    int grow;

    if (s == NULL || r.w <= 0 || r.h <= 0 || t < 0.0f || t >= 1.0f) {
        return;
    }
    grow = OUTLINE_FIRST +
           (int)((float)(TIKU_MOTION_ICON_MARGIN - OUTLINE_FIRST) *
                 tiku_motion_ease_out(t) + 0.5f);
    r.x -= grow;
    r.y -= grow;
    r.w += grow * 2;
    r.h += grow * 2;
    /* The light ring inside the dark one keeps the outline visible over a
     * picture and in every theme. */
    outline(s, r, TIKU_RGB(0, 0, 0), 0.60f * (1.0f - t));
    outline(s, tiku_inset(r, 1), TIKU_RGB(255, 255, 255),
            0.65f * (1.0f - t));
}

tiku_surface_t *
tiku_motion_fade_begin(tiku_surface_t *s, tiku_rect_t r, float t,
                       tiku_rect_t *captured)
{
    int left, top, right, bottom;

    if (s == NULL || captured == NULL || t < 0.0f || t >= 1.0f) {
        return NULL;
    }
    left = (r.x > s->clip.x) ? r.x : s->clip.x;
    top = (r.y > s->clip.y) ? r.y : s->clip.y;
    right = (r.x + r.w < s->clip.x + s->clip.w) ? r.x + r.w
                                                : s->clip.x + s->clip.w;
    bottom = (r.y + r.h < s->clip.y + s->clip.h) ? r.y + r.h
                                                 : s->clip.y + s->clip.h;
    *captured = (tiku_rect_t){ left, top, right - left, bottom - top };
    if (captured->w <= 0 || captured->h <= 0) {
        return NULL;
    }
    return tiku_grab(s, *captured);
}

void
tiku_motion_fade_end(tiku_surface_t *s, tiku_surface_t *under,
                     tiku_rect_t at, float t)
{
    if (under == NULL) {
        return;
    }
    tiku_blend(s, at.x, at.y, under,
               0.75f * (1.0f - tiku_motion_ease_out(t)));
    tiku_surface_free(under);
}
