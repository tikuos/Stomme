/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_textfield_clip.c - a text field drawn within a clip its caller
 * set: nothing lands outside the clip, inside it the field is drawn as it
 * is with no clip at all, and the clip is the caller's again afterwards.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "tiku_ui.h"

#define W       160                 /* the surface, across              */
#define H       100                 /* and down                         */
#define GROUND  0xff123456u         /* its colour, which no control uses */

/** @brief The field the suite draws, its text partly selected and
 *         scrolled, onto @p s within whatever clip @p s has. */
static void
draw_field(tiku_surface_t *s)
{
    tiku_ui_textfield_scroll(s, (tiku_rect_t){15, 15, 125, 30},
                             "Search text", 3, 0, 6, TIKU_S_FOCUS, 5);
}

/** @brief Whether the pixel (@p x, @p y) lies inside @p r. */
static int
inside(tiku_rect_t r, int x, int y)
{
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

/** @brief A clip across part of the field's text, one clear of the field
 *         and an empty one, each against the field drawn unclipped. */
int
main(void)
{
    static const tiku_rect_t clips[] = {
        {20, 20, 40, 20}, {0, 0, 10, 10}, {70, 60, 0, 0}
    };
    tiku_surface_t *s = tiku_surface_new(W, H, GROUND);
    tiku_surface_t *whole = tiku_surface_new(W, H, GROUND);

    assert(s != NULL && whole != NULL);
    draw_field(whole);
    for (size_t i = 0; i < sizeof clips / sizeof clips[0]; i++) {
        tiku_rect_t saved;
        int text = 0;

        tiku_clip_reset(s);
        tiku_fill(s, (tiku_rect_t){0, 0, W, H}, GROUND);
        tiku_clip_set(s, clips[i]);
        saved = s->clip;
        draw_field(s);
        assert(!memcmp(&s->clip, &saved, sizeof saved));
        for (int y = 0; y < H; y++) {
            for (int x = 0; x < W; x++) {
                tiku_rgb_t px = s->px[y * W + x];

                if (!inside(saved, x, y)) {
                    assert(px == GROUND);
                    continue;
                }
                assert(px == whole->px[y * W + x]);
                text += px == TIKU_C_TEXT;
            }
        }
        /* The first clip crosses the field's text: some of it shows. */
        assert(i > 0 || text > 0);
    }
    tiku_surface_free(whole);
    tiku_surface_free(s);
    puts("PASS: a text field draws within its caller's clip as it would "
         "unclipped, and leaves the clip as it found it");
    return 0;
}
