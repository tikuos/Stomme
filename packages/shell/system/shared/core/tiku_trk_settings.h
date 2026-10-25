/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_settings.h - shared preference ownership and resource publication.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_SETTINGS_H_
#define TIKU_TRK_SETTINGS_H_
#include "tiku_trk_prefs.h"
#include "tiku_resource_registry.h"
#include "tiku_trk_record_settings.h"
#include "tiku_trk_catalog_settings.h"
#include "tiku_trk_desktop_settings.h"

/** @brief Owner-thread preference controller with isolated persistence workers. */
typedef struct tiku_trk_settings tiku_trk_settings_t;
/** @brief Bind the existing model and file; registry may be NULL.
 *  @note changed runs on the owner thread after a saved or reloaded model applies. */
tiku_trk_settings_t *tiku_trk_settings_open(tiku_trk_prefs_t *model,
    const char *path, tiku_resource_registry_t *registry,
    void (*changed)(void *), void *context);
/** @brief Bind table and record owners under one settings provider. */
tiku_trk_settings_t *tiku_trk_settings_open_with_records(tiku_trk_prefs_t *,
    const char *, tiku_resource_registry_t *, tiku_trk_record_settings_t *,
    void (*changed)(void *), void *context);
/** @brief Bind native table, record and catalog owners under one settings provider. */
tiku_trk_settings_t *tiku_trk_settings_open_with_catalog(tiku_trk_prefs_t *,
    const char *, tiku_resource_registry_t *, tiku_trk_record_settings_t *,
    tiku_trk_catalog_settings_t *, void (*changed)(void *), void *context);
/** @brief Bind table, session, catalog and native Desktop preference owners. */
tiku_trk_settings_t *tiku_trk_settings_open_with_desktop(tiku_trk_prefs_t *,
    const char *, tiku_resource_registry_t *, tiku_trk_record_settings_t *,
    tiku_trk_catalog_settings_t *, tiku_trk_desktop_settings_t *, void (*changed)(void *), void *);
/** @brief Schedule a bounded content reload, without file I/O on the owner thread. */
void tiku_trk_settings_poll(tiku_trk_settings_t *, uint64_t monotonic_ms);
/** @brief Detach worker completions and unregister before the model is freed. */
void tiku_trk_settings_close(tiku_trk_settings_t *);
/** @brief Last completed persistence/reload error, or OK. */
tiku_resource_error_t tiku_trk_settings_error(const tiku_trk_settings_t *);
/** @brief Number of scalar and profile entries managed by the controller. */
size_t tiku_trk_settings_count(const tiku_trk_settings_t *);
#endif
