/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_work.c - workers for anything that waits.
 *
 * One table of slots under one lock.  A worker takes the oldest QUEUED
 * slot whose key has nothing RUNNING, and for a slow job only while fewer
 * than SLOW_MAX slow jobs run; it runs the job unlocked and marks it
 * FINISHED.  The loop takes the FINISHED slots at collect and runs their
 * done unlocked.  Nothing here allocates: the slots are a fixed table,
 * and what a job needs it brought in its own context.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_work.h"

#include <pthread.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "tiku_trail.h"

/*---------------------------------------------------------------------------*/
/* THE TABLE                                                                 */
/*---------------------------------------------------------------------------*/

#define SLOTS    64
#define WORKERS  4
/* Slow jobs run on at most this many workers at once. */
#define SLOW_MAX (WORKERS - 1)

enum { FREE = 0, QUEUED, RUNNING, FINISHED };

typedef struct {
    tiku_work_job_t job;
    int             state;
    int             cancelled;
    int             slow;           /* posted by tiku_work_post_slow() */
    unsigned long   order;          /* posted when; oldest runs first */
    long            posted_ms, began_ms, ended_ms;
} slot_t;

static slot_t          slots[SLOTS];
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  more = PTHREAD_COND_INITIALIZER;
static unsigned long   posted;
static int             started;
static tiku_work_observation_t history[SLOTS];
static unsigned history_at, history_count;
static unsigned long long collected_count, cancelled_count, rejected_count;
static int             busy;        /* posted, not yet collected */

/** @brief Milliseconds on a clock that does not step. */
static long
now_ms(void)
{
    struct timespec t;

    (void)clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000L + t.tv_nsec / 1000000L;
}

/** @brief Whether a job with @p key is running now; never for an empty
 *         key.
 *  @note Call with the lock held. */
static int
key_running(const char *key)
{
    int i;

    if (key[0] == '\0') {
        return 0;
    }
    for (i = 0; i < SLOTS; i++) {
        if (slots[i].state == RUNNING &&
            strcmp(slots[i].job.key, key) == 0) {
            return 1;
        }
    }
    return 0;
}

/** @brief How many slow jobs are running now.
 *  @note Call with the lock held. */
static int
slow_running(void)
{
    int i, n = 0;

    for (i = 0; i < SLOTS; i++) {
        n += (slots[i].state == RUNNING && slots[i].slow);
    }
    return n;
}

/**
 * @brief The oldest QUEUED slot a worker may take, cancelled or with no
 *        job of its key running, and a slow one only while fewer than
 *        SLOW_MAX slow jobs run; -1 when there is none.
 *
 * Oldest by the order posted: a freed slot is refilled by a later post,
 * so a slot's index does not give its order.
 * @note Call with the lock held.
 */
static int
next_runnable(void)
{
    int i, best = -1, slow_room = slow_running() < SLOW_MAX;

    for (i = 0; i < SLOTS; i++) {
        if (slots[i].state != QUEUED) {
            continue;
        }
        if (best >= 0 && slots[i].order > slots[best].order) {
            continue;
        }
        if (slots[i].cancelled) {
            best = i;
        } else if (!key_running(slots[i].job.key) &&
                   (!slots[i].slow || slow_room)) {
            best = i;
        }
    }
    return best;
}

/*---------------------------------------------------------------------------*/
/* THE WORKERS                                                               */
/*---------------------------------------------------------------------------*/

/** @brief A worker thread: take the oldest runnable slot, run its job
 *         without the lock, mark it FINISHED, and wait for the next. */
static void *
working(void *ignored)
{
    (void)ignored;
    for (;;) {
        int i;

        pthread_mutex_lock(&lock);
        while ((i = next_runnable()) < 0) {
            pthread_cond_wait(&more, &lock);
        }
        if (slots[i].cancelled) {
            /* Never started: finished without running, so the loop
             * frees it without calling done. */
            slots[i].state = FINISHED;
            pthread_mutex_unlock(&lock);
            continue;
        }
        slots[i].state = RUNNING;
        slots[i].began_ms = now_ms();
        pthread_mutex_unlock(&lock);

        slots[i].job.run(slots[i].job.ctx);

        pthread_mutex_lock(&lock);
        slots[i].ended_ms = now_ms();
        slots[i].state = FINISHED;
        /* A key just freed may unblock a job behind it. */
        pthread_cond_broadcast(&more);
        pthread_mutex_unlock(&lock);
    }
    return NULL;
}

/** @brief Start WORKERS detached worker threads, and mark the pool
 *         started when at least one of them was created.
 *  @note Call with the lock held. */
static void
start_workers(void)
{
    int i;

    for (i = 0; i < WORKERS; i++) {
        pthread_t t;

        if (pthread_create(&t, NULL, working, NULL) == 0) {
            (void)pthread_detach(t);
            started = 1;
        }
    }
}

/*---------------------------------------------------------------------------*/
/* THE LOOP'S SIDE                                                           */
/*---------------------------------------------------------------------------*/

/** @brief Queue @p job in a free slot, @p slow or not, and wake a worker.
 *  @return 0 when queued; -1 when the table is full or the job has no
 *          run. */
