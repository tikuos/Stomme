/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_resource_remote.c - connection-owned application providers and dispatch.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_resource_service_private.h"
#include "tiku_resource_provider_wire.h"
#include "tiku_resource_stream.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define REMOTE_RECEIPTS 4

/** @brief One copied request and correlated connection-local outcome. */
typedef struct {
    char id[TIKU_RESOURCE_ID_MAX];
    tiku_msg_t *request;
    tiku_msg_t *reply;
    uint64_t expires;
} remote_receipt_t;

/** @brief One host-created dispatch identity, separate from public job IDs. */
typedef struct {
    char id[TIKU_RESOURCE_ID_MAX], job[TIKU_RESOURCE_ID_MAX];
    tiku_resource_stamp_t expected;
    tiku_msg_t *event;
    int sent, cancel;
} remote_dispatch_t;

/** @brief An assigned namespace and bounded owner records on the registry thread. */
typedef struct resource_remote {
    struct resource_remote *next;
    tiku_resource_service_t *service;
    char key[TIKU_RESOURCE_KEY_MAX], prefix[TIKU_RESOURCE_PATH_MAX];
    tiku_resource_stamp_t stamp;
    remote_receipt_t receipts[REMOTE_RECEIPTS];
    remote_dispatch_t dispatches[RESOURCE_JOBS_ACTIVE_MAX];
    unsigned next_receipt;
    tiku_resource_initial_t *staged;
    size_t staged_count,staged_total,staged_bytes;
    uint64_t staged_deadline;
    char catalog[TIKU_RESOURCE_ID_MAX];
    unsigned char *upload;
    size_t upload_offset,upload_total;
    char transfer[TIKU_RESOURCE_ID_MAX],upload_path[TIKU_RESOURCE_PATH_MAX];
    tiku_resource_stamp_t upload_stamp;
    uint64_t upload_deadline;
} resource_remote_t;

/** @brief Release an unpublished catalog and its copied descriptors. */
static void stage_clear(resource_remote_t *remote)
{
    for (size_t i = 0; i < remote->staged_count; i++)
        tiku_resource_initial_clear(&remote->staged[i]);
    free(remote->staged);
    remote->staged = NULL;
    remote->staged_count = remote->staged_total = remote->staged_bytes = 0;
    remote->catalog[0]=0;remote->staged_deadline=0;
}
/** @brief Release unpublished stream bytes without changing a public snapshot. */
static void upload_clear(resource_remote_t *remote)
{
    free(remote->upload);
    remote->upload = NULL;
    remote->upload_offset = remote->upload_total = 0;
    remote->transfer[0] = remote->upload_path[0] = 0;
    remote->upload_deadline = 0;
    memset(&remote->upload_stamp, 0, sizeof remote->upload_stamp);
}
/** @brief Find a job retained by the shared engine without invoking callbacks. */
static resource_job_t *job_find(resource_remote_t *remote, const char *id)
{
    tiku_resource_registry_t *registry = remote->service->registry;
    unsigned i;
    if (registry->jobs) for (i = 0; i < RESOURCE_JOBS_MAX; i++)
        if (!strcmp(registry->jobs[i].info.id, id)) return &registry->jobs[i];
    return NULL;
}

/** @brief Whether a job can still receive an owner completion. */
static int running(const resource_job_t *job)
{
    return job && (job->info.state == TIKU_RESOURCE_JOB_RUNNING ||
                   job->info.state == TIKU_RESOURCE_JOB_AWAITING_CONFIRMATION);
}

/** @brief Find this connection's live provider incarnation. */
static resource_provider_t *provider(resource_remote_t *remote)
{
    resource_provider_t *owner = tiku_resource_registry_provider(
        remote->service->registry, remote->key);
    return owner && owner->owner == remote &&
                   !strcmp(owner->stamp.incarnation, remote->stamp.incarnation)
               ? owner
               : NULL;
}

/** @brief Release an undelivered event and its dispatch identity. */
static void dispatch_clear(remote_dispatch_t *dispatch)
{
    tiku_msg_free(dispatch->event); memset(dispatch, 0, sizeof *dispatch);
}

/** @brief Retire dispatch identities whose jobs have reached an outcome. */
static void collect(resource_remote_t *remote)
{
    unsigned i;
    for (i = 0; i < RESOURCE_JOBS_ACTIVE_MAX; i++) {
        remote_dispatch_t *dispatch = &remote->dispatches[i];
        if (dispatch->id[0] && !running(job_find(remote, dispatch->job)))
            dispatch_clear(dispatch);
    }
}

/** @brief Validate a typed envelope before its application validates the live model. */
static tiku_resource_error_t validate_envelope(void *context, const char *path,
                                               const tiku_msg_t *value,
                                               int invoke)
{
    (void)context;
    (void)path;
    (void)invoke;
    return tiku_resource_value_check(value);
}

