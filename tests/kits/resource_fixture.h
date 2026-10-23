/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * resource_fixture.h - deterministic resource broker and controller fixtures.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef RESOURCE_FIXTURE_H_
#define RESOURCE_FIXTURE_H_
#include "tiku_resource_service.h"
#include "tiku_resource_channel.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RES_CHECK(test) do { if (!(test)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #test); exit(1); } } while (0)

typedef struct resource_fixture resource_fixture_t;
/** @brief Worker-owned inputs and test counters, separate from registry records. */
typedef struct {
    resource_fixture_t *fixture;
    unsigned starts, cancels;
    int reject, immediate;
    char last_job[33];
} resource_worker_t;

/** @brief Host state with an injected monotonic clock. */
struct resource_fixture {
    tiku_resource_registry_t *registry;
    tiku_resource_service_t *a, *b;
    tiku_resource_channel_t *channel;
    tiku_resource_caller_t caller;
    tiku_resource_stamp_t settings, work;
    resource_worker_t *worker;
    uint64_t now;
    unsigned changes;
    int slow;
};

/** @brief Read the fixture's monotonic time without accessing the registry. */
static inline uint64_t resource_test_clock(void *opaque)
{
    return ((resource_fixture_t *)opaque)->now;
}

/** @brief Produce independently owned desired/persisted/applied values. */
static inline void resource_test_snapshot(tiku_resource_snapshot_t *out, int enabled)
{
    memset(out, 0, sizeof *out);
    out->desired = tiku_resource_value_integer(TIKU_RESOURCE_BOOLEAN, enabled);
    out->persisted = tiku_resource_value_integer(TIKU_RESOURCE_BOOLEAN, enabled);
    out->applied = tiku_resource_value_integer(TIKU_RESOURCE_BOOLEAN, enabled);
    strcpy(out->apply_state, "applied");
    RES_CHECK(out->desired && out->persisted && out->applied);
}

/** @brief Update the fixture model through the synchronous controller. */
static inline tiku_resource_error_t resource_test_apply(void *opaque, const char *path,
    const tiku_msg_t *value, int invoke, tiku_resource_snapshot_t *out)
{
    resource_fixture_t *fixture = opaque;
    int enabled;
    (void)path; (void)invoke;
    RES_CHECK(tiku_msg_find_bool(value, "value", 0, &enabled));
    fixture->changes++;
    if (fixture->slow) fixture->now += 100;
    resource_test_snapshot(out, enabled);
    return TIKU_RESOURCE_OK;
}

/** @brief Validate a worker generation before acceptance and dispatch. */
static inline tiku_resource_error_t resource_test_validate(void *opaque, const char *path,
    const tiku_msg_t *value, int invoke)
{
    resource_worker_t *worker = opaque;
    (void)path; (void)value; (void)invoke;
    return worker->reject ? TIKU_RESOURCE_VALUE : TIKU_RESOURCE_OK;
}

/** @brief Accept copied work without performing blocking I/O on the owner thread. */
static inline tiku_resource_error_t resource_test_start(void *opaque, const char *job,
    const char *path, const tiku_msg_t *value, int invoke, tiku_resource_snapshot_t *out)
{
    resource_worker_t *worker = opaque;
    int enabled;
    (void)path; (void)invoke;
    worker->starts++; strcpy(worker->last_job, job);
    RES_CHECK(tiku_resource_unregister(worker->fixture->registry, &worker->fixture->work) == TIKU_RESOURCE_BUSY);
    if (!worker->immediate) return TIKU_RESOURCE_PENDING;
    RES_CHECK(tiku_msg_find_bool(value, "value", 0, &enabled));
    resource_test_snapshot(out, enabled);
    return worker->immediate == 2 ? TIKU_RESOURCE_PENDING : TIKU_RESOURCE_OK;
}

/** @brief Accept one cancellation attempt without claiming completed rollback. */
static inline tiku_resource_cancel_t resource_test_cancel(void *opaque, const char *id)
{
    resource_worker_t *worker = opaque;
    RES_CHECK(!strcmp(worker->last_job, id));
    worker->cancels++;
    return TIKU_RESOURCE_CANCEL_ACCEPTED;
}

/** @brief Register a private async setting under a replaceable owner. */
static inline void resource_test_register_work(resource_fixture_t *fixture)
{
    tiku_resource_initial_t initial = {0};
    tiku_resource_async_controller_t controller = {resource_test_validate, resource_test_start, resource_test_cancel, 100};
    tiku_resource_snapshot_t value;
    resource_test_snapshot(&value, 0);
    initial.spec = (tiku_resource_spec_t){.path="/work/enabled", .kind=TIKU_RESOURCE_SETTING,
        .schema_version=1, .schema={.type=TIKU_RESOURCE_BOOLEAN}, .read_scopes=TIKU_RESOURCE_READ,
        .change_scopes=TIKU_RESOURCE_WRITE, .sensitive=1, .persistence="user", .controller="worker"};
    initial.initial = value;
    fixture->worker = calloc(1, sizeof *fixture->worker); RES_CHECK(fixture->worker);
    fixture->worker->fixture = fixture;
    RES_CHECK(!tiku_resource_register_async(fixture->registry, "work", "/work", NULL,
        &initial, 1, &controller, fixture->worker, &fixture->work));
    tiku_resource_snapshot_clear(&value);
}

