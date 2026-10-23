/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_resource_service_transaction.c - atomic setting group protocol dispatch.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_resource_service_private.h"

tiku_resource_error_t tiku_resource_service_transaction(tiku_resource_service_t *service,
    const tiku_msg_t *request, tiku_msg_t **result, tiku_resource_effect_t *effect)
{
    tiku_resource_replacement_t items[TIKU_RESOURCE_TRANSACTION_MAX] = {0};
    tiku_resource_stamp_t expected;
    tiku_msg_t *changes;
    tiku_resource_error_t error;
    resource_receipt_t *receipt;
    int existing;
    int32_t deadline;
    size_t count = 0;
    if (!tiku_resource_service_expected(request, &expected) || !tiku_msg_find_int32(request, "deadline_ms", 0, &deadline))
        return TIKU_RESOURCE_INVALID;
    changes = tiku_msg_find_message(request, "changes", 0);
    error = tiku_resource_replacements_decode(changes, items, &count); tiku_msg_free(changes);
    if (!error) error = tiku_resource_transaction_limited(service->registry, &service->caller, &expected,
        tiku_msg_find_string(request, "request_id", 0), items, count, (unsigned)deadline, &service->limits, result);
    tiku_resource_replacements_clear(items, count);
    receipt = tiku_resource_registry_receipt(service->registry, service->caller.principal,
        tiku_msg_find_string(request, "request_id", 0), &existing);
    if (error == TIKU_RESOURCE_UNKNOWN_OUTCOME) *effect = TIKU_RESOURCE_EFFECT_UNKNOWN;
    else if (!error && existing && receipt) *effect = receipt->effect;
    return error;
}
