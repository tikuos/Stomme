/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_resource_channel.h - authenticated local resource message streams.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_RESOURCE_CHANNEL_H_
#define TIKU_RESOURCE_CHANNEL_H_
#include "tiku_resource_limits.h"
#include <sys/types.h>

/** @brief Local stream owned and serviced on its creating thread. */
typedef struct tiku_resource_channel tiku_resource_channel_t;

/** @brief Trusted host callbacks; peer UID/PID are supplied by the OS.
 *  @note authorize binds a launch capability or interactive policy to the peer.
 *        It fills principal/scopes and returns nonzero to accept. dispatch
 *        transfers reply ownership to the channel. closed runs once when the
 *        stream retires. */
typedef struct {
    int (*authorize)(void *host, uid_t uid, pid_t pid, tiku_resource_caller_t *out);
    tiku_msg_t *(*dispatch)(void *host, const tiku_resource_caller_t *, const tiku_msg_t *);
    void (*closed)(void *host, const tiku_resource_caller_t *);
} tiku_resource_channel_ops_t;

/** @brief Duplicate a connected Unix stream for a client; NULL on failure.
 *  @note The caller retains its original fd. No rendering or network fd is accepted. */
tiku_resource_channel_t *tiku_resource_channel_client(int fd);
/** @brief Authenticate a connected local peer and duplicate its stream.
 *  @note The host distributes the endpoint through its private launch/session
 *        channel and services this object on its registry's owner thread. */
tiku_resource_channel_t *tiku_resource_channel_server(int fd, const char *session,
    uint64_t features, const tiku_resource_channel_ops_t *, void *host);
/** @brief Set a valid offer on an unnegotiated owner-thread channel. */
int tiku_resource_channel_offer(tiku_resource_channel_t *, const tiku_resource_protocol_limits_t *);
/** @brief Copy selected limits after negotiation. */
int tiku_resource_channel_limits(const tiku_resource_channel_t *, tiku_resource_protocol_limits_t *);
/** @brief Negotiate version/features; zero on success, -1 after closing on error. */
int tiku_resource_channel_negotiate(tiku_resource_channel_t *, uint64_t required,
                                   uint64_t optional, int timeout_ms);
/** @brief Service one handshake/request and reply; zero on success, -1 on error.
 *  @note Dispatch begins after negotiation and session validation. */
int tiku_resource_channel_step(tiku_resource_channel_t *, int timeout_ms);
/** @brief Send a bounded message on a negotiated stream; zero on success. */
int tiku_resource_channel_send(tiku_resource_channel_t *, const tiku_msg_t *, int timeout_ms);
/** @brief Receive one complete message; output is NULL on error. */
int tiku_resource_channel_receive(tiku_resource_channel_t *, tiku_msg_t **out, int timeout_ms);
/** @brief Negotiated feature mask, or zero before negotiation. */
uint64_t tiku_resource_channel_features(const tiku_resource_channel_t *);
/** @brief Negotiated session identity, or NULL before negotiation. */
const char *tiku_resource_channel_session(const tiku_resource_channel_t *);
/** @brief Copy the authenticated grant on a negotiated owner-thread channel. */
int tiku_resource_channel_caller(const tiku_resource_channel_t *, tiku_resource_caller_t *out);
/** @brief Release the stream and its host-side connection context. */
void tiku_resource_channel_free(tiku_resource_channel_t *);

#endif
