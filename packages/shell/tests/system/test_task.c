/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_task.c - the task loop's contract, headless.
 *
 * The accumulate cases are the ones that matter: they are what keeps a window
 * responsive during an update storm, and they are easy to get subtly wrong.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "tiku_trk_task.h"

static double fake_load_high(void) { return 99.0; }
static double fake_load_low(void)  { return 0.0; }

static int fails;

static void
check(int cond, const char *what)
{
    printf("  [%s] %s\n", cond ? "PASS" : "FAIL", what);
    if (!cond) { fails++; }
}

struct counter { int runs; int folded; };

static int
bump(void *ctx)
{
    ((struct counter *)ctx)->runs++;
    return 0;
}

static int
bump_once(void *ctx)
{
    ((struct counter *)ctx)->runs++;
    return 1;
}

static int
can_always(void *a, void *b) { (void)a; (void)b; return 1; }

static void
fold(void *pending, void *incoming)
{
    (void)incoming;
    ((struct counter *)pending)->folded++;
}

static int
can_never(void *a, void *b) { (void)a; (void)b; return 0; }

int
main(void)
{
    tiku_trk_loop_t *loop = tiku_trk_loop_new();
    struct counter c = { 0, 0 };

    printf("task loop\n");

    /* one-shot: does not run early, runs once, then is gone */
    tiku_trk_run_later(loop, bump, &c, NULL, 30000);
    tiku_trk_loop_run(loop);
    check(c.runs == 0, "one-shot does not run before its time");
    usleep(45000);
    tiku_trk_loop_run(loop);
    check(c.runs == 1, "one-shot runs when due");
    tiku_trk_loop_run(loop);
    check(c.runs == 1 && tiku_trk_loop_count(loop) == 0,
          "one-shot is removed after running");

    /* periodic: repeats until the body says stop */
    c.runs = 0;
    tiku_trk_run_periodic(loop, bump, &c, NULL, 10000, 0);
    usleep(12000); tiku_trk_loop_run(loop);
    usleep(12000); tiku_trk_loop_run(loop);
    check(c.runs == 2, "periodic runs each period");
    {
        struct counter d = { 0, 0 };
        tiku_trk_run_periodic(loop, bump_once, &d, NULL, 5000, 0);
        usleep(7000); tiku_trk_loop_run(loop);
        usleep(7000); tiku_trk_loop_run(loop);
        check(d.runs == 1, "periodic stops when the body returns done");
    }

    /* accumulate: the update-storm collapser */
    {
        tiku_trk_loop_t *l2 = tiku_trk_loop_new();
        struct counter a = { 0, 0 };
        int i;

        for (i = 0; i < 50; i++) {
            tiku_trk_run_accumulating(l2, "/folder", bump, &a, NULL,
                                      can_always, fold, 20000, 0, 0);
        }
        check(tiku_trk_loop_count(l2) == 1,
              "50 requests under one key collapse to a single task");
        check(a.folded == 49, "49 of them folded into the pending task");
        usleep(30000);
        tiku_trk_loop_run(l2);
        check(a.runs == 1, "the collapsed task runs exactly once");

        /* the deadline slides while work keeps arriving */
        a.runs = 0; a.folded = 0;
        tiku_trk_run_accumulating(l2, "/f2", bump, &a, NULL, can_always,
                                  fold, 40000, 0, 0);
        usleep(30000);
        tiku_trk_run_accumulating(l2, "/f2", bump, &a, NULL, can_always,
                                  fold, 40000, 0, 0);
        usleep(20000);
        tiku_trk_loop_run(l2);
        check(a.runs == 0, "a re-armed task waits out the new delay");
        usleep(30000);
        tiku_trk_loop_run(l2);
        check(a.runs == 1, "and runs once the storm stops");

        /* limits: a continuous storm must still get serviced */
        a.runs = 0; a.folded = 0;
        for (i = 0; i < 10; i++) {
            tiku_trk_run_accumulating(l2, "/f3", bump, &a, NULL, can_always,
                                      fold, 20000, 0, 3);
        }
        check(tiku_trk_loop_count(l2) > 1,
              "max_count stops one task absorbing forever");

        /* a refusing predicate never folds */
        a.folded = 0;
        for (i = 0; i < 3; i++) {
            tiku_trk_run_accumulating(l2, "/f4", bump, &a, NULL, can_never,
                                      fold, 20000, 0, 0);
        }
        check(a.folded == 0, "can_accumulate=false queues separately");

        /* different keys never merge */
        {
            struct counter x = { 0, 0 };
            tiku_trk_loop_t *l3 = tiku_trk_loop_new();
            tiku_trk_run_accumulating(l3, "/a", bump, &x, NULL, can_always,
                                      fold, 5000, 0, 0);
            tiku_trk_run_accumulating(l3, "/b", bump, &x, NULL, can_always,
                                      fold, 5000, 0, 0);
            check(tiku_trk_loop_count(l3) == 2, "different keys stay separate");
            tiku_trk_loop_free(l3);
        }
        tiku_trk_loop_free(l2);
    }

    /* run-when-idle: waits for quiet */
    {
        tiku_trk_loop_t *l4 = tiku_trk_loop_new();

        /* The USER-idle half under test; the host's own load is pinned
         * quiet so a busy build machine cannot fail it (AW-085). */
        tiku_trk_task_set_load_fn(fake_load_low);
        struct counter q = { 0, 0 };

        tiku_trk_run_when_idle(l4, bump_once, &q, NULL, 40000);
        usleep(15000);
        tiku_trk_loop_note_activity(l4);
        usleep(15000);
        tiku_trk_loop_run(l4);
        check(q.runs == 0, "idle task holds off while there is activity");
        usleep(50000);
        tiku_trk_loop_run(l4);
        check(q.runs == 1, "idle task runs once the loop goes quiet");
        tiku_trk_loop_free(l4);
    }

    /* the sleep hint */
    {
        tiku_trk_loop_t *l5 = tiku_trk_loop_new();
        struct counter z = { 0, 0 };
        int64_t due;

        check(tiku_trk_loop_next_due(l5) == -1, "idle loop asks to sleep forever");
        tiku_trk_run_later(l5, bump, &z, NULL, 50000);
        due = tiku_trk_loop_next_due(l5);
        check(due > 30000 && due <= 50000, "next-due reports the wait");
        tiku_trk_loop_free(l5);
    }

    tiku_trk_loop_stop(loop);
    check(tiku_trk_loop_stopping(loop) && tiku_trk_loop_run(loop) == 0,
          "a stop request quiesces the event loop before teardown");
    check(tiku_trk_loop_pulse(loop) == 0,
          "deferred panel work is pumped by an existing pulse, not a worker thread");

    /* --- idle work also waits for the MACHINE to be idle (AW-085) ------- */
    {
        tiku_trk_loop_t *l6 = tiku_trk_loop_new();
        struct counter q6 = { 0 };

        tiku_trk_task_set_load_fn(fake_load_high);
        tiku_trk_run_when_idle(l6, bump_once, &q6, NULL, 1);
        usleep(2000);
        (void)tiku_trk_loop_pulse(l6);
        check(q6.runs == 0,
              "AW-085: idle work holds off while the host's load is high, "
              "however quiet the user is");
        tiku_trk_task_set_load_fn(fake_load_low);
        usleep(2000);
        (void)tiku_trk_loop_pulse(l6);
        check(q6.runs == 1,
              "and runs once the load falls under the gate");
        tiku_trk_task_set_load_fn(NULL);
        tiku_trk_loop_free(l6);
    }

    tiku_trk_loop_free(loop);
    printf("%s\n", fails ? "FAILURES" : "all task-loop checks passed");
    return fails ? 1 : 0;
}
