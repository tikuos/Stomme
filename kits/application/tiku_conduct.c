/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_conduct.c - the conductor channel, the shell's end and the
 * driver's.
 *
 * The framing is the window session's ([u32 type][u32 length][payload]),
 * one wire format for both channels, over a local socket or a serial line
 * (tiku_conduct_open_tty(), tiku_conduct_adopt()).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_conduct.h"
#include "tiku_sys.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

/** @brief The largest payload either end accepts; a longer length ends
 *         the stream. */
#define CONDUCT_MAX_PAYLOAD (64u * 1024u)
/** @brief How long, in ms, a write waits on a full socket before the peer
 *         is closed, and the step it waits in. */
#define CONDUCT_STALL_MS 2000
#define CONDUCT_STEP_MS    50

/** @brief Make @p fd nonblocking, and ignore SIGPIPE: a write to a peer
 *         that has gone then fails with EPIPE.  @return 0, or -1. */
static int
set_nonblock(int fd)
{
    int flags;

    (void)signal(SIGPIPE, SIG_IGN);
    flags = fcntl(fd, F_GETFL, 0);
    return (flags >= 0) ? fcntl(fd, F_SETFL, flags | O_NONBLOCK) : -1;
}

/**
 * @brief Write all of @p data to @p *fd, waiting while the socket is full.
 *
 * A peer that takes nothing for CONDUCT_STALL_MS, or a write error, closes
 * the descriptor and sets @p *fd to -1: the rest of a message cannot be
 * sent later without the far end reading it as a header.
 */
static void
send_all(int *fd, const void *data, size_t n)
{
    const unsigned char *p = data;
    int waited = 0;

    while (n > 0u && *fd >= 0) {
        ssize_t wrote = write(*fd, p, n);

        if (wrote > 0) {
            p += wrote;
            n -= (size_t)wrote;
            waited = 0;
            continue;
        }
        if (wrote < 0 && errno == EINTR) {
            continue;
        }
        if (wrote < 0 && (errno == EAGAIN || errno == EWOULDBLOCK) &&
            waited < CONDUCT_STALL_MS) {
            struct pollfd one = { *fd, POLLOUT, 0 };

            (void)poll(&one, 1u, CONDUCT_STEP_MS);
            waited += CONDUCT_STEP_MS;
            continue;
        }
        (void)close(*fd);
        *fd = -1;
    }
}

/** @brief Write one message: its type and length, then @p n bytes of
 *         @p payload.  A stalled peer is closed, as send_all() says. */
static void
send_msg(int *fd, uint32_t type, const void *payload, uint32_t n)
{
    uint32_t head[2];

    head[0] = type;
    head[1] = n;
    send_all(fd, head, sizeof head);
    if (n > 0u && payload != NULL) {
        send_all(fd, payload, n);
    }
}

/**
 * @brief Read one whole message into the buffer, without blocking.
 *
 * @return 1 when one is complete, 0 when not yet, -1 when the peer left.
 */
