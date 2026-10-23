/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_resource_service_cancel.c - retained cancellation acknowledgements.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_resource_service_private.h"
#include <string.h>

/** @brief Allocate an acknowledgement before a controller can receive cancellation. */
static tiku_msg_t *acknowledgement(const char *value)
{
    tiku_msg_t *message = tiku_msg_new(0);
    if (message && tiku_msg_add_string(message, "cancellation", value)) return message;
    tiku_msg_free(message); return NULL;
}

tiku_resource_error_t tiku_resource_service_cancel(tiku_resource_service_t *service,
    const tiku_msg_t *request, tiku_msg_t **out, tiku_resource_effect_t *effect)
{
    const char *id = tiku_msg_find_string(request, "request_id", 0), *target = tiku_msg_find_string(request, "job", 0);
    tiku_resource_stamp_t expected = {0};
    resource_receipt_t *receipt;
    resource_job_t *original;
    tiku_resource_job_info_t job;
    tiku_resource_cancel_t cancellation;
    tiku_resource_error_t error;
    tiku_msg_t *choices[3] = {0}, *value;
    uint64_t before;
    int existing, i;
    int32_t deadline;
    if (!tiku_resource_service_expected(request, &expected) || !tiku_msg_find_int32(request, "deadline_ms", 0, &deadline))
        return TIKU_RESOURCE_INVALID;
    receipt = tiku_resource_registry_receipt(service->registry, service->caller.principal, id, &existing);
    if (existing) {
        const char *saved = tiku_msg_find_string(receipt->request, "value", 0);
        if (strcmp(receipt->operation, "cancel") || !saved || strcmp(saved, target) ||
            strcmp(receipt->expected.provider, expected.provider) || strcmp(receipt->expected.incarnation, expected.incarnation) ||
            receipt->expected.revision != expected.revision || receipt->protocol_deadline != (unsigned)deadline)
            return TIKU_RESOURCE_ID_REUSED;
        if (!tiku_resource_registry_receipt_visible(receipt, &service->caller)) return TIKU_RESOURCE_OUTCOME_UNAVAILABLE;
        *effect = receipt->effect;
        if (receipt->protocol_result && !(*out = tiku_msg_copy(receipt->protocol_result))) return TIKU_RESOURCE_NO_MEMORY;
        return receipt->error;
    }
    if (!receipt) return TIKU_RESOURCE_BUSY;
    error = tiku_resource_job_status(service->registry, &service->caller, target, &job);
    if (error) return error;
    original = tiku_resource_jobs_request(service->registry, service->caller.principal, job.request_id);
    tiku_resource_job_info_clear(&job);
    if (!original) return TIKU_RESOURCE_OUTCOME_UNAVAILABLE;
    before = tiku_resource_registry_now(service->registry);
    if (!before || before > UINT64_MAX - (uint64_t)deadline) return TIKU_RESOURCE_UNAVAILABLE;
    choices[0] = acknowledgement("accepted"); choices[1] = acknowledgement("too_late"); choices[2] = acknowledgement("unsupported");
    value = tiku_resource_value_string(TIKU_RESOURCE_TEXT, target);
    if (!choices[0] || !choices[1] || !choices[2] || !value) {
        for (i = 0; i < 3; i++) tiku_msg_free(choices[i]);
        tiku_msg_free(value); return TIKU_RESOURCE_NO_MEMORY;
    }
    tiku_resource_registry_receipt_clear(receipt);
    receipt->request = value;
    strcpy(receipt->id, id); strcpy(receipt->principal, service->caller.principal);
    strcpy(receipt->operation, "cancel"); receipt->expected = expected;
    receipt->protocol_bound = 1; receipt->protocol_deadline = (unsigned)deadline;
    receipt->read_scopes = original->read_scopes; receipt->sensitive = original->sensitive;
    strcpy(receipt->provider_principal, original->provider_principal);
    receipt->pending = 1;
    error = tiku_resource_job_cancel(service->registry, &service->caller, target, &expected, &cancellation);
    receipt->pending = 0; receipt->completed = tiku_resource_registry_now(service->registry);
    if (!error) {
        receipt->protocol_result = choices[cancellation]; choices[cancellation] = NULL;
        if (!receipt->completed || receipt->completed >= before + (uint64_t)deadline) {
            error = TIKU_RESOURCE_UNKNOWN_OUTCOME; *effect = TIKU_RESOURCE_EFFECT_UNKNOWN;
        }
    } else if (error == TIKU_RESOURCE_UNKNOWN_OUTCOME) *effect = TIKU_RESOURCE_EFFECT_UNKNOWN;
    receipt->error = error; receipt->effect = *effect;
    for (i = 0; i < 3; i++) tiku_msg_free(choices[i]);
    if (receipt->protocol_result && !(*out = tiku_msg_copy(receipt->protocol_result))) {
        *effect = TIKU_RESOURCE_EFFECT_UNKNOWN; return TIKU_RESOURCE_UNKNOWN_OUTCOME;
    }
    return error;
}
