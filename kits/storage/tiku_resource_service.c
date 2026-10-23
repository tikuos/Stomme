/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_resource_service.c - scoped resource dispatch and event delivery.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_resource_service_private.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

int tiku_resource_service_caller(const tiku_resource_service_t *service, const tiku_resource_caller_t *caller)
{
    return service && caller && tiku_resource_registry_thread(service->registry) &&
        caller->authenticated && tiku_resource_id_valid(caller->principal) &&
        !strcmp(service->caller.principal, caller->principal) && service->caller.scopes == caller->scopes;
}

tiku_resource_service_t *tiku_resource_service_new(tiku_resource_registry_t *registry,
    const tiku_resource_caller_t *caller, uint64_t features)
{
    tiku_resource_service_t *service;
    if (!tiku_resource_registry_thread(registry) || registry->dispatching || registry->services >= TIKU_RESOURCE_CONNECTIONS_MAX || !caller ||
        !caller->authenticated || !tiku_resource_id_valid(caller->principal) ||
        (features & ~TIKU_RESOURCE_FEATURES)) return NULL;
    service = calloc(1, sizeof *service);
    if (!service) return NULL;
    if (tiku_resource_new_id(service->connection)) { free(service); return NULL; }
    registry->services++;
    tiku_resource_protocol_limits_default(&service->limits);
    service->registry = registry; service->caller = *caller; service->features = features;
    return service;
}

void tiku_resource_service_free(tiku_resource_service_t *service)
{
    unsigned i;
    if (!service || !tiku_resource_registry_thread(service->registry) || service->registry->dispatching) return;
    for (i = 0; i < RESOURCE_SERVICE_WATCHES; i++)
        if (service->watches[i].native) tiku_resource_watch_close(service->registry, service->watches[i].native);
    tiku_resource_service_streams_close(service,0);
    tiku_resource_remote_close(service);
    tiku_resource_jobs_disconnect(service->registry, service->connection);
    service->registry->services--;
    free(service);
}

uint64_t tiku_resource_service_features(const tiku_resource_service_t *service)
{
    return service && tiku_resource_registry_thread(service->registry) ? service->features : 0;
}

int tiku_resource_service_pump(tiku_resource_service_t *service)
{
    return service && tiku_resource_registry_thread(service->registry) ? tiku_resource_jobs_pump(service->registry) : -1;
}

/** @brief Find a subscription in this connection's owned handle table. */
static resource_subscription_t *subscription(tiku_resource_service_t *service, const char *id)
{
    unsigned i;
    if (!id) return NULL;
    for (i = 0; i < RESOURCE_SERVICE_WATCHES; i++)
        if (service->watches[i].native && !strcmp(service->watches[i].id, id)) return &service->watches[i];
    return NULL;
}

/** @brief Create a caller-filtered watch with a random public connection ID. */
static tiku_resource_error_t subscribe(tiku_resource_service_t *service, const tiku_msg_t *request,
    tiku_msg_t **result, resource_subscription_t **created)
{
    resource_subscription_t *slot = NULL;
    tiku_resource_error_t error;
    unsigned i;
    for (i = 0; i < service->limits.subscriptions; i++) if (!service->watches[i].native) { slot = &service->watches[i]; break; }
    if (!slot) return TIKU_RESOURCE_LIMIT;
    if (tiku_resource_new_id(slot->id)) return TIKU_RESOURCE_UNAVAILABLE;
    error = tiku_resource_watch_open(service->registry, &service->caller,
        tiku_msg_find_string(request, "provider", 0), tiku_msg_find_string(request, "path", 0), &slot->native, result);
    if (error) { memset(slot, 0, sizeof *slot); return error; }
    if (!tiku_msg_add_string(*result, "subscription", slot->id)) {
        tiku_resource_watch_close(service->registry, slot->native); memset(slot, 0, sizeof *slot);
        return TIKU_RESOURCE_NO_MEMORY;
    }
    *created = slot;
    return TIKU_RESOURCE_OK;
}

