/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * counter.c - native counter window with an owner-thread resource setting.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_app_resources.h"
#include "tiku_client.h"
#include "tiku_gfx.h"
#include "tiku_font.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/** @brief Native state retained until the application descriptor stops. */
typedef struct {
    const tiku_app_services_t *services;
    uint32_t window;
    tiku_surface_t *surface;
    int value;
} counter_t;
/** @brief Render the current native model and submit its complete frame. */
static void paint(counter_t *counter)
{
    if (!counter->surface)
        return;
    char text[32];
    snprintf(text, sizeof text, "Counter: %d", counter->value);
    tiku_fill(counter->surface, (tiku_rect_t){0, 0, 240, 100},
              TIKU_RGB(255, 255, 255));
    tiku_text(counter->surface, tiku_font_plain(), 16, 40, text,
              TIKU_RGB(0, 0, 0));
    counter->services->frame(counter->services->ctx, counter->window,
                             counter->surface->px, 240, 100);
}
/** @brief Apply one validated native counter value from a key or resource callback. */
static int replace(counter_t *counter, int64_t value)
{
    if (value < 0 || value > 1000)
        return 0;
    counter->value = (int)value;
    paint(counter);
    return 1;
}
/** @brief Allocate native state and its window, releasing partial construction on failure. */
static int start(void **out, const tiku_app_services_t *services)
{
    counter_t *counter = calloc(1, sizeof *counter);
    if (!counter)
        return -1;
    counter->services = services;
    counter->surface = tiku_surface_new(240, 100, TIKU_RGB(255,255,255));
    counter->window =
        services->open(services->ctx, "Resource counter", 240, 100);
    if (!counter->surface || !counter->window) {
        tiku_surface_free(counter->surface);
        free(counter);
        return -1;
    }
    *out = counter;
    paint(counter);
    return 0;
}
/** @brief Release native state after the runner retires its resource callbacks. */
static void stop(void *state)
{
    counter_t *counter = state;
    tiku_surface_free(counter->surface);
    free(counter);
}
/** @brief Arrow keys use the same validated native replacement controller. */
static int event(void *state, const tiku_event_t *input)
{
    counter_t *counter = state;
    if (input->type == TIKU_EVENT_CLOSE)
        return 1;
    if (input->type == TIKU_EVENT_KEY_DOWN) {
        if (input->key == TIKU_KEY_UP)
            replace(counter, counter->value + 1);
        if (input->key == TIKU_KEY_DOWN)
            replace(counter, counter->value - 1);
    }
    return 0;
}
/** @brief Capture independent owned holds of a current session setting. */
static tiku_resource_error_t capture(void *state, tiku_app_resources_t *owner,
                                     const char *path,
                                     tiku_resource_snapshot_t *out)
{
    (void)owner;
    if (strcmp(path, "preferences/value"))
        return TIKU_RESOURCE_UNSUPPORTED;
    return tiku_app_resource_snapshot(
               out,
               tiku_resource_value_integer(TIKU_RESOURCE_INTEGER,
                                           ((counter_t *)state)->value),
               "observed")
               ? TIKU_RESOURCE_NO_MEMORY
               : 0;
}
/** @brief Enumerate the native setting beneath the assigned application namespace. */
static int catalog(void *state, tiku_app_resources_t *owner, size_t index,
                   tiku_resource_initial_t *out)
{
    if (index)
        return 0;
    out->spec =
        (tiku_resource_spec_t){.path = "preferences/value",
                               .kind = TIKU_RESOURCE_SETTING,
                               .schema_version = 1,
                               .schema = {.type = TIKU_RESOURCE_INTEGER,
                                          .integer_min = 0,
                                          .integer_max = 1000},
                               .persistence = "session"};
    return capture(state, owner, out->spec.path, &out->initial) ? -1 : 1;
}
/** @brief Recheck native bounds before applying a resource replacement. */
static tiku_resource_error_t change(void *state, tiku_app_resources_t *owner,
                                    const char *path, const tiku_msg_t *value,
                                    int invoke, tiku_resource_snapshot_t *out,
                                    tiku_resource_effect_t *effect)
{
    if (strcmp(path, "preferences/value") || invoke)
        return TIKU_RESOURCE_UNSUPPORTED;
    int64_t n;
    if (!tiku_app_resource_scalar(value, TIKU_RESOURCE_INTEGER, &n) ||
        !replace(state, n))
        return TIKU_RESOURCE_VALUE;
    *effect = TIKU_RESOURCE_EFFECT_PARTIAL;
    return capture(state, owner, path, out);
}
static const tiku_app_descriptor_t descriptor = {.id = "resource-counter",
                                                 .name = "Resource counter",
                                                 .start = start,
                                                 .stop = stop,
                                                 .event = event};
/** @brief Optional owner callback extension loaded without changing the application descriptor ABI. */
__attribute__((visibility("default")))
const tiku_app_resource_export_t tiku_app_resources_v1 = {
    .version = 1,
    .size = sizeof(tiku_app_resource_export_t),
    .catalog = catalog,
    .capture = capture,
    .change = change};
#ifdef TIKU_APP_SO
/** @brief Public native application descriptor export. */
__attribute__((visibility("default"))) const tiku_app_export_t tiku_app_v1 = {
    TIKU_APP_ABI, sizeof(tiku_app_descriptor_t), &descriptor};
#else
/** @brief Run a native application or exercise its callbacks without a display. */
int main(int argc, char **argv)
{
    if (argc == 2 && !strcmp(argv[1], "--smoke")) {
        counter_t counter = {0};
        tiku_resource_snapshot_t out = {0};
        tiku_resource_effect_t effect = 0;
        tiku_msg_t *value =
            tiku_resource_value_integer(TIKU_RESOURCE_INTEGER, 42);
        int error = change(&counter, NULL, "preferences/value", value, 0, &out,
                           &effect);
        tiku_msg_free(value);
        tiku_resource_snapshot_clear(&out);
        value = tiku_resource_value_integer(TIKU_RESOURCE_INTEGER, 1001);
        int refused = change(&counter, NULL, "preferences/value", value, 0,
                             &out, &effect);
        tiku_msg_free(value);
        tiku_resource_snapshot_clear(&out);
        if (error || refused != TIKU_RESOURCE_VALUE || counter.value != 42)
            return 1;
        puts("PASS native application resource callbacks");
        return 0;
    }
    return tiku_client_run(&descriptor);
}
#endif
