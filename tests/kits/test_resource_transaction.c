/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_resource_transaction.c - atomic persistence, publication and replay.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "resource_fixture.h"
#include "tiku_resource_transaction.h"
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

/** @brief Prepared file replacement and planned controller values. */
typedef struct { int next[3]; char staged[256]; } prepared_t;
/** @brief Owner model, persistence path, injected failures and callback counts. */
typedef struct {
    resource_fixture_t host;
    tiku_resource_stamp_t stamp;
    int model[3], mode;
    unsigned prepares, commits, discards;
    char directory[128], file[160];
} atomic_fixture_t;

static const char *const paths[] = {"/atomic/a", "/atomic/b", "/atomic/private/c", "/atomic/status", "/atomic/run"};

/** @brief Resolve the controller's declared settings. */
static unsigned index_for(const char *path)
{
    unsigned i;
    for (i = 0; i < 3; i++) if (!strcmp(paths[i], path)) return i;
    RES_CHECK(0); return 0;
}

/** @brief Validate and stage a complete file without replacing the live file. */
static tiku_resource_error_t prepare(void *opaque, const tiku_resource_replacement_t *items,
    size_t count, tiku_resource_snapshot_t *planned, void **token)
{
    atomic_fixture_t *fixture = opaque;
    prepared_t *state = calloc(1, sizeof *state);
    FILE *stream; int fd; size_t i;
    fixture->prepares++; RES_CHECK(state); *token = state;
    RES_CHECK(tiku_resource_unregister(fixture->host.registry, &fixture->stamp) == TIKU_RESOURCE_BUSY);
    memcpy(state->next, fixture->model, sizeof state->next);
    for (i = 0; i < count; i++) {
        int enabled; RES_CHECK(tiku_msg_find_bool(items[i].value, "value", 0, &enabled));
        state->next[index_for(items[i].path)] = enabled; resource_test_snapshot(&planned[i], enabled);
    }
    if (fixture->mode == 1) return TIKU_RESOURCE_VALUE;
    if (fixture->mode == 2) {
        tiku_msg_free(planned[count-1].applied); planned[count-1].applied = tiku_resource_value_string(TIKU_RESOURCE_TEXT, "wrong");
    }
    snprintf(state->staged, sizeof state->staged, "%s/pendingXXXXXX", fixture->directory);
    fd = mkstemp(state->staged); RES_CHECK(fd >= 0); stream = fdopen(fd, "w"); RES_CHECK(stream);
    RES_CHECK(fprintf(stream, "%d %d %d\n", state->next[0], state->next[1], state->next[2]) > 0 && !fclose(stream));
    if (fixture->mode == 3) fixture->host.now += 100;
    return TIKU_RESOURCE_OK;
}

/** @brief Replace persistence before committing the complete in-memory group. */
static tiku_resource_error_t commit(void *opaque, void *token)
{
    atomic_fixture_t *fixture = opaque; prepared_t *state = token;
    fixture->commits++;
    RES_CHECK(tiku_resource_unregister(fixture->host.registry, &fixture->stamp) == TIKU_RESOURCE_BUSY);
    if (fixture->mode == 4) return TIKU_RESOURCE_PERSISTENCE_FAILED;
    RES_CHECK(!rename(state->staged, fixture->file)); state->staged[0] = 0;
    if (fixture->mode == 5) { fixture->model[0] = state->next[0]; return TIKU_RESOURCE_UNKNOWN_OUTCOME; }
    memcpy(fixture->model, state->next, sizeof fixture->model);
    if (fixture->mode == 6) fixture->host.now += 100;
    return TIKU_RESOURCE_OK;
}

/** @brief Remove an uncommitted staging file and release the preparation token. */
static void discard(void *opaque, void *token)
{
    atomic_fixture_t *fixture = opaque; prepared_t *state = token;
    fixture->discards++;
    if (state) { if (state->staged[0]) RES_CHECK(!unlink(state->staged)); free(state); }
}

