/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_resource_registry_private.h - registry-owned resource and observer state.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_RESOURCE_REGISTRY_PRIVATE_H_
#define TIKU_RESOURCE_REGISTRY_PRIVATE_H_
#include "tiku_resource_registry.h"
#include "tiku_resource_jobs.h"
#include "tiku_resource_transaction.h"
#include <pthread.h>

/** @brief One registered controller lifetime. */
typedef struct {
    int live;
    char key[TIKU_RESOURCE_KEY_MAX], prefix[TIKU_RESOURCE_PATH_MAX];
    char principal[TIKU_RESOURCE_ID_MAX];
    tiku_resource_stamp_t stamp;
    tiku_resource_controller_t controller;
    void *owner;
    tiku_resource_async_controller_t async;
    int asynchronous;
    tiku_resource_atomic_controller_t atomic;
} resource_provider_t;

/** @brief A registered value or an implicit parent directory. */
typedef struct {
    tiku_resource_entry_t entry;
    resource_provider_t *provider;
    tiku_resource_spec_t spec;
    tiku_resource_snapshot_t value;
    char **choices;
    int unavailable;
    struct resource_blob *blob;
} resource_node_t;

/** @brief One caller-filtered subscription queue. */
typedef struct {
    tiku_resource_watch_t id;
    tiku_resource_caller_t caller;
    char provider[TIKU_RESOURCE_KEY_MAX], incarnation[TIKU_RESOURCE_ID_MAX];
    char prefix[TIKU_RESOURCE_PATH_MAX];
    tiku_resource_event_t *events;
    unsigned head, count;
    uint64_t sequence, revision;
    int paused, retired;
} resource_watch_t;

/** @brief A retained synchronous mutation outcome. */
typedef struct {
    char principal[TIKU_RESOURCE_ID_MAX], id[TIKU_RESOURCE_ID_MAX];
    char path[TIKU_RESOURCE_PATH_MAX];
    tiku_resource_stamp_t expected;
    tiku_msg_t *request;
    tiku_resource_snapshot_t result;
    tiku_resource_error_t error;
    uint64_t completed;
    int invoke, pending, sensitive, protocol_bound;
    unsigned protocol_deadline;
    char operation[16];
    tiku_msg_t *protocol_result;
    char protocol_durability[16];
    tiku_resource_effect_t effect;
    uint64_t read_scopes;
    char provider_principal[TIKU_RESOURCE_ID_MAX], job[TIKU_RESOURCE_ID_MAX];
} resource_receipt_t;

/** @brief A queued or retained job with copied arguments and authority. */
typedef struct {
    tiku_resource_job_info_t info;
    tiku_resource_caller_t caller;
    char connection[TIKU_RESOURCE_ID_MAX], provider_principal[TIKU_RESOURCE_ID_MAX];
    tiku_msg_t *request;
    tiku_resource_stamp_t expected;
    uint64_t acceptance_deadline, execution_deadline, completed, read_scopes;
    int invoke, sensitive;
    unsigned acceptance_ms;
    tiku_resource_error_t rejected;
} resource_job_t;
/** @brief Maximum charged observer bytes retained for one principal. */
#define RESOURCE_EVENT_BYTES_MAX (1024u * 1024u)
/** @brief Record storage plus bounded path-codec and envelope overhead. */
#define RESOURCE_EVENT_CHARGE (sizeof(tiku_resource_event_t) + TIKU_RESOURCE_TRANSACTION_MAX * 128u + 1024u)
#define RESOURCE_JOBS_ACTIVE_MAX 16
#define RESOURCE_JOBS_DONE_MAX 16
#define RESOURCE_JOBS_MAX (RESOURCE_JOBS_ACTIVE_MAX + RESOURCE_JOBS_DONE_MAX)

