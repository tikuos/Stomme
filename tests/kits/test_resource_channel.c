/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_resource_channel.c - local peer negotiation and independent client calls.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_resource_channel.h"
#include "tiku_resource_registry.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#define CHECK(test) do { if (!(test)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #test); exit(1); } } while (0)

/** @brief A test host which grants one local interactive principal. */
typedef struct {
    tiku_resource_registry_t *registry;
    tiku_resource_stamp_t stamp;
    tiku_resource_watch_t watch;
    char subscription[33];
    unsigned dispatches, changes, closed;
    int deny;
} host_t;

/** @brief Grant explicit host policy after checking OS credentials. */
static int authorize(void *opaque, uid_t uid, pid_t pid, tiku_resource_caller_t *caller)
{
    host_t *host = opaque;
    (void)pid;
    CHECK(uid == geteuid());
    memset(caller->principal, '1', 32); caller->principal[32] = 0;
    caller->scopes = TIKU_RESOURCE_READ | TIKU_RESOURCE_WRITE | TIKU_RESOURCE_HOST_CONTROL;
    return !host->deny;
}

/** @brief Copy an accepted setting into independent controller values. */
static tiku_resource_error_t apply(void *opaque, const char *path, const tiku_msg_t *value,
                                  int invoke, tiku_resource_snapshot_t *out)
{
    host_t *host = opaque;
    (void)path; (void)invoke;
    host->changes++;
    out->desired = tiku_msg_copy(value); out->persisted = tiku_msg_copy(value);
    out->applied = tiku_msg_copy(value); strcpy(out->apply_state, "applied");
    CHECK(out->desired && out->persisted && out->applied);
    return TIKU_RESOURCE_OK;
}

/** @brief Build the fixture-specific protocol reply through registry APIs. */
static tiku_msg_t *dispatch(void *opaque, const tiku_resource_caller_t *caller, const tiku_msg_t *request)
{
    host_t *host = opaque;
    const char *op = tiku_msg_find_string(request, "op", 0);
    const char *path = tiku_msg_find_string(request, "path", 0);
    tiku_msg_t *reply = tiku_msg_new(TIKU_RESOURCE_MESSAGE_CODE), *result = NULL;
    tiku_resource_error_t error = TIKU_RESOURCE_UNSUPPORTED;
    tiku_resource_snapshot_t snapshot = {0};
    char revision[21];
    host->dispatches++;
    CHECK(reply && caller->authenticated && caller->scopes == (TIKU_RESOURCE_READ | TIKU_RESOURCE_WRITE | TIKU_RESOURCE_HOST_CONTROL));
    if (!strcmp(op, "describe")) error = tiku_resource_describe(host->registry, caller, path, &result);
    else if (!strcmp(op, "read")) error = tiku_resource_read(host->registry, caller, path, &snapshot);
    else if (!strcmp(op, "set")) {
        tiku_resource_stamp_t expected = {0};
        tiku_msg_t *tuple = tiku_msg_find_message(request, "expected", 0);
        tiku_msg_t *value = tiku_msg_find_message(request, "value", 0);
        CHECK(!strcmp(tiku_msg_find_string(request, "durability", 0), "user"));
        snprintf(expected.provider, sizeof expected.provider, "%s", tiku_msg_find_string(tuple, "provider", 0));
        snprintf(expected.incarnation, sizeof expected.incarnation, "%s", tiku_msg_find_string(tuple, "incarnation", 0));
        CHECK(!tiku_resource_counter_parse(tiku_msg_find_string(tuple, "revision", 0), &expected.revision));
        error = tiku_resource_change(host->registry, caller, path, &expected,
            tiku_msg_find_string(request, "request_id", 0), value, 0, &snapshot);
        tiku_msg_free(tuple); tiku_msg_free(value);
    } else if (!strcmp(op, "subscribe")) {
        error = tiku_resource_watch_open(host->registry, caller, "settings", path, &host->watch, &result);
        CHECK(!tiku_resource_new_id(host->subscription));
        CHECK(result && tiku_msg_add_string(result, "subscription", host->subscription));
    } else if (!strcmp(op, "unsubscribe")) {
        CHECK(!strcmp(tiku_msg_find_string(request, "subscription", 0), host->subscription));
        tiku_resource_watch_close(host->registry, host->watch); host->watch = 0;
        error = TIKU_RESOURCE_OK;
    }
    if (snapshot.desired && !error) {
        result = tiku_msg_new(0);
        host->stamp = snapshot.stamp;
        snprintf(revision, sizeof revision, "%llu", (unsigned long long)snapshot.stamp.revision);
        CHECK(result && tiku_msg_add_string(result, "provider", snapshot.stamp.provider) &&
            tiku_msg_add_string(result, "incarnation", snapshot.stamp.incarnation) &&
            tiku_msg_add_string(result, "revision", revision) && tiku_msg_add_message(result, "desired", snapshot.desired));
    }
    tiku_resource_snapshot_clear(&snapshot);
    if (!result) result = tiku_msg_new(0);
    CHECK(result);
    if (error) CHECK(tiku_msg_add_string(result, "code", tiku_resource_error_name(error)));
    CHECK(tiku_msg_add_string(reply, "session", tiku_msg_find_string(request, "session", 0)) &&
        tiku_msg_add_string(reply, "request_id", tiku_msg_find_string(request, "request_id", 0)) &&
        tiku_msg_add_message(reply, error ? "error" : "result", result));
    tiku_msg_free(result);
    return reply;
}

