/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_type_settings.h - typed native file definitions and worker saves.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_TYPE_SETTINGS_H_
#define TIKU_TRK_TYPE_SETTINGS_H_
#include "tiku_trk_types.h"
#include "tiku_resource_registry.h"
/** @brief Owner-thread definitions with a startup applied snapshot. */
typedef struct tiku_trk_type_settings tiku_trk_type_settings_t;
/** @brief Copy startup definitions and schedule a bounded asynchronous file read. */
tiku_trk_type_settings_t *tiku_trk_type_settings_open(const char *, const tiku_trk_types_t *);
/** @brief Number of child descriptors; the catalog supplies the collection root. */
size_t tiku_trk_type_settings_count(const tiku_trk_type_settings_t *);
/** @brief Fill owned initial values; caller clears them after registration. */
int tiku_trk_type_settings_initial(tiku_trk_type_settings_t *, tiku_resource_initial_t *);
/** @brief Bind the borrowed combined settings registry. */
void tiku_trk_type_settings_bind(tiku_trk_type_settings_t *, tiku_resource_registry_t *);
/** @brief Validate a complete record, empty deletion object or batch action. */
tiku_resource_error_t tiku_trk_type_settings_validate(tiku_trk_type_settings_t *, const char *, const tiku_msg_t *, int);
/** @brief Copy a proposal for one guarded native-file replacement. */
tiku_resource_error_t tiku_trk_type_settings_start(tiku_trk_type_settings_t *, const char *, const char *, const tiku_msg_t *, int);
/** @brief Publish completions and schedule external-file reads every 500 ms. */
void tiku_trk_type_settings_poll(tiku_trk_type_settings_t *);
/** @brief Drain work for five seconds, then cancel and detach remaining work. */
void tiku_trk_type_settings_close(tiku_trk_type_settings_t *);
#endif
