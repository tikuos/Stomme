/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_resource_provider_wire.h - application provider descriptor messages.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_RESOURCE_PROVIDER_WIRE_H_
#define TIKU_RESOURCE_PROVIDER_WIRE_H_
#include "tiku_resource.h"

/** @brief Maximum resources in one atomic application registration. */
#define TIKU_RESOURCE_PROVIDER_RESOURCES_MAX 32
/** @brief Encode a descriptor and initial values without caller authority. */
tiku_msg_t *tiku_resource_initial_message(const tiku_resource_initial_t *);
/** @brief Decode an owned descriptor; clear the output after use. */
tiku_resource_error_t tiku_resource_initial_decode(const tiku_msg_t *, tiku_resource_initial_t *);
/** @brief Release a descriptor allocated by initial_decode. */
void tiku_resource_initial_clear(tiku_resource_initial_t *);
/** @brief Encode complete snapshot values without accepting a peer stamp. */
tiku_msg_t *tiku_resource_snapshot_message(const tiku_resource_snapshot_t *);
/** @brief Decode complete owned snapshot values; schema validation is separate. */
tiku_resource_error_t tiku_resource_snapshot_decode(const tiku_msg_t *, tiku_resource_snapshot_t *);

#endif
