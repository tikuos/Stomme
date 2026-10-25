/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_springmenu.c - spring-loaded menus: arming over an area, the
 * delay before the menu opens, and the margin the pointer may wander.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_trk_springmenu.h"
#include <string.h>

void tiku_trk_springmenu_reset(tiku_trk_springmenu_t *m)
{ if (m != NULL) memset(m, 0, sizeof *m); }

void
tiku_trk_springmenu_enter(tiku_trk_springmenu_t *m, int x, int y, int w, int h,
                          int64_t now)
{
    if (m == NULL) return;
    m->armed = 1; m->open = 0; m->x = x; m->y = y; m->w = w; m->h = h;
    m->entered = now;
}

int
tiku_trk_springmenu_pulse(tiku_trk_springmenu_t *m, int64_t now)
{
    if (m == NULL || !m->armed || m->open ||
        now - m->entered < TIKU_TRK_SPRING_DELAY_US)
        return 0;
    m->open = 1;
    return 1;
}

int
tiku_trk_springmenu_inside(const tiku_trk_springmenu_t *m, int x, int y)
{
    if (m == NULL || !m->armed) return 0;
    return x >= m->x - TIKU_TRK_SPRING_MARGIN &&
           x < m->x + m->w + TIKU_TRK_SPRING_MARGIN &&
           y >= m->y - TIKU_TRK_SPRING_MARGIN &&
           y < m->y + m->h + TIKU_TRK_SPRING_MARGIN;
}

void
tiku_trk_springmenu_leave(tiku_trk_springmenu_t *m, int x, int y)
{
    if (m != NULL && !tiku_trk_springmenu_inside(m, x, y))
        tiku_trk_springmenu_reset(m);
}

int
tiku_trk_springmenu_item_enabled(int container, int accepts_type, int broken)
{ return container ? !broken : (accepts_type && !broken); }
