/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_lifecycle_resources.h - native application and job resource
 * lifetimes.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_LIFECYCLE_RESOURCES_H_
#define TIKU_TRK_LIFECYCLE_RESOURCES_H_
#include "tiku_resource_jobs.h"
/** @brief Native owner families with independent resource incarnations. */
typedef enum {
    TIKU_TRK_LIFECYCLE_APP,
    TIKU_TRK_LIFECYCLE_WORK,
    TIKU_TRK_LIFECYCLE_FILE
} tiku_trk_lifecycle_kind_t;
/** @brief Owner-thread resource host, with copied native lifetime tokens. */
typedef struct tiku_trk_lifecycle_resources tiku_trk_lifecycle_resources_t;
/** @brief Native callbacks; metadata capture returns owned typed values.
 *  @note enumerate returns 0 for an item, -1 past the end. validate rechecks
 *        native lifetime and capabilities. No callback may mutate the
 * registry. */
typedef struct {
    int (*enumerate)(void *,tiku_trk_lifecycle_kind_t,int,char *,size_t);
    tiku_msg_t *(*capture)(void *, tiku_trk_lifecycle_kind_t, const char *,
                           const char *);
    tiku_resource_error_t (*validate)(void *, tiku_trk_lifecycle_kind_t,
                                      const char *, const char *,
                                      const tiku_msg_t *);
    tiku_resource_error_t (*perform)(void *, tiku_trk_lifecycle_kind_t,
                                     const char *, const char *,
                                     const tiku_msg_t *, tiku_msg_t **);
} tiku_trk_lifecycle_host_t;
/** @brief Register native instances and jobs; callbacks and context outlive
 * the owner.
 */
tiku_trk_lifecycle_resources_t *tiku_trk_lifecycle_resources_open(tiku_resource_registry_t *,
                                  const tiku_trk_lifecycle_host_t *, void *);
/** @brief Reconcile native identities and publish current observations. */
void tiku_trk_lifecycle_resources_poll(tiku_trk_lifecycle_resources_t *);
/** @brief Copy discovery and registration counts; incomplete registration is
 * explicit.
 */
void tiku_trk_lifecycle_resources_counts(
    const tiku_trk_lifecycle_resources_t *, int *expected, int *registered);
/** @brief Retire entities before their native owners and registry are
 * destroyed.
 */
void tiku_trk_lifecycle_resources_close(tiku_trk_lifecycle_resources_t *);
#endif
