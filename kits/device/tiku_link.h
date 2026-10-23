/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_link.h - a link: whole messages to and from one board, on any medium.
 *
 * The desktop's mirror of the kernel's link.  A channel adopts a link and
 * does not know whether it is a serial cable, a socket or a radio: the
 * serial link (tiku_link_serial.h) runs over a session's frames, the TCP
 * link (tiku_link_tcp.h) over a socket a board dialled.  A message arrives
 * whole and intact or not at all; a link that loses one says so through
 * lost(), and the channel above greets the board again.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_LINK_H_
#define TIKU_LINK_H_

#include <stddef.h>

typedef struct tiku_link tiku_link_t;

/** @brief One whole message from the board, its escapes already undone. */
typedef void (*tiku_link_msg_fn)(const unsigned char *msg, size_t len,
                                 void *ctx);

/**
 * @brief What a backend supplies.  ready, pump, lost and mtu may be NULL:
 *        a medium that never waits, needs no timers, loses nothing and
 *        takes a message of any size.
 */
typedef struct {
    int    (*send)(tiku_link_t *l, const void *msg, size_t len);
    int    (*ready)(tiku_link_t *l);   /**< a send would go out now */
    void   (*pump)(tiku_link_t *l);    /**< timers, retransmits */
    int    (*lost)(tiku_link_t *l);    /**< faults since last asked */
    size_t (*mtu)(tiku_link_t *l);     /**< the largest message taken */
    void   (*close)(tiku_link_t *l);
} tiku_link_ops_t;

/** @brief A link.  A backend fills ops and ctx; a consumer installs recv. */
struct tiku_link {
    const tiku_link_ops_t *ops;
    void                  *ctx;
    tiku_link_msg_fn       recv;
    void                  *recv_ctx;
};

/**
 * @brief Send one whole message: out now, or queued behind what is in
 *        flight.  @return 0 when accepted, -1 when the link refuses it:
 *        it is broken, or the message is larger than the peer takes.
 */
int  tiku_link_send(tiku_link_t *l, const void *msg, size_t len);

/** @brief The largest message the peer takes, or 0 for no limit. */
size_t tiku_link_mtu(tiku_link_t *l);

/** @brief Whether a send would go out now rather than wait. */
int  tiku_link_ready(tiku_link_t *l);

/** @brief Service the medium: retransmit timers and the like. */
void tiku_link_pump(tiku_link_t *l);

/**
 * @brief How many faults the link met since last asked: a window it gave
 *        up on, or a gap in the board's stream.  Anything above zero means
 *        the board's picture of the session may be stale and a greeting is
 *        due.
 */
int  tiku_link_lost(tiku_link_t *l);

/** @brief Where whole messages from the board go; NULL drops them. */
void tiku_link_on_message(tiku_link_t *l, tiku_link_msg_fn fn, void *ctx);

/** @brief For a backend: hand a whole message to the consumer, if any. */
void tiku_link_deliver(tiku_link_t *l, const unsigned char *msg, size_t len);

/** @brief Release the backend; the link delivers nothing after this. */
void tiku_link_close(tiku_link_t *l);

#endif /* TIKU_LINK_H_ */