/** @brief Queue one bounded controller event before returning pending. */
static tiku_resource_error_t start(void *opaque, const char *id,
                                   const char *path, const tiku_msg_t *value,
                                   int invoke, tiku_resource_snapshot_t *out)
{
    resource_remote_t *remote = opaque;
    remote_dispatch_t *slot = NULL;
    resource_job_t *job = job_find(remote, id);
    tiku_msg_t *event, *expected, *origin;
    uint64_t now = tiku_resource_registry_now(remote->service->registry),
             remaining;
    unsigned i;
    int ok;
    (void)out;
    collect(remote);
    if (!running(job) || !now || now >= job->execution_deadline)
        return TIKU_RESOURCE_DEADLINE;
    for (i = 0; i < RESOURCE_JOBS_ACTIVE_MAX; i++)
        if (!remote->dispatches[i].id[0]) {
            slot = &remote->dispatches[i];
            break;
        }
    if (!slot) return TIKU_RESOURCE_LIMIT;
    if (tiku_resource_new_id(slot->id)) return TIKU_RESOURCE_UNAVAILABLE;
    remaining = job->execution_deadline - now;
    event = tiku_msg_new(TIKU_RESOURCE_MESSAGE_CODE);
    expected = tiku_msg_new(0);
    origin = tiku_msg_new(0);
    ok = event && expected && origin &&
         tiku_resource_service_stamp(expected, &job->expected) &&
         tiku_msg_add_message(event, "expected", expected) &&
         tiku_msg_add_string(event, "kind", "provider-dispatch") &&
         tiku_msg_add_string(event, "session",
                             remote->service->registry->session) &&
         tiku_msg_add_string(event, "dispatch", slot->id) &&
         tiku_msg_add_string(event, "job", id) &&
         tiku_msg_add_string(event, "path", path) &&
         tiku_msg_add_bool(event, "invoke", invoke) &&
         tiku_msg_add_message(event, "value", value) &&
         tiku_msg_add_int32(event, "deadline_ms", (int32_t)remaining) &&
         tiku_msg_add_string(event, "origin_request_id",
                             job->info.request_id) &&
         tiku_msg_add_string(origin, "principal", job->caller.principal) &&
         tiku_msg_add_int64(
             origin, "scopes",
             (int64_t)(job->caller.scopes &
                       (TIKU_RESOURCE_READ | TIKU_RESOURCE_WRITE |
                        TIKU_RESOURCE_INVOKE | TIKU_RESOURCE_CONTENT))) &&
         tiku_msg_add_message(event, "origin", origin);
    tiku_msg_free(expected); tiku_msg_free(origin);
    if (!ok || !tiku_resource_service_encodable(event)) {
        tiku_msg_free(event);
        dispatch_clear(slot);
        return ok ? TIKU_RESOURCE_LIMIT : TIKU_RESOURCE_NO_MEMORY;
    }
    slot->event = event; slot->expected = job->expected; strcpy(slot->job, id);
    return TIKU_RESOURCE_PENDING;
}

/** @brief Record a cancellation request for delivery on the provider connection. */
static tiku_resource_cancel_t cancel(void *opaque, const char *job)
{
    resource_remote_t *remote = opaque;
    unsigned i;
    for (i = 0; i < RESOURCE_JOBS_ACTIVE_MAX; i++)
        if (!strcmp(remote->dispatches[i].job, job)) {
            remote->dispatches[i].cancel = 1;
            return TIKU_RESOURCE_CANCEL_ACCEPTED;
        }
    return TIKU_RESOURCE_CANCEL_TOO_LATE;
}

tiku_resource_error_t
tiku_resource_service_provider_enable(tiku_resource_service_t *service)
{
    resource_remote_t *remote, *at;
    unsigned count = 0;
    if (!service || !tiku_resource_registry_thread(service->registry))
        return TIKU_RESOURCE_INVALID;
    if (service->registry->dispatching) return TIKU_RESOURCE_BUSY;
    if (!(service->features & TIKU_RESOURCE_FEATURE_PROVIDER) ||
        !(service->caller.scopes & TIKU_RESOURCE_PROVIDER))
        return TIKU_RESOURCE_DENIED;
    if (service->remote) return TIKU_RESOURCE_OK;
    for (at = service->registry->remote_providers; at; at = at->next) {
        if (!strcmp(at->service->caller.principal, service->caller.principal))
            return TIKU_RESOURCE_BUSY;
        count++;
    }
    if (count >= service->registry->limits.providers)
        return TIKU_RESOURCE_LIMIT;
    remote = calloc(1, sizeof *remote);
    if (!remote) return TIKU_RESOURCE_NO_MEMORY;
    remote->service = service;
    snprintf(remote->prefix, sizeof remote->prefix, "/apps/%s",
             service->caller.principal);
    snprintf(remote->key, sizeof remote->key, "app.%s",
             service->caller.principal);
    remote->next = service->registry->remote_providers;
    service->registry->remote_providers = remote; service->remote = remote;
    return TIKU_RESOURCE_OK;
}

