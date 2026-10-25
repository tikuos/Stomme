/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_desktop_app_vfs.c - assigned providers on native application connections.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_trk_desktop_private.h"
#include "tiku_app_resources.h"
#include "tiku_resource_service.h"
#include <stdlib.h>
/** @brief One native connection's grant and connection-owned provider broker. */
typedef struct {
    uint64_t generation;
    tiku_resource_caller_t caller;
    tiku_resource_service_t *service;
} native_provider_t;
/** @brief Native resource brokers share the desktop registry and its thread. */
struct desktop_app_vfs {
    tiku_resource_registry_t *registry;
    native_provider_t peers[TIKU_REMOTE_SESSIONS];
};
/** @brief Retire the assignment before its connection generation can be replaced. */
static void close_peer(native_provider_t *peer)
{
    tiku_resource_service_free(peer->service);
    memset(peer, 0, sizeof *peer);
}
int desktop_app_vfs_open(desktop_t *app)
{
    tiku_resource_registry_t *registry =
        tiku_trk_resource_session_registry(app->resource_session);
    if (!registry)
        return 0;
    app->app_vfs = calloc(1, sizeof *app->app_vfs);
    if (!app->app_vfs)
        return -1;
    app->app_vfs->registry = registry;
    return 0;
}
void desktop_app_vfs_close(desktop_t *app)
{
    if (!app->app_vfs)
        return;
    for (int i = 0; i < TIKU_REMOTE_SESSIONS; i++)
        close_peer(&app->app_vfs->peers[i]);
    free(app->app_vfs);
    app->app_vfs = NULL;
}
int desktop_app_vfs_prefix(desktop_t *app, uint64_t generation, char *out,
                           size_t capacity)
{
    if (!app || !app->app_vfs || !out || !capacity)
        return 0;
    out[0] = 0;
    for (int i = 0; i < TIKU_REMOTE_SESSIONS; i++) {
        native_provider_t *peer = &app->app_vfs->peers[i];
        if (peer->service && peer->generation == generation) {
            int n =
                snprintf(out, capacity, "/apps/%s", peer->caller.principal);
            return n > 0 && (size_t)n < capacity;
        }
    }
    return 0;
}
int desktop_app_vfs_message(desktop_t *app, int index,
                            const tiku_msg_t *message)
{
    uint32_t what = tiku_msg_what(message);
    if (what != TIKU_APP_RESOURCE_HELLO && what != TIKU_RESOURCE_MESSAGE_CODE)
        return 0;
    if (!app->app_vfs || index < 0 || index >= TIKU_REMOTE_SESSIONS)
        return 1;
    tiku_remote_session_t *session = &app->remote.session[index];
    native_provider_t *peer = &app->app_vfs->peers[index];
    if (!session->used || session->fd < 0)
        return 1;
    if (peer->generation != session->generation)
        close_peer(peer);
    if (what == TIKU_APP_RESOURCE_HELLO) {
        int32_t version;
        if (tiku_msg_field_count(message) != 1 ||
            !tiku_msg_find_int32(message, "version", 0, &version) ||
            version != 1)
            return 1;
        if (!peer->service) {
            peer->generation = session->generation;
            peer->caller.authenticated = 1;
            peer->caller.scopes = TIKU_RESOURCE_READ | TIKU_RESOURCE_WRITE |
                                  TIKU_RESOURCE_INVOKE |
                                  TIKU_RESOURCE_CONTENT |
                                  TIKU_RESOURCE_PROVIDER;
            if (tiku_resource_new_id(peer->caller.principal))
                return 1;
            peer->service = tiku_resource_service_new(
                app->app_vfs->registry, &peer->caller, TIKU_RESOURCE_FEATURES);
            if (!peer->service ||
                tiku_resource_service_provider_enable(peer->service)) {
                close_peer(peer);
                return 1;
            }
        }
        char generation[32];
        snprintf(generation, sizeof generation, "%llu",
                 (unsigned long long)peer->generation);
        tiku_msg_t *reply = tiku_msg_new(TIKU_APP_RESOURCE_HELLO);
        if (reply && tiku_msg_add_int32(reply, "version", 1) &&
            tiku_msg_add_int64(reply, "features", TIKU_RESOURCE_FEATURES) &&
            tiku_msg_add_string(
                reply, "session",
                tiku_resource_registry_session(app->app_vfs->registry)) &&
            tiku_msg_add_string(reply, "principal", peer->caller.principal) &&
            tiku_msg_add_string(reply, "connection", generation))
            (void)tiku_remote_session_tell(session, reply);
        tiku_msg_free(reply);
        return 1;
    }
    const char *op = tiku_msg_find_string(message, "op", 0);
    if (!peer->service || !op || strncmp(op, "provider-", 9))
        return 1;
    tiku_msg_t *reply =
        tiku_resource_service_dispatch(peer->service, &peer->caller, message);
    if (reply)
        (void)tiku_remote_session_tell(session, reply);
    tiku_msg_free(reply);
    return 1;
}
void desktop_app_vfs_poll(desktop_t *app)
{
    if (!app->app_vfs)
        return;
    for (int i = 0; i < TIKU_REMOTE_SESSIONS; i++) {
        native_provider_t *peer = &app->app_vfs->peers[i];
        tiku_remote_session_t *session = &app->remote.session[i];
        if (!peer->service)
            continue;
        if (!session->used || session->fd < 0 ||
            peer->generation != session->generation) {
            close_peer(peer);
            continue;
        }
        tiku_msg_t *event = NULL;
        int available =
            tiku_resource_service_event(peer->service, &peer->caller, &event);
        if (available < 0 ||
            (available && !tiku_remote_session_tell(session, event)))
            close_peer(peer);
        tiku_msg_free(event);
    }
}