/** @brief Release subscriptions when the transport retires a connection. */
static void closed(void *opaque, const tiku_resource_caller_t *caller)
{
    host_t *host = opaque;
    (void)caller;
    host->closed++;
    if (host->watch) { tiku_resource_watch_close(host->registry, host->watch); host->watch = 0; }
}

static const tiku_resource_channel_ops_t ops = {authorize, dispatch, closed};

/** @brief Register one host-owned boolean fixture. */
static void host_open(host_t *host)
{
    tiku_resource_initial_t initial = {0};
    tiku_resource_controller_t controller = {.apply = apply};
    tiku_msg_t *value = tiku_resource_value_integer(TIKU_RESOURCE_BOOLEAN, 0);
    memset(host, 0, sizeof *host);
    host->registry = tiku_resource_registry_new(NULL);
    initial.spec = (tiku_resource_spec_t){.path = "/settings/enabled", .kind = TIKU_RESOURCE_SETTING,
        .schema_version = 1, .schema = {.type = TIKU_RESOURCE_BOOLEAN},
        .read_scopes = TIKU_RESOURCE_READ, .change_scopes = TIKU_RESOURCE_WRITE,
        .public_read = 1, .persistence = "user", .controller = "fixture"};
    initial.initial.desired = initial.initial.persisted = initial.initial.applied = value;
    strcpy(initial.initial.apply_state, "applied");
    CHECK(host->registry && !tiku_resource_register(host->registry, "settings", "/settings", NULL,
        &initial, 1, &controller, host, &host->stamp));
    tiku_msg_free(value);
}

/** @brief Create a correlated client envelope. */
static tiku_msg_t *request(tiku_resource_channel_t *channel, const char *op, unsigned serial)
{
    char id[33];
    tiku_msg_t *out = tiku_msg_new(TIKU_RESOURCE_MESSAGE_CODE);
    snprintf(id, sizeof id, "%032x", serial);
    CHECK(out && tiku_msg_add_string(out, "op", op) && tiku_msg_add_string(out, "request_id", id) &&
        tiku_msg_add_string(out, "session", tiku_resource_channel_session(channel)));
    if (strcmp(op, "unsubscribe")) CHECK(tiku_msg_add_string(out, "path", "/settings/enabled"));
    return out;
}

/** @brief Exchange one request while preserving the caller's ownership. */
static tiku_msg_t *exchange(tiku_resource_channel_t *channel, tiku_msg_t *out)
{
    tiku_msg_t *reply = NULL;
    CHECK(!tiku_resource_channel_send(channel, out, 3000));
    CHECK(!tiku_resource_channel_receive(channel, &reply, 3000));
    CHECK(!strcmp(tiku_msg_find_string(out, "request_id", 0), tiku_msg_find_string(reply, "request_id", 0)));
    return reply;
}