void tiku_resource_remote_close(tiku_resource_service_t *service)
{
    resource_remote_t *remote = service->remote, **at;
    resource_provider_t *owner;
    unsigned i;
    if (!remote) return;
    owner = provider(remote);
    if (owner) tiku_resource_unregister(service->registry, &owner->stamp);
    for (at = &service->registry->remote_providers; *at && *at != remote;
         at = &(*at)->next) {
    }
    if (*at) *at = remote->next;
    for (i = 0; i < RESOURCE_JOBS_ACTIVE_MAX; i++)
        dispatch_clear(&remote->dispatches[i]);
    for (i = 0; i < REMOTE_RECEIPTS; i++) {
        tiku_msg_free(remote->receipts[i].request);
        tiku_msg_free(remote->receipts[i].reply);
    }
    upload_clear(remote);
    stage_clear(remote);
    free(remote);
    service->remote = NULL;
}

/** @brief Encode the assigned namespace and its current registration state. */
static tiku_msg_t *status(resource_remote_t *remote)
{
    tiku_msg_t *result = tiku_msg_new(0);
    resource_provider_t *owner = provider(remote);
    if (result && tiku_msg_add_string(result, "prefix", remote->prefix) &&
        tiku_msg_add_bool(result, "registered", owner != NULL) &&
        (owner ? tiku_resource_service_stamp(result, &owner->stamp)
               : tiku_msg_add_string(result, "provider", remote->key)))
        return result;
    tiku_msg_free(result); return NULL;
}

/** @brief Atomically register a private subtree with host-assigned access policy. */
static tiku_resource_error_t registration(resource_remote_t *remote,
                                          const tiku_msg_t *request)
{
    tiku_msg_t *resources = tiku_msg_find_message(request, "resources", 0);
    tiku_resource_initial_t *initial = NULL;
    tiku_resource_async_controller_t controller = {
        .validate = validate_envelope, .start = start, .cancel = cancel};
    tiku_resource_error_t error = TIKU_RESOURCE_INVALID;
    int count = tiku_msg_count(resources, "entry"), i;
    if (provider(remote)) {
        tiku_msg_free(resources);
        return TIKU_RESOURCE_BUSY;
    }
    if (!resources || tiku_msg_what(resources) ||
        tiku_msg_field_count(resources) != 1 ||
        !tiku_msg_has(resources, "entry", TIKU_MSG_T_MESSAGE) || count < 1 ||
        count > (int)remote->service->limits.provider_resources)
        goto done;
    initial = calloc((size_t)count, sizeof *initial);
    if (!initial) { error = TIKU_RESOURCE_NO_MEMORY; goto done; }
    for (i = 0; i < count; i++) {
        tiku_msg_t *entry = tiku_msg_find_message(resources, "entry", i);
        error = tiku_resource_initial_decode(entry, &initial[i]);
        tiku_msg_free(entry);
        if (error) goto done;
        if (!tiku_resource_registry_below(initial[i].spec.path,
                                          remote->prefix) ||
            !strcmp(initial[i].spec.path, remote->prefix)) {
            error = TIKU_RESOURCE_DENIED; goto done;
        }
        if ((initial[i].spec.kind == TIKU_RESOURCE_STREAM &&
             ((remote->service->features &
               (TIKU_RESOURCE_FEATURE_STREAM |
                TIKU_RESOURCE_FEATURE_PROVIDER_CATALOG)) !=
              (TIKU_RESOURCE_FEATURE_STREAM |
               TIKU_RESOURCE_FEATURE_PROVIDER_CATALOG))) ||
            (initial[i].spec.kind == TIKU_RESOURCE_COLLECTION &&
             ((remote->service->features &
               (TIKU_RESOURCE_FEATURE_COLLECTION |
                TIKU_RESOURCE_FEATURE_PROVIDER_CATALOG)) !=
              (TIKU_RESOURCE_FEATURE_COLLECTION |
               TIKU_RESOURCE_FEATURE_PROVIDER_CATALOG)))) {
            error = TIKU_RESOURCE_UNSUPPORTED;
            goto done;
        }
        initial[i].spec.read_scopes =
            TIKU_RESOURCE_READ | TIKU_RESOURCE_CONTENT;
        initial[i].spec.change_scopes =
            initial[i].spec.kind == TIKU_RESOURCE_ACTION ? TIKU_RESOURCE_INVOKE
                                                         : TIKU_RESOURCE_WRITE;
        initial[i].spec.sensitive = 1;
        initial[i].spec.controller = "local-application";
    }
    error = tiku_resource_register_async(
        remote->service->registry, remote->key, remote->prefix,
        remote->service->caller.principal, initial, (size_t)count, &controller,
        remote, &remote->stamp);
done:
    if (initial) {
        for (i = 0; i < count; i++)
            tiku_resource_initial_clear(&initial[i]);
        free(initial);
    }
    tiku_msg_free(resources); return error;
}