static int
stream_read(int fd, unsigned char *hbuf, size_t *hgot, unsigned char **buf,
            size_t *got, size_t *want, uint32_t *cur_type)
{
    if (*want == 0u) {
        while (*hgot < 8u) {
            ssize_t n = read(fd, hbuf + *hgot, 8u - *hgot);

            if (n > 0) {
                *hgot += (size_t)n;
                continue;
            }
            if (n == 0) {
                return -1;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return 0;
            }
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        {
            uint32_t head[2];

            memcpy(head, hbuf, sizeof head);
            *hgot = 0u;
            if (head[1] > CONDUCT_MAX_PAYLOAD) {
                return -1;      /* past CONDUCT_MAX_PAYLOAD: end it */
            }
            *cur_type = head[0];
            *want = head[1];
            *got = 0u;
            free(*buf);
            *buf = (*want > 0u) ? malloc(*want) : NULL;
            if (*want > 0u && *buf == NULL) {
                return -1;
            }
        }
    }
    while (*got < *want) {
        ssize_t n = read(fd, *buf + *got, *want - *got);

        if (n > 0) {
            *got += (size_t)n;
            continue;
        }
        if (n == 0) {
            return -1;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return 0;
        }
        if (errno == EINTR) {
            continue;
        }
        return -1;
    }
    return 1;
}

/*---------------------------------------------------------------------------*/
/* THE SHELL'S SIDE                                                          */
/*---------------------------------------------------------------------------*/

int
tiku_conduct_listen(tiku_conduct_t *c, const char *path,
                         void *ctx, tiku_conduct_inject_fn inject,
                         tiku_conduct_pixel_fn pixel,
                         tiku_conduct_text_fn text)
{
    struct sockaddr_un addr;
    char dir[108];
    char *slash;

    if (c == NULL || path == NULL) {
        return -1;
    }
    memset(c, 0, sizeof *c);
    c->fd = -1;
    c->peer = -1;
    c->grant = TIKU_CONDUCT_GRANT_FULL;
    snprintf(c->principal, sizeof c->principal, "unnamed");
    c->ctx = ctx;
    c->inject = inject;
    c->pixel = pixel;
    c->text = text;
    if ((size_t)snprintf(c->path, sizeof c->path, "%s", path) >=
        sizeof c->path) {
        return -1;
    }
    /* Make every directory on the way, as tiku_remote_listen() does. */
    snprintf(dir, sizeof dir, "%s", c->path);
    slash = strrchr(dir, '/');
    if (slash != NULL) {
        char *step;

        *slash = '\0';
        for (step = strchr(dir + 1, '/'); step != NULL;
             step = strchr(step + 1, '/')) {
            *step = '\0';
            (void)mkdir(dir, 0700);
            *step = '/';
        }
        (void)mkdir(dir, 0700);
    }
    (void)unlink(c->path);
    c->fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (c->fd < 0) {
        return -1;
    }
    memset(&addr, 0, sizeof addr);
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof addr.sun_path, "%s", c->path);
    if (bind(c->fd, (struct sockaddr *)&addr, sizeof addr) != 0 ||
        listen(c->fd, 1) != 0) {
        (void)close(c->fd);
        c->fd = -1;
        return -1;
    }
    (void)set_nonblock(c->fd);
    return 0;
}

int
tiku_conduct_adopt(tiku_conduct_t *c, int fd, void *ctx,
                        tiku_conduct_inject_fn inject,
                        tiku_conduct_pixel_fn pixel,
                        tiku_conduct_text_fn text)
{
    if (c == NULL || fd < 0) {
        return -1;
    }
    memset(c, 0, sizeof *c);
    c->fd = -1;                 /* nothing to listen on: the line is it */
    c->peer = fd;
    c->grant = TIKU_CONDUCT_GRANT_FULL;
    snprintf(c->principal, sizeof c->principal, "unnamed");
    c->ctx = ctx;
    c->inject = inject;
    c->pixel = pixel;
    c->text = text;
    (void)set_nonblock(fd);
    return 0;
}

/** @brief Map standard serial rates; zero requires the host-specific driver. */
static speed_t
conduct_speed(int baud)
{
    switch (baud) {
    case 1200: return B1200;
    case 2400: return B2400;
    case 4800: return B4800;
    case 9600: return B9600;
    case 19200: return B19200;
    case 38400: return B38400;
    case 57600: return B57600;
    case 115200: return B115200;
    case 230400: return B230400;
#ifdef B460800
    case 460800: return B460800;
#endif
#ifdef B921600
    case 921600: return B921600;
#endif
#ifdef B1000000
    case 1000000: return B1000000;
#endif
    default: return 0;
    }
}

