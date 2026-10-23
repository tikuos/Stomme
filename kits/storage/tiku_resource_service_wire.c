/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_resource_service_wire.c - bounded protocol replies and cursor pages.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_resource_service_private.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** @brief Append a canonical unsigned decimal field. */
static int counter(tiku_msg_t *message, const char *field, uint64_t value)
{
    char text[21];
    snprintf(text, sizeof text, "%llu", (unsigned long long)value);
    return tiku_msg_add_string(message, field, text);
}

int tiku_resource_service_stamp(tiku_msg_t *message, const tiku_resource_stamp_t *stamp)
{
    return tiku_msg_add_string(message, "provider", stamp->provider) &&
        tiku_msg_add_string(message, "incarnation", stamp->incarnation) && counter(message, "revision", stamp->revision);
}

tiku_msg_t *tiku_resource_service_snapshot(const tiku_resource_snapshot_t *snapshot)
{
    tiku_msg_t *message = tiku_msg_new(0);
    int ok;
    if (!message) return NULL;
    ok = tiku_resource_service_stamp(message, &snapshot->stamp) &&
        tiku_msg_add_string(message, "apply_state", snapshot->apply_state);
    if (snapshot->desired) ok = ok && tiku_msg_add_message(message, "desired", snapshot->desired);
    if (snapshot->persisted) ok = ok && tiku_msg_add_message(message, "persisted", snapshot->persisted);
    if (snapshot->applied) ok = ok && tiku_msg_add_message(message, "applied", snapshot->applied);
    if (!ok) { tiku_msg_free(message); return NULL; }
    return message;
}

tiku_msg_t *tiku_resource_service_job(const tiku_resource_job_info_t *job)
{
    tiku_msg_t *message = tiku_msg_new(0), *resource = tiku_msg_new(0), *result = NULL;
    int ok;
    if (!message || !resource) goto fail;
    ok = tiku_resource_service_stamp(message, &job->stamp) &&
        tiku_msg_add_string(message, "job", job->id) && tiku_msg_add_string(message, "request_id", job->request_id) &&
        tiku_msg_add_string(message, "path", job->path) &&
        tiku_msg_add_string(message, "state", tiku_resource_job_state_name(job->state)) &&
        tiku_msg_add_string(message, "code", tiku_resource_error_name(job->error)) &&
        tiku_msg_add_string(message, "effect", job->effect == TIKU_RESOURCE_EFFECT_UNKNOWN ? "unknown" :
            job->effect == TIKU_RESOURCE_EFFECT_PARTIAL ? "partial" : "none") &&
        tiku_msg_add_bool(message, "cancel_requested", job->cancel_requested) &&
        tiku_msg_add_int32(message, "progress", (int32_t)job->progress) &&
        counter(message, "expires_at_ms", job->expires_at) &&
        tiku_resource_service_stamp(resource, &job->resource_stamp) &&
        tiku_msg_add_message(message, "resource", resource);
    if (job->result.desired) {
        result = tiku_resource_service_snapshot(&job->result);
        ok = ok && result && tiku_msg_add_message(message, "snapshot", result);
    }
    tiku_msg_free(resource); tiku_msg_free(result);
    if (!ok) { tiku_msg_free(message); return NULL; }
    return message;
fail:
    tiku_msg_free(message); tiku_msg_free(resource); return NULL;
}

/** @brief Whether a reply fits framing, nesting and typed codec bounds. */
int tiku_resource_service_encodable(const tiku_msg_t *message)
{
    size_t size = tiku_msg_flat_size(message);
    void *bytes;
    int valid;
    if (!size || size > TIKU_RESOURCE_FRAME_MAX) return 0;
    bytes = malloc(size);
    if (!bytes) return 0;
    valid = tiku_msg_flatten(message, bytes, size, NULL) && !tiku_resource_message_check(bytes, size);
    free(bytes);
    return valid;
}

tiku_msg_t *tiku_resource_service_reply(tiku_resource_service_t *service, const tiku_msg_t *request,
    tiku_msg_t *result, tiku_resource_error_t error, tiku_resource_effect_t effect)
{
    const char *id = tiku_msg_find_string(request, "request_id", 0);
    tiku_msg_t *reply = tiku_msg_new(TIKU_RESOURCE_MESSAGE_CODE), *details = NULL;
    int ok;
    if (!reply || !tiku_resource_id_valid(id)) goto fail;
    ok = tiku_msg_add_string(reply, "request_id", id) &&
        tiku_msg_add_string(reply, "session", service->registry->session);
    if (error) {
        details = tiku_msg_new(0);
        ok = ok && details && tiku_msg_add_string(details, "code", tiku_resource_error_name(error)) &&
            tiku_msg_add_string(details, "message", tiku_resource_error_name(error)) &&
            tiku_msg_add_string(details, "effect", effect == TIKU_RESOURCE_EFFECT_UNKNOWN ? "unknown" :
                effect == TIKU_RESOURCE_EFFECT_PARTIAL ? "partial" : "none");
        if (result) ok = ok && tiku_msg_add_message(details, "current", result);
        ok = ok && tiku_msg_add_message(reply, "error", details);
    } else ok = ok && result && tiku_msg_add_message(reply, "result", result);
    tiku_msg_free(details);
    if (ok && tiku_resource_service_encodable(reply) && !tiku_resource_message_limits(reply,&service->limits,0)) return reply;
    tiku_msg_free(reply);
    if (result) return tiku_resource_service_reply(service, request, NULL,
        effect == TIKU_RESOURCE_EFFECT_NONE ? TIKU_RESOURCE_LIMIT : TIKU_RESOURCE_UNKNOWN_OUTCOME, effect);
    return NULL;
fail:
    tiku_msg_free(reply); tiku_msg_free(details); return NULL;
}