/** @brief Stage bounded descriptor pages and atomically publish a complete catalog. */
static tiku_resource_error_t catalog_request(resource_remote_t *remote,
                                             const tiku_msg_t *request)
{
    const char *op = tiku_msg_find_string(request, "op", 0),
               *catalog = tiku_msg_find_string(request, "catalog", 0);
    uint64_t now=tiku_resource_registry_now(remote->service->registry);
    if(remote->staged&&now>=remote->staged_deadline)stage_clear(remote);
    if(provider(remote))return TIKU_RESOURCE_BUSY;
    if(!strcmp(op,"provider-abort")) {
        if (remote->staged && strcmp(remote->catalog, catalog))
            return TIKU_RESOURCE_OWNER_LOST;
        stage_clear(remote);return TIKU_RESOURCE_OK;
    }
    if(!strcmp(op,"provider-commit")) {
        if (!remote->staged || strcmp(remote->catalog, catalog))
            return TIKU_RESOURCE_OWNER_LOST;
        if (remote->staged_count != remote->staged_total)
            return TIKU_RESOURCE_INVALID;
        const tiku_resource_async_controller_t controller = {
            .validate = validate_envelope, .start = start, .cancel = cancel};
        tiku_resource_error_t error = tiku_resource_register_async(
            remote->service->registry, remote->key, remote->prefix,
            remote->service->caller.principal, remote->staged,
            remote->staged_count, &controller, remote, &remote->stamp);
        if(!error)stage_clear(remote);
        return error;
    }
    int32_t offset, total;
    tiku_msg_t *resources = tiku_msg_find_message(request, "resources", 0);
    int count = tiku_msg_count(resources, "entry");
    tiku_resource_error_t error = TIKU_RESOURCE_INVALID;
    if (!tiku_msg_find_int32(request, "offset", 0, &offset) ||
        !tiku_msg_find_int32(request, "total", 0, &total) || !resources ||
        tiku_msg_what(resources) || tiku_msg_field_count(resources) != 1 ||
        !tiku_msg_has(resources, "entry", TIKU_MSG_T_MESSAGE) || count < 1 ||
        count > (int)remote->service->limits.provider_resources ||
        offset < 0 || total < 1 ||
        total > TIKU_RESOURCE_PROVIDER_CATALOG_MAX || count > total - offset)
        goto done;
    size_t bytes=tiku_msg_flat_size(resources);
    if(!remote->staged) {
        if(offset)goto done;
        remote->staged = calloc((size_t)total, sizeof *remote->staged);
        if (!remote->staged) {
            error = TIKU_RESOURCE_NO_MEMORY;
            goto done;
        }
        strcpy(remote->catalog,catalog);remote->staged_total=(size_t)total;
    }
    if(strcmp(remote->catalog,catalog)){error=TIKU_RESOURCE_BUSY;goto done;}
    if ((size_t)offset != remote->staged_count ||
        (size_t)total != remote->staged_total)
        goto done;
    if (!bytes || bytes > TIKU_RESOURCE_STREAM_BYTES - remote->staged_bytes) {
        error = TIKU_RESOURCE_LIMIT;
        goto done;
    }
    tiku_resource_initial_t batch[TIKU_RESOURCE_PROVIDER_RESOURCES_MAX]={0};
    for(int i=0;i<count;i++) {
        tiku_msg_t *entry = tiku_msg_find_message(resources, "entry", i);
        error = tiku_resource_initial_decode(entry, &batch[i]);
        tiku_msg_free(entry);
        if(error)break;
        if (!tiku_resource_registry_below(batch[i].spec.path,
                                          remote->prefix) ||
            !strcmp(batch[i].spec.path, remote->prefix)) {
            error = TIKU_RESOURCE_DENIED;
            break;
        }
        if ((batch[i].spec.kind == TIKU_RESOURCE_STREAM &&
             !(remote->service->features & TIKU_RESOURCE_FEATURE_STREAM)) ||
            (batch[i].spec.kind == TIKU_RESOURCE_COLLECTION &&
             !(remote->service->features &
               TIKU_RESOURCE_FEATURE_COLLECTION))) {
            error = TIKU_RESOURCE_UNSUPPORTED;
            break;
        }
        batch[i].spec.read_scopes = TIKU_RESOURCE_READ | TIKU_RESOURCE_CONTENT;
        batch[i].spec.sensitive = 1;
        batch[i].spec.change_scopes =
            batch[i].spec.kind == TIKU_RESOURCE_ACTION ? TIKU_RESOURCE_INVOKE
                                                       : TIKU_RESOURCE_WRITE;
        batch[i].spec.controller="local-application";
    }
    if(!error) {
        memcpy(remote->staged + offset, batch, (size_t)count * sizeof *batch);
        memset(batch, 0, sizeof batch);
        remote->staged_count += (size_t)count;
        remote->staged_bytes += bytes;
        remote->staged_deadline = now + 30000;
    }
    for(int i=0;i<count;i++)tiku_resource_initial_clear(&batch[i]);
 done:tiku_msg_free(resources);return error;
}

