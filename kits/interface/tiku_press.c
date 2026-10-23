/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_press.c - a button held down under the pointer.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_press.h"

#include <stddef.h>

int
tiku_press_arm(tiku_press_t *p, int id)
{
    if (p == NULL || id == 0) {
        return 0;
    }
    p->id = id;
    p->inside = 1;
    return 1;
}

int
tiku_press_track(tiku_press_t *p, int under)
{
    int now;

    if (p == NULL || p->id == 0) {
        return 0;
    }
    now = (under == p->id);
    if (now == p->inside) {
        return 0;
    }
    p->inside = now;
    return 1;
}

int
tiku_press_release(tiku_press_t *p, int under)
{
    int id;

    if (p == NULL || p->id == 0) {
        return 0;
    }
    id = (under == p->id) ? p->id : 0;
    p->id = 0;
    p->inside = 0;
    return id;
}

int
tiku_press_held(const tiku_press_t *p, int id)
{
    return p != NULL && id != 0 && p->id == id && p->inside;
}
