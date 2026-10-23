/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_splash.h - what the screen says while the desktop is coming up.
 *
 * A black screen, the mark in the middle, and one line under it naming the
 * piece of the shell being built, as it is built.  Each line stays up for
 * at least TIKU_TRK_SPLASH_STEP_US.  When loading ends the screen holds,
 * then fades off the running desktop.  Closing is the same screen
 * backwards: the black fades back over the desktop, then the lines name
 * the pieces being put away, in the order they are.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_SPLASH_H_
#define TIKU_TRK_SPLASH_H_

#include "tiku_gfx.h"
#include <stdint.h>

/** @brief The least time a stage stays up, so it can be read. */
#define TIKU_TRK_SPLASH_STEP_US 220000
/** @brief How long the finished screen holds before it fades, and how long
 *         the closing screen holds once down. */
#define TIKU_TRK_SPLASH_HOLD_US 700000
/** @brief How long a fade takes, out or back in. */
#define TIKU_TRK_SPLASH_FADE_US 900000

/** @brief The splash screen's state. */
typedef struct {
    int     live;            /* whether there is a splash at all      */
    int64_t stage_us;        /* when the line now up was written      */
    int64_t done_us;         /* when loading finished; 0 until it has */
    int64_t close_us;        /* when the closing began; 0 until it has*/
    int     count;           /* how many stages have been named       */
    char    what[48];        /* the line now up                       */
} tiku_trk_splash_t;

/**
 * @brief Put the splash up, or not.
 *
 * @param live Zero makes every call below a no-op; a scripted run passes
 *             zero.
 */
void tiku_trk_splash_begin(tiku_trk_splash_t *sp, int live, int64_t now);

/** @brief Name the part of the shell that has just been built; ignored
 *         between the end of loading and the start of closing. */
void tiku_trk_splash_say(tiku_trk_splash_t *sp, const char *what,
                         int64_t now);

/**
 * @brief How long the line now up is still owed, in microseconds.
 *
 * Zero when nothing is owed, and always when the splash is not live.
 *
 * @note The caller sleeps this out before starting the next piece of
 *       work; the splash itself never waits.
 */
int64_t tiku_trk_splash_owed(const tiku_trk_splash_t *sp, int64_t now);

/** @brief The shell is up: hold the screen, then fade it out. */
void tiku_trk_splash_done(tiku_trk_splash_t *sp, int64_t now);

/** @brief Whether any of it is still on the screen. */
int tiku_trk_splash_visible(const tiku_trk_splash_t *sp, int64_t now);

/**
 * @brief How solid it is: 1 while loading and holding, 0 once gone, and
 *        rising again while closing.
 *
 * Everything the splash draws is drawn at this strength, so the mark, the
 * line and the black fade together, out and back in.
 */
float tiku_trk_splash_veil(const tiku_trk_splash_t *sp, int64_t now);

/**
 * @brief Start the closing: the black comes back down over the desktop.
 *
 * The last loading line is dropped: the lines from here name what is being
 * put away.  A splash that is not live reports closed at once through
 * tiku_trk_splash_closed().
 */
void tiku_trk_splash_closing(tiku_trk_splash_t *sp, int64_t now);

/**
 * @brief Whether the screen is down and the shell may stop.
 *
 * True once the black has faded in and held for TIKU_TRK_SPLASH_HOLD_US,
 * and at once when the splash is not live.
 */
int tiku_trk_splash_closed(const tiku_trk_splash_t *sp, int64_t now);

/** @brief Lay it over @p screen, whatever is already drawn there. */
void tiku_trk_splash_draw(const tiku_trk_splash_t *sp, tiku_surface_t *s,
                          tiku_rect_t screen, int64_t now);

#endif /* TIKU_TRK_SPLASH_H_ */