/** @brief Compare complete expected tuples without accepting another owner. */
static int stamp_equal(const tiku_resource_stamp_t *a,
                       const tiku_resource_stamp_t *b)
{
    return !strcmp(a->provider, b->provider) &&
           !strcmp(a->incarnation, b->incarnation) &&
           a->revision == b->revision;
}

/** @brief Publish one complete bounded stream after ordered, incarnation-bound upload. */
static tiku_resource_error_t
stream_request(resource_remote_t *remote, const tiku_msg_t *request,
               resource_provider_t *owner,
               const tiku_resource_stamp_t *expected)
{
    const char *path = tiku_msg_find_string(request, "path", 0),
               *transfer = tiku_msg_find_string(request, "transfer", 0);
    resource_node_t *node =
        tiku_resource_registry_node(remote->service->registry, path);
    uint64_t offset, total,
        now = tiku_resource_registry_now(remote->service->registry);
    size_t bytes = 0;
    const void *data=tiku_msg_find_blob(request,"data",0,&bytes);
    if (!node || node->provider != owner ||
        node->spec.kind != TIKU_RESOURCE_STREAM)
        return TIKU_RESOURCE_DENIED;
    if (!tiku_resource_registry_matches(owner, expected))
        return TIKU_RESOURCE_STALE;
    if (tiku_resource_counter_parse(tiku_msg_find_string(request, "offset", 0),
                                    &offset) ||
        tiku_resource_counter_parse(tiku_msg_find_string(request, "total", 0),
                                    &total) ||
        (!data && bytes) || !tiku_msg_has(request, "data", TIKU_MSG_T_BLOB) ||
        offset > total || total > TIKU_RESOURCE_STREAM_BYTES ||
        bytes > total - offset || bytes > TIKU_RESOURCE_STREAM_CHUNK)
        return TIKU_RESOURCE_INVALID;
    if(remote->upload&&now>=remote->upload_deadline)upload_clear(remote);
    if(!offset) {
        upload_clear(remote);
        remote->upload = malloc(total ? (size_t)total : 1);
        if (!remote->upload)
            return TIKU_RESOURCE_NO_MEMORY;
        remote->upload_total = (size_t)total;
        strcpy(remote->transfer, transfer);
        strcpy(remote->upload_path, path);
        remote->upload_stamp = *expected;
    }
    if (!remote->upload || strcmp(remote->transfer, transfer) ||
        strcmp(remote->upload_path, path) || total != remote->upload_total ||
        offset != remote->upload_offset ||
        !stamp_equal(&remote->upload_stamp, expected))
        return TIKU_RESOURCE_OWNER_LOST;
    if(bytes)memcpy(remote->upload+offset,data,bytes);
    remote->upload_offset+=bytes;remote->upload_deadline=now+30000;
    if(remote->upload_offset==remote->upload_total) {
        tiku_resource_error_t error = tiku_resource_stream_publish(
            remote->service->registry, expected, path, remote->upload,
            remote->upload_total, &remote->stamp);
        upload_clear(remote);return error;
    }
    return TIKU_RESOURCE_OK;
}

/** @brief Find a delivered live dispatch with the original expected tuple. */
static remote_dispatch_t *dispatch_find(resource_remote_t *remote,
                                        const tiku_msg_t *request,
                                        const tiku_resource_stamp_t *expected)
{
    const char *id = tiku_msg_find_string(request, "dispatch", 0);
    unsigned i;
    collect(remote);
    for (i = 0; i < RESOURCE_JOBS_ACTIVE_MAX; i++) {
        remote_dispatch_t *dispatch = &remote->dispatches[i];
        if (dispatch->sent && !strcmp(dispatch->id, id) &&
            stamp_equal(&dispatch->expected, expected))
            return dispatch;
    }
    return NULL;
}

