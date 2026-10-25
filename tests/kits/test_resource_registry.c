/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_resource_registry.c - typed values, provider lifetimes and caller scopes.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_resource_backend.h"
#include <pthread.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(test) do { if (!(test)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #test); exit(1); } } while (0)

/** @brief One controller and its published provider identity. */
typedef struct {
    tiku_resource_registry_t *registry;
    tiku_resource_stamp_t stamp;
    tiku_resource_caller_t caller;
    unsigned calls, validations;
    int mode, host_owned;
} fixture_t;

/** @brief Construct three independently owned copies for a controller result. */
static void snapshot(tiku_resource_snapshot_t *out, const tiku_msg_t *value)
{
    memset(out, 0, sizeof *out);
    out->desired = tiku_msg_copy(value);
    out->persisted = tiku_msg_copy(value);
    out->applied = tiku_msg_copy(value);
    strcpy(out->apply_state, "applied");
    CHECK(out->desired && out->persisted && out->applied);
}

/** @brief Validate action arguments through the owning controller. */
static tiku_resource_error_t validate(void *owner, const char *path, const tiku_msg_t *value, int invoke)
{
    fixture_t *fixture = owner;
    (void)path;
    fixture->validations++;
    return invoke && !strcmp(tiku_msg_find_string(value, "value", 0), "deny")
        ? TIKU_RESOURCE_VALUE : TIKU_RESOURCE_OK;
}

/** @brief Apply one value, exercising reentrancy and persistence outcomes. */
static tiku_resource_error_t apply(void *owner, const char *path, const tiku_msg_t *value,
                                  int invoke, tiku_resource_snapshot_t *out)
{
    fixture_t *fixture = owner;
    (void)path; (void)invoke;
    fixture->calls++;
    CHECK(tiku_resource_unregister(fixture->registry, &fixture->stamp) == TIKU_RESOURCE_BUSY);
    if (fixture->mode == 2) return TIKU_RESOURCE_OK;
    snapshot(out, value);
    if (fixture->mode == 1) {
        tiku_msg_free(out->persisted);
        out->persisted = tiku_resource_value_string(TIKU_RESOURCE_ENUM, "light");
        strcpy(out->apply_state, "persistence_failed");
        return TIKU_RESOURCE_PERSISTENCE_FAILED;
    }
    return TIKU_RESOURCE_OK;
}

/** @brief Register independently copied values under one app-owned provider. */
static tiku_resource_error_t register_fixture(fixture_t *fixture, const char *key, const char *prefix)
{
    static const char *const choices[] = {"light", "dark", "monochrome"};
    tiku_resource_initial_t items[4] = {0};
    tiku_resource_controller_t controller = {validate, apply};
    const char *paths[] = {"/settings/theme", "/settings/count", "/settings/private/content", "/settings/beep"};
    tiku_msg_t *values[4];
    size_t i;
    tiku_resource_error_t result;
    values[0] = tiku_resource_value_string(TIKU_RESOURCE_ENUM, "light");
    values[1] = tiku_resource_value_integer(TIKU_RESOURCE_INTEGER, 1);
    values[2] = tiku_resource_value_string(TIKU_RESOURCE_TEXT, "secret");
    values[3] = tiku_resource_value_string(TIKU_RESOURCE_TEXT, "ready");
    for (i = 0; i < 4; i++) {
        tiku_resource_spec_t *spec = &items[i].spec;
        spec->path = paths[i];
        spec->schema_version = 1;
        spec->kind = i == 1 ? TIKU_RESOURCE_STATE : (i == 3 ? TIKU_RESOURCE_ACTION : TIKU_RESOURCE_SETTING);
        spec->schema.type = i == 0 ? TIKU_RESOURCE_ENUM : (i == 1 ? TIKU_RESOURCE_INTEGER : TIKU_RESOURCE_TEXT);
        spec->schema.text_max = 128;
        spec->schema.integer_max = 100;
        spec->read_scopes = TIKU_RESOURCE_READ;
        spec->change_scopes = i == 3 ? TIKU_RESOURCE_INVOKE : TIKU_RESOURCE_WRITE;
        spec->public_read = i != 2;
        spec->sensitive = i == 2;
        spec->persistence = "user";
        spec->controller = "fixture";
        items[i].initial.desired = items[i].initial.persisted = items[i].initial.applied = values[i];
        strcpy(items[i].initial.apply_state, "applied");
    }
    items[0].spec.schema.choices = choices;
    items[0].spec.schema.choice_count = 3;
    result = tiku_resource_register(fixture->registry, key, prefix, fixture->host_owned ? NULL : fixture->caller.principal,
                                    items, 4, &controller, fixture, &fixture->stamp);
    for (i = 0; i < 4; i++) tiku_msg_free(values[i]);
    return result;
}

