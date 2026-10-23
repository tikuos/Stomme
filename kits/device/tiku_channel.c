/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_channel.c - the socket pair between a link and the desktop's
 * listener, the shell road beside it, and a program's output.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_channel.h"
#include "tiku_link_serial.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define MSG_HEAD 8u
#define OUT_MAX  (MSG_HEAD + TIKU_SESSION_FRAME_MAX)

/* The session's own vocabulary, only as far as coalescing and the shell
 * road need it. */
#define RMSG_EVENT        16u
#define EVENT_POINTER_MOVE 8u
#define RMSG_SHELL_IN     21u
#define RMSG_SHELL_OUT    22u
#define RMSG_BASIC_IN     23u
#define RMSG_BASIC_OUT    24u
/* What a program has printed and nobody has read yet; past this, newer
 * bytes are dropped. */
#define BASIC_CAP         4096u

/* The most shell text one message carries; a board's inject ring is 256. */
#define SHELL_CHUNK       128u
/* How long a shell write waits for the link to take a message. */
#define SHELL_WAIT_MS     2000L

/* A greeting the board has not answered is said again after SILENCE_MS,
 * and after a loss; REGREETS of those go out at once since the board
 * last spoke, and past that only one every HEARTBEAT_MS, so a board with
 * a link and no windows costs two spare greetings and then a slow retry
 * that reaches one that comes back. */
#define SILENCE_MS        2000L
#define REGREETS          2
#define HEARTBEAT_MS      10000L

/* A port of the channel's own that broke is opened again this often. */
#define REOPEN_MS         1000L

/* How long a listener that has stopped reading is waited for, and the
 * step the wait is taken in; the wait starts again whenever it reads. */
#define STALL_MS          2000
#define STALL_STEP_MS     50

/*
 * The board's shell as a socket pair: the session at one end, the channel
 * at the other.  Only the thread that opened it, the one pumping the
 * channel, touches the link; a reader on another thread waits on its
 * socket.  The channel and the transport each hold it and the last to let
 * go closes it, so a read in flight as the channel closes hears an end.
 */
struct shell_road {
    tiku_channel_t *ch;             /* NULL once the channel is closed */
    pthread_t       owner;          /* the thread that pumps the channel */
    int             desk_fd;        /* the session's end */
    int             mine_fd;        /* the channel's end */
    int             refs;           /* the channel's and the transport's */
};

struct tiku_channel {
    tiku_link_t    *link;           /* the board, over its medium */
    tiku_session_t *sess;           /* a port of its own, or NULL */
    long            reopen_at;      /* ms, the next try after a break */
    int             desk_fd;        /* the listener's end */
    int             mine_fd;        /* fed by messages, drained to messages */
    unsigned char   out[OUT_MAX];   /* a message being gathered */
    size_t          got;
    size_t          skip;           /* bytes of one too big still to pass */
        unsigned char   basic[BASIC_CAP]; /* what a program printed, unread */
    size_t          basic_len;
    int             greeted;        /* a greeting has gone out */
    long            greeted_at;     /* ms, the last one */
    long            last_rx;        /* ms, the board's last message */
    int             regreets;       /* greetings said again since last_rx */
    int             losses;         /* the link's and stalls, in all */
    int             stalled;        /* the listener's stream is shut */
    struct shell_road *road;        /* the board's shell, or NULL */
};

/** @brief Milliseconds on the monotonic clock. */
static long
now_ms(void)
{
    struct timespec ts;

    (void)clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long)ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

/**
 * @brief Write all of @p bytes to the listener's stream.
 *
 * A listener that takes nothing for STALL_MS has its stream shut, and
 * reads an end there.  The stall counts as a loss, and later messages are
 * dropped.  Another error drops the rest.
 */
static void
write_all(tiku_channel_t *ch, const unsigned char *bytes, size_t len)
{
    size_t off = 0;
    int waited = 0;

    while (!ch->stalled && off < len) {
        ssize_t n = write(ch->mine_fd, bytes + off, len - off);

        if (n > 0) {
            off += (size_t)n;
            waited = 0;
        } else if (n < 0 && errno == EINTR) {
            continue;
        } else if (n < 0 && errno == EAGAIN && waited < STALL_MS) {
            struct pollfd one = { ch->mine_fd, POLLOUT, 0 };

            (void)poll(&one, 1u, STALL_STEP_MS);
            waited += STALL_STEP_MS;
        } else if (n < 0 && errno == EAGAIN) {
            (void)shutdown(ch->mine_fd, SHUT_WR);
            ch->stalled = 1;
            ch->losses++;
        } else {
            return;
        }
    }
}

