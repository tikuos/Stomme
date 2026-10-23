/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_sys_pinned.h - the table of pinned processes, by handle.
 *
 * tiku_sys_darwin.c keeps a row for each handle tiku_sys_process_pin()
 * issued.  The caller frees a handle by closing it, so a row whose handle
 * is closed, or whose number a new handle now carries, is stale.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_SYS_PINNED_H_
#define TIKU_SYS_PINNED_H_

#include <errno.h>
#include <fcntl.h>

#define TIKU_SYS_PINNED_MAX 64

typedef struct {
    int       handle;           /* the descriptor issued; 0 when free */
    long      pid;              /* the process pinned */
    long long started;          /* when it began, from the process itself */
} tiku_sys_pinned_t;

/**
 * @brief Free each row of @p table whose handle is closed or equals
 *        @p handle, a descriptor just opened.
 */
static inline void
tiku_sys_pinned_sweep(tiku_sys_pinned_t *table, int handle)
{
    int i;

    for (i = 0; i < TIKU_SYS_PINNED_MAX; i++) {
        if (table[i].handle > 0 && (table[i].handle == handle ||
            (fcntl(table[i].handle, F_GETFD) < 0 && errno == EBADF))) {
            table[i].handle = 0;
        }
    }
}

/**
 * @brief Record @p handle, just opened, as pinning @p pid, which began at
 *        @p started.
 * @return 0, or -1 with errno ENOMEM when every row holds an open handle.
 */
static inline int
tiku_sys_pinned_add(tiku_sys_pinned_t *table, int handle, long pid,
                    long long started)
{
    int i;

    tiku_sys_pinned_sweep(table, handle);
    for (i = 0; i < TIKU_SYS_PINNED_MAX && table[i].handle > 0; i++) {
        ;
    }
    if (i == TIKU_SYS_PINNED_MAX) {
        errno = ENOMEM;
        return -1;
    }
    table[i].handle = handle;
    table[i].pid = pid;
    table[i].started = started;
    return 0;
}

/** @brief The row of @p table that issued @p handle, or NULL. */
static inline tiku_sys_pinned_t *
tiku_sys_pinned_find(tiku_sys_pinned_t *table, int handle)
{
    int i;

    for (i = 0; i < TIKU_SYS_PINNED_MAX; i++) {
        if (table[i].handle == handle && handle > 0) {
            return &table[i];
        }
    }
    return NULL;
}

#endif /* TIKU_SYS_PINNED_H_ */