int
tiku_conduct_open_tty(const char *path, int baud)
{
    struct termios tio;
    int fd;

    if (path == NULL || baud < 0) {
        return -1;
    }
    fd = open(path, O_RDWR | O_NOCTTY);
    if (fd < 0) {
        return -1;
    }
    if (tcgetattr(fd, &tio) == 0) {
        cfmakeraw(&tio);
        /* cfmakeraw leaves VMIN 1 and VTIME 0, and they stay so: with
         * VMIN 0 a read with nothing to give returns 0, which this code,
         * reading sockets too, takes as the peer hanging up.  The line is
         * made nonblocking (set_nonblock()) before it is read, and its
         * EAGAIN is not end-of-file. */
        if (baud > 0) {
            speed_t sp = conduct_speed(baud);
            if (sp && (cfsetispeed(&tio, sp) != 0 ||
                       cfsetospeed(&tio, sp) != 0)) {
                close(fd);
                return -1;
            }
            if (tcsetattr(fd, TCSANOW, &tio) != 0 ||
                tcgetattr(fd, &tio) != 0) {
                close(fd);
                return -1;
            }
            if ((!sp || cfgetospeed(&tio) != sp || cfgetispeed(&tio) != sp) &&
                tiku_sys_serial_speed(fd, baud) != 0) {
                close(fd);
                return -1;
            }
            return fd;
        }
        if (tcsetattr(fd, TCSANOW, &tio) == 0) return fd;
    }
    close(fd);
    return -1;
}

void
tiku_conduct_shutdown(tiku_conduct_t *c)
{
    if (c == NULL) {
        return;
    }
    if (c->peer >= 0) {
        (void)close(c->peer);
        c->peer = -1;
    }
    if (c->fd >= 0) {
        (void)close(c->fd);
        (void)unlink(c->path);
        c->fd = -1;
    }
    free(c->buf);
    c->buf = NULL;
    c->got = c->want = c->hgot = 0u;
}

void
tiku_conduct_require(tiku_conduct_t *c, const char *full_token,
                          const char *query_token)
{
    if (c == NULL) {
        return;
    }
    c->required = 1;
    snprintf(c->token_full, sizeof c->token_full, "%s",
             (full_token != NULL) ? full_token : "");
    snprintf(c->token_query, sizeof c->token_query, "%s",
             (query_token != NULL) ? query_token : "");
    /* A peer already connected may do nothing until it names itself. */
    c->grant = TIKU_CONDUCT_GRANT_NONE;
    snprintf(c->principal, sizeof c->principal, "unnamed");
    c->subs = 0;
}

void
tiku_conduct_quota(tiku_conduct_t *c, long messages)
{
    if (c != NULL) {
        c->quota = (messages > 0) ? messages : 0;
        c->spent = 0;
    }
}

/** @brief An FNV-1a hash of a fact's text, to tell whether it changed. */
static uint32_t
conduct_sig(const char *text)
{
    uint32_t h = UINT32_C(2166136261);

    while (*text != '\0') {
        h ^= (unsigned char)*text++;
        h *= UINT32_C(16777619);
    }
    return h;
}

/** @brief Remember @p name as watched, seeded with its current text. */
static int
conduct_watch(tiku_conduct_t *c, const char *name, const char *now)
{
    int i;

    for (i = 0; i < c->subs; i++) {
        if (strcmp(c->sub_name[i], name) == 0) {
            /* Watched twice is watched once, re-seeded: the answer the
             * driver just got is the value changes are measured from. */
            c->sub_sig[i] = conduct_sig(now);
            return 1;
        }
    }
    if (c->subs >= TIKU_CONDUCT_SUBS) {
        return 0;
    }
    snprintf(c->sub_name[c->subs], sizeof c->sub_name[c->subs], "%s", name);
    c->sub_sig[c->subs] = conduct_sig(now);
    c->subs++;
    return 1;
}

