/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_desktop_host_vfs.c - observed display, input, audio and runtime
 * services.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_desktop_host_vfs_private.h"
#include <unistd.h>
static const tiku_trk_observation_t input_fields[] = {
    {"/input/capabilities", 0, 3},
    {"/input/focus", 0, 3},
    {"/input/keymap", 0, 3},
    {"/input/modifiers", 0, 3},
    {"/input/pointer", 0, 3}};
static const tiku_trk_observation_t audio_fields[]={
    {"/audio/availability",0,3},{"/audio/playback",0,3},{"/audio/volume",0,3}
};
static const tiku_trk_observation_t metric_fields[] = {
    {"/metrics/provider-count", 0, 3},
    {"/metrics/session-count", 0, 3},
    {"/metrics/jobs", 0, 3},
    {"/metrics/queue-depth", 0, 3},
    {"/metrics/drops", 0, 3}};
static const tiku_trk_observation_t access_fields[] = {
    {"/accessibility/applications", 1, 3},
    {"/accessibility/capabilities", 0, 3},
    {"/accessibility/generations", 1, 3},
    {"/accessibility/trees", 1, 3},
    {"/accessibility/actions/perform-action", 2, 3}};
static const tiku_trk_observation_t print_fields[] = {
    {"/printers/native/identity", 0, 3},
    {"/printers/native/capabilities", 0, 3},
    {"/printers/native/state", 0, 3},
    {"/printers/native/jobs", 1, 3},
    {"/printers/native/actions/submit", 2, 3},
    {"/printers/native/actions/cancel", 2, 3}};
/** @brief These observation groups contain no streams. */
static int count_items(void *context, const char *path)
{
    (void)context;
    (void)path;
    return 0;
}
/** @brief Copy this host's canvas facts without claiming a monitor topology.
 */
static tiku_msg_t *display_capture(void *context,const char *path,int index)
{
    struct desktop_host_vfs *owner = context;
    desktop_t *app = owner->app;
    tiku_msg_t *out = trk_value_container("object");
    int bad = 0;
    (void)index;
    const char *field=strrchr(path,'/');
    if (!strcmp(field, "/identity")) {
        bad |= TRK_TEXT(out, "id", owner->display_id);
        bad |= TRK_TEXT(out, "kind", "desktop_host_canvas");
        bad |= TRK_INT(out, "pid", getpid());
    } else if (!strcmp(field, "/host")) {
        bad |= TRK_TEXT(out, "backend", owner->host.backend);
        bad |= TRK_BOOL(out, "native_available", owner->host.available);
        bad |= TRK_BOOL(out, "mapped", owner->host.mapped);
    } else if (!strcmp(field, "/geometry")) {
        bad |= TRK_INT(out, "x", 0);
        bad |= TRK_INT(out, "y", 0);
        bad |= TRK_INT(out, "width", app->surf ? app->surf->w : 0);
        bad |= TRK_INT(out, "height", app->surf ? app->surf->h : 0);
        bad |= TRK_INT(out, "native_width", owner->host.native_width);
        bad |= TRK_INT(out, "native_height", owner->host.native_height);
        bad |= TRK_TEXT(out, "coordinates", "host_logical_canvas");
    } else if (!strcmp(field, "/scale")) {
        bad |= TRK_BOOL(out, "known", owner->host.available);
        bad |=
            TRK_INT(out, "native_pixels_per_logical_pixel", owner->host.scale);
    } else {
        bad |= TRK_BOOL(out, "native_window", owner->host.available);
        bad |= TRK_BOOL(out, "physical_monitor_inventory", 0);
        bad |= TRK_BOOL(out, "fullscreen", owner->host.fullscreen);
        bad |= TRK_BOOL(out, "configuration_actions", 0);
        bad |= TRK_TEXT(out, "scope", "existing_single_host_canvas");
    }
    if(bad){tiku_msg_free(out);return NULL;}return out;
}
/** @brief Copy current normalized input facts without recording key content.
 */