struct tiku_resource_registry {
    pthread_t thread;
    tiku_resource_limits_t limits;
    char session[TIKU_RESOURCE_ID_MAX];
    resource_provider_t *providers;
    resource_node_t **nodes;
    resource_watch_t *watches;
    resource_receipt_t *receipts;
    unsigned count;
    uint64_t next_node, next_watch, epoch;
    int dispatching;
    unsigned services, stream_leases;
    size_t stream_bytes;
    struct resource_remote *remote_providers;
    tiku_resource_clock_fn clock;
    void *clock_context;
    resource_job_t *jobs;
    tiku_resource_stamp_t jobs_stamp;
};

/** @brief Whether the current thread owns the registry. */
int tiku_resource_registry_thread(const tiku_resource_registry_t *);
/** @brief Match a path and descendants at component boundaries. */
int tiku_resource_registry_below(const char *path, const char *prefix);
/** @brief Find a registered node by canonical path. */
resource_node_t *tiku_resource_registry_node(tiku_resource_registry_t *, const char *);
/** @brief Release a registered node and its stream snapshot reference. */
void tiku_resource_registry_node_free(resource_node_t *);
/** @brief Drop a stream blob reference and its owning registry budget. */
void tiku_resource_stream_drop(struct resource_blob *);
/** @brief Find a live provider key. */
resource_provider_t *tiku_resource_registry_provider(tiku_resource_registry_t *, const char *);
/** @brief Whether caller may read this node's public snapshot. */
int tiku_resource_registry_visible(const resource_node_t *, const tiku_resource_caller_t *);
/** @brief Copy a bounded snapshot, or leave an empty output on failure. */
int tiku_resource_registry_copy(tiku_resource_snapshot_t *, const tiku_resource_snapshot_t *);
/** @brief Validate a controller snapshot against its resource schema. */
tiku_resource_error_t tiku_resource_registry_snapshot_check(const resource_node_t *, const tiku_resource_snapshot_t *);
/** @brief Validate a proposed typed value against one schema. */
tiku_resource_error_t tiku_resource_registry_validate(const tiku_resource_spec_t *, const tiku_msg_t *);
/** @brief Queue caller-filtered observer events. */
void tiku_resource_registry_emit(tiku_resource_registry_t *, const resource_node_t *, int removed);
/** @brief Emit one caller-filtered record for a committed provider group. */
void tiku_resource_registry_emit_group(tiku_resource_registry_t *, resource_node_t *const *, size_t);
/** @brief Apply a validated snapshot and emit a revision when values differ. */
void tiku_resource_registry_install(tiku_resource_registry_t *, resource_node_t *, tiku_resource_snapshot_t *);

/** @brief The configured monotonic clock, or zero on clock failure. */
uint64_t tiku_resource_registry_now(tiku_resource_registry_t *);
/** @brief Validate fixed-size stamp fields. */
int tiku_resource_registry_stamp_valid(const tiku_resource_stamp_t *);
/** @brief Compare provider incarnation and revision. */
int tiku_resource_registry_matches(const resource_provider_t *, const tiku_resource_stamp_t *);
/** @brief Check kind, scopes and provider ownership for mutation. */
int tiku_resource_registry_authorized(const resource_node_t *, const tiku_resource_caller_t *, int);
/** @brief Find an outcome or choose a completed cache slot; pending slots stay. */
resource_receipt_t *tiku_resource_registry_receipt(tiku_resource_registry_t *, const char *, const char *, int *);
/** @brief Release one retained mutation outcome. */
void tiku_resource_registry_receipt_clear(resource_receipt_t *);
/** @brief Whether a caller retains authority to read a receipt's result. */
int tiku_resource_registry_receipt_visible(const resource_receipt_t *, const tiku_resource_caller_t *);
/** @brief Mark a value unavailable after effects cannot be reconciled. */
void tiku_resource_registry_uncertain(tiku_resource_registry_t *, resource_node_t *);
/** @brief Retire jobs without callbacks when their provider disappears. */
void tiku_resource_jobs_owner_lost(tiku_resource_registry_t *, const resource_provider_t *);
/** @brief Free copied job records during registry destruction. */
void tiku_resource_jobs_drop(tiku_resource_registry_t *);
/** @brief Find a retained job request by principal and request identity. */
resource_job_t *tiku_resource_jobs_request(tiku_resource_registry_t *, const char *, const char *);
#endif