/** @brief A monotonic clock for the tell interval, in microseconds. */
static int64_t
conduct_now_us(void)
{
    struct timespec ts;

    (void)clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

/**
 * @brief Send a TOLD for each watched fact whose text changed, at most
 *        once per TIKU_CONDUCT_TELL_MS.
 *
 * Evaluating a fact costs what answering its query costs, a content draw
 * for a narration, so the facts are not read every turn.
 */
static void
conduct_tell(tiku_conduct_t *c)
{
    int64_t now;
    int i;

    if (c->peer < 0 || c->subs == 0 || c->text == NULL) {
        return;
    }
    now = conduct_now_us();
    if (now - c->told_at_us < (int64_t)TIKU_CONDUCT_TELL_MS * 1000) {
        return;
    }
    c->told_at_us = now;
    for (i = 0; i < c->subs; i++) {
        char text[TIKU_CONDUCT_TEXT];
        uint32_t sig;

        text[0] = '\0';
        if (!c->text(c->ctx, c->sub_name[i], text, sizeof text)) {
            continue;           /* unanswered now: not a change */
        }
        sig = conduct_sig(text);
        if (sig != c->sub_sig[i]) {
            unsigned char out[TIKU_CONDUCT_ARG + TIKU_CONDUCT_TEXT];

            c->sub_sig[i] = sig;
            memset(out, 0, TIKU_CONDUCT_ARG);
            snprintf((char *)out, TIKU_CONDUCT_ARG, "%s", c->sub_name[i]);
            memcpy(out + TIKU_CONDUCT_ARG, text, sizeof text);
            send_msg(&c->peer, TIKU_CMSG_TOLD, out, sizeof out);
            if (c->peer < 0) {
                c->subs = 0;    /* watches live as long as the peer */
                return;
            }
        }
    }
}

/** @brief Serve one complete message from the driver.
 *  @return nonzero if it injected an event. */
static int
conduct_message(tiku_conduct_t *c)
{
    const unsigned char *p = c->buf;

    switch (c->cur_type) {
    case TIKU_CMSG_HELLO:
        /* A HELLO naming another version closes the peer.  Without
         * required tokens, a peer that sends no HELLO keeps the full
         * grant it got when it connected. */
        if (c->want >= 4u) {
            uint32_t version;

            memcpy(&version, p, 4);
            if (version != TIKU_CONDUCT_VERSION) {
                (void)close(c->peer);
                c->peer = -1;
                break;
            }
        }
        if (c->required) {
            /* With tokens required, the token after the version names the
             * peer: the full token grants FULL, the query token QUERY, and
             * anything else closes the peer. */
            char token[TIKU_CONDUCT_TOKEN];

            token[0] = '\0';
            if (c->want >= 4u + TIKU_CONDUCT_TOKEN) {
                memcpy(token, p + 4, TIKU_CONDUCT_TOKEN);
                token[TIKU_CONDUCT_TOKEN - 1] = '\0';
            }
            if (c->token_full[0] != '\0' &&
                strcmp(token, c->token_full) == 0) {
                c->grant = TIKU_CONDUCT_GRANT_FULL;
                snprintf(c->principal, sizeof c->principal, "driver");
            } else if (c->token_query[0] != '\0' &&
                       strcmp(token, c->token_query) == 0) {
                c->grant = TIKU_CONDUCT_GRANT_QUERY;
                snprintf(c->principal, sizeof c->principal, "reader");
            } else {
                (void)close(c->peer);
                c->peer = -1;
                c->subs = 0;
            }
        }
        break;
    case TIKU_CMSG_INJECT:
    case TIKU_CMSG_QUERY:
        if (c->quota > 0 && ++c->spent > c->quota) {
            (void)close(c->peer);
            c->peer = -1;
            c->subs = 0;
            return 0;
        }
        break;
    default:
        break;
    }
    switch (c->cur_type) {
    case TIKU_CMSG_INJECT:
        if (c->grant < TIKU_CONDUCT_GRANT_FULL) {
            /* A reader's grant cannot inject: the event is dropped here,
             * before the shell sees it. */
            break;
        }
        if (c->want >= sizeof(tiku_event_t) && c->inject != NULL) {
            tiku_event_t ev;

            memcpy(&ev, p, sizeof ev);
            c->inject(c->ctx, &ev);
            return 1;
        }
        break;
    case TIKU_CMSG_QUERY:
        if (c->grant < TIKU_CONDUCT_GRANT_QUERY) {
            break;              /* no grant: no answer at all */
        }
        if (c->want >= 12u + TIKU_CONDUCT_ARG) {
            uint32_t what;
            int32_t a, b;
            char arg[TIKU_CONDUCT_ARG];
            int32_t reply[2];
            char text[TIKU_CONDUCT_TEXT];
            unsigned char out[8 + TIKU_CONDUCT_TEXT];

            memcpy(&what, p, 4);
            memcpy(&a, p + 4, 4);
            memcpy(&b, p + 8, 4);
            memcpy(arg, p + 12, sizeof arg);
            arg[sizeof arg - 1] = '\0';
            reply[0] = 0;
            reply[1] = 0;
            text[0] = '\0';
            if (what == TIKU_CQ_PIXEL && c->pixel != NULL) {
                reply[1] = (int32_t)c->pixel(c->ctx, a, b);
                reply[0] = 1;
            } else if (what == TIKU_CQ_TEXT && c->text != NULL &&
                       arg[0] == '~') {
                /* "~name" answers the fact's current text and watches it,
                 * so a change is pushed as a TOLD frame; an unknown name
                 * or a full watch table is answered 0. */
                reply[0] = (c->text(c->ctx, arg + 1, text, sizeof text) &&
                            conduct_watch(c, arg + 1, text)) ? 1 : 0;
            } else if (what == TIKU_CQ_TEXT && c->text != NULL) {
                reply[0] = c->text(c->ctx, arg, text, sizeof text) ? 1 : 0;
            }
            memcpy(out, reply, sizeof reply);
            memcpy(out + 8, text, sizeof text);
            send_msg(&c->peer, TIKU_CMSG_ANSWER, out, sizeof out);
            if (c->peer < 0) {
                c->subs = 0;
            }
        }
        break;
    default:
        break;
    }
    return 0;
}

int
tiku_conduct_poll(tiku_conduct_t *c)
{
    int injected = 0;

    if (c == NULL || (c->fd < 0 && c->peer < 0)) {
        return 0;
    }
    if (c->fd >= 0 && c->peer < 0) {
        int fd = accept(c->fd, NULL, NULL);

        if (fd >= 0) {
            (void)set_nonblock(fd);
            c->peer = fd;
            c->hgot = c->got = c->want = 0u;
            c->subs = 0;
            c->grant = c->required ? TIKU_CONDUCT_GRANT_NONE
                                   : TIKU_CONDUCT_GRANT_FULL;
            snprintf(c->principal, sizeof c->principal, "unnamed");
            c->spent = 0;
        }
    }
    while (c->peer >= 0) {
        int r = stream_read(c->peer, c->hbuf, &c->hgot, &c->buf, &c->got,
                            &c->want, &c->cur_type);

        if (r < 0) {
            (void)close(c->peer);
            c->peer = -1;
            c->subs = 0;        /* watches live as long as the peer */
            break;
        }
        if (r == 0) {
            break;
        }
        /* Served before the length is cleared: every branch checks the
         * payload size, and a zeroed one refuses them all. */
        injected |= conduct_message(c);
        c->want = 0u;
    }
    conduct_tell(c);
    return injected;
}

/*---------------------------------------------------------------------------*/
/* THE DRIVER'S SIDE                                                         */
/*---------------------------------------------------------------------------*/

/** @brief A watch notice retained while a query waits for its answer. */
struct tiku_conduct_notice {
    struct tiku_conduct_notice *next;
    unsigned char *payload;
};

/** @brief Free the frame @p c holds and set it to read a header next. */
static void
client_drop_frame(tiku_conduct_client_t *c)
{
    free(c->buf);
    c->buf = NULL;
    c->got = c->want = c->hgot = 0u;
}

int
tiku_conduct_connect_as(tiku_conduct_client_t *c, const char *path,
                             int wait_ms, const char *token)
{
    struct sockaddr_un addr;
    int waited = 0;

    if (c == NULL || path == NULL) {
        return -1;
    }
    memset(c, 0, sizeof *c);
    c->fd = -1;
    memset(&addr, 0, sizeof addr);
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof addr.sun_path, "%s", path);
    for (;;) {
        int fd = socket(AF_UNIX, SOCK_STREAM, 0);

        if (fd >= 0) {
            if (connect(fd, (struct sockaddr *)&addr, sizeof addr) == 0) {
                /* connect_fd_as makes the fd nonblocking, so the bounded
                 * wait for an answer can time out. */
                return tiku_conduct_connect_fd_as(c, fd, token);
            }
            (void)close(fd);
        }
        /* The shell may still be starting, so the connect is retried. */
        if (waited >= wait_ms) {
            return -1;
        }
        {
            struct timespec ts;

            ts.tv_sec = 0;
            ts.tv_nsec = 20 * 1000 * 1000;
            (void)nanosleep(&ts, NULL);
            waited += 20;
        }
    }
}