/** @brief Apply a single setting with the same owner persistence ordering. */
static tiku_resource_error_t single(void *opaque, const char *path, const tiku_msg_t *value,
    int invoke, tiku_resource_snapshot_t *out)
{
    tiku_resource_replacement_t item = {path, value, "user"};
    void *token = NULL;
    tiku_resource_error_t error;
    (void)invoke;
    error = prepare(opaque, &item, 1, out, &token);
    if (!error) error = commit(opaque, token);
    discard(opaque, token); return error;
}

/** @brief Register an atomic owner with two public settings and private content. */
static void open_atomic(atomic_fixture_t *fixture)
{
    tiku_resource_initial_t initial[5] = {0};
    tiku_resource_atomic_controller_t controller = {.single={.apply=single}, .prepare=prepare, .commit=commit, .discard=discard};
    FILE *stream; unsigned i;
    memset(fixture, 0, sizeof *fixture); resource_test_open(&fixture->host, 0);
    snprintf(fixture->directory, sizeof fixture->directory, "%s/stomme-atomic-XXXXXX",
        getenv("STOMME_RESOURCE_TEST_TMP") ? getenv("STOMME_RESOURCE_TEST_TMP") : "/tmp");
    RES_CHECK(mkdtemp(fixture->directory));
    snprintf(fixture->file, sizeof fixture->file, "%s/settings", fixture->directory);
    stream = fopen(fixture->file, "w"); RES_CHECK(stream && fputs("0 0 0\n", stream) >= 0 && !fclose(stream));
    for (i = 0; i < 5; i++) {
        initial[i].spec = (tiku_resource_spec_t){.path=paths[i], .kind=i==3 ? TIKU_RESOURCE_STATE : i==4 ? TIKU_RESOURCE_ACTION : TIKU_RESOURCE_SETTING, .schema_version=1,
            .schema={.type=TIKU_RESOURCE_BOOLEAN}, .read_scopes=TIKU_RESOURCE_READ, .change_scopes=TIKU_RESOURCE_WRITE,
            .public_read=i!=2, .sensitive=i==2, .persistence="user", .controller="atomic-owner"};
        resource_test_snapshot(&initial[i].initial, 0);
    }
    RES_CHECK(!tiku_resource_register_atomic(fixture->host.registry, "atomic", "/atomic", NULL, initial, 5, &controller, fixture, &fixture->stamp));
    for (i = 0; i < 5; i++) tiku_resource_snapshot_clear(&initial[i].initial);
    tiku_resource_service_free(fixture->host.a); tiku_resource_service_free(fixture->host.b);
    fixture->host.a = tiku_resource_service_new(fixture->host.registry, &fixture->host.caller, 63);
    fixture->host.b = tiku_resource_service_new(fixture->host.registry, &fixture->host.caller, 63);
    RES_CHECK(fixture->host.a && fixture->host.b);
}

/** @brief Release the registry and assert that no staging file was retained. */
static void close_atomic(atomic_fixture_t *fixture)
{
    resource_test_close(&fixture->host); RES_CHECK(!unlink(fixture->file) && !rmdir(fixture->directory));
}

/** @brief Check persisted and in-memory controller values together. */
static void model(atomic_fixture_t *fixture, int a, int b, int c)
{
    int values[3]; FILE *stream = fopen(fixture->file, "r");
    RES_CHECK(stream && fscanf(stream, "%d %d %d", &values[0], &values[1], &values[2]) == 3 && !fclose(stream));
    RES_CHECK(values[0] == a && values[1] == b && values[2] == c);
    RES_CHECK(fixture->model[0] == a && fixture->model[1] == b && fixture->model[2] == c);
}

/** @brief Replace two settings with an independently owned complete value. */
static tiku_resource_error_t change(atomic_fixture_t *fixture, unsigned serial, int enabled, tiku_msg_t **out)
{
    char id[33]; tiku_msg_t *value = tiku_resource_value_integer(TIKU_RESOURCE_BOOLEAN, enabled);
    tiku_resource_replacement_t items[] = {{paths[0],value,"user"}, {paths[1],value,"user"}};
    tiku_resource_error_t error;
    snprintf(id, sizeof id, "%032x", serial);
    error = tiku_resource_transaction(fixture->host.registry, &fixture->host.caller, &fixture->stamp, id, items, 2, 50, out);
    tiku_msg_free(value); return error;
}

