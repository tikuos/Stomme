/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * test_resource_upload.c - atomic owner streams, expiry and scoped frozen reads.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_app_resources.h"
#include "tiku_resource_service.h"
#include "tiku_resource_stream.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static tiku_resource_registry_t *registry;
static tiku_resource_service_t *service;
static tiku_resource_caller_t caller;
static uint64_t now = 1000;
/** @brief Advance expiry deterministically without wall-clock sleeps. */
static uint64_t clock_now(void *context)
{
    (void)context;
    return now;
}
/** @brief Deliver one SDK request through the actual application broker. */
static tiku_resource_error_t exchange(tiku_resource_provider_client_t *client,
                                      tiku_msg_t *request)
{
    assert(request);
    tiku_msg_t *reply =
        tiku_resource_service_dispatch(service, &caller, request);
    assert(reply);
    tiku_resource_error_t result =
        tiku_resource_provider_client_reply(client, reply);
    tiku_msg_free(reply);
    tiku_msg_free(request);
    return result;
}
/** @brief Open the latest frozen native bytes at a checked current stamp. */
static tiku_resource_stream_t *open_stream(const char *path, size_t *bytes)
{
    tiku_resource_snapshot_t value = {0};
    tiku_resource_stream_t *stream = NULL;
    assert(!tiku_resource_read(registry, &caller, path, &value));
    assert(!tiku_resource_stream_open(registry, &caller, path, &value.stamp,
                                      100000, &stream, bytes));
    tiku_resource_snapshot_clear(&value);
    return stream;
}
/** @brief Compare frozen content without relying on mutable publisher buffers. */
static void holds(tiku_resource_stream_t *stream, const void *expected,
                  size_t bytes)
{
    unsigned char data[32768];
    size_t at = 0;
    while (at < bytes) {
        size_t read = 0;
        assert(!tiku_resource_stream_read(stream, &caller, at, data,
                                          sizeof data, &read));
        assert(read && read <= bytes - at &&
               !memcmp(data, (const unsigned char *)expected + at, read));
        at += read;
    }
}
/** @brief Verify unpublished chunks and expired transfers cannot change public bytes. */
int main(void)
{
    registry = tiku_resource_registry_new(NULL);
    assert(registry &&
           !tiku_resource_registry_clock(registry, clock_now, NULL));
    caller = (tiku_resource_caller_t){
        .authenticated = 1,
        .scopes = TIKU_RESOURCE_READ | TIKU_RESOURCE_WRITE |
                  TIKU_RESOURCE_INVOKE | TIKU_RESOURCE_CONTENT |
                  TIKU_RESOURCE_PROVIDER};
    strcpy(caller.principal, "00000000000000000000000000000001");
    service =
        tiku_resource_service_new(registry, &caller, TIKU_RESOURCE_FEATURES);
    assert(service && !tiku_resource_service_provider_enable(service));
    tiku_resource_provider_client_t *client =
        tiku_resource_provider_client_assigned(
            tiku_resource_registry_session(registry), caller.principal,
            TIKU_RESOURCE_FEATURES);
    assert(client);
    char path[160], state[160], transfer[33];
    snprintf(path, sizeof path, "%s/content",
             tiku_resource_provider_client_prefix(client));
    snprintf(state, sizeof state, "%s/observation",
             tiku_resource_provider_client_prefix(client));
    assert(!tiku_resource_new_id(transfer));
    tiku_resource_initial_t initial[2] = {0};
    assert(tiku_app_resource_initial(
               &initial[0], path, TIKU_RESOURCE_STREAM, TIKU_RESOURCE_OBJECT,
               tiku_resource_stream_metadata(0, "")) == 1);
    assert(tiku_app_resource_initial(
               &initial[1], state, TIKU_RESOURCE_STATE, TIKU_RESOURCE_INTEGER,
               tiku_resource_value_integer(TIKU_RESOURCE_INTEGER, 42)) == 1);
    assert(!exchange(
        client, tiku_resource_provider_client_register(client, initial, 2)));
    for (size_t i = 0; i < 2; i++)
        tiku_resource_snapshot_clear(&initial[i].initial);
    assert(!exchange(client, tiku_resource_provider_client_stream(
                                 client, path, transfer, 0, 3, "old", 3)));
    size_t bytes = 0;
    tiku_resource_stream_t *old = open_stream(path, &bytes);
    assert(bytes == 3);
    holds(old, "old", 3);
    unsigned char *content = malloc(70000);
    assert(content);
    for (size_t i = 0; i < 70000; i++)
        content[i] = (unsigned char)(i % 251);
    assert(!tiku_resource_new_id(transfer));
    assert(!exchange(
        client, tiku_resource_provider_client_stream(client, path, transfer, 0,
                                                     70000, content, 32768)));
    tiku_resource_stream_t *partial = open_stream(path, &bytes);
    assert(bytes == 3);
    holds(partial, "old", 3);
    tiku_resource_stream_close(partial);
    assert(exchange(client, tiku_resource_provider_client_stream(
                                client, path, transfer, 0, 70000, content,
                                32768)) == 0);
    assert(!exchange(client, tiku_resource_provider_client_stream(
                                 client, path, transfer, 32768, 70000,
                                 content + 32768, 32768)));
    assert(!exchange(client, tiku_resource_provider_client_stream(
                                 client, path, transfer, 65536, 70000,
                                 content + 65536, 4464)));
    tiku_resource_stream_t *fresh = open_stream(path, &bytes);
    assert(bytes == 70000);
    holds(fresh, content, 70000);
    holds(old, "old", 3);
    tiku_resource_caller_t denied = caller;
    denied.scopes &= ~TIKU_RESOURCE_CONTENT;
    char data[8];
    size_t read = 1;
    assert(tiku_resource_stream_read(fresh, &denied, 0, data, sizeof data,
                                     &read) == TIKU_RESOURCE_DENIED &&
           !read);
    assert(!exchange(
        client, tiku_resource_provider_client_availability(client, state, 0)));
    tiku_resource_snapshot_t value = {0};
    int64_t number = 0;
    assert(!tiku_resource_read(registry, &caller, state, &value));
    assert(!strcmp(value.apply_state, "last_good_unavailable") &&
           tiku_msg_find_int64(value.applied, "value", 0, &number) &&
           number == 42);
    tiku_resource_snapshot_clear(&value);
    assert(!exchange(
        client, tiku_resource_provider_client_availability(client, state, 1)));
    assert(!tiku_resource_new_id(transfer));
    assert(!exchange(
        client, tiku_resource_provider_client_stream(client, path, transfer, 0,
                                                     70000, content, 32768)));
    now += 31000;
    assert(exchange(client, tiku_resource_provider_client_stream(
                                client, path, transfer, 32768, 70000,
                                content + 32768, 32768)) ==
           TIKU_RESOURCE_OWNER_LOST);
    holds(fresh, content, 70000);
    assert(!tiku_resource_new_id(transfer));
    assert(!exchange(client, tiku_resource_provider_client_stream(
                                 client, path, transfer, 0, 0, NULL, 0)));
    tiku_resource_stream_t *empty = open_stream(path, &bytes);
    assert(!bytes);
    tiku_resource_stream_close(empty);
    holds(fresh, content, 70000);
    assert(
        !exchange(client, tiku_resource_provider_client_unregister(client)));
    read = 1;
    assert(tiku_resource_stream_read(fresh, &caller, 0, data, sizeof data,
                                     &read) == TIKU_RESOURCE_OWNER_LOST &&
           !read);
    tiku_resource_stream_close(fresh);
    tiku_resource_stream_close(old);
    free(content);
    tiku_resource_provider_client_free(client);
    tiku_resource_service_free(service);
    tiku_resource_registry_free(registry);
    puts(
        "PASS owner streams: atomic chunks, frozen generations, empty content, access, availability, expiry and retirement");
    return 0;
}
