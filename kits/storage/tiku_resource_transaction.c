/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_resource_transaction.c - prepared atomic publication and retained outcomes.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_resource_registry_private.h"
#include "tiku_resource_provider_wire.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

tiku_resource_error_t tiku_resource_register_atomic(tiku_resource_registry_t *registry,
    const char *key, const char *prefix, const char *principal,
    const tiku_resource_initial_t *initial, size_t count,
    const tiku_resource_atomic_controller_t *controller, void *owner, tiku_resource_stamp_t *out)
{
    tiku_resource_error_t error;
    if (!controller || !controller->prepare || !controller->commit || !controller->discard) return TIKU_RESOURCE_INVALID;
    error = tiku_resource_register(registry, key, prefix, principal, initial, count, &controller->single, owner, out);
    if (!error) tiku_resource_registry_provider(registry, key)->atomic = *controller;
    return error;
}

/** @brief Sort a group by canonical path before matching retained identities. */
static int compare(const void *a, const void *b)
{
    return strcmp(((const tiku_resource_replacement_t *)a)->path, ((const tiku_resource_replacement_t *)b)->path);
}

/** @brief Compare canonical list entries using typed-value equality. */
static int same(const tiku_msg_t *a, const tiku_msg_t *b)
{
    int i, count = tiku_msg_count(a, "entry");
    if (count != tiku_msg_count(b, "entry")) return 0;
    for (i = 0; i < count; i++) {
        tiku_msg_t *left = tiku_msg_find_message(a, "entry", i), *right = tiku_msg_find_message(b, "entry", i);
        tiku_msg_t *lv = tiku_msg_find_message(left, "value", 0), *rv = tiku_msg_find_message(right, "value", 0);
        const char *lp = tiku_msg_find_string(left, "path", 0), *rp = tiku_msg_find_string(right, "path", 0);
        const char *ld = tiku_msg_find_string(left, "durability", 0), *rd = tiku_msg_find_string(right, "durability", 0);
        int equal = lp && rp && ld && rd && !strcmp(lp, rp) && !strcmp(ld, rd) && tiku_resource_value_equal(lv, rv);
        tiku_msg_free(left); tiku_msg_free(right); tiku_msg_free(lv); tiku_msg_free(rv);
        if (!equal) return 0;
    }
    return 1;
}

/** @brief Require a bounded result including its outer reply or error envelope. */
static tiku_resource_error_t encodable(const tiku_msg_t *message)
{
    size_t size = tiku_msg_flat_size(message);
    void *bytes;
    tiku_resource_error_t error;
    if (!size || size > TIKU_RESOURCE_FRAME_MAX - 2048) return TIKU_RESOURCE_LIMIT;
    bytes = malloc(size);
    if (!bytes) return TIKU_RESOURCE_NO_MEMORY;
    error = tiku_msg_flatten(message, bytes, size, NULL) ?
        tiku_resource_message_check_nested(bytes, size, 2) : TIKU_RESOURCE_INVALID;
    free(bytes); return error;
}

/** @brief Construct a planned group result at the revision reserved for commit. */
static tiku_msg_t *result_message(const resource_provider_t *provider, resource_node_t *const *nodes,
    const tiku_resource_snapshot_t *planned, size_t count)
{
    tiku_msg_t *result = tiku_msg_new(0);
    char revision[21]; size_t i;
    snprintf(revision, sizeof revision, "%llu", (unsigned long long)(provider->stamp.revision + 1));
    if (!result || !tiku_msg_add_string(result, "provider", provider->key) ||
        !tiku_msg_add_string(result, "incarnation", provider->stamp.incarnation) ||
        !tiku_msg_add_string(result, "revision", revision) || !tiku_msg_add_int32(result, "count", (int32_t)count)) goto fail;
    for (i = 0; i < count; i++) {
        tiku_msg_t *entry = tiku_resource_snapshot_message(&planned[i]);
        int ok = entry && tiku_msg_add_string(entry, "path", nodes[i]->entry.path) && tiku_msg_add_message(result, "entry", entry);
        tiku_msg_free(entry); if (!ok) goto fail;
    }
    return result;
fail:
    tiku_msg_free(result); return NULL;
}

/** @brief Publish a prepared group or mark its prior values unavailable once. */
static void publish(tiku_resource_registry_t *registry, resource_provider_t *provider,
    resource_node_t *const *nodes, tiku_resource_snapshot_t *planned, size_t count, int uncertain)
{
    size_t i;
    provider->stamp.revision++; registry->epoch++;
    for (i = 0; i < count; i++) {
        resource_node_t *node = nodes[i];
        if (!uncertain) {
            tiku_resource_snapshot_clear(&node->value); node->value = planned[i];
            memset(&planned[i], 0, sizeof planned[i]);
        }
        node->unavailable = uncertain; node->value.stamp = provider->stamp;
    }
    tiku_resource_registry_emit_group(registry, nodes, count);
}