/** @brief Create a fixture with a bounded event queue. */
static void open_fixture(fixture_t *fixture)
{
    tiku_resource_limits_t limits = {.events_per_watch = 2};
    memset(fixture, 0, sizeof *fixture);
    fixture->registry = tiku_resource_registry_new(&limits);
    CHECK(fixture->registry);
    memset(fixture->caller.principal, '1', 32);
    fixture->caller.principal[32] = 0;
    fixture->caller.authenticated = 1;
    fixture->caller.scopes = TIKU_RESOURCE_READ | TIKU_RESOURCE_WRITE | TIKU_RESOURCE_INVOKE | TIKU_RESOURCE_CONTENT;
    CHECK(register_fixture(fixture, "settings", "/settings") == TIKU_RESOURCE_OK);
}

/** @brief Dispatch a fresh conditional setting request. */
static tiku_resource_error_t change(fixture_t *fixture, const tiku_resource_stamp_t *expected,
    unsigned serial, const char *text, tiku_resource_snapshot_t *out)
{
    char id[33];
    tiku_resource_error_t error;
    tiku_msg_t *value = tiku_resource_value_string(TIKU_RESOURCE_ENUM, text);
    snprintf(id, sizeof id, "%032x", serial);
    CHECK(value);
    error = tiku_resource_change(fixture->registry, &fixture->caller, "/settings/theme", expected, id, value, 0, out);
    tiku_msg_free(value);
    return error;
}

/** @brief Check invalid wire/text inputs and complete representation bounds. */
static void values_test(void)
{
    tiku_msg_t *value, *decoded = NULL, *raw;
    unsigned char bytes[1024];
    size_t length, i;
    char text[32];
    uint64_t counter = 7;
    CHECK(tiku_resource_path_valid("/apps/catalog/editor"));
    CHECK(!tiku_resource_path_valid("/settings/../theme"));
    CHECK(!tiku_resource_path_valid("/settings//theme"));
    CHECK(!tiku_resource_path_valid("/settings/theme/"));
    CHECK(!tiku_resource_path_valid("/settings/%2f"));
    CHECK(!tiku_resource_counter_parse("18446744073709551615", &counter) && counter == UINT64_MAX);
    CHECK(tiku_resource_counter_parse("18446744073709551616", &counter));
    CHECK(tiku_resource_counter_parse("01", &counter));
    CHECK(!tiku_resource_value_string(TIKU_RESOURCE_TEXT, "\xc0\xaf"));
    CHECK(!tiku_resource_value_string(TIKU_RESOURCE_TEXT, "\xed\xa0\x80"));
    value = tiku_resource_value_integer(TIKU_RESOURCE_BOOLEAN, 1);
    CHECK(value && !tiku_resource_value_check(value));
    CHECK(tiku_resource_value_render(value, text, sizeof text) == 5 && !memcmp(text, "true\n", 5));
    memset(text, 'x', sizeof text);
    CHECK(tiku_resource_value_render(value, text, 4) == -1 && text[0] == 'x');
    CHECK(tiku_msg_flatten(value, bytes, sizeof bytes, &length));
    for (i = 0; i < length; i++) {
        CHECK(tiku_resource_message_decode(bytes, i, &decoded) != TIKU_RESOURCE_OK && !decoded);
    }
    CHECK(!tiku_resource_message_decode(bytes, length, &decoded));
    tiku_msg_free(decoded); decoded = NULL;
    CHECK(tiku_resource_message_check(bytes, TIKU_RESOURCE_FRAME_MAX + 1) == TIKU_RESOURCE_LIMIT);
    bytes[length - 1] = 2;
    CHECK(tiku_resource_message_decode(bytes, length, &decoded) == TIKU_RESOURCE_INVALID && !decoded);
    tiku_msg_free(value);
    raw = tiku_msg_new(0);
    CHECK(tiku_msg_add_string(raw, "name", "one") && tiku_msg_add_string(raw, "same", "two"));
    CHECK(tiku_msg_flatten(raw, bytes, sizeof bytes, &length));
    for (i = 0; i + 4 <= length; i++) if (!memcmp(bytes + i, "same", 4)) { memcpy(bytes + i, "name", 4); break; }
    CHECK(tiku_resource_message_decode(bytes, length, &decoded) == TIKU_RESOURCE_INVALID && !decoded);
    tiku_msg_free(raw);
}

