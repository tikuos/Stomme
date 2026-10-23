/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_resource_provider.c - assigned remote provider lifetimes and local clients.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "resource_fixture.h"
#include "tiku_resource_provider_client.h"
#include <pthread.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

/** @brief Host fixture and a separately authorized application connection. */
typedef struct {
    resource_fixture_t host;
    tiku_resource_caller_t caller;
    tiku_resource_service_t *service;
    tiku_resource_stamp_t stamp;
    char prefix[64], path[80];
} provider_fixture_t;

/** @brief Construct an assigned provider while retaining a cross-app administrator. */
static void open_provider(provider_fixture_t *fixture, int enable)
{
    memset(fixture, 0, sizeof *fixture); resource_test_open(&fixture->host, 0);
    tiku_resource_service_free(fixture->host.a); tiku_resource_service_free(fixture->host.b);
    fixture->host.caller.scopes |= TIKU_RESOURCE_CROSS_APP;
    fixture->host.a = tiku_resource_service_new(fixture->host.registry, &fixture->host.caller, 31);
    fixture->host.b = tiku_resource_service_new(fixture->host.registry, &fixture->host.caller, 31);
    fixture->caller = fixture->host.caller; memset(fixture->caller.principal, '2', 32);
    fixture->caller.scopes = TIKU_RESOURCE_PROVIDER | TIKU_RESOURCE_READ | TIKU_RESOURCE_CONTENT | TIKU_RESOURCE_WRITE | TIKU_RESOURCE_INVOKE;
    fixture->service = tiku_resource_service_new(fixture->host.registry, &fixture->caller, TIKU_RESOURCE_FEATURE_PROVIDER);
    RES_CHECK(fixture->host.a && fixture->host.b && fixture->service);
    snprintf(fixture->prefix, sizeof fixture->prefix, "/apps/%s", fixture->caller.principal);
    snprintf(fixture->path, sizeof fixture->path, "%s/enabled", fixture->prefix);
    if (enable) RES_CHECK(!tiku_resource_service_provider_enable(fixture->service));
}

/** @brief Retire the provider before releasing its host registry. */
static void close_provider(provider_fixture_t *fixture)
{
    tiku_resource_service_free(fixture->service); resource_test_close(&fixture->host);
}

/** @brief Create a bounded application-owned boolean setting descriptor. */
static tiku_resource_initial_t initial(const char *path)
{
    tiku_resource_initial_t item = {0};
    item.spec = (tiku_resource_spec_t){.path=path, .kind=TIKU_RESOURCE_SETTING, .schema_version=1,
        .schema={.type=TIKU_RESOURCE_BOOLEAN}, .persistence="user"};
    resource_test_snapshot(&item.initial, 0); return item;
}

/** @brief Build an atomic registration packet containing a copied entry. */
static tiku_msg_t *register_request(provider_fixture_t *fixture, const char *path, unsigned serial)
{
    tiku_resource_initial_t item = initial(path);
    tiku_msg_t *entry = tiku_resource_initial_message(&item), *resources = tiku_msg_new(0);
    tiku_msg_t *request = resource_test_request(&fixture->host, "provider-register", NULL, serial);
    RES_CHECK(entry && resources && tiku_msg_add_message(resources, "entry", entry) && tiku_msg_add_message(request, "resources", resources));
    tiku_resource_snapshot_clear(&item.initial); tiku_msg_free(entry); tiku_msg_free(resources); return request;
}

/** @brief Register the fixture and copy its host-created incarnation. */
static void register_provider(provider_fixture_t *fixture)
{
    tiku_msg_t *request = register_request(fixture, fixture->path, 1);
    tiku_msg_t *result = resource_test_call(fixture->service, &fixture->caller, request, NULL);
    fixture->stamp = resource_test_stamp(result);
    RES_CHECK(!strcmp(tiku_msg_find_string(result, "prefix", 0), fixture->prefix));
    tiku_msg_free(result); tiku_msg_free(request);
}

/** @brief Build a conditional GUI snapshot publication. */
static tiku_msg_t *publish_request(provider_fixture_t *fixture, const char *path, unsigned serial, int enabled)
{
    tiku_resource_snapshot_t snapshot;
    tiku_msg_t *request = resource_test_request(&fixture->host, "provider-publish", path, serial), *value;
    resource_test_snapshot(&snapshot, enabled); value = tiku_resource_snapshot_message(&snapshot);
    resource_test_expected(request, &fixture->stamp); RES_CHECK(value && tiku_msg_add_message(request, "snapshot", value));
    tiku_msg_free(value); tiku_resource_snapshot_clear(&snapshot); return request;
}