tiku_resource_error_t tiku_resource_transaction_limited(tiku_resource_registry_t *registry,
    const tiku_resource_caller_t *caller, const tiku_resource_stamp_t *expected,
    const char *id, const tiku_resource_replacement_t *input, size_t count,
    unsigned deadline_ms, const tiku_resource_protocol_limits_t *limits, tiku_msg_t **out)
{
    tiku_resource_replacement_t items[TIKU_RESOURCE_TRANSACTION_MAX];
    resource_node_t *nodes[TIKU_RESOURCE_TRANSACTION_MAX];
    tiku_resource_snapshot_t planned[TIKU_RESOURCE_TRANSACTION_MAX] = {0};
    resource_provider_t *provider;
    resource_receipt_t *receipt;
    tiku_msg_t *request, *result = NULL, *retained = NULL;
    tiku_resource_error_t error;
    uint64_t before, now;
    size_t i;
    void *token = NULL;
    int existing, prepared = 0, committed = 0;
    if (out) *out = NULL;
    if (!tiku_resource_registry_thread(registry) || !out || !tiku_resource_registry_stamp_valid(expected) ||
        !tiku_resource_id_valid(id) || !deadline_ms || deadline_ms > 30000) return TIKU_RESOURCE_INVALID;
    if (registry->dispatching) return TIKU_RESOURCE_BUSY;
    if (!caller || !caller->authenticated || !tiku_resource_id_valid(caller->principal)) return TIKU_RESOURCE_DENIED;
    before = tiku_resource_registry_now(registry);
    if (!before || before > UINT64_MAX - deadline_ms) return TIKU_RESOURCE_UNAVAILABLE;
    if(limits && (!tiku_resource_protocol_limits_valid(limits) || count>limits->transaction_settings))return TIKU_RESOURCE_LIMIT;
    error = tiku_resource_replacements_check(input, count);
    if (error) return error;
    memcpy(items, input, count * sizeof *items); qsort(items, count, sizeof *items, compare);
    request = tiku_resource_replacements_message(items, count);
    if (!request) return TIKU_RESOURCE_NO_MEMORY;
    error = encodable(request);
    if(!error && limits)error=tiku_resource_message_limits(request,limits,2);
    if (error) { tiku_msg_free(request); return error; }
    if (tiku_resource_jobs_request(registry, caller->principal, id)) { tiku_msg_free(request); return TIKU_RESOURCE_ID_REUSED; }
    receipt = tiku_resource_registry_receipt(registry, caller->principal, id, &existing);
    if (!receipt) { tiku_msg_free(request); return TIKU_RESOURCE_BUSY; }
    if (existing) {
        int equal = !strcmp(receipt->operation, "transaction") && !strcmp(receipt->expected.provider, expected->provider) &&
            !strcmp(receipt->expected.incarnation, expected->incarnation) && receipt->expected.revision == expected->revision &&
            receipt->protocol_deadline == deadline_ms && same(request, receipt->request);
        tiku_msg_free(request);
        if (!equal) return TIKU_RESOURCE_ID_REUSED;
        if (!tiku_resource_registry_receipt_visible(receipt, caller)) return TIKU_RESOURCE_DENIED;
        if (receipt->pending) return TIKU_RESOURCE_BUSY;
        if (receipt->protocol_result && !(*out = tiku_msg_copy(receipt->protocol_result))) return TIKU_RESOURCE_NO_MEMORY;
        return receipt->error;
    }
    provider = tiku_resource_registry_provider(registry, expected->provider);
    error = TIKU_RESOURCE_NOT_FOUND;
    if (!provider) goto reject;
    for (i = 0; i < count; i++) {
        resource_node_t *node = tiku_resource_registry_node(registry, items[i].path);
        error = TIKU_RESOURCE_NOT_FOUND;
        if (!node || !node->provider || !tiku_resource_registry_visible(node, caller)) goto reject;
        error = TIKU_RESOURCE_INVALID;
        if (node->provider != provider) goto reject;
        error = TIKU_RESOURCE_READ_ONLY;
        if (node->spec.kind != TIKU_RESOURCE_SETTING) goto reject;
        error = TIKU_RESOURCE_DENIED;
        if (!tiku_resource_registry_authorized(node, caller, 0)) goto reject;
        error = TIKU_RESOURCE_UNAVAILABLE; if (node->unavailable) goto reject;
        error = TIKU_RESOURCE_UNSUPPORTED; if (strcmp(node->spec.persistence, items[i].durability)) goto reject;
        error = tiku_resource_registry_validate(&node->spec, items[i].value); if (error) goto reject;
        nodes[i] = node;
    }
    error = TIKU_RESOURCE_ATOMIC_UNSUPPORTED;
    if (!provider->atomic.commit || provider->asynchronous) goto reject;
    error = TIKU_RESOURCE_STALE; if (!tiku_resource_registry_matches(provider, expected)) goto reject;
    error = TIKU_RESOURCE_UNAVAILABLE;
    if (provider->stamp.revision == UINT64_MAX || registry->epoch == UINT64_MAX) goto reject;
    tiku_resource_registry_receipt_clear(receipt);
    receipt->request = request; receipt->expected = *expected; receipt->pending = 1;
    strcpy(receipt->operation, "transaction"); strcpy(receipt->id, id); strcpy(receipt->principal, caller->principal);
    strcpy(receipt->path, provider->prefix); strcpy(receipt->provider_principal, provider->principal);
    receipt->protocol_deadline = deadline_ms; receipt->protocol_bound = 1;
    for (i = 0; i < count; i++) {
        receipt->read_scopes |= nodes[i]->spec.read_scopes; receipt->sensitive |= nodes[i]->spec.sensitive;
    }
    for (i = 0; i < count; i++) {
        if (provider->controller.validate) {
            registry->dispatching = 1;
            error = provider->controller.validate(provider->owner, items[i].path, items[i].value, 0);
            registry->dispatching = 0; if (error) goto finish;
        }
    }
    registry->dispatching = 1; prepared = 1;
    error = provider->atomic.prepare(provider->owner, items, count, planned, &token);
    registry->dispatching = 0;
    if (error == TIKU_RESOURCE_PENDING) error = TIKU_RESOURCE_INVALID;
    if (error) goto finish;
    for (i = 0; i < count; i++) {
        error = tiku_resource_registry_snapshot_check(nodes[i], &planned[i]); if (error) goto finish;
    }
    result = result_message(provider, nodes, planned, count);
    error = result ? encodable(result) : TIKU_RESOURCE_NO_MEMORY; if (error) goto finish;
    if(limits){
        error=tiku_resource_message_limits(result,limits,2);
        if(!error && tiku_msg_flat_size(result)>limits->frame_bytes-2048)error=TIKU_RESOURCE_LIMIT;
        if(error)goto finish;
    }
    retained = tiku_msg_copy(result); if (!retained) { error = TIKU_RESOURCE_NO_MEMORY; goto finish; }
    now = tiku_resource_registry_now(registry);
    error = !now ? TIKU_RESOURCE_UNAVAILABLE : now >= before + deadline_ms ? TIKU_RESOURCE_DEADLINE : TIKU_RESOURCE_OK;
    if (error) goto finish;
    registry->dispatching = 1;
    error = provider->atomic.commit(provider->owner, token); committed = 1;
    registry->dispatching = 0;
    if (error == TIKU_RESOURCE_PENDING) error = TIKU_RESOURCE_UNKNOWN_OUTCOME;
    if (!error) {
        publish(registry, provider, nodes, planned, count, 0);
        receipt->protocol_result = retained; retained = NULL; *out = result; result = NULL;
        receipt->effect = TIKU_RESOURCE_EFFECT_PARTIAL;
    }
finish:
    if (error == TIKU_RESOURCE_UNKNOWN_OUTCOME) {
        publish(registry, provider, nodes, planned, count, 1); receipt->effect = TIKU_RESOURCE_EFFECT_UNKNOWN;
    }
    if (prepared) {
        registry->dispatching = 1; provider->atomic.discard(provider->owner, token); registry->dispatching = 0;
    }
    for (i = 0; i < count; i++) tiku_resource_snapshot_clear(&planned[i]);
    tiku_msg_free(result); tiku_msg_free(retained);
    receipt->error = error; receipt->pending = 0;
    now = tiku_resource_registry_now(registry); receipt->completed = now ? now : before;
    return committed && !error && (!now || now >= before + deadline_ms) ? TIKU_RESOURCE_UNKNOWN_OUTCOME : error;
reject:
    tiku_msg_free(request); return error;
}

tiku_resource_error_t tiku_resource_transaction(tiku_resource_registry_t *registry,
    const tiku_resource_caller_t *caller,const tiku_resource_stamp_t *expected,
    const char *id,const tiku_resource_replacement_t *items,size_t count,unsigned deadline,tiku_msg_t **out)
{
    return tiku_resource_transaction_limited(registry,caller,expected,id,items,count,deadline,NULL,out);
}
