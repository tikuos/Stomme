/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_clock.c - reject clock regressions and deadline overflow.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_clock.h"

int tiku_clock_now(tiku_clock_t *clock, uint64_t *microseconds)
{
    uint64_t now;
    if (!clock || !clock->read || !microseconds ||
        !clock->read(clock->context, &now) || now < clock->last) {
        return 0;
    }
    clock->last = now;
    *microseconds = now;
    return 1;
}

int tiku_clock_after(tiku_clock_t *clock, uint64_t delay, uint64_t *deadline)
{
    uint64_t now;
    if (!deadline || !tiku_clock_now(clock, &now) || delay > UINT64_MAX - now) {
        return 0;
    }
    *deadline = now + delay;
    return 1;
}