/** @brief Retain one consumer request and return its public job identity. */
static void submit(provider_fixture_t *fixture, unsigned serial, int enabled, char id[33])
{
    tiku_msg_t *request = resource_test_set(&fixture->host, fixture->path, &fixture->stamp, serial, enabled);
    tiku_msg_t *result = resource_test_call(fixture->host.a, &fixture->host.caller, request, NULL);
    strcpy(id, tiku_msg_find_string(result, "job", 0)); tiku_msg_free(result); tiku_msg_free(request);
}

/** @brief Read a retained consumer job and check its terminal or active state. */
static void job_state(provider_fixture_t *fixture, const char *id, tiku_resource_job_state_t state, tiku_resource_error_t error)
{
    tiku_resource_job_info_t info;
    RES_CHECK(!tiku_resource_job_status(fixture->host.registry, &fixture->host.caller, id, &info));
    RES_CHECK(info.state == state && info.error == error); tiku_resource_job_info_clear(&info);
}

/** @brief Build an owner completion tied to one delivered event. */
static tiku_msg_t *completion(provider_fixture_t *fixture, const tiku_msg_t *event, unsigned serial, int enabled)
{
    tiku_msg_t *request = resource_test_request(&fixture->host, "provider-complete", NULL, serial);
    tiku_msg_t *expected = tiku_msg_find_message(event, "expected", 0), *value;
    tiku_resource_snapshot_t snapshot;
    resource_test_snapshot(&snapshot, enabled); value = tiku_resource_snapshot_message(&snapshot);
    RES_CHECK(expected && value && tiku_msg_add_message(request, "expected", expected) &&
        tiku_msg_add_string(request, "dispatch", tiku_msg_find_string(event, "dispatch", 0)) &&
        tiku_msg_add_string(request, "code", "ok") && tiku_msg_add_string(request, "effect", "partial") &&
        tiku_msg_add_message(request, "snapshot", value));
    tiku_msg_free(expected); tiku_msg_free(value); tiku_resource_snapshot_clear(&snapshot); return request;
}

/** @brief Require explicit assignment, negotiated provider support and caller scope. */
static void assignment_test(void)
{
    provider_fixture_t fixture;
    tiku_resource_service_t *other;
    tiku_resource_caller_t limited;
    tiku_msg_t *request, *result;
    open_provider(&fixture, 0); request = register_request(&fixture, fixture.path, 1);
    result = resource_test_call(fixture.service, &fixture.caller, request, "permission_denied"); tiku_msg_free(result);
    RES_CHECK(!tiku_resource_service_provider_enable(fixture.service));
    other = tiku_resource_service_new(fixture.host.registry, &fixture.caller, 16); RES_CHECK(other);
    RES_CHECK(tiku_resource_service_provider_enable(other) == TIKU_RESOURCE_BUSY); tiku_resource_service_free(other);
    limited = fixture.caller; limited.scopes &= ~TIKU_RESOURCE_PROVIDER;
    other = tiku_resource_service_new(fixture.host.registry, &limited, 16); RES_CHECK(other);
    RES_CHECK(tiku_resource_service_provider_enable(other) == TIKU_RESOURCE_DENIED);
    result = resource_test_call(other, &limited, request, "permission_denied"); tiku_msg_free(result); tiku_resource_service_free(other);
    other = tiku_resource_service_new(fixture.host.registry, &fixture.caller, 15); RES_CHECK(other);
    RES_CHECK(tiku_resource_service_provider_enable(other) == TIKU_RESOURCE_DENIED);
    result = resource_test_call(other, &fixture.caller, request, "unsupported"); tiku_msg_free(result); tiku_resource_service_free(other);
    tiku_msg_free(request); close_provider(&fixture);
}

/** @brief Reject namespace escapes and malformed registrations without partial nodes. */
static void registration_test(void)
{
    provider_fixture_t fixture;
    tiku_resource_snapshot_t snapshot;
    tiku_msg_t *request, *result, *resources, *entry;
    int variant;
    const char *paths[] = {"/settings/hijack", "/apps/33333333333333333333333333333333/enabled", "/apps/22222222222222222222222222222222other/enabled"};
    open_provider(&fixture, 1);
    for (variant = 0; variant < 3; variant++) {
        request = register_request(&fixture, paths[variant], (unsigned)variant + 1);
        result = resource_test_call(fixture.service, &fixture.caller, request, "permission_denied"); tiku_msg_free(result); tiku_msg_free(request);
    }
    for (variant = 0; variant < 4; variant++) {
        request = register_request(&fixture, fixture.path, (unsigned)variant + 10);
        resources = tiku_msg_find_message(request, "resources", 0); entry = tiku_msg_find_message(resources, "entry", 0);
        if (variant == 0) RES_CHECK(tiku_msg_add_bool(entry, "public_read", 1));
        if (variant == 1) RES_CHECK(tiku_msg_add_int64(entry, "read_scopes", 0));
        tiku_msg_remove(resources, "entry"); RES_CHECK(tiku_msg_add_message(resources, "entry", entry));
        if (variant == 2) RES_CHECK(tiku_msg_add_message(resources, "entry", entry));
        if (variant == 3) { int i; for (i = 0; i < 32; i++) RES_CHECK(tiku_msg_add_message(resources, "entry", entry)); }
        tiku_msg_remove(request, "resources"); RES_CHECK(tiku_msg_add_message(request, "resources", resources));
        result = resource_test_call(fixture.service, &fixture.caller, request, "invalid_request"); tiku_msg_free(result);
        RES_CHECK(tiku_resource_read(fixture.host.registry, &fixture.host.caller, fixture.path, &snapshot) == TIKU_RESOURCE_NOT_FOUND);
        tiku_msg_free(request); tiku_msg_free(resources); tiku_msg_free(entry);
    }
    register_provider(&fixture);
    request = resource_test_request(&fixture.host, "describe", fixture.path, 30);
    result = resource_test_call(fixture.host.a, &fixture.host.caller, request, NULL);
    RES_CHECK(!strcmp(tiku_msg_find_string(result, "dispatch", 0), "asynchronous")); tiku_msg_free(result); tiku_msg_free(request);
    close_provider(&fixture);
}

