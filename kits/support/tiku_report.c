/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_report.c - what a program was doing when it was killed.
 *
 * The report's paths, its header and the program's name are composed
 * when the handlers are installed.  On a fault the handler first writes
 * the report with async-signal-safe calls only, then appends a backtrace
 * from backtrace() and backtrace_symbols_fd(), which are not
 * async-signal-safe: a fault there loses the backtrace, not the report.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_report.h"

#include <execinfo.h>
#include <stdint.h>
#include <fcntl.h>
#include <dirent.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "tiku_sys.h"
#include "tiku_trail.h"

/*---------------------------------------------------------------------------*/
/* WHAT IS READY BEFORE THE FAULT                                            */
/*---------------------------------------------------------------------------*/

#define PATH_ROOM  512
#define FRAMES     64

static char  report_path[PATH_ROOM];
static char  latest_path[PATH_ROOM];
static char  stall_path[PATH_ROOM];
static char  latest_stall[PATH_ROOM];
static char  header[512];           /* program, start time, pid, build */
static char  folder_path[PATH_ROOM / 2];
static char  program_name[64];
static pid_t armed;
static volatile sig_atomic_t asked;
static void *frames[FRAMES];        /* the last backtrace taken */
/*
 * The instruction that faulted, from the signal's context: an array of
 * one, for backtrace_symbols_fd() to name as it names the frames.
 */
static void *fault_pc[1];
static uintptr_t fault_address;
static int fault_code;
/* The stack the fault handler runs on (sigaltstack), so that it runs
 * after a stack overflow too. */
static char  alt_stack[64 * 1024];

/** @brief The signals the fault handler takes. */
static const int FATAL[] = { SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGABRT };

/** @brief Digits of @p v into @p out, and where the text now ends. */
static size_t
put_num(char *out, size_t at, long v)
{
    char digits[24];
    size_t n = 0u;

    if (v < 0) {
        out[at++] = '-';
        v = -v;
    }
    do {
        digits[n++] = (char)('0' + (v % 10));
        v /= 10;
    } while (v != 0 && n < sizeof digits);
    while (n > 0u) {
        out[at++] = digits[--n];
    }
    return at;
}

/** @brief @p s into @p out, and where the text now ends. */
static size_t
put_str(char *out, size_t at, const char *s)
{
    while (s != NULL && *s != '\0') {
        out[at++] = *s++;
    }
    return at;
}

/** @brief The name of @p sig, or "signal" for one not listed here. */
static const char *
named(int sig)
{
    switch (sig) {
    case SIGSEGV: return "SIGSEGV";
    case SIGBUS:  return "SIGBUS";
    case SIGFPE:  return "SIGFPE";
    case SIGILL:  return "SIGILL";
    case SIGABRT: return "SIGABRT";
    case SIGKILL: return "SIGKILL";
    case SIGTERM: return "SIGTERM";
    default:      return "signal";
    }
}

/** @brief @p v in hex after "0x" into @p out, and where the text now
 *         ends; it calls no library function. */
static size_t put_hex(char *out, size_t at, uintptr_t v)
{
    char digits[2 * sizeof v]; size_t n = 0;
    at = put_str(out, at, "0x");
    do { digits[n++] = "0123456789abcdef"[v & 15]; v >>= 4; } while (v);
    while (n) out[at++] = digits[--n];
    return at;
}

/**
 * @brief Append to @p fd the faulting instruction and @p depth frames of
 *        frames[], named by backtrace_symbols_fd().
 * @note Not async-signal-safe: called once the report is written.
 */
static void write_stack(int fd, int depth)
{
    const char label[] = "\nsymbolized stack (best effort):\n";
    (void)!write(fd, label, sizeof label - 1);
    if (fault_pc[0]) backtrace_symbols_fd(fault_pc, 1, fd);
    if (depth > 0) backtrace_symbols_fd(frames, depth, fd);
}

