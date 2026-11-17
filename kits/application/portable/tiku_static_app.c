/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_static_app.c - allocator-bound state and serialized lifecycle callbacks.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_static_app.h"
#include <stdint.h>

/** @brief Lifecycle states, with STARTING available to deferred request leases. */
enum { APP_NEW, APP_STARTING, APP_RUNNING, APP_STOPPED };
/** @brief Retained state, input and callback serialization. */
struct tiku_static_app {
    tiku_owned_buffer_t self, state;
    const tiku_static_app_ops_t *ops;
    tiku_event_queue_t *events;
    size_t leases;
    int phase, foreground, calling;
};

tiku_static_app_t *tiku_static_app_create(tiku_allocator_t allocator,
    const tiku_static_app_ops_t *ops, size_t events)
{
    tiku_owned_buffer_t owner;
    tiku_static_app_t *app;
    if (!ops || !ops->state_bytes || !ops->start || !ops->dispatch ||
        !ops->stop || !ops->dispose || !events ||
        !tiku_buffer_init(&owner, allocator, _Alignof(tiku_static_app_t)) ||
        !tiku_buffer_resize(&owner, 1, sizeof *app, 1)) return NULL;
    app = owner.data;
    app->self = owner;
    app->ops = ops;
    if (!tiku_buffer_init(&app->state, allocator, _Alignof(max_align_t)) ||
        !tiku_buffer_resize(&app->state, ops->state_bytes, 1, 1) ||
        !(app->events = tiku_event_queue_create(allocator, events))) {
        tiku_buffer_dispose(&app->state);
        tiku_buffer_dispose(&owner);
        return NULL;
    }
    tiku_event_queue_close(app->events);
    tiku_event_queue_take(app->events, &(tiku_input_event_t){0});
    return app;
}

tiku_resource_error_t tiku_static_app_start(tiku_static_app_t *app, void *argument)
{
    tiku_resource_error_t result;
    if (!app || app->calling || app->phase != APP_NEW) return TIKU_RESOURCE_INVALID;
    app->phase = APP_STARTING;
    app->calling = 1;
    result = app->ops->start(app->state.data, app, argument);
    app->calling = 0;
    app->phase = APP_RUNNING;
    if (result != TIKU_RESOURCE_OK) {
        tiku_static_app_stop(app);
        return result;
    }
    tiku_static_app_foreground(app, 1);
    return TIKU_RESOURCE_OK;
}

size_t tiku_static_app_dispatch(tiku_static_app_t *app, size_t budget)
{
    size_t count = 0;
    tiku_input_event_t event;
    tiku_queue_result_t result;
    if (!app || app->calling || app->phase != APP_RUNNING || !app->foreground) return 0;
    app->calling = 1;
    while (count < budget && (result = tiku_event_queue_take(app->events, &event)) != TIKU_QUEUE_EMPTY) {
        app->ops->dispatch(app->state.data, result, result == TIKU_QUEUE_EVENT ? &event : NULL);
        ++count;
    }
    app->calling = 0;
    return count;
}

int tiku_static_app_post(tiku_static_app_t *app, const tiku_input_event_t *event)
{
    return app && !app->calling && app->phase == APP_RUNNING && app->foreground &&
        tiku_event_queue_push(app->events, event);
}

int tiku_static_app_foreground(tiku_static_app_t *app, int foreground)
{
    if (!app || app->calling || app->phase != APP_RUNNING) return 0;
    foreground = !!foreground;
    if (foreground == app->foreground) return 1;
    if (foreground && !tiku_event_queue_open(app->events)) return 0;
    app->foreground = foreground;
    app->calling = 1;
    if (!foreground) {
        tiku_event_queue_close(app->events);
        tiku_event_queue_take(app->events, &(tiku_input_event_t){0});
        app->ops->dispatch(app->state.data, TIKU_QUEUE_CANCEL, NULL);
    }
    if (app->ops->visibility) app->ops->visibility(app->state.data, foreground);
    app->calling = 0;
    return 1;
}

int tiku_static_app_stop(tiku_static_app_t *app)
{
    if (!app || app->calling) return 0;
    if (app->phase == APP_STOPPED) return 1;
    tiku_event_queue_close(app->events);
    app->foreground = 0;
    if (app->phase == APP_NEW) { app->phase = APP_STOPPED; return 1; }
    app->phase = APP_STOPPED;
    app->calling = 1;
    app->ops->stop(app->state.data);
    app->calling = 0;
    return 1;
}

int tiku_static_app_destroy(tiku_static_app_t **pointer)
{
    tiku_static_app_t *app;
    tiku_owned_buffer_t owner;
    if (!pointer || !(app = *pointer)) return 1;
    if (app->calling || app->phase != APP_STOPPED || app->leases) return 0;
    if (!tiku_event_queue_destroy(&app->events)) return 0;
    app->calling = 1;
    app->ops->dispose(app->state.data);
    tiku_buffer_dispose(&app->state);
    owner = app->self;
    tiku_buffer_dispose(&owner);
    *pointer = NULL;
    return 1;
}

int tiku_app_lease_begin(tiku_static_app_t *app, tiku_app_lease_t *lease)
{
    if (!app || !lease || lease->app || app->leases == SIZE_MAX ||
        (app->phase != APP_RUNNING && app->phase != APP_STARTING)) return 0;
    ++app->leases;
    lease->app = app;
    return 1;
}

void *tiku_app_lease_state(const tiku_app_lease_t *lease)
{
    tiku_static_app_t *app = lease ? lease->app : NULL;
    return app && (app->phase == APP_RUNNING || app->phase == APP_STARTING) ? app->state.data : NULL;
}

void tiku_app_lease_release(tiku_app_lease_t *lease)
{
    if (!lease || !lease->app) return;
    --lease->app->leases;
    lease->app = NULL;
}