int
tiku_conduct_connect_fd_as(tiku_conduct_client_t *c, int fd,
                                const char *token)
{
    unsigned char hello[4 + TIKU_CONDUCT_TOKEN];
    uint32_t version = TIKU_CONDUCT_VERSION;

    if (c == NULL) {
        return -1;
    }
    memset(c, 0, sizeof *c);
    c->fd = -1;
    if (fd < 0) {
        return -1;
    }
    (void)signal(SIGPIPE, SIG_IGN);
    (void)set_nonblock(fd);
    c->fd = fd;
    memcpy(hello, &version, 4);
    memset(hello + 4, 0, TIKU_CONDUCT_TOKEN);
    if (token != NULL) {
        snprintf((char *)hello + 4, TIKU_CONDUCT_TOKEN, "%s", token);
    }
    send_msg(&c->fd, TIKU_CMSG_HELLO, hello, sizeof hello);
    return 0;
}

int
tiku_conduct_connect_fd(tiku_conduct_client_t *c, int fd)
{
    return tiku_conduct_connect_fd_as(c, fd, NULL);
}

void
tiku_conduct_disconnect(tiku_conduct_client_t *c)
{
    if (c != NULL && c->fd >= 0) {
        (void)close(c->fd);
        c->fd = -1;
    }
    if (c != NULL) {
        client_drop_frame(c);
        while (c->pending_head) {
            struct tiku_conduct_notice *n = c->pending_head;
            c->pending_head = n->next;
            free(n->payload);
            free(n);
        }
        c->pending_tail = NULL;
        c->pending_count = 0;
    }
}

