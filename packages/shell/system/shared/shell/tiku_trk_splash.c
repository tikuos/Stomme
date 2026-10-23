/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_splash.c - the screen the desktop comes up behind.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_font.h"
#include "tiku_logo.h"
#include "tiku_trk_splash.h"
#include <stdio.h>
#include <string.h>

#define MARK    96      /* the mark's size, in pixels            */
#define GAP     26      /* the gap between the mark and the line */

/**
 * @brief @p c taken toward black by @p k, 0 for gone and 1 for itself.
 *
 * tiku_tint() under one moves a colour toward white instead.
 */
static tiku_rgb_t
toward_black(tiku_rgb_t c, float k)
{
    unsigned r = (unsigned)((float)((c >> 16) & 0xFFu) * k);
    unsigned g = (unsigned)((float)((c >> 8) & 0xFFu) * k);
    unsigned b = (unsigned)((float)(c & 0xFFu) * k);

    return (tiku_rgb_t)((r << 16) | (g << 8) | b);
}

void
tiku_trk_splash_begin(tiku_trk_splash_t *sp, int live, int64_t now)
{
    if (sp == NULL) {
        return;
    }
    memset(sp, 0, sizeof *sp);
    sp->live = live ? 1 : 0;
    sp->stage_us = now;
}

/**
 * @brief Whether there is a screen to name a stage on: while loading, or
 *        while closing.
 *
 * Between the two the desktop is up, and a stage named then is ignored and
 * owes no dwell.  The teardown names its stages on every way out,
 * including those with no closing screen.
 */
static int
on_screen(const tiku_trk_splash_t *sp)
{
    return sp->close_us != 0 || sp->done_us == 0;
}

void
tiku_trk_splash_say(tiku_trk_splash_t *sp, const char *what, int64_t now)
{
    if (sp == NULL || !sp->live || !on_screen(sp)) {
        return;
    }
    snprintf(sp->what, sizeof sp->what, "%s", (what != NULL) ? what : "");
    sp->stage_us = now;
    sp->count++;
}

int64_t
tiku_trk_splash_owed(const tiku_trk_splash_t *sp, int64_t now)
{
    int64_t gone;

    if (sp == NULL || !sp->live || sp->count == 0) {
        return 0;
    }
    gone = now - sp->stage_us;
    if (gone >= TIKU_TRK_SPLASH_STEP_US) {
        return 0;
    }
    return TIKU_TRK_SPLASH_STEP_US - gone;
}

void
tiku_trk_splash_done(tiku_trk_splash_t *sp, int64_t now)
{
    if (sp == NULL || !sp->live || sp->done_us != 0) {
        return;
    }
    sp->done_us = now;
}

void
tiku_trk_splash_closing(tiku_trk_splash_t *sp, int64_t now)
{
    if (sp == NULL || !sp->live || sp->close_us != 0) {
        return;
    }
    sp->close_us = now;
    /* The last loading line is cleared: the screen is blank until the
     * first piece is put away, and the count starts over so that piece is
     * held as long as any other. */
    sp->what[0] = '\0';
    sp->count = 0;
    sp->stage_us = now;
}

int
tiku_trk_splash_closed(const tiku_trk_splash_t *sp, int64_t now)
{
    if (sp == NULL || !sp->live) {
        return 1;               /* nothing to lower, nothing to wait for */
    }
    if (sp->close_us == 0) {
        return 0;
    }
    return now - sp->close_us >=
           TIKU_TRK_SPLASH_FADE_US + TIKU_TRK_SPLASH_HOLD_US;
}

float
tiku_trk_splash_veil(const tiku_trk_splash_t *sp, int64_t now)
{
    int64_t gone;

    if (sp == NULL || !sp->live) {
        return 0.0f;
    }
    if (sp->close_us != 0) {
        /* Fading back in over TIKU_TRK_SPLASH_FADE_US, then solid while the
         * teardown names its stages. */
        gone = now - sp->close_us;
        if (gone >= TIKU_TRK_SPLASH_FADE_US) {
            return 1.0f;
        }
        return (float)gone / (float)TIKU_TRK_SPLASH_FADE_US;
    }
    if (sp->done_us == 0) {
        return 1.0f;            /* still loading: solid */
    }
    gone = now - sp->done_us;
    if (gone < TIKU_TRK_SPLASH_HOLD_US) {
        return 1.0f;
    }
    gone -= TIKU_TRK_SPLASH_HOLD_US;
    if (gone >= TIKU_TRK_SPLASH_FADE_US) {
        return 0.0f;
    }
    return 1.0f - (float)gone / (float)TIKU_TRK_SPLASH_FADE_US;
}

int
tiku_trk_splash_visible(const tiku_trk_splash_t *sp, int64_t now)
{
    /* While closing it counts as visible from the first frame, when the
     * veil is still clear: the caller repaints only while this is true,
     * and the fade needs those frames. */
    if (sp != NULL && sp->live && sp->close_us != 0) {
        return 1;
    }
    return tiku_trk_splash_veil(sp, now) > 0.0f;
}

void
tiku_trk_splash_draw(const tiku_trk_splash_t *sp, tiku_surface_t *s,
                     tiku_rect_t screen, int64_t now)
{
    float a = tiku_trk_splash_veil(sp, now);
    const tiku_font_t *f = tiku_font_plain();
    tiku_logo_palette_t p;
    tiku_rect_t mark, line;
    int block, top;

    if (s == NULL || a <= 0.0f) {
        return;
    }
    tiku_veil(s, screen, TIKU_RGB(0, 0, 0), a);

    /*
     * The mark and the line under it, centred as one block.
     */
    block = MARK + GAP + f->height;
    top = screen.y + (screen.h - block) / 2;
    mark.x = screen.x + (screen.w - MARK) / 2;
    mark.y = top;
    mark.w = MARK;
    mark.h = MARK;
    line.x = screen.x;
    line.y = top + MARK + GAP;
    line.w = screen.w;
    line.h = f->height;

    /*
     * Everything at the veil's strength, by scaling the colours toward
     * black: on the black just laid down, that is the same as blending.
     */
    tiku_logo_palette(0, 0.0f, &p);
    p.ground = toward_black(p.ground, a);
    p.dark   = toward_black(p.dark, a);
    p.grey   = toward_black(p.grey, a);
    p.accent = toward_black(p.accent, a);
    p.tint   = toward_black(p.tint, a);
    p.mix_a  = toward_black(p.mix_a, a);
    p.mix_b  = toward_black(p.mix_b, a);
    tiku_logo_paint_with(s, mark, 0u, &p, 1.0f);

    if (sp->what[0] != '\0') {
        (void)tiku_text_centered(s, f, line, sp->what,
                                 toward_black(TIKU_RGB(220, 220, 220), a));
    }
}