/**
 * @brief Write what of @p bytes the socket takes now and drop the rest.
 *
 * For the shell road, which nobody may be reading: a command discards
 * whatever arrived before it.
 */
static void
write_fit(int fd, const unsigned char *bytes, size_t len)
{
    size_t off = 0;

    while (off < len) {
        ssize_t n = write(fd, bytes + off, len - off);

        if (n > 0) {
            off += (size_t)n;
        } else if (n < 0 && errno == EINTR) {
            continue;
        } else {
            return;
        }
    }
}

/** @brief The little-endian length at @p b + 4. */
static size_t
len_at(const unsigned char *b)
{
    return (size_t)b[4] | ((size_t)b[5] << 8) | ((size_t)b[6] << 16) |
           ((size_t)b[7] << 24);
}

/**
 * @brief A message from the board: handed to the desktop as a stream,
 *        except what its shell wrote, which goes to the shell road (and
 *        is dropped when none is open), and what a program printed, which
 *        is kept for tiku_channel_basic_recv().  The desktop's listener
 *        knows neither, and the shell's session does not know windows.
 */
static void
on_message(const unsigned char *bytes, size_t len, void *ctx)
{
    tiku_channel_t *ch = ctx;

    ch->last_rx = now_ms();
    ch->regreets = 0;
        if (len >= MSG_HEAD && bytes[0] == RMSG_SHELL_OUT && bytes[1] == 0u &&
        bytes[2] == 0u && bytes[3] == 0u && MSG_HEAD + len_at(bytes) <= len) {
        if (ch->road != NULL) {
            write_fit(ch->road->mine_fd, bytes + MSG_HEAD, len_at(bytes));
        }
        return;
    }
    if (len >= MSG_HEAD && bytes[0] == RMSG_BASIC_OUT && bytes[1] == 0u &&
        bytes[2] == 0u && bytes[3] == 0u && MSG_HEAD + len_at(bytes) <= len) {
        size_t n = len_at(bytes);

        if (ch->basic_len + n > BASIC_CAP) {
            n = BASIC_CAP - ch->basic_len;   /* the oldest keeps its place */
        }
        memcpy(ch->basic + ch->basic_len, bytes + MSG_HEAD, n);
        ch->basic_len += n;
        return;
    }
    write_all(ch, bytes, len);
}

tiku_channel_t *
tiku_channel_adopt(tiku_link_t *link)
{
    tiku_channel_t *ch;
    int sv[2];

    if (link == NULL || socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0) {
        tiku_link_close(link);
        return NULL;
    }
    ch = calloc(1, sizeof *ch);
    if (ch == NULL) {
        tiku_link_close(link);
        close(sv[0]);
        close(sv[1]);
        return NULL;
    }
    ch->link = link;
    ch->desk_fd = sv[0];
    ch->mine_fd = sv[1];
    (void)fcntl(ch->mine_fd, F_SETFL, O_NONBLOCK);
    tiku_link_on_message(link, on_message, ch);
    return ch;
}

/** @brief Pass what the session wrote on the shell road to the board, as
 *         SHELL_IN messages, while the link takes them. */
static void
shell_forward(tiku_channel_t *ch)
{
    unsigned char msg[MSG_HEAD + SHELL_CHUNK];

    while (ch->road != NULL && tiku_link_ready(ch->link)) {
        ssize_t n = read(ch->road->mine_fd, msg + MSG_HEAD, SHELL_CHUNK);

        if (n <= 0) {
            return;
        }
        memset(msg, 0, MSG_HEAD);
        msg[0] = RMSG_SHELL_IN;
        msg[4] = (unsigned char)n;
        (void)tiku_link_send(ch->link, msg, MSG_HEAD + (size_t)n);
    }
}

/** @brief Service the link while a read of the board's shell waits, when
 *         the reader is the thread that pumps the channel; a reader on any
 *         other thread waits for that thread's next turn. */
