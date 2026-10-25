/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_application_provider.c - native assignment, owner callbacks and teardown.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_trk_desktop_private.h"
#include "tiku_app_resources.h"
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
static tiku_app_resources_t *native_owner;
static tiku_msg_t *to_host[128], *to_owner[128];
static size_t host_count, owner_count;
static int captures, changes, picks, retired, exit_pick, fail_capture,
    held_exit;
static int64_t counter = 3;
static int64_t now = 1000000;
/** @brief Return the isolated registry without opening a control socket. */
tiku_resource_registry_t *
tiku_trk_resource_session_registry(tiku_trk_resource_session_t *session)
{
    (void)session;
    return registry;
}
/** @brief Copy host output at the native connection boundary. */
int tiku_remote_session_tell(tiku_remote_session_t *session,
                             const tiku_msg_t *message)
{
    CHECK(session && session->used && session->fd >= 0 && owner_count < 128);
    to_owner[owner_count++] = tiku_msg_copy(message);
    return to_owner[owner_count - 1] != NULL;
}
/** @brief Copy application requests without serializing function pointers or model data. */
static int send_message(void *context, const tiku_msg_t *message)
{
    CHECK(context && host_count < 128);
    to_host[host_count++] = tiku_msg_copy(message);
    return to_host[host_count - 1] != NULL;
}
/** @brief Record an actual current native menu dispatch. */
static int picked(void *state, uint32_t window, int command)
{
    CHECK(state && window == 1 && command == 42);
    picks++;
    if (exit_pick == 2) {
        tiku_app_resources_window(native_owner, 1, NULL, 0, 0, 1);
        return 0;
    }
    return exit_pick;
}
/** @brief Create an initial integer preference with its native range. */
static int catalog(void *state, tiku_app_resources_t *owner, size_t index,
                   tiku_resource_initial_t *out)
{
    CHECK(state && owner);
    if (index)
        return 0;
    out->spec =
        (tiku_resource_spec_t){.path = "preferences/counter",
                               .kind = TIKU_RESOURCE_SETTING,
                               .schema_version = 1,
                               .schema = {.type = TIKU_RESOURCE_INTEGER,
                                          .integer_min = 0,
                                          .integer_max = 100},
                               .persistence = "session"};
    return tiku_app_resource_snapshot(
               &out->initial,
               tiku_resource_value_integer(TIKU_RESOURCE_INTEGER, counter),
               "observed")
               ? -1
               : 1;
}
/** @brief Capture the live application preference through its owning model. */
static tiku_resource_error_t capture(void *state, tiku_app_resources_t *owner,
                                     const char *path,
                                     tiku_resource_snapshot_t *out)
{
    CHECK(state && owner);
    if (strcmp(path, "preferences/counter"))
        return TIKU_RESOURCE_UNSUPPORTED;
    captures++;
    if(fail_capture)return TIKU_RESOURCE_UNAVAILABLE;
    return tiku_app_resource_snapshot(
               out,
               tiku_resource_value_integer(TIKU_RESOURCE_INTEGER, counter),
               "observed")
               ? TIKU_RESOURCE_NO_MEMORY
               : 0;
}
/** @brief Reject invalid values before changing the private model. */
static tiku_resource_error_t change(void *state, tiku_app_resources_t *owner,
                                    const char *path, const tiku_msg_t *value,
                                    int invoke, tiku_resource_snapshot_t *out,
                                    tiku_resource_effect_t *effect)
{
    CHECK(state && owner);
    if (strcmp(path, "preferences/counter"))
        return TIKU_RESOURCE_UNSUPPORTED;
    int64_t number;
    if (invoke || !tiku_msg_find_int64(value, "value", 0, &number) ||
        number < 0 || number > 100)
        return TIKU_RESOURCE_VALUE;
    counter = number;
    changes++;
    *effect = TIKU_RESOURCE_EFFECT_PARTIAL;
    return capture(state, owner, path, out);
}
/** @brief Drive both owner threads' bounded native protocol turns. */
static void pump(desktop_t *desktop, tiku_app_resources_t *owner, int turns)
{
    for (int tick = 0; tick < turns; tick++) {
        now += 10000;
        tiku_app_resources_poll(owner, now);
        if (exit_pick && picks == 2 && !tiku_app_resources_finished(owner)) {
            CHECK(tiku_app_resources_hold_exit(owner));
            held_exit++;
        }
        for (size_t i = 0; i < host_count; i++) {
            CHECK(desktop_app_vfs_message(desktop, 0, to_host[i]));
            tiku_msg_free(to_host[i]);
        }
        host_count = 0;
        CHECK(tiku_resource_jobs_pump(registry) >= 0);
        desktop_app_vfs_poll(desktop);
        for (size_t i = 0; i < owner_count; i++) {
            CHECK(tiku_app_resources_message(owner, to_owner[i]));
            tiku_msg_free(to_owner[i]);
        }
        owner_count = 0;
    }
}
/** @brief Build an exact native menu command argument object. */
static tiku_msg_t *arguments(void)
{
    tiku_msg_t *out = tiku_app_resource_object(0);
    CHECK(!tiku_app_resource_member(
        out, "window", tiku_resource_value_integer(TIKU_RESOURCE_INTEGER, 1)));
    CHECK(!tiku_app_resource_member(
        out, "command",
        tiku_resource_value_integer(TIKU_RESOURCE_INTEGER, 42)));
    return out;
}
/** @brief Release module-owned resources while the application model is live. */
static void retire(void *state)
{
 CHECK(state);retired++;
}
/** @brief Exercise separate owner and broker turns with bounded native messages. */
int main(void)
{
    CHECK(!setenv("STOMME_RESOURCE_DIRECTORY", "/fixture", 1));
    tiku_resource_limits_t limits = {.providers = 32, .resources = 1024};
    registry = tiku_resource_registry_new(&limits);
    CHECK(registry);
    desktop_t *desktop = calloc(1, sizeof *desktop);
    CHECK(desktop && !desktop_app_vfs_open(desktop));
    desktop->remote.session[0].used = 1;
    desktop->remote.session[0].fd = 123;
    desktop->remote.session[0].generation = 7;
    tiku_app_services_t services = {.ctx = desktop,
                                    .send_message = send_message};
    tiku_app_descriptor_t descriptor = {
        .id = "org.stomme.fixture", .name = "Fixture", .pick = picked};
    const tiku_app_resource_export_t extension = {.version = 1,
                                                  .size = sizeof extension,
                                                  .retire = retire,
                                                  .catalog = catalog,
                                                  .capture = capture,
                                                  .change = change};
    tiku_app_resources_t *owner =
        tiku_app_resources_new(&services, &descriptor, &extension);
    CHECK(owner);
    native_owner=owner;
    tiku_app_resources_model(owner, desktop);
    tiku_app_resources_window(owner, 1, "Fixture", 320, 240, 0);
    tiku_menuset_t menus = {.nmenu = 1};
    strcpy(menus.menu[0].title, "File");
    menus.menu[0].nitem = 1;
    menus.menu[0].item[0].command = 42;
    menus.menu[0].item[0].enabled = 1;
    strcpy(menus.menu[0].item[0].label, "Native command");
    tiku_app_resources_menus(owner, 1, &menus);
    pump(desktop, owner, 200);
    char prefix[128], path[200], request[33];
    CHECK(desktop_app_vfs_prefix(desktop, 7, prefix, sizeof prefix));
    tiku_resource_caller_t consumer = {.authenticated = 1, .scopes = 63};
    strcpy(consumer.principal, "00000000000000000000000000000001");
    tiku_resource_snapshot_t snapshot = {0};
    snprintf(path, sizeof path, "%s/preferences/counter", prefix);
    CHECK(!tiku_resource_read(registry, &consumer, path, &snapshot));
    int64_t observed;
    CHECK(tiku_msg_find_int64(snapshot.applied, "value", 0, &observed) &&
          observed == 3 && captures);
    tiku_resource_snapshot_clear(&snapshot);
    tiku_resource_caller_t denied = consumer;
    denied.scopes &= ~TIKU_RESOURCE_CROSS_APP;
    CHECK(tiku_resource_read(registry, &denied, path, &snapshot) ==
          TIKU_RESOURCE_NOT_FOUND);
    CHECK(!tiku_resource_read(registry, &consumer, path, &snapshot) &&
          !tiku_resource_new_id(request));
    tiku_msg_t *value = tiku_resource_value_integer(TIKU_RESOURCE_INTEGER, 9);
    tiku_resource_job_info_t job = {0}, status = {0};
    CHECK(!tiku_resource_job_submit(
        registry, &consumer, "00000000000000000000000000000002", path,
        &snapshot.stamp, request, value, 0, 3000, &job));
    pump(desktop, owner, 30);
    CHECK(!tiku_resource_job_status(registry, &consumer, job.id, &status) &&
          status.state == TIKU_RESOURCE_JOB_SUCCEEDED && counter == 9 &&
          changes == 1);
    tiku_resource_job_info_clear(&job);
    tiku_resource_job_info_clear(&status);
    tiku_msg_free(value);
    tiku_resource_snapshot_clear(&snapshot);
    snprintf(path, sizeof path, "%s/commands/invoke", prefix);
    CHECK(!tiku_resource_read(registry, &consumer, path, &snapshot) &&
          !tiku_resource_new_id(request));
    value = arguments();
    CHECK(!tiku_resource_job_submit(
        registry, &consumer, "00000000000000000000000000000002", path,
        &snapshot.stamp, request, value, 1, 3000, &job));
    pump(desktop, owner, 30);
    CHECK(picks == 1);
    tiku_resource_job_info_clear(&job);
    tiku_resource_snapshot_clear(&snapshot);
    menus.menu[0].item[0].enabled = 0;
    tiku_app_resources_menus(owner, 1, &menus);
    CHECK(!tiku_resource_read(registry, &consumer, path, &snapshot) &&
          !tiku_resource_new_id(request));
    CHECK(!tiku_resource_job_submit(
        registry, &consumer, "00000000000000000000000000000002", path,
        &snapshot.stamp, request, value, 1, 3000, &job));
    CHECK(tiku_resource_jobs_pump(registry) >= 0);
    desktop_app_vfs_poll(desktop);
    for (size_t i = 0; i < owner_count; i++) {
        CHECK(tiku_app_resources_message(owner, to_owner[i]));
        tiku_msg_free(to_owner[i]);
    }
    owner_count = 0;
    pump(desktop, owner, 30);
    CHECK(!tiku_resource_job_status(registry, &consumer, job.id, &status) &&
          status.error == TIKU_RESOURCE_UNSUPPORTED && picks == 1);
    tiku_resource_job_info_clear(&job);
    tiku_resource_job_info_clear(&status);
    tiku_resource_snapshot_clear(&snapshot);
    tiku_msg_free(value);
    fail_capture=1;pump(desktop,owner,200);
    char setting_path[200];
    snprintf(setting_path, sizeof setting_path, "%s/preferences/counter",
             prefix);
    CHECK(!tiku_resource_read(registry,&consumer,setting_path,&snapshot));
    CHECK(tiku_msg_find_int64(snapshot.applied, "value", 0, &observed) &&
          observed == 9);
    tiku_resource_snapshot_clear(&snapshot);
    fail_capture=0;pump(desktop,owner,200);
    menus.menu[0].item[0].enabled = 1;
    tiku_app_resources_menus(owner, 1, &menus);
    pump(desktop, owner, 200);
    exit_pick = 2;
    CHECK(!tiku_resource_read(registry, &consumer, path, &snapshot) &&
          !tiku_resource_new_id(request));
    value = arguments();
    CHECK(!tiku_resource_job_submit(
        registry, &consumer, "00000000000000000000000000000002", path,
        &snapshot.stamp, request, value, 1, 3000, &job));
    CHECK(!tiku_app_resources_finished(owner));pump(desktop,owner,30);
    CHECK(!tiku_resource_job_status(registry,&consumer,job.id,&status));
    tiku_resource_job_info_clear(&status);
    CHECK(tiku_app_resources_finished(owner) && picks == 2 && held_exit &&
          !tiku_app_resources_hold_exit(owner));
    CHECK(!tiku_resource_job_status(registry, &consumer, job.id, &status) &&
          status.state == TIKU_RESOURCE_JOB_SUCCEEDED);
    tiku_resource_job_info_clear(&job);
    tiku_resource_job_info_clear(&status);
    tiku_resource_snapshot_clear(&snapshot);
    tiku_msg_free(value);
    desktop->remote.session[0].used = 0;
    desktop_app_vfs_poll(desktop);
    CHECK(tiku_resource_read(registry, &consumer, path, &snapshot) ==
          TIKU_RESOURCE_NOT_FOUND);
    int before = captures;
    tiku_app_resources_free(owner);
    desktop_app_vfs_close(desktop);
    CHECK(captures == before && retired==1);
    for (size_t i = 0; i < host_count; i++)
        tiku_msg_free(to_host[i]);
    for (size_t i = 0; i < owner_count; i++)
        tiku_msg_free(to_owner[i]);
    free(desktop);
    tiku_resource_registry_free(registry);
    puts(
        "PASS native app provider: assigned authority, owner preference, live menu refusal and disconnect retirement");
    return 0;
}
