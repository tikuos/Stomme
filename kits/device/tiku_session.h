/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_session.h - prompt-aware conversation with one device.
 *
 * Sends a command, returns what the device printed before the next prompt, and
 * routes asynchronous `~<path>` subscription lines to a callback wherever they
 * land in the stream.  Frames for a window session share the line and are
 * cut out of it first.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_SESSION_H_
#define TIKU_SESSION_H_

#include <stddef.h>
#include "tiku_tx.h"

/** @brief A conversation with one device over one transport. */
typedef struct tiku_session tiku_session_t;

/**
 * @brief Called for every `~<path>` line the device emits.
 *
 * @note Called from inside a session call: a handler must not re-enter
 *       the session, but queue the path and re-read it after the current
 *       command returns.
 */
typedef void (*tiku_notify_fn)(const char *path, void *ctx);

/**
 * @brief The board's text, handed over as it arrives.
 *
 * What a client that owns the line reads: bytes as the board wrote them,
 * with ANSI escape sequences taken out and no prompt framing; `~` lines
 * still go to the notify callback.
 */
typedef void (*tiku_text_fn)(const unsigned char *bytes, size_t n,
                             void *ctx);

/** @brief Wrap an open transport.  Takes ownership of @p tx. */
tiku_session_t *tiku_session_new(tiku_tx_t *tx);

/** @brief Close the transport and free the session. */
void tiku_session_free(tiku_session_t *s);

/** @brief Install the subscription-line handler (NULL to drop them). */
void tiku_session_on_notify(tiku_session_t *s,
                                 tiku_notify_fn fn, void *ctx);

/**
 * @brief Discard pending bytes and wait for a prompt, so the next command
 *        starts from a known state.  0 on success.
 */
int tiku_session_sync(tiku_session_t *s, int timeout_ms);

/**
 * @brief Run one command line and capture its output.
 *
 * Echo and the trailing prompt are removed; `~` lines are routed to the
 * notify callback rather than into @p out.
 *
 * @param out  Buffer for the reply; always NUL-terminated when non-NULL.
 * @return Bytes placed in @p out, or -1 on timeout or a broken link.
 */
int tiku_session_cmd(tiku_session_t *s, const char *line,
                          char *out, size_t max, int timeout_ms);

/**
 * @brief Service the link without sending anything, so subscription lines
 *        arrive while the UI is idle.
 *
 * @return Number of `~` lines dispatched, or -1 on a broken link.
 */
int tiku_session_poll(tiku_session_t *s, int timeout_ms);

/** @brief Reopen the transport and resynchronise.  0 on success. */
int tiku_session_reconnect(tiku_session_t *s);

/** @brief Reopen the transport alone, for a board with no prompt to
 *         resynchronise on; what was half read is dropped.  0 on success. */
int tiku_session_reopen(tiku_session_t *s);

/**
 * @brief The largest frame, unescaped: a larger one coming in is dropped
 *        whole, and one going out is refused.
 *
 * A board's console carries typed text and, between 0xC0 delimiters
 * after a 0xF1 marker, binary frames escaped the SLIP way; the frames are
 * cut out of the stream before any text is looked at, so a shell and a
 * window session share one cable.
 */
#define TIKU_SESSION_FRAME_MAX 4096

/** @brief Called with each whole frame's unescaped bytes. */
typedef void (*tiku_frame_fn)(const unsigned char *bytes, size_t len,
                              void *ctx);

/** @brief Route frames to @p fn; NULL drops them. */
void tiku_session_on_frame(tiku_session_t *s, tiku_frame_fn fn, void *ctx);

/**
 * @brief Hand the board's text to @p fn as it arrives, instead of keeping
 *        it for the next command's reply.  NULL gives it back.
 *
 * Frames are untouched: a board whose windows share the line goes on
 * drawing while a program prints.
 * @note While @p fn is set a command's reply goes to it, so a caller
 *       takes the line, uses it, and gives it back before
 *       tiku_session_cmd().
 */
void tiku_session_on_text(tiku_session_t *s, tiku_text_fn fn, void *ctx);

/**
 * @brief Write bytes exactly as given: no newline added, no prompt waited
 *        for, no timeout.  For the client that owns the line.
 * @return 0, or -1 when the transport refused them.
 */
int tiku_session_send_raw(tiku_session_t *s, const void *bytes, size_t n);

/**
 * @brief Read and discard until the board has been quiet for @p quiet_ms.
 *
 * @note A caller handing the line back calls this.  tiku_session_sync()
 *       alone may take a prompt already on its way from the last client's
 *       traffic as its own, leaving the next command one prompt out of
 *       step.
 */
void tiku_session_drain(tiku_session_t *s, int quiet_ms);

/** @brief Send @p len bytes, at most TIKU_SESSION_FRAME_MAX, as one frame.
 *         @return 0, or -1. */
int tiku_session_send_frame(tiku_session_t *s, const void *bytes,
                            size_t len);

/** @brief Endpoint name, for titles and logs. */
const char *tiku_session_name(const tiku_session_t *s);

#endif /* TIKU_SESSION_H_ */