/** @brief Parse a terminal owner outcome without permitting pending completion. */
static int outcome(const tiku_msg_t *request, tiku_resource_error_t *error,
                   tiku_resource_effect_t *effect)
{
    const char *code = tiku_msg_find_string(request, "code", 0),
               *effects = tiku_msg_find_string(request, "effect", 0);
    int i;
    if (!code || !effects) return 0;
    for (i = TIKU_RESOURCE_OK; i <= TIKU_RESOURCE_ATOMIC_UNSUPPORTED; i++)
        if (!strcmp(code, tiku_resource_error_name(i)))
            break;
    if (i > TIKU_RESOURCE_ATOMIC_UNSUPPORTED || i == TIKU_RESOURCE_PENDING)
        return 0;
    *error = i;
    if (!strcmp(effects, "none")) *effect = TIKU_RESOURCE_EFFECT_NONE;
    else if (!strcmp(effects, "partial"))
        *effect = TIKU_RESOURCE_EFFECT_PARTIAL;
    else if (!strcmp(effects, "unknown"))
        *effect = TIKU_RESOURCE_EFFECT_UNKNOWN;
    else return 0;
    return 1;
}

/** @brief Apply one owner operation after its request has been retained. */
static tiku_resource_error_t operation(resource_remote_t *remote,
                                       const tiku_msg_t *request,
                                       tiku_resource_effect_t *effects)
{
    const char *op = tiku_msg_find_string(request, "op", 0);
    tiku_resource_stamp_t expected;
    resource_provider_t *owner;
    remote_dispatch_t *dispatch;
    tiku_resource_error_t error, reported;
    tiku_resource_effect_t effect;
    tiku_resource_snapshot_t snapshot = {0};
    tiku_msg_t *message;
    if (!strcmp(op, "provider-status")) return TIKU_RESOURCE_OK;
    if (!strcmp(op, "provider-stage") || !strcmp(op, "provider-commit") ||
        !strcmp(op, "provider-abort"))
        return catalog_request(remote, request);
    if (!strcmp(op, "provider-register")) return registration(remote, request);
    if (!tiku_resource_service_expected(request, &expected))
        return TIKU_RESOURCE_INVALID;
    owner = provider(remote);
    if (!owner || strcmp(expected.provider, owner->key) ||
        strcmp(expected.incarnation, owner->stamp.incarnation))
        return TIKU_RESOURCE_OWNER_LOST;
    if (!strcmp(op, "provider-stream"))
        return stream_request(remote, request, owner, &expected);
    if(!strcmp(op,"provider-availability")) {
        const char *path=tiku_msg_find_string(request,"path",0);int available;
        resource_node_t *node =
            tiku_resource_registry_node(remote->service->registry, path);
        if (!node || node->provider != owner ||
            node->spec.kind == TIKU_RESOURCE_ACTION ||
            node->spec.kind == TIKU_RESOURCE_SETTING)
            return TIKU_RESOURCE_DENIED;
        if (!tiku_resource_registry_matches(owner, &expected))
            return TIKU_RESOURCE_STALE;
        if (!tiku_msg_find_bool(request, "available", 0, &available))
            return TIKU_RESOURCE_INVALID;
        tiku_resource_snapshot_t value = {0};
        if (!tiku_resource_registry_copy(&value, &node->value))
            return TIKU_RESOURCE_NO_MEMORY;
        snprintf(value.apply_state, sizeof value.apply_state, "%s",
                 available ? "observed" : "last_good_unavailable");
        tiku_resource_error_t error =
            tiku_resource_publish(remote->service->registry, &expected, path,
                                  &value, &remote->stamp);
        tiku_resource_snapshot_clear(&value);return error;
    }
    if (!strcmp(op, "provider-publish") || !strcmp(op, "provider-unregister")) {
        if (!tiku_resource_registry_matches(owner, &expected))
            return TIKU_RESOURCE_STALE;
        if (!strcmp(op, "provider-unregister")) {
            error =
                tiku_resource_unregister(remote->service->registry, &expected);
            collect(remote);
            return error;
        }
        {
            const char *path = tiku_msg_find_string(request, "path", 0);
            resource_node_t *node =
                tiku_resource_registry_node(remote->service->registry, path);
            if (!node || node->provider != owner) return TIKU_RESOURCE_DENIED;
            message = tiku_msg_find_message(request, "snapshot", 0);
            error = tiku_resource_snapshot_decode(message, &snapshot);
            tiku_msg_free(message);
            if (!error)
                error =
                    tiku_resource_publish(remote->service->registry, &expected,
                                          path, &snapshot, &remote->stamp);
            tiku_resource_snapshot_clear(&snapshot); return error;
        }
    }
    dispatch = dispatch_find(remote, request, &expected);
    if (!dispatch) return TIKU_RESOURCE_OWNER_LOST;
    if (!strcmp(op, "provider-progress")) {
        int32_t progress; int awaiting;
        if (!tiku_msg_find_int32(request, "progress", 0, &progress) ||
            !tiku_msg_find_bool(request, "awaiting_confirmation", 0,
                                &awaiting))
            return TIKU_RESOURCE_INVALID;
        return tiku_resource_job_progress(remote->service->registry,
                                          dispatch->job, (unsigned)progress,
                                          awaiting);
    }
    if (!strcmp(op, "provider-complete")) {
        if (!outcome(request, &reported, &effect)) return TIKU_RESOURCE_INVALID;
        message = tiku_msg_find_message(request, "snapshot", 0);
        if (message) {
            error = tiku_resource_snapshot_decode(message, &snapshot);
            tiku_msg_free(message);
            if (error) return error;
        } else if (!reported || tiku_msg_count(request, "snapshot"))
            return TIKU_RESOURCE_INVALID;
        error = tiku_resource_job_complete(
            remote->service->registry, dispatch->job,
            snapshot.desired ? &snapshot : NULL, reported, effect);
        if (error && effect != TIKU_RESOURCE_EFFECT_NONE)
            *effects = TIKU_RESOURCE_EFFECT_UNKNOWN;
        tiku_resource_snapshot_clear(&snapshot); collect(remote); return error;
    }
    return TIKU_RESOURCE_UNSUPPORTED;
}

