/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_resource_client.c - correlated requests with bounded interleaved events.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_resource_client.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdio.h>
#define CLIENT_EVENTS 32
struct tiku_resource_client {
    tiku_resource_channel_t *channel;
    tiku_resource_caller_t caller;
    tiku_resource_protocol_limits_t limits;
    tiku_msg_t *events[CLIENT_EVENTS], *error;
    unsigned count;
    int lost, timeout;
    char mutation[33];
};
/** @brief Nonzero monotonic milliseconds for one bounded reply wait. */
static uint64_t now(void)
{
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC, &t))
        return 0;
    return (uint64_t)t.tv_sec * 1000 + (uint64_t)t.tv_nsec / 1000000;
}
/** @brief Recognize a mutation without granting any authority. */
static int mutation(const char *op)
{
    return op && (!strcmp(op, "set") || !strcmp(op, "invoke") ||
                  !strcmp(op, "cancel") || !strcmp(op, "transaction"));
}
/** @brief Map known protocol errors without treating an unknown code as success. */
static tiku_resource_error_t error_code(const char *code)
{
    for (int i = 1; code && i <= TIKU_RESOURCE_ATOMIC_UNSUPPORTED; i++)
        if (!strcmp(code, tiku_resource_error_name(i)))
            return i;
    return TIKU_RESOURCE_INVALID;
}
/** @brief Retire a failed connection while preserving its last mutation identity. */
static tiku_resource_error_t disconnected(tiku_resource_client_t *c,
                                          int changes)
{
    tiku_resource_channel_free(c->channel);
    c->channel = NULL;
    return changes ? TIKU_RESOURCE_UNKNOWN_OUTCOME : TIKU_RESOURCE_UNAVAILABLE;
}
/** @brief Whether an event is associated with this authenticated session. */
static int event(tiku_resource_client_t *c, const tiku_msg_t *message)
{
    const char *kind = tiku_msg_find_string(message, "kind", 0),
               *session = tiku_msg_find_string(message, "session", 0),
               *id = tiku_msg_find_string(message, "subscription", 0);
    return kind &&
           (!strcmp(kind, "changed") || !strcmp(kind, "removed") ||
            !strcmp(kind, "overflow")) &&
           session && c->channel &&
           !strcmp(session, tiku_resource_channel_session(c->channel)) &&
           tiku_resource_id_valid(id) &&
           !tiku_msg_count(message, "request_id");
}
/** @brief Queue complete events or report that the local delta sequence was lost. */
static void enqueue(tiku_resource_client_t *c, tiku_msg_t *message)
{
    if (c->count == CLIENT_EVENTS || c->lost) {
        for (unsigned i = 0; i < c->count; i++)
            tiku_msg_free(c->events[i]);
        c->count = 0;
        c->lost = 1;
        tiku_msg_free(message);
    } else
        c->events[c->count++] = message;
}
tiku_resource_client_t *tiku_resource_client_open(const char *directory,
                                                  int timeout)
{
    if (timeout < 1 || timeout > 30000)
        return NULL;
    tiku_resource_client_t *c = calloc(1, sizeof *c);
    if (!c)
        return NULL;
    c->timeout = timeout;
    c->channel = tiku_resource_endpoint_connect(directory, timeout);
    if (!c->channel ||
        tiku_resource_channel_negotiate(
            c->channel, TIKU_RESOURCE_FEATURE_READ,
            TIKU_RESOURCE_FEATURES &
                ~(TIKU_RESOURCE_FEATURE_READ | TIKU_RESOURCE_FEATURE_PROVIDER),
            timeout) ||
        tiku_resource_channel_caller(c->channel, &c->caller) ||
        tiku_resource_channel_limits(c->channel, &c->limits)) {
        tiku_resource_client_close(c);
        return NULL;
    }
    return c;
}
void tiku_resource_client_close(tiku_resource_client_t *c)
{
    if (!c)
        return;
    for (unsigned i = 0; i < c->count; i++)
        tiku_msg_free(c->events[i]);
    tiku_msg_free(c->error);
    tiku_resource_channel_free(c->channel);
    free(c);
}
int tiku_resource_client_info(const tiku_resource_client_t *c,
                              tiku_resource_caller_t *caller,
                              tiku_resource_protocol_limits_t *limits)
{
    if (!c || !c->channel)
        return -1;
    if (caller)
        *caller = c->caller;
    if (limits)
        *limits = c->limits;
    return 0;
}
tiku_msg_t *tiku_resource_client_request(tiku_resource_client_t *c,
                                         const char *op, const char *path)
{
    char id[33];
    if (!c || !c->channel || !op || tiku_resource_new_id(id))
        return NULL;
    const char *session = tiku_resource_channel_session(c->channel);
    tiku_msg_t *out = tiku_msg_new(TIKU_RESOURCE_MESSAGE_CODE);
    if (!out || !session || !tiku_msg_add_string(out, "op", op) ||
        !tiku_msg_add_string(out, "request_id", id) ||
        !tiku_msg_add_string(out, "session", session) ||
        (path && !tiku_msg_add_string(out, "path", path))) {
        tiku_msg_free(out);
        return NULL;
    }
    return out;
}
tiku_resource_error_t tiku_resource_client_exchange(tiku_resource_client_t *c,
                                                    const tiku_msg_t *request,
                                                    tiku_msg_t **out)
{
    if (out)
        *out = NULL;
    if (!c || !c->channel || !request || !out)
        return TIKU_RESOURCE_INVALID;
    tiku_msg_free(c->error);
    c->error = NULL;
    tiku_resource_error_t status = tiku_resource_request_check(
        request, tiku_resource_channel_features(c->channel));
    if (status)
        return status;
    const char *id = tiku_msg_find_string(request, "request_id", 0),
               *op = tiku_msg_find_string(request, "op", 0),
               *session = tiku_msg_find_string(request, "session", 0);
    if (!id || !session ||
        strcmp(session, tiku_resource_channel_session(c->channel)))
        return TIKU_RESOURCE_INVALID;
    int changes = mutation(op);
    if (changes)
        snprintf(c->mutation, sizeof c->mutation, "%s", id);
    uint64_t began = now();
    if (!began)
        return TIKU_RESOURCE_UNAVAILABLE;
    if (tiku_resource_channel_send(c->channel, request, c->timeout))
        return disconnected(c, changes);
    for (;;) {
        uint64_t at = now();
        if (!at || at - began >= (unsigned)c->timeout)
            return disconnected(c, changes);
        tiku_msg_t *reply = NULL;
        if (tiku_resource_channel_receive(c->channel, &reply,
                                          c->timeout - (int)(at - began)))
            return disconnected(c, changes);
        if (event(c, reply)) {
            enqueue(c, reply);
            continue;
        }
        const char *reply_id = tiku_msg_find_string(reply, "request_id", 0);
        const char *reply_session = tiku_msg_find_string(reply, "session", 0);
        if (!reply_id || strcmp(reply_id, id) || !reply_session ||
            strcmp(reply_session, session) ||
            tiku_msg_count(reply, "request_id") != 1 ||
            tiku_msg_count(reply, "session") != 1 ||
            tiku_msg_count(reply, "error") + tiku_msg_count(reply, "result") !=
                1) {
            tiku_msg_free(reply);
            (void)disconnected(c, changes);
            return changes ? TIKU_RESOURCE_UNKNOWN_OUTCOME
                           : TIKU_RESOURCE_INVALID;
        }
        c->error = tiku_msg_find_message(reply, "error", 0);
        *out = tiku_msg_find_message(reply, "result", 0);
        tiku_msg_free(reply);
        if (c->error) {
            tiku_msg_free(*out);
            *out = NULL;
            return error_code(tiku_msg_find_string(c->error, "code", 0));
        }
        return *out ? TIKU_RESOURCE_OK : TIKU_RESOURCE_INVALID;
    }
}
tiku_msg_t *tiku_resource_client_error(const tiku_resource_client_t *c)
{
    return c ? tiku_msg_copy(c->error) : NULL;
}
const char *tiku_resource_client_mutation(const tiku_resource_client_t *c)
{
    return c ? c->mutation : "";
}
/** @brief Exchange and release one newly constructed request. */
static tiku_resource_error_t send(tiku_resource_client_t *c,
                                  tiku_msg_t *request, tiku_msg_t **out)
{
    if (!request) {
        if (out)
            *out = NULL;
        return TIKU_RESOURCE_NO_MEMORY;
    }
    tiku_resource_error_t e = tiku_resource_client_exchange(c, request, out);
    tiku_msg_free(request);
    return e;
}
tiku_resource_error_t tiku_resource_client_observe(tiku_resource_client_t *c,
                                                   const char *op,
                                                   const char *path,
                                                   tiku_msg_t **out)
{
    if (!op || (strcmp(op, "read") && strcmp(op, "describe")) ||
        !tiku_resource_path_valid(path)) {
        if (out)
            *out = NULL;
        return TIKU_RESOURCE_INVALID;
    }
    return send(c, tiku_resource_client_request(c, op, path), out);
}
tiku_resource_error_t tiku_resource_client_list(tiku_resource_client_t *c,
                                                const char *path,
                                                const char *cursor,
                                                tiku_msg_t **out)
{
    if (out)
        *out = NULL;
    if (!c || !tiku_resource_path_valid(path) ||
        (cursor && *cursor && !tiku_resource_id_valid(cursor)))
        return TIKU_RESOURCE_INVALID;
    tiku_msg_t *request = tiku_resource_client_request(c, "list", path);
    if (!request ||
        !tiku_msg_add_int32(request, "page_size", c->limits.page_entries) ||
        (cursor && *cursor &&
         !tiku_msg_add_string(request, "cursor", cursor))) {
        tiku_msg_free(request);
        return TIKU_RESOURCE_NO_MEMORY;
    }
    return send(c, request, out);
}
int tiku_resource_client_stamp(const tiku_msg_t *description,
                               tiku_resource_stamp_t *out)
{
    if (!description || !out)
        return 0;
    memset(out, 0, sizeof *out);
    const char *provider = tiku_msg_find_string(description, "provider", 0),
               *inc = tiku_msg_find_string(description, "incarnation", 0),
               *rev = tiku_msg_find_string(description, "revision", 0);
    if (!provider || !*provider || strlen(provider) >= sizeof out->provider ||
        !tiku_resource_id_valid(inc) ||
        tiku_resource_counter_parse(rev, &out->revision) || !out->revision)
        return 0;
    strcpy(out->provider, provider);
    strcpy(out->incarnation, inc);
    return 1;
}
/** @brief Encode the exact stamp captured by the caller's observation. */
static int expected(tiku_msg_t *request, const tiku_msg_t *description)
{
    tiku_resource_stamp_t stamp;
    if (!tiku_resource_client_stamp(description, &stamp))
        return 0;
    char revision[32];
    snprintf(revision, sizeof revision, "%llu",
             (unsigned long long)stamp.revision);
    tiku_msg_t *tuple = tiku_msg_new(0);
    int ok = tuple && tiku_msg_add_string(tuple, "provider", stamp.provider) &&
             tiku_msg_add_string(tuple, "incarnation", stamp.incarnation) &&
             tiku_msg_add_string(tuple, "revision", revision) &&
             tiku_msg_add_message(request, "expected", tuple);
    tiku_msg_free(tuple);
    return ok;
}
tiku_resource_error_t
tiku_resource_client_validate(const tiku_resource_client_t *c,
                              const tiku_msg_t *d, const tiku_msg_t *value,
                              int invoke)
{
    if (!c || !c->channel || !d || !value || (invoke != 0 && invoke != 1))
        return TIKU_RESOURCE_INVALID;
    tiku_resource_error_t e = tiku_resource_value_check(value);
    if (e)
        return e;
    const char *kind = tiku_msg_find_string(d, "kind", 0),
               *availability = tiku_msg_find_string(d, "availability", 0);
    if (!kind || strcmp(kind, invoke ? "action" : "setting"))
        return TIKU_RESOURCE_READ_ONLY;
    if (!availability || strcmp(availability, "available"))
        return TIKU_RESOURCE_UNAVAILABLE;
    int64_t needed = 0, features = 0;
    tiku_msg_find_int64(d, "change_scopes", 0, &needed);
    tiku_msg_find_int64(d, "change_features", 0, &features);
    int host = 1, sensitive = 0;
    tiku_msg_find_bool(d, "host_control_required", 0, &host);
    tiku_msg_find_bool(d, "sensitive", 0, &sensitive);
    uint64_t required = (uint64_t)needed |
                        (invoke ? TIKU_RESOURCE_INVOKE : TIKU_RESOURCE_WRITE) |
                        (host ? TIKU_RESOURCE_HOST_CONTROL : 0) |
                        (sensitive ? TIKU_RESOURCE_CONTENT : 0);
    if ((c->caller.scopes & required) != required)
        return TIKU_RESOURCE_DENIED;
    if ((tiku_resource_channel_features(c->channel) & (uint64_t)features) !=
        (uint64_t)features)
        return TIKU_RESOURCE_UNSUPPORTED;
    tiku_msg_t *schema = tiku_msg_find_message(d, "value_schema", 0);
    const char *type = tiku_msg_find_string(schema, "type", 0),
               *actual = tiku_msg_find_string(value, "type", 0);
    e = type && actual && !strcmp(type, actual) ? TIKU_RESOURCE_OK
                                                : TIKU_RESOURCE_VALUE;
    if (!e && (!strcmp(type, "integer") || !strcmp(type, "color"))) {
        int64_t low = 0, high = 0, n = 0;
        tiku_msg_find_int64(schema, "integer_min", 0, &low);
        tiku_msg_find_int64(schema, "integer_max", 0, &high);
        tiku_msg_find_int64(value, "value", 0, &n);
        if (n < low || n > high)
            e = TIKU_RESOURCE_VALUE;
    }
    if (!e && (!strcmp(type, "text") || !strcmp(type, "enum"))) {
        const char *text = tiku_msg_find_string(value, "value", 0);
        int32_t max = 0;
        tiku_msg_find_int32(schema, "text_max", 0, &max);
        if (!text || (strlen(text) > (unsigned)max && !strcmp(type, "text")))
            e = TIKU_RESOURCE_VALUE;
        if (!e && !strcmp(type, "enum")) {
            int found = 0;
            for (int i = 0; i < tiku_msg_count(schema, "choice"); i++)
                if (!strcmp(text, tiku_msg_find_string(schema, "choice", i)))
                    found = 1;
            if (!found)
                e = TIKU_RESOURCE_VALUE;
        }
    }
    if (!e && !strcmp(type, "number")) {
        double n, low, high;
        if (!tiku_msg_find_double(value, "value", 0, &n) ||
            !tiku_msg_find_double(schema, "number_min", 0, &low) ||
            !tiku_msg_find_double(schema, "number_max", 0, &high) || n < low ||
            n > high)
            e = TIKU_RESOURCE_VALUE;
    }
    tiku_msg_free(schema);
    return e;
}
tiku_resource_error_t tiku_resource_client_change(tiku_resource_client_t *c,
                                                  const tiku_msg_t *d,
                                                  const tiku_msg_t *value,
                                                  int invoke, tiku_msg_t **out)
{
    if (out)
        *out = NULL;
    if (c) {
        tiku_msg_free(c->error);
        c->error = NULL;
    }
    tiku_resource_error_t e =
        tiku_resource_client_validate(c, d, value, invoke);
    if (e)
        return e;
    const char *path = tiku_msg_find_string(d, "path", 0),
               *durability = tiku_msg_find_string(d, "persistence", 0);
    if (!tiku_resource_path_valid(path) || (!invoke && !durability))
        return TIKU_RESOURCE_INVALID;
    tiku_msg_t *request =
        tiku_resource_client_request(c, invoke ? "invoke" : "set", path);
    if (!request || !expected(request, d) ||
        !tiku_msg_add_message(request, invoke ? "arguments" : "value",
                              value) ||
        (!invoke && !tiku_msg_add_string(request, "durability", durability)) ||
        !tiku_msg_add_int32(request, "deadline_ms", 3000)) {
        tiku_msg_free(request);
        return TIKU_RESOURCE_NO_MEMORY;
    }
    return send(c, request, out);
}
tiku_resource_error_t tiku_resource_client_job(tiku_resource_client_t *c,
                                               const char *id,
                                               tiku_msg_t **out)
{
    if (out)
        *out = NULL;
    if (!tiku_resource_id_valid(id))
        return TIKU_RESOURCE_INVALID;
    tiku_msg_t *request = tiku_resource_client_request(c, "job-status", NULL);
    if (!request || !tiku_msg_add_string(request, "job", id)) {
        tiku_msg_free(request);
        return TIKU_RESOURCE_NO_MEMORY;
    }
    return send(c, request, out);
}
tiku_resource_error_t tiku_resource_client_receipt(tiku_resource_client_t *c,
                                                   const char *id,
                                                   tiku_msg_t **out)
{
    if (out)
        *out = NULL;
    if (!tiku_resource_id_valid(id))
        return TIKU_RESOURCE_INVALID;
    tiku_msg_t *request =
        tiku_resource_client_request(c, "request-status", NULL);
    if (!request || !tiku_msg_add_string(request, "prior_request_id", id)) {
        tiku_msg_free(request);
        return TIKU_RESOURCE_NO_MEMORY;
    }
    return send(c, request, out);
}
tiku_resource_error_t tiku_resource_client_cancel(tiku_resource_client_t *c,
                                                  const char *id,
                                                  tiku_msg_t **out)
{
    if (out)
        *out = NULL;
    tiku_msg_t *status = NULL;
    tiku_resource_error_t e = tiku_resource_client_job(c, id, &status);
    if (e)
        return e;
    tiku_msg_t *request = tiku_resource_client_request(c, "cancel", NULL);
    int ok = request && expected(request, status) &&
             tiku_msg_add_string(request, "job", id) &&
             tiku_msg_add_int32(request, "deadline_ms", 3000);
    tiku_msg_free(status);
    if (!ok) {
        tiku_msg_free(request);
        return TIKU_RESOURCE_NO_MEMORY;
    }
    return send(c, request, out);
}
tiku_resource_error_t tiku_resource_client_watch(tiku_resource_client_t *c,
                                                 const tiku_msg_t *d,
                                                 const char *path,
                                                 tiku_msg_t **out)
{
    if (out)
        *out = NULL;
    const char *provider = tiku_msg_find_string(d, "provider", 0);
    if (!provider || !tiku_resource_path_valid(path))
        return TIKU_RESOURCE_INVALID;
    tiku_msg_t *request = tiku_resource_client_request(c, "subscribe", path);
    if (!request || !tiku_msg_add_string(request, "provider", provider)) {
        tiku_msg_free(request);
        return TIKU_RESOURCE_NO_MEMORY;
    }
    return send(c, request, out);
}
tiku_resource_error_t tiku_resource_client_unwatch(tiku_resource_client_t *c,
                                                   const char *id)
{
    if (!tiku_resource_id_valid(id))
        return TIKU_RESOURCE_INVALID;
    tiku_msg_t *request = tiku_resource_client_request(c, "unsubscribe", NULL),
               *out = NULL;
    if (!request || !tiku_msg_add_string(request, "subscription", id)) {
        tiku_msg_free(request);
        return TIKU_RESOURCE_NO_MEMORY;
    }
    tiku_resource_error_t e = send(c, request, &out);
    tiku_msg_free(out);
    return e;
}
tiku_resource_error_t tiku_resource_client_event(tiku_resource_client_t *c,
                                                 int timeout, tiku_msg_t **out)
{
    if (out)
        *out = NULL;
    if (!c || !c->channel || !out || timeout < 0 || timeout > 30000)
        return TIKU_RESOURCE_INVALID;
    if (c->lost) {
        c->lost = 0;
        return TIKU_RESOURCE_LIMIT;
    }
    if (c->count) {
        *out = c->events[0];
        memmove(c->events, c->events + 1, --c->count * sizeof c->events[0]);
        return TIKU_RESOURCE_OK;
    }
    int ready = tiku_resource_channel_wait(c->channel, timeout);
    if (ready < 0)
        return TIKU_RESOURCE_UNAVAILABLE;
    if (!ready)
        return TIKU_RESOURCE_PENDING;
    if (tiku_resource_channel_receive(c->channel, out, c->timeout))
        return TIKU_RESOURCE_UNAVAILABLE;
    if (!event(c, *out)) {
        tiku_msg_free(*out);
        *out = NULL;
        return TIKU_RESOURCE_INVALID;
    }
    return TIKU_RESOURCE_OK;
}
tiku_resource_error_t tiku_resource_client_stream(tiku_resource_client_t *c,
                                                  const tiku_msg_t *d,
                                                  size_t maximum, void **bytes,
                                                  size_t *length)
{
    if (bytes)
        *bytes = NULL;
    if (length)
        *length = 0;
    if (!c || !bytes || !length || !maximum ||
        maximum > c->limits.stream_bytes)
        return TIKU_RESOURCE_INVALID;
    const char *path = tiku_msg_find_string(d, "path", 0);
    if (!tiku_resource_path_valid(path))
        return TIKU_RESOURCE_INVALID;
    tiku_msg_t *request = tiku_resource_client_request(c, "stream-open", path),
               *result = NULL;
    char text[32], lease[33] = {0};
    snprintf(text, sizeof text, "%zu", maximum);
    if (!request || !expected(request, d) ||
        !tiku_msg_add_string(request, "maximum_bytes", text)) {
        tiku_msg_free(request);
        return TIKU_RESOURCE_NO_MEMORY;
    }
    tiku_resource_error_t e = send(c, request, &result);
    if (e)
        return e;
    const char *id = tiku_msg_find_string(result, "lease", 0);
    uint64_t size = 0;
    if (!tiku_resource_id_valid(id) ||
        tiku_resource_counter_parse(tiku_msg_find_string(result, "bytes", 0),
                                    &size) ||
        size > maximum) {
        tiku_msg_free(result);
        return TIKU_RESOURCE_INVALID;
    }
    strcpy(lease, id);
    tiku_msg_free(result);
    result = NULL;
    unsigned char *data = malloc((size_t)size + 1);
    size_t at = 0;
    if (!data)
        e = TIKU_RESOURCE_NO_MEMORY;
    while (!e && at < size) {
        request = tiku_resource_client_request(c, "stream-read", NULL);
        snprintf(text, sizeof text, "%zu", at);
        if (!request || !tiku_msg_add_string(request, "lease", lease) ||
            !tiku_msg_add_string(request, "offset", text) ||
            !tiku_msg_add_int32(request, "byte_count",
                                c->limits.stream_chunk_bytes)) {
            tiku_msg_free(request);
            e = TIKU_RESOURCE_NO_MEMORY;
            break;
        }
        e = send(c, request, &result);
        if (!e) {
            size_t n = 0;
            const void *chunk = tiku_msg_find_blob(result, "data", 0, &n);
            if (!chunk || !n || n > size - at)
                e = TIKU_RESOURCE_INVALID;
            else {
                memcpy(data + at, chunk, n);
                at += n;
            }
        }
        tiku_msg_free(result);
        result = NULL;
    }
    if (c->channel) {
        request = tiku_resource_client_request(c, "stream-close", NULL);
        if (request && tiku_msg_add_string(request, "lease", lease)) {
            tiku_resource_error_t close_error = send(c, request, &result);
            if (!e)
                e = close_error;
            tiku_msg_free(result);
        } else {
            tiku_msg_free(request);
            if (!e)
                e = TIKU_RESOURCE_NO_MEMORY;
        }
    }
    if (e) {
        free(data);
        return e;
    }
    data[size] = 0;
    *bytes = data;
    *length = (size_t)size;
    return TIKU_RESOURCE_OK;
}
