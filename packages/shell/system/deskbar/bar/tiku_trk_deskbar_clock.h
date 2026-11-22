/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_trk_deskbar_clock.h - the Deskbar clock's standing, and the words and
 * ink it is drawn with in each.
 *
 * Header-only.  The product's tiku_shell_product.h defines
 * TIKU_SHELL_CLOCK_STATE as 1 to have tiku_shell_clock_state() asked;
 * without it the clock is current.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_DESKBAR_CLOCK_H_
#define TIKU_TRK_DESKBAR_CLOCK_H_

#include <stdio.h>
#include <string.h>
#include <time.h>
#include "tiku_gfx.h"
#include "tiku_shell_product.h"
#include "tiku_trk_deskbar.h"
#include "tiku_trk_deskbar_prefs.h"

#ifndef TIKU_SHELL_CLOCK_STATE
#define TIKU_SHELL_CLOCK_STATE 0
#endif

/** @brief The clock's standing: the product hook's TIKU_TRK_CLOCK_* value,
 *         or current without the hook or for a value it does not name. */
static inline int db_clock_state(void)
{
#if TIKU_SHELL_CLOCK_STATE
    int state = tiku_shell_clock_state();

    if (state == TIKU_TRK_CLOCK_UNSET || state == TIKU_TRK_CLOCK_RESUMED) {
        return state;
    }
#endif
    return TIKU_TRK_CLOCK_CURRENT;
}

/** @brief The clock's ink: the text colour while current, else the text
 *         pulled toward the panel, as a disabled menu item is drawn. */
static inline tiku_rgb_t db_clock_ink(int state)
{
    return state == TIKU_TRK_CLOCK_CURRENT ? TIKU_C_TEXT
                                           : tiku_dim(TIKU_C_TEXT, TIKU_C_PANEL);
}

/**
 * @brief The clock's words at @p now: the date, the time and, as
 *        @p settings ask, the seconds, the weekday and the zone.  Unset:
 *        dashes for the time and no date.  Resumed: a question mark after
 *        the time.
 */
static inline void db_clock_text(const tiku_trk_deskbar_settings_t *settings,
                                 time_t now, int state, char *clock, size_t cap)
{
    struct tm tmv;
    char date[32];
    char time_text[32];
    const char *time_fmt = settings->show_seconds ? "%H:%M:%S" : "%H:%M";

    if (state == TIKU_TRK_CLOCK_UNSET) {
        snprintf(clock, cap, "%s", settings->show_seconds ? "--:--:--" : "--:--");
        return;
    }
    (void)localtime_r(&now, &tmv);
    if (strftime(date, sizeof date,
                 settings->show_day_of_week ? "%a %e %b" : "%e %b",
                 &tmv) == 0) {
        snprintf(date, sizeof date, "%d", tmv.tm_mday);
    }
    if (strftime(time_text, sizeof time_text, time_fmt, &tmv) == 0) {
        snprintf(time_text, sizeof time_text, "%02d:%02d",
                 tmv.tm_hour, tmv.tm_min);
    }
    if (state == TIKU_TRK_CLOCK_RESUMED) {
        size_t used = strlen(time_text);

        if (used + 1u < sizeof time_text) {
            time_text[used] = '?';
            time_text[used + 1u] = '\0';
        }
    }
    if (settings->show_timezone) {
        char zone[16];

        if (strftime(zone, sizeof zone, "%Z", &tmv) == 0 || zone[0] == '\0') {
            snprintf(zone, sizeof zone, "local");
        }
        snprintf(clock, cap, "%s %s %s", date, time_text, zone);
    } else {
        snprintf(clock, cap, "%s %s", date, time_text);
    }
}

#endif /* TIKU_TRK_DESKBAR_CLOCK_H_ */
