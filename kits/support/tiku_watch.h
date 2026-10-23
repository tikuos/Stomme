/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_watch.h - noticing that a turn has not ended.
 *
 * The loop thread beats once a turn.  A watcher thread counts the beats,
 * and when they stop for longer than the limit it has a stall report
 * written, with the loop thread's own stack when that thread answers.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_WATCH_H_
#define TIKU_WATCH_H_

/**
 * @brief Watch the caller's thread, and report when it stops turning.
 *
 * Installs a SIGUSR1 handler and starts the watcher thread.  Once the
 * watcher runs, a later call does nothing.
 *
 * @param after_ms how long a quiet turn is a stall; 0 or less for five
 *                 seconds.
 * @note Call from the thread that runs the loop, after
 *       tiku_report_install().  SIGUSR1 then cuts short a nanosleep() in
 *       that thread, SA_RESTART or not, so a loop that sleeps must allow
 *       for waking early.
 */
void tiku_watch_install(int after_ms);

/**
 * @brief Count a turn of the loop, and the longest gap between turns.
 *
 * Once tiku_watch_install() has run, a beat from any other thread does
 * not count as a turn; tiku_watch_refused() counts it.
 * @note Call it once a turn, from the watched thread.
 */
void tiku_watch_beat(void);

/** @brief Beats refused because they came from another thread. */
int tiku_watch_refused(void);

/** @brief The longest gap between beats since the last reset, in ms. */
long tiku_watch_longest_ms(void);

/** @brief Start measuring the longest gap afresh. */
void tiku_watch_longest_reset(void);

/** @brief How many stall reports have been written; a long stall is
 *         reported again as it goes on, and counted each time. */
int tiku_watch_stalls(void);

#endif /* TIKU_WATCH_H_ */
