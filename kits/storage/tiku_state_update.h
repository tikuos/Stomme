/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_state_update.h - staged sidecar updates and owner-thread cache reconciliation.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_STATE_UPDATE_H_
#define TIKU_STATE_UPDATE_H_
#include "tiku_state.h"
/** @brief Copied cache, persistence baseline and one isolated worker result. */
typedef struct tiku_store_update tiku_store_update_t;
/** @brief Staged persistence results; uncertain means a replacement may have occurred. */
typedef enum {
    TIKU_STORE_UPDATE_OK, TIKU_STORE_UPDATE_FAILED, TIKU_STORE_UPDATE_CONFLICT,
    TIKU_STORE_UPDATE_CANCELLED, TIKU_STORE_UPDATE_UNCERTAIN, TIKU_STORE_UPDATE_NO_MEMORY
} tiku_store_update_result_t;
/** @brief Begin one update on the store owner; xattr stores are unsupported.
 *  @note Cached writes remain available; a concurrent flush returns EBUSY.
 *        errno reports EBUSY, ENOTSUP, EACCES or ENOMEM on NULL. */
tiku_store_update_t *tiku_store_update_begin(tiku_store_t *);
/** @brief The copied editable store, owner-thread access before run only. */
tiku_store_t *tiku_store_update_model(tiku_store_update_t *);
/** @brief Persist copied inputs on a worker; call once, without live store access.
 *  @note Sidecars retain unknown records and reject conflicting external edits.
 *        Byte/record limits are 4 MiB and 4096. Memory stores remain volatile. */
void tiku_store_update_run(tiku_store_update_t *);
/** @brief Install a completed result on the owner thread, preserving later cached edits.
 *  @note NO_MEMORY retains the operation for another finish attempt. */
tiku_store_update_result_t tiku_store_update_finish(tiku_store_update_t *);
/** @brief Request worker cancellation; a completed replacement cannot be undone. */
void tiku_store_update_cancel(tiku_store_update_t *);
/** @brief Detach on the owner thread before destroying the source store.
 *  @note Legacy flush waits for a successful staged reconciliation afterward. */
void tiku_store_update_detach(tiku_store_update_t *);
/** @brief Release after run has returned, or before run has started. */
void tiku_store_update_free(tiku_store_update_t *);
/** @brief Read a sidecar's last known saved record without filesystem I/O.
 *  @note Memory stores read their volatile cache. errno is ENOENT for absence,
 *        EAGAIN without a saved snapshot, ENOTSUP for xattrs, EINVAL for NULL.
 *  @return Bytes copied within max, or -1. Owner-thread access only. */
int tiku_store_persisted_read(tiku_store_t *, const char *node, const char *attr,
    void *buffer, size_t max);
/** @brief Whether the store has a writable sidecar rather than volatile records. */
int tiku_store_persistent(const tiku_store_t *);
#endif