/** @brief Check composite schemas, numeric values and encoding limits. */
static void composites_test(void)
{
    tiku_msg_t *value = tiku_msg_new(0), *object, *member, *child, *array;
    char text[128], large[TIKU_RESOURCE_TEXT_MAX + 2];
    unsigned i;
    CHECK(value && tiku_msg_add_string(value, "type", "number") && tiku_msg_add_double(value, "value", NAN));
    CHECK(tiku_resource_value_check(value) == TIKU_RESOURCE_VALUE);
    tiku_msg_remove(value, "value");
    CHECK(tiku_msg_add_double(value, "value", -1.5) && !tiku_resource_value_check(value));
    CHECK(tiku_resource_value_render(value, text, sizeof text) == 5 && !memcmp(text, "-1.5\n", 5));
    tiku_msg_free(value);
    value = tiku_resource_value_integer(TIKU_RESOURCE_COLOR, UINT32_MAX);
    CHECK(value && !tiku_resource_value_check(value)); tiku_msg_free(value);
    CHECK(!tiku_resource_value_integer(TIKU_RESOURCE_COLOR, -1));
    CHECK(!tiku_resource_value_integer(TIKU_RESOURCE_BOOLEAN, 2));
    memset(large, 'a', sizeof large); large[TIKU_RESOURCE_TEXT_MAX] = 0;
    value = tiku_resource_value_string(TIKU_RESOURCE_TEXT, large);
    CHECK(value); tiku_msg_free(value);
    large[TIKU_RESOURCE_TEXT_MAX] = 'a'; large[TIKU_RESOURCE_TEXT_MAX + 1] = 0;
    CHECK(!tiku_resource_value_string(TIKU_RESOURCE_TEXT, large));
    object = tiku_msg_new(0); member = tiku_msg_new(0);
    child = tiku_resource_value_string(TIKU_RESOURCE_TEXT, "a\n\"");
    CHECK(object && member && child && tiku_msg_add_string(object, "type", "object") &&
        tiku_msg_add_string(member, "key", "x") && tiku_msg_add_message(member, "value", child) &&
        tiku_msg_add_message(object, "member", member) && !tiku_resource_value_check(object));
    CHECK(tiku_resource_value_render(object, text, sizeof text) > 0);
    CHECK(!memcmp(text, "{\"x\":\"a\\u000a\\\"\"}\n", strlen("{\"x\":\"a\\u000a\\\"\"}\n")));
    CHECK(tiku_msg_add_message(object, "member", member));
    CHECK(tiku_resource_value_check(object) == TIKU_RESOURCE_VALUE);
    tiku_msg_free(object); tiku_msg_free(member); tiku_msg_free(child);
    array = tiku_msg_new(0); child = tiku_resource_value_integer(TIKU_RESOURCE_BOOLEAN, 1);
    CHECK(array && child && tiku_msg_add_string(array, "type", "array"));
    for (i = 0; i < TIKU_RESOURCE_MEMBERS_MAX; i++) CHECK(tiku_msg_add_message(array, "item", child));
    CHECK(!tiku_resource_value_check(array));
    CHECK(tiku_msg_add_message(array, "item", child));
    CHECK(tiku_resource_value_check(array) == TIKU_RESOURCE_LIMIT);
    tiku_msg_free(array);
    for (i = 0; i < TIKU_RESOURCE_DEPTH_MAX; i++) {
        array = tiku_msg_new(0);
        CHECK(array && tiku_msg_add_string(array, "type", "array") && tiku_msg_add_message(array, "item", child));
        tiku_msg_free(child); child = array;
    }
    CHECK(tiku_resource_value_check(child) == TIKU_RESOURCE_LIMIT);
    tiku_msg_free(child);
}

