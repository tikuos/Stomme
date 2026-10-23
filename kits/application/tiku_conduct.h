/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_conduct.h - the conductor channel: driving a desktop from
 * outside it.
 *
 * The desktop offers a small automation surface: INJECT one input event,
 * QUERY one pixel or one named string, and watch a named string for
 * changes.  The driver is an ordinary process on the far end of a stream,
 * and scripts, waits and expectations live in it.  The channel is off
 * unless the desktop is started with -conduct or -conduct-tty, because
 * whatever reaches it can drive the desktop.  The framing is
 * [u32 type][u32 length][payload], little-endian, over any stream;
 * tiku_conduct.md beside this file gives the reasoning.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_CONDUCT_H_
#define TIKU_CONDUCT_H_

#include <stddef.h>
#include <stdint.h>

#include "tiku_event.h"

/** @brief The channel version: the shell closes a driver whose HELLO names
 *         another, so a driver built for other frame sizes is refused. */
#define TIKU_CONDUCT_VERSION 2
/** @brief A query's argument, or a told fact's name, NUL included. */
#define TIKU_CONDUCT_ARG     64
/** @brief An answer's text, NUL included: room for a window's narration. */
#define TIKU_CONDUCT_TEXT    2048
/** @brief How long tiku_conduct_query() waits for its answer before
 *         failing. */
#define TIKU_CONDUCT_ANSWER_MS 4000

/* Driver to shell. */
#define TIKU_CMSG_HELLO   1u   /* u32 version                        */
#define TIKU_CMSG_INJECT  2u   /* tiku_event_t                  */
#define TIKU_CMSG_QUERY   3u   /* u32 what, i32 a, i32 b, arg[64]    */
/* Shell to driver. */
#define TIKU_CMSG_ANSWER  16u  /* i32 ok, i32 num, text[]            */
#define TIKU_CMSG_TOLD    17u  /* arg[64] names it, text[] says it   */

/** @brief What a QUERY asks for. */
#define TIKU_CQ_PIXEL     1u   /* a, b are x, y; the answer is num   */
#define TIKU_CQ_TEXT      2u   /* arg names it; the answer is text   */

/** @brief How many named facts one driver may watch at once; each costs
 *         the shell an evaluation per TIKU_CONDUCT_TELL_MS, a full
 *         content draw for "narrate". */
#define TIKU_CONDUCT_SUBS     4
/** @brief How often the shell looks for a change in a watched fact. */
#define TIKU_CONDUCT_TELL_MS  100

/**
 * @brief A HELLO's token, after its version, NUL included.
 *
 * When the shell requires tokens (tiku_conduct_require()), the token
 * names the peer and decides its grant, what the session may do, before
 * anything else is served.  A query grant asks and watches but cannot
 * inject.
 */
#define TIKU_CONDUCT_TOKEN       64
#define TIKU_CONDUCT_GRANT_NONE   0   /* nothing until a named HELLO   */
#define TIKU_CONDUCT_GRANT_QUERY  1   /* ask and watch, never inject   */
#define TIKU_CONDUCT_GRANT_FULL   2   /* the driver's full surface     */

/**
 * @brief What the shell answers a named text query with.
 *
 * The shell supplies this; the channel knows no names of its own.
 *
 * @return nonzero when @p name was known and @p out was filled.
 * @note A name the shell does not know is answered 0.
 */
typedef int (*tiku_conduct_text_fn)(void *ctx, const char *name,
                                         char *out, size_t max);

/** @brief What the shell reads a pixel with. */
typedef unsigned (*tiku_conduct_pixel_fn)(void *ctx, int x, int y);

/** @brief Where the shell delivers an injected event. */
typedef void (*tiku_conduct_inject_fn)(void *ctx,
                                            const tiku_event_t *event);

/** @brief The shell's end of the channel: the listening socket, the one
 *         driver, and what that driver watches and may do. */
typedef struct {
    int                         fd;         /* listening, or -1           */
    int                         peer;       /* the one driver, or -1      */
    char                        path[108];
    void                       *ctx;
    tiku_conduct_text_fn   text;
    tiku_conduct_pixel_fn  pixel;
    tiku_conduct_inject_fn inject;
    /* The stream has no peek: a header is gathered across reads. */
    unsigned char               hbuf[8];
    size_t                      hgot;
    unsigned char              *buf;
    size_t                      got, want;
    uint32_t                    cur_type;
    /* What the one driver asked to be told about: QUERY "~name" answers
     * with the fact's current text and registers it here; a change is
     * then pushed as a TOLD frame on the same stream.  Watches end with
     * the peer. */
    char                        sub_name[TIKU_CONDUCT_SUBS]
                                        [TIKU_CONDUCT_ARG];
    uint32_t                    sub_sig[TIKU_CONDUCT_SUBS];
    int                         subs;
    int64_t                     told_at_us;
    /* Who the peer is and what it may do.  Without required tokens every
     * peer drives fully from the moment it connects, HELLO or not; with
     * them a session may do nothing until its HELLO names it. */
    int                         required;
    char                        token_full[TIKU_CONDUCT_TOKEN];
    char                        token_query[TIKU_CONDUCT_TOKEN];
    int                         grant;
    char                        principal[TIKU_CONDUCT_TOKEN];
    /* The session's INJECTs and QUERYs: past a quota other than 0 the
     * peer is closed. */
    long                        quota;
    long                        spent;
} tiku_conduct_t;

