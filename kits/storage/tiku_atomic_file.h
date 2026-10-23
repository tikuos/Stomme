/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_atomic_file.h - a local file replaced whole, or left as it was.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_ATOMIC_FILE_H_
#define TIKU_ATOMIC_FILE_H_
#include <stdio.h>

/**
 * @brief Replace @p path with what @p produce writes, whole.
 *
 * The content goes to a private temporary file beside @p path, which is
 * synced and renamed over it; then the directory is synced.  An existing
 * @p path keeps its mode, and a new one gets 0666 under the caller's umask.
 *
 * @note Call it from a worker: producing, syncing and replacing can block.
 *       An existing @p path must be a regular file, not a symbolic link.
 * @return 0, or -1.  When @p produce or a write fails, the destination is
 *         untouched; a failed directory sync after the rename is -1 too.
 */
int tiku_atomic_file(const char *path, int (*produce)(FILE *, void *),
                     void *ctx);

/** @brief Outcome of guarded replacement; UNCERTAIN means rename succeeded. */
typedef enum {
    TIKU_ATOMIC_OK, TIKU_ATOMIC_FAILED, TIKU_ATOMIC_CONFLICT,
    TIKU_ATOMIC_UNCERTAIN
} tiku_atomic_result_t;

/** @brief Write a private temporary file and check permission to replace.
 *  @note check(ctx) must return zero before creation and immediately before
 *        rename. It can compare a recorded file version. This is cooperative
 *        conflict detection, not a filesystem compare-and-swap. New files
 *        remain mode 0600; existing regular files retain their mode. No
 *        process-global umask is changed. Call from a worker.
 *  @return FAILED leaves the destination unchanged, CONFLICT means the
 *          check refused, UNCERTAIN means directory sync failed afterward. */
tiku_atomic_result_t tiku_atomic_file_guarded(const char *path,
    int (*produce)(FILE *, void *), int (*check)(void *), void *ctx);
#endif
