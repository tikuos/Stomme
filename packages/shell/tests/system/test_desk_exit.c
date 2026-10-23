#include "tiku_shell_product.h"
/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_desk_exit.c - the supervisor's exit report names the desk's own
 * crash and stall reports, and no other program's.
 *
 * The supervisor's file is compiled into the test with its main()
 * renamed.  The desk is /bin/sh: one exits with status 1 beside another
 * program's crash and stall reports, and one writes a crash report under
 * its own pid and is killed by SIGSEGV.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#define main tiku_desk_main
#include "../../system/supervisor/tiku_desk.c"
#undef main

static int fails;

/** @brief One result line; a failure counts. */
static void
check(int ok, const char *what)
{
    printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) {
        fails++;
    }
}

/** @brief Write @p text to @p name in the reports folder. */
static void
plant(const char *name, const char *text)
{
    char path[700];
    FILE *f;

    snprintf(path, sizeof path, "%s/%s", folder, name);
    f = fopen(path, "w");
    if (f != NULL) {
        fputs(text, f);
        (void)fclose(f);
    }
}

/** @brief The exit report at @p path, into @p out. */
static void
slurp(const char *path, char *out, size_t max)
{
    FILE *f = fopen(path, "r");
    size_t n = 0u;

    if (f != NULL) {
        n = fread(out, 1u, max - 1u, f);
        (void)fclose(f);
    }
    out[n] = '\0';
}

/** @brief Run sh -c @p cmd as the desk and write its exit report. */
static int
desk_ends(const char *cmd, char *report, size_t max)
{
    char *argv[] = { "tiku-desk", "-c", (char *)cmd, NULL };
    int status = run_desk("/bin/sh", 3, argv, NULL);

    write_exit(status, report, max);
    return status;
}

int
main(void)
{
    static char text[TAIL_ROOM + 4096];
    char dir[] = "/tmp/test_desk_exit_XXXXXX";
    char report[600], cmd[900];
    int status;

    if (mkdtemp(dir) == NULL) {
        printf("could not make a folder\n");
        return 1;
    }
    snprintf(folder, sizeof folder, "%s", dir);

    printf("a plain exit after another program's crash (D-209)\n");
    plant("crash-20260101-090000-Other-9999999.txt", "Other\npid: 9999999\n");
    plant("latest-crash.txt", "Other\npid: 9999999\n");
    plant("stall-20260101-090000-Other-9999999.txt", "Other\npid: 9999999\n");
    plant("latest-stall.txt", "Other\npid: 9999999\n");
    status = desk_ends("exit 1", report, sizeof report);
    slurp(report, text, sizeof text);
    printf("%s", text);
    check(WIFEXITED(status) && WEXITSTATUS(status) == 1,
          "the desk exits with status 1");
    check(strstr(text, "stopped by: exit 1") != NULL,
          "the report names the exit status");
    check(strstr(text, "its own account") == NULL &&
          strstr(text, "a stall before it") == NULL,
          "and names no crash or stall report as the desk's");

    printf("a desk that crashed with a report of its own\n");
    snprintf(cmd, sizeof cmd,
             TIKU_SHELL_TEST_CRASH_COMMAND
             " kill -SEGV $$", folder);
    sleep(1);       /* a second report in the same second keeps its name */
    status = desk_ends(cmd, report, sizeof report);
    slurp(report, text, sizeof text);
    printf("%s", text);
    check(WIFSIGNALED(status) && WTERMSIG(status) == SIGSEGV,
          "the desk is stopped by SIGSEGV");
    check(strstr(text, "its own account: ") != NULL &&
          strstr(text, TIKU_SHELL_CRASH_NAME_FRAGMENT) != NULL,
          "the report names the desk's own crash report");
    check(strstr(text, "Other-9999999") == NULL &&
          strstr(text, "latest-crash.txt") == NULL &&
          strstr(text, "a stall before it") == NULL,
          "and not the other program's reports");

    snprintf(cmd, sizeof cmd, "rm -rf '%s'", dir);
    (void)!system(cmd);
    printf("%s\n", fails ? "FAILURES" : "all passed");
    return fails ? 1 : 0;
}