/** @brief Check revision ordering, result replay and controller validation. */
static void conflicts_test(void)
{
    fixture_t fixture;
    tiku_resource_snapshot_t out;
    tiku_resource_stamp_t original;
    open_fixture(&fixture); original = fixture.stamp;
    CHECK(!change(&fixture, &original, 1, "dark", &out));
    CHECK(out.stamp.revision == 2 && fixture.calls == 1);
    fixture.stamp = out.stamp; tiku_resource_snapshot_clear(&out);
    CHECK(!change(&fixture, &original, 1, "dark", &out) && fixture.calls == 1);
    tiku_resource_snapshot_clear(&out);
    CHECK(change(&fixture, &original, 1, "monochrome", &out) == TIKU_RESOURCE_ID_REUSED);
    CHECK(change(&fixture, &original, 2, "monochrome", &out) == TIKU_RESOURCE_STALE);
    CHECK(change(&fixture, &fixture.stamp, 3, "blue", &out) == TIKU_RESOURCE_VALUE);
    CHECK(fixture.calls == 1);
    CHECK(!tiku_resource_read(fixture.registry, &fixture.caller, "/settings/theme", &out));
    CHECK(!strcmp(tiku_msg_find_string(out.desired, "value", 0), "dark"));
    tiku_msg_remove(out.desired, "value");
    tiku_resource_snapshot_clear(&out);
    CHECK(!tiku_resource_read(fixture.registry, &fixture.caller, "/settings/theme", &out));
    CHECK(!strcmp(tiku_msg_find_string(out.desired, "value", 0), "dark"));
    tiku_resource_snapshot_clear(&out);
    tiku_resource_registry_free(fixture.registry);
}

