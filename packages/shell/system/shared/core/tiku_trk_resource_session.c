/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_resource_session.c - owner-thread desktop resource service.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_trk_resource_session.h"
#include "tiku_trk_resource_discovery.h"
#include "tiku_shell_product.h"
#include "tiku_resource_endpoint.h"
#include "tiku_resource_service.h"
#include "tiku_resource_stream.h"
#include "tiku_resource_backend.h"
#include "tiku_mount.h"
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/** @brief One authenticated local stream, broker and idle deadline. */
typedef struct {
    struct tiku_trk_resource_session *session;
    int fd;
    tiku_resource_channel_t *channel;
    tiku_resource_service_t *service;
    uint64_t activity;
} resource_peer_t;

struct tiku_trk_resource_session {
    pthread_t thread;
    tiku_resource_registry_t *registry;
    tiku_trk_resource_discovery_t *discovery;
    tiku_resource_endpoint_t *endpoint;
    tiku_backend_t *namespace_backend,*backend;
    tiku_resource_caller_t interactive;
    resource_peer_t peers[8];
    char mount[TIKU_RESOURCE_PATH_MAX],pending[TIKU_RESOURCE_ID_MAX];
    uint64_t finish_at;
    unsigned next;
    int enabled,pending_enabled,cancelled,stop;
};

/** @brief Monotonic local service deadlines. */
static uint64_t now_ms(void)
{
    struct timespec time;if(clock_gettime(CLOCK_MONOTONIC,&time))return 0;
    return (uint64_t)time.tv_sec*1000+(uint64_t)time.tv_nsec/1000000;
}

/** @brief Build three independently owned values for a fixture snapshot. */
static void snapshot(tiku_resource_snapshot_t *out,const tiku_msg_t *value)
{
    memset(out, 0, sizeof *out);
    out->desired = tiku_msg_copy(value);
    out->persisted = tiku_msg_copy(value);
    out->applied = tiku_msg_copy(value);
    strcpy(out->apply_state,"applied");
}

/** @brief Validate fixture setting values and typed action arguments. */
static tiku_resource_error_t validate(void *owner, const char *path,
                                      const tiku_msg_t *value, int invoke)
{
    int enabled;(void)owner;(void)path;
    if (invoke) {
        const char *type = tiku_msg_find_string(value, "type", 0);
        return type && !strcmp(type, "object") ? TIKU_RESOURCE_OK
                                               : TIKU_RESOURCE_VALUE;
    }
    return tiku_msg_find_bool(value, "value", 0, &enabled)
               ? TIKU_RESOURCE_OK
               : TIKU_RESOURCE_VALUE;
}

/** @brief Apply synthetic state without changing a product preference. */
static tiku_resource_error_t apply(void *opaque, const char *path,
                                   const tiku_msg_t *value, int invoke,
                                   tiku_resource_snapshot_t *out)
{
    tiku_trk_resource_session_t *session=opaque;
    tiku_msg_t *result;
    if(!strcmp(path,"/test/fail"))return TIKU_RESOURCE_PERSISTENCE_FAILED;
    if(invoke){
        if(!strcmp(path,"/test/stop"))session->stop=1;
        result = tiku_msg_new(0);
        if (result)
            tiku_msg_add_string(result, "type", "null");
    }else{
        tiku_msg_find_bool(value, "value", 0, &session->enabled);
        result = tiku_resource_value_integer(TIKU_RESOURCE_BOOLEAN,
                                             session->enabled);
    }
    snapshot(out, result);
    tiku_msg_free(result);
    return out->desired && out->persisted && out->applied
               ? TIKU_RESOURCE_OK
               : TIKU_RESOURCE_NO_MEMORY;
}

/** @brief Queue fixture work for completion on a later desktop turn. */
static tiku_resource_error_t start(void *opaque, const char *job,
                                   const char *path, const tiku_msg_t *value,
                                   int invoke, tiku_resource_snapshot_t *out)
{
    tiku_trk_resource_session_t *session = opaque;
    (void)path;
    (void)invoke;
    (void)out;
    strcpy(session->pending, job);
    tiku_msg_find_bool(value, "value", 0, &session->pending_enabled);
    session->finish_at = now_ms() + 50;
    session->cancelled = 0;
    return TIKU_RESOURCE_PENDING;
}