/** @brief Preserve scalar schema bounds and reject authority fields in the codec. */
static void codec_test(void)
{
    tiku_resource_initial_t item = initial("/apps/22222222222222222222222222222222/value"), decoded;
    const char *choices[] = {"alpha", "beta"};
    tiku_msg_t *message;
    item.spec.schema = (tiku_resource_schema_t){.type=TIKU_RESOURCE_ENUM, .choices=choices, .choice_count=2};
    tiku_resource_snapshot_clear(&item.initial);
    item.initial.desired = tiku_resource_value_string(TIKU_RESOURCE_ENUM, "alpha");
    item.initial.persisted = tiku_msg_copy(item.initial.desired); item.initial.applied = tiku_msg_copy(item.initial.desired);
    strcpy(item.initial.apply_state, "applied");
    message = tiku_resource_initial_message(&item); RES_CHECK(message && !tiku_resource_initial_decode(message, &decoded));
    RES_CHECK(decoded.spec.schema.choice_count == 2 && !strcmp(decoded.spec.schema.choices[1], "beta"));
    tiku_resource_initial_clear(&decoded); RES_CHECK(tiku_msg_add_string(message, "controller", "host"));
    RES_CHECK(tiku_resource_initial_decode(message, &decoded) == TIKU_RESOURCE_INVALID);
    tiku_resource_initial_clear(&decoded); tiku_msg_free(message); tiku_resource_snapshot_clear(&item.initial);
    item = initial("/apps/22222222222222222222222222222222/value");
    item.spec.schema = (tiku_resource_schema_t){.type=TIKU_RESOURCE_INTEGER, .integer_min=-22, .integer_max=33};
    message = tiku_resource_initial_message(&item); RES_CHECK(message && !tiku_resource_initial_decode(message, &decoded));
    RES_CHECK(decoded.spec.schema.integer_min == -22 && decoded.spec.schema.integer_max == 33);
    tiku_resource_initial_clear(&decoded); tiku_msg_free(message); tiku_resource_snapshot_clear(&item.initial);
}

