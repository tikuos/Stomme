/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_resource_transaction.h - prepared atomic provider setting groups.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_RESOURCE_TRANSACTION_H_
#define TIKU_RESOURCE_TRANSACTION_H_
#include "tiku_resource_registry.h"
#include "tiku_resource_limits.h"

/** @brief Synchronous group preparation, commit and prepared-state disposal.
 *  @note prepare validates the group without effects and supplies owned planned
 *        snapshots and a token. commit atomically saves/applies the prepared
 *        group; an ordinary failure asserts rollback without effects. Uncertain
 *        or partial effects return unknown_outcome. discard runs after prepare,
 *        including failures, with a possibly NULL token. Callbacks cannot block. */
typedef struct {
    tiku_resource_controller_t single;
    tiku_resource_error_t (*prepare)(void *owner, const tiku_resource_replacement_t *,
        size_t count, tiku_resource_snapshot_t *planned, void **token);
    tiku_resource_error_t (*commit)(void *owner, void *token);
    void (*discard)(void *owner, void *token);
} tiku_resource_atomic_controller_t;

/** @brief Register a provider with explicit atomic ownership and single changes. */
tiku_resource_error_t tiku_resource_register_atomic(tiku_resource_registry_t *,
    const char *key, const char *prefix, const char *principal,
    const tiku_resource_initial_t *, size_t count,
    const tiku_resource_atomic_controller_t *, void *owner, tiku_resource_stamp_t *out);
/** @brief Replace one provider's settings at one revision and retain the outcome.
 *  @note Groups have 1 through TRANSACTION_MAX unique paths. deadline_ms is
 *        1 through 30000, including preparation. The caller frees the result.
 *        Success publishes one revision/event after commit; uncertain effects
 *        make the group unavailable. Retained equivalent requests do not commit
 *        again, including after the original deadline or provider lifetime. */
tiku_resource_error_t tiku_resource_transaction(tiku_resource_registry_t *,
    const tiku_resource_caller_t *, const tiku_resource_stamp_t *expected,
    const char *request_id, const tiku_resource_replacement_t *, size_t count,
    unsigned deadline_ms, tiku_msg_t **result);

/** @brief Apply a group with additional negotiated response/value ceilings. */
tiku_resource_error_t tiku_resource_transaction_limited(tiku_resource_registry_t *,
    const tiku_resource_caller_t *, const tiku_resource_stamp_t *, const char *,
    const tiku_resource_replacement_t *, size_t, unsigned,
    const tiku_resource_protocol_limits_t *, tiku_msg_t **);
#endif