/** @brief Request cancellation without completing work inside the callback.
 */
static tiku_resource_cancel_t cancel(void *opaque,const char *job)
{
    tiku_trk_resource_session_t *session=opaque;
    if(strcmp(job,session->pending))return TIKU_RESOURCE_CANCEL_TOO_LATE;
    session->cancelled=1;return TIKU_RESOURCE_CANCEL_ACCEPTED;
}

/** @brief Register product identity and optional fixtures with copied
 * lifetimes.
 */
static int providers(tiku_trk_resource_session_t *session, const char *product,
                     int fixture)
{
    tiku_resource_initial_t initial[9]={0};tiku_resource_stamp_t stamp;
    tiku_resource_controller_t controller={validate,apply};
    tiku_resource_async_controller_t asynchronous={validate,start,cancel,1000};
    const char *paths[] = {"/test/enabled",     "/test/readonly",
                           "/test/fail",        "/test/action",
                           "/test/stop",        "/test/items",
                           "/test/items/first", "/test/content"};
    size_t i;
    tiku_msg_t *value;
    (void)product;
    if (!fixture)
        return 0;
    memset(initial,0,sizeof initial);
    for(i=0;i<8;i++){
        tiku_resource_kind_t kind = i == 1 || i == 6   ? TIKU_RESOURCE_STATE
                                    : i == 3 || i == 4 ? TIKU_RESOURCE_ACTION
                                    : i == 5 ? TIKU_RESOURCE_COLLECTION
                                    : i == 7 ? TIKU_RESOURCE_STREAM
                                             : TIKU_RESOURCE_SETTING;
        initial[i].spec = (tiku_resource_spec_t){
            .path = paths[i],
            .kind = kind,
            .schema_version = 1,
            .schema = {.type = kind == TIKU_RESOURCE_ACTION ||
                                       kind == TIKU_RESOURCE_STREAM
                                   ? TIKU_RESOURCE_OBJECT
                               : kind == TIKU_RESOURCE_COLLECTION
                                   ? TIKU_RESOURCE_NULL
                                   : TIKU_RESOURCE_BOOLEAN},
            .read_scopes = TIKU_RESOURCE_READ,
            .change_scopes = kind == TIKU_RESOURCE_ACTION
                                 ? TIKU_RESOURCE_INVOKE
                                 : TIKU_RESOURCE_WRITE,
            .public_read = kind != TIKU_RESOURCE_STREAM,
            .sensitive = kind == TIKU_RESOURCE_STREAM,
            .persistence = kind == TIKU_RESOURCE_SETTING ? "user" : "none",
            .controller = "test-fixture"};
        if(kind==TIKU_RESOURCE_STREAM)value=tiku_resource_stream_metadata(0,"");
        else if (kind == TIKU_RESOURCE_ACTION ||
                 kind == TIKU_RESOURCE_COLLECTION) {
            value = tiku_msg_new(0);
            if (value)
                tiku_msg_add_string(value, "type", "null");
        } else
            value = tiku_resource_value_integer(TIKU_RESOURCE_BOOLEAN, 0);
        snapshot(&initial[i].initial,value);tiku_msg_free(value);
    }
    i = tiku_resource_register(session->registry, "test", "/test", NULL,
                               initial, 8, &controller, session, &stamp);
    {
        size_t j;
        for (j = 0; j < 8; j++)
            tiku_resource_snapshot_clear(&initial[j].initial);
    }
    if (i || tiku_resource_stream_publish(
                 session->registry, &stamp, "/test/content",
                 "Stomme resource snapshot\n", 25, &stamp))
        return -1;
    memset(initial, 0, sizeof initial);
    initial[0].spec =
        (tiku_resource_spec_t){.path = "/test-work/enabled",
                               .kind = TIKU_RESOURCE_SETTING,
                               .schema_version = 1,
                               .schema = {.type = TIKU_RESOURCE_BOOLEAN},
                               .public_read = 1,
                               .read_scopes = TIKU_RESOURCE_READ,
                               .change_scopes = TIKU_RESOURCE_WRITE,
                               .persistence = "user",
                               .controller = "test-worker"};
    value = tiku_resource_value_integer(TIKU_RESOURCE_BOOLEAN, 0);
    snapshot(&initial[0].initial, value);
    tiku_msg_free(value);
    i = tiku_resource_register_async(session->registry, "test-work",
                                     "/test-work", NULL, initial, 1,
                                     &asynchronous, session, &stamp);
    tiku_resource_snapshot_clear(&initial[0].initial);return i?-1:0;
}

