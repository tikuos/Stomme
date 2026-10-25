/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_resource_catalog.c - atomic paged catalogs on an assigned owner.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_resource_provider_client.h"
#include "tiku_resource_service.h"
#include "tiku_resource_jobs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(t)                                                              \
    do {                                                                      \
        if (!(t)) {                                                           \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #t);           \
            exit(1);                                                          \
        }                                                                     \
    } while (0)
static tiku_resource_registry_t *registry;
static tiku_resource_service_t *service;
static tiku_resource_caller_t caller;
/** @brief Send one actual SDK management envelope to its native broker. */
static tiku_resource_error_t exchange(tiku_resource_provider_client_t *client,
                                      tiku_msg_t *request)
{
    CHECK(request);
    tiku_msg_t *reply =
        tiku_resource_service_dispatch(service, &caller, request);
    CHECK(reply);
    tiku_resource_error_t error =
        tiku_resource_provider_client_reply(client, reply);
    if (error)
        fprintf(stderr, "%s: %s\n", tiku_msg_find_string(request, "op", 0),
                tiku_resource_error_name(error));
    tiku_msg_free(reply);
    tiku_msg_free(request);
    return error;
}
/** @brief Build a bounded empty object snapshot with independent holds. */
static void initial(tiku_resource_initial_t *out, const char *path)
{
    memset(out, 0, sizeof *out);
    out->spec =
        (tiku_resource_spec_t){.path = path,
                               .kind = TIKU_RESOURCE_ACTION,
                               .schema_version = 1,
                               .schema = {.type = TIKU_RESOURCE_OBJECT},
                               .persistence = "none"};
    out->initial.desired = tiku_msg_new(0);
    CHECK(out->initial.desired &&
          tiku_msg_add_string(out->initial.desired, "type", "object"));
    out->initial.persisted = tiku_msg_copy(out->initial.desired);
    out->initial.applied = tiku_msg_copy(out->initial.desired);
    strcpy(out->initial.apply_state, "observed");
}
int main(void)
{
    tiku_resource_limits_t limits = {.providers = 8, .resources = 4096};
    registry = tiku_resource_registry_new(&limits);
    CHECK(registry);
    caller = (tiku_resource_caller_t){.authenticated = 1, .scopes = 127};
    strcpy(caller.principal, "00000000000000000000000000000001");
    const char *session = tiku_resource_registry_session(registry),
               *catalog = "00000000000000000000000000000002";
    service =
        tiku_resource_service_new(registry, &caller, TIKU_RESOURCE_FEATURES);
    CHECK(service && !tiku_resource_service_provider_enable(service));
    tiku_resource_provider_client_t *client =
        tiku_resource_provider_client_assigned(session, caller.principal,
                                               TIKU_RESOURCE_FEATURES);
    CHECK(client);
    tiku_resource_initial_t entries[32];
    char paths[32][160], first[160];
    const char *prefix = tiku_resource_provider_client_prefix(client);
    snprintf(first, sizeof first, "%s/commands/0", prefix);
    for (int offset = 0; offset < 450; offset += 32) {
        int count = 450 - offset;
        if (count > 32)
            count = 32;
        for (int i = 0; i < count; i++) {
            snprintf(paths[i], sizeof paths[i], "%s/commands/%d", prefix,
                     offset + i);
            initial(&entries[i], paths[i]);
        }
        CHECK(!exchange(client, tiku_resource_provider_client_stage(
                                    client, catalog, (size_t)offset, 450,
                                    entries, (size_t)count)));
        tiku_resource_snapshot_t value = {0};
        CHECK(tiku_resource_read(registry, &caller, first, &value) ==
              TIKU_RESOURCE_NOT_FOUND);
        if (!offset)
            CHECK(exchange(client, tiku_resource_provider_client_commit(
                                       client, catalog)) ==
                  TIKU_RESOURCE_INVALID);
        for (int i = 0; i < count; i++)
            tiku_resource_snapshot_clear(&entries[i].initial);
    }
    CHECK(!exchange(client,
                    tiku_resource_provider_client_commit(client, catalog)));
    tiku_resource_snapshot_t value = {0};
    CHECK(!tiku_resource_read(registry, &caller, first, &value));
    tiku_resource_snapshot_clear(&value);
    snprintf(first, sizeof first, "%s/commands/449", prefix);
    CHECK(!tiku_resource_read(registry, &caller, first, &value));
    tiku_resource_snapshot_clear(&value);
    CHECK(!exchange(client, tiku_resource_provider_client_unregister(client)));
    initial(&entries[0], "/outside");
    CHECK(!tiku_resource_provider_client_stage(client, catalog, 0, 1, entries,
                                               1));
    tiku_resource_snapshot_clear(&entries[0].initial);
    snprintf(paths[0], sizeof paths[0], "%s/state", prefix);
    initial(&entries[0], paths[0]);
    CHECK(!exchange(client, tiku_resource_provider_client_stage(
                                client, catalog, 0, 2, entries, 1)));
    CHECK(exchange(client, tiku_resource_provider_client_stage(
                               client, catalog, 0, 2, entries, 1)) ==
          TIKU_RESOURCE_INVALID);
    CHECK(!exchange(client,
                    tiku_resource_provider_client_abort(client, catalog)));
    CHECK(exchange(client, tiku_resource_provider_client_commit(
                               client, catalog)) == TIKU_RESOURCE_OWNER_LOST);
    CHECK(!exchange(client, tiku_resource_provider_client_stage(
                                client, catalog, 0, 1, entries, 1)));
    tiku_resource_snapshot_clear(&entries[0].initial);
    tiku_resource_provider_client_free(client);
    tiku_resource_service_free(service);
    CHECK(tiku_resource_read(registry, &caller, paths[0], &value) ==
          TIKU_RESOURCE_NOT_FOUND);
    tiku_resource_registry_free(registry);
    puts(
        "PASS provider catalogs: 450 commands, atomic commit, exact page order, isolation and stage cleanup");
    return 0;
}
