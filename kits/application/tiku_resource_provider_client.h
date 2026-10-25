/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_resource_provider_client.h - owner-thread application provider messages.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_RESOURCE_PROVIDER_CLIENT_H_
#define TIKU_RESOURCE_PROVIDER_CLIENT_H_
#include "tiku_resource_channel.h"
#include "tiku_resource_provider_wire.h"

/** @brief Provider message state owned by the creating application thread. */
typedef struct tiku_resource_provider_client tiku_resource_provider_client_t;
/** @brief Copied controller input; value is owned and cleared by call_clear. */
typedef struct {
    char dispatch[TIKU_RESOURCE_ID_MAX], job[TIKU_RESOURCE_ID_MAX];
    char path[TIKU_RESOURCE_PATH_MAX], request_id[TIKU_RESOURCE_ID_MAX];
    tiku_resource_stamp_t expected;
    tiku_resource_caller_t origin;
    tiku_msg_t *value;
    unsigned deadline_ms;
    int invoke, cancel;
} tiku_resource_provider_call_t;

/** @brief Create after negotiating provider feature and scope; borrow no channel. */
tiku_resource_provider_client_t *
tiku_resource_provider_client_new(const tiku_resource_channel_t *);
/** @brief Free client state on its owner thread, after retiring application work. */
void tiku_resource_provider_client_free(tiku_resource_provider_client_t *);
/** @brief The host-assigned namespace, valid until client destruction. */
const char *
tiku_resource_provider_client_prefix(const tiku_resource_provider_client_t *);
/** @brief Build one atomic registration; NULL on invalid input or pending reply. */
tiku_msg_t *
tiku_resource_provider_client_register(tiku_resource_provider_client_t *,
                                       const tiku_resource_initial_t *,
                                       size_t count);
/** @brief Build a complete GUI-origin snapshot publication at the last acked stamp. */
tiku_msg_t *
tiku_resource_provider_client_publish(tiku_resource_provider_client_t *,
                                      const char *path,
                                      const tiku_resource_snapshot_t *);
/** @brief Build a provider retirement request at the last acked stamp. */
tiku_msg_t *
tiku_resource_provider_client_unregister(tiku_resource_provider_client_t *);
/** @brief Build an assignment query to reconcile a retained or unavailable outcome. */
tiku_msg_t *
tiku_resource_provider_client_status(tiku_resource_provider_client_t *);
/** @brief Consume a correlated reply and update registration state.
 *  @note Requests are sent by the application's channel loop. One reply is
 *        outstanding per client. Retain request bytes for identical retries;
 *        a lost connection requires a new client and registration. */
tiku_resource_error_t
tiku_resource_provider_client_reply(tiku_resource_provider_client_t *,
                                    const tiku_msg_t *);
/** @brief Consume a delivered dispatch/cancel event into owned input.
 *  @note The application validates the input against its live model before
 *        effects. Origin metadata describes the request and grants no authority.
 *        Retained dispatch identities return id_reused without model callbacks. */
tiku_resource_error_t
tiku_resource_provider_client_event(tiku_resource_provider_client_t *,
                                    const tiku_msg_t *,
                                    tiku_resource_provider_call_t *out);
/** @brief Release copied call values and zero the input. */
void tiku_resource_provider_call_clear(tiku_resource_provider_call_t *);
/** @brief Build completion for a delivered dispatch; success needs a snapshot. */
tiku_msg_t *tiku_resource_provider_client_complete(
    tiku_resource_provider_client_t *, const char *dispatch,
    const tiku_resource_snapshot_t *, tiku_resource_error_t,
    tiku_resource_effect_t);
/** @brief Build progress or confirmation-wait publication for a delivered dispatch. */
tiku_msg_t *
tiku_resource_provider_client_progress(tiku_resource_provider_client_t *,
                                       const char *dispatch, unsigned progress,
                                       int awaiting_confirmation);

/** @brief Create client state from a host-authenticated native application assignment.
 *  @note These identifiers describe a grant; construction creates no host authority. */
tiku_resource_provider_client_t *tiku_resource_provider_client_assigned(
    const char *session, const char *principal, uint64_t features);
/** @brief Stage one ordered catalog page, with at most 32 copied descriptors. */
tiku_msg_t *tiku_resource_provider_client_stage(
    tiku_resource_provider_client_t *, const char *catalog, size_t offset,
    size_t total, const tiku_resource_initial_t *, size_t count);
/** @brief Commit a complete staged catalog atomically. */
tiku_msg_t *
tiku_resource_provider_client_commit(tiku_resource_provider_client_t *,
                                     const char *catalog);
/** @brief Release an unpublished staged catalog. */
tiku_msg_t *
tiku_resource_provider_client_abort(tiku_resource_provider_client_t *,
                                    const char *catalog);
/** @brief Whether no management request awaits acknowledgment. */
int tiku_resource_provider_client_ready(
    const tiku_resource_provider_client_t *);
/** @brief Upload one ordered stream chunk; the final chunk publishes atomically. */
tiku_msg_t *tiku_resource_provider_client_stream(
    tiku_resource_provider_client_t *, const char *path, const char *transfer,
    size_t offset, size_t total, const void *bytes, size_t count);
/** @brief Mark an owned read-only observation's capture availability, retaining its data. */
tiku_msg_t *
tiku_resource_provider_client_availability(tiku_resource_provider_client_t *,
                                           const char *path, int available);
#endif