/** @brief Enforce private reads, conditional publication and retained request identity. */
static void publication_test(void)
{
    provider_fixture_t fixture;
    tiku_resource_caller_t limited;
    tiku_resource_snapshot_t snapshot;
    tiku_resource_watch_t watch;
    tiku_resource_event_t event;
    tiku_msg_t *request, *result, *initial;
    uint64_t revision;
    open_provider(&fixture, 1); register_provider(&fixture);
    limited = fixture.host.caller; limited.scopes &= ~TIKU_RESOURCE_CROSS_APP;
    RES_CHECK(tiku_resource_read(fixture.host.registry, &limited, fixture.path, &snapshot) == TIKU_RESOURCE_NOT_FOUND);
    limited = fixture.host.caller; limited.scopes &= ~TIKU_RESOURCE_CONTENT;
    RES_CHECK(tiku_resource_read(fixture.host.registry, &limited, fixture.path, &snapshot) == TIKU_RESOURCE_NOT_FOUND);
    RES_CHECK(!tiku_resource_watch_open(fixture.host.registry, &fixture.host.caller, fixture.stamp.provider, fixture.prefix, &watch, &initial));
    tiku_msg_free(initial); revision = fixture.stamp.revision;
    request = publish_request(&fixture, fixture.path, 2, 1);
    result = resource_test_call(fixture.service, &fixture.caller, request, NULL); fixture.stamp = resource_test_stamp(result);
    RES_CHECK(fixture.stamp.revision == revision + 1); tiku_msg_free(result);
    RES_CHECK(tiku_resource_watch_next(fixture.host.registry, watch, &event) == 1 && !event.removed);
    result = resource_test_call(fixture.service, &fixture.caller, request, NULL);
    RES_CHECK(resource_test_stamp(result).revision == fixture.stamp.revision); tiku_msg_free(result);
    RES_CHECK(tiku_resource_watch_next(fixture.host.registry, watch, &event) == 0);
    tiku_msg_remove(request, "op"); RES_CHECK(tiku_msg_add_string(request, "op", "provider-publish"));
    result = resource_test_call(fixture.service, &fixture.caller, request, NULL); tiku_msg_free(result);
    RES_CHECK(tiku_resource_watch_next(fixture.host.registry, watch, &event) == 0);
    tiku_msg_remove(request, "path"); RES_CHECK(tiku_msg_add_string(request, "path", "/settings/enabled"));
    result = resource_test_call(fixture.service, &fixture.caller, request, "request_id_reused"); tiku_msg_free(result); tiku_msg_free(request);
    request = publish_request(&fixture, "/settings/enabled", 3, 0);
    result = resource_test_call(fixture.service, &fixture.caller, request, "permission_denied"); tiku_msg_free(result); tiku_msg_free(request);
    RES_CHECK(!tiku_resource_read(fixture.host.registry, &fixture.caller, fixture.path, &snapshot));
    { int enabled; RES_CHECK(tiku_msg_find_bool(snapshot.applied, "value", 0, &enabled) && enabled); }
    tiku_resource_snapshot_clear(&snapshot); tiku_resource_watch_close(fixture.host.registry, watch);
    close_provider(&fixture);
}

/** @brief Dispatch once and reject forged, changed and repeated completions. */
static void dispatch_test(void)
{
    provider_fixture_t fixture;
    char job[33];
    tiku_msg_t *event, *request, *result, *origin, *again = NULL;
    open_provider(&fixture, 1); register_provider(&fixture); submit(&fixture, 10, 1, job);
    RES_CHECK(tiku_resource_service_pump(fixture.service) == 1);
    RES_CHECK(tiku_resource_service_event(fixture.service, &fixture.caller, &event) == 1);
    RES_CHECK(!strcmp(tiku_msg_find_string(event, "kind", 0), "provider-dispatch") && !strcmp(tiku_msg_find_string(event, "job", 0), job));
    origin = tiku_msg_find_message(event, "origin", 0);
    { int64_t scopes; RES_CHECK(tiku_msg_find_int64(origin, "scopes", 0, &scopes) && !(scopes & (TIKU_RESOURCE_HOST_CONTROL | TIKU_RESOURCE_CROSS_APP))); }
    tiku_msg_free(origin);
    RES_CHECK(tiku_resource_service_event(fixture.service, &fixture.caller, &again) == 0 && !again);
    request = completion(&fixture, event, 11, 1);
    tiku_msg_remove(request, "dispatch"); RES_CHECK(tiku_msg_add_string(request, "dispatch", job));
    result = resource_test_call(fixture.service, &fixture.caller, request, "owner_lost"); tiku_msg_free(result); tiku_msg_free(request);
    job_state(&fixture, job, TIKU_RESOURCE_JOB_RUNNING, TIKU_RESOURCE_OK);
    {
        provider_fixture_t other = fixture;
        memset(other.caller.principal, '3', 32);
        snprintf(other.prefix, sizeof other.prefix, "/apps/%s", other.caller.principal);
        snprintf(other.path, sizeof other.path, "%s/enabled", other.prefix);
        other.service = tiku_resource_service_new(fixture.host.registry, &other.caller, 16);
        RES_CHECK(other.service && !tiku_resource_service_provider_enable(other.service)); register_provider(&other);
        request = completion(&fixture, event, 11, 1);
        result = resource_test_call(other.service, &other.caller, request, "owner_lost");
        tiku_msg_free(result); tiku_msg_free(request); tiku_resource_service_free(other.service);
    }
    request = completion(&fixture, event, 12, 1);
    result = resource_test_call(fixture.service, &fixture.caller, request, NULL); fixture.stamp = resource_test_stamp(result); tiku_msg_free(result);
    job_state(&fixture, job, TIKU_RESOURCE_JOB_SUCCEEDED, TIKU_RESOURCE_OK);
    result = resource_test_call(fixture.service, &fixture.caller, request, NULL); tiku_msg_free(result);
    tiku_msg_remove(request, "request_id"); RES_CHECK(tiku_msg_add_string(request, "request_id", "00000000000000000000000000000013"));
    result = resource_test_call(fixture.service, &fixture.caller, request, "owner_lost"); tiku_msg_free(result);
    tiku_msg_free(event); tiku_msg_free(request); close_provider(&fixture);
}

