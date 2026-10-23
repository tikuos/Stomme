/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_config_outbox.h - configuration requests kept on disk before they
 * are sent, and what the device's receipts say became of them.
 *
 * One record per board, keyed by its hardware UID, in a private directory.
 * Nothing here writes to a device unless a call asks it to.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_CONFIG_OUTBOX_H
#define TIKU_CONFIG_OUTBOX_H
#include "tiku_config.h"

/** @brief What became of a kept request, as the device's receipts show. */
enum {
    TIKU_CONFIG_PENDING_NONE,      /**< no request is kept              */
    TIKU_CONFIG_PENDING_RETRY,     /**< unconfirmed, the setting still at
                                        its revision: it can be resent  */
    TIKU_CONFIG_PENDING_CONFIRMED, /**< a receipt matches the request   */
    TIKU_CONFIG_PENDING_CONFLICT,  /**< another request changed it      */
    TIKU_CONFIG_PENDING_OBSOLETE,  /**< the device's configuration store
                                        has a new identity              */
    TIKU_CONFIG_PENDING_UNKNOWN,   /**< the outcome cannot be told      */
    TIKU_CONFIG_PENDING_ERROR      /**< the record or the receipts could
                                        not be read                     */
};

/** @brief A request kept for one board, as read back and resolved. */
typedef struct {
    int state;                     /**< TIKU_CONFIG_PENDING_*, not stored */
    int enrollment;                /**< 1 for an enrollment request     */
    /** The board's hardware UID, the setting's path, the request as it is
     *  sent, and a sentence for the user. */
    char uid[33], path[64], request[160], summary[192];
} tiku_config_pending_t;

/** @brief The outbox directory: $TIKU_CONFIG_OUTBOX, else
 *         ~/.config/tracker/config-requests, without trailing slashes.
 *  @return 0, or -1 when neither is set or the path is not absolute. */
int tiku_config_outbox_dir(char *out, size_t max);

/**
 * @brief Read the board's kept request, if any, and resolve it against the
 *        device's receipts.  No file is made and nothing is written to the
 *        device.
 * @return 0 with @p out's state set (NONE when nothing is kept), or -1 with
 *         state ERROR.
 */
int tiku_config_outbox_inspect(const char *dir, tiku_backend_t *backend,
                              tiku_config_pending_t *out);

/**
 * @brief Keep a request to set @p path to @p value, synced to disk, then
 *        send it and resolve the outcome from the device's receipts.
 *
 * @note The record is keyed by the board's hardware UID, read live, and
 *       the directory is locked against writers in other processes; a
 *       board with a request already kept takes no other.
 * @return 0 when the device confirmed the request (its record is then
 *         removed), -1 otherwise with @p error filled.
 */
int tiku_config_outbox_save(const char *dir, tiku_backend_t *backend,
    const char *path, const tiku_config_snapshot_t *snapshot,
    const char *value, size_t length, char *error, size_t max);

/** @brief Keep and send an enrollment: a random token written to
 *         /sys/config/provision, under the same rules as a save.
 *  @return 0 when the device confirmed it, -1 otherwise with @p error
 *          filled. */
int tiku_config_outbox_enroll(const char *dir, tiku_backend_t *backend,
                             char *error, size_t max);

/**
 * @brief Resolve the kept request @p displayed again, and send it once more
 *        when it is RETRY.  A confirmed request's record is removed.
 * @return 0 when the device confirmed it, -1 otherwise with @p error
 *         filled, also when the kept record no longer matches @p displayed.
 */
int tiku_config_outbox_retry(const char *dir, tiku_backend_t *backend,
    const tiku_config_pending_t *displayed, char *error, size_t max);

/**
 * @brief Remove the kept record when it still matches @p displayed.  It
 *        cancels and undoes nothing on the device.
 * @note The caller says so before asking the user to confirm.
 * @return 0 when the record was removed, -1 otherwise with @p error filled.
 */
int tiku_config_outbox_forget(const char *dir, tiku_backend_t *backend,
    const tiku_config_pending_t *displayed, char *error, size_t max);
#endif