/*---------------------------------------------------------------------------*/
/* THE REPORT                                                                */
/*---------------------------------------------------------------------------*/

/**
 * @brief Write a report to @p path: the header, @p why with @p num and
 *        @p unit when @p unit is not NULL, the trail's last line, where
 *        the fault was, then the whole trail.
 *
 * With @p depth 0 or more the stack follows, from frames[]; a negative
 * @p depth leaves it out, and what is left is async-signal-safe.
 */
static void
write_report(const char *path, const char *why, long num, const char *unit,
             int depth)
{
    char line[256];
    size_t at;
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC |
                        O_NOFOLLOW, 0600);

    if (fd < 0) {
        return;
    }
    (void)!write(fd, header, strlen(header));
    struct timespec captured;
    if (!clock_gettime(CLOCK_REALTIME, &captured)) {
        at = put_str(line, 0, "captured unix time: ");
        at = put_num(line, at, captured.tv_sec); line[at++] = '\n';
        (void)!write(fd, line, at);
    }
    at = put_str(line, 0u, "stopped by: ");
    /* At most 120 bytes of @p why, so the numbers after it fit. */
    for (size_t i = 0; why && why[i] && i < 120; i++) line[at++] = why[i];
    if (unit != NULL) {
        at = put_str(line, at, " (");
        at = put_num(line, at, num);
        at = put_str(line, at, unit);
        line[at++] = ')';
    }
    at = put_str(line, at, "\nlines it had written: ");
    at = put_num(line, at, (long)tiku_trail_written());
    line[at++] = '\n';
    (void)!write(fd, line, at);

    /* The trail's last line comes first, ahead of where the fault was
     * and the stack. */
    at = put_str(line, 0u, "the last thing it did:\n");
    (void)!write(fd, line, at);
    tiku_trail_write_last(fd);

    /* The faulting instruction, from the signal context: on arm64 a
     * function that only stores keeps no frame pointer, and backtrace()
     * leaves its frame out. */
    at = put_str(line, 0u, "\nwhere it was:\n");
    at = put_str(line, at, "instruction: ");
    at = put_hex(line, at, (uintptr_t)fault_pc[0]);
    at = put_str(line, at, "\nfault address: ");
    at = put_hex(line, at, fault_address);
    at = put_str(line, at, "\nsignal code: ");
    at = put_num(line, at, fault_code); line[at++] = '\n';
    (void)!write(fd, line, at);
    at = put_str(line, 0u, "\nwhat it was doing:\n");
    (void)!write(fd, line, at);
    tiku_trail_write(fd);
    if (depth >= 0) write_stack(fd, depth);
    (void)close(fd);
}

/** @brief The fault handler: write the report, append the backtrace,
 *         then raise @p sig again under its default action. */
static void
took_signal(int sig, siginfo_t *info, void *context)
{
    int depth;

    fault_address = info ? (uintptr_t)info->si_addr : 0;
    fault_code = info ? info->si_code : 0;
    fault_pc[0] = tiku_sys_fault_pc(context);
    /* The report is written before backtrace() runs: a fault in the
     * unwinder leaves it in place. */
    write_report(report_path, named(sig), sig, "", -1);
    write_report(latest_path, named(sig), sig, "", -1);
    depth = backtrace(frames, FRAMES);
    const char *paths[] = {report_path, latest_path};
    for (int i = 0; i < 2; i++) {
        int fd = open(paths[i], O_WRONLY | O_APPEND | O_CLOEXEC | O_NOFOLLOW);
        if (fd >= 0) { write_stack(fd, depth); close(fd); }
    }
    /* Raised again under the default action: a core is dumped where
     * cores are enabled, and the parent sees the signal. */
    (void)signal(sig, SIG_DFL);
    (void)raise(sig);
}

/*---------------------------------------------------------------------------*/
/* ARMING                                                                    */
/*---------------------------------------------------------------------------*/