static int
post(const tiku_work_job_t *job, int slow)
{
    int i;

    if (job == NULL || job->run == NULL) {
        return -1;
    }
    pthread_mutex_lock(&lock);
    if (!started) {
        start_workers();
    }
    for (i = 0; i < SLOTS && slots[i].state != FREE; i++) {
    }
    if (i == SLOTS) {
        if(rejected_count<ULLONG_MAX)rejected_count++;
        pthread_mutex_unlock(&lock);
        tiku_trail("work: full, %s refused", job->key);
        return -1;
    }
    if(posted==ULONG_MAX){if(rejected_count<ULLONG_MAX)rejected_count++;pthread_mutex_unlock(&lock);return -1;}
    slots[i].began_ms=slots[i].ended_ms=0;
    slots[i].job = *job;
    slots[i].job.key[TIKU_WORK_KEY - 1] = '\0';
    slots[i].state = QUEUED;
    slots[i].cancelled = 0;
    slots[i].slow = slow;
    slots[i].order = ++posted;
    slots[i].posted_ms = now_ms();
    busy++;
    pthread_cond_broadcast(&more);
    pthread_mutex_unlock(&lock);
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

int
tiku_work_collect(void)
{
    int taken[SLOTS];
    int i, n = 0, k;

    /* What is finished at entry, and nothing that finishes during. */
    pthread_mutex_lock(&lock);
    for (i = 0; i < SLOTS; i++) {
        if (slots[i].state == FINISHED) {
            taken[n++] = i;
        }
    }
    pthread_mutex_unlock(&lock);

    for (k = 0; k < n; k++) {
        slot_t *s = &slots[taken[k]];
        tiku_work_job_t job = s->job;
        int cancelled = s->cancelled;

        if (!cancelled) {
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
        pthread_mutex_lock(&lock);
        tiku_work_observation_t *record = &history[history_at++ % SLOTS];
        memset(record,0,sizeof *record);
        record->id=s->order;snprintf(record->key,sizeof record->key,"%s",s->job.key);
        record->state=s->cancelled?3:2;record->slow=s->slow;record->cancellation_requested=s->cancelled;
        record->posted_ms=s->posted_ms;record->began_ms=s->began_ms;record->ended_ms=s->ended_ms;
        if(history_count<SLOTS)history_count++;
        if(collected_count<ULLONG_MAX)collected_count++;
        if(s->cancelled&&cancelled_count<ULLONG_MAX)cancelled_count++;
        s->state = FREE;
        busy--;
        pthread_mutex_unlock(&lock);
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
    pthread_mutex_lock(&lock);
    for (i = 0; i < SLOTS; i++) {
        if ((slots[i].state == QUEUED || slots[i].state == RUNNING) &&
            strcmp(slots[i].job.key, key) == 0 && !slots[i].cancelled) {
            slots[i].cancelled = 1;
            n++;
        }
    }
    pthread_cond_broadcast(&more);
    pthread_mutex_unlock(&lock);
    return n;
}

int
tiku_work_cancelled(const void *ctx)
{
    int i, yes = 0;

    pthread_mutex_lock(&lock);
    for (i = 0; i < SLOTS; i++) {
        if (slots[i].state == RUNNING && slots[i].job.ctx == ctx) {
            yes = slots[i].cancelled;
            break;
        }
    }
    pthread_mutex_unlock(&lock);
    return yes;
}

int
tiku_work_busy(void)
{
    int n;

    pthread_mutex_lock(&lock);
    n = busy;
    pthread_mutex_unlock(&lock);
    return n;
}

size_t tiku_work_observe(tiku_work_observation_t *out,size_t capacity)
{
    size_t count=0;int i;
    pthread_mutex_lock(&lock);
    for(i=0;i<SLOTS;i++)if(slots[i].state!=FREE) {
        slot_t *slot=&slots[i];
        if(out && count<capacity) {
            tiku_work_observation_t *record=&out[count];memset(record,0,sizeof *record);
            record->id=slot->order;snprintf(record->key,sizeof record->key,"%s",slot->job.key);
            record->state=slot->state==QUEUED?0:slot->state==RUNNING?1:slot->cancelled?3:2;
            record->slow=slot->slow;record->cancellation_requested=slot->cancelled;
            record->posted_ms=slot->posted_ms;record->began_ms=slot->began_ms;record->ended_ms=slot->ended_ms;
        }
        count++;
    }
    for(i=0;i<(int)history_count;i++) {
        if(out && count<capacity)out[count]=history[(history_at-history_count+(unsigned)i)%SLOTS];
        count++;
    }
    pthread_mutex_unlock(&lock);return count;
}

void tiku_work_stats(tiku_work_stats_t *out)
{
    if(!out)return;
    pthread_mutex_lock(&lock);memset(out,0,sizeof *out);
    out->posted=posted;out->collected=collected_count;out->cancelled=cancelled_count;out->rejected=rejected_count;
    out->active=(unsigned)busy;out->retained=history_count;
    for(int i=0;i<SLOTS;i++) {
        if(slots[i].state==QUEUED)out->queued++;
        else if(slots[i].state==RUNNING)out->running++;
        else if(slots[i].state==FINISHED)out->finished_pending++;
    }
    pthread_mutex_unlock(&lock);
}
