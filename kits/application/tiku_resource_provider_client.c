/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_resource_provider_client.c - application provider envelope state.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_resource_provider_client.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CLIENT_ACTIVE_MAX 16
#define CLIENT_DONE_MAX 16
#define CLIENT_DISPATCHES (CLIENT_ACTIVE_MAX + CLIENT_DONE_MAX)

/** @brief A delivered controller identity retained until completion acknowledgement. */
typedef struct {
    char id[TIKU_RESOURCE_ID_MAX], job[TIKU_RESOURCE_ID_MAX];
    tiku_resource_stamp_t expected;
    int cancelled, completed;
    uint64_t sequence;
} client_dispatch_t;

struct tiku_resource_provider_client {
    pthread_t thread;
    char session[TIKU_RESOURCE_ID_MAX], key[TIKU_RESOURCE_KEY_MAX], prefix[TIKU_RESOURCE_PATH_MAX];
    char pending[TIKU_RESOURCE_ID_MAX], completing[TIKU_RESOURCE_ID_MAX];
    tiku_resource_stamp_t stamp;
    client_dispatch_t dispatches[CLIENT_DISPATCHES];
    int registered;
    uint64_t sequence;
};

/** @brief Whether the creating application thread owns this state. */
static int owned(const tiku_resource_provider_client_t *client)
{
    return client && pthread_equal(client->thread, pthread_self());
}

/** @brief Require singular fields from an exact set. */
static int fields(const tiku_msg_t *message, const char *const *allowed, size_t length)
{
    int i;
    for (i = 0; i < tiku_msg_field_count(message); i++) {
        int count; size_t j;
        const char *name = tiku_msg_field_at(message, i, NULL, &count);
        for (j = 0; j < length; j++) if (!strcmp(name, allowed[j])) break;
        if (count != 1 || j == length) return 0;
    }
    return message != NULL;
}

/** @brief Read a canonical provider stamp into fixed storage. */
static int stamp_read(const tiku_msg_t *message, tiku_resource_stamp_t *out)
{
    const char *key = tiku_msg_find_string(message, "provider", 0), *incarnation = tiku_msg_find_string(message, "incarnation", 0);
    if (!key || !key[0] || strlen(key) >= sizeof out->provider || strchr(key, '/') || !tiku_resource_id_valid(incarnation) ||
        tiku_resource_counter_parse(tiku_msg_find_string(message, "revision", 0), &out->revision) || !out->revision) return 0;
    strcpy(out->provider, key); strcpy(out->incarnation, incarnation); return 1;
}

/** @brief Encode an expected stamp with canonical revision text. */
static int expected_add(tiku_msg_t *message, const tiku_resource_stamp_t *stamp)
{
    char revision[21];
    tiku_msg_t *expected = tiku_msg_new(0);
    int ok;
    snprintf(revision, sizeof revision, "%llu", (unsigned long long)stamp->revision);
    ok = expected && tiku_msg_add_string(expected, "provider", stamp->provider) &&
        tiku_msg_add_string(expected, "incarnation", stamp->incarnation) && tiku_msg_add_string(expected, "revision", revision) &&
        tiku_msg_add_message(message, "expected", expected);
    tiku_msg_free(expected); return ok;
}

/** @brief Preflight the complete outbound frame before tracking a pending reply. */
static tiku_msg_t *finish(tiku_resource_provider_client_t *client, tiku_msg_t *message, int ok)
{
    size_t size = tiku_msg_flat_size(message);
    void *bytes = size && size <= TIKU_RESOURCE_FRAME_MAX ? malloc(size) : NULL;
    ok = ok && bytes && tiku_msg_flatten(message, bytes, size, NULL) && !tiku_resource_message_check(bytes, size) &&
        !tiku_resource_request_check(message, TIKU_RESOURCE_FEATURE_PROVIDER);
    free(bytes);
    if (ok) { strcpy(client->pending, tiku_msg_find_string(message, "request_id", 0)); return message; }
    tiku_msg_free(message); return NULL;
}

