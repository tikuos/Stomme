/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_link_tcp.h - a link over a socket a board dialled.
 *
 * The desktop listens; a board with an address dials it and its window
 * session rides the connection.  A message is its 32-bit length followed
 * by its bytes, which TCP delivers whole and in order.  The link owns the
 * socket it is given.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_LINK_TCP_H_
#define TIKU_LINK_TCP_H_

#include "tiku_link.h"
#include "tiku_session.h"           /* the largest message a board sends */

/** @brief The port a desktop listens on for a board's window session. */
#define TIKU_LINK_TCP_PORT 7749

/** @brief The message's length in front of it, 32-bit little-endian. */
#define TIKU_LINK_TCP_HEADER 4u

/** @brief The largest message a board takes; its own buffer is this. */
#define TIKU_LINK_TCP_MTU 256u

/**
 * @brief The largest message a board sends.
 *
 * A board sends display lists larger than any message it takes, so this
 * is larger than TIKU_LINK_TCP_MTU.
 */
#define TIKU_LINK_TCP_RECV_MAX TIKU_SESSION_FRAME_MAX

/** @brief A link's counters since it opened. */
typedef struct {
    unsigned long rx;        /**< messages delivered */
    unsigned long tx;        /**< messages sent */
    unsigned long refused;   /**< sends refused: too large, or gone */
    unsigned long oversize;  /**< messages too large to take, skipped */
    unsigned long faults;    /**< times the connection was found gone */
} tiku_link_tcp_stats_t;

/**
 * @brief Open a link on the connected socket @p fd, which it then owns.
 *
 * @return the link, or NULL
 */
tiku_link_t *tiku_link_tcp_open(int fd);

/** @brief Listen on @p port for boards dialling in.  @return the socket,
 *         or -1; the caller accepts on it and closes it. */
int tiku_link_tcp_listen(int port);

/** @brief The counters of a TCP link, or NULL for another kind. */
const tiku_link_tcp_stats_t *tiku_link_tcp_stats(const tiku_link_t *l);

#endif /* TIKU_LINK_TCP_H_ */
