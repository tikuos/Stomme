/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_link_serial.h - a link over a prompt-aware session's frames.
 *
 * The board's console carries typed text and, between 0xC0 delimiters after
 * a 0xF1 marker, the window session's messages, one per frame.  The console
 * has no flow control and the board loses bytes while it is busy, so this
 * link carries a CRC-16 on every frame and paces what it sends: a small
 * window of numbered frames the board acknowledges, retransmitted when an
 * acknowledgement is late, given up on and counted as lost after enough
 * tries.  A SYN opens the stream, so a board that has already talked to
 * another desktop starts again from zero.  It does not own the session.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_LINK_SERIAL_H_
#define TIKU_LINK_SERIAL_H_

#include "tiku_link.h"
#include "tiku_session.h"

/*
 * The frame inside the channel, shared with the kernel's
 * services/link/tiku_link_console.h:
 *
 *   +0  ctl      bits 0-1 the kind, bit 7 WANT_ACK, the other bits zero
 *   +1  seq      DATA and SYN: this frame's number; ACK: the last delivered
 *   +2  payload  DATA only
 *   -2  crc      CRC-16/CCITT-FALSE over ctl..payload, high byte first
 */
#define TIKU_LINK_SERIAL_OVERHEAD  4u
#define TIKU_LINK_SERIAL_DATA      0x00u
#define TIKU_LINK_SERIAL_ACK       0x01u
#define TIKU_LINK_SERIAL_SYN       0x02u
#define TIKU_LINK_SERIAL_KIND      0x03u
#define TIKU_LINK_SERIAL_WANT_ACK  0x80u

/** @brief The largest message the board's session channel takes; the
 *         kernel's TIKU_LINK_CONSOLE_SESSION_MTU. */
#define TIKU_LINK_SERIAL_MTU       256u

/** @brief Frames in flight before a send waits; what a busy board may
 *         hold in its receive ring at once. */
#define TIKU_LINK_SERIAL_WINDOW    2

/** @brief Messages queued behind the window before a send is refused. */
#define TIKU_LINK_SERIAL_PENDING   32

/** @brief How long an acknowledgement may take before the window is sent
 *         again; longer than the board's largest frame takes on the wire. */
#define TIKU_LINK_SERIAL_RETRY_MS  500

/** @brief Retransmissions of the window before its frames count as lost. */
#define TIKU_LINK_SERIAL_RETRIES   4

/** @brief How far behind the expected seq a paced frame from the peer is
 *         still a repeat of one delivered rather than a stranger. */
#define TIKU_LINK_SERIAL_DUP_SPAN  8u

/** @brief The link's counters since it opened. */
typedef struct {
    unsigned long rx;          /**< messages delivered */
    unsigned long tx;          /**< DATA and SYN frames sent the first time */
    unsigned long bad;         /**< frames dropped: short, or a wrong CRC */
    unsigned long dup;         /**< paced frames seen again: answered only */
    unsigned long reject;      /**< paced frames out of order: dropped */
    unsigned long acked;       /**< frames of this link's own the board took */
    unsigned long answered;    /**< ACKs this link sent for a peer's frames */
    unsigned long gap;         /**< board frames missing before one that came */
    unsigned long retransmit;  /**< frames sent again */
    unsigned long lost;        /**< messages given up on */
    unsigned long refused;     /**< sends refused: too large, or a dead link */
    unsigned long faults;      /**< times the link gave up or saw a gap */
} tiku_link_serial_stats_t;

/** @brief Open a serial link on @p s; a SYN goes out at once.
 *         @return the link, or NULL. */
tiku_link_t *tiku_link_serial_open(tiku_session_t *s);

/** @brief The counters of a serial link, or NULL for another kind. */
const tiku_link_serial_stats_t *tiku_link_serial_stats(const tiku_link_t *l);

/** @brief CRC-16/CCITT-FALSE continued from @p crc; start it at 0xFFFF. */
unsigned tiku_link_serial_crc(unsigned crc, const void *bytes, size_t len);

#endif /* TIKU_LINK_SERIAL_H_ */
