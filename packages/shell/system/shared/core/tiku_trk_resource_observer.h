/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_resource_observer.h - copied observations of owner-thread shell
 * state.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_RESOURCE_OBSERVER_H_
#define TIKU_TRK_RESOURCE_OBSERVER_H_
#include "tiku_resource_registry.h"
#include "tiku_resource_jobs.h"
/** @brief Entry kind: state, complete NDJSON stream, or native action. */
enum {
    TIKU_TRK_OBSERVE_STATE = 0,
    TIKU_TRK_OBSERVE_STREAM = 1,
    TIKU_TRK_OBSERVE_ACTION = 2
};
/** @brief Content and cross-application requirements for an observation. */
enum {TIKU_TRK_OBSERVE_SENSITIVE=1,TIKU_TRK_OBSERVE_CROSS_APP=2};
/** @brief One entry; stream selects kind and sensitive combines scope flags.
 */
typedef struct {
    const char *path;
    int stream, sensitive;
} tiku_trk_observation_t;
/** @brief Owner-thread provider with retained last successful stream bytes.
 */
typedef struct tiku_trk_resource_observer tiku_trk_resource_observer_t;
/** @brief Register copied observations; paths and callbacks must outlive the observer.
 *  @note count is used for streams; capture returns owned typed objects, with
 * index=-1
 *        for states. Callbacks must neither block nor change the registry.
 * NULL registry
 *        returns NULL without creating a provider. */
tiku_trk_resource_observer_t *tiku_trk_resource_observer_open(
    tiku_resource_registry_t *, const char *key, const char *prefix,
    const tiku_trk_observation_t *, size_t count,
    int (*count_items)(void *, const char *),
    tiku_msg_t *(*capture)(void *, const char *, int), void *);
/** @brief Publish changed observations; retain last successful data on
 * capture failure.
 */
void tiku_trk_resource_observer_poll(tiku_trk_resource_observer_t *,
                                     uint64_t now_ms);
/** @brief Unregister before destroying observed owners or the registry. */
void tiku_trk_resource_observer_close(tiku_trk_resource_observer_t *);
/** @brief Register observations and native async actions beneath one owner.
 *  @note Action snapshots are retained results and are not refreshed by poll.
 *        Controller callbacks receive context; borrowed inputs outlive owner.
 * */
tiku_trk_resource_observer_t *tiku_trk_resource_observer_open_controlled(
    tiku_resource_registry_t *, const char *key, const char *prefix,
    const tiku_trk_observation_t *, size_t count,
    int (*count_items)(void *, const char *),
    tiku_msg_t *(*capture)(void *, const char *, int),
    const tiku_resource_async_controller_t *, void *context);

#endif
