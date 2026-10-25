/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_resource_client.h - owner-thread local resource discovery and requests.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_RESOURCE_CLIENT_H_
#define TIKU_RESOURCE_CLIENT_H_
#include "tiku_resource_endpoint.h"
#include "tiku_resource_literal.h"
/** @brief Negotiated same-user client; operations stay on its creating thread. */
typedef struct tiku_resource_client tiku_resource_client_t;
/** @brief Connect and negotiate available non-provider features; NULL on failure. */
tiku_resource_client_t *tiku_resource_client_open(const char *directory,
                                                  int timeout_ms);
/** @brief Release events, reply diagnostics and the owned transport. */
void tiku_resource_client_close(tiku_resource_client_t *);
/** @brief Copy the authenticated grant and negotiated limits. */
int tiku_resource_client_info(const tiku_resource_client_t *,
                              tiku_resource_caller_t *,
                              tiku_resource_protocol_limits_t *);
/** @brief Allocate a fresh correlated request; the caller owns it. */
tiku_msg_t *tiku_resource_client_request(tiku_resource_client_t *,
                                         const char *operation,
                                         const char *path);
/** @brief Exchange a borrowed request and return an owned result on success.
 * @note Events are queued while waiting. Mutation transport loss is unknown_outcome;
 * no mutation is retried. Reuse the exact envelope for an explicit replay. */
tiku_resource_error_t tiku_resource_client_exchange(tiku_resource_client_t *,
                                                    const tiku_msg_t *,
                                                    tiku_msg_t **result);
/** @brief Copy the last structured error, or NULL when none is retained. */
tiku_msg_t *tiku_resource_client_error(const tiku_resource_client_t *);
/** @brief Last attempted mutation ID; retained across observations, empty before one. */
const char *tiku_resource_client_mutation(const tiku_resource_client_t *);
/** @brief Read or describe one canonical path, returning an owned result. */
tiku_resource_error_t tiku_resource_client_observe(tiku_resource_client_t *,
                                                   const char *operation,
                                                   const char *path,
                                                   tiku_msg_t **result);
/** @brief Enumerate a bounded page; cursor is NULL/empty for the first page. */
tiku_resource_error_t tiku_resource_client_list(tiku_resource_client_t *,
                                                const char *path,
                                                const char *cursor,
                                                tiku_msg_t **result);
/** @brief Decode a provider stamp from public description/snapshot/job metadata. */
int tiku_resource_client_stamp(const tiku_msg_t *, tiku_resource_stamp_t *);
/** @brief Validate a proposal against published kind/schema, grant and features. */
tiku_resource_error_t tiku_resource_client_validate(
                              const tiku_resource_client_t *,
                              const tiku_msg_t *description,
                              const tiku_msg_t *value, int invoke);
/** @brief Submit at the displayed description's revision with its declared durability.
 * @note Returns the initial receipt/job. Model/controller validation stays native. */
tiku_resource_error_t tiku_resource_client_change(
    tiku_resource_client_t *, const tiku_msg_t *description,
    const tiku_msg_t *value, int invoke, tiku_msg_t **result);
/** @brief Query a retained job by ID, returning an owned status. */
tiku_resource_error_t tiku_resource_client_job(tiku_resource_client_t *,
                                               const char *id,
                                               tiku_msg_t **result);
/** @brief Reconcile a retained mutation ID without executing its proposal again. */
tiku_resource_error_t tiku_resource_client_receipt(tiku_resource_client_t *,
                                                   const char *request_id,
                                                   tiku_msg_t **result);
/** @brief Request cancellation at the latest copied job stamp, returning an owned ack. */
tiku_resource_error_t tiku_resource_client_cancel(tiku_resource_client_t *,
                                                  const char *id,
                                                  tiku_msg_t **result);
/** @brief Subscribe at the description's provider, returning its atomic initial snapshot. */
tiku_resource_error_t tiku_resource_client_watch(tiku_resource_client_t *,
                                                 const tiku_msg_t *description,
                                                 const char *path,
                                                 tiku_msg_t **result);
/** @brief Retire one connection-owned subscription by ID. */
tiku_resource_error_t tiku_resource_client_unwatch(tiku_resource_client_t *,
                                                   const char *subscription);
/** @brief Return an owned event, pending when idle, or limit after local overflow.
 * @note Local overflow discards queued deltas and requires an explicit resnapshot. */
tiku_resource_error_t tiku_resource_client_event(tiku_resource_client_t *,
                                                 int timeout_ms,
                                                 tiku_msg_t **event);
/** @brief Capture a frozen stream at the displayed revision into owned bytes.
 * @note maximum is bounded by negotiated stream limits; no bytes on failure. */
tiku_resource_error_t tiku_resource_client_stream(
                            tiku_resource_client_t *,
                            const tiku_msg_t *description, size_t maximum,
                            void **bytes, size_t *length);
/** @brief Render public message fields as bounded JSON; no output on failure.
 * @note Repeated values remain arrays. Binary values report byte counts. */
int tiku_resource_client_format(const tiku_msg_t *, void *out,
                                size_t capacity);
#endif