/** @brief Publish progress and route one cancellation request to the active owner. */
static void cancellation_test(void)
{
    provider_fixture_t fixture;
    tiku_resource_job_info_t info;
    tiku_resource_cancel_t cancellation;
    char job[33];
    tiku_msg_t *event, *cancel_event, *request, *result, *expected;
    open_provider(&fixture, 1); register_provider(&fixture); submit(&fixture, 10, 1, job);
    RES_CHECK(tiku_resource_service_pump(fixture.service) == 1 && tiku_resource_service_event(fixture.service, &fixture.caller, &event) == 1);
    request = resource_test_request(&fixture.host, "provider-progress", NULL, 11);
    expected = tiku_msg_find_message(event, "expected", 0);
    RES_CHECK(tiku_msg_add_message(request, "expected", expected) && tiku_msg_add_string(request, "dispatch", tiku_msg_find_string(event, "dispatch", 0)) &&
        tiku_msg_add_int32(request, "progress", 250) && tiku_msg_add_bool(request, "awaiting_confirmation", 1));
    result = resource_test_call(fixture.service, &fixture.caller, request, NULL); tiku_msg_free(result); tiku_msg_free(request); tiku_msg_free(expected);
    RES_CHECK(!tiku_resource_job_status(fixture.host.registry, &fixture.host.caller, job, &info) && info.progress == 250 && info.state == TIKU_RESOURCE_JOB_AWAITING_CONFIRMATION);
    RES_CHECK(!tiku_resource_job_cancel(fixture.host.registry, &fixture.host.caller, job, &info.stamp, &cancellation) && cancellation == TIKU_RESOURCE_CANCEL_ACCEPTED);
    tiku_resource_job_info_clear(&info);
    RES_CHECK(tiku_resource_service_event(fixture.service, &fixture.caller, &cancel_event) == 1 &&
        !strcmp(tiku_msg_find_string(cancel_event, "kind", 0), "provider-cancel"));
    tiku_msg_free(cancel_event); RES_CHECK(tiku_resource_service_event(fixture.service, &fixture.caller, &cancel_event) == 0);
    request = completion(&fixture, event, 12, 0); tiku_msg_remove(request, "snapshot"); tiku_msg_remove(request, "code"); tiku_msg_remove(request, "effect");
    RES_CHECK(tiku_msg_add_string(request, "code", "cancelled") && tiku_msg_add_string(request, "effect", "none"));
    result = resource_test_call(fixture.service, &fixture.caller, request, NULL); tiku_msg_free(result);
    job_state(&fixture, job, TIKU_RESOURCE_JOB_CANCELLED, TIKU_RESOURCE_CANCELLED);
    tiku_msg_free(event); tiku_msg_free(request); close_provider(&fixture);
}