int
tiku_conduct_inject(tiku_conduct_client_t *c,
                         const tiku_event_t *event)
{
    if (c == NULL || c->fd < 0 || event == NULL) {
        return -1;
    }
    send_msg(&c->fd, TIKU_CMSG_INJECT, event, (uint32_t)sizeof *event);
    return (c->fd >= 0) ? 0 : -1;
}

int
tiku_conduct_query(tiku_conduct_client_t *c, uint32_t what,
                        int a, int b, const char *arg, int *num,
                        char *text, size_t max)
{
    unsigned char msg[12 + TIKU_CONDUCT_ARG];
    int32_t ab[2];
    int ok = 0;
    int waited;

    if (c == NULL || c->fd < 0) {
        return 0;
    }
    ab[0] = a;
    ab[1] = b;
    memcpy(msg, &what, 4);
    memcpy(msg + 4, ab, sizeof ab);
    memset(msg + 12, 0, TIKU_CONDUCT_ARG);
    if (arg != NULL) {
        snprintf((char *)msg + 12, TIKU_CONDUCT_ARG, "%s", arg);
    }
    send_msg(&c->fd, TIKU_CMSG_QUERY, msg, (uint32_t)sizeof msg);
    if (c->fd < 0) {
        return 0;
    }

    /* Wait at most TIKU_CONDUCT_ANSWER_MS; a shell that stopped
     * answering makes the query fail. */
    for (waited = 0; waited < TIKU_CONDUCT_ANSWER_MS; ) {
        int r = stream_read(c->fd, c->hbuf, &c->hgot, &c->buf, &c->got,
                            &c->want, &c->cur_type);

        if (r < 0) {
            client_drop_frame(c);
            return 0;
        }
        if (r == 0) {
            struct timespec ts;

            ts.tv_sec = 0;
            ts.tv_nsec = 2 * 1000 * 1000;
            (void)nanosleep(&ts, NULL);
            waited += 2;
            continue;
        }
        if (c->cur_type == TIKU_CMSG_ANSWER &&
            c->want >= 8u + TIKU_CONDUCT_TEXT) {
            int32_t reply[2];

            memcpy(reply, c->buf, sizeof reply);
            ok = (reply[0] != 0);
            if (num != NULL) {
                *num = (int)reply[1];
            }
            if (text != NULL && max > 0u) {
                snprintf(text, max, "%s", (const char *)c->buf + 8);
            }
            client_drop_frame(c);
            return ok;
        }
        if (c->cur_type == TIKU_CMSG_TOLD &&
            c->want >= TIKU_CONDUCT_ARG + TIKU_CONDUCT_TEXT) {
            struct tiku_conduct_notice *n = c->pending_count < 256
                ? calloc(1, sizeof *n) : NULL;
            if (n == NULL) {
                tiku_conduct_disconnect(c);
                return 0;
            }
            n->payload = c->buf;
            c->buf = NULL;
            if (c->pending_tail) c->pending_tail->next = n;
            else c->pending_head = n;
            c->pending_tail = n;
            c->pending_count++;
        }
        client_drop_frame(c);
    }
    return 0;                   /* it never answered */
}

