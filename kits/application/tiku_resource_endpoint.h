/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_resource_endpoint.h - private local endpoint ownership and SDK connection.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_RESOURCE_ENDPOINT_H_
#define TIKU_RESOURCE_ENDPOINT_H_
#include "tiku_resource_channel.h"
/** @brief An owner-thread listener in a user-owned 0700 directory. */
typedef struct tiku_resource_endpoint tiku_resource_endpoint_t;
/** @brief Bind control.sock without replacing existing files or endpoints. */
tiku_resource_endpoint_t *tiku_resource_endpoint_open(const char *directory);
/** @brief Remove a same-user 0600 socket with no listener under its directory lock.
 *  @return 0 when absent or removed; -1 for active, replaced or untrusted paths.
 *  @note Explicit recovery; ordinary open preserves preexisting endpoint paths. */
int tiku_resource_endpoint_recover(const char *directory);
/** @brief Accept a nonblocking descriptor, or -1 when empty/error; caller closes. */
int tiku_resource_endpoint_accept(tiku_resource_endpoint_t *);
/** @brief Retire the owned socket; retain replaced paths and preexisting directories. */
void tiku_resource_endpoint_close(tiku_resource_endpoint_t *);
/** @brief Connect to a private same-user endpoint within a bounded timeout. */
tiku_resource_channel_t *tiku_resource_endpoint_connect(const char *directory, int timeout_ms);
/** @brief Whether one bounded frame is buffered: 1 ready, 0 partial, -1 retired. */
int tiku_resource_endpoint_ready(int fd, unsigned frame_max);
#endif