/** @brief Return a retained mutation without reexecuting its controller. */
static tiku_resource_error_t request_status(tiku_resource_service_t *service, const char *id, tiku_msg_t **result)
{
    resource_job_t *job = tiku_resource_jobs_request(service->registry, service->caller.principal, id);
    resource_receipt_t *receipt;
    int existing;
    if (job) {
        tiku_resource_job_info_t info;
        tiku_resource_error_t error = tiku_resource_job_status(service->registry, &service->caller, job->info.id, &info);
        if (error) return error;
        *result = tiku_resource_service_job(&info); tiku_resource_job_info_clear(&info);
        return *result ? TIKU_RESOURCE_OK : TIKU_RESOURCE_NO_MEMORY;
    }
    receipt = tiku_resource_registry_receipt(service->registry, service->caller.principal, id, &existing);
    if (!existing || !receipt || receipt->pending || !tiku_resource_registry_receipt_visible(receipt, &service->caller))
        return TIKU_RESOURCE_OUTCOME_UNAVAILABLE;
    *result = tiku_msg_new(0);
    if (!*result || !tiku_msg_add_string(*result, "state", "completed") ||
        !tiku_msg_add_string(*result, "code", tiku_resource_error_name(receipt->error)) ||
        !tiku_msg_add_string(*result, "effect", receipt->effect == TIKU_RESOURCE_EFFECT_UNKNOWN ? "unknown" :
            receipt->effect == TIKU_RESOURCE_EFFECT_PARTIAL ? "partial" : "none")) return TIKU_RESOURCE_NO_MEMORY;
    if (receipt->protocol_result && !tiku_msg_add_message(*result, "result", receipt->protocol_result)) return TIKU_RESOURCE_NO_MEMORY;
    if (receipt->result.desired) {
        tiku_msg_t *snapshot = tiku_resource_service_snapshot(&receipt->result);
        int ok = snapshot && tiku_msg_add_message(*result, "snapshot", snapshot);
        tiku_msg_free(snapshot);
        if (!ok) return TIKU_RESOURCE_NO_MEMORY;
    }
    return TIKU_RESOURCE_OK;
}