/** @brief Exercise reads, a conditional write and a watch from another process. */
static void independent_client(int fd)
{
    tiku_resource_channel_t *channel = tiku_resource_channel_client(fd);
    tiku_msg_t *out, *reply, *result, *tuple, *value, *event;
    char incarnation[33], subscription[33];
    const char *code;
    close(fd);
    CHECK(channel && !tiku_resource_channel_negotiate(channel, 7, 0, 3000));
    out = request(channel, "describe", 1); reply = exchange(channel, out);
    result = tiku_msg_find_message(reply, "result", 0);
    CHECK(result && !strcmp(tiku_msg_find_string(result, "kind", 0), "setting"));
    strcpy(incarnation, tiku_msg_find_string(result, "incarnation", 0));
    tiku_msg_free(result); tiku_msg_free(reply); tiku_msg_free(out);
    out = request(channel, "read", 2); reply = exchange(channel, out);
    CHECK(tiku_msg_count(reply, "result") == 1);
    tiku_msg_free(reply); tiku_msg_free(out);
    out = request(channel, "subscribe", 3); CHECK(tiku_msg_add_string(out, "provider", "settings"));
    reply = exchange(channel, out); result = tiku_msg_find_message(reply, "result", 0);
    CHECK(result); strcpy(subscription, tiku_msg_find_string(result, "subscription", 0));
    tiku_msg_free(result); tiku_msg_free(reply); tiku_msg_free(out);
    tuple = tiku_msg_new(0); value = tiku_resource_value_integer(TIKU_RESOURCE_BOOLEAN, 1);
    CHECK(tuple && value && tiku_msg_add_string(tuple, "provider", "settings") &&
        tiku_msg_add_string(tuple, "incarnation", incarnation) && tiku_msg_add_string(tuple, "revision", "1"));
    out = request(channel, "set", 4);
    CHECK(tiku_msg_add_message(out, "expected", tuple) && tiku_msg_add_message(out, "value", value) &&
        tiku_msg_add_string(out, "durability", "user") && tiku_msg_add_int32(out, "deadline_ms", 3000));
    reply = exchange(channel, out); CHECK(tiku_msg_count(reply, "result") == 1);
    tiku_msg_free(reply); tiku_msg_free(out);
    CHECK(!tiku_resource_channel_receive(channel, &event, 3000));
    CHECK(!strcmp(tiku_msg_find_string(event, "kind", 0), "changed"));
    CHECK(!strcmp(tiku_msg_find_string(event, "subscription", 0), subscription));
    tiku_msg_free(event);
    out = request(channel, "set", 5);
    CHECK(tiku_msg_add_message(out, "expected", tuple) && tiku_msg_add_message(out, "value", value) &&
        tiku_msg_add_string(out, "durability", "user") && tiku_msg_add_int32(out, "deadline_ms", 3000));
    reply = exchange(channel, out); result = tiku_msg_find_message(reply, "error", 0);
    code = tiku_msg_find_string(result, "code", 0); CHECK(code && !strcmp(code, "stale_revision"));
    tiku_msg_free(result); tiku_msg_free(reply); tiku_msg_free(out); tiku_msg_free(tuple); tiku_msg_free(value);
    out = request(channel, "read", 6); CHECK(tiku_msg_add_int64(out, "caller_scopes", INT64_MAX));
    reply = exchange(channel, out); result = tiku_msg_find_message(reply, "error", 0);
    code = tiku_msg_find_string(result, "code", 0); CHECK(code && !strcmp(code, "invalid_request"));
    tiku_msg_free(result); tiku_msg_free(reply); tiku_msg_free(out);
    out = request(channel, "unsubscribe", 7); CHECK(tiku_msg_add_string(out, "subscription", subscription));
    reply = exchange(channel, out); CHECK(tiku_msg_count(reply, "result") == 1);
    tiku_msg_free(reply); tiku_msg_free(out);
    tiku_resource_channel_free(channel);
}

/** @brief Run the host on its registry thread and a client in a child process. */
static void roundtrip_test(void)
{
    host_t host;
    int pair[2], status, i;
    pid_t child;
    tiku_resource_channel_t *channel;
    host_open(&host);
    CHECK(!socketpair(AF_UNIX, SOCK_STREAM, 0, pair));
    fflush(NULL);
    child = fork(); CHECK(child >= 0);
    if (!child) { close(pair[0]); independent_client(pair[1]); tiku_resource_registry_free(host.registry); exit(0); }
    close(pair[1]);
    channel = tiku_resource_channel_server(pair[0], tiku_resource_registry_session(host.registry), 7, &ops, &host);
    close(pair[0]); CHECK(channel);
    for (i = 0; i < 8; i++) {
        CHECK(!tiku_resource_channel_step(channel, 3000));
        if (i == 4) {
            tiku_resource_event_t event;
            tiku_msg_t *message = tiku_msg_new(TIKU_RESOURCE_MESSAGE_CODE);
            CHECK(tiku_resource_watch_next(host.registry, host.watch, &event) == 1);
            CHECK(message && tiku_msg_add_string(message, "subscription", host.subscription) &&
                tiku_msg_add_string(message, "kind", "changed") &&
                tiku_msg_add_string(message, "session", tiku_resource_registry_session(host.registry)));
            CHECK(!tiku_resource_channel_send(channel, message, 3000));
            tiku_msg_free(message);
        }
    }
    CHECK(waitpid(child, &status, 0) == child && WIFEXITED(status) && !WEXITSTATUS(status));
    CHECK(host.dispatches == 6 && host.changes == 1);
    tiku_resource_channel_free(channel); CHECK(host.closed == 1 && !host.watch);
    tiku_resource_registry_free(host.registry);
}

