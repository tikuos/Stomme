/*
 * Desktop shell and file manager.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_deskbar_paint.c - shared Deskbar row backgrounds and window glyphs.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_deskbar_paint.h"
#define EXPANDER_PEN 2
#define EXPANDER_ARM 5

/** @brief A window's state icon: filled when shown, hollow when minimised,
 *         and ringed in the selection colour when @p current. */
void
tiku_deskbar_window_icon(tiku_surface_t *s, int x, int y, int minimized,
              int current)
{
    tiku_rect_t b = { x, y, 9, 9 };
    tiku_rgb_t dark = tiku_tint(TIKU_C_PANEL, 1.40f);

    tiku_fill(s, b, minimized ? TIKU_C_PANEL : TIKU_C_DOC);
    tiku_frame(s, b, dark);
    if (!minimized) {
        /* A shown window's glyph carries a title bar. */
        tiku_hline(s, b.x + 1, b.y + 1, b.w - 2, TIKU_C_TAB);
        tiku_hline(s, b.x + 1, b.y + 2, b.w - 2, TIKU_C_TAB);
    }
    if (current) {
        tiku_frame(s, (tiku_rect_t){ b.x - 2, b.y - 2, b.w + 4,
                                               b.h + 4 }, TIKU_C_SELECT);
    }
}

/**
 * @brief The expander arrow beside a team: pointing down when its windows
 *        are shown, right when they are not.
 */
void
tiku_deskbar_expander(tiku_surface_t *s, int x, int y, int expanded)
{
    tiku_rgb_t dark = tiku_tint(TIKU_C_PANEL, 1.40f);
    int k;

    /* An open three-point polyline, stroked with an EXPANDER_PEN-pixel
     * pen. */
    for (k = 0; k < EXPANDER_PEN; k++) {
        int i;

        for (i = 0; i < EXPANDER_ARM; i++) {
            if (expanded) {           /* pointing down: \ then /            */
                tiku_pixel(s, x + i, y + i + k, dark);
                tiku_pixel(s, x + 2 * EXPANDER_ARM - 2 - i, y + i + k,
                                dark);
            } else {                  /* pointing right: / then \           */
                tiku_pixel(s, x + i + k, y + i, dark);
                tiku_pixel(s, x + i + k, y + 2 * EXPANDER_ARM - 2 - i,
                                dark);
            }
        }
    }
}

/**
 * @brief A list row's background.
 *
 * An idle row is bevelled on every edge over a light gradient; a selected
 * one is a darker gradient with a single dark line on top and no side
 * bevel.  The label colour is the same either way.
 */
void
tiku_deskbar_row_paint(tiku_surface_t *s, tiku_rect_t r, int selected)
{
    int i, first = 0, end = r.h;

    if (!s->record) {
        if (r.x >= s->clip.x + s->clip.w || r.x + r.w <= s->clip.x ||
            r.y >= s->clip.y + s->clip.h || r.y + r.h <= s->clip.y)
            return;
        if (s->clip.y > r.y) first = s->clip.y - r.y;
        if (end > s->clip.y + s->clip.h - r.y)
            end = s->clip.y + s->clip.h - r.y;
    }

    for (i = first; i < end; i++) {
        float t = (r.h > 1) ? (float)i / (float)(r.h - 1) : 0.0f;
        float tint = selected ? (1.25f + (1.20f - 1.25f) * t)
                              : (0.69f + (1.03f - 0.69f) * t);

        tiku_hline(s, r.x, r.y + i, r.w,
                        tiku_tint(TIKU_C_PANEL, tint));
    }
    if (selected) {
        tiku_hline(s, r.x, r.y, r.w,
                        tiku_tint(TIKU_C_PANEL, 1.40f));
        return;
    }
    tiku_hline(s, r.x, r.y, r.w,
                    tiku_tint(TIKU_C_PANEL, 0.50f));
    tiku_vline(s, r.x, r.y, r.h,
                    tiku_tint(TIKU_C_PANEL, 0.70f));
    tiku_vline(s, r.x + r.w - 1, r.y, r.h,
                    tiku_tint(TIKU_C_PANEL, 1.08f));
    tiku_hline(s, r.x, r.y + r.h - 1, r.w,
                    tiku_tint(TIKU_C_PANEL, 1.08f));
}
