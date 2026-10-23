/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_resource_change.c - revision-checked controller changes and receipts.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_resource_registry_private.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

/** @brief Whether a fixed-size provider stamp is well formed. */
int tiku_resource_registry_stamp_valid(const tiku_resource_stamp_t *stamp)
{
    return stamp && memchr(stamp->provider, 0, sizeof stamp->provider) &&
        stamp->provider[0] && tiku_resource_id_valid(stamp->incarnation) && stamp->revision;
}

/** @brief Whether a request targets the provider's current revision. */
int tiku_resource_registry_matches(const resource_provider_t *provider, const tiku_resource_stamp_t *stamp)
{
    return !strcmp(provider->key, stamp->provider) &&
        !strcmp(provider->stamp.incarnation, stamp->incarnation) &&
        provider->stamp.revision == stamp->revision;
}

/** @brief Whether a caller can change this resource through its owner. */
int tiku_resource_registry_authorized(const resource_node_t *node, const tiku_resource_caller_t *caller, int invoke)
{
    uint64_t needed = node->spec.change_scopes |
        (invoke ? TIKU_RESOURCE_INVOKE : TIKU_RESOURCE_WRITE);
    if (!caller || !caller->authenticated || !tiku_resource_id_valid(caller->principal) ||
        (caller->scopes & needed) != needed) return 0;
    if (node->provider->principal[0] && strcmp(caller->principal, node->provider->principal) &&
        !(caller->scopes & TIKU_RESOURCE_CROSS_APP)) return 0;
    if (!node->provider->principal[0] && !(caller->scopes & TIKU_RESOURCE_HOST_CONTROL)) return 0;
    return !node->spec.sensitive || (caller->scopes & TIKU_RESOURCE_CONTENT);
}

/** @brief Compare optional snapshot parts. */
static int part_equal(const tiku_msg_t *a, const tiku_msg_t *b)
{
    return (!a && !b) || (a && b && tiku_resource_value_equal(a, b));
}

void tiku_resource_registry_install(tiku_resource_registry_t *registry, resource_node_t *node,
                                    tiku_resource_snapshot_t *value)
{
    int changed = node->spec.kind == TIKU_RESOURCE_ACTION || node->unavailable || strcmp(node->value.apply_state, value->apply_state) ||
        !part_equal(node->value.desired, value->desired) ||
        !part_equal(node->value.persisted, value->persisted) ||
        !part_equal(node->value.applied, value->applied);
    tiku_resource_snapshot_clear(&node->value);
    node->value = *value;
    memset(value, 0, sizeof *value);
    node->unavailable = 0;
    if (changed) {
        node->provider->stamp.revision++;
        registry->epoch++;
        tiku_resource_registry_emit(registry, node, 0);
    }
    node->value.stamp = node->provider->stamp;
}

/** @brief Monotonic milliseconds, or zero when the clock cannot be read. */
uint64_t tiku_resource_registry_now(tiku_resource_registry_t *registry)
{
    struct timespec time;
    if (registry->clock) return registry->clock(registry->clock_context);
    if (clock_gettime(CLOCK_MONOTONIC, &time)) return 0;
    return (uint64_t)time.tv_sec * 1000 + (uint64_t)time.tv_nsec / 1000000;
}

/** @brief Clear one retained outcome. */
void tiku_resource_registry_receipt_clear(resource_receipt_t *receipt)
{
    tiku_msg_free(receipt->request);
    tiku_msg_free(receipt->protocol_result);
    tiku_resource_snapshot_clear(&receipt->result);
    memset(receipt, 0, sizeof *receipt);
}