/** @brief Apply a typed request through the owner's synchronous or job path. */
static tiku_resource_error_t change(tiku_resource_service_t *service, const tiku_msg_t *request,
    tiku_msg_t **result, tiku_resource_effect_t *effect, char accepted_job[TIKU_RESOURCE_ID_MAX])
{
    const char *path = tiku_msg_find_string(request, "path", 0);
    const char *op = tiku_msg_find_string(request, "op", 0);
    int invoke = !strcmp(op, "invoke");
    tiku_resource_stamp_t expected = {0};
    resource_node_t *node = tiku_resource_registry_node(service->registry, path);
    tiku_msg_t *value;
    tiku_resource_error_t error;
    tiku_resource_snapshot_t snapshot = {0};
    int32_t deadline;
    uint64_t before;
    resource_receipt_t *receipt;
    int existing;
    const char *durability = invoke ? "" : tiku_msg_find_string(request, "durability", 0);
    if (!node || !node->provider || !tiku_resource_registry_visible(node, &service->caller)) return TIKU_RESOURCE_NOT_FOUND;
    if ((!invoke && node->spec.kind != TIKU_RESOURCE_SETTING) || (invoke && node->spec.kind != TIKU_RESOURCE_ACTION)) return TIKU_RESOURCE_READ_ONLY;
    if (!tiku_resource_service_expected(request, &expected) ||
        !tiku_msg_find_int32(request, "deadline_ms", 0, &deadline)) return TIKU_RESOURCE_INVALID;
    receipt = tiku_resource_registry_receipt(service->registry, service->caller.principal,
        tiku_msg_find_string(request, "request_id", 0), &existing);
    if (existing && receipt->protocol_bound && ((unsigned)deadline != receipt->protocol_deadline ||
        strcmp(durability, receipt->protocol_durability))) return TIKU_RESOURCE_ID_REUSED;
    if (!invoke && strcmp(node->spec.persistence, durability)) return TIKU_RESOURCE_UNSUPPORTED;
    value = tiku_msg_find_message(request, invoke ? "arguments" : "value", 0);
    before = tiku_resource_registry_now(service->registry);
    if (!before || before > UINT64_MAX - (uint64_t)deadline) { tiku_msg_free(value); return TIKU_RESOURCE_UNAVAILABLE; }
    if (node->provider->asynchronous) {
        unsigned active=0,i;
        if(service->registry->jobs)for(i=0;i<RESOURCE_JOBS_MAX;i++){
            resource_job_t *running=&service->registry->jobs[i];
            if(running->info.id[0] && running->info.state<=TIKU_RESOURCE_JOB_AWAITING_CONFIRMATION &&
                !strcmp(running->connection,service->connection))active++;
        }
        if(active>=service->limits.active_jobs && !tiku_resource_jobs_request(service->registry,service->caller.principal,
            tiku_msg_find_string(request,"request_id",0))){tiku_msg_free(value);return TIKU_RESOURCE_BUSY;}
        tiku_resource_job_info_t job;
        if (!(service->features & TIKU_RESOURCE_FEATURE_JOBS)) { tiku_msg_free(value); return TIKU_RESOURCE_UNSUPPORTED; }
        int fresh = !tiku_resource_jobs_request(service->registry, service->caller.principal,
            tiku_msg_find_string(request, "request_id", 0));
        error = tiku_resource_job_submit(service->registry, &service->caller, service->connection, path,
            &expected, tiku_msg_find_string(request, "request_id", 0), value, invoke, (unsigned)deadline, &job);
        if (!error && fresh) strcpy(accepted_job, job.id);
        if (!error) *result = tiku_resource_service_job(&job);
        tiku_resource_job_info_clear(&job);
        if (!error && !*result) error = TIKU_RESOURCE_NO_MEMORY;
    } else {
        error = tiku_resource_change(service->registry, &service->caller, path, &expected,
            tiku_msg_find_string(request, "request_id", 0), value, invoke, &snapshot);
        if (snapshot.desired) {
            *result = tiku_resource_service_snapshot(&snapshot);
            if (!*result) error = TIKU_RESOURCE_UNKNOWN_OUTCOME;
            *effect = snapshot.stamp.revision != expected.revision ? TIKU_RESOURCE_EFFECT_PARTIAL : TIKU_RESOURCE_EFFECT_NONE;
        }
        if (error == TIKU_RESOURCE_UNKNOWN_OUTCOME ||
            (snapshot.desired && tiku_resource_registry_now(service->registry) >= before + (uint64_t)deadline)) {
            error = TIKU_RESOURCE_UNKNOWN_OUTCOME; *effect = TIKU_RESOURCE_EFFECT_UNKNOWN;
        }
        tiku_resource_snapshot_clear(&snapshot);
    }
    receipt = tiku_resource_registry_receipt(service->registry, service->caller.principal,
        tiku_msg_find_string(request, "request_id", 0), &existing);
    if (existing && !receipt->protocol_bound && !strcmp(receipt->path, path) &&
        receipt->invoke == invoke && !strcmp(receipt->expected.provider, expected.provider) &&
        !strcmp(receipt->expected.incarnation, expected.incarnation) && receipt->expected.revision == expected.revision &&
        tiku_resource_value_equal(receipt->request, value)) {
        receipt->protocol_bound = 1; receipt->protocol_deadline = (unsigned)deadline;
        strcpy(receipt->protocol_durability, durability);
    }
    tiku_msg_free(value);
    return error;
}