/** @brief Verify one commit, one shared revision and filtered grouped observations. */
static void publication_test(void)
{
    atomic_fixture_t fixture;
    tiku_resource_watch_t whole, narrow;
    tiku_resource_event_t event;
    tiku_resource_snapshot_t snapshot;
    tiku_msg_t *initial, *result, *description;
    uint64_t before; unsigned i;
    open_atomic(&fixture); before = fixture.stamp.revision;
    RES_CHECK(!tiku_resource_watch_open(fixture.host.registry, &fixture.host.caller, "atomic", "/atomic", &whole, &initial)); tiku_msg_free(initial);
    RES_CHECK(!tiku_resource_watch_open(fixture.host.registry, &fixture.host.caller, "atomic", paths[1], &narrow, &initial)); tiku_msg_free(initial);
    RES_CHECK(!change(&fixture, 1, 1, &result)); fixture.stamp = resource_test_stamp(result); tiku_msg_free(result);
    RES_CHECK(fixture.prepares == 1 && fixture.commits == 1 && fixture.discards == 1 && fixture.stamp.revision == before + 1); model(&fixture, 1, 1, 0);
    for (i = 0; i < 2; i++) {
        RES_CHECK(!tiku_resource_read(fixture.host.registry, &fixture.host.caller, paths[i], &snapshot) && snapshot.stamp.revision == fixture.stamp.revision);
        { int enabled; RES_CHECK(tiku_msg_find_bool(snapshot.applied, "value", 0, &enabled) && enabled); }
        tiku_resource_snapshot_clear(&snapshot);
    }
    RES_CHECK(tiku_resource_watch_next(fixture.host.registry, whole, &event) == 1 && event.affected_count == 2 && event.sequence == 1 && event.stamp.revision == fixture.stamp.revision);
    RES_CHECK(!strcmp(event.affected, paths[0]) && !strcmp(event.affected + strlen(paths[0]) + 1, paths[1]));
    RES_CHECK(tiku_resource_watch_next(fixture.host.registry, whole, &event) == 0);
    RES_CHECK(tiku_resource_watch_next(fixture.host.registry, narrow, &event) == 1 && event.affected_count == 1 && !strcmp(event.affected, paths[1]));
    RES_CHECK(!tiku_resource_describe(fixture.host.registry, &fixture.host.caller, paths[0], &description));
    { int atomic; int64_t features; RES_CHECK(tiku_msg_find_bool(description, "atomic", 0, &atomic) && atomic && tiku_msg_find_int64(description, "change_features", 0, &features) && (features & 32)); }
    tiku_msg_free(description); tiku_resource_watch_close(fixture.host.registry, whole); tiku_resource_watch_close(fixture.host.registry, narrow); close_atomic(&fixture);
}

