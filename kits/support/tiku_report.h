/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_report.h - what a program was doing when it was killed.
 *
 * A fault leaves a file naming the signal, where it happened, and the
 * trail up to it; then the program dies by the signal's default action,
 * which dumps a core where cores are enabled.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_REPORT_H_
#define TIKU_REPORT_H_

#include <stddef.h>
#include <stdio.h>

/**
 * @brief Take the fatal signals, make the reports folder, and set the
 *        paths this process's reports are written to.
 *
 * @param program the program's name, in the report and in its file name.
 * @param dir     the folder for reports, or NULL for the one
 *                tiku_report_folder() names.
 * @note A second call in the same process does nothing.
 */
void tiku_report_install(const char *program, const char *dir);

/** @brief Where the report of a fault would be written; "" before
 *         tiku_report_install(). */
const char *tiku_report_path(void);

/**
 * @brief The path of latest-crash.txt in the reports folder, which every
 *        program's fault report and tiku_report_now() write as well, so
 *        it holds the newest of them whichever program wrote it.
 */
const char *tiku_report_latest(void);

/**
 * @brief Write a report of a turn that has not ended, to
 *        tiku_report_stall_path() and latest-stall.txt, and return.
 *
 * Does nothing before tiku_report_install().
 *
 * @param ms         how long it has been quiet.
 * @param with_stack nonzero to add a backtrace of the calling thread;
 *                   pass it only from the stuck thread itself.
 */
void tiku_report_stall(long ms, int with_stack);

/**
 * @brief Put the reports folder in @p out: @p dir, or $TIKU_TRK_REPORTS
 *        when @p dir is NULL; when that is empty or unset,
 *        $HOME/.config/tracker/reports, with /tmp for an unset $HOME.
 */
void tiku_report_folder(const char *dir, char *out, size_t max);

/** @brief Where a report of a stall would be written. */
const char *tiku_report_stall_path(void);

/*
 * Reports on demand: the SIGUSR2 handler sets a flag, and the program's
 * loop reads it with tiku_report_asked() and writes the report with
 * tiku_report_write().
 */

/** @brief Arm SIGUSR2 to ask for a report. */
void tiku_report_ask_install(void);

/** @brief Whether SIGUSR2 arrived since the last call; clears the flag. */
int tiku_report_asked(void);

/**
 * @brief Write report-<time>-<program>-<pid>.txt: the header, @p why,
 *        what @p snapshot prints with @p ctx, and the trail; then point
 *        latest-report.txt at it.
 *
 * @param out where it was written, or "" when it could not be.
 * @return 0 when written; -1 when it could not be, or before
 *         tiku_report_install().
 * @note Call from the program's loop, not from a signal handler.
 */
int tiku_report_write(const char *why, void (*snapshot)(FILE *, void *),
                      void *ctx, char *out, size_t max);

/**
 * @brief Put the path of the newest report of any kind in @p out, or ""
 *        when there is none, as tiku_report_list() finds them.
 * @return 0, or -1 when there is none.
 */
int tiku_report_newest(char *out, size_t max);

/**
 * @brief Every report in the folder, newest first by the time in its
 *        name, as full paths; at most @p max and at most 64.
 * @return how many were written to @p out.
 * @note Reads the folder tiku_report_folder() names for NULL, not one
 *       given to tiku_report_install().
 */
int tiku_report_list(char out[][512], int max);

/**
 * @brief Write a report now, with a backtrace, to the files a fault would
 *        write, and return; @p why is written as what stopped it.
 *
 * Does nothing before tiku_report_install().
 */
void tiku_report_now(const char *why);

/**
 * @brief Write exit-<time>-<pid>.txt for child @p pid, running
 *        @p program, which died by @p sig, or exited with @p status when
 *        @p sig is 0: what the parent saw, the paths of the child's crash
 *        reports in the folder, and the parent's trail.
 *
 * latest-application-exit.txt is pointed at it; latest-crash.txt is left
 * alone.  It does not need tiku_report_install().
 *
 * @param out the file written, or "" when it could not be.
 * @return 0 when written, else -1.
 * @note Call it only for exits the caller did not expect.
 */
int tiku_report_child(const char *program, long pid, int sig, int status,
                      char *out, size_t max);

#endif /* TIKU_REPORT_H_ */