/** @brief Bind the private interactive endpoint to explicit same-user
 * authority.
 */
static int authorize(void *opaque, uid_t uid, pid_t pid,
                     tiku_resource_caller_t *out)
{
    resource_peer_t *peer=opaque;(void)pid;
    if(uid!=geteuid())return 0;
    *out=peer->session->interactive;return 1;
}

/** @brief Compose negotiated channel limits and broker state on the desktop
 * thread.
 */
static tiku_msg_t *dispatch(void *opaque, const tiku_resource_caller_t *caller,
                            const tiku_msg_t *request)
{
    resource_peer_t *peer=opaque;
    if(!peer->service){
        tiku_resource_protocol_limits_t limits;
        peer->service = tiku_resource_service_new(
            peer->session->registry, caller,
            tiku_resource_channel_features(peer->channel));
        if (!peer->service ||
            tiku_resource_channel_limits(peer->channel, &limits) ||
            tiku_resource_service_limits(peer->service, &limits))
            return NULL;
    }
    return tiku_resource_service_dispatch(peer->service,caller,request);
}

/** @brief Retire connection-owned watches, jobs and streams. */
static void closed(void *opaque,const tiku_resource_caller_t *caller)
{
    resource_peer_t *peer = opaque;
    (void)caller;
    tiku_resource_service_free(peer->service);
    peer->service = NULL;
}

/** @brief Release a peer after dispatch or transport retirement. */
static void peer_close(resource_peer_t *peer)
{
    tiku_resource_channel_free(peer->channel);if(peer->fd>=0)close(peer->fd);
    peer->channel=NULL;peer->fd=-1;
}

tiku_trk_resource_session_t *
tiku_trk_resource_session_open(tiku_backend_t *namespace_backend,
                               const char *directory, const char *mount,
                               const char *product, int fixture)
{
    tiku_trk_resource_session_t *session; tiku_model_t existing;unsigned i;
    if (!tiku_mount_is(namespace_backend) ||
        !tiku_resource_path_valid(mount) || !strcmp(mount, "/") ||
        strlen(mount) + TIKU_RESOURCE_PATH_MAX > TIKU_PATH_MAX || !product) {
        errno = EINVAL;
        return NULL;
    }
    errno = 0;
    if (!namespace_backend->ops->stat(namespace_backend, mount, &existing) ||
        errno != ENOENT) {
        errno = EEXIST;
        return NULL;
    }
    session=calloc(1,sizeof *session);if(!session)return NULL;
    session->thread = pthread_self();
    session->namespace_backend = namespace_backend;
    strcpy(session->mount, mount);
    for(i=0;i<8;i++){session->peers[i].fd=-1;session->peers[i].session=session;}
    {
        tiku_resource_limits_t limits={.providers=512,.resources=16384};
        session->registry=tiku_resource_registry_new(&limits);
    }
    if (!session->registry ||
        tiku_resource_new_id(session->interactive.principal))
        goto fail;
    session->interactive.authenticated=1;session->interactive.scopes=63;
    session->discovery = tiku_trk_resource_discovery_open(
        session->registry, product, TIKU_SHELL_ABOUT_VERSION);
    if (!session->discovery || providers(session, product, fixture))
        goto fail;
    session->backend =
        tiku_resource_backend_open(session->registry, &session->interactive);
    if (!session->backend)
        goto fail;
    session->endpoint = tiku_resource_endpoint_open(directory);
    if (!session->endpoint)
        goto fail;
    if(tiku_mount_add(namespace_backend,mount,session->backend,0))goto fail;
    return session;
fail:
    tiku_resource_endpoint_close(session->endpoint);
    tiku_backend_close(session->backend);
    tiku_trk_resource_discovery_close(session->discovery);
    tiku_resource_registry_free(session->registry);
    free(session);
    return NULL;
}