/** @brief Reject group/model errors before commit and preserve persisted values. */
static void validation_test(void)
{
    atomic_fixture_t fixture;
    tiku_msg_t *result, *value;
    tiku_resource_replacement_t items[2];
    open_atomic(&fixture); fixture.mode = 1;
    RES_CHECK(change(&fixture, 1, 1, &result) == TIKU_RESOURCE_VALUE && !result && !fixture.commits && fixture.discards == 1); model(&fixture, 0, 0, 0);
    fixture.mode = 2; RES_CHECK(change(&fixture, 2, 1, &result) == TIKU_RESOURCE_VALUE && !result && !fixture.commits && fixture.discards == 2); model(&fixture, 0, 0, 0);
    fixture.mode = 0; value = tiku_resource_value_integer(TIKU_RESOURCE_BOOLEAN, 1);
    items[0] = (tiku_resource_replacement_t){paths[0],value,"user"}; items[1] = items[0];
    RES_CHECK(tiku_resource_transaction(fixture.host.registry, &fixture.host.caller, &fixture.stamp, "00000000000000000000000000000003", items, 2, 50, &result) == TIKU_RESOURCE_INVALID);
    items[1].path = "/settings/enabled";
    RES_CHECK(tiku_resource_transaction(fixture.host.registry, &fixture.host.caller, &fixture.stamp, "00000000000000000000000000000004", items, 2, 50, &result) == TIKU_RESOURCE_INVALID);
    items[1].path = paths[1]; items[1].durability = "session";
    RES_CHECK(tiku_resource_transaction(fixture.host.registry, &fixture.host.caller, &fixture.stamp, "00000000000000000000000000000005", items, 2, 50, &result) == TIKU_RESOURCE_UNSUPPORTED);
    items[1].durability = "user"; items[1].path = paths[3];
    RES_CHECK(tiku_resource_transaction(fixture.host.registry, &fixture.host.caller, &fixture.stamp, "00000000000000000000000000000006", items, 2, 50, &result) == TIKU_RESOURCE_READ_ONLY);
    items[1].path = paths[4];
    RES_CHECK(tiku_resource_transaction(fixture.host.registry, &fixture.host.caller, &fixture.stamp, "00000000000000000000000000000007", items, 2, 50, &result) == TIKU_RESOURCE_READ_ONLY);
    RES_CHECK(tiku_resource_transaction(fixture.host.registry, &fixture.host.caller, &fixture.stamp, "00000000000000000000000000000008", items, 17, 50, &result) == TIKU_RESOURCE_LIMIT);
    RES_CHECK(fixture.prepares == 2 && !fixture.commits); tiku_msg_free(value); close_atomic(&fixture);
}

/** @brief Separate rolled-back persistence failures from uncertain application effects. */
static void rollback_test(void)
{
    atomic_fixture_t fixture;
    tiku_msg_t *result, *initial;
    tiku_resource_watch_t watch;
    tiku_resource_event_t event;
    tiku_resource_snapshot_t snapshot;
    open_atomic(&fixture);
    RES_CHECK(!tiku_resource_watch_open(fixture.host.registry, &fixture.host.caller, "atomic", "/atomic", &watch, &initial)); tiku_msg_free(initial);
    fixture.mode = 4;
    RES_CHECK(change(&fixture, 1, 1, &result) == TIKU_RESOURCE_PERSISTENCE_FAILED && !result && fixture.commits == 1 && fixture.discards == 1);
    model(&fixture, 0, 0, 0); RES_CHECK(tiku_resource_watch_next(fixture.host.registry, watch, &event) == 0);
    RES_CHECK(!tiku_resource_read(fixture.host.registry, &fixture.host.caller, paths[0], &snapshot) && snapshot.stamp.revision == fixture.stamp.revision); tiku_resource_snapshot_clear(&snapshot);
    fixture.mode = 5;
    RES_CHECK(change(&fixture, 2, 1, &result) == TIKU_RESOURCE_UNKNOWN_OUTCOME && !result && fixture.commits == 2 && fixture.discards == 2);
    RES_CHECK(tiku_resource_read(fixture.host.registry, &fixture.host.caller, paths[0], &snapshot) == TIKU_RESOURCE_UNAVAILABLE);
    RES_CHECK(tiku_resource_read(fixture.host.registry, &fixture.host.caller, paths[1], &snapshot) == TIKU_RESOURCE_UNAVAILABLE);
    RES_CHECK(tiku_resource_watch_next(fixture.host.registry, watch, &event) == 1 && event.affected_count == 2 && event.stamp.revision == fixture.stamp.revision + 1);
    tiku_resource_watch_close(fixture.host.registry, watch); close_atomic(&fixture);
}

