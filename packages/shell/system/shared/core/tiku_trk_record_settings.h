/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_record_settings.h - Deskbar, Fonts and folder defaults over staged
 * session records.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_RECORD_SETTINGS_H_
#define TIKU_TRK_RECORD_SETTINGS_H_
#include "tiku_trk_deskbar_prefs.h"
#include "tiku_trk_fontprefs.h"
#include "tiku_resource_registry.h"
/** @brief Owner-thread record settings and worker-owned persistence updates.
 */
typedef struct tiku_trk_record_settings tiku_trk_record_settings_t;
/** @brief Bind the existing models and session store; changed applies
 * committed values.
 */
tiku_trk_record_settings_t *tiku_trk_record_settings_open(tiku_store_t *, tiku_trk_deskbar_prefs_t *,
                              tiku_trk_fontprefs_t *, void (*changed)(void *),
                              void *);
/** @brief Submit GUI proposals; font selects the Fonts model as the source.
 */
int tiku_trk_record_settings_submit(tiku_trk_record_settings_t *, int font);
/** @brief Schedule bounded record reloads or retry cache reconciliation. */
void tiku_trk_record_settings_poll(tiku_trk_record_settings_t *,
                                   uint64_t now_ms);
/** @brief Collect accepted work with a five-second wait deadline, then detach
 * the owner.
 */
void tiku_trk_record_settings_close(tiku_trk_record_settings_t *);
/** @brief Pending or last failed save feedback; successful saves return empty
 * text.
 */
const char *tiku_trk_record_settings_status(const tiku_trk_record_settings_t *);
/** @brief Number of scalar, array and composite descriptors supplied to the
 * shared provider.
 */
size_t tiku_trk_record_settings_count(void);
/** @brief Fill independently owned initial snapshots; caller clears them. */
int tiku_trk_record_settings_initial(tiku_trk_record_settings_t *,
                                     tiku_resource_initial_t *);
/** @brief Bind the borrowed registry after the combined settings provider is
 * registered.
 */
void tiku_trk_record_settings_bind(tiku_trk_record_settings_t *,
                                   tiku_resource_registry_t *);
/** @brief Validate a typed request without live changes or filesystem I/O. */
tiku_resource_error_t tiku_trk_record_settings_validate(tiku_trk_record_settings_t *, const char *,
                                  const tiku_msg_t *, int);
/** @brief Copy one resource job for asynchronous persistence. */
tiku_resource_error_t tiku_trk_record_settings_start(tiku_trk_record_settings_t *, const char *,
                               const char *, const tiku_msg_t *, int);
#endif