static tiku_msg_t *input_capture(void *context,const char *path,int index)
{
    struct desktop_host_vfs *owner = context;
    desktop_t *app = owner->app;
    tiku_msg_t *out = trk_value_container("object");
    int bad = 0;
    (void)index;
    if(!strcmp(path,"/input/capabilities")) {
        bad |= TRK_BOOL(out, "native_available", owner->host.available);
        bad |= TRK_BOOL(out, "normalized_input_observed", owner->input_seen);
        bad |= TRK_BOOL(out, "vfs_input_injection", 0);
        bad |= TRK_BOOL(out, "native_layout_query", 0);
    } else if(!strcmp(path,"/input/focus")) {
        char id[33]={0};tiku_window_t *window=tiku_workspace_focused(app->desk);
        int known =
            window && !tiku_trk_window_resources_id(app->window_resources,
                                                    window, id, sizeof id);
        bad |= TRK_BOOL(out, "window_known", known);
        bad |= TRK_TEXT(out, "window", known ? id : "");
        bad |= TRK_BOOL(out, "activation_observed", owner->native_active);
    } else if(!strcmp(path,"/input/keymap")) {
        bad |= TRK_TEXT(out, "encoding", "portable-logical-keys");
        bad |= TRK_BOOL(out, "native_layout_known", 0);
        bad |= TRK_TEXT(out, "reason", "native_layout_query_not_provided");
    } else if (!strcmp(path, "/input/modifiers")) {
        bad |= TRK_BOOL(out, "known", owner->input_seen);
        bad |= TRK_INT(out, "value", owner->modifiers);
    } else {
        bad |= TRK_BOOL(out, "known", owner->pointer_seen);
        bad |= TRK_INT(out, "x", app->ptr_x);
        bad |= TRK_INT(out, "y", app->ptr_y);
        bad |= TRK_TEXT(out, "coordinates", "host_logical_canvas");
    }
    if(bad){tiku_msg_free(out);return NULL;}return out;
}
/** @brief Copy sound-engine facts without opening an audio backend. */
static tiku_msg_t *audio_capture(void *context,const char *path,int index)
{
    struct desktop_host_vfs *owner = context;
    tiku_sound_observation_t *sound = &owner->sound;
    tiku_msg_t *out = trk_value_container("object");
    int bad = 0;
    (void)index;
    if(!strcmp(path,"/audio/availability")) {
        bad |= TRK_BOOL(out, "configured", sound->configured);
        bad |= TRK_BOOL(out, "opened", sound->opened);
        bad |= TRK_BOOL(out, "opening", sound->opening);
        bad |= TRK_BOOL(out, "last_open_known", sound->last_open_known);
        bad |= TRK_BOOL(out, "last_open_ok", sound->last_open_ok);
        bad |= TRK_TEXT(out, "backend", sound->backend);
        bad |= TRK_TEXT(out, "availability_scope",
                        "configured_engine_and_last_open_result");
    } else if(!strcmp(path,"/audio/volume")) {
        bad |= TRK_INT(out, "percent", sound->volume);
        bad |= TRK_BOOL(out, "observed_only", 1);
        bad |= TRK_TEXT(out, "setting", "/settings/sound/volume");
    } else {
        bad |= TRK_BOOL(out, "playing", sound->event >= 0);
        bad |= TRK_INT(out, "event", sound->event);
        bad |= trk_counter(out, "frames_written", sound->written);
        bad |= trk_counter(out, "frames", sound->frames);
        for (int i = 0; i < TIKU_SOUND_EVENTS; i++) {
            char key[32];
            snprintf(key, sizeof key, "count_%s",
                     tiku_sound_name((tiku_sound_event_t)i));
            bad |=
                trk_counter(out, key, tiku_sound_count((tiku_sound_event_t)i));
        }
    }
    if(bad){tiku_msg_free(out);return NULL;}return out;
}
/** @brief Copy registered providers and explicit scopes of runtime counters.
 */