tiku_msg_t *tiku_resource_service_dispatch(tiku_resource_service_t *service,
    const tiku_resource_caller_t *caller, const tiku_msg_t *request)
{
    const char *op, *path, *session;
    tiku_msg_t *result = NULL, *reply;
    tiku_resource_error_t error;
    tiku_resource_effect_t effect = TIKU_RESOURCE_EFFECT_NONE;
    resource_subscription_t *created = NULL;
    char accepted_job[TIKU_RESOURCE_ID_MAX] = {0};
    if (!service || !tiku_resource_registry_thread(service->registry)) return NULL;
    if (!tiku_resource_service_caller(service, caller))
        return tiku_resource_service_reply(service, request, NULL, TIKU_RESOURCE_DENIED, effect);
    if (service->registry->dispatching)
        return tiku_resource_service_reply(service, request, NULL, TIKU_RESOURCE_BUSY, effect);
    service->used=1;
    error=tiku_resource_message_limits(request,&service->limits,0);
    if(error)return tiku_resource_service_reply(service,request,NULL,error,effect);
    error = tiku_resource_service_encodable(request) ? tiku_resource_request_check(request, service->features) : TIKU_RESOURCE_INVALID;
    session = tiku_msg_find_string(request, "session", 0);
    if (!error && (!session || strcmp(session, service->registry->session))) error = TIKU_RESOURCE_INVALID;
    if (error) return tiku_resource_service_reply(service, request, NULL, error, effect);
    if (!tiku_resource_registry_now(service->registry))
        return tiku_resource_service_reply(service, request, NULL, TIKU_RESOURCE_UNAVAILABLE, effect);
    op = tiku_msg_find_string(request, "op", 0); path = tiku_msg_find_string(request, "path", 0);
    if (!strncmp(op,"stream-",7)) error=tiku_resource_service_stream(service,request,&result);
    else if (!strncmp(op, "provider-", 9)) return tiku_resource_remote_request(service, request);
    else if (!strcmp(op, "list")) error = tiku_resource_service_list(service, request, &result);
    else if (!strcmp(op, "describe")) error = tiku_resource_service_describe(service, path, &result);
    else if (!strcmp(op, "read")) {
        tiku_resource_snapshot_t snapshot;
        error = tiku_resource_read(service->registry, caller, path, &snapshot);
        if (!error) result = tiku_resource_service_snapshot(&snapshot);
        tiku_resource_snapshot_clear(&snapshot);
    } else if (!strcmp(op, "set") || !strcmp(op, "invoke")) error = change(service, request, &result, &effect, accepted_job);
    else if (!strcmp(op, "transaction")) error = tiku_resource_service_transaction(service, request, &result, &effect);
    else if (!strcmp(op, "subscribe")) error = subscribe(service, request, &result, &created);
    else if (!strcmp(op, "unsubscribe")) {
        resource_subscription_t *watch = subscription(service, tiku_msg_find_string(request, "subscription", 0));
        if (!watch) error = TIKU_RESOURCE_NOT_FOUND;
        else { tiku_resource_watch_close(service->registry, watch->native); memset(watch, 0, sizeof *watch); }
    } else if (!strcmp(op, "job-status")) {
        tiku_resource_job_info_t job;
        error = tiku_resource_job_status(service->registry, caller, tiku_msg_find_string(request, "job", 0), &job);
        if (!error) result = tiku_resource_service_job(&job);
        tiku_resource_job_info_clear(&job);
    } else if (!strcmp(op, "cancel")) {
        error = tiku_resource_service_cancel(service, request, &result, &effect);
    } else if (!strcmp(op, "request-status")) error = request_status(service, tiku_msg_find_string(request, "prior_request_id", 0), &result);
    else error = TIKU_RESOURCE_UNSUPPORTED;
    if (!error && !result) result = tiku_msg_new(0);
    if (!error && !result) error = TIKU_RESOURCE_NO_MEMORY;
    reply = tiku_resource_service_reply(service, request, result, error, effect);
    if (created && (!reply || tiku_msg_count(reply, "error"))) {
        tiku_resource_watch_close(service->registry, created->native); memset(created, 0, sizeof *created);
    }
    if (accepted_job[0] && (!reply || tiku_msg_count(reply, "error"))) {
        tiku_resource_job_info_t job;
        if (!tiku_resource_job_status(service->registry, caller, accepted_job, &job)) {
            tiku_resource_cancel_t cancellation;
            if (job.state == TIKU_RESOURCE_JOB_QUEUED)
                tiku_resource_job_cancel(service->registry, caller, accepted_job, &job.stamp, &cancellation);
            tiku_resource_job_info_clear(&job);
        }
    }
    tiku_msg_free(result);
    return reply;
}

