/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_resource_channel.c - bounded framing and local peer negotiation.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif
#include "tiku_resource_channel.h"
#include "tiku_resource_endpoint.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

struct tiku_resource_channel {
    int fd, server, ready, notified, active;
    pthread_t thread;
    uint64_t offered, features;
    tiku_resource_protocol_limits_t limits;
    char session[TIKU_RESOURCE_ID_MAX];
    tiku_resource_caller_t caller;
    tiku_resource_channel_ops_t ops;
    void *host;
};

/** @brief Whether this thread owns a connected channel. */
static int usable(const tiku_resource_channel_t *channel)
{
    return channel && channel->fd >= 0 && pthread_equal(channel->thread, pthread_self());
}

/** @brief Retire the descriptor and notify the host once. */
static int fail(tiku_resource_channel_t *channel)
{
    if (channel->fd >= 0) { close(channel->fd); channel->fd = -1; }
    channel->ready = 0;
    if (channel->server && !channel->notified) {
        channel->notified = 1;
        if (channel->ops.closed) {
            int active = channel->active;
            channel->active = 1;
            channel->ops.closed(channel->host, &channel->caller);
            channel->active = active;
        }
    }
    return -1;
}

/** @brief Monotonic milliseconds, or -1 when unavailable. */
static int64_t now_ms(void)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now)) return -1;
    return (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

/** @brief Transfer exactly length bytes within one deadline. */
static int transfer(tiku_resource_channel_t *channel, void *bytes, size_t length,
                    int writing, int64_t deadline)
{
    size_t done = 0;
    while (done < length) {
        int64_t now = now_ms(), remaining;
        struct pollfd pollfd = {.fd = channel->fd, .events = writing ? POLLOUT : POLLIN};
        int ready;
        ssize_t count;
        if (now < 0 || (remaining = deadline - now) < 0) return fail(channel);
        ready = poll(&pollfd, 1, (int)remaining);
        if (ready < 0 && errno == EINTR) continue;
        if (ready <= 0 || (pollfd.revents & (POLLERR | POLLNVAL))) return fail(channel);
        if (writing) {
            int flags = MSG_DONTWAIT;
#ifdef MSG_NOSIGNAL
            flags |= MSG_NOSIGNAL;
#endif
            count = send(channel->fd, (unsigned char *)bytes + done, length - done, flags);
        } else count = recv(channel->fd, (unsigned char *)bytes + done, length - done, MSG_DONTWAIT);
        if (count < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) continue;
        if (count <= 0) return fail(channel);
        done += (size_t)count;
    }
    return 0;
}

/** @brief Encode and transmit a checked complete frame. */
static int send_message(tiku_resource_channel_t *channel, const tiku_msg_t *message, int timeout)
{
    unsigned char header[4], *bytes;
    size_t size = tiku_msg_flat_size(message);
    int64_t deadline;
    int result;
    if (!usable(channel) || timeout < 0 || timeout > 30000 || !size ||
        size > channel->limits.frame_bytes ||
        (channel->ready && tiku_resource_message_limits(message, &channel->limits, 0)) || tiku_msg_what(message) != TIKU_RESOURCE_MESSAGE_CODE) return -1;
    bytes = malloc(size);
    if (!bytes) return -1;
    if (!tiku_msg_flatten(message, bytes, size, NULL) || tiku_resource_message_check(bytes, size)) {
        free(bytes); return -1;
    }
    deadline = now_ms();
    if (deadline < 0) { free(bytes); return fail(channel); }
    deadline += timeout;
    header[0] = (unsigned char)(size >> 24); header[1] = (unsigned char)(size >> 16);
    header[2] = (unsigned char)(size >> 8); header[3] = (unsigned char)size;
    result = transfer(channel, header, sizeof header, 1, deadline);
    if (!result) result = transfer(channel, bytes, size, 1, deadline);
    free(bytes);
    return result;
}

/** @brief Read and preflight one bounded complete frame. */
static int receive_message(tiku_resource_channel_t *channel, tiku_msg_t **out, int timeout)
{
    unsigned char header[4], *bytes;
    size_t size;
    int64_t deadline;
    int result;
    if (out) *out = NULL;
    if (!usable(channel) || !out || timeout < 0 || timeout > 30000) return -1;
    deadline = now_ms();
    if (deadline < 0) return fail(channel);
    deadline += timeout;
    if (transfer(channel, header, sizeof header, 0, deadline)) return -1;
    size = (size_t)header[0] << 24 | (size_t)header[1] << 16 | (size_t)header[2] << 8 | header[3];
    if (size < 12 || size > channel->limits.frame_bytes) return fail(channel);
    bytes = malloc(size);
    if (!bytes) return fail(channel);
    result = transfer(channel, bytes, size, 0, deadline);
    if (!result) result = tiku_resource_message_decode(bytes, size, out);
    free(bytes);
    if (result || (channel->ready && tiku_resource_message_limits(*out, &channel->limits, 0)) || tiku_msg_what(*out) != TIKU_RESOURCE_MESSAGE_CODE) {
        tiku_msg_free(*out); *out = NULL;
        return fail(channel);
    }
    return 0;
}

/** @brief Require singular fields from an explicit field-name set. */
static int fields(const tiku_msg_t *message, const char *const *allowed, size_t count)
{
    int i;
    for (i = 0; i < tiku_msg_field_count(message); i++) {
        int values;
        size_t j;
        const char *name = tiku_msg_field_at(message, i, NULL, &values);
        if (values != 1) return 0;
        for (j = 0; j < count; j++) if (!strcmp(name, allowed[j])) break;
        if (j == count) return 0;
    }
    return 1;
}

/** @brief Duplicate a connected Unix stream without changing the original fd. */
static tiku_resource_channel_t *open_channel(int fd)
{
    struct sockaddr_un local, peer;
    socklen_t local_size = sizeof local, peer_size = sizeof peer, type_size;
    int type, duplicate;
    tiku_resource_channel_t *channel;
    duplicate = fcntl(fd, F_DUPFD_CLOEXEC, 0);
    if (duplicate < 0) return NULL;
    type_size = sizeof type;
    if (getsockopt(duplicate, SOL_SOCKET, SO_TYPE, &type, &type_size) || type != SOCK_STREAM ||
        getsockname(duplicate, (struct sockaddr *)&local, &local_size) || local.sun_family != AF_UNIX ||
        getpeername(duplicate, (struct sockaddr *)&peer, &peer_size) || peer.sun_family != AF_UNIX) {
        close(duplicate); return NULL;
    }
#ifdef SO_NOSIGPIPE
    {
        int yes = 1;
        if (setsockopt(duplicate, SOL_SOCKET, SO_NOSIGPIPE, &yes, sizeof yes)) { close(duplicate); return NULL; }
    }
#endif
    channel = calloc(1, sizeof *channel);
    if (!channel) { close(duplicate); return NULL; }
    channel->fd = duplicate;
    channel->thread = pthread_self();
    tiku_resource_protocol_limits_default(&channel->limits);
    return channel;
}

tiku_resource_channel_t *tiku_resource_channel_client(int fd)
{
    return open_channel(fd);
}

tiku_resource_channel_t *tiku_resource_channel_server(int fd, const char *session,
    uint64_t features, const tiku_resource_channel_ops_t *ops, void *host)
{
    tiku_resource_channel_t *channel;
    uid_t uid;
    pid_t pid = 0;
    if (!tiku_resource_id_valid(session) || !ops || !ops->authorize || !ops->dispatch ||
        (features & ~TIKU_RESOURCE_FEATURES)) return NULL;
    channel = open_channel(fd);
    if (!channel) return NULL;
#ifdef __linux__
    {
        struct ucred credentials;
        socklen_t size = sizeof credentials;
        if (getsockopt(channel->fd, SOL_SOCKET, SO_PEERCRED, &credentials, &size) || size != sizeof credentials) {
            close(channel->fd); free(channel); return NULL;
        }
        uid = credentials.uid; pid = credentials.pid;
    }
#elif defined(__APPLE__)
    {
        gid_t group;
        if (getpeereid(channel->fd, &uid, &group)) { close(channel->fd); free(channel); return NULL; }
    }
#else
    close(channel->fd); free(channel); return NULL;
#endif
    if (uid != geteuid()) { close(channel->fd); free(channel); return NULL; }
    if (!ops->authorize(host, uid, pid, &channel->caller) ||
        !tiku_resource_id_valid(channel->caller.principal) || channel->caller.scopes > INT64_MAX) {
        close(channel->fd); free(channel); return NULL;
    }
    channel->caller.authenticated = 1;
    channel->server = 1; channel->offered = features; channel->ops = *ops;
    channel->host = host;
    strcpy(channel->session, session);
    return channel;
}

int tiku_resource_channel_negotiate(tiku_resource_channel_t *channel, uint64_t required,
                                   uint64_t optional, int timeout)
{
    tiku_msg_t *request, *reply = NULL, *offer;
    int64_t features, scopes;
    int32_t major, minor, frame;
    const char *session, *principal, *op;
    int result;
    static const char *const allowed[] = {"op", "major", "minor", "features", "scopes", "session", "principal", "frame_max", "limits"};
    if (!usable(channel) || channel->server || channel->ready || required > INT64_MAX || optional > INT64_MAX) return -1;
    request = tiku_msg_new(TIKU_RESOURCE_MESSAGE_CODE);
    if (!request) return -1;
    offer = tiku_resource_protocol_limits_message(&channel->limits);
    result = tiku_msg_add_string(request, "op", "hello") &&
        tiku_msg_add_int32(request, "major", 1) && tiku_msg_add_int32(request, "minor", 0) &&
        tiku_msg_add_int64(request, "required_features", (int64_t)required) &&
        tiku_msg_add_int64(request, "optional_features", (int64_t)optional) && offer && tiku_msg_add_message(request,"limits",offer);
    tiku_msg_free(offer);
    if (!result || send_message(channel, request, timeout) || receive_message(channel, &reply, timeout)) {
        tiku_msg_free(request); tiku_msg_free(reply); return fail(channel);
    }
    tiku_msg_free(request);
    session = tiku_msg_find_string(reply, "session", 0);
    principal = tiku_msg_find_string(reply, "principal", 0);
    op = tiku_msg_find_string(reply, "op", 0);
    result = fields(reply, allowed, sizeof allowed / sizeof allowed[0]) && op && !strcmp(op, "hello") &&
        tiku_msg_find_int32(reply, "major", 0, &major) && major == 1 &&
        tiku_msg_find_int32(reply, "minor", 0, &minor) && minor == 0 &&
        tiku_msg_find_int32(reply, "frame_max", 0, &frame) && frame >= 4096 && frame <= (int32_t)channel->limits.frame_bytes &&
        tiku_msg_find_int64(reply, "features", 0, &features) && features >= 0 &&
        (((uint64_t)features & required) == required) && !((uint64_t)features & ~(required | optional)) &&
        tiku_msg_find_int64(reply, "scopes", 0, &scopes) && scopes >= 0 &&
        tiku_resource_id_valid(session) && tiku_resource_id_valid(principal);
    if (result && tiku_msg_count(reply,"limits")) {
        tiku_resource_protocol_limits_t selected;
        tiku_msg_t *record=tiku_msg_find_message(reply,"limits",0);
        result=tiku_resource_protocol_limits_decode(record,&selected);tiku_msg_free(record);
        if(result){
            result=tiku_resource_protocol_limits_contains(&channel->limits,&selected) && selected.frame_bytes==(unsigned)frame;
            if(result)channel->limits=selected;
        }
    } else if(result) {
        tiku_resource_protocol_limits_t legacy; tiku_resource_protocol_limits_default(&legacy);
        result=frame==TIKU_RESOURCE_FRAME_MAX && tiku_resource_protocol_limits_contains(&channel->limits,&legacy);
    }
    if (result) {
        strcpy(channel->session, session); strcpy(channel->caller.principal, principal);
        channel->caller.scopes = (uint64_t)scopes; channel->caller.authenticated = 1;
        channel->features = (uint64_t)features; channel->ready = 1;
    }
    tiku_msg_free(reply);
    return result ? 0 : fail(channel);
}

/** @brief Negotiate a server connection without invoking its resource callback. */
static int hello(tiku_resource_channel_t *channel, const tiku_msg_t *request, int timeout)
{
    static const char *const allowed[] = {"op", "major", "minor", "required_features", "optional_features", "limits"};
    int32_t major, minor;
    int64_t required, optional;
    const char *op = tiku_msg_find_string(request, "op", 0), *error = NULL;
    tiku_msg_t *reply;
    int ok;
    if (!fields(request, allowed, sizeof allowed / sizeof allowed[0]) || !op || strcmp(op, "hello") ||
        !tiku_msg_find_int32(request, "major", 0, &major) ||
        !tiku_msg_find_int32(request, "minor", 0, &minor) || minor < 0 ||
        !tiku_msg_find_int64(request, "required_features", 0, &required) || required < 0 ||
        !tiku_msg_find_int64(request, "optional_features", 0, &optional) || optional < 0) return fail(channel);
    if (tiku_msg_count(request,"limits")) {
        tiku_resource_protocol_limits_t peer;
        tiku_msg_t *offer=tiku_msg_find_message(request,"limits",0);
        ok=tiku_resource_protocol_limits_decode(offer,&peer);tiku_msg_free(offer);
        if(!ok)return fail(channel);
        tiku_resource_protocol_limits_merge(&channel->limits,&peer);
    }
    if (major != 1) error = "version_mismatch";
    else if ((uint64_t)required & ~channel->offered) error = "feature_unavailable";
    reply = tiku_msg_new(TIKU_RESOURCE_MESSAGE_CODE);
    if (!reply) return fail(channel);
    ok = tiku_msg_add_string(reply, "op", "hello");
    if (error) ok = ok && tiku_msg_add_string(reply, "error", error);
    else {
        channel->features = channel->offered & ((uint64_t)required | (uint64_t)optional);
        ok = ok && tiku_msg_add_int32(reply, "major", 1) && tiku_msg_add_int32(reply, "minor", 0) &&
            tiku_msg_add_int64(reply, "features", (int64_t)channel->features) &&
            tiku_msg_add_int64(reply, "scopes", (int64_t)channel->caller.scopes) &&
            tiku_msg_add_string(reply, "session", channel->session) &&
            tiku_msg_add_string(reply, "principal", channel->caller.principal) &&
            tiku_msg_add_int32(reply, "frame_max", (int32_t)channel->limits.frame_bytes);
        if(tiku_msg_count(request,"limits")){
            tiku_msg_t *selected=tiku_resource_protocol_limits_message(&channel->limits);
            ok=ok && selected && tiku_msg_add_message(reply,"limits",selected);tiku_msg_free(selected);
        }
    }
    ok = ok && !send_message(channel, reply, timeout);
    tiku_msg_free(reply);
    if (!ok || error) return fail(channel);
    channel->ready = 1;
    return 0;
}

int tiku_resource_channel_step(tiku_resource_channel_t *channel, int timeout)
{
    tiku_msg_t *request = NULL, *reply;
    const char *session, *id, *reply_id, *reply_session;
    int result;
    if (!usable(channel) || !channel->server || channel->active) return -1;
    channel->active = 1;
    if (receive_message(channel, &request, timeout)) { channel->active = 0; return -1; }
    if (!channel->ready) {
        result = hello(channel, request, timeout);
        tiku_msg_free(request); channel->active = 0; return result;
    }
    session = tiku_msg_find_string(request, "session", 0);
    id = tiku_msg_find_string(request, "request_id", 0);
    if (!session || strcmp(session, channel->session) || !tiku_resource_id_valid(id) ||
        tiku_msg_count(request, "session") != 1 || tiku_msg_count(request, "request_id") != 1) {
        tiku_msg_free(request); result = fail(channel); channel->active = 0; return result;
    }
    {
        tiku_resource_error_t error = tiku_resource_request_check(request, channel->features);
        if (error) {
            tiku_msg_t *details = tiku_msg_new(0);
            reply = tiku_msg_new(TIKU_RESOURCE_MESSAGE_CODE);
            result = details && reply && tiku_msg_add_string(details, "code", tiku_resource_error_name(error)) &&
                tiku_msg_add_string(details, "effect", "none") &&
                tiku_msg_add_string(reply, "request_id", id) && tiku_msg_add_string(reply, "session", session) &&
                tiku_msg_add_message(reply, "error", details) && !send_message(channel, reply, timeout);
            tiku_msg_free(details); tiku_msg_free(reply); tiku_msg_free(request);
            if (!result) fail(channel);
            channel->active = 0;
            return result ? 0 : -1;
        }
    }
    reply = channel->ops.dispatch(channel->host, &channel->caller, request);
    reply_id = tiku_msg_find_string(reply, "request_id", 0);
    reply_session = tiku_msg_find_string(reply, "session", 0);
    {
        static const char *const allowed[] = {"request_id", "session", "result", "error"};
        int has_result = tiku_msg_has(reply, "result", TIKU_MSG_T_MESSAGE);
        int has_error = tiku_msg_has(reply, "error", TIKU_MSG_T_MESSAGE);
        result = reply_id && !strcmp(id, reply_id) && reply_session && !strcmp(session, reply_session) &&
            fields(reply, allowed, sizeof allowed / sizeof allowed[0]) && (has_result != has_error) &&
            !send_message(channel, reply, timeout);
    }
    tiku_msg_free(request); tiku_msg_free(reply);
    if (!result) result = fail(channel);
    else result = 0;
    channel->active = 0;
    return result;
}

int tiku_resource_channel_send(tiku_resource_channel_t *channel, const tiku_msg_t *message, int timeout)
{
    if (!usable(channel) || !channel->ready || channel->active) return -1;
    return send_message(channel, message, timeout);
}

int tiku_resource_channel_receive(tiku_resource_channel_t *channel, tiku_msg_t **out, int timeout)
{
    if (out) *out = NULL;
    if (!usable(channel) || !channel->ready || channel->active) return -1;
    return receive_message(channel, out, timeout);
}
int tiku_resource_channel_wait(tiku_resource_channel_t *channel, int timeout)
{
    if (!usable(channel) || !channel->ready || timeout < 0 || timeout > 30000)
        return -1;
    struct pollfd fd = {.fd = channel->fd, .events = POLLIN};
    int n;
    do {
        n = poll(&fd, 1, timeout);
    } while (n < 0 && errno == EINTR);
    if (n < 0)
        return fail(channel);
    if (!n)
        return 0;
    int ready =
        tiku_resource_endpoint_ready(channel->fd, channel->limits.frame_bytes);
    return ready < 0 ? fail(channel) : ready;
}

uint64_t tiku_resource_channel_features(const tiku_resource_channel_t *channel)
{
    return usable(channel) && channel->ready ? channel->features : 0;
}

const char *tiku_resource_channel_session(const tiku_resource_channel_t *channel)
{
    return usable(channel) && channel->ready ? channel->session : NULL;
}

int tiku_resource_channel_caller(const tiku_resource_channel_t *channel, tiku_resource_caller_t *out)
{
    if (!out || !usable(channel) || !channel->ready) return -1;
    *out = channel->caller;
    return 0;
}

void tiku_resource_channel_free(tiku_resource_channel_t *channel)
{
    if (!channel || !pthread_equal(channel->thread, pthread_self()) || channel->active) return;
    channel->active = 1;
    fail(channel);
    free(channel);
}

int tiku_resource_channel_offer(tiku_resource_channel_t *channel,const tiku_resource_protocol_limits_t *limits)
{
    if(!usable(channel) || channel->ready || channel->active || !tiku_resource_protocol_limits_valid(limits))return -1;
    channel->limits=*limits;return 0;
}

int tiku_resource_channel_limits(const tiku_resource_channel_t *channel,tiku_resource_protocol_limits_t *out)
{
    if(!usable(channel) || !channel->ready || !out)return -1;
    *out=channel->limits;return 0;
}