/** @brief Compare provider records by named fields and typed-value semantics. */
static int request_equal(const tiku_msg_t *a, const tiku_msg_t *b)
{
    int i, j;
    if (!a || !b || tiku_msg_what(a) != tiku_msg_what(b) ||
        tiku_msg_field_count(a) != tiku_msg_field_count(b))
        return 0;
    if (!tiku_resource_value_check(a) || !tiku_resource_value_check(b))
        return tiku_resource_value_equal(a, b);
    for (i = 0; i < tiku_msg_field_count(a); i++) {
        tiku_msg_type_t type, other_type = 0;
        int count, other_count = 0, n;
        const char *name = tiku_msg_field_at(a, i, &type, &count);
        for (j = 0; j < tiku_msg_field_count(b); j++) {
            const char *other =
                tiku_msg_field_at(b, j, &other_type, &other_count);
            if (!strcmp(name, other)) break;
        }
        if (j == tiku_msg_field_count(b) || type != other_type ||
            count != other_count)
            return 0;
        for (n = 0; n < count; n++) {
            int equal = 0;
            if (type == TIKU_MSG_T_MESSAGE) {
                tiku_msg_t *left = tiku_msg_find_message(a, name, n),
                           *right = tiku_msg_find_message(b, name, n);
                equal = request_equal(left, right);
                tiku_msg_free(left);
                tiku_msg_free(right);
            } else if (type == TIKU_MSG_T_STRING) {
                const char *left = tiku_msg_find_string(a, name, n),
                           *right = tiku_msg_find_string(b, name, n);
                equal = left && right && !strcmp(left, right);
            } else if (type == TIKU_MSG_T_INT32) {
                int32_t left, right;
                equal = tiku_msg_find_int32(a, name, n, &left) &&
                        tiku_msg_find_int32(b, name, n, &right) &&
                        left == right;
            } else if (type == TIKU_MSG_T_INT64) {
                int64_t left, right;
                equal = tiku_msg_find_int64(a, name, n, &left) &&
                        tiku_msg_find_int64(b, name, n, &right) &&
                        left == right;
            } else if (type == TIKU_MSG_T_BOOL) {
                int left, right;
                equal = tiku_msg_find_bool(a, name, n, &left) &&
                        tiku_msg_find_bool(b, name, n, &right) &&
                        left == right;
            } else if (type == TIKU_MSG_T_DOUBLE) {
                double left, right;
                equal = tiku_msg_find_double(a, name, n, &left) &&
                        tiku_msg_find_double(b, name, n, &right) &&
                        left == right;
            } else {
                size_t left_size = tiku_msg_flat_size(a),
                       right_size = tiku_msg_flat_size(b);
                void *left = malloc(left_size), *right = malloc(right_size);
                equal = left && right && left_size == right_size &&
                        tiku_msg_flatten(a, left, left_size, NULL) &&
                        tiku_msg_flatten(b, right, right_size, NULL) &&
                        !memcmp(left, right, left_size);
                free(left); free(right);
                return equal;
            }
            if (!equal) return 0;
        }
    }
    return 1;
}

