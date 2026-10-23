/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_task.c - the task loop.
 *
 * One ordered list of tasks, each with a due time; the loop runs what is due
 * and reports when to wake next, so an idle Tracker costs nothing.  Cancelled
 * tasks are marked and reaped after the run, since a task body may cancel its
 * own siblings.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_trk_task.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define TASK_KEY_MAX 64

/* The flavours of task, as the original's TaskLoop has them. */
enum task_kind {
    TASK_ONCE,
    TASK_PERIODIC,
    TASK_IDLE,
    TASK_ACCUM,
};

struct tiku_trk_task {
    enum task_kind             kind;
    tiku_trk_task_fn           fn;
    void                      *ctx;
    tiku_trk_free_fn           freefn;
    int64_t                    due;
    int64_t                    period;
    int64_t                    expires;      /* 0 = never                */
    int64_t                    first_queued; /* accumulate window start  */
    int64_t                    idle_for;
    tiku_trk_can_accumulate_fn can;
    tiku_trk_accumulate_fn     acc;
    int32_t                    accumulated;
    int32_t                    max_count;
    int64_t                    max_us;
    char                       key[TASK_KEY_MAX];
    int                        dead;
    struct tiku_trk_task      *next;
};

struct tiku_trk_loop {
    tiku_trk_task_t *head;
    int64_t          last_activity;
    int              running;
    int              stop_requested;
};

static double (*task_load_fn)(void);

void
tiku_trk_task_set_load_fn(double (*fn)(void))
{
    task_load_fn = fn;
}

int
tiku_trk_task_system_busy(void)
{
    double load = -1.0;

    if (task_load_fn != NULL) {
        load = task_load_fn();
    } else if (getloadavg(&load, 1) != 1) {
        /* getloadavg(3): the one-minute load average. */
        load = -1.0;
    }
    if (load < 0.0) {
        return 0;               /* unknown counts as not busy */
    }
    {
        long ncpu = sysconf(_SC_NPROCESSORS_ONLN);

        if (ncpu < 1) {
            ncpu = 1;
        }
        /* The source's kIdleTreshold: idle-only work waits for CPU load
         * under 15% (AW-085). */
        return load / (double)ncpu > 0.15;
    }
}