/** @brief Build a correlated envelope while no prior request awaits its reply. */
static tiku_msg_t *request(tiku_resource_provider_client_t *client, const char *op)
{
    char id[TIKU_RESOURCE_ID_MAX];
    tiku_msg_t *message;
    if (!owned(client) || client->pending[0] || tiku_resource_new_id(id)) return NULL;
    message = tiku_msg_new(TIKU_RESOURCE_MESSAGE_CODE);
    if (message && tiku_msg_add_string(message, "op", op) && tiku_msg_add_string(message, "session", client->session) &&
        tiku_msg_add_string(message, "request_id", id)) return message;
    tiku_msg_free(message); return NULL;
}

/** @brief Whether a canonical resource is below this client's assigned namespace. */
static int below(const tiku_resource_provider_client_t *client, const char *path)
{
    size_t length = strlen(client->prefix);
    return tiku_resource_path_valid(path) && !strncmp(path, client->prefix, length) && path[length] == '/';
}

tiku_resource_provider_client_t *tiku_resource_provider_client_new(const tiku_resource_channel_t *channel)
{
    tiku_resource_caller_t caller;
    tiku_resource_provider_client_t *client;
    if (!(tiku_resource_channel_features(channel) & TIKU_RESOURCE_FEATURE_PROVIDER) ||
        tiku_resource_channel_caller(channel, &caller) || !(caller.scopes & TIKU_RESOURCE_PROVIDER)) return NULL;
    client = calloc(1, sizeof *client);
    if (!client) return NULL;
    client->thread = pthread_self(); strcpy(client->session, tiku_resource_channel_session(channel));
    snprintf(client->prefix, sizeof client->prefix, "/apps/%s", caller.principal);
    snprintf(client->key, sizeof client->key, "app.%s", caller.principal);
    return client;
}

void tiku_resource_provider_client_free(tiku_resource_provider_client_t *client)
{
    if (owned(client)) free(client);
}

const char *tiku_resource_provider_client_prefix(const tiku_resource_provider_client_t *client)
{
    return owned(client) ? client->prefix : NULL;
}

tiku_msg_t *tiku_resource_provider_client_register(tiku_resource_provider_client_t *client,
    const tiku_resource_initial_t *initial, size_t count)
{
    tiku_msg_t *message, *resources;
    size_t i;
    int ok;
    if (!owned(client) || client->registered || !initial || !count || count > TIKU_RESOURCE_PROVIDER_RESOURCES_MAX) return NULL;
    message = request(client, "provider-register"); resources = tiku_msg_new(0); ok = message && resources;
    for (i = 0; ok && i < count; i++) {
        tiku_msg_t *entry = tiku_resource_initial_message(&initial[i]);
        ok = below(client, initial[i].spec.path) && entry && tiku_msg_add_message(resources, "entry", entry);
        tiku_msg_free(entry);
    }
    ok = ok && tiku_msg_add_message(message, "resources", resources);
    tiku_msg_free(resources); return finish(client, message, ok);
}

tiku_msg_t *tiku_resource_provider_client_publish(tiku_resource_provider_client_t *client,
    const char *path, const tiku_resource_snapshot_t *snapshot)
{
    tiku_msg_t *message, *value;
    int ok;
    if (!owned(client) || !client->registered || !below(client, path)) return NULL;
    message = request(client, "provider-publish"); value = tiku_resource_snapshot_message(snapshot);
    ok = message && value && expected_add(message, &client->stamp) && tiku_msg_add_string(message, "path", path) &&
        tiku_msg_add_message(message, "snapshot", value);
    tiku_msg_free(value); return finish(client, message, ok);
}

tiku_msg_t *tiku_resource_provider_client_unregister(tiku_resource_provider_client_t *client)
{
    tiku_msg_t *message;
    if (!owned(client) || !client->registered) return NULL;
    message = request(client, "provider-unregister");
    return finish(client, message, message && expected_add(message, &client->stamp));
}