/** @brief Expire preparation without effects and reconcile a late committed result. */
static void deadlines_test(void)
{
    atomic_fixture_t fixture;
    tiku_msg_t *result, *request, *nested;
    open_atomic(&fixture); fixture.mode = 3;
    RES_CHECK(change(&fixture, 1, 1, &result) == TIKU_RESOURCE_DEADLINE && !result && !fixture.commits && fixture.discards == 1); model(&fixture, 0, 0, 0);
    fixture.mode = 6;
    RES_CHECK(change(&fixture, 2, 1, &result) == TIKU_RESOURCE_UNKNOWN_OUTCOME && result && fixture.commits == 1); model(&fixture, 1, 1, 0); tiku_msg_free(result);
    request = resource_test_request(&fixture.host, "request-status", NULL, 3);
    RES_CHECK(tiku_msg_add_string(request, "prior_request_id", "00000000000000000000000000000002"));
    result = resource_test_call(fixture.host.b, &fixture.host.caller, request, NULL);
    RES_CHECK(!strcmp(tiku_msg_find_string(result, "code", 0), "ok")); nested = tiku_msg_find_message(result, "result", 0);
    RES_CHECK(nested && resource_test_stamp(nested).revision == fixture.stamp.revision + 1);
    tiku_msg_free(nested); tiku_msg_free(result); tiku_msg_free(request); close_atomic(&fixture);
}

/** @brief Match semantic group replay and reject changed inputs or stale revisions. */
static void identity_test(void)
{
    atomic_fixture_t fixture;
    tiku_msg_t *result, *value;
    tiku_resource_replacement_t items[2];
    tiku_resource_stamp_t stale;
    open_atomic(&fixture); stale = fixture.stamp;
    RES_CHECK(!change(&fixture, 1, 1, &result)); tiku_msg_free(result);
    RES_CHECK(!change(&fixture, 1, 1, &result) && fixture.commits == 1); fixture.stamp = resource_test_stamp(result); tiku_msg_free(result);
    RES_CHECK(change(&fixture, 1, 0, &result) == TIKU_RESOURCE_ID_REUSED && fixture.commits == 1);
    fixture.stamp = stale;
    RES_CHECK(change(&fixture, 1, 0, &result) == TIKU_RESOURCE_ID_REUSED && fixture.commits == 1);
    value = tiku_resource_value_integer(TIKU_RESOURCE_BOOLEAN, 1);
    items[0] = (tiku_resource_replacement_t){paths[1],value,"user"}; items[1] = (tiku_resource_replacement_t){paths[0],value,"user"};
    RES_CHECK(!tiku_resource_transaction(fixture.host.registry, &fixture.host.caller, &fixture.stamp, "00000000000000000000000000000001", items, 2, 50, &result) && fixture.commits == 1); tiku_msg_free(result);
    RES_CHECK(tiku_resource_transaction(fixture.host.registry, &fixture.host.caller, &fixture.stamp, "00000000000000000000000000000001", items, 2, 51, &result) == TIKU_RESOURCE_ID_REUSED);
    RES_CHECK(change(&fixture, 2, 0, &result) == TIKU_RESOURCE_STALE && fixture.commits == 1);
    RES_CHECK(!tiku_resource_unregister(fixture.host.registry, &fixture.stamp));
    RES_CHECK(!tiku_resource_transaction(fixture.host.registry, &fixture.host.caller, &fixture.stamp, "00000000000000000000000000000001", items, 2, 50, &result) && fixture.commits == 1); tiku_msg_free(result);
    RES_CHECK(change(&fixture, 3, 0, &result) == TIKU_RESOURCE_NOT_FOUND);
    tiku_msg_free(value); close_atomic(&fixture);
}

/** @brief Require current read/control scopes and retain combined private-result scopes. */
static void scopes_test(void)
{
    atomic_fixture_t fixture;
    tiku_resource_caller_t limited;
    tiku_msg_t *value, *result;
    tiku_resource_replacement_t items[2];
    open_atomic(&fixture); value = tiku_resource_value_integer(TIKU_RESOURCE_BOOLEAN, 1);
    items[0] = (tiku_resource_replacement_t){paths[0],value,"user"}; items[1] = (tiku_resource_replacement_t){paths[2],value,"user"};
    limited = fixture.host.caller; limited.scopes &= ~TIKU_RESOURCE_HOST_CONTROL;
    RES_CHECK(tiku_resource_transaction(fixture.host.registry, &limited, &fixture.stamp, "00000000000000000000000000000001", items, 2, 50, &result) == TIKU_RESOURCE_DENIED && !fixture.prepares);
    limited = fixture.host.caller; limited.scopes &= ~TIKU_RESOURCE_CONTENT;
    RES_CHECK(tiku_resource_transaction(fixture.host.registry, &limited, &fixture.stamp, "00000000000000000000000000000002", items, 2, 50, &result) == TIKU_RESOURCE_NOT_FOUND && !fixture.prepares);
    RES_CHECK(!tiku_resource_transaction(fixture.host.registry, &fixture.host.caller, &fixture.stamp, "00000000000000000000000000000003", items, 2, 50, &result)); tiku_msg_free(result);
    RES_CHECK(tiku_resource_transaction(fixture.host.registry, &limited, &fixture.stamp, "00000000000000000000000000000003", items, 2, 50, &result) == TIKU_RESOURCE_DENIED && !result && fixture.commits == 1);
    tiku_msg_free(value); close_atomic(&fixture);
}

