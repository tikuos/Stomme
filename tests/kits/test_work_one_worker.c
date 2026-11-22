/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_work_one_worker.c - a pool built with one job thread runs slow jobs
 * and runs one job at a time.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#define TIKU_WORK_WORKERS 1
#include "../../kits/support/tiku_work.c"
#include <stdatomic.h>
#include <stdlib.h>
#include <unistd.h>
#define CHECK(t) do{if(!(t)){fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#t);exit(1);}}while(0)
static atomic_int gate, running, most, ran;
/** @brief Count the jobs running at once, and hold until the gate opens. */
static void hold(void *context)
{
    int now = ++running;

    (void)context;
    if (now > most) most = now;
    while (!gate) usleep(1000);
    ran++;
    running--;
}
/** @brief Count the jobs running at once without waiting. */
static void quick(void *context)
{
    int now = ++running;

    (void)context;
    if (now > most) most = now;
    ran++;
    running--;
}
int main(void)
{
    tiku_work_job_t slow = {.run = hold}, fast = {.run = quick};

    strcpy(slow.key, "slow");
    strcpy(fast.key, "fast");
    CHECK(tiku_work_post_slow(&slow) == 0);
    for (int i = 0; i < 2000 && running == 0; i++) usleep(1000);
    CHECK(running == 1);
    CHECK(tiku_work_post(&fast) == 0);
    usleep(50000);
    CHECK(ran == 0);
    gate = 1;
    for (int i = 0; i < 10000 && tiku_work_busy(); i++) { tiku_work_collect(); usleep(1000); }
    CHECK(!tiku_work_busy() && ran == 2 && most == 1);
    puts("PASS work with one worker: slow jobs run, one job at a time");
    return 0;
}
