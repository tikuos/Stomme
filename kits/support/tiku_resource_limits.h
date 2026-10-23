/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_resource_limits.h - negotiated resource protocol ceilings.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_RESOURCE_LIMITS_H_
#define TIKU_RESOURCE_LIMITS_H_
#include "tiku_resource.h"
/** @brief Per-connection maxima selected as the minimum of both peer offers. */
typedef struct {
    unsigned frame_bytes, message_depth, text_bytes, members, page_entries;
    unsigned transaction_settings, subscriptions, active_jobs, provider_resources;
    unsigned stream_leases, stream_chunk_bytes, stream_bytes;
} tiku_resource_protocol_limits_t;
/** @brief Fill the implemented hard ceilings. */
void tiku_resource_protocol_limits_default(tiku_resource_protocol_limits_t *);
/** @brief Whether every limit is within the implemented range. */
int tiku_resource_protocol_limits_valid(const tiku_resource_protocol_limits_t *);
/** @brief Whether a valid selection fits within a valid offer. */
int tiku_resource_protocol_limits_contains(const tiku_resource_protocol_limits_t *offer, const tiku_resource_protocol_limits_t *selection);
/** @brief Encode owned limit metadata. */
tiku_msg_t *tiku_resource_protocol_limits_message(const tiku_resource_protocol_limits_t *);
/** @brief Decode a complete exact limit record. */
int tiku_resource_protocol_limits_decode(const tiku_msg_t *, tiku_resource_protocol_limits_t *);
/** @brief Select the componentwise minimum of two valid offers. */
void tiku_resource_protocol_limits_merge(tiku_resource_protocol_limits_t *, const tiku_resource_protocol_limits_t *);
/** @brief Preflight framing, typed-value ceilings and reserved envelope depth. */
tiku_resource_error_t tiku_resource_message_limits(const tiku_msg_t *, const tiku_resource_protocol_limits_t *, unsigned ancestors);
#endif