/** @brief Look up a retained request or select the oldest completed slot. */
resource_receipt_t *tiku_resource_registry_receipt(tiku_resource_registry_t *registry,
    const char *principal, const char *id, int *existing)
{
    resource_receipt_t *oldest = NULL;
    uint64_t now = tiku_resource_registry_now(registry);
    unsigned i;
    *existing = 0;
    for (i = 0; i < registry->limits.receipts; i++) {
        resource_receipt_t *item = &registry->receipts[i];
        if (item->id[0] && !item->pending && now >= item->completed && now - item->completed >= 60000)
            tiku_resource_registry_receipt_clear(item);
        if (item->id[0] && !strcmp(item->principal, principal) && !strcmp(item->id, id)) {
            *existing = 1; return item;
        }
        if (!item->pending && (!oldest || !item->id[0] || (oldest->id[0] && item->completed < oldest->completed))) oldest = item;
    }
    return oldest;
}

tiku_resource_error_t tiku_resource_change(tiku_resource_registry_t *registry,
    const tiku_resource_caller_t *caller, const char *path,
    const tiku_resource_stamp_t *expected, const char *id,
    const tiku_msg_t *value, int invoke, tiku_resource_snapshot_t *out)
{
    resource_node_t *node;
    resource_provider_t *provider;
    resource_receipt_t *receipt;
    tiku_resource_snapshot_t result = {0};
    tiku_resource_error_t error;
    int existing;
    if (out) memset(out, 0, sizeof *out);
    if (!tiku_resource_registry_thread(registry) || !out || !tiku_resource_path_valid(path) ||
        !tiku_resource_registry_stamp_valid(expected) || !tiku_resource_id_valid(id) || (invoke != 0 && invoke != 1))
        return TIKU_RESOURCE_INVALID;
    if (registry->dispatching) return TIKU_RESOURCE_BUSY;
    node = tiku_resource_registry_node(registry, path);
    if (!node || !node->provider || !tiku_resource_registry_visible(node, caller)) return TIKU_RESOURCE_NOT_FOUND;
    if ((!invoke && node->spec.kind != TIKU_RESOURCE_SETTING) ||
        (invoke && node->spec.kind != TIKU_RESOURCE_ACTION)) return TIKU_RESOURCE_READ_ONLY;
    if (!tiku_resource_registry_authorized(node, caller, invoke)) return TIKU_RESOURCE_DENIED;
    if (node->unavailable) return TIKU_RESOURCE_UNAVAILABLE;
    if (node->provider->asynchronous) return TIKU_RESOURCE_UNSUPPORTED;
    if (tiku_resource_jobs_request(registry, caller->principal, id)) return TIKU_RESOURCE_ID_REUSED;
    error = tiku_resource_registry_validate(&node->spec, value);
    if (error) return error;
    receipt = tiku_resource_registry_receipt(registry, caller->principal, id, &existing);
    if (!receipt) return TIKU_RESOURCE_BUSY;
    if (existing) {
        if (receipt->operation[0] || strcmp(receipt->path, path) || receipt->invoke != invoke ||
            strcmp(receipt->expected.provider, expected->provider) ||
            strcmp(receipt->expected.incarnation, expected->incarnation) ||
            receipt->expected.revision != expected->revision ||
            !tiku_resource_value_equal(receipt->request, value)) return TIKU_RESOURCE_ID_REUSED;
        if (!tiku_resource_registry_receipt_visible(receipt, caller)) return TIKU_RESOURCE_DENIED;
        if (!tiku_resource_registry_copy(out, &receipt->result)) return TIKU_RESOURCE_NO_MEMORY;
        return receipt->error;
    }
    provider = node->provider;
    if (!tiku_resource_registry_matches(provider, expected)) return TIKU_RESOURCE_STALE;
    if (provider->stamp.revision == UINT64_MAX || registry->epoch == UINT64_MAX) return TIKU_RESOURCE_UNAVAILABLE;
    if (provider->controller.validate) {
        registry->dispatching = 1;
        error = provider->controller.validate(provider->owner, path, value, invoke);
        registry->dispatching = 0;
        if (error) return error;
    }
    tiku_resource_registry_receipt_clear(receipt);
    receipt->request = tiku_msg_copy(value);
    if (!receipt->request) return TIKU_RESOURCE_NO_MEMORY;
    strcpy(receipt->id, id); strcpy(receipt->principal, caller->principal);
    receipt->read_scopes = node->spec.read_scopes;
    receipt->sensitive = node->spec.sensitive;
    strcpy(receipt->provider_principal, provider->principal);
    strcpy(receipt->path, path); receipt->expected = *expected; receipt->invoke = invoke;
    registry->dispatching = 1;
    error = provider->controller.apply(provider->owner, path, value, invoke, &result);
    registry->dispatching = 0;
    if (result.desired || !error || error == TIKU_RESOURCE_UNKNOWN_OUTCOME) {
        if (tiku_resource_registry_snapshot_check(node, &result)) {
            tiku_resource_snapshot_clear(&result);
            tiku_resource_registry_uncertain(registry, node);
            error = TIKU_RESOURCE_UNKNOWN_OUTCOME;
        } else tiku_resource_registry_install(registry, node, &result);
    }
    tiku_resource_snapshot_clear(&result);
    receipt->error = error;
    receipt->completed = tiku_resource_registry_now(registry);
    if (!tiku_resource_registry_copy(&receipt->result, &node->value)) receipt->error = TIKU_RESOURCE_UNKNOWN_OUTCOME;
    receipt->effect = receipt->error == TIKU_RESOURCE_UNKNOWN_OUTCOME ? TIKU_RESOURCE_EFFECT_UNKNOWN :
        provider->stamp.revision != expected->revision ? TIKU_RESOURCE_EFFECT_PARTIAL : TIKU_RESOURCE_EFFECT_NONE;
    receipt->result.stamp = provider->stamp;
    if (!tiku_resource_registry_copy(out, &receipt->result)) return TIKU_RESOURCE_UNKNOWN_OUTCOME;
    return receipt->error;
}