static void
shell_pump(void *ctx)
{
    struct shell_road *road = ctx;

    if (pthread_equal(pthread_self(), road->owner) && road->ch != NULL) {
        tiku_link_pump(road->ch->link);
        shell_forward(road->ch);
    }
}

/** @brief Bytes for the board's shell, into the road for the channel's
 *         pump to pass on.  @return @p len, or -1 when the channel is
 *         closed or the road stays full for SHELL_WAIT_MS. */
static int
shell_write(const void *buf, size_t len, void *ctx)
{
    struct shell_road *road = ctx;
    const unsigned char *p = buf;
    long deadline = now_ms() + SHELL_WAIT_MS;
    size_t off = 0;

    while (off < len) {
        ssize_t n = send(road->desk_fd, p + off, len - off, MSG_NOSIGNAL);

        if (n > 0) {
            off += (size_t)n;
        } else if (n < 0 && errno == EINTR) {
            continue;
        } else if (n < 0 && errno == EAGAIN && now_ms() < deadline) {
            struct pollfd one = { road->desk_fd, POLLOUT, 0 };

            shell_pump(road);
            (void)poll(&one, 1u, STALL_STEP_MS);
        } else {
            return -1;
        }
    }
    return (int)len;
}

/** @brief Let go of the road; the last holder closes and frees it. */
static void
road_release(void *ctx)
{
    struct shell_road *road = ctx;

    if (__atomic_sub_fetch(&road->refs, 1, __ATOMIC_ACQ_REL) == 0) {
        close(road->desk_fd);
        close(road->mine_fd);
        free(road);
    }
}

/** @brief A write to @p fd once its peer is gone fails rather than raising
 *         SIGPIPE; Darwin's sockets take that per socket. */
static void
no_sigpipe(int fd)
{
#ifdef SO_NOSIGPIPE
    int on = 1;

    (void)setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof on);
#else
    (void)fd;
#endif
}

int
tiku_channel_basic_send(tiku_channel_t *ch, const void *bytes, size_t len)
{
    const unsigned char *p = bytes;
    size_t off = 0;

    if (ch == NULL || bytes == NULL) {
        return -1;
    }
    while (off < len) {
        unsigned char msg[MSG_HEAD + SHELL_CHUNK];
        size_t n = len - off;
        long deadline = now_ms() + SHELL_WAIT_MS;

        if (n > SHELL_CHUNK) {
            n = SHELL_CHUNK;
        }
        while (!tiku_link_ready(ch->link)) {
            if (now_ms() >= deadline) {
                return -1;
            }
            tiku_link_pump(ch->link);
        }
        memset(msg, 0, MSG_HEAD);
        msg[0] = RMSG_BASIC_IN;
        msg[4] = (unsigned char)n;
        memcpy(msg + MSG_HEAD, p + off, n);
        if (tiku_link_send(ch->link, msg, MSG_HEAD + n) != 0) {
            return -1;
        }
        off += n;
    }
    return (int)len;
}

int
tiku_channel_basic_recv(tiku_channel_t *ch, void *buf, size_t max)
{
    size_t n;

    if (ch == NULL || buf == NULL || max == 0u) {
        return 0;
    }
    n = (ch->basic_len < max) ? ch->basic_len : max;
    if (n == 0u) {
        return 0;
    }
    memcpy(buf, ch->basic, n);
    ch->basic_len -= n;
    memmove(ch->basic, ch->basic + n, ch->basic_len);
    return (int)n;
}

tiku_tx_t *
tiku_channel_shell_tx(tiku_channel_t *ch)
{
    struct shell_road *road;
    tiku_tx_t *tx;
    int sv[2];

    if (ch == NULL || ch->road != NULL) {
        return NULL;
    }
    road = calloc(1, sizeof *road);
    if (road == NULL) {
        return NULL;
    }
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0) {
        free(road);
        return NULL;
    }
    road->ch = ch;
    road->owner = pthread_self();
    road->desk_fd = sv[0];
    road->mine_fd = sv[1];
    road->refs = 2;
    (void)fcntl(road->desk_fd, F_SETFL, O_NONBLOCK);
    (void)fcntl(road->mine_fd, F_SETFL, O_NONBLOCK);
    no_sigpipe(road->desk_fd);
    no_sigpipe(road->mine_fd);
    tx = tiku_tx_open_custom(road->desk_fd, shell_write, shell_pump, road,
                             "shell over a link");
    if (tx == NULL) {
        close(sv[0]);
        close(sv[1]);
        free(road);
        return NULL;
    }
    tiku_tx_on_close(tx, road_release);
    ch->road = road;
    return tx;
}

