/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_resource_session.h - desktop-owned resource namespace and endpoint.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_RESOURCE_SESSION_H_
#define TIKU_TRK_RESOURCE_SESSION_H_
#include "tiku_model.h"
/** @brief Resource registry, namespace mount and local interactive connections. */
typedef struct tiku_trk_resource_session tiku_trk_resource_session_t;
/** @brief Start on the desktop thread; reject socket/mount collisions.
 *  @note test_provider enables synthetic resources under /test and /test-work. */
tiku_trk_resource_session_t *tiku_trk_resource_session_open(tiku_backend_t *namespace_backend,
    const char *directory,const char *mount_prefix,const char *product,int test_provider);
/** @brief Service bounded buffered requests/jobs/events; return requested test stop. */
int tiku_trk_resource_session_pump(tiku_trk_resource_session_t *);
/** @brief Close peers, detach the read mount, then release the registry. */
void tiku_trk_resource_session_close(tiku_trk_resource_session_t *);
#endif
