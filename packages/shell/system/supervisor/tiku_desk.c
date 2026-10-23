/*
 * Desktop shell and file manager.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_desk.c - the desktop supervisor: runs TikuDesktop and reports how it
 * ended.
 *
 * Starts TikuDesktop with its stderr in desk.log and waits.  When the desk
 * ends badly it writes an exit report (the signal or status, the paths of
 * the desk's own crash and stall reports, and the log's last lines) and
 * starts the desk once more, passing -after and the report's path.  A desk
 * that ends badly again is left down, with the reason on stderr.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_shell_product.h"
#include "tiku_report.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define LOG_LIMIT       (1024L * 1024L)     /* desk.log rolls over at */
#define TAIL_LINES      200                 /* log lines in a report  */
#define TAIL_ROOM       (64 * 1024)         /* bytes read for them    */
#define SOON_S          60                  /* a second bad end sooner
                                             * than this after the
                                             * restart is reported as
                                             * "within a minute" */

static pid_t child;                          /* the running desk, or 0 */
static pid_t ended;                          /* the desk last waited for */
static char  folder[512];                    /* the reports folder     */
static volatile sig_atomic_t asked_to_end;   /* SIGTERM, SIGINT or SIGHUP
                                              * reached this program   */

/**
 * @brief The desk to run: $TIKU_DESK when set (the test harness sets it),
 *        else the TikuDesktop beside this program, else "TikuDesktop" on
 *        the PATH.
 */
static void
find_desk(const char *argv0, char *out, size_t max)
{
    const char *named = getenv("TIKU_DESK");
    char dir[512];
    char *slash;

    if (named != NULL && named[0] != '\0') {
        snprintf(out, max, "%s", named);
        return;
    }
    snprintf(dir, sizeof dir, "%s", argv0);
    slash = strrchr(dir, '/');
    if (slash != NULL) {
        *slash = '\0';
        snprintf(out, max, TIKU_SHELL_DESKTOP_EXECUTABLE_PATH, dir);
        if (access(out, X_OK) == 0) {
            return;
        }
    }
    snprintf(out, max, TIKU_SHELL_DESKTOP_NAME);
}

/** @brief mkdir -p, for the reports folder on a fresh home. */
static void
make_path(const char *path)
{
    char part[512];
    size_t i;

    snprintf(part, sizeof part, "%s", path);
    for (i = 1; part[i] != '\0'; i++) {
        if (part[i] == '/') {
            part[i] = '\0';
            (void)mkdir(part, 0700);
            part[i] = '/';
        }
    }
    (void)mkdir(part, 0700);
}

/**
 * @brief Open desk.log for the desk's stderr, rolling the old one over
 *        to desk.log.1 when it has grown past its limit.
 */
static int
open_log(void)
{
    char path[600], old[600];
    struct stat st;

    snprintf(path, sizeof path, "%s/desk.log", folder);
    snprintf(old, sizeof old, "%s/desk.log.1", folder);
    if (stat(path, &st) == 0 && st.st_size > LOG_LIMIT) {
        (void)rename(path, old);
    }
    return open(path, O_WRONLY | O_CREAT | O_APPEND, 0600);
}

/** @brief The last TAIL_LINES lines of desk.log, at most @p max - 1 bytes,
 *         into @p out.  @return their length. */
static size_t
log_tail(char *out, size_t max)
{
    char path[600];
    FILE *f;
    long size, from;
    size_t n, lines = 0u, i;

    snprintf(path, sizeof path, "%s/desk.log", folder);
    f = fopen(path, "r");
    if (f == NULL) {
        out[0] = '\0';
        return 0u;
    }
    (void)fseek(f, 0L, SEEK_END);
    size = ftell(f);
    from = (size > (long)max - 1L) ? size - ((long)max - 1L) : 0L;
    (void)fseek(f, from, SEEK_SET);
    n = fread(out, 1u, max - 1u, f);
    out[n] = '\0';
    (void)fclose(f);
    /* Count back TAIL_LINES line ends and start there. */
    for (i = n; i > 0u; i--) {
        if (out[i - 1u] == '\n' && ++lines > TAIL_LINES) {
            memmove(out, out + i, n - i + 1u);
            n -= i;
            break;
        }
    }
    return n;
}

/**
 * @brief Write exit-<time>.txt for a desk that ended with @p status, its
 *        path into @p path, and point latest-exit.txt at it.  The crash
 *        and stall reports it names are those whose file names end in the
 *        desk's pid.
 */
