/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_resource_service.h - connection-scoped resource protocol dispatch.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_RESOURCE_SERVICE_H_
#define TIKU_RESOURCE_SERVICE_H_
#include "tiku_resource_jobs.h"
#include "tiku_resource_limits.h"
/** @brief Maximum broker connections retained by one registry. */
#define TIKU_RESOURCE_CONNECTIONS_MAX 64

/** @brief A broker connection with copied caller authority and owned handles. */
typedef struct tiku_resource_service tiku_resource_service_t;

/** @brief Create a broker on the registry thread for one authenticated principal.
 *  @note features is the connection's allowed feature mask. The host must close
 *        this broker before its registry or caller authority is replaced. */
tiku_resource_service_t *tiku_resource_service_new(tiku_resource_registry_t *,
    const tiku_resource_caller_t *, uint64_t features);
/** @brief Bind negotiated limits before the first request or event. */
tiku_resource_error_t tiku_resource_service_limits(tiku_resource_service_t *, const tiku_resource_protocol_limits_t *);
/** @brief Assign /apps/<principal> to this granted provider connection.
 *  @note Requires provider feature and scope; one assignment per live principal.
 *        Host policy calls this explicitly after channel negotiation. */
tiku_resource_error_t tiku_resource_service_provider_enable(tiku_resource_service_t *);
/** @brief Close owned watches/cursors and cancel queued connection requests. */
void tiku_resource_service_free(tiku_resource_service_t *);
/** @brief Dispatch a checked request; transfer reply ownership to the caller.
 *  @note caller must match the connection grant. NULL means a reply cannot be
 *        encoded; the transport retires the connection without repeating effects. */
tiku_msg_t *tiku_resource_service_dispatch(tiku_resource_service_t *,
    const tiku_resource_caller_t *, const tiku_msg_t *request);
/** @brief Consume a caller-filtered event: 1 with message, 0 empty, -1 on error.
 *  @note Message ownership transfers to the caller. */
int tiku_resource_service_event(tiku_resource_service_t *,
    const tiku_resource_caller_t *, tiku_msg_t **out);
/** @brief Process queued jobs and deadlines on the registry thread. */
int tiku_resource_service_pump(tiku_resource_service_t *);
/** @brief Read the broker's offered feature mask. */
uint64_t tiku_resource_service_features(const tiku_resource_service_t *);

#endif