int tiku_resource_service_expected(const tiku_msg_t *request, tiku_resource_stamp_t *stamp)
{
    tiku_msg_t *tuple = tiku_msg_find_message(request, "expected", 0);
    const char *provider, *incarnation;
    int ok = 0;
    if (!tuple) return 0;
    provider = tiku_msg_find_string(tuple, "provider", 0); incarnation = tiku_msg_find_string(tuple, "incarnation", 0);
    if (provider && strlen(provider) < sizeof stamp->provider && tiku_resource_id_valid(incarnation) &&
        !tiku_resource_counter_parse(tiku_msg_find_string(tuple, "revision", 0), &stamp->revision) && stamp->revision) {
        strcpy(stamp->provider, provider); strcpy(stamp->incarnation, incarnation); ok = 1;
    }
    tiku_msg_free(tuple);
    return ok;
}

tiku_resource_error_t tiku_resource_service_describe(tiku_resource_service_t *service, const char *path, tiku_msg_t **out)
{
    tiku_resource_error_t error = tiku_resource_describe(service->registry, &service->caller, path, out);
    tiku_msg_t *limits;
    int ok;
    if (error || strcmp(path, "/")) return error;
    limits = tiku_msg_new(0);
    if (!limits) return TIKU_RESOURCE_NO_MEMORY;
    ok = tiku_msg_add_int32(limits, "frame_bytes", (int32_t)service->limits.frame_bytes) &&
        tiku_msg_add_int32(limits, "page_entries", (int32_t)service->limits.page_entries) &&
        tiku_msg_add_int32(limits, "message_depth", (int32_t)service->limits.message_depth) &&
        tiku_msg_add_int32(limits, "snapshot_value_depth", (int32_t)service->limits.message_depth - 3) &&
        tiku_msg_add_int32(limits, "connections", TIKU_RESOURCE_CONNECTIONS_MAX) &&
        tiku_msg_add_int32(limits, "transaction_settings", (int32_t)service->limits.transaction_settings) &&
        tiku_msg_add_int32(limits, "group_path_bytes", TIKU_RESOURCE_GROUP_PATH_BYTES) &&
        tiku_msg_add_int32(limits, "provider_resources", (int32_t)service->limits.provider_resources) &&
        tiku_msg_add_int32(limits, "provider_receipts", 4) &&
        tiku_msg_add_int32(limits,"event_queue_bytes",RESOURCE_EVENT_BYTES_MAX) &&
        tiku_msg_add_int32(limits,"event_records",(int32_t)(RESOURCE_EVENT_BYTES_MAX/RESOURCE_EVENT_CHARGE < 256 ? RESOURCE_EVENT_BYTES_MAX/RESOURCE_EVENT_CHARGE:256)) &&
        tiku_msg_add_int32(limits, "cursor_leases", RESOURCE_SERVICE_CURSORS) &&
        tiku_msg_add_int32(limits, "subscriptions", (int32_t)service->limits.subscriptions) &&
        tiku_msg_add_int32(limits, "nodes", (int32_t)service->registry->limits.resources) &&
        tiku_msg_add_int32(limits, "retained_mutations", (int32_t)service->registry->limits.receipts) &&
        tiku_msg_add_int32(limits, "active_jobs", (int32_t)(service->registry->limits.receipts < service->limits.active_jobs ? service->registry->limits.receipts : service->limits.active_jobs)) &&
        tiku_msg_add_int32(limits,"stream_leases",(int32_t)service->limits.stream_leases) &&
        tiku_msg_add_int32(limits,"stream_chunk_bytes",(int32_t)service->limits.stream_chunk_bytes) &&
        tiku_msg_add_int32(limits,"stream_bytes",(int32_t)service->limits.stream_bytes) &&
        tiku_msg_add_message(*out, "limits", limits) &&
        tiku_msg_add_int64(*out, "service_features", (int64_t)service->features);
    tiku_msg_free(limits);
    return ok ? TIKU_RESOURCE_OK : TIKU_RESOURCE_NO_MEMORY;
}