int
tiku_channel_losses(const tiku_channel_t *ch)
{
    return (ch != NULL) ? ch->losses : 0;
}

tiku_channel_t *
tiku_channel_open(tiku_session_t *s)
{
    return tiku_channel_adopt(tiku_link_serial_open(s));
}

tiku_channel_t *
tiku_channel_open_port(const char *port, int baud)
{
    tiku_tx_t *tx = tiku_tx_open_serial(port, baud);
    tiku_session_t *s;
    tiku_channel_t *ch;

    if (tx == NULL) {
        return NULL;
    }
    s = tiku_session_new(tx);
    if (s == NULL) {
        tiku_tx_close(tx);
        return NULL;
    }
    ch = tiku_channel_open(s);
    if (ch == NULL) {
        tiku_session_free(s);
        return NULL;
    }
    ch->sess = s;
    return ch;
}

/** @brief Read the port the channel owns; a line that broke is opened
 *         again, once a second, until it is back. */
static void
poll_own(tiku_channel_t *ch)
{
    if (ch->sess == NULL || tiku_session_poll(ch->sess, 0) >= 0) {
        return;
    }
    if (now_ms() >= ch->reopen_at) {
        ch->reopen_at = now_ms() + REOPEN_MS;
        (void)tiku_session_reopen(ch->sess);
    }
}

int
tiku_channel_probe(tiku_channel_t *ch, int timeout_ms)
{
    const tiku_link_serial_stats_t *st;
    long deadline;

    if (ch == NULL) {
        return 0;
    }
    st = tiku_link_serial_stats(ch->link);
    if (ch->sess == NULL || st == NULL) {
        return 1;
    }
    deadline = now_ms() + timeout_ms;
    while (now_ms() < deadline) {
        (void)tiku_session_poll(ch->sess, 20);
        tiku_link_pump(ch->link);
        if (st->acked > 0) {
            return 1;
        }
        if (st->faults > 0) {
            return 0;
        }
    }
    return 0;
}

void
tiku_channel_greet(tiku_channel_t *ch)
{
    /* type 20 (WELCOME), 8 bytes: version 1, features: command streams
     * and the invitation bit, which the listener's own reply to a HELLO
     * never carries -- so a board answers the invitation with its hello
     * and lets the reply pass, and the two do not go round for ever. */
    static const unsigned char welcome[16] = { 20, 0, 0, 0, 8, 0, 0, 0,
                                               1, 0, 0, 0, 2, 0, 0, 0x80 };

    if (ch != NULL) {
        (void)tiku_link_send(ch->link, welcome, sizeof welcome);
        ch->greeted = 1;
        ch->greeted_at = now_ms();
    }
}

int
tiku_channel_fd(const tiku_channel_t *ch)
{
    return (ch != NULL) ? ch->desk_fd : -1;
}

tiku_link_t *
tiku_channel_link(const tiku_channel_t *ch)
{
    return (ch != NULL) ? ch->link : NULL;
}

/** @brief The length field of the message at the head of @p ch->out. */
static size_t
head_len(const tiku_channel_t *ch)
{
    return (size_t)ch->out[4] | ((size_t)ch->out[5] << 8) |
           ((size_t)ch->out[6] << 16) | ((size_t)ch->out[7] << 24);
}

/** @brief Whether the message in @p ch->out is a pointer move. */
static int
is_move(const tiku_channel_t *ch)
{
    return ch->out[0] == RMSG_EVENT && ch->out[1] == 0u && head_len(ch) >= 12u
           && ch->out[MSG_HEAD + 4] == EVENT_POINTER_MOVE;
}

/** @brief Greet again: at once while fewer than REGREETS have gone
 *         since the board last spoke, else only HEARTBEAT_MS after the
 *         last one. */
static void
regreet(tiku_channel_t *ch)
{
    if (ch->regreets < REGREETS) {
        ch->regreets++;
        tiku_channel_greet(ch);
    } else if (now_ms() - ch->greeted_at >= HEARTBEAT_MS) {
        tiku_channel_greet(ch);
    }
}