/** @brief Check scope filtering and observed-state immutability. */
static void permissions_test(void)
{
    fixture_t fixture;
    tiku_resource_snapshot_t out;
    tiku_resource_entry_t entry, page[16];
    uint64_t epoch = 0;
    size_t count;
    tiku_msg_t *integer = tiku_resource_value_integer(TIKU_RESOURCE_INTEGER, 2);
    open_fixture(&fixture);
    CHECK(tiku_resource_read(fixture.registry, NULL, "/settings/private/content", &out) == TIKU_RESOURCE_NOT_FOUND);
    CHECK(tiku_resource_entry(fixture.registry, NULL, "/settings/private", &entry) == TIKU_RESOURCE_NOT_FOUND);
    CHECK(!tiku_resource_list(fixture.registry, NULL, "/settings", 0, &epoch, page, 16, &count) && count == 3);
    fixture.caller.scopes &= ~TIKU_RESOURCE_CONTENT;
    CHECK(tiku_resource_read(fixture.registry, &fixture.caller, "/settings/private/content", &out) == TIKU_RESOURCE_NOT_FOUND);
    fixture.caller.scopes &= ~TIKU_RESOURCE_WRITE;
    CHECK(change(&fixture, &fixture.stamp, 1, "dark", &out) == TIKU_RESOURCE_DENIED);
    fixture.caller.scopes |= TIKU_RESOURCE_WRITE;
    memset(fixture.caller.principal, '2', 32);
    CHECK(change(&fixture, &fixture.stamp, 2, "dark", &out) == TIKU_RESOURCE_DENIED);
    fixture.caller.scopes |= TIKU_RESOURCE_CROSS_APP;
    CHECK(!change(&fixture, &fixture.stamp, 3, "dark", &out));
    fixture.stamp = out.stamp; tiku_resource_snapshot_clear(&out);
    CHECK(tiku_resource_change(fixture.registry, &fixture.caller, "/settings/count", &fixture.stamp,
        "00000000000000000000000000000004", integer, 0, &out) == TIKU_RESOURCE_READ_ONLY);
    CHECK(fixture.calls == 1);
    CHECK(!tiku_resource_unregister(fixture.registry, &fixture.stamp));
    fixture.host_owned = 1;
    CHECK(!register_fixture(&fixture, "settings", "/settings"));
    CHECK(change(&fixture, &fixture.stamp, 5, "dark", &out) == TIKU_RESOURCE_DENIED);
    fixture.caller.scopes |= TIKU_RESOURCE_HOST_CONTROL;
    CHECK(!change(&fixture, &fixture.stamp, 6, "dark", &out));
    tiku_resource_snapshot_clear(&out);
    tiku_msg_free(integer);
    tiku_resource_registry_free(fixture.registry);
}

/** @brief Check atomic watch creation, bounded overflow and catalog conflicts. */
static void observation_test(void)
{
    fixture_t fixture;
    tiku_resource_snapshot_t out;
    tiku_resource_watch_t watch;
    tiku_resource_event_t event;
    tiku_msg_t *initial;
    tiku_resource_entry_t page[16];
    uint64_t epoch = 0;
    size_t count;
    unsigned i;
    open_fixture(&fixture);
    CHECK(!tiku_resource_watch_open(fixture.registry, &fixture.caller, "settings", "/settings/theme", &watch, &initial));
    CHECK(tiku_msg_count(initial, "entry") == 1);
    tiku_msg_free(initial);
    CHECK(!tiku_resource_list(fixture.registry, &fixture.caller, "/settings", 0, &epoch, page, 16, &count));
    for (i = 0; i < 3; i++) {
        CHECK(!change(&fixture, &fixture.stamp, i + 1, i % 2 ? "light" : "dark", &out));
        fixture.stamp = out.stamp; tiku_resource_snapshot_clear(&out);
    }
    CHECK(tiku_resource_watch_next(fixture.registry, watch, &event) == 1 && event.overflow && event.sequence == 3);
    CHECK(!tiku_resource_watch_next(fixture.registry, watch, &event));
    CHECK(tiku_resource_list(fixture.registry, &fixture.caller, "/settings", 0, &epoch, page, 16, &count) == TIKU_RESOURCE_STALE);
    tiku_resource_watch_close(fixture.registry, watch);
    CHECK(!tiku_resource_watch_open(fixture.registry, &fixture.caller, "settings", "/settings/theme", &watch, &initial));
    tiku_msg_free(initial);
    CHECK(!change(&fixture, &fixture.stamp, 5, "light", &out));
    fixture.stamp = out.stamp; tiku_resource_snapshot_clear(&out);
    CHECK(tiku_resource_watch_next(fixture.registry, watch, &event) == 1 && !event.overflow && event.sequence == 1 && event.stamp.revision == fixture.stamp.revision);
    tiku_resource_registry_free(fixture.registry);
}

/** @brief Verify operations on a different thread are rejected. */
static void *other_thread(void *opaque)
{
    fixture_t *fixture = opaque;
    tiku_resource_snapshot_t out;
    CHECK(tiku_resource_read(fixture->registry, &fixture->caller, "/settings/theme", &out) == TIKU_RESOURCE_INVALID);
    return NULL;
}