/** @brief Find a live cursor owned by this connection and exact path. */
static resource_cursor_t *cursor_find(tiku_resource_service_t *service, const char *id, const char *path, uint64_t now)
{
    unsigned i;
    for (i = 0; i < RESOURCE_SERVICE_CURSORS; i++) {
        resource_cursor_t *cursor = &service->cursors[i];
        if (cursor->id[0] && now >= cursor->expires) memset(cursor, 0, sizeof *cursor);
        if (cursor->id[0] && !strcmp(cursor->id, id) && !strcmp(cursor->path, path)) return cursor;
    }
    return NULL;
}

/** @brief Reserve a cursor slot without replacing an unread enumeration. */
static resource_cursor_t *cursor_slot(tiku_resource_service_t *service, uint64_t now)
{
    resource_cursor_t *oldest = NULL;
    unsigned i;
    for (i = 0; i < RESOURCE_SERVICE_CURSORS; i++) {
        resource_cursor_t *cursor = &service->cursors[i];
        if (!cursor->id[0] || now >= cursor->expires) return cursor;
        if (cursor->consumed && (!oldest || cursor->expires < oldest->expires)) oldest = cursor;
    }
    return oldest;
}

tiku_resource_error_t tiku_resource_service_list(tiku_resource_service_t *service, const tiku_msg_t *request, tiku_msg_t **out)
{
    static const char *const kinds[] = {"directory", "setting", "state", "action", "collection", "stream"};
    const char *path = tiku_msg_find_string(request, "path", 0), *id = tiku_msg_find_string(request, "cursor", 0);
    tiku_resource_entry_t page[128], probe;
    resource_cursor_t *input = NULL, *next = NULL;
    uint64_t now = tiku_resource_registry_now(service->registry), epoch = 0, after = 0;
    size_t count, actual = 0, more = 0, i, bytes = 256;
    int32_t size;
    tiku_msg_t *message;
    tiku_resource_error_t error;
    if (!now || now > UINT64_MAX - 30000) return TIKU_RESOURCE_UNAVAILABLE;
    if (!tiku_msg_find_int32(request, "page_size", 0, &size) || size < 1 || size > (int32_t)service->limits.page_entries) return TIKU_RESOURCE_INVALID;
    if (id) {
        input = cursor_find(service, id, path, now);
        if (!input) return TIKU_RESOURCE_STALE_CURSOR;
        epoch = input->epoch; after = input->after;
    }
    error = tiku_resource_list(service->registry, &service->caller, path, after, &epoch, page, (size_t)size, &count);
    if (error) {
        if (input) memset(input, 0, sizeof *input);
        return error == TIKU_RESOURCE_STALE ? TIKU_RESOURCE_STALE_CURSOR : error;
    }
    message = tiku_msg_new(0);
    if (!message) return TIKU_RESOURCE_NO_MEMORY;
    for (i = 0; i < count; i++) {
        tiku_msg_t *entry = tiku_msg_new(0);
        int ok = entry && tiku_msg_add_string(entry, "path", page[i].path) &&
            tiku_msg_add_string(entry, "id", page[i].id) && tiku_msg_add_string(entry, "kind", kinds[page[i].kind]);
        size_t entry_size = tiku_msg_flat_size(entry) + 4;
        if (!ok) { tiku_msg_free(entry); tiku_msg_free(message); return TIKU_RESOURCE_NO_MEMORY; }
        if (entry_size > service->limits.frame_bytes - 2048 - bytes) { tiku_msg_free(entry); break; }
        ok = tiku_msg_add_message(message, "entry", entry); tiku_msg_free(entry);
        if (!ok) { tiku_msg_free(message); return TIKU_RESOURCE_NO_MEMORY; }
        bytes += entry_size; actual++; after = page[i].node_id;
    }
    if (actual < count) more = 1;
    else {
        error = tiku_resource_list(service->registry, &service->caller, path, after, &epoch, &probe, 1, &more);
        if (error) { tiku_msg_free(message); return error; }
    }
    if (input) input->consumed = 1;
    if (more) {
        next = cursor_slot(service, now);
        if (!next) { tiku_msg_free(message); return TIKU_RESOURCE_BUSY; }
        memset(next, 0, sizeof *next);
        if (tiku_resource_new_id(next->id)) { tiku_msg_free(message); return TIKU_RESOURCE_UNAVAILABLE; }
        strcpy(next->path, path); next->epoch = epoch; next->after = after; next->expires = now + 30000;
        if (!tiku_msg_add_string(message, "next_cursor", next->id)) { memset(next, 0, sizeof *next); tiku_msg_free(message); return TIKU_RESOURCE_NO_MEMORY; }
    }
    if (!tiku_msg_add_int32(message, "count", (int32_t)actual)) { tiku_msg_free(message); return TIKU_RESOURCE_NO_MEMORY; }
    *out = message;
    return TIKU_RESOURCE_OK;
}
