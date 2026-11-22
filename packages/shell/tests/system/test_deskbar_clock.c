/* The Deskbar clock's standing from a product's hook: current, unset (no
 * date, dashes for the time, dimmed) and resumed (dimmed, a question mark).
 * SPDX-License-Identifier: Apache-2.0 */
#define TIKU_SHELL_CLOCK_STATE 1
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../system/deskbar/bar/tiku_trk_deskbar_clock.h"

static int clock_state;

int tiku_shell_clock_state(void) { return clock_state; }

static int same(tiku_rgb_t a, tiku_rgb_t b) { return memcmp(&a, &b, sizeof a) == 0; }

int main(void)
{
    tiku_trk_deskbar_settings_t settings;
    const time_t at = 1795000000;      /* 18 Nov 2026 11:06:40 UTC */
    char text[64];

    memset(&settings, 0, sizeof settings);
    settings.show_clock = 1;
    setenv("TZ", "UTC0", 1);
    tzset();

    clock_state = TIKU_TRK_CLOCK_CURRENT;
    assert(db_clock_state() == TIKU_TRK_CLOCK_CURRENT);
    db_clock_text(&settings, at, db_clock_state(), text, sizeof text);
    assert(strcmp(text, "18 Nov 11:06") == 0);
    assert(same(db_clock_ink(TIKU_TRK_CLOCK_CURRENT), TIKU_C_TEXT));

    /* Unset: no date and no time, in the dimmed ink. */
    clock_state = TIKU_TRK_CLOCK_UNSET;
    assert(db_clock_state() == TIKU_TRK_CLOCK_UNSET);
    db_clock_text(&settings, at, db_clock_state(), text, sizeof text);
    assert(strcmp(text, "--:--") == 0);
    assert(!same(db_clock_ink(TIKU_TRK_CLOCK_UNSET), TIKU_C_TEXT));
    settings.show_seconds = 1;
    db_clock_text(&settings, at, db_clock_state(), text, sizeof text);
    assert(strcmp(text, "--:--:--") == 0);

    /* Resumed: the time with a question mark, dimmed as unset is. */
    clock_state = TIKU_TRK_CLOCK_RESUMED;
    assert(db_clock_state() == TIKU_TRK_CLOCK_RESUMED);
    db_clock_text(&settings, at, db_clock_state(), text, sizeof text);
    assert(strcmp(text, "18 Nov 11:06:40?") == 0);
    settings.show_seconds = 0;
    settings.show_timezone = 1;
    db_clock_text(&settings, at, db_clock_state(), text, sizeof text);
    assert(strcmp(text, "18 Nov 11:06? UTC") == 0);
    assert(same(db_clock_ink(TIKU_TRK_CLOCK_RESUMED), db_clock_ink(TIKU_TRK_CLOCK_UNSET)));

    /* A value the hook does not name is current. */
    clock_state = 7;
    assert(db_clock_state() == TIKU_TRK_CLOCK_CURRENT);

    puts("deskbar clock: current, unset and resumed from the product's hook");
    return 0;
}