int
tiku_conduct_told(tiku_conduct_client_t *c, char *name,
                       size_t name_max, char *text, size_t text_max,
                       int wait_ms)
{
    int waited;

    if (c == NULL || c->fd < 0) {
        return 0;
    }
    if (name != NULL && name_max > 0u) {
        name[0] = '\0';
    }
    if (text != NULL && text_max > 0u) {
        text[0] = '\0';
    }
    if (c->pending_head) {
        struct tiku_conduct_notice *n = c->pending_head;
        if (name && name_max) snprintf(name, name_max, "%.*s",
            TIKU_CONDUCT_ARG, (char *)n->payload);
        if (text && text_max) snprintf(text, text_max, "%.*s",
            TIKU_CONDUCT_TEXT, (char *)n->payload + TIKU_CONDUCT_ARG);
        c->pending_head = n->next;
        if (!c->pending_head) c->pending_tail = NULL;
        c->pending_count--;
        free(n->payload);
        free(n);
        return 1;
    }
    /* Nothing is sent: the QUERY that set the watch arranged the telling,
     * and this only listens, for at most wait_ms. */
    for (waited = 0; waited < wait_ms; ) {
        int r = stream_read(c->fd, c->hbuf, &c->hgot, &c->buf, &c->got,
                            &c->want, &c->cur_type);

        if (r < 0) {
            client_drop_frame(c);
            return 0;
        }
        if (r == 0) {
            struct timespec ts;

            ts.tv_sec = 0;
            ts.tv_nsec = 2 * 1000 * 1000;
            (void)nanosleep(&ts, NULL);
            waited += 2;
            continue;
        }
        if (c->cur_type == TIKU_CMSG_TOLD &&
            c->want >= TIKU_CONDUCT_ARG + TIKU_CONDUCT_TEXT) {
            if (name != NULL && name_max > 0u) {
                snprintf(name, name_max, "%.*s", TIKU_CONDUCT_ARG,
                         (const char *)c->buf);
            }
            if (text != NULL && text_max > 0u) {
                snprintf(text, text_max, "%.*s", TIKU_CONDUCT_TEXT,
                         (const char *)c->buf + TIKU_CONDUCT_ARG);
            }
            client_drop_frame(c);
            return 1;
        }
        client_drop_frame(c);   /* another frame: dropped, keep listening */
    }
    return 0;                   /* the notice never came */
}

int
tiku_conduct_connect(tiku_conduct_client_t *c, const char *path,
                          int wait_ms)
{
    return tiku_conduct_connect_as(c, path, wait_ms, NULL);
}