static void
write_exit(int status, char *path, size_t max)
{
    static char tail[TAIL_ROOM];
    char latest[600];
    struct tm when;
    time_t now = time(NULL);
    char suffix[32];
    DIR *dir;
    FILE *f;

    (void)localtime_r(&now, &when);
    snprintf(path, max, "%s/exit-%04d%02d%02d-%02d%02d%02d.txt", folder,
             when.tm_year + 1900, when.tm_mon + 1, when.tm_mday,
             when.tm_hour, when.tm_min, when.tm_sec);
    f = fopen(path, "w");
    if (f == NULL) {
        return;
    }
    fprintf(f, TIKU_SHELL_CRASH_REPORT_FORMAT,
            when.tm_year + 1900, when.tm_mon + 1, when.tm_mday,
            when.tm_hour, when.tm_min, when.tm_sec);
    if (WIFSIGNALED(status)) {
        fprintf(f, "stopped by: signal %d (%s)\n", WTERMSIG(status),
                strsignal(WTERMSIG(status)));
    } else {
        fprintf(f, "stopped by: exit %d\n", WEXITSTATUS(status));
    }
    /* tiku_report names a process's crash and stall reports
     * crash-<time>-<program>-<pid>.txt and stall-<time>-<program>-<pid>.txt;
     * other programs write theirs into the same folder. */
    snprintf(suffix, sizeof suffix, "-%ld.txt", (long)ended);
    dir = opendir(folder);
    while (dir != NULL) {
        struct dirent *e = readdir(dir);
        size_t n, k = strlen(suffix);

        if (e == NULL) {
            break;
        }
        n = strlen(e->d_name);
        if (n <= k || strcmp(e->d_name + n - k, suffix) != 0) {
            continue;
        }
        if (strncmp(e->d_name, "crash-", 6) == 0) {
            fprintf(f, "its own account: %s/%s\n", folder, e->d_name);
        } else if (strncmp(e->d_name, "stall-", 6) == 0) {
            fprintf(f, "a stall before it: %s/%s\n", folder, e->d_name);
        }
    }
    if (dir != NULL) {
        (void)closedir(dir);
    }
    fprintf(f, "\nthe last it printed:\n");
    if (log_tail(tail, sizeof tail) > 0u) {
        fputs(tail, f);
    }
    (void)fclose(f);
    snprintf(latest, sizeof latest, "%s/latest-exit.txt", folder);
    (void)unlink(latest);
    (void)symlink(path, latest);
}

/*---------------------------------------------------------------------------*/
/* RUNNING THE DESK                                                          */
/*---------------------------------------------------------------------------*/

/** @brief Signal handler: note that this program was asked to end, and
 *         pass @p sig on to the running desk. */
static void
pass_on(int sig)
{
    asked_to_end = 1;
    if (child > 0) {
        (void)kill(child, sig);
    }
}

/**
 * @brief Start the desk and wait for it.
 *
 * @param after the exit report to tell it about, or NULL.
 * @return the wait status, or -1 when no process could be made or waited
 *         for; a desk that cannot be executed exits with status 127.
 */
static int
run_desk(const char *desk, int argc, char **argv, const char *after)
{
    /* argv[0], the caller's arguments, -after, its path, the end. */
    const char **args = calloc((size_t)argc + 4u, sizeof *args);
    int status = 0, i, n = 0, log;

    if (args == NULL) {
        return -1;
    }
    args[n++] = desk;
    for (i = 1; i < argc; i++) {
        args[n++] = argv[i];
    }
    if (after != NULL) {
        args[n++] = "-after";
        args[n++] = after;
    }
    args[n] = NULL;

    log = open_log();
    child = fork();
    if (child == 0) {
        /* Only what is needed to become the desk: no allocation, no
         * stdio, between here and exec. */
        if (log >= 0) {
            (void)dup2(log, STDERR_FILENO);
            (void)close(log);
        }
        execvp(desk, (char *const *)(void *)args);
        _exit(127);
    }
    if (log >= 0) {
        (void)close(log);
    }
    free(args);
    if (child < 0) {
        return -1;
    }
    while (waitpid(child, &status, 0) < 0) {
        if (errno != EINTR) {
            return -1;
        }
    }
    ended = child;
    child = 0;
    return status;
}

/**
 * @brief Whether the desk ended well: exit status 0, or any end after this
 *        program was itself asked to end.
 */
static int
ended_well(int status)
{
    return asked_to_end ||
           (WIFEXITED(status) && WEXITSTATUS(status) == 0);
}

/**
 * @brief tiku-desk [desk arguments]: run TikuDesktop with the arguments,
 *        restarting it once after a bad end.
 * @return 0 when the desk ended well, 127 when run_desk() fails, 1 when
 *         it ended badly a second time, which includes a desk that cannot
 *         be executed.
 */
int
main(int argc, char **argv)
{
    char desk[512], report[600];
    time_t began;
    int status;
    struct sigaction sa;

    tiku_report_folder(NULL, folder, sizeof folder);
    make_path(folder);
    find_desk(argv[0], desk, sizeof desk);

    memset(&sa, 0, sizeof sa);
    sa.sa_handler = pass_on;
    (void)sigaction(SIGTERM, &sa, NULL);
    (void)sigaction(SIGINT, &sa, NULL);
    (void)sigaction(SIGHUP, &sa, NULL);

    began = time(NULL);
    status = run_desk(desk, argc, argv, NULL);
    if (status < 0) {
        fprintf(stderr, "tiku-desk: could not start %s\n", desk);
        return 127;
    }
    if (ended_well(status)) {
        return 0;
    }
    write_exit(status, report, sizeof report);
    fprintf(stderr, "tiku-desk: the desk ended badly; see %s\n", report);

    /* Once more, passing -after and the report.  A second bad end is not
     * restarted; the reason goes to stderr. */
    began = time(NULL);
    status = run_desk(desk, argc, argv, report);
    if (status >= 0 && ended_well(status)) {
        return 0;
    }
    if (status >= 0) {
        write_exit(status, report, sizeof report);
    }
    fprintf(stderr, "tiku-desk: ended badly again%s; not started a "
                    "third time.  See %s\n",
            (time(NULL) - began < SOON_S) ? " within a minute" : "",
            report);
    return 1;
}
