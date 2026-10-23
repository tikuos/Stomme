/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_channel.h - a board's window session over its console.
 *
 * The desktop's window session speaks over a file descriptor.  A channel
 * gives it one: a socket pair, one end for the desktop's listener to
 * adopt, the other fed by the messages a link carries from the board and
 * drained into messages the link carries to it, as fast as the link will
 * take them.  A loss the link reports discards the backlog, stale by
 * then, and greets the board afresh; a greeting it leaves unanswered for
 * two seconds is said again, twice, then only every ten seconds.  A
 * channel runs over a session the shell owns, or over a port of its own
 * for a board built without a shell.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_CHANNEL_H_
#define TIKU_CHANNEL_H_

#include "tiku_link.h"
#include "tiku_session.h"
#include "tiku_tx.h"

typedef struct tiku_channel tiku_channel_t;

/** @brief The features bit that marks a WELCOME as an invitation. */
#define TIKU_CHANNEL_INVITE 0x80000000u

/** @brief Open a channel on @p s: a serial link is opened over the session
 *         and its messages flow to the channel.  NULL when out of memory
 *         or sockets. */
tiku_channel_t *tiku_channel_open(tiku_session_t *s);

/**
 * @brief Open a channel on @p link, whatever medium it is: the channel
 *        owns it from here and closes it with itself.  NULL when out of
 *        memory or sockets, and @p link is closed in that case.
 */
tiku_channel_t *tiku_channel_adopt(tiku_link_t *link);

/**
 * @brief Open a channel on a port of its own, for a board with windows
 *        and no shell: the transport and the session are the channel's,
 *        polled from its pump, reopened after a replug, closed with it.
 *        NULL when the port will not open.
 */
tiku_channel_t *tiku_channel_open_port(const char *port, int baud);

/**
 * @brief Whether a board is behind a channel opened on a port of its own:
 *        pumps until the link's opening SYN is acknowledged, or given up
 *        on after its retries, for at most @p timeout_ms.  @return 1 for
 *        a board that answered, 0 for a port with nothing there; a
 *        channel on a session the caller owns, or on a link other than a
 *        serial one, is taken as answered.
 */
int tiku_channel_probe(tiku_channel_t *ch, int timeout_ms);

/**
 * @brief Tell the board a desktop is listening: a WELCOME carrying the
 *        invitation bit (TIKU_CHANNEL_INVITE in its features), which a
 *        board answers with a HELLO and by opening its windows again.
 */
void tiku_channel_greet(tiku_channel_t *ch);

/** @brief The descriptor for the desktop's listener to adopt. */
int tiku_channel_fd(const tiku_channel_t *ch);

/** @brief The link under the channel, for its counters. */
tiku_link_t *tiku_channel_link(const tiku_channel_t *ch);

/**
 * @brief The board's shell over the same link: a transport whose bytes
 *        out go as SHELL_IN messages and whose bytes back are what the
 *        board's shell wrote, so a session and a namespace can run on a
 *        board reached only over the link.  One per channel, opened on the
 *        thread that pumps it: a read there services the link, one on a
 *        worker waits for that thread's pump.  The transport is the
 *        caller's to close, and reads an end once the channel is closed.
 *        NULL when out of sockets.
 */
tiku_tx_t *tiku_channel_shell_tx(tiku_channel_t *ch);

/**
 * @brief Send text to the board's interpreter, as BASIC_IN messages.
 *
 * The interpreter's stream is separate from the shell's, so the board's
 * namespace answers while a program runs.  Waits up to two seconds for
 * the link to take each message.
 * @return the bytes taken, or -1.
 */
int tiku_channel_basic_send(tiku_channel_t *ch, const void *bytes, size_t len);

/**
 * @brief Take what a program on the board has printed, up to @p max.
 * @return the bytes written into @p buf; 0 when it has said nothing.
 */
int tiku_channel_basic_recv(tiku_channel_t *ch, void *buf, size_t max);

/** @brief How many losses the link has reported since the channel
 *         opened, counting a listener whose stream was shut for not
 *         reading. */
int tiku_channel_losses(const tiku_channel_t *ch);

/**
 * @brief Service the link, then send what the desktop wrote to its end:
 *        one whole message per call, pulled from the socket only when
 *        the link would take it now; pointer moves queued behind a newer
 *        message are dropped, and one the link refuses is dropped too.
 *        Greets again after a loss the link reports, the backlog
 *        discarded, and after a greeting the board has not answered.
 *        @return how many went out, 0 or 1.
 */
int tiku_channel_pump(tiku_channel_t *ch);

/** @brief Close both ends; the session stops routing frames, and one the
 *         channel owns is closed with it. */
void tiku_channel_close(tiku_channel_t *ch);

#endif /* TIKU_CHANNEL_H_ */