int64_t
tiku_trk_now(void)
{
    struct timespec ts;

    (void)clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

tiku_trk_loop_t *
tiku_trk_loop_new(void)
{
    tiku_trk_loop_t *loop = calloc(1, sizeof *loop);

    if (loop != NULL) {
        loop->last_activity = tiku_trk_now();
    }
    return loop;
}

/** @brief Free a task, its context released through its free function. */
static void
task_free(tiku_trk_task_t *t)
{
    if (t->freefn != NULL && t->ctx != NULL) {
        t->freefn(t->ctx);
    }
    free(t);
}

void
tiku_trk_loop_free(tiku_trk_loop_t *loop)
{
    tiku_trk_task_t *t;

    if (loop == NULL) {
        return;
    }
    tiku_trk_loop_stop(loop);
    t = loop->head;
    while (t != NULL) {
        tiku_trk_task_t *next = t->next;
        task_free(t);
        t = next;
    }
    free(loop);
}

void
tiku_trk_loop_stop(tiku_trk_loop_t *loop)
{
    if (loop != NULL) {
        /* The shells pump this loop on their own event thread, so there is
         * no worker to join: the stop is a flag, set before teardown, which
         * a running callback checks to finish. */
        loop->stop_requested = 1;
    }
}

int
tiku_trk_loop_stopping(const tiku_trk_loop_t *loop)
{
    return loop != NULL && loop->stop_requested;
}

/** @brief Link a task into the loop, soonest-due first. */
static void
task_insert(tiku_trk_loop_t *loop, tiku_trk_task_t *t)
{
    tiku_trk_task_t **p = &loop->head;

    while (*p != NULL && (*p)->due <= t->due) {
        p = &(*p)->next;
    }
    t->next = *p;
    *p = t;
}

/**
 * @brief A task of @p kind due @p delay microseconds from now, linked into
 * the loop in due order.  @return NULL without a loop or a function, or
 * out of memory.
 */
static tiku_trk_task_t *
task_new(tiku_trk_loop_t *loop, enum task_kind kind, tiku_trk_task_fn fn,
         void *ctx, tiku_trk_free_fn freefn, int64_t delay)
{
    tiku_trk_task_t *t;

    if (loop == NULL || fn == NULL) {
        return NULL;
    }
    t = calloc(1, sizeof *t);
    if (t == NULL) {
        return NULL;
    }
    t->kind = kind;
    t->fn = fn;
    t->ctx = ctx;
    t->freefn = freefn;
    t->due = tiku_trk_now() + delay;
    task_insert(loop, t);
    return t;
}

tiku_trk_task_t *
tiku_trk_run_later(tiku_trk_loop_t *loop, tiku_trk_task_fn fn, void *ctx,
                   tiku_trk_free_fn freefn, int64_t delay_us)
{
    return task_new(loop, TASK_ONCE, fn, ctx, freefn, delay_us);
}

tiku_trk_task_t *
tiku_trk_run_periodic(tiku_trk_loop_t *loop, tiku_trk_task_fn fn, void *ctx,
                      tiku_trk_free_fn freefn, int64_t period_us,
                      int64_t timeout_us)
{
    tiku_trk_task_t *t = task_new(loop, TASK_PERIODIC, fn, ctx, freefn,
                                  period_us);

    if (t != NULL) {
        t->period = (period_us > 0) ? period_us : 1;
        t->expires = (timeout_us > 0) ? tiku_trk_now() + timeout_us : 0;
    }
    return t;
}

tiku_trk_task_t *
tiku_trk_run_when_idle(tiku_trk_loop_t *loop, tiku_trk_task_fn fn, void *ctx,
                       tiku_trk_free_fn freefn, int64_t idle_us)
{
    /* Polled at a fraction of the idle window so the task fires promptly
     * once quiet, without waking often while the user is working. */
    int64_t period = (idle_us > 4) ? idle_us / 4 : 1;
    tiku_trk_task_t *t = task_new(loop, TASK_IDLE, fn, ctx, freefn, period);

    if (t != NULL) {
        t->period = period;
        t->idle_for = idle_us;
    }
    return t;
}

void
tiku_trk_loop_note_activity(tiku_trk_loop_t *loop)
{
    if (loop != NULL) {
        loop->last_activity = tiku_trk_now();
    }
}

tiku_trk_task_t *
tiku_trk_run_accumulating(tiku_trk_loop_t *loop, const char *key,
                          tiku_trk_task_fn fn, void *ctx,
                          tiku_trk_free_fn freefn,
                          tiku_trk_can_accumulate_fn can,
                          tiku_trk_accumulate_fn acc, int64_t delay_us,
                          int64_t max_us, int32_t max_count)
{
    tiku_trk_task_t *t;
    int64_t now;

    if (loop == NULL || fn == NULL || key == NULL) {
        return NULL;
    }
    now = tiku_trk_now();

    /* Fold into a pending task under the same key when it will still have
     * it: the count and time limits stop a continuous storm from deferring
     * the work forever. */
    for (t = loop->head; t != NULL; t = t->next) {
        if (t->dead || t->kind != TASK_ACCUM || strcmp(t->key, key) != 0) {
            continue;
        }
        if (t->max_count > 0 && t->accumulated >= t->max_count) {
            break;
        }
        if (t->max_us > 0 && now > t->first_queued + t->max_us) {
            break;
        }
        if (t->can != NULL && !t->can(t->ctx, ctx)) {
            break;
        }
        if (t->acc != NULL) {
            t->acc(t->ctx, ctx);
        }
        if (freefn != NULL && ctx != NULL) {
            freefn(ctx);          /* the fold consumed it */
        }
        t->accumulated++;
        /* Re-arm: the deadline slides while work keeps arriving. */
        t->due = now + delay_us;
        return t;
    }

    t = task_new(loop, TASK_ACCUM, fn, ctx, freefn, delay_us);
    if (t == NULL) {
        return NULL;
    }
    snprintf(t->key, sizeof t->key, "%s", key);
    t->can = can;
    t->acc = acc;
    t->max_count = max_count;
    t->max_us = max_us;
    t->first_queued = now;
    return t;
}

void
tiku_trk_task_cancel(tiku_trk_loop_t *loop, tiku_trk_task_t *task)
{
    tiku_trk_task_t **p;

    if (loop == NULL || task == NULL) {
        return;
    }
    if (loop->running) {
        task->dead = 1;           /* reaped after the run */
        return;
    }
    for (p = &loop->head; *p != NULL; p = &(*p)->next) {
        if (*p == task) {
            *p = task->next;
            task_free(task);
            return;
        }
    }
}

int32_t
tiku_trk_task_accumulated(const tiku_trk_task_t *task)
{
    return (task != NULL) ? task->accumulated : 0;
}

int
tiku_trk_loop_count(const tiku_trk_loop_t *loop)
{
    const tiku_trk_task_t *t;
    int n = 0;

    for (t = (loop != NULL) ? loop->head : NULL; t != NULL; t = t->next) {
        if (!t->dead) {
            n++;
        }
    }
    return n;
}

int64_t
tiku_trk_loop_next_due(const tiku_trk_loop_t *loop)
{
    const tiku_trk_task_t *t;
    int64_t now = tiku_trk_now();
    int64_t earliest = -1;

    for (t = (loop != NULL) ? loop->head : NULL; t != NULL; t = t->next) {
        if (t->dead) {
            continue;
        }
        int64_t wait = (t->due > now) ? t->due - now : 0;

        if (earliest < 0 || wait < earliest) {
            earliest = wait;
        }
    }
    return earliest;
}

int
tiku_trk_loop_run(tiku_trk_loop_t *loop)
{
    tiku_trk_task_t *t, **p;
    int64_t now;
    int ran = 0;

    if (loop == NULL) {
        return 0;
    }
    if (loop->stop_requested) {
        return 0;
    }
    now = tiku_trk_now();
    loop->running = 1;

    /* The pass compares with one reading of the clock: a task a body adds
     * is due from a later reading, so it waits for the next pass. */
    for (t = loop->head; t != NULL; t = t->next) {
        int done;

        if (t->dead || t->due > now) {
            continue;
        }
        if (t->kind == TASK_IDLE) {
            t->due = now + t->period;
            if (now - loop->last_activity < t->idle_for ||
                tiku_trk_task_system_busy()) {
                /* Still busy, by the user's hand or by the host's load
                 * (AW-085): the wait starts again. */
                continue;
            }
        }
        if (t->expires != 0 && now > t->expires) {
            t->dead = 1;
            continue;
        }
        done = t->fn(t->ctx);
        ran++;
        if (done || t->kind == TASK_ONCE || t->kind == TASK_ACCUM) {
            t->dead = 1;
        } else if (t->kind == TASK_PERIODIC) {
            t->due = now + t->period;
        }
    }
    loop->running = 0;

    p = &loop->head;
    while (*p != NULL) {
        if ((*p)->dead) {
            tiku_trk_task_t *dead = *p;
            *p = dead->next;
            task_free(dead);
        } else {
            p = &(*p)->next;
        }
    }
    return ran;
}

int
tiku_trk_loop_pulse(tiku_trk_loop_t *loop)
{
    /* File panels and the desktop call this from their event heartbeat;
     * there are no worker threads. */
    return tiku_trk_loop_run(loop);
}