tiku_resource_error_t tiku_resource_publish(tiku_resource_registry_t *registry,
    const tiku_resource_stamp_t *expected, const char *path,
    const tiku_resource_snapshot_t *value, tiku_resource_stamp_t *out)
{
    resource_node_t *node;
    tiku_resource_snapshot_t copy = {0};
    tiku_resource_error_t error;
    if (!tiku_resource_registry_thread(registry) || !tiku_resource_registry_stamp_valid(expected) ||
        !tiku_resource_path_valid(path) || !value || !out) return TIKU_RESOURCE_INVALID;
    if (registry->dispatching) return TIKU_RESOURCE_BUSY;
    node = tiku_resource_registry_node(registry, path);
    if (!node || !node->provider) return TIKU_RESOURCE_NOT_FOUND;
    if (!tiku_resource_registry_matches(node->provider, expected)) return TIKU_RESOURCE_STALE;
    if (node->provider->stamp.revision == UINT64_MAX || registry->epoch == UINT64_MAX)
        return TIKU_RESOURCE_UNAVAILABLE;
    error = tiku_resource_registry_snapshot_check(node, value);
    if (error) return error;
    if (!tiku_resource_registry_copy(&copy, value)) return TIKU_RESOURCE_NO_MEMORY;
    tiku_resource_registry_install(registry, node, &copy);
    *out = node->provider->stamp;
    return TIKU_RESOURCE_OK;
}

int tiku_resource_registry_receipt_visible(const resource_receipt_t *receipt, const tiku_resource_caller_t *caller)
{
    if (!caller || !caller->authenticated || strcmp(receipt->principal, caller->principal) ||
        (caller->scopes & receipt->read_scopes) != receipt->read_scopes) return 0;
    return !receipt->sensitive || ((caller->scopes & TIKU_RESOURCE_CONTENT) &&
        (!receipt->provider_principal[0] || !strcmp(receipt->provider_principal, caller->principal) ||
         (caller->scopes & TIKU_RESOURCE_CROSS_APP)));
}

void tiku_resource_registry_uncertain(tiku_resource_registry_t *registry, resource_node_t *node)
{
    node->unavailable = 1;
    if (node->provider->stamp.revision != UINT64_MAX) node->provider->stamp.revision++;
    if (registry->epoch != UINT64_MAX) registry->epoch++;
    tiku_resource_registry_emit(registry, node, 0);
}