/** @brief Reject stale owner results and retire queued/running work on disconnect. */
static void lifetime_test(void)
{
    provider_fixture_t fixture;
    char running_job[33], queued_job[33], old_incarnation[33];
    tiku_msg_t *event, *request, *result;
    tiku_resource_snapshot_t snapshot;
    open_provider(&fixture, 1); register_provider(&fixture); strcpy(old_incarnation, fixture.stamp.incarnation);
    submit(&fixture, 10, 1, running_job); RES_CHECK(tiku_resource_service_pump(fixture.service) == 1);
    RES_CHECK(tiku_resource_service_event(fixture.service, &fixture.caller, &event) == 1);
    request = publish_request(&fixture, fixture.path, 11, 1);
    result = resource_test_call(fixture.service, &fixture.caller, request, NULL); fixture.stamp = resource_test_stamp(result); tiku_msg_free(result); tiku_msg_free(request);
    request = completion(&fixture, event, 12, 0); tiku_msg_remove(request, "snapshot"); tiku_msg_remove(request, "code"); tiku_msg_remove(request, "effect");
    RES_CHECK(tiku_msg_add_string(request, "code", "stale_revision") && tiku_msg_add_string(request, "effect", "none"));
    result = resource_test_call(fixture.service, &fixture.caller, request, "stale_revision"); tiku_msg_free(result); tiku_msg_free(request); tiku_msg_free(event);
    job_state(&fixture, running_job, TIKU_RESOURCE_JOB_FAILED, TIKU_RESOURCE_STALE);
    submit(&fixture, 20, 0, running_job); submit(&fixture, 21, 0, queued_job);
    RES_CHECK(tiku_resource_service_pump(fixture.service) == 1 && tiku_resource_service_event(fixture.service, &fixture.caller, &event) == 1);
    request = completion(&fixture, event, 22, 0); tiku_msg_free(event);
    tiku_resource_service_free(fixture.service); fixture.service = NULL;
    job_state(&fixture, running_job, TIKU_RESOURCE_JOB_UNKNOWN, TIKU_RESOURCE_UNKNOWN_OUTCOME);
    job_state(&fixture, queued_job, TIKU_RESOURCE_JOB_FAILED, TIKU_RESOURCE_OWNER_LOST);
    RES_CHECK(tiku_resource_read(fixture.host.registry, &fixture.host.caller, fixture.path, &snapshot) == TIKU_RESOURCE_NOT_FOUND);
    fixture.service = tiku_resource_service_new(fixture.host.registry, &fixture.caller, 16);
    RES_CHECK(fixture.service && !tiku_resource_service_provider_enable(fixture.service)); register_provider(&fixture);
    RES_CHECK(strcmp(old_incarnation, fixture.stamp.incarnation));
    result = resource_test_call(fixture.service, &fixture.caller, request, "owner_lost"); tiku_msg_free(result); tiku_msg_free(request);
    submit(&fixture, 25, 1, running_job); RES_CHECK(tiku_resource_service_pump(fixture.service) == 1);
    RES_CHECK(tiku_resource_service_event(fixture.service, &fixture.caller, &event) == 1);
    request = publish_request(&fixture, fixture.path, 26, 1);
    result = resource_test_call(fixture.service, &fixture.caller, request, NULL); fixture.stamp = resource_test_stamp(result); tiku_msg_free(result); tiku_msg_free(request);
    request = completion(&fixture, event, 27, 0);
    result = resource_test_call(fixture.service, &fixture.caller, request, "stale_revision");
    RES_CHECK(!strcmp(tiku_msg_find_string(result, "effect", 0), "unknown"));
    tiku_msg_free(result); tiku_msg_free(request); tiku_msg_free(event);
    job_state(&fixture, running_job, TIKU_RESOURCE_JOB_UNKNOWN, TIKU_RESOURCE_UNKNOWN_OUTCOME);
    request = resource_test_request(&fixture.host, "provider-status", NULL, 28);
    result = resource_test_call(fixture.service, &fixture.caller, request, NULL); fixture.stamp = resource_test_stamp(result); tiku_msg_free(result); tiku_msg_free(request);
    request = publish_request(&fixture, fixture.path, 29, 1);
    result = resource_test_call(fixture.service, &fixture.caller, request, NULL); fixture.stamp = resource_test_stamp(result); tiku_msg_free(result); tiku_msg_free(request);
    submit(&fixture, 30, 0, running_job); RES_CHECK(tiku_resource_service_pump(fixture.service) == 1);
    fixture.host.now += 300001; RES_CHECK(tiku_resource_service_pump(fixture.service) == 0);
    RES_CHECK(tiku_resource_service_event(fixture.service, &fixture.caller, &event) == 0);
    job_state(&fixture, running_job, TIKU_RESOURCE_JOB_UNKNOWN, TIKU_RESOURCE_UNKNOWN_OUTCOME); close_provider(&fixture);
}

/** @brief Trusted channel grant copied from the launch-bound fixture principal. */
static int authorize(void *opaque, uid_t uid, pid_t pid, tiku_resource_caller_t *out)
{
    provider_fixture_t *fixture = opaque;
    (void)pid; RES_CHECK(uid == geteuid()); *out = fixture->caller; return 1;
}

/** @brief Bind the negotiated provider role explicitly before dispatching requests. */
static tiku_msg_t *dispatch(void *opaque, const tiku_resource_caller_t *caller, const tiku_msg_t *request)
{
    provider_fixture_t *fixture = opaque;
    if (!fixture->service) {
        fixture->service = tiku_resource_service_new(fixture->host.registry, caller,
            tiku_resource_channel_features(fixture->host.channel));
        RES_CHECK(fixture->service && !tiku_resource_service_provider_enable(fixture->service));
    }
    return tiku_resource_service_dispatch(fixture->service, caller, request);
}

/** @brief Retire remote ownership before the authenticated stream context is freed. */
static void closed(void *opaque, const tiku_resource_caller_t *caller)
{
    provider_fixture_t *fixture = opaque;
    RES_CHECK(!strcmp(caller->principal, fixture->caller.principal));
    tiku_resource_service_free(fixture->service); fixture->service = NULL;
}

/** @brief Send an application request and consume its correlated provider reply. */
static void exchange(tiku_resource_channel_t *channel, tiku_resource_provider_client_t *client, tiku_msg_t *request)
{
    tiku_msg_t *reply = NULL;
    RES_CHECK(request && !tiku_resource_channel_send(channel, request, 5000) && !tiku_resource_channel_receive(channel, &reply, 5000));
    RES_CHECK(!tiku_resource_provider_client_reply(client, reply)); tiku_msg_free(reply); tiku_msg_free(request);
}

/** @brief Verify client access is restricted to its creating application thread. */
static void *wrong_thread(void *opaque)
{
    tiku_resource_provider_client_t *client = opaque;
    RES_CHECK(!tiku_resource_provider_client_prefix(client) && !tiku_resource_provider_client_status(client)); return NULL;
}

