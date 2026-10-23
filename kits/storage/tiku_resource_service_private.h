/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_resource_service_private.h - broker leases and message encoders.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_RESOURCE_SERVICE_PRIVATE_H_
#define TIKU_RESOURCE_SERVICE_PRIVATE_H_
#include "tiku_resource_service.h"
#include "tiku_resource_registry_private.h"
#include "tiku_resource_stream.h"

#define RESOURCE_SERVICE_CURSORS 16
#define RESOURCE_SERVICE_WATCHES 32

/** @brief One connection-owned enumeration position and snapshot epoch. */
typedef struct {
    char id[TIKU_RESOURCE_ID_MAX], path[TIKU_RESOURCE_PATH_MAX];
    uint64_t epoch, after, expires;
    int consumed;
} resource_cursor_t;

/** @brief A public subscription ID mapped to a trusted native watch handle. */
typedef struct {
    char id[TIKU_RESOURCE_ID_MAX];
    tiku_resource_watch_t native;
} resource_subscription_t;

/** @brief Connection-owned frozen content and monotonic idle lease. */
typedef struct {
    char id[TIKU_RESOURCE_ID_MAX];
    tiku_resource_stream_t *snapshot;
    tiku_resource_stamp_t stamp;
    size_t bytes;
    uint64_t expires;
} resource_stream_lease_t;

struct tiku_resource_service {
    tiku_resource_registry_t *registry;
    tiku_resource_caller_t caller;
    char connection[TIKU_RESOURCE_ID_MAX];
    uint64_t features;
    tiku_resource_protocol_limits_t limits;
    int used;
    resource_stream_lease_t streams[8];
    resource_cursor_t cursors[RESOURCE_SERVICE_CURSORS];
    resource_subscription_t watches[RESOURCE_SERVICE_WATCHES];
    unsigned next_watch;
    struct resource_remote *remote;
};

/** @brief Compare a transport grant with the broker's immutable grant. */
int tiku_resource_service_caller(const tiku_resource_service_t *, const tiku_resource_caller_t *);
/** @brief Check complete message framing and nesting before dispatch or send. */
int tiku_resource_service_encodable(const tiku_msg_t *);
/** @brief Encode a provider stamp with canonical unsigned revision text. */
int tiku_resource_service_stamp(tiku_msg_t *, const tiku_resource_stamp_t *);
/** @brief Encode copied snapshot values; return an owned message. */
tiku_msg_t *tiku_resource_service_snapshot(const tiku_resource_snapshot_t *);
/** @brief Encode a job and its current controller/resource stamps. */
tiku_msg_t *tiku_resource_service_job(const tiku_resource_job_info_t *);
/** @brief Construct and preflight a correlated result or structured error. */
tiku_msg_t *tiku_resource_service_reply(tiku_resource_service_t *, const tiku_msg_t *,
    tiku_msg_t *result, tiku_resource_error_t, tiku_resource_effect_t);
/** @brief Decode a validated expected tuple. */
int tiku_resource_service_expected(const tiku_msg_t *, tiku_resource_stamp_t *);
/** @brief Cancel with retained request identity and scope requirements. */
tiku_resource_error_t tiku_resource_service_cancel(tiku_resource_service_t *,
    const tiku_msg_t *, tiku_msg_t **, tiku_resource_effect_t *);
/** @brief Dispatch bounded frozen stream operations. */
tiku_resource_error_t tiku_resource_service_stream(tiku_resource_service_t *, const tiku_msg_t *, tiku_msg_t **);
/** @brief Release expired or disconnected stream leases. */
void tiku_resource_service_streams_close(tiku_resource_service_t *, int expired_only);
/** @brief Dispatch one negotiated atomic setting group. */
tiku_resource_error_t tiku_resource_service_transaction(tiku_resource_service_t *, const tiku_msg_t *,
    tiku_msg_t **, tiku_resource_effect_t *);
/** @brief Enumerate with connection-bound cursor leases. */
tiku_resource_error_t tiku_resource_service_list(tiku_resource_service_t *, const tiku_msg_t *, tiku_msg_t **);
/** @brief Describe a resource with broker/root capability metadata. */
tiku_resource_error_t tiku_resource_service_describe(tiku_resource_service_t *, const char *, tiku_msg_t **);
/** @brief Dispatch assigned-provider operations with retained connection outcomes. */
tiku_msg_t *tiku_resource_remote_request(tiku_resource_service_t *, const tiku_msg_t *);
/** @brief Retire the assigned provider before releasing its owner context. */
void tiku_resource_remote_close(tiku_resource_service_t *);
/** @brief Consume one live controller dispatch or cancellation event. */
int tiku_resource_remote_event(tiku_resource_service_t *, tiku_msg_t **);
#endif
