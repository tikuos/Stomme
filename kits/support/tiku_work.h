/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_work.h - workers for anything that waits.
 *
 * The loop thread posts a job; a worker thread runs it with nothing but
 * what its context holds; the loop collects the finished job on a later
 * turn and applies its result.  The jobs wait in one fixed table, from
 * which a small pool of worker threads takes them.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_WORK_H_
#define TIKU_WORK_H_

#include <stddef.h>

#define TIKU_WORK_KEY 48                  /* a key's bytes, with its NUL */

/**
 * @brief One piece of work.
 *
 * @c run is called on a worker thread with @c ctx; it may block, and
 * touches nothing but what @c ctx holds.  @c done is called on the loop,
 * from tiku_work_collect(), after @c run returns, unless the job was
 * cancelled; @c free is called there after @c done, or in its place.
 * Either may be NULL.  Jobs sharing a @c key run one at a time, in the
 * order posted; a job with an empty key runs beside anything.
 */
typedef struct tiku_work_job {
    void (*run)(void *ctx);
    void (*done)(void *ctx);
    void (*free)(void *ctx);
    void *ctx;
    char  key[TIKU_WORK_KEY];
} tiku_work_job_t;

/**
 * @brief Copy @p job into the table for a worker, starting the workers on
 *        the first call and on each later one until a worker starts; a
 *        key is cut to TIKU_WORK_KEY - 1 bytes.
 * @return 0 when queued; -1 when the table is full or the job has no run.
 * @note Loop thread only.
 */
int tiku_work_post(const tiku_work_job_t *job);

/**
 * @brief The same for a job that waits on a device, such as asking a port
 *        who is on it.  Slow jobs never take the last idle worker, so a
 *        job posted with tiku_work_post() always finds one.
 * @return 0 when queued; -1 when the table is full or the job has no run.
 * @note Loop thread only.
 */
int tiku_work_post_slow(const tiku_work_job_t *job);

/**
 * @brief Run @c done, then @c free, for every job finished since last
 *        time, and free its slot.
 *
 * Takes the jobs finished at entry; a job posted from a @c done here is
 * collected by a later call.
 * @return how many were collected.
 * @note Loop thread only; call it once a turn.
 */
int tiku_work_collect(void);

/**
 * @brief Cancel every queued or running job under @p key.  A job not yet
 *        started never runs; one running sees tiku_work_cancelled() say
 *        so and may stop early.  Neither has its @c done called.
 * @return how many were marked.
 */
int tiku_work_cancel(const char *key);

/** @brief Whether the running job whose context is @p ctx was
 *         cancelled; 0 when no running job has it.
 *  @note Any thread. */
int tiku_work_cancelled(const void *ctx);

/** @brief Jobs posted and not yet collected. */
int tiku_work_busy(void);

#endif /* TIKU_WORK_H_ */