/** @brief Reject unauthenticated policy and oversized frames without dispatch. */
static void rejection_test(void)
{
    host_t host;
    int pair[2];
    tiku_resource_channel_t *channel;
    const unsigned char oversized[] = {0, 1, 0, 1};
    host_open(&host); host.deny = 1;
    CHECK(!socketpair(AF_UNIX, SOCK_STREAM, 0, pair));
    CHECK(!tiku_resource_channel_server(pair[0], tiku_resource_registry_session(host.registry), 7, &ops, &host));
    close(pair[0]); close(pair[1]); CHECK(!host.dispatches && !host.closed);
    host.deny = 0;
    CHECK(!socketpair(AF_UNIX, SOCK_STREAM, 0, pair));
    channel = tiku_resource_channel_server(pair[0], tiku_resource_registry_session(host.registry), 7, &ops, &host);
    close(pair[0]); CHECK(channel);
    CHECK(write(pair[1], oversized, sizeof oversized) == (ssize_t)sizeof oversized);
    CHECK(tiku_resource_channel_step(channel, 100) == -1);
    CHECK(host.closed == 1 && !host.dispatches);
    tiku_resource_channel_free(channel); CHECK(host.closed == 1);
    close(pair[1]);
    tiku_resource_registry_free(host.registry);
}

/** @brief Reject unnegotiated requests and unknown required features. */
static void negotiation_test(void)
{
    host_t host;
    int pair[2], status;
    pid_t child;
    tiku_resource_channel_t *channel;
    tiku_msg_t *message;
    unsigned char bytes[1024], header[4];
    size_t size;
    host_open(&host);
    CHECK(!socketpair(AF_UNIX, SOCK_STREAM, 0, pair));
    channel = tiku_resource_channel_server(pair[0], tiku_resource_registry_session(host.registry), 7, &ops, &host);
    close(pair[0]); CHECK(channel);
    message = tiku_msg_new(TIKU_RESOURCE_MESSAGE_CODE);
    CHECK(message && tiku_msg_add_string(message, "op", "read") && tiku_msg_flatten(message, bytes, sizeof bytes, &size));
    header[0] = 0; header[1] = 0; header[2] = (unsigned char)(size >> 8); header[3] = (unsigned char)size;
    CHECK(write(pair[1], header, sizeof header) == 4 && write(pair[1], bytes, size) == (ssize_t)size);
    CHECK(tiku_resource_channel_step(channel, 100) == -1 && !host.dispatches && host.closed == 1);
    tiku_msg_free(message); close(pair[1]); tiku_resource_channel_free(channel);
    CHECK(!socketpair(AF_UNIX, SOCK_STREAM, 0, pair));
    fflush(NULL);
    child = fork(); CHECK(child >= 0);
    if (!child) {
        close(pair[0]); channel = tiku_resource_channel_client(pair[1]); close(pair[1]);
        CHECK(channel && tiku_resource_channel_negotiate(channel, 8, 0, 3000) == -1);
        tiku_resource_channel_free(channel); tiku_resource_registry_free(host.registry); exit(0);
    }
    close(pair[1]);
    channel = tiku_resource_channel_server(pair[0], tiku_resource_registry_session(host.registry), 7, &ops, &host);
    close(pair[0]); CHECK(channel && tiku_resource_channel_step(channel, 3000) == -1);
    CHECK(waitpid(child, &status, 0) == child && WIFEXITED(status) && !WEXITSTATUS(status));
    CHECK(host.closed == 2 && !host.dispatches);
    tiku_resource_channel_free(channel); tiku_resource_registry_free(host.registry);
}

int main(int argc, char **argv)
{
    if (argc == 2 && !strcmp(argv[1], "rejections")) { rejection_test(); return 0; }
    roundtrip_test(); puts("PASS resource local client, revisions, watch and authority filtering");
    rejection_test(); puts("PASS resource authentication and oversized frame rejection");
    negotiation_test(); puts("PASS resource negotiation gates before dispatch");
    return 0;
}