tiku_resource_registry_t *
tiku_trk_resource_session_registry(tiku_trk_resource_session_t *session)
{
    return session ? session->registry : NULL;
}

int tiku_trk_resource_session_pump(tiku_trk_resource_session_t *session)
{
    static const tiku_resource_channel_ops_t ops={authorize,dispatch,closed};
    unsigned i,handled=0;uint64_t now;
    if(!session || !pthread_equal(session->thread,pthread_self()))return 0;
    now=now_ms();
    tiku_trk_resource_discovery_poll(session->discovery, now);
    for(i=0;i<8;i++)if(!session->peers[i].channel){
            resource_peer_t *peer = &session->peers[i];
            peer->fd = tiku_resource_endpoint_accept(session->endpoint);
            if (peer->fd < 0)
                break;
            peer->activity = now;
            peer->channel = tiku_resource_channel_server(
                peer->fd, tiku_resource_registry_session(session->registry),
                TIKU_RESOURCE_FEATURES & ~TIKU_RESOURCE_FEATURE_PROVIDER, &ops,
                peer);
            if (!peer->channel) {
                close(peer->fd);
                peer->fd = -1;
            }
    }
    tiku_resource_jobs_pump(session->registry);
    if(session->pending[0] && now>=session->finish_at){
        tiku_resource_snapshot_t value = {0};
        tiku_msg_t *enabled = tiku_resource_value_integer(
            TIKU_RESOURCE_BOOLEAN, session->pending_enabled);
        if(!session->cancelled)snapshot(&value,enabled);
        tiku_msg_free(enabled);
        tiku_resource_job_complete(
            session->registry, session->pending,
            session->cancelled ? NULL : &value,
            session->cancelled ? TIKU_RESOURCE_CANCELLED : TIKU_RESOURCE_OK,
            session->cancelled ? TIKU_RESOURCE_EFFECT_NONE
                               : TIKU_RESOURCE_EFFECT_PARTIAL);
        tiku_resource_snapshot_clear(&value);session->pending[0]=0;
    }
    for(i=0;i<8;i++){
        unsigned index = (session->next + i) % 8;
        resource_peer_t *peer = &session->peers[index];
        int ready;
        tiku_resource_protocol_limits_t limits;
        if(!peer->channel)continue;
        tiku_resource_protocol_limits_default(&limits);
        tiku_resource_channel_limits(peer->channel, &limits);
        ready=tiku_resource_endpoint_ready(peer->fd,limits.frame_bytes);
        if (ready < 0 || !now || now - peer->activity > 30000) {
            peer_close(peer);
            continue;
        }
        if(ready && handled<2){
            handled++;peer->activity=now;
            if (tiku_resource_channel_step(peer->channel, 5)) {
                peer_close(peer);
                continue;
            }
        }
        if(peer->service){
            tiku_msg_t *event=NULL;int available;
            available = tiku_resource_service_event(
                peer->service, &session->interactive, &event);
            if (available < 0 || (available && tiku_resource_channel_send(
                                                   peer->channel, event, 5))) {
                tiku_msg_free(event);
                peer_close(peer);
                continue;
            }
            tiku_msg_free(event);
        }
    }
    session->next=(session->next+1)%8;return session->stop;
}

void tiku_trk_resource_session_close(tiku_trk_resource_session_t *session)
{
    unsigned i;
    if (!session || !pthread_equal(session->thread, pthread_self()))
        return;
    for(i=0;i<8;i++)peer_close(&session->peers[i]);
    tiku_mount_remove(session->namespace_backend, session->mount);
    tiku_backend_close(session->backend);
    tiku_resource_endpoint_close(session->endpoint);
    tiku_trk_resource_discovery_close(session->discovery);
    tiku_resource_registry_free(session->registry);
    free(session);
}

unsigned tiku_trk_resource_session_connections(
    const tiku_trk_resource_session_t *session)
{
    unsigned count = 0;
    if (!session || !pthread_equal(session->thread, pthread_self()))
        return 0;
    for(unsigned i=0;i<8;i++)count+=session->peers[i].channel!=NULL;
    return count;
}
