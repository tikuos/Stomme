/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_app_resources.h - installed catalog observations and launch requests.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_APP_RESOURCES_H_
#define TIKU_TRK_APP_RESOURCES_H_
#include "tiku_trk_launch.h"
#include "tiku_resource_jobs.h"
/** @brief Installed app catalog and copied observation streams. */
typedef struct tiku_trk_app_resources tiku_trk_app_resources_t;
/** @brief Register loaded manifest IDs; callbacks and catalog outlive this owner.
 *  @note table is manifests, capabilities, types or associations; index is a row.
 *        count returns the complete row count, capture returns an owned object.
 *        run dispatches a native launch or one absolute document path. */
tiku_trk_app_resources_t *tiku_trk_app_resources_open(tiku_resource_registry_t *,const tiku_trk_apps_t *,
    int (*count)(void *,const char *),tiku_msg_t *(*capture)(void *,const char *,int),
    tiku_resource_error_t (*run)(void *,const tiku_trk_app_t *,const char *,tiku_msg_t **),void *);
/** @brief Refresh complete streams while retaining unchanged content identities. */
void tiku_trk_app_resources_poll(tiku_trk_app_resources_t *,uint64_t now_ms);
/** @brief Retire catalog requests before releasing the loaded catalog. */
void tiku_trk_app_resources_close(tiku_trk_app_resources_t *);
#endif
