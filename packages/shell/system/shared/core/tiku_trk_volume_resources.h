/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_volume_resources.h - cached volume identities and native requests.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_VOLUME_RESOURCES_H_
#define TIKU_TRK_VOLUME_RESOURCES_H_
#include "tiku_resource_registry.h"
#include "tiku_volume.h"
/** @brief Owner-thread adapter over the native cached mount inventory. */
typedef struct tiku_trk_volume_resources tiku_trk_volume_resources_t;
/** @brief Native request returns queue acceptance and copies refusal text. */
typedef int (*tiku_trk_volume_request_fn)(void *, const tiku_volume_t *,
                                          char *, size_t);
/** @brief Register current volumes; request and context outlive the adapter.
 */
tiku_trk_volume_resources_t *tiku_trk_volume_resources_open(tiku_resource_registry_t *,
                               tiku_trk_volume_request_fn, void *);
/** @brief Reconcile native cached records; no mount scan is performed. */
void tiku_trk_volume_resources_poll(tiku_trk_volume_resources_t *,uint64_t now);
/** @brief Retire volume incarnations before destroying native owners. */
void tiku_trk_volume_resources_close(tiku_trk_volume_resources_t *);
#endif
