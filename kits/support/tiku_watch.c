/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_watch.c - noticing that a turn has not ended.
 *
 * The watcher thread reads the beat count.  When it stops moving the
 * watcher sends SIGUSR1 to the loop thread, whose handler writes the
 * stall report with that thread's stack; when no answer comes, the
 * watcher writes the report itself, without a stack.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_watch.h"

#include <pthread.h>
#include <signal.h>
#include <string.h>
#include <time.h>

#include "tiku_report.h"
#include "tiku_trail.h"

/*---------------------------------------------------------------------------*/
/* THE COUNT, AND WHO IS WATCHING IT                                         */
/*---------------------------------------------------------------------------*/

#define LOOK_EVERY_MS   250
#define ANSWER_WITHIN_MS 1000

/* Shared by the loop thread, its SIGUSR1 handler and the watcher thread
 * without a lock; volatile, so each read goes to memory. */
static volatile unsigned long beats;
static volatile int           answered;
static volatile long          quiet_ms;
static volatile int           stalls;

static pthread_t turning;           /* the thread that runs the loop */
static int       after_ms = 5000;
static int       watching_on;
static int       refused;
static long      last_beat_ms;      /* loop thread only */
static volatile long longest_ms;

/** @brief The SIGUSR1 handler, on the loop thread: write the stall
 *         report with this thread's stack, and say it answered. */
static void
asked_to_say(int sig)
{
    (void)sig;
    tiku_report_stall(quiet_ms, 1);
    answered = 1;
}

/** @brief Sleep @p ms; a signal handled on this thread ends it early. */
static void
rest(long ms)
{
    struct timespec t;

    t.tv_sec = ms / 1000L;
    t.tv_nsec = (ms % 1000L) * 1000000L;
    (void)nanosleep(&t, NULL);
}

/**
 * @brief The watcher thread: look at the beat count every LOOK_EVERY_MS,
 *        and when it has not moved for after_ms, have a stall report
 *        written.
 *
 * The report is written again at 30 s and 120 s of quiet, then every
 * 120 s.  The watcher never stops the program.
 */
static void *
watching(void *ignored)
{
    unsigned long seen = beats;
    long quiet = 0L;
    long say_at = after_ms;

    (void)ignored;
    for (;;) {
        rest(LOOK_EVERY_MS);
        if (beats != seen) {
            seen = beats;
            quiet = 0L;
            say_at = after_ms;
            continue;
        }
        quiet += LOOK_EVERY_MS;
        if (quiet < say_at) {
            continue;
        }
        quiet_ms = quiet;
        answered = 0;
        /* The signal cuts short a nanosleep() the loop thread is in,
         * SA_RESTART or not. */
        (void)pthread_kill(turning, SIGUSR1);
        {
            /* A thread in a wait the kernel will not interrupt cannot
             * answer: after ANSWER_WITHIN_MS the watcher writes the
             * report itself, with the trail and no stack. */
            long waited = 0L;

            while (!answered && waited < ANSWER_WITHIN_MS) {
                rest(50L);
                waited += 50L;
            }
            if (!answered) {
                tiku_report_stall(quiet, 0);
            }
        }
        stalls++;
        say_at = (say_at < 30000L) ? 30000L
               : (say_at < 120000L) ? 120000L : quiet + 120000L;
    }
    return NULL;
}

/*---------------------------------------------------------------------------*/
/* ARMING                                                                    */
/*---------------------------------------------------------------------------*/

void
tiku_watch_install(int ms)
{
    pthread_t watcher;
    struct sigaction sa;

    if (watching_on) {
        return;
    }
    after_ms = (ms > 0) ? ms : 5000;
    turning = pthread_self();

    memset(&sa, 0, sizeof sa);
    sa.sa_handler = asked_to_say;
    sa.sa_flags = SA_RESTART | SA_ONSTACK;
    (void)sigemptyset(&sa.sa_mask);
    (void)sigaction(SIGUSR1, &sa, NULL);

    if (pthread_create(&watcher, NULL, watching, NULL) != 0) {
        return;
    }
    (void)pthread_detach(watcher);
    watching_on = 1;
    tiku_trail("a turn quiet for %d ms is written down", after_ms);
}

/** @brief Milliseconds on a clock that does not step. */
static long
now_ms(void)
{
    struct timespec t;

    (void)clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000L + t.tv_nsec / 1000000L;
}

void
tiku_watch_beat(void)
{
    long now;

    if (watching_on && !pthread_equal(pthread_self(), turning)) {
        refused++;
        return;
    }
    now = now_ms();
    if (last_beat_ms != 0L && now - last_beat_ms > longest_ms) {
        longest_ms = now - last_beat_ms;
    }
    last_beat_ms = now;
    beats++;
}

int
tiku_watch_refused(void)
{
    return refused;
}

long
tiku_watch_longest_ms(void)
{
    return longest_ms;
}

void
tiku_watch_longest_reset(void)
{
    longest_ms = 0L;
    last_beat_ms = 0L;
}

int
tiku_watch_stalls(void)
{
    return stalls;
}
