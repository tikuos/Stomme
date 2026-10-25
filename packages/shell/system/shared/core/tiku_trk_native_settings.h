/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_native_settings.h - native record families under the session namespace.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_NATIVE_SETTINGS_H_
#define TIKU_TRK_NATIVE_SETTINGS_H_
#include "tiku_trk_record_settings.h"
/** @brief Native record families; aliases retain their existing settings owner. */
typedef enum {
    TIKU_TRK_NATIVE_COLUMNS, TIKU_TRK_NATIVE_DEFAULTS, TIKU_TRK_NATIVE_DESK_COLUMNS,
    TIKU_TRK_NATIVE_DESK_VIEW, TIKU_TRK_NATIVE_DESKBAR_PREFS, TIKU_TRK_NATIVE_DESKBAR_FRAME,
    TIKU_TRK_NATIVE_DISKS_COLUMNS, TIKU_TRK_NATIVE_DISKS_VIEW, TIKU_TRK_NATIVE_DOCK,
    TIKU_TRK_NATIVE_FRAME, TIKU_TRK_NATIVE_ORIGINAL, TIKU_TRK_NATIVE_POSE,
    TIKU_TRK_NATIVE_REPLICANTS, TIKU_TRK_NATIVE_SESSION, TIKU_TRK_NATIVE_FONT,
    TIKU_TRK_NATIVE_VIEW, TIKU_TRK_NATIVE_COUNT
} tiku_trk_native_family_t;
/** @brief Catalog, copied persistence work and typed native record snapshots. */
typedef struct tiku_trk_native_settings tiku_trk_native_settings_t;
/** @brief Register record families and existing settings aliases on the owner thread. */
tiku_trk_native_settings_t *tiku_trk_native_settings_open(tiku_resource_registry_t *, tiku_trk_record_settings_t *);
/** @brief Apply committed records through existing live owners; callback must not block. */
void tiku_trk_native_settings_bind_saved(tiku_trk_native_settings_t *,
    void (*saved)(void *, tiku_trk_native_family_t, const char *node, const tiku_msg_t *), void *);
/** @brief Bind capture of the current folder list for an explicit saved-session action. */
void tiku_trk_native_settings_bind_capture(tiku_trk_native_settings_t *, tiku_msg_t *(*capture)(void *), void *);
/** @brief Add one known native node; preserve existing identities and report capacity refusal.
 *  @note The store must outlive the owner. No filesystem access occurs here. */
int tiku_trk_native_settings_add(tiku_trk_native_settings_t *, tiku_trk_native_family_t, tiku_store_t *, const char *node);
/** @brief Reload native attributes on copied workers and inspect cached sidecar records. */
void tiku_trk_native_settings_poll(tiku_trk_native_settings_t *, uint64_t now_ms);
/** @brief Drain accepted work for five seconds, then unregister and detach copied work. */
void tiku_trk_native_settings_close(tiku_trk_native_settings_t *);
#endif
