/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_link_tcp.c - whole messages over a socket, a length in front of each.
 *
 * What cannot go out now waits in a buffer the pump drains, so a board that
 * reads slowly never blocks the desktop's turn.  A message is written whole
 * into that buffer or not at all, since a peer that reads lengths cannot
 * recover from half of one.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_link_tcp.h"

#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

/* About what one turn reads, so a flood cannot hold the loop, and how
 * much may wait to go out while the link still reports itself ready. */
#define CHUNK 4096u

struct tcp_link {
    tiku_link_t   link;
    int           fd;
    unsigned char in[TIKU_LINK_TCP_RECV_MAX + TIKU_LINK_TCP_HEADER];
    size_t        in_len;
    size_t        skip;        /* bytes of one too large still to pass */
    unsigned char *out;        /* what has not gone out yet */
    size_t        out_len;
    size_t        out_cap;
    tiku_link_tcp_stats_t stats;
    unsigned long reported_faults; /* lifetime faults already consumed */
};

/** @brief Write what waits, as far as the socket will take it. */
static void
flush_out(struct tcp_link *tl)
{
    while (tl->out_len > 0u && tl->fd >= 0) {
        ssize_t n = send(tl->fd, tl->out, tl->out_len, MSG_NOSIGNAL);

        if (n > 0) {
            memmove(tl->out, tl->out + n, tl->out_len - (size_t)n);
            tl->out_len -= (size_t)n;
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            return;
        }
        if (n < 0 && errno == EINTR) {
            continue;
        }
        close(tl->fd);
        tl->fd = -1;
        tl->stats.faults++;
        return;
    }
}

/** @brief Room for @p want more bytes in the out buffer.  @return 0, or -1. */
static int
reserve(struct tcp_link *tl, size_t want)
{
    size_t need = tl->out_len + want;
    unsigned char *p;

    if (need <= tl->out_cap) {
        return 0;
    }
    p = realloc(tl->out, need);
    if (p == NULL) {
        return -1;
    }
    tl->out = p;
    tl->out_cap = need;
    return 0;
}

/** @brief One message: its length, then its bytes, into the out buffer. */
static int
tcp_send(tiku_link_t *l, const void *msg, size_t len)
{
    struct tcp_link *tl = l->ctx;
    unsigned char head[TIKU_LINK_TCP_HEADER];

    if (tl->fd < 0 || msg == NULL || len > TIKU_LINK_TCP_MTU ||
        reserve(tl, len + sizeof head) != 0) {
        tl->stats.refused++;
        return -1;
    }
    head[0] = (unsigned char)len;
    head[1] = (unsigned char)(len >> 8);
    head[2] = (unsigned char)(len >> 16);
    head[3] = (unsigned char)(len >> 24);
    memcpy(tl->out + tl->out_len, head, sizeof head);
    tl->out_len += sizeof head;
    memcpy(tl->out + tl->out_len, msg, len);
    tl->out_len += len;
    tl->stats.tx++;
    flush_out(tl);
    return 0;
}

/** @brief The length in front of the message now being gathered. */
static size_t
head_len(const struct tcp_link *tl)
{
    return (size_t)tl->in[0] | ((size_t)tl->in[1] << 8) |
           ((size_t)tl->in[2] << 16) | ((size_t)tl->in[3] << 24);
}

/**
 * @brief Take @p len bytes of the stream: a length, then that many bytes.
 *        A message longer than TIKU_LINK_TCP_RECV_MAX is skipped whole and
 *        counted as oversize, so the stream stays in step.
 */
static void
absorb(struct tcp_link *tl, const unsigned char *p, size_t len)
{
    while (len > 0u) {
        size_t want, take;

        if (tl->skip > 0u) {
            take = (tl->skip < len) ? tl->skip : len;
            tl->skip -= take;
            p += take;
            len -= take;
            continue;
        }
        if (tl->in_len < TIKU_LINK_TCP_HEADER) {
            want = TIKU_LINK_TCP_HEADER - tl->in_len;
        } else if (head_len(tl) > TIKU_LINK_TCP_RECV_MAX) {
            tl->stats.oversize++;
            tl->skip = head_len(tl);
            tl->in_len = 0u;
            continue;
        } else {
            want = head_len(tl) + TIKU_LINK_TCP_HEADER - tl->in_len;
        }
        take = (want < len) ? want : len;
        memcpy(tl->in + tl->in_len, p, take);
        tl->in_len += take;
        p += take;
        len -= take;
        /* Checked after each take, so a message finished by the chunk's
         * last byte is delivered on this turn. */
        if (tl->in_len >= TIKU_LINK_TCP_HEADER &&
            head_len(tl) <= TIKU_LINK_TCP_RECV_MAX &&
            tl->in_len == head_len(tl) + TIKU_LINK_TCP_HEADER) {
            tl->stats.rx++;
            tiku_link_deliver(&tl->link, tl->in + TIKU_LINK_TCP_HEADER,
                              head_len(tl));
            tl->in_len = 0u;
        }
    }
}