/** @brief Check removal, identity nonreuse and provider replacement conflicts. */
static void lifecycle_test(void)
{
    fixture_t fixture;
    tiku_resource_entry_t before, after;
    tiku_resource_stamp_t old;
    tiku_resource_snapshot_t out;
    pthread_t thread;
    open_fixture(&fixture); old = fixture.stamp;
    CHECK(!pthread_create(&thread, NULL, other_thread, &fixture));
    CHECK(!pthread_join(thread, NULL));
    CHECK(!tiku_resource_entry(fixture.registry, &fixture.caller, "/settings/theme", &before));
    CHECK(!tiku_resource_unregister(fixture.registry, &fixture.stamp));
    CHECK(tiku_resource_read(fixture.registry, &fixture.caller, "/settings/theme", &out) == TIKU_RESOURCE_NOT_FOUND);
    CHECK(!register_fixture(&fixture, "settings", "/settings"));
    CHECK(!tiku_resource_entry(fixture.registry, &fixture.caller, "/settings/theme", &after));
    CHECK(before.node_id != after.node_id && strcmp(before.id, after.id));
    CHECK(strcmp(old.incarnation, fixture.stamp.incarnation));
    CHECK(change(&fixture, &old, 1, "dark", &out) == TIKU_RESOURCE_STALE && !fixture.calls);
    CHECK(tiku_resource_unregister(fixture.registry, &old) == TIKU_RESOURCE_OWNER_LOST);
    tiku_resource_registry_free(fixture.registry);
}

/** @brief Check persistence failure and malformed controller completion. */
static void failures_test(void)
{
    fixture_t fixture;
    tiku_resource_snapshot_t out;
    open_fixture(&fixture); fixture.mode = 1;
    CHECK(change(&fixture, &fixture.stamp, 1, "dark", &out) == TIKU_RESOURCE_PERSISTENCE_FAILED);
    CHECK(!strcmp(tiku_msg_find_string(out.persisted, "value", 0), "light"));
    CHECK(!strcmp(tiku_msg_find_string(out.applied, "value", 0), "dark"));
    CHECK(out.stamp.revision == 2);
    fixture.stamp = out.stamp; tiku_resource_snapshot_clear(&out);
    fixture.mode = 2;
    CHECK(change(&fixture, &fixture.stamp, 2, "monochrome", &out) == TIKU_RESOURCE_UNKNOWN_OUTCOME);
    tiku_resource_snapshot_clear(&out);
    CHECK(tiku_resource_read(fixture.registry, &fixture.caller, "/settings/theme", &out) == TIKU_RESOURCE_UNAVAILABLE);
    tiku_resource_registry_free(fixture.registry);
}

/** @brief Check legacy write denial and backend identity lifetime. */
static void backend_test(void)
{
    fixture_t fixture;
    tiku_backend_t *backend;
    tiku_model_t model;
    tiku_resource_snapshot_t out;
    char text[512], error[64];
    uint64_t id;
    int complete;
    open_fixture(&fixture);
    backend = tiku_resource_backend_open(fixture.registry, NULL);
    CHECK(backend && !backend->ops->stat(backend, "/settings/theme", &model));
    CHECK(model.facts.perm == TIKU_P_READ);
    id = model.node_id;
    CHECK(backend->ops->read(backend, "/settings/theme", text, sizeof text) == 6 && !memcmp(text, "light\n", 6));
    CHECK(backend->ops->write(backend, "/settings/theme", "dark", 4, error, sizeof error) == -1);
    CHECK(!strcmp(error, "requires_context") && !fixture.calls);
    CHECK(!backend->ops->changes(backend, NULL, 0, &complete) && complete);
    CHECK(!change(&fixture, &fixture.stamp, 1, "dark", &out));
    fixture.stamp = out.stamp; tiku_resource_snapshot_clear(&out);
    CHECK(!backend->ops->changes(backend, NULL, 0, &complete) && !complete);
    CHECK(!tiku_resource_unregister(fixture.registry, &fixture.stamp));
    CHECK(!register_fixture(&fixture, "settings", "/settings"));
    CHECK(backend->ops->path_of_id(backend, id, text, sizeof text) == -1);
    backend->ops->close(backend);
    tiku_resource_registry_free(fixture.registry);
}