int tiku_resource_service_event(tiku_resource_service_t *service,
    const tiku_resource_caller_t *caller, tiku_msg_t **out)
{
    unsigned i;
    if (out) *out = NULL;
    int remote;
    if (!out || !tiku_resource_service_caller(service, caller) || service->registry->dispatching) return -1;
    tiku_resource_service_streams_close(service,1);
    remote = tiku_resource_remote_event(service, out);
    if (remote) return remote;
    if (!(service->features & TIKU_RESOURCE_FEATURE_WATCH)) return 0;
    for (i = 0; i < RESOURCE_SERVICE_WATCHES; i++) {
        unsigned index = (service->next_watch + i) % RESOURCE_SERVICE_WATCHES;
        resource_subscription_t *watch = &service->watches[index];
        tiku_resource_event_t event;
        tiku_msg_t *message, *paths;
        char sequence[21];
        int status, ok;
        if (!watch->native) continue;
        status = tiku_resource_watch_next(service->registry, watch->native, &event);
        if (status < 0) { memset(watch, 0, sizeof *watch); continue; }
        if (!status) continue;
        message = tiku_msg_new(TIKU_RESOURCE_MESSAGE_CODE); paths = tiku_msg_new(0);
        snprintf(sequence, sizeof sequence, "%llu", (unsigned long long)event.sequence);
        ok = message && paths && tiku_msg_add_string(paths, "type", "array");
        {
            unsigned n, count = event.affected_count ? event.affected_count : 1;
            const char *path = event.affected_count ? event.affected : event.path;
            for (n = 0; ok && n < count; n++) {
                tiku_msg_t *value = tiku_resource_value_string(TIKU_RESOURCE_TEXT, path);
                ok = value && tiku_msg_add_message(paths, "item", value);
                tiku_msg_free(value); path += strlen(path) + 1;
            }
        }
        ok = ok && tiku_msg_add_message(message, "paths", paths) &&
            tiku_msg_add_string(message, "session", service->registry->session) &&
            tiku_msg_add_string(message, "subscription", watch->id) &&
            tiku_msg_add_string(message, "sequence", sequence) && tiku_resource_service_stamp(message, &event.stamp) &&
            tiku_msg_add_string(message, "kind", event.overflow ? "overflow" : event.removed ? "removed" : "changed");
        tiku_msg_free(paths);
        if (!ok) {
            tiku_msg_free(message);
            tiku_resource_watch_close(service->registry, watch->native); memset(watch, 0, sizeof *watch);
            return -1;
        }
        service->next_watch = (index + 1) % RESOURCE_SERVICE_WATCHES;
        *out = message; return 1;
    }
    return 0;
}

tiku_resource_error_t tiku_resource_service_limits(tiku_resource_service_t *service,const tiku_resource_protocol_limits_t *limits)
{
    if(!service || !tiku_resource_registry_thread(service->registry) || !tiku_resource_protocol_limits_valid(limits))return TIKU_RESOURCE_INVALID;
    if(service->used || service->registry->dispatching)return TIKU_RESOURCE_BUSY;
    service->limits=*limits;return TIKU_RESOURCE_OK;
}