/** @brief Copy planned text snapshots while counting pre-commit result reservations. */
static tiku_resource_error_t large_prepare(void *opaque, const tiku_resource_replacement_t *items,
    size_t count, tiku_resource_snapshot_t *planned, void **token)
{
    atomic_fixture_t *fixture = opaque; size_t i;
    (void)token; fixture->prepares++;
    for (i = 0; i < count; i++) {
        planned[i].desired = tiku_msg_copy(items[i].value); planned[i].persisted = tiku_msg_copy(items[i].value);
        planned[i].applied = tiku_msg_copy(items[i].value); strcpy(planned[i].apply_state, "applied");
    }
    return TIKU_RESOURCE_OK;
}

/** @brief Count a commit which has no external file token. */
static tiku_resource_error_t large_commit(void *opaque, void *token)
{
    atomic_fixture_t *fixture = opaque; (void)token; fixture->commits++; return TIKU_RESOURCE_OK;
}

/** @brief Bound descriptor/result sizes before committing and reject unsupported owners. */
static void limits_test(void)
{
    atomic_fixture_t fixture;
    tiku_resource_initial_t initial[6] = {0};
    tiku_resource_replacement_t items[6];
    tiku_resource_atomic_controller_t controller = {.single={.apply=single}, .prepare=large_prepare, .commit=large_commit, .discard=discard};
    tiku_resource_stamp_t stamp;
    char text[4097], names[6][32];
    tiku_msg_t *empty, *value, *result;
    unsigned i;
    open_atomic(&fixture); memset(text,'x',4096); text[4096]=0;
    empty = tiku_resource_value_string(TIKU_RESOURCE_TEXT, ""); value = tiku_resource_value_string(TIKU_RESOURCE_TEXT, text);
    for (i = 0; i < 6; i++) {
        snprintf(names[i],sizeof names[i],"/large/value%u",i);
        initial[i].spec=(tiku_resource_spec_t){.path=names[i],.kind=TIKU_RESOURCE_SETTING,.schema_version=1,.schema={.type=TIKU_RESOURCE_TEXT,.text_max=4096},
            .public_read=1,.persistence="user",.controller="large"};
        initial[i].initial.desired=initial[i].initial.persisted=initial[i].initial.applied=empty;strcpy(initial[i].initial.apply_state,"applied");
        items[i]=(tiku_resource_replacement_t){names[i],value,"user"};
    }
    RES_CHECK(!tiku_resource_register_atomic(fixture.host.registry,"large","/large",NULL,initial,6,&controller,&fixture,&stamp));
    RES_CHECK(tiku_resource_transaction(fixture.host.registry,&fixture.host.caller,&stamp,"00000000000000000000000000000001",items,6,50,&result)==TIKU_RESOURCE_LIMIT && !result && !fixture.commits && fixture.discards==1);
    {
        tiku_resource_protocol_limits_t selected;
        tiku_resource_protocol_limits_default(&selected);selected.frame_bytes=8192;selected.stream_chunk_bytes=4096;
        RES_CHECK(tiku_resource_transaction_limited(fixture.host.registry,&fixture.host.caller,&stamp,"00000000000000000000000000000003",items,1,50,&selected,&result)==TIKU_RESOURCE_LIMIT && !result && !fixture.commits && fixture.discards==2);
    }
    items[0]=(tiku_resource_replacement_t){"/settings/enabled",empty,"user"};
    tiku_msg_free(empty); empty=tiku_resource_value_integer(TIKU_RESOURCE_BOOLEAN,1);items[0].value=empty;
    RES_CHECK(tiku_resource_transaction(fixture.host.registry,&fixture.host.caller,&fixture.host.settings,"00000000000000000000000000000002",items,1,50,&result)==TIKU_RESOURCE_ATOMIC_UNSUPPORTED);
    tiku_msg_free(empty);tiku_msg_free(value);close_atomic(&fixture);
}