tiku_msg_t *tiku_resource_remote_request(tiku_resource_service_t *service,
                                         const tiku_msg_t *request)
{
    resource_remote_t *remote = service->remote;
    const char *id = tiku_msg_find_string(request, "request_id", 0);
    tiku_msg_t *result = NULL, *reply;
    remote_receipt_t *receipt;
    tiku_resource_error_t error;
    tiku_resource_effect_t effects = TIKU_RESOURCE_EFFECT_NONE;
    tiku_msg_t *retained;
    uint64_t now = tiku_resource_registry_now(service->registry);
    unsigned i;
    if (!remote || !(service->caller.scopes & TIKU_RESOURCE_PROVIDER))
        return tiku_resource_service_reply(service, request, NULL,
                                           TIKU_RESOURCE_DENIED,
                                           TIKU_RESOURCE_EFFECT_NONE);
    for (i = 0; i < REMOTE_RECEIPTS; i++) {
        receipt = &remote->receipts[i];
        if (receipt->request && now < receipt->expires &&
            !strcmp(receipt->id, id)) {
            if (!request_equal(request, receipt->request))
                return tiku_resource_service_reply(service, request, NULL,
                                                   TIKU_RESOURCE_ID_REUSED,
                                                   TIKU_RESOURCE_EFFECT_NONE);
            return receipt->reply ? tiku_msg_copy(receipt->reply)
                                  : tiku_resource_service_reply(
                                        service, request, NULL,
                                        TIKU_RESOURCE_UNKNOWN_OUTCOME,
                                        TIKU_RESOURCE_EFFECT_UNKNOWN);
        }
    }
    retained = tiku_msg_copy(request);
    if (!retained)
        return tiku_resource_service_reply(service, request, NULL,
                                           TIKU_RESOURCE_NO_MEMORY,
                                           TIKU_RESOURCE_EFFECT_NONE);
    receipt = &remote->receipts[remote->next_receipt++ % REMOTE_RECEIPTS];
    tiku_msg_free(receipt->request);
    tiku_msg_free(receipt->reply);
    memset(receipt, 0, sizeof *receipt);
    receipt->request = retained; strcpy(receipt->id, id);
    receipt->expires = now > UINT64_MAX - 60000 ? UINT64_MAX : now + 60000;
    error = operation(remote, request, &effects);
    if (!error) {
        result = status(remote);
        if (!result)
            error = TIKU_RESOURCE_UNKNOWN_OUTCOME;
    }
    reply = tiku_resource_service_reply(service, request, result, error,
                                        error == TIKU_RESOURCE_UNKNOWN_OUTCOME
                                            ? TIKU_RESOURCE_EFFECT_UNKNOWN
                                            : effects);
    tiku_msg_free(result); receipt->reply = reply;
    return reply ? tiku_msg_copy(reply) : NULL;
}

int tiku_resource_remote_event(tiku_resource_service_t *service,
                               tiku_msg_t **out)
{
    resource_remote_t *remote = service->remote;
    unsigned i;
    if (!remote) return 0;
    collect(remote);
    if (remote->staged && tiku_resource_registry_now(service->registry) >=
                              remote->staged_deadline)
        stage_clear(remote);
    if (remote->upload && tiku_resource_registry_now(service->registry) >=
                              remote->upload_deadline)
        upload_clear(remote);
    for (i = 0; i < RESOURCE_JOBS_ACTIVE_MAX; i++) {
        remote_dispatch_t *dispatch = &remote->dispatches[i];
        if (dispatch->event) {
            resource_job_t *job = job_find(remote, dispatch->job);
            uint64_t now = tiku_resource_registry_now(service->registry);
            if (!now || !job || now >= job->execution_deadline) continue;
            tiku_msg_remove(dispatch->event, "deadline_ms");
            if (!tiku_msg_add_int32(dispatch->event, "deadline_ms",
                                    (int32_t)(job->execution_deadline - now)))
                return -1;
            *out = dispatch->event;
            dispatch->event = NULL;
            dispatch->sent = 1;
            return 1;
        }
        if (dispatch->id[0] && dispatch->sent && dispatch->cancel == 1) {
            tiku_msg_t *event = tiku_msg_new(TIKU_RESOURCE_MESSAGE_CODE),
                       *expected = tiku_msg_new(0);
            int ok =
                event && expected &&
                tiku_resource_service_stamp(expected, &dispatch->expected) &&
                tiku_msg_add_message(event, "expected", expected) &&
                tiku_msg_add_string(event, "kind", "provider-cancel") &&
                tiku_msg_add_string(event, "session",
                                    service->registry->session) &&
                tiku_msg_add_string(event, "dispatch", dispatch->id) &&
                tiku_msg_add_string(event, "job", dispatch->job);
            tiku_msg_free(expected);
            if (!ok) { tiku_msg_free(event); return -1; }
            dispatch->cancel = 2; *out = event; return 1;
        }
    }
    return 0;
}
