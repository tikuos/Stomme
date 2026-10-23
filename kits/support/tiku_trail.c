/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trail.c - what this program was doing, kept in a ring.
 *
 * One ring per process, a static table of lines, which the crash and
 * stall reports print.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_trail.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/*---------------------------------------------------------------------------*/
/* THE RING                                                                  */
/*---------------------------------------------------------------------------*/

static char          ring[TIKU_TRAIL_LINES][TIKU_TRAIL_LINE];
static unsigned long written;            /* lines ever written */
static long          began_ms;

/** @brief Milliseconds since the first line, which starts the clock. */
static long
since(void)
{
    struct timespec t;
    long now;

    (void)clock_gettime(CLOCK_MONOTONIC, &t);
    now = t.tv_sec * 1000L + t.tv_nsec / 1000000L;
    if (began_ms == 0L) {
        began_ms = now;
    }
    return now - began_ms;
}

void
tiku_trail(const char *fmt, ...)
{
    char *slot = ring[written % TIKU_TRAIL_LINES];
    va_list ap;
    int n;

    n = snprintf(slot, TIKU_TRAIL_LINE, "%7ld ", since());
    if (n < 0 || (size_t)n >= TIKU_TRAIL_LINE) {
        return;
    }
    va_start(ap, fmt);
    (void)vsnprintf(slot + n, TIKU_TRAIL_LINE - (size_t)n, fmt, ap);
    va_end(ap);
    written++;
}

/** @brief The number of the oldest line still held; its slot is that
 *         number modulo TIKU_TRAIL_LINES. */
static unsigned long
oldest(void)
{
    return (written > TIKU_TRAIL_LINES)
               ? written - TIKU_TRAIL_LINES : 0uL;
}

/** @brief Whether @p line holds @p needle; a NULL or empty @p needle
 *         takes every line. */
static int
wanted(const char *line, const char *needle)
{
    return (needle == NULL || needle[0] == '\0' ||
            strstr(line, needle) != NULL);
}

int
tiku_trail_find(const char *needle, char *out, size_t max)
{
    unsigned long i, first = oldest();
    size_t used = 0u;

    if (out == NULL || max == 0u) {
        return 0;
    }
    out[0] = '\0';
    /* Count back from the newest line until @p out is full, so that the
     * newest lines are the ones kept. */
    for (i = written; i > first; i--) {
        const char *line = ring[(i - 1uL) % TIKU_TRAIL_LINES];

        if (!wanted(line, needle)) {
            continue;
        }
        /* The newline, and room for the NUL that ends the whole. */
        if (used + strlen(line) + 2u > max) {
            first = i;
            break;
        }
        used += strlen(line) + 1u;
    }
    used = 0u;
    for (i = first; i < written; i++) {
        const char *line = ring[i % TIKU_TRAIL_LINES];
        size_t n = strlen(line);

        if (!wanted(line, needle)) {
            continue;
        }
        if (used + n + 2u > max) {
            break;
        }
        memcpy(out + used, line, n);
        used += n;
        out[used++] = '\n';
    }
    out[used] = '\0';
    return (int)used;
}

int
tiku_trail_read(char *out, size_t max)
{
    return tiku_trail_find(NULL, out, max);
}

void
tiku_trail_write(int fd)
{
    unsigned long i;

    for (i = oldest(); i < written; i++) {
        const char *line = ring[i % TIKU_TRAIL_LINES];

        (void)!write(fd, line, strlen(line));
        (void)!write(fd, "\n", 1u);
    }
}

void
tiku_trail_write_last(int fd)
{
    const char *line;

    if (written == 0uL) {
        return;
    }
    line = ring[(written - 1uL) % TIKU_TRAIL_LINES];
    (void)!write(fd, line, strlen(line));
    (void)!write(fd, "\n", 1u);
}

unsigned long
tiku_trail_written(void)
{
    return written;
}