/** @brief Construct a wire transaction using the public Support replacement codec. */
static tiku_msg_t *wire_request(resource_fixture_t *host, const tiku_resource_stamp_t *stamp, unsigned serial)
{
    tiku_msg_t *request=resource_test_request(host,"transaction",NULL,serial), *value=tiku_resource_value_integer(TIKU_RESOURCE_BOOLEAN,1), *changes;
    tiku_resource_replacement_t items[]={{paths[0],value,"user"},{paths[1],value,"user"}};
    changes=tiku_resource_replacements_message(items,2);resource_test_expected(request,stamp);
    RES_CHECK(changes && tiku_msg_add_message(request,"changes",changes) && tiku_msg_add_string(request,"provider",stamp->provider) && tiku_msg_add_int32(request,"deadline_ms",50));
    tiku_msg_free(value);tiku_msg_free(changes);return request;
}

/** @brief Gate wire transactions and reject unknown fields/count mismatches. */
static void protocol_test(void)
{
    atomic_fixture_t fixture;
    tiku_resource_service_t *limited;
    tiku_msg_t *request,*result,*changes;
    open_atomic(&fixture);request=wire_request(&fixture.host,&fixture.stamp,1);
    RES_CHECK(tiku_resource_request_check(request,32)==TIKU_RESOURCE_UNSUPPORTED);
    limited=tiku_resource_service_new(fixture.host.registry,&fixture.host.caller,15);RES_CHECK(limited);
    result=resource_test_call(limited,&fixture.host.caller,request,"unsupported");tiku_msg_free(result);tiku_resource_service_free(limited);
    RES_CHECK(tiku_msg_add_int64(request,"caller_scopes",INT64_MAX));
    result=resource_test_call(fixture.host.a,&fixture.host.caller,request,"invalid_request");tiku_msg_free(result);tiku_msg_remove(request,"caller_scopes");
    changes=tiku_msg_find_message(request,"changes",0);tiku_msg_remove(changes,"count");RES_CHECK(tiku_msg_add_int32(changes,"count",3));
    tiku_msg_remove(request,"changes");RES_CHECK(tiku_msg_add_message(request,"changes",changes));
    result=resource_test_call(fixture.host.a,&fixture.host.caller,request,"invalid_request");tiku_msg_free(result);tiku_msg_free(changes);tiku_msg_free(request);
    RES_CHECK(!fixture.prepares && !fixture.commits);close_atomic(&fixture);
}

/** @brief Launch-bound caller authority for the independent consumer process. */
static int authorize(void *opaque,uid_t uid,pid_t pid,tiku_resource_caller_t *out)
{
    atomic_fixture_t *fixture=opaque;(void)pid;RES_CHECK(uid==geteuid());*out=fixture->host.caller;return 1;
}

/** @brief Compose the negotiated channel with the general broker. */
static tiku_msg_t *dispatch(void *opaque,const tiku_resource_caller_t *caller,const tiku_msg_t *request)
{
    atomic_fixture_t *fixture=opaque;return tiku_resource_service_dispatch(fixture->host.a,caller,request);
}

/** @brief Send a client request and check its correlated result. */
static tiku_msg_t *exchange(tiku_resource_channel_t *channel,tiku_msg_t *request)
{
    tiku_msg_t *reply=NULL,*result;
    RES_CHECK(!tiku_resource_channel_send(channel,request,5000) && !tiku_resource_channel_receive(channel,&reply,5000));
    RES_CHECK(!strcmp(tiku_msg_find_string(reply,"request_id",0),tiku_msg_find_string(request,"request_id",0)));
    result=tiku_msg_find_message(reply,"result",0);RES_CHECK(result && !tiku_msg_count(reply,"error"));tiku_msg_free(reply);return result;
}