tiku_msg_t *tiku_resource_provider_client_status(tiku_resource_provider_client_t *client)
{
    tiku_msg_t *message = request(client, "provider-status");
    return finish(client, message, message != NULL);
}

/** @brief Parse a stable error name from an owner reply. */
static tiku_resource_error_t error_read(const char *code)
{
    int i;
    if (code) for (i = TIKU_RESOURCE_OK; i <= TIKU_RESOURCE_ATOMIC_UNSUPPORTED; i++)
        if (!strcmp(code, tiku_resource_error_name(i))) return i;
    return TIKU_RESOURCE_INVALID;
}

tiku_resource_error_t tiku_resource_provider_client_reply(tiku_resource_provider_client_t *client, const tiku_msg_t *reply)
{
    static const char *const allowed[] = {"session", "request_id", "result", "error"};
    static const char *const status_fields[] = {"prefix", "registered", "provider", "incarnation", "revision"};
    const char *id, *session, *prefix;
    tiku_msg_t *result;
    tiku_resource_stamp_t stamp = {0};
    tiku_resource_error_t error = TIKU_RESOURCE_INVALID;
    int registered, i, valid = 0;
    if (!owned(client) || !client->pending[0] || tiku_msg_what(reply) != TIKU_RESOURCE_MESSAGE_CODE) return error;
    id = tiku_msg_find_string(reply, "request_id", 0); session = tiku_msg_find_string(reply, "session", 0);
    if (!id || strcmp(id, client->pending) || !session || strcmp(session, client->session) ||
        !fields(reply, allowed, sizeof allowed / sizeof allowed[0]) ||
        (!!tiku_msg_count(reply, "error") == !!tiku_msg_count(reply, "result"))) return error;
    result = tiku_msg_find_message(reply, "result", 0);
    if (result) {
        prefix = tiku_msg_find_string(result, "prefix", 0);
        valid = prefix && !strcmp(prefix, client->prefix) && fields(result, status_fields, sizeof status_fields / sizeof status_fields[0]) &&
            tiku_msg_find_bool(result, "registered", 0, &registered);
        if (valid && registered) valid = stamp_read(result, &stamp) && !strcmp(stamp.provider, client->key);
        if (valid && !registered) {
            const char *key = tiku_msg_find_string(result, "provider", 0);
            valid = key && !strcmp(key, client->key) && !tiku_msg_count(result, "incarnation") && !tiku_msg_count(result, "revision");
        }
        if (valid) {
            if (!registered || strcmp(stamp.incarnation, client->stamp.incarnation)) memset(client->dispatches, 0, sizeof client->dispatches);
            client->registered = registered; client->stamp = stamp; error = TIKU_RESOURCE_OK;
        }
    } else {
        tiku_msg_t *details = tiku_msg_find_message(reply, "error", 0);
        const char *code = tiku_msg_find_string(details, "code", 0);
        error = error_read(code); valid = details && code && error != TIKU_RESOURCE_OK;
        tiku_msg_free(details);
    }
    tiku_msg_free(result);
    if (!valid) return TIKU_RESOURCE_INVALID;
    if (client->completing[0] && (!error || error == TIKU_RESOURCE_OWNER_LOST || error == TIKU_RESOURCE_STALE ||
        error == TIKU_RESOURCE_DEADLINE || error == TIKU_RESOURCE_UNKNOWN_OUTCOME)) {
        int count = 0, oldest = -1;
        for (i = 0; i < CLIENT_DISPATCHES; i++) {
            client_dispatch_t *dispatch = &client->dispatches[i];
            if (!strcmp(dispatch->id, client->completing)) { dispatch->completed = 1; dispatch->sequence = ++client->sequence; }
            if (dispatch->completed) {
                count++;
                if (oldest < 0 || dispatch->sequence < client->dispatches[oldest].sequence) oldest = i;
            }
        }
        if (count > CLIENT_DONE_MAX) memset(&client->dispatches[oldest], 0, sizeof client->dispatches[oldest]);
    }
    client->pending[0] = client->completing[0] = 0;
    return error;
}

