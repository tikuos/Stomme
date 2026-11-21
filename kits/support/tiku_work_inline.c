/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_work_inline.c - the work table for a platform without job threads.
 *
 * The same contract as tiku_work.c with the jobs run on the loop thread:
 * tiku_work_collect() runs the oldest queued job to completion, then runs
 * done and free for every finished job, that one included, as a worker
 * that finished between two turns would have them collected.  Jobs sharing
 * a key run in the order posted because one runs at a time.  A running job
 * blocks the loop for as long as its run takes.  It is compiled when the
 * build defines TIKU_WORK_INLINE as 1; tiku_work.c is compiled otherwise.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_work.h"

#if defined(TIKU_WORK_INLINE) && TIKU_WORK_INLINE

#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "tiku_trail.h"

#define SLOTS 64

enum { FREE = 0, QUEUED, RUNNING, FINISHED };

typedef struct {
    tiku_work_job_t job;
    int             state;
    int             cancelled;
    int             slow;
    unsigned long   order;
    long            posted_ms, began_ms, ended_ms;
} slot_t;

static slot_t slots[SLOTS];
static unsigned long posted;
static tiku_work_observation_t history[SLOTS];
static unsigned history_at, history_count;
static unsigned long long collected_count, cancelled_count, rejected_count;
static int busy;
static const void *running_ctx;
static int running_cancelled;

/** @brief Milliseconds on a clock that does not step. */
static long
now_ms(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long)ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

/** @brief Queue @p job in a free slot; 0, or -1 when full or without run. */
static int
post(const tiku_work_job_t *job, int slow)
{
    int i;

    if (job == NULL || job->run == NULL) {
        return -1;
    }
    for (i = 0; i < SLOTS && slots[i].state != FREE; i++) {
    }
    if (i == SLOTS || posted == ULONG_MAX) {
        if (rejected_count < ULLONG_MAX) rejected_count++;
        tiku_trail("work: full, %s refused", job->key);
        return -1;
    }
    memset(&slots[i], 0, sizeof slots[i]);
    slots[i].job = *job;
    slots[i].job.key[TIKU_WORK_KEY - 1] = '\0';
    slots[i].state = QUEUED;
    slots[i].slow = slow;
    slots[i].order = ++posted;
    slots[i].posted_ms = now_ms();
    busy++;
    tiku_trail("work: post %s%s", job->key, slow ? ", slow" : "");
    return 0;
}

int
tiku_work_post(const tiku_work_job_t *job)
{
    return post(job, 0);
}

int
tiku_work_post_slow(const tiku_work_job_t *job)
{
    return post(job, 1);
}

/** @brief Record a collected slot in the history and free it. */
static void
retire(slot_t *s)
{
    tiku_work_observation_t *record = &history[history_at++ % SLOTS];

    memset(record, 0, sizeof *record);
    record->id = s->order;
    snprintf(record->key, sizeof record->key, "%s", s->job.key);
    record->state = s->cancelled ? 3 : 2;
    record->slow = s->slow;
    record->cancellation_requested = s->cancelled;
    record->posted_ms = s->posted_ms;
    record->began_ms = s->began_ms;
    record->ended_ms = s->ended_ms;
    if (history_count < SLOTS) history_count++;
    if (collected_count < ULLONG_MAX) collected_count++;
    if (s->cancelled && cancelled_count < ULLONG_MAX) cancelled_count++;
    s->state = FREE;
    busy--;
}

/** @brief Run the oldest queued job that was not cancelled. */
static void
run_oldest(void)
{
    slot_t *s = NULL;
    int i;

    for (i = 0; i < SLOTS; i++) {
        if (slots[i].state == QUEUED &&
            (s == NULL || slots[i].order < s->order)) {
            s = &slots[i];
        }
    }
    if (s == NULL) {
        return;
    }
    s->state = RUNNING;
    s->began_ms = now_ms();
    if (!s->cancelled) {
        running_ctx = s->job.ctx;
        running_cancelled = 0;
        s->job.run(s->job.ctx);
        running_ctx = NULL;
    }
    s->ended_ms = now_ms();
    s->state = FINISHED;
}

int
tiku_work_collect(void)
{
    int taken[SLOTS];
    int i, n = 0, k;

    run_oldest();
    for (i = 0; i < SLOTS; i++) {
        if (slots[i].state == FINISHED) {
            taken[n++] = i;
        }
    }
    for (k = 0; k < n; k++) {
        slot_t *s = &slots[taken[k]];
        tiku_work_job_t job = s->job;

        if (!s->cancelled) {
            tiku_trail("work: done %s (%ld ms, waited %ld)", job.key,
                       s->ended_ms - s->began_ms,
                       s->began_ms - s->posted_ms);
            if (job.done != NULL) {
                job.done(job.ctx);
            }
        } else {
            tiku_trail("work: dropped %s", job.key);
        }
        if (job.free != NULL) {
            job.free(job.ctx);
        }
        retire(s);
    }
    return n;
}

int
tiku_work_cancel(const char *key)
{
    int i, n = 0;

    if (key == NULL) {
        return 0;
    }
    for (i = 0; i < SLOTS; i++) {
        if ((slots[i].state == QUEUED || slots[i].state == RUNNING) &&
            strcmp(slots[i].job.key, key) == 0) {
            slots[i].cancelled = 1;
            if (slots[i].state == RUNNING &&
                slots[i].job.ctx == running_ctx) {
                running_cancelled = 1;
            }
            n++;
        }
    }
    return n;
}

int
tiku_work_cancelled(const void *ctx)
{
    return ctx != NULL && ctx == running_ctx && running_cancelled;
}

int
tiku_work_busy(void)
{
    return busy;
}

size_t
tiku_work_observe(tiku_work_observation_t *out, size_t capacity)
{
    size_t need = 0;
    int i;
    unsigned k;

    for (i = 0; i < SLOTS; i++) {
        if (slots[i].state == FREE) continue;
        if (out != NULL && need < capacity) {
            tiku_work_observation_t *o = &out[need];

            memset(o, 0, sizeof *o);
            o->id = slots[i].order;
            snprintf(o->key, sizeof o->key, "%s", slots[i].job.key);
            o->state = slots[i].state == QUEUED ? 0 :
                       slots[i].state == RUNNING ? 1 :
                       slots[i].cancelled ? 3 : 2;
            o->slow = slots[i].slow;
            o->cancellation_requested = slots[i].cancelled;
            o->posted_ms = slots[i].posted_ms;
            o->began_ms = slots[i].began_ms;
            o->ended_ms = slots[i].ended_ms;
        }
        need++;
    }
    for (k = 0; k < history_count; k++) {
        if (out != NULL && need < capacity) {
            out[need] = history[(history_at - history_count + k) % SLOTS];
        }
        need++;
    }
    return need;
}

void
tiku_work_stats(tiku_work_stats_t *out)
{
    int i;

    if (out == NULL) {
        return;
    }
    memset(out, 0, sizeof *out);
    out->posted = posted;
    out->collected = collected_count;
    out->cancelled = cancelled_count;
    out->rejected = rejected_count;
    for (i = 0; i < SLOTS; i++) {
        switch (slots[i].state) {
        case QUEUED:   out->queued++; break;
        case RUNNING:  out->running++; break;
        case FINISHED: out->finished_pending++; break;
        default: break;
        }
    }
    out->active = (unsigned)busy;
    out->retained = history_count;
}

#endif
