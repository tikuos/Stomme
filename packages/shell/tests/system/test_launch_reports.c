/* Failed exits remain observable independently of child signal handlers.
 * SPDX-License-Identifier: Apache-2.0 */
#define _GNU_SOURCE 1
#include <assert.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <unistd.h>
#include "tiku_trk_launch.h"

/** More children than the old 16-slot ring, collected one at a time. */
int main(void)
{
    char reports[] = "/tmp/tiku-launch-reports-XXXXXX";
    assert(mkdtemp(reports)); setenv("TIKU_TRK_REPORTS", reports, 1);
    long killed = 0, expected = 0;
    for (int i = 0; i < 24; i++) {
        pid_t child = fork(); assert(child >= 0);
        if (!child) {
            struct rlimit limit = {0, 0}; setrlimit(RLIMIT_CORE, &limit);
            if (i == 1 || i == 2) { raise(SIGKILL); _exit(9); }
            _exit(i == 0 ? 7 : 0);
        }
        tiku_trk_launch_note(child, "ChildFixture");
        if (i == 1) killed = child;
        if (i == 2) { expected = child; tiku_trk_launch_expect_stop(child); }
    }
    int count = 0, failed = 0, forced = 0;
    for (int i = 0; i < 3000 && count < 24; i++) {
        tiku_trk_death_t d;
        int n = tiku_trk_launch_reap(&d, 1); assert(n <= 1);
        if (!n) { usleep(1000); continue; }
        count++;
        if (d.expected) { assert(d.pid == expected && !d.report[0]); forced++; }
        else if (d.signal || d.status) {
            FILE *f = fopen(d.report, "r"); assert(f);
            char text[65536]; size_t k = fread(text, 1, sizeof text - 1, f);
            text[k] = 0; fclose(f);
            assert(strstr(text, "ChildFixture") && strstr(text, "not a stack trace"));
            if (d.pid == killed) assert(strstr(text, "SIGKILL"));
            else assert(d.status == 7 && strstr(text, "exit status: 7"));
            failed++;
        } else assert(!d.report[0]);
    }
    assert(count == 24 && failed == 2 && forced == 1);
    /* Exec failures must reach the same reaper, not an eager waitpid(NULL). */
    tiku_trk_app_t app = {0};
    strcpy(app.name, "MissingExecutable");
    strcpy(app.cmd, "/not-a-real-tiku-application");
    assert(tiku_trk_launch_run(&app, NULL, 0) == TIKU_TRK_LAUNCH_OK);
    tiku_trk_death_t d; int reaped = 0;
    for (int i = 0; i < 2000 && !reaped; i++) {
        reaped = tiku_trk_launch_reap(&d, 1); if (!reaped) usleep(1000);
    }
    assert(reaped == 1 && d.status == 127 && d.report[0]);
    puts("PASS: all 24 exits retained, failed exits logged, Force Quit excluded, exec failure captured");
    return 0;
}