void tiku_resource_provider_call_clear(tiku_resource_provider_call_t *call)
{
    if (call) { tiku_msg_free(call->value); memset(call, 0, sizeof *call); }
}

tiku_resource_error_t tiku_resource_provider_client_event(tiku_resource_provider_client_t *client,
    const tiku_msg_t *event, tiku_resource_provider_call_t *out)
{
    static const char *const allowed[] = {"kind", "session", "dispatch", "job", "expected", "path", "invoke", "value", "deadline_ms", "origin_request_id", "origin"};
    static const char *const cancel_fields[] = {"kind", "session", "dispatch", "job", "expected"};
    static const char *const expected_fields[] = {"provider", "incarnation", "revision"};
    static const char *const origin_fields[] = {"principal", "scopes"};
    const char *kind, *session, *id, *job, *path, *request_id, *principal;
    tiku_msg_t *expected, *origin;
    client_dispatch_t *slot = NULL;
    int i, valid, cancel_event, active = 0;
    int32_t deadline;
    int64_t scopes;
    if (out) memset(out, 0, sizeof *out);
    if (!owned(client) || !out || !client->registered || tiku_msg_what(event) != TIKU_RESOURCE_MESSAGE_CODE) return TIKU_RESOURCE_INVALID;
    kind = tiku_msg_find_string(event, "kind", 0); session = tiku_msg_find_string(event, "session", 0);
    id = tiku_msg_find_string(event, "dispatch", 0); job = tiku_msg_find_string(event, "job", 0);
    if (!kind || (strcmp(kind, "provider-dispatch") && strcmp(kind, "provider-cancel")) ||
        !session || strcmp(session, client->session) || !tiku_resource_id_valid(id) || !tiku_resource_id_valid(job)) return TIKU_RESOURCE_INVALID;
    cancel_event = !strcmp(kind, "provider-cancel");
    if (!fields(event, cancel_event ? cancel_fields : allowed,
        cancel_event ? sizeof cancel_fields / sizeof cancel_fields[0] : sizeof allowed / sizeof allowed[0])) return TIKU_RESOURCE_INVALID;
    expected = tiku_msg_find_message(event, "expected", 0);
    valid = fields(expected, expected_fields, sizeof expected_fields / sizeof expected_fields[0]) && stamp_read(expected, &out->expected) &&
        !strcmp(out->expected.provider, client->key) && !strcmp(out->expected.incarnation, client->stamp.incarnation);
    tiku_msg_free(expected);
    if (!valid) return TIKU_RESOURCE_OWNER_LOST;
    for (i = 0; i < CLIENT_DISPATCHES; i++) if (!strcmp(client->dispatches[i].id, id)) { slot = &client->dispatches[i]; break; }
    if (cancel_event) {
        if (!slot || slot->completed || strcmp(slot->job, job) || strcmp(slot->expected.incarnation, out->expected.incarnation) ||
            slot->expected.revision != out->expected.revision) return TIKU_RESOURCE_OWNER_LOST;
        if (slot->cancelled) return TIKU_RESOURCE_ID_REUSED;
        slot->cancelled = out->cancel = 1; strcpy(out->dispatch, id); strcpy(out->job, job); return TIKU_RESOURCE_OK;
    }
    if (slot) return TIKU_RESOURCE_ID_REUSED;
    for (i = 0; i < CLIENT_DISPATCHES; i++) if (client->dispatches[i].id[0] && !client->dispatches[i].completed) active++;
    if (active >= CLIENT_ACTIVE_MAX) return TIKU_RESOURCE_LIMIT;
    for (i = 0; i < CLIENT_DISPATCHES; i++) if (!client->dispatches[i].id[0]) { slot = &client->dispatches[i]; break; }
    if (!slot) return TIKU_RESOURCE_LIMIT;
    path = tiku_msg_find_string(event, "path", 0); request_id = tiku_msg_find_string(event, "origin_request_id", 0);
    origin = tiku_msg_find_message(event, "origin", 0); principal = tiku_msg_find_string(origin, "principal", 0);
    valid = below(client, path) && tiku_resource_id_valid(request_id) && tiku_resource_id_valid(principal) &&
        fields(origin, origin_fields, sizeof origin_fields / sizeof origin_fields[0]) &&
        tiku_msg_find_int64(origin, "scopes", 0, &scopes) && scopes >= 0 && !((uint64_t)scopes & ~UINT64_C(15)) &&
        tiku_msg_find_int32(event, "deadline_ms", 0, &deadline) && deadline > 0 && deadline <= 300000 &&
        tiku_msg_find_bool(event, "invoke", 0, &out->invoke);
    if (valid) { strcpy(out->origin.principal, principal); out->origin.scopes = (uint64_t)scopes; }
    tiku_msg_free(origin);
    out->value = tiku_msg_find_message(event, "value", 0);
    if (!valid || tiku_resource_value_check(out->value)) { tiku_resource_provider_call_clear(out); return TIKU_RESOURCE_INVALID; }
    strcpy(out->path, path); strcpy(out->request_id, request_id); strcpy(out->dispatch, id); strcpy(out->job, job);
    out->deadline_ms = (unsigned)deadline;
    strcpy(slot->id, id); strcpy(slot->job, job); slot->expected = out->expected;
    return TIKU_RESOURCE_OK;
}