/** @brief Make @p dir and everything above it, as far as it can. */
static void
make_path(const char *dir)
{
    char work[PATH_ROOM];
    size_t i;

    snprintf(work, sizeof work, "%s", dir);
    for (i = 1u; work[i] != '\0'; i++) {
        if (work[i] == '/') {
            work[i] = '\0';
            (void)mkdir(work, 0700);
            work[i] = '/';
        }
    }
    (void)mkdir(work, 0700);
}

void
tiku_report_folder(const char *dir, char *out, size_t max)
{
    const char *home = getenv("HOME");

    if (dir == NULL) {
        dir = getenv("TIKU_TRK_REPORTS");
    }
    if (dir != NULL && dir[0] != '\0') {
        snprintf(out, max, "%s", dir);
    } else {
        snprintf(out, max, "%s/.config/tracker/reports",
                 (home != NULL) ? home : "/tmp");
    }
}

void
tiku_report_install(const char *program, const char *dir)
{
    char folder[PATH_ROOM / 2];
    struct tm when;
    time_t now = time(NULL);
    size_t i;

    if (armed == getpid()) {
        return;
    }
        tiku_report_folder(dir, folder, sizeof folder);
    make_path(folder);
    snprintf(folder_path, sizeof folder_path, "%s", folder);
    snprintf(program_name, sizeof program_name, "%s",
             (program != NULL) ? program : "program");
    for (char *p = program_name; *p; p++)
        if (*p == '/' || *p == '\\' || (unsigned char)*p < 32) *p = '_';

    /* The names and the header are formatted now, outside any handler. */
    (void)localtime_r(&now, &when);
    snprintf(report_path, sizeof report_path,
             "%s/crash-%04d%02d%02d-%02d%02d%02d-%s-%ld.txt", folder,
             when.tm_year + 1900, when.tm_mon + 1, when.tm_mday,
             when.tm_hour, when.tm_min, when.tm_sec,
             program_name, (long)getpid());
    snprintf(latest_path, sizeof latest_path, "%s/latest-crash.txt", folder);
    snprintf(stall_path, sizeof stall_path,
             "%s/stall-%04d%02d%02d-%02d%02d%02d-%s-%ld.txt", folder,
             when.tm_year + 1900, when.tm_mon + 1, when.tm_mday,
             when.tm_hour, when.tm_min, when.tm_sec,
             program_name, (long)getpid());
    snprintf(latest_stall, sizeof latest_stall, "%s/latest-stall.txt",
             folder);
    snprintf(header, sizeof header,
             "%s\nstarted: %04d-%02d-%02d %02d:%02d:%02d\npid: %ld\n"
             "report runtime build: " __DATE__ " " __TIME__ "\n",
             (program != NULL) ? program : "program",
             when.tm_year + 1900, when.tm_mon + 1, when.tm_mday,
             when.tm_hour, when.tm_min, when.tm_sec, (long)getpid());

    /* The unwinder loads what it needs on its first use; calling it once
     * here keeps the fault's own backtrace from allocating. */
    (void)backtrace(frames, FRAMES);

    {
        stack_t ss;

        ss.ss_sp = alt_stack;
        ss.ss_size = sizeof alt_stack;
        ss.ss_flags = 0;
        (void)sigaltstack(&ss, NULL);
    }
    for (i = 0u; i < sizeof FATAL / sizeof FATAL[0]; i++) {
        struct sigaction sa;

        memset(&sa, 0, sizeof sa);
        sa.sa_sigaction = took_signal;
        /* ONSTACK so a stack overflow is caught; RESETHAND so a fault
         * inside the handler kills the process; NODEFER so the handler's
         * raise() reaches the default action at once; SIGINFO for the
         * faulting address, and the context that holds the faulting
         * instruction. */
        sa.sa_flags = SA_RESETHAND | SA_NODEFER | SA_ONSTACK | SA_SIGINFO;
        (void)sigemptyset(&sa.sa_mask);
        (void)sigaction(FATAL[i], &sa, NULL);
    }
    armed = getpid();
    tiku_trail("reports go to %s", report_path);
}

