/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_clock.h - monotonic microseconds and checked relative deadlines.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_CLOCK_H_
#define TIKU_CLOCK_H_
#include <stdint.h>

/** @brief Owner-confined clock binding; failed or decreasing readings are rejected. */
typedef struct {
    int (*read)(void *context, uint64_t *microseconds);
    void *context;
    uint64_t last;
} tiku_clock_t;

/** @brief Read monotonic time; failure preserves both output and last reading. */
int tiku_clock_now(tiku_clock_t *clock, uint64_t *microseconds);
/** @brief Set now + delay; overflow or clock failure leaves deadline unchanged. */
int tiku_clock_after(tiku_clock_t *clock, uint64_t delay, uint64_t *deadline);
#endif