/** @brief Find a previously delivered application dispatch. */
static client_dispatch_t *dispatch_find(tiku_resource_provider_client_t *client, const char *id)
{
    int i;
    if (!owned(client) || !tiku_resource_id_valid(id)) return NULL;
    for (i = 0; i < CLIENT_DISPATCHES; i++) if (!client->dispatches[i].completed && !strcmp(client->dispatches[i].id, id)) return &client->dispatches[i];
    return NULL;
}

tiku_msg_t *tiku_resource_provider_client_complete(tiku_resource_provider_client_t *client, const char *id,
    const tiku_resource_snapshot_t *snapshot, tiku_resource_error_t error, tiku_resource_effect_t effect)
{
    client_dispatch_t *dispatch = dispatch_find(client, id);
    tiku_msg_t *message, *value = NULL;
    int ok;
    if (!dispatch || error < TIKU_RESOURCE_OK || error > TIKU_RESOURCE_ATOMIC_UNSUPPORTED || error == TIKU_RESOURCE_PENDING ||
        effect < TIKU_RESOURCE_EFFECT_NONE || effect > TIKU_RESOURCE_EFFECT_UNKNOWN || (!snapshot && !error)) return NULL;
    message = request(client, "provider-complete");
    if (snapshot) value = tiku_resource_snapshot_message(snapshot);
    ok = message && expected_add(message, &dispatch->expected) && tiku_msg_add_string(message, "dispatch", id) &&
        tiku_msg_add_string(message, "code", tiku_resource_error_name(error)) &&
        tiku_msg_add_string(message, "effect", effect == TIKU_RESOURCE_EFFECT_NONE ? "none" : effect == TIKU_RESOURCE_EFFECT_PARTIAL ? "partial" : "unknown") &&
        (!snapshot || (value && tiku_msg_add_message(message, "snapshot", value)));
    tiku_msg_free(value); message = finish(client, message, ok);
    if (message) strcpy(client->completing, id);
    return message;
}

tiku_msg_t *tiku_resource_provider_client_progress(tiku_resource_provider_client_t *client,
    const char *id, unsigned progress, int awaiting)
{
    client_dispatch_t *dispatch = dispatch_find(client, id);
    tiku_msg_t *message;
    if (!dispatch || progress > 1000 || (awaiting != 0 && awaiting != 1)) return NULL;
    message = request(client, "provider-progress");
    return finish(client, message, message && expected_add(message, &dispatch->expected) &&
        tiku_msg_add_string(message, "dispatch", id) && tiku_msg_add_int32(message, "progress", (int32_t)progress) &&
        tiku_msg_add_bool(message, "awaiting_confirmation", awaiting));
}