/**
 * @brief Listen for a driver on the local socket @p path, making the
 *        directories on the way.
 *
 * @return 0, or -1.
 * @note A shell not started with -conduct never calls this, so nothing
 *       can drive it.
 */
int tiku_conduct_listen(tiku_conduct_t *c, const char *path,
                             void *ctx, tiku_conduct_inject_fn inject,
                             tiku_conduct_pixel_fn pixel,
                             tiku_conduct_text_fn text);

/**
 * @brief Serve the channel over @p fd, already connected to a driver: a
 *        serial line, a pty or a socket.
 *
 * Nothing listens or accepts: the line is the one peer, with the full
 * grant.
 * @return 0, or -1 for a bad @p fd.
 */
int tiku_conduct_adopt(tiku_conduct_t *c, int fd, void *ctx,
                            tiku_conduct_inject_fn inject,
                            tiku_conduct_pixel_fn pixel,
                            tiku_conduct_text_fn text);

/**
 * @brief Open @p path as a raw serial line at @p baud.
 *
 * Raw, because the channel's bytes are not text and a line discipline
 * that translates a carriage return corrupts a frame.  @p baud of 0
 * leaves the port's own speed alone, which is what a pty wants.
 *
 * @return the fd, or -1.
 */
int tiku_conduct_open_tty(const char *path, int baud);

/** @brief Close the driver's connection, and the listening socket with its
 *         path. */
void tiku_conduct_shutdown(tiku_conduct_t *c);

/**
 * @brief Require a named HELLO: @p full_token drives, @p query_token
 *        asks and watches but cannot inject; either may be NULL to offer
 *        no such grant.
 *
 * Once required, an unnamed or wrongly named peer is closed at its
 * HELLO, as a version mismatch is, and a peer already connected may do
 * nothing until it names itself.
 */
void tiku_conduct_require(tiku_conduct_t *c, const char *full_token,
                               const char *query_token);

/**
 * @brief Cap what one session may do: after @p messages INJECTs and
 *        QUERYs together, the peer is closed.  0 lifts the cap; either
 *        way the count starts again.
 */
void tiku_conduct_quota(tiku_conduct_t *c, long messages);

/**
 * @brief Accept and serve whatever is ready, without blocking, then tell
 *        the driver of watched facts that changed.
 *
 * Injected events reach the shell through the inject callback during this
 * call, so they land where the shell's own events do.  A reply to a driver
 * whose socket stays full waits up to two seconds, then the driver is
 * closed.
 *
 * @return nonzero when anything was injected and a repaint may be owed.
 */
int tiku_conduct_poll(tiku_conduct_t *c);

/*---------------------------------------------------------------------------*/
/* THE DRIVER'S SIDE                                                         */
/*---------------------------------------------------------------------------*/

/** @brief The driver's end of the channel.  A frame read in part when a
 *         wait ends is kept here, and the next wait reads on from it. */
typedef struct {
    int            fd;
    unsigned char  hbuf[8];
    size_t         hgot;
    unsigned char *buf;
    size_t         got, want;
    uint32_t       cur_type;
    struct tiku_conduct_notice *pending_head, *pending_tail;
    unsigned       pending_count;
} tiku_conduct_client_t;

/** @brief Connect to the shell's socket at @p path, retrying every 20 ms
 *         for up to @p wait_ms, and send an unnamed HELLO.
 *         @return 0, or -1. */
int tiku_conduct_connect(tiku_conduct_client_t *c,
                              const char *path, int wait_ms);

/** @brief Drive over @p fd, already connected to a shell, sending an
 *         unnamed HELLO.  @return 0, or -1 for a bad @p fd. */
int tiku_conduct_connect_fd(tiku_conduct_client_t *c, int fd);

/** @brief As tiku_conduct_connect(), the HELLO offering @p token as the
 *         session's name; NULL offers none. */
int tiku_conduct_connect_as(tiku_conduct_client_t *c,
                                 const char *path, int wait_ms,
                                 const char *token);
/** @brief As tiku_conduct_connect_fd(), the HELLO offering @p token as the
 *         session's name; NULL offers none. */
int tiku_conduct_connect_fd_as(tiku_conduct_client_t *c, int fd,
                                    const char *token);

/** @brief Close the driver's connection. */
void tiku_conduct_disconnect(tiku_conduct_client_t *c);

/** @brief Deliver one input event to the shell.
 *  @return 0, or -1 when not connected. */
int tiku_conduct_inject(tiku_conduct_client_t *c,
                             const tiku_event_t *event);

/**
 * @brief Ask, and block up to TIKU_CONDUCT_ANSWER_MS for the answer.
 *
 * @return nonzero when the shell answered and knew the question.
 * @note Any other frame that arrives meanwhile, a TOLD included, is
 *       dropped.
 */
int tiku_conduct_query(tiku_conduct_client_t *c, uint32_t what,
                            int a, int b, const char *arg,
                            int *num, char *text, size_t max);

/**
 * @brief Wait to be told: block until the shell pushes a watched fact's
 *        new value, or @p wait_ms runs out.
 *
 * A QUERY of "~name" sets the watch; the shell then sends a TOLD whenever
 * the fact's text changes.
 *
 * @return nonzero when a TOLD arrived; @p name and @p text then say
 *         which fact and what it says now.
 */
int tiku_conduct_told(tiku_conduct_client_t *c,
                           char *name, size_t name_max,
                           char *text, size_t text_max, int wait_ms);

#endif /* TIKU_CONDUCT_H_ */