/** @brief Exercise group requests, grouped watch events and replay through IPC. */
static void local_client_test(void)
{
    atomic_fixture_t fixture;
    tiku_resource_channel_ops_t ops={authorize,dispatch,NULL};
    int sockets[2],status;pid_t child;unsigned i;
    open_atomic(&fixture);RES_CHECK(!socketpair(AF_UNIX,SOCK_STREAM,0,sockets));fflush(NULL);child=fork();RES_CHECK(child>=0);
    if(!child){
        tiku_resource_channel_t *channel;
        tiku_msg_t *request,*result,*event,*affected;
        close(sockets[0]);channel=tiku_resource_channel_client(sockets[1]);close(sockets[1]);RES_CHECK(channel && !tiku_resource_channel_negotiate(channel,38,1,5000));
        request=resource_test_request(&fixture.host,"subscribe","/atomic",10);RES_CHECK(tiku_msg_add_string(request,"provider","atomic"));
        result=exchange(channel,request);tiku_msg_free(result);tiku_msg_free(request);
        request=wire_request(&fixture.host,&fixture.stamp,11);result=exchange(channel,request);RES_CHECK(resource_test_stamp(result).revision==fixture.stamp.revision+1);tiku_msg_free(result);
        RES_CHECK(!tiku_resource_channel_receive(channel,&event,5000));affected=tiku_msg_find_message(event,"paths",0);
        RES_CHECK(affected && tiku_msg_count(affected,"item")==2);tiku_msg_free(affected);tiku_msg_free(event);
        result=exchange(channel,request);tiku_msg_free(result);tiku_msg_free(request);
        request=resource_test_request(&fixture.host,"request-status",NULL,12);RES_CHECK(tiku_msg_add_string(request,"prior_request_id","0000000000000000000000000000000b"));
        result=exchange(channel,request);RES_CHECK(!strcmp(tiku_msg_find_string(result,"code",0),"ok"));tiku_msg_free(result);tiku_msg_free(request);
        tiku_resource_channel_free(channel);exit(0);
    }
    close(sockets[1]);fixture.host.channel=tiku_resource_channel_server(sockets[0],tiku_resource_registry_session(fixture.host.registry),63,&ops,&fixture);close(sockets[0]);RES_CHECK(fixture.host.channel);
    for(i=0;i<3;i++)RES_CHECK(!tiku_resource_channel_step(fixture.host.channel,5000));
    {
        tiku_msg_t *event;RES_CHECK(tiku_resource_service_event(fixture.host.a,&fixture.host.caller,&event)==1 && !tiku_resource_channel_send(fixture.host.channel,event,5000));tiku_msg_free(event);
    }
    for(i=0;i<2;i++)RES_CHECK(!tiku_resource_channel_step(fixture.host.channel,5000));
    RES_CHECK(waitpid(child,&status,0)==child && WIFEXITED(status) && !WEXITSTATUS(status));
    RES_CHECK(fixture.prepares==1 && fixture.commits==1 && fixture.discards==1);model(&fixture,1,1,0);
    tiku_resource_channel_free(fixture.host.channel);close_atomic(&fixture);
}

int main(int argc,char **argv)
{
    const struct {const char *name;void (*run)(void);} tests[]={
        {"publication",publication_test},{"validation",validation_test},{"rollback",rollback_test},
        {"deadlines",deadlines_test},{"identity",identity_test},{"scopes",scopes_test},{"limits",limits_test},
        {"protocol",protocol_test},{"local-client",local_client_test}
    };
    size_t i;
    for(i=0;i<sizeof tests/sizeof tests[0];i++)if(argc<2 || !strcmp(argv[1],tests[i].name)){
        tests[i].run();printf("PASS resource transaction %s\n",tests[i].name);
    }
    return 0;
}