/** @brief Create two brokers with the same principal and separate handle tables. */
static inline void resource_test_open(resource_fixture_t *fixture, unsigned receipts)
{
    const char *paths[] = {"/settings/enabled", "/settings/other", "/settings/private/enabled"};
    tiku_resource_initial_t initial[3] = {0};
    tiku_resource_controller_t controller = {.apply=resource_test_apply};
    tiku_resource_limits_t limits = {.events_per_watch=2, .receipts=receipts};
    tiku_resource_snapshot_t value;
    unsigned i;
    memset(fixture, 0, sizeof *fixture);
    fixture->now = 1000; fixture->registry = tiku_resource_registry_new(&limits);
    RES_CHECK(fixture->registry && !tiku_resource_registry_clock(fixture->registry, resource_test_clock, fixture));
    memset(fixture->caller.principal, '1', 32); fixture->caller.principal[32]=0;
    fixture->caller.authenticated=1;
    fixture->caller.scopes=TIKU_RESOURCE_READ|TIKU_RESOURCE_WRITE|TIKU_RESOURCE_INVOKE|TIKU_RESOURCE_CONTENT|TIKU_RESOURCE_HOST_CONTROL;
    resource_test_snapshot(&value, 0);
    for (i=0; i<3; i++) {
        initial[i].spec=(tiku_resource_spec_t){.path=paths[i], .kind=TIKU_RESOURCE_SETTING,
            .schema_version=1, .schema={.type=TIKU_RESOURCE_BOOLEAN}, .read_scopes=TIKU_RESOURCE_READ,
            .change_scopes=TIKU_RESOURCE_WRITE, .public_read=i!=2, .sensitive=i==2,
            .persistence="user", .controller="fixture"};
        initial[i].initial=value;
    }
    RES_CHECK(!tiku_resource_register(fixture->registry,"settings","/settings",NULL,initial,3,&controller,fixture,&fixture->settings));
    tiku_resource_snapshot_clear(&value);
    resource_test_register_work(fixture);
    fixture->a=tiku_resource_service_new(fixture->registry,&fixture->caller,15);
    fixture->b=tiku_resource_service_new(fixture->registry,&fixture->caller,15);
    RES_CHECK(fixture->a && fixture->b);
}

/** @brief Release brokers before their registry and controller storage. */
static inline void resource_test_close(resource_fixture_t *fixture)
{
    tiku_resource_service_free(fixture->a); tiku_resource_service_free(fixture->b);
    tiku_resource_registry_free(fixture->registry); free(fixture->worker);
}

/** @brief Create a correlated request for the fixture's session. */
static inline tiku_msg_t *resource_test_request(resource_fixture_t *fixture, const char *op,
    const char *path, unsigned serial)
{
    char id[33];
    tiku_msg_t *request=tiku_msg_new(TIKU_RESOURCE_MESSAGE_CODE);
    snprintf(id,sizeof id,"%032x",serial);
    RES_CHECK(request && tiku_msg_add_string(request,"op",op) && tiku_msg_add_string(request,"request_id",id) &&
        tiku_msg_add_string(request,"session",tiku_resource_registry_session(fixture->registry)));
    if (path) RES_CHECK(tiku_msg_add_string(request,"path",path));
    return request;
}

/** @brief Add a revision tuple to a mutation or cancellation. */
static inline void resource_test_expected(tiku_msg_t *request, const tiku_resource_stamp_t *stamp)
{
    char revision[21]; tiku_msg_t *tuple=tiku_msg_new(0);
    snprintf(revision,sizeof revision,"%llu",(unsigned long long)stamp->revision);
    RES_CHECK(tuple && tiku_msg_add_string(tuple,"provider",stamp->provider) &&
        tiku_msg_add_string(tuple,"incarnation",stamp->incarnation) && tiku_msg_add_string(tuple,"revision",revision) &&
        tiku_msg_add_message(request,"expected",tuple));
    tiku_msg_free(tuple);
}

/** @brief Create a complete user-persistent boolean replacement. */
static inline tiku_msg_t *resource_test_set(resource_fixture_t *fixture, const char *path,
    const tiku_resource_stamp_t *stamp, unsigned serial, int enabled)
{
    tiku_msg_t *request=resource_test_request(fixture,"set",path,serial);
    tiku_msg_t *value=tiku_resource_value_integer(TIKU_RESOURCE_BOOLEAN,enabled);
    resource_test_expected(request,stamp);
    RES_CHECK(value && tiku_msg_add_message(request,"value",value) &&
        tiku_msg_add_string(request,"durability","user") && tiku_msg_add_int32(request,"deadline_ms",50));
    tiku_msg_free(value); return request;
}

/** @brief Dispatch and return an owned result/error after checking its code. */
static inline tiku_msg_t *resource_test_call(tiku_resource_service_t *service,
    const tiku_resource_caller_t *caller, const tiku_msg_t *request, const char *error)
{
    tiku_msg_t *reply=tiku_resource_service_dispatch(service,caller,request), *result;
    RES_CHECK(reply && !strcmp(tiku_msg_find_string(reply,"request_id",0),tiku_msg_find_string(request,"request_id",0)));
    result=tiku_msg_find_message(reply,error?"error":"result",0);
    RES_CHECK(result && !tiku_msg_count(reply,error?"result":"error"));
    if (error) RES_CHECK(!strcmp(tiku_msg_find_string(result,"code",0),error));
    tiku_msg_free(reply); return result;
}

/** @brief Decode a status stamp for a subsequent conditional request. */
static inline tiku_resource_stamp_t resource_test_stamp(const tiku_msg_t *message)
{
    tiku_resource_stamp_t stamp={0};
    const char *provider=tiku_msg_find_string(message,"provider",0), *inc=tiku_msg_find_string(message,"incarnation",0);
    RES_CHECK(provider && inc && strlen(provider)<sizeof stamp.provider && tiku_resource_id_valid(inc));
    strcpy(stamp.provider,provider); strcpy(stamp.incarnation,inc);
    RES_CHECK(!tiku_resource_counter_parse(tiku_msg_find_string(message,"revision",0),&stamp.revision));
    return stamp;
}

#endif