const char *
tiku_report_path(void)
{
    return report_path;
}

const char *
tiku_report_latest(void)
{
    return latest_path;
}

const char *
tiku_report_stall_path(void)
{
    return stall_path;
}

void
tiku_report_stall(long ms, int with_stack)
{
    int depth = 0;

    if (!armed) {
        return;
    }
    if (with_stack) {
        depth = backtrace(frames, FRAMES);
    }
    write_report(stall_path, "a turn that has not ended", ms, " ms", depth);
    write_report(latest_stall, "a turn that has not ended", ms, " ms",
                 depth);
}

void
tiku_report_now(const char *why)
{
    int depth;

    if (!armed) {
        return;
    }
    depth = backtrace(frames, FRAMES);
    write_report(report_path, (why != NULL) ? why : "asked for", 0L, NULL,
                 depth);
    write_report(latest_path, (why != NULL) ? why : "asked for", 0L, NULL,
                 depth);
}

int tiku_report_child(const char *program, long pid, int sig, int status,
                      char *out, size_t max)
{
    char folder[PATH_ROOM / 2], latest[PATH_ROOM], suffix[48];
    struct tm when; time_t now = time(NULL);
    if (!out || !max) return -1;
    out[0] = 0;
    tiku_report_folder(NULL, folder, sizeof folder); make_path(folder);
    localtime_r(&now, &when);
    if ((size_t)snprintf(out, max,
        "%s/exit-%04d%02d%02d-%02d%02d%02d-%ld.txt", folder,
        when.tm_year + 1900, when.tm_mon + 1, when.tm_mday,
        when.tm_hour, when.tm_min, when.tm_sec, pid) >= max) {
        out[0] = 0; return -1;
    }
    int fd = open(out, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    if (fd < 0) { out[0] = 0; return -1; }
    FILE *f = fdopen(fd, "w");
    if (!f) { close(fd); unlink(out); out[0] = 0; return -1; }
    fprintf(f, "%s\napplication exit observed by parent %ld\npid: %ld\n",
            program ? program : "application", (long)getpid(), pid);
    fprintf(f, "signal: %d (%s)\nexit status: %d\n",
            sig, sig ? named(sig) : "none", status);
    fprintf(f, "stopped by: %s %s %d\n", program ? program : "application",
            sig ? "signal" : "exit status", sig ? sig : status);
    fputs("This is an exit record, not a stack trace from the child.\n", f);
    if (sig == SIGKILL)
        fputs("SIGKILL cannot be caught; an out-of-memory kill is one possible cause.\n", f);
    snprintf(suffix, sizeof suffix, "-%ld.txt", pid);
    DIR *dir = opendir(folder); struct dirent *entry;
    while (dir && (entry = readdir(dir))) {
        size_t n = strlen(entry->d_name), k = strlen(suffix);
        if (!strncmp(entry->d_name, "crash-", 6) && n > k &&
            !strcmp(entry->d_name + n - k, suffix))
            fprintf(f, "child report: %s/%s\n", folder, entry->d_name);
    }
    if (dir) closedir(dir);
    fputs("\nParent launch/lifecycle trail (not child events):\n", f);
    fflush(f); tiku_trail_write(fd);
    int ok = !ferror(f); if (fclose(f)) ok = 0;
    if (!ok) { out[0] = 0; return -1; }
    snprintf(latest, sizeof latest, "%s/latest-application-exit.txt", folder);
    unlink(latest);
    if (symlink(strrchr(out, '/') + 1, latest))
        tiku_trail("could not update latest application exit link");
    return 0;
}

/*---------------------------------------------------------------------------*/
/* ON DEMAND                                                                 */
/*---------------------------------------------------------------------------*/

/** @brief The SIGUSR2 handler: set the flag tiku_report_asked() reads. */
static void
was_asked(int sig)
{
    (void)sig;
    asked = 1;
}

void
tiku_report_ask_install(void)
{
    struct sigaction sa;

    memset(&sa, 0, sizeof sa);
    sa.sa_handler = was_asked;
    sa.sa_flags = SA_RESTART;
    (void)sigemptyset(&sa.sa_mask);
    (void)sigaction(SIGUSR2, &sa, NULL);
}

int
tiku_report_asked(void)
{
    int was = asked;

    asked = 0;
    return was;
}

int
tiku_report_write(const char *why, void (*snapshot)(FILE *, void *),
                  void *ctx, char *out, size_t max)
{
    char latest[PATH_ROOM];
    struct tm when;
    time_t now = time(NULL);
    FILE *f;

    out[0] = '\0';
    if (!armed) {
        return -1;
    }
    (void)localtime_r(&now, &when);
    snprintf(out, max, "%s/report-%04d%02d%02d-%02d%02d%02d-%s-%ld.txt",
             folder_path, when.tm_year + 1900, when.tm_mon + 1,
             when.tm_mday, when.tm_hour, when.tm_min, when.tm_sec,
             program_name, (long)getpid());
    f = fopen(out, "w");
    if (f == NULL) {
        out[0] = '\0';
        return -1;
    }
    fputs(header, f);
    fprintf(f, "asked for: %s\n", (why != NULL) ? why : "a report");
    fprintf(f, "lines it had written: %lu\n\n", tiku_trail_written());
    if (snapshot != NULL) {
        snapshot(f, ctx);
    }
    fprintf(f, "\nwhat it was doing:\n");
    (void)fflush(f);
    tiku_trail_write(fileno(f));
    (void)fclose(f);
    snprintf(latest, sizeof latest, "%s/latest-report.txt", folder_path);
    (void)unlink(latest);
    if (symlink(out, latest) != 0) {
        latest[0] = '\0';           /* the report stays, with no link */
    }
    return 0;
}


/** @brief The moment in a report's name, as a string that sorts. */
static void
report_stamp(const char *leaf, char *out, size_t max)
{
    const char *dash = strchr(leaf, '-');

    snprintf(out, max, "%.15s", (dash != NULL) ? dash + 1 : leaf);
}

int
tiku_report_list(char out[][512], int max)
{
    char folder[PATH_ROOM / 2];
    DIR *d;
    struct dirent *e;
    char when[64][16];
    int n = 0, i, j;

    if (max > 64) {
        max = 64;
    }
    tiku_report_folder(NULL, folder, sizeof folder);
    d = opendir(folder);
    if (d == NULL) {
        return 0;
    }
    while ((e = readdir(d)) != NULL) {
        char stamp[16];

        if (strncmp(e->d_name, "latest-", 7) == 0 ||
            strstr(e->d_name, ".txt") == NULL) {
            continue;
        }
        /* Newest first, by the time in the name. */
        report_stamp(e->d_name, stamp, sizeof stamp);
        for (i = 0; i < n && strcmp(when[i], stamp) >= 0; i++) {
        }
        if (i >= max) {
            continue;
        }
        if (n < max) {
            n++;
        }
        for (j = n - 1; j > i; j--) {
            memcpy(when[j], when[j - 1], sizeof when[0]);
            snprintf(out[j], 512, "%s", out[j - 1]);
        }
        memcpy(when[i], stamp, sizeof when[0]);
        snprintf(out[i], 512, "%s/%s", folder, e->d_name);
    }
    (void)closedir(d);
    return n;
}

int
tiku_report_newest(char *out, size_t max)
{
    char one[1][512];

    out[0] = '\0';
    if (tiku_report_list(one, 1) < 1) {
        return -1;
    }
    snprintf(out, max, "%s", one[0]);
    return 0;
}
