/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_trk_resource_discovery.h - desktop identity and control-service observations.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_RESOURCE_DISCOVERY_H_
#define TIKU_TRK_RESOURCE_DISCOVERY_H_
#include "tiku_resource_registry.h"
/** @brief Owner-thread discovery providers with borrowed registry lifetime. */
typedef struct tiku_trk_resource_discovery tiku_trk_resource_discovery_t;
/** @brief Register typed identity, capabilities, event policy and schema resources. */
tiku_trk_resource_discovery_t *tiku_trk_resource_discovery_open(
    tiku_resource_registry_t *, const char *product, const char *version);
/** @brief Refresh registry and event observations while retaining complete snapshots. */
void tiku_trk_resource_discovery_poll(tiku_trk_resource_discovery_t *,
                                      uint64_t now_ms);
/** @brief Retire discovery providers before destroying the registry. */
void tiku_trk_resource_discovery_close(tiku_trk_resource_discovery_t *);
#endif