/** @brief Read what has arrived, and write what waits. */
static void
tcp_pump(tiku_link_t *l)
{
    struct tcp_link *tl = l->ctx;
    unsigned char chunk[CHUNK];
    size_t got = 0;

    while (tl->fd >= 0 && got < CHUNK) {
        ssize_t n = recv(tl->fd, chunk, sizeof chunk, 0);

        if (n > 0) {
            got += (size_t)n;
            absorb(tl, chunk, (size_t)n);
            continue;
        }
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            break;
        }
        /* 0 is the board hanging up; anything else is the line breaking. */
        close(tl->fd);
        tl->fd = -1;
        tl->stats.faults++;
        return;
    }
    flush_out(tl);
}

/** @brief Whether the link is up with less than CHUNK bytes waiting to
 *         go out. */
static int
tcp_ready(tiku_link_t *l)
{
    const struct tcp_link *tl = l->ctx;

    return tl->fd >= 0 && tl->out_len < CHUNK;
}

/** @brief Faults since last asked: the connection found gone. */
static int
tcp_lost(tiku_link_t *l)
{
    struct tcp_link *tl = l->ctx;
    unsigned long n = tl->stats.faults - tl->reported_faults;

    tl->reported_faults = tl->stats.faults;
    return (int)n;
}

/** @brief The largest message the board takes. */
static size_t
tcp_mtu(tiku_link_t *l)
{
    (void)l;
    return TIKU_LINK_TCP_MTU;
}

/** @brief Hang up and free what waited. */
static void
tcp_close(tiku_link_t *l)
{
    struct tcp_link *tl = l->ctx;

    if (tl->fd >= 0) {
        close(tl->fd);
    }
    free(tl->out);
    free(tl);
}

static const tiku_link_ops_t tcp_ops = {
    .send  = tcp_send,
    .ready = tcp_ready,
    .pump  = tcp_pump,
    .lost  = tcp_lost,
    .mtu   = tcp_mtu,
    .close = tcp_close,
};

tiku_link_t *
tiku_link_tcp_open(int fd)
{
    struct tcp_link *tl;
    int one = 1;

    if (fd < 0) {
        return NULL;
    }
    tl = calloc(1, sizeof *tl);
    if (tl == NULL) {
        return NULL;
    }
    tl->fd = fd;
    (void)fcntl(fd, F_SETFL, O_NONBLOCK);
    /* TCP_NODELAY: each of a window's small frames goes out at once. */
    (void)setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
    tl->link.ops = &tcp_ops;
    tl->link.ctx = tl;
    return &tl->link;
}

int
tiku_link_tcp_listen(int port)
{
    struct sockaddr_in a;
    int fd = socket(AF_INET, SOCK_STREAM, 0);
#ifdef __APPLE__
    /* Darwin has no MSG_NOSIGNAL: a send to a peer that hung up raises
     * SIGPIPE, which ends a program that does not ignore it.  SO_NOSIGPIPE,
     * set here on the listening socket, suppresses it. */
    if (fd >= 0) {
        int on = 1;

        (void)setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof on);
    }
#endif
    int one = 1;

    if (fd < 0) {
        return -1;
    }
    (void)setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    a.sin_port = htons((unsigned short)port);
    if (bind(fd, (struct sockaddr *)&a, sizeof a) != 0 ||
        listen(fd, 4) != 0) {
        close(fd);
        return -1;
    }
    (void)fcntl(fd, F_SETFL, O_NONBLOCK);
    return fd;
}

const tiku_link_tcp_stats_t *
tiku_link_tcp_stats(const tiku_link_t *l)
{
    const struct tcp_link *tl;

    if (l == NULL || l->ops != &tcp_ops) {
        return NULL;
    }
    tl = l->ctx;
    return &tl->stats;
}