/** @brief Check owner publication and action revisions preserve conflict checks. */
static void publication_test(void)
{
    fixture_t fixture;
    tiku_resource_snapshot_t published, out;
    tiku_resource_stamp_t old;
    tiku_msg_t *value;
    open_fixture(&fixture); old = fixture.stamp;
    value = tiku_resource_value_string(TIKU_RESOURCE_ENUM, "dark");
    snapshot(&published, value); tiku_msg_free(value);
    CHECK(!tiku_resource_publish(fixture.registry, &old, "/settings/theme", &published, &fixture.stamp));
    CHECK(fixture.stamp.revision == 2 && !fixture.calls);
    CHECK(tiku_resource_publish(fixture.registry, &old, "/settings/theme", &published, &old) == TIKU_RESOURCE_STALE);
    CHECK(!tiku_resource_publish(fixture.registry, &fixture.stamp, "/settings/theme", &published, &fixture.stamp));
    CHECK(fixture.stamp.revision == 2);
    tiku_resource_snapshot_clear(&published);
    value = tiku_resource_value_string(TIKU_RESOURCE_TEXT, "ready"); old = fixture.stamp;
    CHECK(!tiku_resource_change(fixture.registry, &fixture.caller, "/settings/beep", &old,
        "00000000000000000000000000000001", value, 1, &out));
    CHECK(out.stamp.revision == 3 && fixture.calls == 1);
    tiku_resource_snapshot_clear(&out);
    CHECK(!tiku_resource_change(fixture.registry, &fixture.caller, "/settings/beep", &old,
        "00000000000000000000000000000001", value, 1, &out) && fixture.calls == 1);
    tiku_resource_snapshot_clear(&out);
    CHECK(tiku_resource_change(fixture.registry, &fixture.caller, "/settings/beep", &old,
        "00000000000000000000000000000002", value, 1, &out) == TIKU_RESOURCE_STALE);
    tiku_msg_free(value);
    tiku_resource_registry_free(fixture.registry);
}

/** @brief Check bounded registration rolls back without exposing partial nodes. */
static void limits_test(void)
{
    fixture_t fixture = {0};
    tiku_resource_limits_t limits = {.resources = 4};
    tiku_resource_entry_t page[4];
    uint64_t epoch = 0;
    size_t count;
    {
        tiku_resource_limits_t host = {.resources = 8192};
        tiku_resource_registry_t *large = tiku_resource_registry_new(&host);
        CHECK(large); tiku_resource_registry_free(large);
        host.resources = 16385; CHECK(!tiku_resource_registry_new(&host));
    }
    fixture.registry = tiku_resource_registry_new(&limits);
    memset(fixture.caller.principal, '1', 32);
    CHECK(fixture.registry && register_fixture(&fixture, "settings", "/settings") == TIKU_RESOURCE_LIMIT);
    CHECK(!tiku_resource_list(fixture.registry, NULL, "/", 0, &epoch, page, 4, &count) && !count);
    tiku_resource_registry_free(fixture.registry);
}

int main(int argc, char **argv)
{
    struct { const char *name; void (*run)(void); } cases[] = {
        {"values", values_test}, {"composites", composites_test}, {"conflicts", conflicts_test}, {"permissions", permissions_test},
        {"observation", observation_test}, {"lifecycle", lifecycle_test}, {"failures", failures_test},
        {"backend", backend_test}, {"limits", limits_test}, {"publication", publication_test}
    };
    size_t i;
    for (i = 0; i < sizeof cases / sizeof cases[0]; i++)
        if (argc == 1 || (argc == 2 && !strcmp(argv[1], cases[i].name))) {
            cases[i].run(); printf("PASS resource %s\n", cases[i].name);
        }
    return 0;
}
