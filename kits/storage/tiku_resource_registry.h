/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_resource_registry.h - owner-thread resource registration and observation.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_RESOURCE_REGISTRY_H_
#define TIKU_RESOURCE_REGISTRY_H_
#include "tiku_resource.h"

/** @brief Nonzero monotonic milliseconds supplied by host policy; no registry calls. */
typedef uint64_t (*tiku_resource_clock_fn)(void *);

/** @brief Registry and provider lifetimes managed by the owner thread. */
typedef struct tiku_resource_registry tiku_resource_registry_t;
/** @brief Subscription identity, invalidated on close or registry destruction. */
typedef uint64_t tiku_resource_watch_t;

/** @brief Controller validation and synchronous mutation callbacks.
 *  @note Callbacks run on the registry thread. The result transfers its values
 *        to the registry. Failure without a snapshot asserts no effects; use
 *        unknown_outcome for uncertain effects. Reentrant mutation returns busy. */
typedef struct {
    tiku_resource_error_t (*validate)(void *owner, const char *path,
                                    const tiku_msg_t *value, int invoke);
    tiku_resource_error_t (*apply)(void *owner, const char *path,
                                 const tiku_msg_t *value, int invoke,
                                 tiku_resource_snapshot_t *result);
} tiku_resource_controller_t;

/** @brief Bounded registry capacities; zero fields select defaults. */
typedef struct {
    unsigned providers, resources, watches, events_per_watch, receipts;
} tiku_resource_limits_t;

/** @brief One discoverable entry, including implicit parent directories. */
typedef struct {
    char path[TIKU_RESOURCE_PATH_MAX], id[TIKU_RESOURCE_ID_MAX];
    uint64_t node_id;
    tiku_resource_kind_t kind;
} tiku_resource_entry_t;

/** @brief One change record; overflow pauses delivery until watch_close. */
typedef struct {
    char path[TIKU_RESOURCE_PATH_MAX];
    tiku_resource_stamp_t stamp;
    uint64_t sequence;
    int removed, overflow;
    unsigned affected_count;
    char affected[TIKU_RESOURCE_GROUP_PATH_BYTES];
} tiku_resource_event_t;

/** @brief Create an owner-thread registry; NULL on invalid limits or allocation.
 *  @note Registry operations, callbacks and destruction use its creating thread. */
tiku_resource_registry_t *tiku_resource_registry_new(const tiku_resource_limits_t *limits);
/** @brief Set a host clock before providers, brokers or subscriptions exist. */
tiku_resource_error_t tiku_resource_registry_clock(tiku_resource_registry_t *,
    tiku_resource_clock_fn, void *context);
/** @brief Destroy a registry after closing its backend adapters and brokers. */
void tiku_resource_registry_free(tiku_resource_registry_t *registry);
/** @brief The registry's session incarnation, valid until destruction. */
const char *tiku_resource_registry_session(const tiku_resource_registry_t *registry);

/** @brief Register a nonoverlapping subtree atomically; copy descriptors/values.
 *  @param principal Owner identity, or NULL for a host-owned provider. */
tiku_resource_error_t tiku_resource_register(tiku_resource_registry_t *,
    const char *key, const char *prefix, const char *principal,
    const tiku_resource_initial_t *, size_t count,
    const tiku_resource_controller_t *, void *owner, tiku_resource_stamp_t *out);
/** @brief Retire a provider incarnation; callbacks cannot run after return. */
tiku_resource_error_t tiku_resource_unregister(tiku_resource_registry_t *,
                                               const tiku_resource_stamp_t *);
/** @brief Read a copied snapshot after caller and lifetime checks. */
tiku_resource_error_t tiku_resource_read(tiku_resource_registry_t *,
    const tiku_resource_caller_t *, const char *path, tiku_resource_snapshot_t *out);
/** @brief Describe a visible resource; caller frees the returned message. */
tiku_resource_error_t tiku_resource_describe(tiku_resource_registry_t *,
    const tiku_resource_caller_t *, const char *path, tiku_msg_t **out);
/** @brief Enumerate immediate visible children after node_id, at a catalog epoch.
 *  @note An input epoch of zero starts enumeration; a changed epoch is stale.
 *  @return A result code; count and epoch describe the returned page. */
tiku_resource_error_t tiku_resource_list(tiku_resource_registry_t *,
    const tiku_resource_caller_t *, const char *path, uint64_t after,
    uint64_t *epoch, tiku_resource_entry_t *out, size_t capacity, size_t *count);
/** @brief Resolve a currently visible node ID. */
tiku_resource_error_t tiku_resource_entry(tiku_resource_registry_t *,
    const tiku_resource_caller_t *, const char *path, tiku_resource_entry_t *out);

/** @brief Replace or invoke through the owner, checking authority and revision.
 *  @note Successful retained request IDs replay their result without effects.
 *        Output is zeroed before dispatch; the caller clears returned values. */
tiku_resource_error_t tiku_resource_change(tiku_resource_registry_t *,
    const tiku_resource_caller_t *, const char *path,
    const tiku_resource_stamp_t *expected, const char *request_id,
    const tiku_msg_t *value, int invoke, tiku_resource_snapshot_t *out);
/** @brief Publish an existing controller's complete snapshot at its revision. */
tiku_resource_error_t tiku_resource_publish(tiku_resource_registry_t *,
    const tiku_resource_stamp_t *expected, const char *path,
    const tiku_resource_snapshot_t *value, tiku_resource_stamp_t *out);

/** @brief Start a provider watch and copy its visible filtered snapshot atomically.
 *  @note The snapshot is a bounded message of entries; the caller frees it. */
tiku_resource_error_t tiku_resource_watch_open(tiku_resource_registry_t *,
    const tiku_resource_caller_t *, const char *provider, const char *prefix,
    tiku_resource_watch_t *out, tiku_msg_t **snapshot);
/** @brief Consume one event: 1 with event, 0 when empty, -1 for an invalid watch. */
int tiku_resource_watch_next(tiku_resource_registry_t *, tiku_resource_watch_t,
                             tiku_resource_event_t *out);
/** @brief Release a watch and its queued events. */
void tiku_resource_watch_close(tiku_resource_registry_t *, tiku_resource_watch_t);

/** @brief Owner-thread counts of live registry resources and pending work. */
typedef struct {
    unsigned providers, provider_capacity, nodes, node_capacity;
    unsigned watches, paused_watches, active_jobs, retained_jobs, services;
    size_t retained_stream_bytes;
} tiku_resource_registry_observation_t;
/** @brief Copy counts without provider callbacks; zero for invalid thread/output. */
int tiku_resource_registry_observe(const tiku_resource_registry_t *,tiku_resource_registry_observation_t *);

/** @brief Counts and maximum sequence across selected live subscriptions. */
typedef struct {
    unsigned subscriptions, paused, queued;
    uint64_t maximum_sequence;
} tiku_resource_event_observation_t;
/** @brief Observe live event queues, excluding one optional provider key.
 * @note Sequences belong to subscriptions; the maximum can fall on retirement. */
int tiku_resource_events_observe(const tiku_resource_registry_t *,
                                 const char *exclude_provider,
                                 tiku_resource_event_observation_t *);

#endif
