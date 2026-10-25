/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * test_resource_client.c - SDK requests, interleaved watches and uncertain receipts.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_resource_client.h"
#include "tiku_resource_service.h"
#include "tiku_resource_registry.h"
#include "tiku_resource_jobs.h"
#include "tiku_resource_stream.h"
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/socket.h>
/** @brief One native fixture owner and its independent client worker. */
typedef struct {
    tiku_resource_registry_t *registry;
    tiku_resource_endpoint_t *endpoint;
    atomic_int done, writes;
    atomic_ullong scopes, features;
    char directory[80], pending[33];
    int cancelled;
} fixture_t;
/** @brief One accepted endpoint connection. */
typedef struct {
    fixture_t *fixture;
    int fd;
    tiku_resource_channel_t *channel;
    tiku_resource_service_t *service;
    tiku_resource_caller_t caller;
    char subscription[33];
} peer_t;
/** @brief Own complete snapshot holds for a native fixture result. */
static void snapshot(tiku_resource_snapshot_t *out, const tiku_msg_t *value)
{
    out->desired = tiku_msg_copy(value);
    out->persisted = tiku_msg_copy(value);
    out->applied = tiku_msg_copy(value);
    strcpy(out->apply_state, "applied");
    assert(out->desired && out->persisted && out->applied);
}
/** @brief Actions accept exact empty objects and typed settings retain schema validation. */
static tiku_resource_error_t validate(void *context, const char *path,
                                      const tiku_msg_t *value, int invoke)
{
    (void)context;
    (void)path;
    if (invoke && tiku_msg_count(value, "member"))
        return TIKU_RESOURCE_VALUE;
    return TIKU_RESOURCE_OK;
}
/** @brief Count native effects and return a complete owner result. */
static tiku_resource_error_t apply(void *context, const char *path,
                                   const tiku_msg_t *value, int invoke,
                                   tiku_resource_snapshot_t *out)
{
    (void)path;
    (void)invoke;
    fixture_t *f = context;
    atomic_fetch_add(&f->writes, 1);
    snapshot(out, value);
    return TIKU_RESOURCE_OK;
}
/** @brief Hold an async job until cancellation or test teardown. */
static tiku_resource_error_t start(void *context, const char *job,
                                   const char *path, const tiku_msg_t *value,
                                   int invoke, tiku_resource_snapshot_t *out)
{
    (void)path;
    (void)value;
    (void)invoke;
    (void)out;
    fixture_t *f = context;
    strcpy(f->pending, job);
    return TIKU_RESOURCE_PENDING;
}
/** @brief Defer cancellation completion to the registry owner loop. */
static tiku_resource_cancel_t cancel(void *context, const char *job)
{
    fixture_t *f = context;
    assert(!strcmp(f->pending, job));
    f->cancelled = 1;
    return TIKU_RESOURCE_CANCEL_ACCEPTED;
}
/** @brief Grant an explicit fixture scope without opening any external service. */
static int authorize(void *context, uid_t uid, pid_t pid,
                     tiku_resource_caller_t *out)
{
    (void)pid;
    peer_t *p = context;
    assert(uid == getuid());
    out->authenticated = 1;
    out->scopes = atomic_load(&p->fixture->scopes);
    strcpy(out->principal, "00000000000000000000000000000001");
    p->caller = *out;
    return 1;
}
/** @brief Write a bounded complete event ahead of an RPC reply. */
static void flood(peer_t *p)
{
    for (unsigned i = 0; i < 33; i++) {
        tiku_msg_t *event = tiku_msg_new(TIKU_RESOURCE_MESSAGE_CODE);
        assert(event);
        assert(tiku_msg_add_string(event, "kind", "changed") &&
               tiku_msg_add_string(
                   event, "session",
                   tiku_resource_registry_session(p->fixture->registry)) &&
               tiku_msg_add_string(event, "subscription", p->subscription));
        size_t n = tiku_msg_flat_size(event);
        unsigned char *bytes = malloc(n + 4);
        assert(bytes);
        bytes[0] = n >> 24;
        bytes[1] = n >> 16;
        bytes[2] = n >> 8;
        bytes[3] = n;
        assert(tiku_msg_flatten(event, bytes + 4, n, NULL));
        assert(send(p->fd, bytes, n + 4, 0) == (ssize_t)(n + 4));
        free(bytes);
        tiku_msg_free(event);
    }
}
/** @brief Use the native broker, with one deliberate post-effect reply loss. */
static tiku_msg_t *dispatch(void *context,
                            const tiku_resource_caller_t *caller,
                            const tiku_msg_t *request)
{
    peer_t *p = context;
    if (!p->service) {
        p->service = tiku_resource_service_new(
            p->fixture->registry, caller,
            tiku_resource_channel_features(p->channel));
        assert(p->service);
    }
    tiku_msg_t *reply =
        tiku_resource_service_dispatch(p->service, caller, request);
    assert(reply);
    const char *op = tiku_msg_find_string(request, "op", 0),
               *path = tiku_msg_find_string(request, "path", 0);
    if (op && !strcmp(op, "subscribe")) {
        tiku_msg_t *result = tiku_msg_find_message(reply, "result", 0);
        const char *id = tiku_msg_find_string(result, "subscription", 0);
        if (id)
            strcpy(p->subscription, id);
        tiku_msg_free(result);
    }
    if (op && path && !strcmp(op, "read") && !strcmp(path, "/fixture/flood"))
        flood(p);
    if (op && path && !strcmp(op, "invoke") &&
        !strcmp(path, "/fixture/drop")) {
        tiku_msg_free(reply);
        return NULL;
    }
    return reply;
}
/** @brief Retire broker subscriptions before releasing the accepted channel. */
static void closed(void *context, const tiku_resource_caller_t *caller)
{
    (void)caller;
    peer_t *p = context;
    tiku_resource_service_free(p->service);
    p->service = NULL;
}
/** @brief Run public client operations on the client channel's creating thread. */
static void *client_run(void *context)
{
    fixture_t *f = context;
    tiku_resource_client_t *c = tiku_resource_client_open(f->directory, 2000);
    assert(c);
    tiku_msg_t *d = NULL, *out = NULL, *v = NULL, *old = NULL;
    assert(
        !tiku_resource_client_observe(c, "describe", "/fixture/enabled", &d));
    old = tiku_msg_copy(d);
    assert(!tiku_resource_client_watch(c, d, "/fixture/enabled", &out));
    tiku_msg_free(out);
    out = NULL;
    assert(!tiku_resource_literal_parse("boolean", "true", &v));
    assert(!tiku_resource_client_change(c, d, v, 0, &out));
    tiku_msg_free(out);
    out = NULL;
    tiku_msg_free(d);
    d = NULL;
    assert(!tiku_resource_client_observe(c, "read", "/fixture/enabled", &out));
    char json[16384];
    int n = tiku_resource_client_format(out, json, sizeof json);
    assert(n > 0);
    json[n] = 0;
    assert(strstr(json, "\"boolean\""));
    tiku_msg_free(out);
    out = NULL;
    assert(!tiku_resource_client_event(c, 1000, &out));
    assert(!strcmp(tiku_msg_find_string(out, "kind", 0), "changed"));
    tiku_msg_free(out);
    out = NULL;
    assert(tiku_resource_client_change(c, old, v, 0, &out) ==
               TIKU_RESOURCE_STALE &&
           !out);
    tiku_msg_free(old);
    tiku_msg_free(v);
    v = NULL;
    assert(
        !tiku_resource_client_observe(c, "describe", "/fixture/number", &d));
    assert(!tiku_resource_literal_parse("integer", "99", &v));
    int before = atomic_load(&f->writes);
    assert(tiku_resource_client_validate(c, d, v, 0) == TIKU_RESOURCE_VALUE);
    assert(tiku_resource_client_change(c, d, v, 0, &out) ==
               TIKU_RESOURCE_VALUE &&
           !out && atomic_load(&f->writes) == before);
    tiku_msg_free(d);
    tiku_msg_free(v);
    d = NULL;
    v = NULL;
    assert(
        !tiku_resource_client_observe(c, "describe", "/fixture/readonly", &d));
    assert(!tiku_resource_literal_parse("boolean", "false", &v));
    assert(tiku_resource_client_change(c, d, v, 0, &out) ==
           TIKU_RESOURCE_READ_ONLY);
    tiku_msg_free(d);
    tiku_msg_free(v);
    d = NULL;
    v = NULL;
    char cursor[33] = {0};
    unsigned entries = 0;
    do {
        assert(!tiku_resource_client_list(c, "/fixture", cursor, &out));
        entries += tiku_msg_count(out, "entry");
        const char *next = tiku_msg_find_string(out, "next_cursor", 0);
        snprintf(cursor, sizeof cursor, "%s", next ? next : "");
        tiku_msg_free(out);
        out = NULL;
    } while (*cursor);
    assert(entries == 6);
    assert(
        !tiku_resource_client_observe(c, "describe", "/fixture/content", &d));
    void *bytes = NULL;
    size_t length = 0;
    assert(!tiku_resource_client_stream(c, d, 1024, &bytes, &length) &&
           length == 8 && !memcmp(bytes, "fixture\n", 8));
    free(bytes);
    tiku_msg_free(d);
    d = NULL;
    assert(!tiku_resource_client_observe(c, "read", "/fixture/flood", &out));
    tiku_msg_free(out);
    out = NULL;
    assert(tiku_resource_client_event(c, 0, &out) == TIKU_RESOURCE_LIMIT &&
           !out);
    assert(!tiku_resource_client_observe(c, "describe", "/async/run", &d));
    assert(!tiku_resource_literal_parse("json", "{}", &v));
    assert(!tiku_resource_client_change(c, d, v, 1, &out));
    char job[33];
    strcpy(job, tiku_msg_find_string(out, "job", 0));
    tiku_msg_free(out);
    out = NULL;
    tiku_msg_free(d);
    d = NULL;
    assert(!tiku_resource_client_cancel(c, job, &out));
    tiku_msg_free(out);
    out = NULL;
    for (unsigned i = 0; i < 100; i++) {
        assert(!tiku_resource_client_job(c, job, &out));
        const char *state = tiku_msg_find_string(out, "state", 0);
        int done = state && !strcmp(state, "cancelled");
        tiku_msg_free(out);
        out = NULL;
        if (done)
            break;
        usleep(1000);
        assert(i < 99);
    }
    assert(!tiku_resource_client_observe(c, "describe", "/fixture/drop", &d));
    before = atomic_load(&f->writes);
    assert(tiku_resource_client_change(c, d, v, 1, &out) ==
               TIKU_RESOURCE_UNKNOWN_OUTCOME &&
           !out);
    char request[33];
    strcpy(request, tiku_resource_client_mutation(c));
    assert(tiku_resource_id_valid(request));
    tiku_msg_free(d);
    d = NULL;
    tiku_msg_free(v);
    v = NULL;
    tiku_resource_client_close(c);
    assert(atomic_load(&f->writes) == before + 1);
    c = tiku_resource_client_open(f->directory, 2000);
    assert(c);
    assert(!tiku_resource_client_receipt(c, request, &out));
    assert(!strcmp(tiku_msg_find_string(out, "code", 0), "ok"));
    assert(atomic_load(&f->writes) == before + 1);
    tiku_msg_free(out);
    out = NULL;
    tiku_resource_client_close(c);
    atomic_store(&f->scopes, TIKU_RESOURCE_READ | TIKU_RESOURCE_CONTENT);
    c = tiku_resource_client_open(f->directory, 2000);
    assert(c);
    assert(
        !tiku_resource_client_observe(c, "describe", "/fixture/enabled", &d));
    assert(!tiku_resource_literal_parse("boolean", "false", &v));
    assert(tiku_resource_client_validate(c, d, v, 0) == TIKU_RESOURCE_DENIED);
    assert(tiku_resource_client_change(c, d, v, 0, &out) ==
               TIKU_RESOURCE_DENIED &&
           atomic_load(&f->writes) == before + 1);
    tiku_msg_free(d);
    tiku_msg_free(v);
    d = NULL;
    v = NULL;
    tiku_resource_client_close(c);
    atomic_store(&f->scopes, 63);
    atomic_store(&f->features, TIKU_RESOURCE_FEATURE_READ);
    c = tiku_resource_client_open(f->directory, 2000);
    assert(c);
    assert(
        !tiku_resource_client_observe(c, "describe", "/fixture/enabled", &d));
    assert(!tiku_resource_literal_parse("boolean", "false", &v));
    assert(tiku_resource_client_change(c, d, v, 0, &out) ==
           TIKU_RESOURCE_UNSUPPORTED);
    tiku_msg_free(d);
    tiku_msg_free(v);
    tiku_resource_client_close(c);
    atomic_store(&f->done, 1);
    return NULL;
}
int main(void)
{
    fixture_t f = {0};
    atomic_init(&f.done, 0);
    atomic_init(&f.writes, 0);
    atomic_init(&f.scopes, 63);
    atomic_init(&f.features,
                TIKU_RESOURCE_FEATURES & ~TIKU_RESOURCE_FEATURE_PROVIDER);
    strcpy(f.directory, "/tmp/stomme-client-XXXXXX");
    assert(mkdtemp(f.directory));
    f.endpoint = tiku_resource_endpoint_open(f.directory);
    f.registry = tiku_resource_registry_new(NULL);
    assert(f.endpoint && f.registry);
    const char *paths[] = {"/fixture/enabled",  "/fixture/number",
                           "/fixture/readonly", "/fixture/drop",
                           "/fixture/content",  "/fixture/flood"};
    tiku_resource_initial_t initial[6] = {0};
    for (int i = 0; i < 6; i++) {
        initial[i].spec = (tiku_resource_spec_t){
            .path = paths[i],
            .kind = i == 2 || i == 5 ? TIKU_RESOURCE_STATE
                    : i == 3         ? TIKU_RESOURCE_ACTION
                    : i == 4         ? TIKU_RESOURCE_STREAM
                                     : TIKU_RESOURCE_SETTING,
            .schema_version = 1,
            .schema = {.type = i == 3 || i == 4 ? TIKU_RESOURCE_OBJECT
                               : i == 1         ? TIKU_RESOURCE_INTEGER
                                                : TIKU_RESOURCE_BOOLEAN,
                       .integer_min = 0,
                       .integer_max = 10},
            .read_scopes = TIKU_RESOURCE_READ,
            .persistence = i < 2 ? "session" : "none",
            .controller = "fixture"};
        tiku_msg_t *value =
            i == 3 ? tiku_msg_new(0)
            : i == 4
                ? tiku_resource_stream_metadata(0, "")
                : tiku_resource_value_integer(i == 1 ? TIKU_RESOURCE_INTEGER
                                                     : TIKU_RESOURCE_BOOLEAN,
                                              0);
        if (i == 3)
            assert(tiku_msg_add_string(value, "type", "object"));
        snapshot(&initial[i].initial, value);
        tiku_msg_free(value);
    }
    tiku_resource_stamp_t stamp;
    tiku_resource_controller_t controls = {.validate = validate,
                                           .apply = apply};
    assert(!tiku_resource_register(f.registry, "fixture", "/fixture", NULL,
                                   initial, 6, &controls, &f, &stamp));
    assert(!tiku_resource_stream_publish(
        f.registry, &stamp, "/fixture/content", "fixture\n", 8, &stamp));
    for (int i = 0; i < 6; i++)
        tiku_resource_snapshot_clear(&initial[i].initial);
    tiku_resource_initial_t async = {
        .spec = {.path = "/async/run",
                 .kind = TIKU_RESOURCE_ACTION,
                 .schema_version = 1,
                 .schema = {.type = TIKU_RESOURCE_OBJECT},
                 .read_scopes = TIKU_RESOURCE_READ,
                 .persistence = "none",
                 .controller = "async"}};
    tiku_msg_t *empty = tiku_msg_new(0);
    assert(empty && tiku_msg_add_string(empty, "type", "object"));
    snapshot(&async.initial, empty);
    tiku_msg_free(empty);
    tiku_resource_async_controller_t asynchronous = {
        .validate = validate, .start = start, .cancel = cancel};
    assert(!tiku_resource_register_async(f.registry, "async", "/async", NULL,
                                         &async, 1, &asynchronous, &f,
                                         &stamp));
    tiku_resource_snapshot_clear(&async.initial);
    pthread_t worker;
    assert(!pthread_create(&worker, NULL, client_run, &f));
    peer_t peers[4] = {0};
    for (int i = 0; i < 4; i++)
        peers[i].fd = -1;
    static const tiku_resource_channel_ops_t ops = {
        .authorize = authorize, .dispatch = dispatch, .closed = closed};
    while (!atomic_load(&f.done)) {
        for (int i = 0; i < 4; i++) {
            peer_t *p = &peers[i];
            if (!p->channel) {
                int fd = tiku_resource_endpoint_accept(f.endpoint);
                if (fd < 0)
                    continue;
                p->fd = fd;
                p->fixture = &f;
                p->channel = tiku_resource_channel_server(
                    fd, tiku_resource_registry_session(f.registry),
                    atomic_load(&f.features), &ops, p);
                assert(p->channel);
                tiku_resource_protocol_limits_t limits;
                tiku_resource_protocol_limits_default(&limits);
                limits.page_entries = 2;
                assert(!tiku_resource_channel_offer(p->channel, &limits));
            }
            int ready =
                tiku_resource_endpoint_ready(p->fd, TIKU_RESOURCE_FRAME_MAX);
            if (ready < 0 ||
                (ready && tiku_resource_channel_step(p->channel, 2000))) {
                tiku_resource_channel_free(p->channel);
                p->channel = NULL;
                close(p->fd);
                p->fd = -1;
                continue;
            }
            if (p->service) {
                tiku_msg_t *event = NULL;
                int n = tiku_resource_service_event(p->service, &p->caller,
                                                    &event);
                if (n > 0) {
                    assert(
                        !tiku_resource_channel_send(p->channel, event, 2000));
                    tiku_msg_free(event);
                }
            }
        }
        (void)tiku_resource_jobs_pump(f.registry);
        if (f.cancelled && f.pending[0]) {
            assert(!tiku_resource_job_complete(f.registry, f.pending, NULL,
                                               TIKU_RESOURCE_CANCELLED,
                                               TIKU_RESOURCE_EFFECT_NONE));
            f.pending[0] = 0;
            f.cancelled = 0;
        }
        usleep(1000);
    }
    pthread_join(worker, NULL);
    for (int i = 0; i < 4; i++)
        if (peers[i].channel) {
            tiku_resource_channel_free(peers[i].channel);
            close(peers[i].fd);
        }
    tiku_resource_endpoint_close(f.endpoint);
    tiku_resource_registry_free(f.registry);
    assert(!rmdir(f.directory));
    puts(
        "PASS SDK client: schema, grants, pagination, events, cancellation, streams and post-effect receipt reconciliation");
    return 0;
}
