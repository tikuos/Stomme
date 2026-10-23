/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_reap.c - a launched program that dies is named, not numbered.
 *
 * A launched program's exit must be collected, or one that dies leaves
 * no word and a zombie.  What is asked here: the death is written down
 * with the program's NAME and the signal, a clean exit with its status,
 * and only children this shell launched are collected.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "tiku_trail.h"
#include "tiku_trk_launch.h"

static int passes, fails;

static void
check(int ok, const char *what)
{
    printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (ok) {
        passes++;
    } else {
        fails++;
    }
}

/** @brief A child that ends the way it is told to. */
static pid_t
child_that(int sig, int code)
{
    pid_t pid = fork();

    if (pid == 0) {
        if (sig != 0) {
            (void)kill(getpid(), sig);
        }
        _exit(code);
    }
    return pid;
}

int
main(void)
{
    char said[8192];
    pid_t victim, tidy, stranger;
    int i, n = 0;
    char reports[] = "/tmp/tiku-reap-reports-XXXXXX";

    if (!mkdtemp(reports) || setenv("TIKU_TRK_REPORTS", reports, 1))
        return 1;

    printf("a program that ends is named\n");

    victim = child_that(SIGSEGV, 0);
    tidy = child_that(0, 3);
    stranger = child_that(0, 0);        /* never noted: not registered for reaping */
    tiku_trk_launch_note(victim, "Victim.so");
    tiku_trk_launch_note(tidy, "Tidy.so");

    /* They have to have ended before they can be collected. */
    for (i = 0; i < 50 && n < 2; i++) {
        usleep(20000);
        n += tiku_trk_launch_reap(NULL, 0);
    }
    (void)tiku_trail_find(NULL, said, sizeof said);
    check(n == 2, "both programs this shell launched are collected");
    check(strstr(said, "Victim.so") != NULL &&
          strstr(said, "died: signal 11") != NULL,
          "one that died is named, with the signal that killed it");
    check(strstr(said, "Tidy.so") != NULL && strstr(said, "ended: 3") != NULL,
          "one that ended is named, with its status");
    check(strstr(said, "signal 11") > strstr(said, "Victim.so") ||
          strstr(said, "Victim.so (pid") != NULL,
          "and the name is on the same line as the fate");

    /* The stranger was never launched through here: it is not taken. */
    {
        int status = 0;
        pid_t got;

        usleep(50000);
        got = waitpid(stranger, &status, WNOHANG);
        check(got == stranger,
              "a child nobody noted is left for whoever forked it");
    }
        check(tiku_trk_launch_reap(NULL, 0) == 0,
          "and asking again, with nothing ended, collects nothing");
    {
        /* Who died and how, for the shell that has to say so. */
        tiku_trk_death_t gone[2];
        pid_t late = child_that(SIGABRT, 0);
        int got = 0, k;

        tiku_trk_launch_note(late, "Late.so");
        for (k = 0; k < 50 && got == 0; k++) {
            usleep(20000);
            got = tiku_trk_launch_reap(gone, 2);
        }
        check(got == 1 && strcmp(gone[0].name, "Late.so") == 0 &&
              gone[0].signal == SIGABRT,
              "and it hands back who died and what killed them");
    }

    {
        tiku_trk_death_t gone;
        pid_t stopped = child_that(SIGKILL, 0);
        int got = 0;
        tiku_trk_launch_note(stopped, "ForceQuit.so");
        tiku_trk_launch_expect_stop(stopped);
        for (int k = 0; k < 50 && !got; k++) {
            usleep(20000);
            got = tiku_trk_launch_reap(&gone, 1);
        }
        check(got == 1 && gone.signal == SIGKILL && gone.expected,
              "deliberate Force Quit is reaped without a false crash notice");
        stopped = child_that(SIGKILL, 0);
        tiku_trk_launch_note(stopped, "Unexpected.so");
        got = 0;
        for (int k = 0; k < 50 && !got; k++) {
            usleep(20000);
            got = tiku_trk_launch_reap(&gone, 1);
        }
        check(got == 1 && !gone.expected,
              "a reused roster slot does not hide an unexpected termination");
    }
    printf("%d passed, %d failed\n", passes, fails);
    return fails ? 1 : 0;
}
