/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_tabs.c - the tab strip's state, layout and hit test.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdio.h>
#include <string.h>

#include "tiku_tabs.h"

void
tiku_tabs_init(tiku_tabs_t *t)
{
    if (t == NULL) {
        return;
    }
    memset(t, 0, sizeof *t);
    t->current = -1;
}

int
tiku_tabs_add(tiku_tabs_t *t, const char *label)
{
    int at;

    if (t == NULL || label == NULL || t->count >= TIKU_TABS_MAX) {
        return -1;
    }
    at = t->count;
    snprintf(t->label[at], sizeof t->label[at], "%s", label);
    t->count++;
    if (t->current < 0) {
        t->current = 0;             /* the first tab is current */
    }
    return at;
}

int
tiku_tabs_count(const tiku_tabs_t *t)
{
    return (t != NULL) ? t->count : 0;
}

int
tiku_tabs_current(const tiku_tabs_t *t)
{
    return (t != NULL) ? t->current : -1;
}

int
tiku_tabs_select(tiku_tabs_t *t, int index)
{
    if (t == NULL || t->count == 0) {
        return -1;
    }
    if (index < 0) {
        index = 0;
    } else if (index >= t->count) {
        index = t->count - 1;
    }
    t->current = index;
    return t->current;
}

int
tiku_tabs_next(tiku_tabs_t *t, int dir)
{
    if (t == NULL || t->count == 0) {
        return -1;
    }
    /* Wrap both ways; adding the count keeps the result non-negative
     * for a negative dir. */
    t->current = ((t->current + dir) % t->count + t->count) % t->count;
    return t->current;
}

tiku_rect_t
tiku_tabs_rect(const tiku_tabs_t *t, tiku_rect_t strip, int index)
{
    tiku_rect_t r = strip;
    int n = (t != NULL) ? t->count : 0;

    if (n <= 0 || index < 0 || index >= n) {
        r.w = 0;
        return r;
    }
    /*
     * Even division, and the last tab takes what the division could not
     * share out, so the tabs cover the strip exactly and no click falls
     * into a gap at the right edge.
     */
    r.w = strip.w / n;
    r.x = strip.x + index * r.w;
    if (index == n - 1) {
        r.w = strip.x + strip.w - r.x;
    }
    return r;
}

int
tiku_tabs_hit(const tiku_tabs_t *t, tiku_rect_t strip, int x, int y)
{
    int i, n = (t != NULL) ? t->count : 0;

    for (i = 0; i < n; i++) {
        tiku_rect_t r = tiku_tabs_rect(t, strip, i);

        if (x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h) {
            return i;
        }
    }
    return -1;
}
