/*
 * KintsugiOS v0.01 kits -- the foundation KintsugiDesktop and KintsugiTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@kinstugi-os.org>
 *
 * tiku_sys.h - what only the machine underneath can answer.
 *
 * Calls whose answer POSIX leaves to the host, declared once:
 * tiku_sys_linux.c and tiku_sys_darwin.c define them, and mk/kits.mk
 * compiles the one for the host.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SYS_H
#define TIKU_SYS_H

#include <stddef.h>

struct stat;
/** @brief Compare device, inode, size, mode and nanosecond mtime/ctime.
 *  @note Access time is excluded because reading a file can change it.
 *        NULL arguments compare unequal. This does not compare contents. */
int tiku_sys_stat_same(const struct stat *a, const struct stat *b);

/**
 * @brief A UTF-8 locale of the caller's own, for reading characters and
 *        ordering them, or NULL when the host has none; the process's
 *        and the thread's locales are left as they are.
 *
 * C.UTF-8 is taken where the host has it, ordering characters by code
 * point, and en_US.UTF-8 where it does not; with en_US.UTF-8 a bracket
 * range in a regular expression may follow its order of letters.
 */
void *tiku_sys_locale_utf8(void);
/** @brief Make @p locale the calling thread's, or with NULL only ask, and
 *         return the thread's locale before. */
void *tiku_sys_locale_use(void *locale);
/** @brief Free a locale tiku_sys_locale_utf8() made; NULL is ignored.
 *  @note Put the thread's locale back first; a locale
 *        tiku_sys_locale_use() returned is not the caller's to free. */
void tiku_sys_locale_free(void *locale);

/**
 * @brief Flush an open file to the drive, through its write cache:
 *        fsync() on Linux, fsync() and F_FULLFSYNC on Darwin.
 * @return 0, or -1 when the flush failed.
 * @note This flushes the file alone: after a file is created, renamed or
 *       removed, sync its parent directory too.
 */
int tiku_sys_sync_file(int fd);

/**
 * @brief The instruction that faulted: the program counter in a signal
 *        handler's context.
 *
 * @param context  The third argument an SA_SIGINFO handler was given.
 * @return Where the program was, or NULL for a NULL context or an
 *         architecture this host's file does not read.
 */
void *tiku_sys_fault_pc(const void *context);

/** @brief One running program, as tiku_sys_processes() offers it. */
typedef struct {
    long        pid;
    const char *name;       /**< the short name a person would know it by */
    const char *command;    /**< the image it was started from */
} tiku_sys_process_t;

/**
 * @brief Offer to @p fn, with @p ctx, each process this user owns that
 *        has an executable image and a name, which leaves out kernel
 *        threads.
 *
 * The strings in each process last only for the call to @p fn.
 *
 * @return How many were offered, or -1 where the host will not say.
 */
int tiku_sys_processes(void (*fn)(const tiku_sys_process_t *process,
                                  void *ctx), void *ctx);

/**
 * @brief Total and available memory, in kilobytes.
 *
 * Available memory counts what the system would reclaim under pressure,
 * as Linux's MemAvailable does.  Both are left at zero where the host
 * will not say.
 */
void tiku_sys_memory(long *total_kb, long *avail_kb);

/** @brief Seconds since the machine came up, or 0 where it will not say. */
long tiku_sys_uptime(void);

/**
 * @brief Put the path of this program's own executable, symbolic links
 *        resolved, in @p out.
 * @return 0 when @p out holds a path; -1, with @p out empty, otherwise.
 */
int tiku_sys_self_path(char *out, size_t max);

/**
 * @brief A backtrace frame as the report wrote it, made readable.
 *
 * @p raw is a line backtrace_symbols_fd() wrote.  On Linux an offset
 * into KintsugiDesktop is named by running addr2line on the running
 * program's image.  @p out is always written, falling back to @p raw
 * itself.
 */
void tiku_sys_frame_name(const char *raw, char *out, size_t max);

/**
 * @brief Pin the process on the other end of the local socket @p fd.
 *
 * A kill through the handle reaches the pinned process or none, even
 * after the system gives its pid to another process.  Only a process of
 * this user's is pinned, never init and never the caller; any other gets
 * EPERM.
 *
 * @param pid  filled with the peer's process id when it is pinned.
 * @return A descriptor for tiku_sys_process_kill(), close-on-exec and
 *         the caller's to close; -1 with errno set otherwise, ENOTSUP
 *         where the host cannot pin at all.
 */
int tiku_sys_process_pin(int fd, long *pid);

/**
 * @brief Send SIGKILL to the process @p handle pinned.
 *
 * @return 0, or -1 with errno set: ESRCH when the pinned process has
 *         gone, and on Darwin for a handle it did not issue.
 */
int tiku_sys_process_kill(int handle);

/**
 * @brief Make sure every byte of @p fd up to @p bytes has storage, so a
 *        write through a shared mapping of it cannot raise SIGBUS for
 *        want of room.
 *
 * Linux calls posix_fallocate().  Darwin does nothing: a shared memory
 * object there has its pages once it is sized.
 *
 * @return 0 when the room is there, an errno value when it is not.
 */
int tiku_sys_reserve(int fd, long long bytes);

/**
 * @brief Run an open serial line at exactly @p baud, when termios could
 *        not set it.
 *
 * Linux has a termios constant for every rate the tree uses, so there
 * this does nothing and returns -1.  Darwin's termios names no rate
 * above 230400, and this sets the rate through the driver.
 *
 * @param fd    An open terminal, already configured.
 * @param baud  The rate in bits per second.
 * @return 0 when the line is running at it, -1 when this host cannot.
 * @note Call it after the last tcsetattr(): on Darwin a later one puts
 *       the termios rate back.
 */
int tiku_sys_serial_speed(int fd, int baud);

/**
 * @brief The serial ports a board could be on the other end of: on
 *        Linux the /dev/serial/by-id names of the probes and boards the
 *        tree knows, on Darwin the /dev/cu.* nodes of USB serial drivers.
 *
 * No port is opened here; opening one whose device does not answer can
 * block for a long time.
 *
 * @param out  Filled with up to @p max paths.
 * @param max  How many will fit.
 * @return how many were written, or -1 when this host has no such list.
 */
int tiku_sys_serial_ports(char out[][256], int max);

/** @brief Native executable for dispatching a validated URI. */
const char *tiku_sys_uri_launcher(void);

#endif /* TIKU_SYS_H */