static tiku_msg_t *metric_capture(void *context,const char *path,int index)
{
    struct desktop_host_vfs *owner = context;
    desktop_t *app = owner->app;
    tiku_msg_t *out = trk_value_container("object");
    int bad = 0;
    (void)index;
    tiku_work_stats_t *work = &owner->work;
    tiku_resource_registry_observation_t *registry = &owner->registry_stats;
    if(!strcmp(path,"/metrics/provider-count")) {
        bad |= TRK_INT(out, "registered", registry->providers);
        bad |= TRK_INT(out, "capacity", registry->provider_capacity);
        bad |= TRK_INT(out, "nodes", registry->nodes);
        bad |= TRK_INT(out, "node_capacity", registry->node_capacity);
        bad |= trk_counter(out, "retained_stream_bytes",
                           registry->retained_stream_bytes);
    } else if(!strcmp(path,"/metrics/jobs")) {
        bad |= trk_counter(out, "worker_posted", work->posted);
        bad |= trk_counter(out, "worker_collected", work->collected);
        bad |= trk_counter(out, "worker_cancelled", work->cancelled);
        bad |= TRK_INT(out, "worker_active", work->active);
        bad |= TRK_INT(out, "resource_active", registry->active_jobs);
        bad |= TRK_INT(out, "resource_retained", registry->retained_jobs);
    } else if(!strcmp(path,"/metrics/queue-depth")) {
        bad |= TRK_INT(out, "worker_queued", work->queued);
        bad |= TRK_INT(out, "worker_running", work->running);
        bad |= TRK_INT(out, "worker_finished_pending", work->finished_pending);
        bad |= TRK_INT(out, "injected_events",
                       (app->cq_tail - app->cq_head +
                        (int)(sizeof app->cq / sizeof app->cq[0])) %
                           (int)(sizeof app->cq / sizeof app->cq[0]));
    } else if(!strcmp(path,"/metrics/drops")) {
        bad |= trk_counter(out, "worker_refusals", work->rejected);
        bad |= trk_counter(out, "injected_input_drops", app->input_drops);
        bad |= TRK_INT(out, "paused_watches", registry->paused_watches);
        bad |= TRK_BOOL(out, "native_input_drop_counter", 0);
        bad |= TRK_TEXT(out, "scope",
                        "work_pool_conductor_and_current_resource_watches");
    } else {
        int connections = 0;
        for (int i = 0; i < TIKU_REMOTE_SESSIONS; i++)
            connections +=
                app->remote.session[i].used && app->remote.session[i].fd >= 0;
        bad |= TRK_INT(out, "application_connections", connections);
        bad |= TRK_INT(
            out, "resource_connections",
            tiku_trk_resource_session_connections(app->resource_session));
        bad |= TRK_INT(out, "resource_brokers", registry->services);
    }
    if(bad){tiku_msg_free(out);return NULL;}return out;
}
void desktop_host_vfs_event(desktop_t *app,const tiku_event_t *event)
{
    struct desktop_host_vfs *owner=app->host_vfs;if(!owner||!event)return;
    if (event->type == TIKU_EVENT_KEY_DOWN ||
        event->type == TIKU_EVENT_KEY_UP ||
        event->type == TIKU_EVENT_POINTER_DOWN ||
        event->type == TIKU_EVENT_POINTER_UP ||
        event->type == TIKU_EVENT_POINTER_MOVE ||
        event->type == TIKU_EVENT_WHEEL) {
        owner->input_seen=1;owner->modifiers=event->modifiers;
        if (event->type >= TIKU_EVENT_POINTER_DOWN &&
            event->type <= TIKU_EVENT_WHEEL)
            owner->pointer_seen = 1;
    }
    if(event->type==TIKU_EVENT_ACTIVATED)owner->native_active=1;
    if(event->type==TIKU_EVENT_DEACTIVATED)owner->native_active=0;
}
void desktop_host_vfs_print_done(desktop_t *app, uint64_t ticket, int status,
                                 const char *reason)
{
    struct desktop_host_vfs *owner=app->host_vfs;if(!owner)return;
    owner->last_print_known = 1;
    owner->last_print_ticket = ticket;
    owner->last_print_status = status;
    snprintf(owner->last_print_reason, sizeof owner->last_print_reason, "%s",
             reason ? reason : "");
}
void desktop_host_vfs_poll(desktop_t *app,uint64_t now)
{
    struct desktop_host_vfs *owner = app->host_vfs;
    if (!owner || now < owner->next_poll)
        return;
    owner->next_poll = now + 250;
    tiku_host_observe(app->host, &owner->host);
    tiku_sound_observe(&owner->sound);
    tiku_work_stats(&owner->work);
    (void)tiku_resource_registry_observe(owner->registry,
                                         &owner->registry_stats);
    owner->print_count =
        tiku_host_print_observe(app->host, owner->print_jobs, 4);
    if (owner->print_count > 4)
        owner->print_count = 4;
    desktop_host_access_refresh(owner);
    tiku_trk_clipboard_resources_poll(owner->clipboard, now);
    tiku_trk_volume_resources_poll(owner->volumes, now);
    tiku_trk_resource_observer_poll(owner->display, now);
    tiku_trk_resource_observer_poll(owner->input, now);
    tiku_trk_resource_observer_poll(owner->audio, now);
    tiku_trk_resource_observer_poll(owner->metrics, now);
    tiku_trk_resource_observer_poll(owner->access, now);
    tiku_trk_resource_observer_poll(owner->printers, now);
}
int desktop_host_vfs_open(desktop_t *app)
{
    tiku_resource_registry_t *registry =
        tiku_trk_resource_session_registry(app->resource_session);
    if (!registry)
        return 0;
    struct desktop_host_vfs *owner = calloc(1, sizeof *owner);
    if (!owner)
        return -1;
    owner->app = app;
    owner->registry = registry;
    app->host_vfs = owner;
    if(tiku_resource_new_id(owner->display_id))return -1;
    const char *names[]={"identity","host","geometry","scale","capabilities"};
    for (int i = 0; i < 5; i++) {
        snprintf(owner->display_paths[i], sizeof owner->display_paths[i],
                 "/display/%s/%s", owner->display_id, names[i]);
        owner->display_fields[i] =
            (tiku_trk_observation_t){owner->display_paths[i], 0, 3};
    }
    tiku_host_observe(app->host, &owner->host);
    tiku_sound_observe(&owner->sound);
    tiku_work_stats(&owner->work);
    (void)tiku_resource_registry_observe(registry, &owner->registry_stats);
    owner->display = tiku_trk_resource_observer_open(
        registry, "native-display", "/display", owner->display_fields, 5,
        count_items, display_capture, owner);
    owner->input = tiku_trk_resource_observer_open(
        registry, "native-input", "/input", input_fields, 5, count_items,
        input_capture, owner);
    owner->audio = tiku_trk_resource_observer_open(
        registry, "native-audio", "/audio", audio_fields, 3, count_items,
        audio_capture, owner);
    owner->metrics = tiku_trk_resource_observer_open(
        registry, "native-metrics", "/metrics", metric_fields, 5, count_items,
        metric_capture, owner);
    const tiku_resource_async_controller_t access = {
        .validate = desktop_host_access_validate,
        .start = desktop_host_access_start};
    const tiku_resource_async_controller_t print = {
        .validate = desktop_host_print_validate,
        .start = desktop_host_print_start};
    owner->access = tiku_trk_resource_observer_open_controlled(
        registry, "native-accessibility", "/accessibility", access_fields, 5,
        desktop_host_access_count, desktop_host_access_capture, &access,
        owner);
    owner->printers = tiku_trk_resource_observer_open_controlled(
        registry, "native-printers", "/printers/native", print_fields, 6,
        desktop_host_print_count, desktop_host_print_capture, &print, owner);
    owner->clipboard = tiku_trk_clipboard_resources_open(registry);
    owner->volumes =
        tiku_trk_volume_resources_open(registry, desktop_request_unmount, app);
    if (!owner->display || !owner->input || !owner->audio || !owner->metrics ||
        !owner->access || !owner->printers || !owner->clipboard ||
        !owner->volumes)
        return -1;
    desktop_host_vfs_poll(app,1);return 0;
}
void desktop_host_vfs_close(desktop_t *app)
{
    struct desktop_host_vfs *owner=app->host_vfs;if(!owner)return;
    tiku_trk_volume_resources_close(owner->volumes);
    tiku_trk_clipboard_resources_close(owner->clipboard);
    tiku_trk_resource_observer_close(owner->printers);
    tiku_trk_resource_observer_close(owner->access);
    desktop_host_access_clear(owner);
    tiku_trk_resource_observer_close(owner->metrics);
    tiku_trk_resource_observer_close(owner->audio);
    tiku_trk_resource_observer_close(owner->input);
    tiku_trk_resource_observer_close(owner->display);
    free(owner);app->host_vfs=NULL;
}