/** @brief Run an application owner in a process without registry pointers. */
static void client_process(int fd)
{
    tiku_resource_channel_t *channel = tiku_resource_channel_client(fd);
    tiku_resource_provider_client_t *client;
    tiku_resource_initial_t item;
    tiku_resource_provider_call_t call, duplicate;
    tiku_resource_snapshot_t snapshot;
    tiku_msg_t *event = NULL, *request;
    char path[80]; pthread_t thread;
    close(fd); RES_CHECK(channel && !tiku_resource_channel_negotiate(channel, 16, 0, 5000));
    client = tiku_resource_provider_client_new(channel); RES_CHECK(client);
    RES_CHECK(!pthread_create(&thread, NULL, wrong_thread, client) && !pthread_join(thread, NULL));
    snprintf(path, sizeof path, "%s/enabled", tiku_resource_provider_client_prefix(client)); item = initial(path);
    request = tiku_resource_provider_client_register(client, &item, 1); RES_CHECK(request && !tiku_resource_provider_client_status(client));
    exchange(channel, client, request); tiku_resource_snapshot_clear(&item.initial);
    RES_CHECK(!tiku_resource_channel_receive(channel, &event, 5000) && !tiku_resource_provider_client_event(client, event, &call));
    RES_CHECK(!strcmp(call.path, path) && !call.cancel && call.value && call.deadline_ms && !call.origin.authenticated);
    RES_CHECK(tiku_resource_provider_client_event(client, event, &duplicate) == TIKU_RESOURCE_ID_REUSED);
    tiku_resource_provider_call_clear(&duplicate); tiku_msg_free(event);
    exchange(channel, client, tiku_resource_provider_client_progress(client, call.dispatch, 400, 0));
    RES_CHECK(!tiku_resource_channel_receive(channel, &event, 5000) && !tiku_resource_provider_client_event(client, event, &duplicate) && duplicate.cancel);
    tiku_resource_provider_call_clear(&duplicate); tiku_msg_free(event);
    exchange(channel, client, tiku_resource_provider_client_complete(client, call.dispatch, NULL, TIKU_RESOURCE_CANCELLED, TIKU_RESOURCE_EFFECT_NONE));
    tiku_resource_provider_call_clear(&call);
    RES_CHECK(!tiku_resource_channel_receive(channel, &event, 5000) && !tiku_resource_provider_client_event(client, event, &call));
    { int enabled; RES_CHECK(tiku_msg_find_bool(call.value, "value", 0, &enabled)); resource_test_snapshot(&snapshot, enabled); }
    exchange(channel, client, tiku_resource_provider_client_complete(client, call.dispatch, &snapshot, TIKU_RESOURCE_OK, TIKU_RESOURCE_EFFECT_PARTIAL));
    RES_CHECK(tiku_resource_provider_client_event(client, event, &duplicate) == TIKU_RESOURCE_ID_REUSED);
    tiku_resource_provider_call_clear(&duplicate); tiku_msg_free(event);
    tiku_resource_provider_call_clear(&call); tiku_resource_snapshot_clear(&snapshot);
    resource_test_snapshot(&snapshot, 0); exchange(channel, client, tiku_resource_provider_client_publish(client, path, &snapshot));
    tiku_resource_snapshot_clear(&snapshot);
    exchange(channel, client, tiku_resource_provider_client_status(client));
    exchange(channel, client, tiku_resource_provider_client_unregister(client));
    item = initial(path); exchange(channel, client, tiku_resource_provider_client_register(client, &item, 1));
    tiku_resource_snapshot_clear(&item.initial);
    RES_CHECK(!tiku_resource_channel_receive(channel, &event, 5000) && !tiku_resource_provider_client_event(client, event, &call));
    tiku_msg_free(event); tiku_resource_provider_call_clear(&call);
    tiku_resource_provider_client_free(client); tiku_resource_channel_free(channel);
}

