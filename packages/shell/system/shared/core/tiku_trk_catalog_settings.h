/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_catalog_settings.h - loaded file types and staged preferred
 * handlers.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_CATALOG_SETTINGS_H_
#define TIKU_TRK_CATALOG_SETTINGS_H_
#include "tiku_trk_associations.h"
#include "tiku_resource_registry.h"
/** @brief Owner-thread catalog with isolated session-store workers. */
typedef struct tiku_trk_catalog_settings tiku_trk_catalog_settings_t;
/** @brief Bind loaded application/type catalogs and the borrowed session
 * store.
 */
tiku_trk_catalog_settings_t *tiku_trk_catalog_settings_open(tiku_store_t *, const tiku_trk_apps_t *,
                               const tiku_trk_types_t *,
                               void (*changed)(void *), void *);
/** @brief Bind startup catalogs with editable definitions from the native
 * type file.
 */
tiku_trk_catalog_settings_t *tiku_trk_catalog_settings_open_with_file(
    tiku_store_t *, const tiku_trk_apps_t *, const tiku_trk_types_t *,
    const char *, void (*changed)(void *), void *);
/** @brief Number of collection and child descriptors supplied at
 * registration.
 */
size_t tiku_trk_catalog_settings_count(const tiku_trk_catalog_settings_t *);
/** @brief Fill owned snapshots for registration; caller clears their values.
 */
int tiku_trk_catalog_settings_initial(tiku_trk_catalog_settings_t *,
                                      tiku_resource_initial_t *);
/** @brief Bind the borrowed combined settings registry after registration. */
void tiku_trk_catalog_settings_bind(tiku_trk_catalog_settings_t *,
                                    tiku_resource_registry_t *);
/** @brief Validate a preferred-handler request without changing live state.
 */
tiku_resource_error_t tiku_trk_catalog_settings_validate(tiku_trk_catalog_settings_t *, const char *,
                                   const tiku_msg_t *, int);
/** @brief Copy one resource proposal and wait for the session-store writer.
 */
tiku_resource_error_t tiku_trk_catalog_settings_start(tiku_trk_catalog_settings_t *, const char *,
                                const char *, const tiku_msg_t *, int);
/** @brief Submit a GUI draft; completion runs on the owner thread after persistence.
 *  @note A changed row's saved value is its conflict precondition. */
int tiku_trk_catalog_settings_submit(
    tiku_trk_catalog_settings_t *, const tiku_trk_associations_t *,
    void (*completed)(void *, tiku_resource_error_t), void *);
/** @brief Validate an installed handler or empty Automatic for each selected
 * catalog type.
 */
tiku_resource_error_t tiku_trk_catalog_settings_prefer_validate(tiku_trk_catalog_settings_t *,
                                          const char *const *types, int count,
                                          const char *handler);
/** @brief Save one handler or Automatic for the selected types in one staged store update.
 *  @note Completion runs on the owner thread after persistence. */
tiku_resource_error_t tiku_trk_catalog_settings_prefer(
    tiku_trk_catalog_settings_t *, const char *const *types, int count,
    const char *handler, void (*completed)(void *, tiku_resource_error_t),
    void *context);
/** @brief Detach a matching GUI completion; accepted persistence continues.
 */
void tiku_trk_catalog_settings_detach(tiku_trk_catalog_settings_t *,
                                      void *context);
/** @brief Observe committed cache values, retry waiting work and publish
 * changes.
 */
void tiku_trk_catalog_settings_poll(tiku_trk_catalog_settings_t *);
/** @brief Drain work for five seconds, then detach before the store is
 * destroyed.
 */
void tiku_trk_catalog_settings_close(tiku_trk_catalog_settings_t *);
/** @brief Encode MIME bytes as a stable lowercase hexadecimal path component.
 */
int tiku_trk_catalog_settings_path(const char *collection, const char *mime,
    const char *leaf, char out[TIKU_RESOURCE_PATH_MAX]);
#endif
