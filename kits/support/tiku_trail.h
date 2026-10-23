/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trail.h - what this program was doing, kept in a ring.
 *
 * The last TIKU_TRAIL_LINES lines a program wrote, in a fixed ring in
 * memory; the crash and stall reports print them.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_TRAIL_H_
#define TIKU_TRAIL_H_

#include <stddef.h>

/* The lines the ring holds, and the bytes of each with its NUL; a longer
 * line is cut. */
#define TIKU_TRAIL_LINES 512
#define TIKU_TRAIL_LINE  112

/**
 * @brief Format one line into the ring, as printf() would, after the
 *        milliseconds since the first line.
 *
 * The line is formatted here; writing the ring out needs only write().
 *
 * @note The ring takes no lock: lines written from two threads at once
 *       can be lost or mixed.
 */
void tiku_trail(const char *fmt, ...);

/**
 * @brief The most recent lines that fit in @p out, oldest of them first.
 * @return the bytes written.
 */
int tiku_trail_read(char *out, size_t max);

/**
 * @brief As tiku_trail_read(), but only the lines holding @p needle;
 *        NULL or "" takes every line.
 * @return the bytes written.
 */
int tiku_trail_find(const char *needle, char *out, size_t max);

/**
 * @brief Every line still in the ring, to @p fd, oldest first.
 *
 * Async-signal-safe: it formats nothing and allocates nothing.
 */
void tiku_trail_write(int fd);

/**
 * @brief The newest line alone, to @p fd; nothing before the first line.
 *
 * Async-signal-safe.
 */
void tiku_trail_write_last(int fd);

/** @brief How many lines have been written since the program began. */
unsigned long tiku_trail_written(void);

#endif /* TIKU_TRAIL_H_ */