/** @brief Qualify provider SDK messages over an authenticated independent process. */
static void local_client_test(void)
{
    provider_fixture_t fixture;
    tiku_resource_channel_ops_t ops = {authorize, dispatch, closed};
    tiku_resource_snapshot_t snapshot;
    tiku_msg_t *event;
    char job[33], queued[33];
    int sockets[2], status; pid_t child;
    open_provider(&fixture, 0); tiku_resource_service_free(fixture.service); fixture.service = NULL;
    RES_CHECK(!socketpair(AF_UNIX, SOCK_STREAM, 0, sockets)); fflush(NULL); child = fork(); RES_CHECK(child >= 0);
    if (!child) { close(sockets[0]); client_process(sockets[1]); exit(0); }
    close(sockets[1]); fixture.host.channel = tiku_resource_channel_server(sockets[0],
        tiku_resource_registry_session(fixture.host.registry), 31, &ops, &fixture); close(sockets[0]); RES_CHECK(fixture.host.channel);
    RES_CHECK(!tiku_resource_channel_step(fixture.host.channel, 5000));
    RES_CHECK(!tiku_resource_channel_step(fixture.host.channel, 5000));
    RES_CHECK(!tiku_resource_read(fixture.host.registry, &fixture.host.caller, fixture.path, &snapshot));
    fixture.stamp = snapshot.stamp; tiku_resource_snapshot_clear(&snapshot);
    submit(&fixture, 10, 1, job); RES_CHECK(tiku_resource_service_pump(fixture.service) == 1);
    RES_CHECK(tiku_resource_service_event(fixture.service, &fixture.caller, &event) == 1 && !tiku_resource_channel_send(fixture.host.channel, event, 5000)); tiku_msg_free(event);
    RES_CHECK(!tiku_resource_channel_step(fixture.host.channel, 5000));
    {
        tiku_resource_job_info_t info; tiku_resource_cancel_t cancellation;
        RES_CHECK(!tiku_resource_job_status(fixture.host.registry, &fixture.host.caller, job, &info) && info.progress == 400);
        RES_CHECK(!tiku_resource_job_cancel(fixture.host.registry, &fixture.host.caller, job, &info.stamp, &cancellation) && cancellation == TIKU_RESOURCE_CANCEL_ACCEPTED);
        tiku_resource_job_info_clear(&info);
    }
    RES_CHECK(tiku_resource_service_event(fixture.service, &fixture.caller, &event) == 1 && !tiku_resource_channel_send(fixture.host.channel, event, 5000)); tiku_msg_free(event);
    RES_CHECK(!tiku_resource_channel_step(fixture.host.channel, 5000)); job_state(&fixture, job, TIKU_RESOURCE_JOB_CANCELLED, TIKU_RESOURCE_CANCELLED);
    submit(&fixture, 11, 1, job); RES_CHECK(tiku_resource_service_pump(fixture.service) == 1);
    RES_CHECK(tiku_resource_service_event(fixture.service, &fixture.caller, &event) == 1 && !tiku_resource_channel_send(fixture.host.channel, event, 5000)); tiku_msg_free(event);
    RES_CHECK(!tiku_resource_channel_step(fixture.host.channel, 5000)); job_state(&fixture, job, TIKU_RESOURCE_JOB_SUCCEEDED, TIKU_RESOURCE_OK);
    RES_CHECK(!tiku_resource_channel_step(fixture.host.channel, 5000));
    RES_CHECK(!tiku_resource_channel_step(fixture.host.channel, 5000));
    RES_CHECK(!tiku_resource_channel_step(fixture.host.channel, 5000));
    RES_CHECK(!tiku_resource_channel_step(fixture.host.channel, 5000));
    RES_CHECK(!tiku_resource_read(fixture.host.registry, &fixture.host.caller, fixture.path, &snapshot));
    fixture.stamp = snapshot.stamp; { int enabled; RES_CHECK(tiku_msg_find_bool(snapshot.applied, "value", 0, &enabled) && !enabled); } tiku_resource_snapshot_clear(&snapshot);
    submit(&fixture, 20, 1, job); submit(&fixture, 21, 1, queued); RES_CHECK(tiku_resource_service_pump(fixture.service) == 1);
    RES_CHECK(tiku_resource_service_event(fixture.service, &fixture.caller, &event) == 1 && !tiku_resource_channel_send(fixture.host.channel, event, 5000)); tiku_msg_free(event);
    RES_CHECK(tiku_resource_channel_step(fixture.host.channel, 5000) == -1 && !fixture.service);
    RES_CHECK(waitpid(child, &status, 0) == child && WIFEXITED(status) && !WEXITSTATUS(status));
    job_state(&fixture, job, TIKU_RESOURCE_JOB_UNKNOWN, TIKU_RESOURCE_UNKNOWN_OUTCOME);
    job_state(&fixture, queued, TIKU_RESOURCE_JOB_FAILED, TIKU_RESOURCE_OWNER_LOST);
    tiku_resource_channel_free(fixture.host.channel); close_provider(&fixture);
}

int main(int argc, char **argv)
{
    const struct { const char *name; void (*run)(void); } tests[] = {
        {"assignment", assignment_test}, {"registration", registration_test}, {"codec", codec_test},
        {"publication", publication_test}, {"dispatch", dispatch_test}, {"cancellation", cancellation_test},
        {"lifetime", lifetime_test}, {"local-client", local_client_test}
    };
    size_t i;
    for (i = 0; i < sizeof tests / sizeof tests[0]; i++) if (argc < 2 || !strcmp(argv[1], tests[i].name)) {
        tests[i].run(); printf("PASS resource provider %s\n", tests[i].name);
    }
    return 0;
}