/** @brief Discard what the desktop wrote and has not gone out: after a
 *         loss those events are stale, and the greeting that follows
 *         has the board say its windows again.  Bytes of a message too
 *         big for the board count against what is still to be passed. */
static void
flush(tiku_channel_t *ch)
{
    unsigned char scratch[256];
    ssize_t n;

    while ((n = read(ch->mine_fd, scratch, sizeof scratch)) > 0) {
        ch->skip = ((size_t)n < ch->skip) ? ch->skip - (size_t)n : 0u;
    }
    ch->got = 0;
}

/**
 * @brief Keep the link alive and the board's picture of the session
 *        fresh: its timers run; a loss it reports empties the backlog
 *        and greets again; so does a greeting left unanswered past
 *        SILENCE_MS.  A repeated greeting is safe, since the desktop's
 *        listener treats a repeated OPEN with the same id as the same
 *        window.
 */
static void
tend(tiku_channel_t *ch)
{
    poll_own(ch);
    tiku_link_pump(ch->link);
    if (tiku_link_lost(ch->link) > 0) {
        ch->losses++;
        flush(ch);
        regreet(ch);
    } else if (ch->greeted && ch->last_rx < ch->greeted_at &&
               now_ms() - ch->greeted_at > SILENCE_MS) {
        regreet(ch);
    }
}

/** @brief Pass over the rest of a message too big for the board: its
 *         bytes still in the socket go to a scratch buffer.  @return 0
 *         when none are left, -1 while the socket has yet to yield them. */
static int
pass(tiku_channel_t *ch)
{
    unsigned char scratch[256];

    while (ch->skip > 0u) {
        size_t want = (ch->skip < sizeof scratch) ? ch->skip : sizeof scratch;
        ssize_t n = read(ch->mine_fd, scratch, want);

        if (n <= 0) {
            return -1;
        }
        ch->skip -= (size_t)n;
    }
    return 0;
}

int
tiku_channel_pump(tiku_channel_t *ch)
{
    int sent = 0;

    if (ch == NULL) {
        return 0;
    }
    tend(ch);
    shell_forward(ch);
    for (;;) {
        ssize_t n;
        size_t len;

        if (pass(ch) != 0) {
            return sent;
        }
        /* The socket is the backlog: a message is read from it only
         * when the link would send one now. */
        if (ch->got < OUT_MAX && tiku_link_ready(ch->link)) {
            n = read(ch->mine_fd, ch->out + ch->got, OUT_MAX - ch->got);
            if (n > 0) {
                ch->got += (size_t)n;
            }
        }
        if (ch->got < MSG_HEAD) {
            return sent;
        }
        len = head_len(ch);
        if (MSG_HEAD + len > OUT_MAX) {
            /* Too big for the board: what is gathered is dropped and the
             * rest, which the length says how far, is passed over before
             * the next header is read, so the stream stays in step. */
            ch->skip = MSG_HEAD + len - ch->got;
            ch->got = 0;
            continue;
        }
        if (ch->got < MSG_HEAD + len) {
            return sent;
        }
        {
            /* A move overtaken by the next message is stale: the pointer
             * has already gone on, and a board reads at line rate. */
            int drop = is_move(ch) && ch->got > MSG_HEAD + len;

            if (!drop && !tiku_link_ready(ch->link)) {
                return sent;
            }
            /* A message the link refuses, one larger than the board
             * takes, is dropped here and counted there as refused. */
            if (!drop &&
                tiku_link_send(ch->link, ch->out, MSG_HEAD + len) == 0) {
                sent++;
            }
            memmove(ch->out, ch->out + MSG_HEAD + len,
                    ch->got - MSG_HEAD - len);
            ch->got -= MSG_HEAD + len;
            if (sent > 0) {
                /* One frame a turn: the board reads at line rate, and the
                 * desktop's turn must not wait on it. */
                return sent;
            }
        }
    }
}

void
tiku_channel_close(tiku_channel_t *ch)
{
    if (ch == NULL) {
        return;
    }
    tiku_link_close(ch->link);
    tiku_session_free(ch->sess);
    close(ch->desk_fd);
    close(ch->mine_fd);
    if (ch->road != NULL) {
        /* A read still waiting on the road hears its end now. */
        ch->road->ch = NULL;
        (void)shutdown(ch->road->mine_fd, SHUT_RDWR);
        road_release(ch->road);
    }
    free(ch);
}
