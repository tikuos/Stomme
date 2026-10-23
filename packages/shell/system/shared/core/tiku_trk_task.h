/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_task.h - delayed, periodic and accumulating tasks.
 *
 * Ported in shape from Tracker's TaskLoop (TaskLoop.h/.cpp): its flavours of
 * task and its accumulate discipline.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_TASK_H_
#define TIKU_TRK_TASK_H_

#include <stdint.h>

/** @brief Microseconds on the monotonic clock. */
int64_t tiku_trk_now(void);

/**
 * @brief Whether the host is too busy for idle-only work (AW-085).
 *
 * The source's gate is CPU load under 15%; here it is the one-minute load
 * average, getloadavg(3), over the online CPU count, against 0.15.  A load
 * that cannot be read counts as not busy.
 */
int tiku_trk_task_system_busy(void);

/** @brief Replace the load source, for tests.  NULL restores the host's. */
void tiku_trk_task_set_load_fn(double (*fn)(void));

/** @brief A task loop: the tasks, in due order, and the last activity. */
typedef struct tiku_trk_loop tiku_trk_loop_t;
/** @brief One scheduled task. */
typedef struct tiku_trk_task tiku_trk_task_t;

/**
 * @brief A task's body.
 *
 * @return Non-zero when the task is finished and must not run again; a
 *         periodic task returns zero to stay scheduled.
 */
typedef int (*tiku_trk_task_fn)(void *ctx);

/**
 * @brief Decide whether a pending task can absorb new work.
 *
 * Tracker's AccumulatingFunctionObject::CanAccumulate: a burst of identical
 * requests collapses into the one already queued.  @p pending is the queued
 * task's context, @p incoming the new one.
 *
 * @return non-zero to fold @p incoming into @p pending.
 */
typedef int (*tiku_trk_can_accumulate_fn)(void *pending, void *incoming);

/** @brief Fold @p incoming into the already-queued @p pending context. */
typedef void (*tiku_trk_accumulate_fn)(void *pending, void *incoming);

/** @brief Release a task's context when the task is destroyed. */
typedef void (*tiku_trk_free_fn)(void *ctx);

/** @brief A loop with no tasks, its last activity now; NULL out of memory. */
tiku_trk_loop_t *tiku_trk_loop_new(void);

/**
 * @brief Run every task whose time has come.
 *
 * Nothing runs once a stop has been requested.
 *
 * @return tasks run.
 */
int tiku_trk_loop_run(tiku_trk_loop_t *loop);
/** @brief Run every task whose time has come, as tiku_trk_loop_run()
 *         does, from a window's own event tick; the loop has no thread of
 *         its own (AW-088).  @return tasks run. */
int tiku_trk_loop_pulse(tiku_trk_loop_t *loop);

/**
 * @brief Microseconds until the next task is due.
 *
 * The event loop sleeps this long; -1 when nothing is scheduled.
 */
int64_t tiku_trk_loop_next_due(const tiku_trk_loop_t *loop);

/** @brief How many live tasks the loop holds. */
int tiku_trk_loop_count(const tiku_trk_loop_t *loop);

/**
 * @brief Ask the loop to stop before it is destroyed: tiku_trk_loop_run()
 *        runs nothing after this, and a running task can check
 *        tiku_trk_loop_stopping() to finish.
 */
void tiku_trk_loop_stop(tiku_trk_loop_t *loop);
/** @brief Whether a stop was requested; a callback checks it to finish. */
int tiku_trk_loop_stopping(const tiku_trk_loop_t *loop);

/** @brief Stop the loop and free every task, contexts included. */
void tiku_trk_loop_free(tiku_trk_loop_t *loop);

/** @brief Run @p fn once, @p delay_us from now.  @return the task, or NULL. */
tiku_trk_task_t *tiku_trk_run_later(tiku_trk_loop_t *loop,
                                    tiku_trk_task_fn fn, void *ctx,
                                    tiku_trk_free_fn freefn,
                                    int64_t delay_us);

/**
 * @brief Run @p fn every @p period_us, the first time one period from now,
 *        until it returns non-zero.
 *
 * @param timeout_us  Give up after this long (0 = never).
 */
tiku_trk_task_t *tiku_trk_run_periodic(tiku_trk_loop_t *loop,
                                       tiku_trk_task_fn fn, void *ctx,
                                       tiku_trk_free_fn freefn,
                                       int64_t period_us,
                                       int64_t timeout_us);

/**
 * @brief Run @p fn whenever the loop has been quiet for @p idle_us and the
 *        host is not busy, until it returns non-zero.
 *
 * Tracker's RunWhenIdleTask: work that should happen, but never while the
 * user is busy.  Activity is reported by tiku_trk_loop_note_activity().
 */
tiku_trk_task_t *tiku_trk_run_when_idle(tiku_trk_loop_t *loop,
                                        tiku_trk_task_fn fn, void *ctx,
                                        tiku_trk_free_fn freefn,
                                        int64_t idle_us);

/** @brief Tell the loop the user did something (resets idle timers). */
void tiku_trk_loop_note_activity(tiku_trk_loop_t *loop);

/**
 * @brief Queue work that collapses with work already queued under @p key.
 *
 * This is the update-storm collapser: a hundred notifications for one folder
 * become one refresh.  When a pending task shares @p key and the hooks it
 * was queued with agree, its accumulate hook folds @p ctx in, @p freefn
 * releases @p ctx, and the deadline moves to @p delay_us from now;
 * otherwise a fresh task is queued with @p can and @p acc.
 *
 * @param max_count  Stop accumulating after this many folds (0 = no limit),
 *                   so a continuous storm still gets serviced.
 * @param max_us     Stop accumulating this long after the first request
 *                   (0 = no limit).
 * @return The task holding the work, or NULL on failure.
 */
tiku_trk_task_t *tiku_trk_run_accumulating(tiku_trk_loop_t *loop,
                                           const char *key,
                                           tiku_trk_task_fn fn, void *ctx,
                                           tiku_trk_free_fn freefn,
                                           tiku_trk_can_accumulate_fn can,
                                           tiku_trk_accumulate_fn acc,
                                           int64_t delay_us,
                                           int64_t max_us,
                                           int32_t max_count);

/** @brief Cancel a task; its context is released, after the run when a
 *         task body cancels it. */
void tiku_trk_task_cancel(tiku_trk_loop_t *loop, tiku_trk_task_t *task);

/** @brief How many times this task absorbed extra work (tests read this). */
int32_t tiku_trk_task_accumulated(const tiku_trk_task_t *task);

#endif /* TIKU_TRK_TASK_H_ */
