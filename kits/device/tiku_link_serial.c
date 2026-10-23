/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_link_serial.c - the serial backend of the link, over a session.
 *
 * The desktop paces what it sends: a window of numbered frames the board
 * acknowledges, written again when an acknowledgement is late and given
 * up on after enough tries.  What the board sends comes unpaced, checked
 * and counted here by the rules the board's receiver keeps
 * (services/link/tiku_link_console.c).
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_link_serial.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

/*
 * One frame the board has yet to acknowledge, or one waiting behind the
 * window: its bytes as built, header, payload and CRC, kept whole so
 * they can be written again unchanged.
 */
struct entry {
    unsigned char *frame;
    size_t         len;
    unsigned char  seq;
    int            is_syn;
    long           sent_at;         /* ms, the last time it was written */
};

struct serial_link {
    tiku_link_t              link;
    tiku_session_t          *sess;
    /* the sender */
    unsigned char            tx_seq;        /* the next frame's number */
    struct entry             flight[TIKU_LINK_SERIAL_WINDOW];
    int                      in_flight;
    struct entry             pending[TIKU_LINK_SERIAL_PENDING];
    int                      n_pending;
    int                      retries;       /* rounds since the last ACK */
    int                      need_syn;      /* the next send opens a stream */
    int                      dead;          /* the board never answered */
    int                      ever_acked;
    /* the receiver */
    unsigned char            paced_expect;  /* the next WANT_ACK seq taken */
    int                      paced_synced;
    unsigned char            plain_expect;  /* the next unpaced seq */
    int                      plain_seen;
    int                      unread_faults; /* since lost() last asked */
    tiku_link_serial_stats_t stats;
};

/** @brief Milliseconds on the monotonic clock. */
static long
now_ms(void)
{
    struct timespec ts;

    (void)clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long)ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

unsigned
tiku_link_serial_crc(unsigned crc, const void *bytes, size_t len)
{
    const unsigned char *p = bytes;
    size_t i;
    int bit;

    for (i = 0; i < len; i++) {
        crc ^= (unsigned)p[i] << 8;
        for (bit = 0; bit < 8; bit++) {
            crc = (crc & 0x8000u) ? ((crc << 1) ^ 0x1021u) : (crc << 1);
        }
        crc &= 0xFFFFu;
    }
    return crc;
}

/** @brief A frame as the wire carries it before escaping: @p ctl, @p seq,
 *         @p len bytes of @p payload, the CRC high byte first.
 *         @return the malloc'd frame, or NULL. */
static unsigned char *
build(unsigned char ctl, unsigned char seq, const void *payload, size_t len)
{
    unsigned char *f = malloc(len + TIKU_LINK_SERIAL_OVERHEAD);
    unsigned crc;

    if (f == NULL) {
        return NULL;
    }
    f[0] = ctl;
    f[1] = seq;
    if (len > 0u) {
        memcpy(f + 2, payload, len);
    }
    crc = tiku_link_serial_crc(0xFFFFu, f, len + 2u);
    f[len + 2u] = (unsigned char)(crc >> 8);
    f[len + 3u] = (unsigned char)crc;
    return f;
}

/** @brief @p e's frame onto the wire, stamped with the time it went. */
static void
write_entry(struct serial_link *sl, struct entry *e)
{
    (void)tiku_session_send_frame(sl->sess, e->frame, e->len);
    e->sent_at = now_ms();
}

/** @brief Frames waiting behind the window go out while it has room;
 *         each first send counts as tx. */
static void
refill(struct serial_link *sl)
{
    while (sl->in_flight < TIKU_LINK_SERIAL_WINDOW && sl->n_pending > 0) {
        sl->flight[sl->in_flight] = sl->pending[0];
        sl->n_pending--;
        memmove(sl->pending, sl->pending + 1,
                (size_t)sl->n_pending * sizeof sl->pending[0]);
        write_entry(sl, &sl->flight[sl->in_flight]);
        sl->in_flight++;
        sl->stats.tx++;
    }
}

/** @brief @p e into the window and out now when it has room and nothing
 *         waits ahead, else behind the window.  @return 0, or -1 with the
 *         frame freed when the queue is full too. */
static int
place(struct serial_link *sl, const struct entry *e)
{
    if (sl->in_flight < TIKU_LINK_SERIAL_WINDOW && sl->n_pending == 0) {
        sl->flight[sl->in_flight] = *e;
        write_entry(sl, &sl->flight[sl->in_flight]);
        sl->in_flight++;
        sl->stats.tx++;
        return 0;
    }
    if (sl->n_pending < TIKU_LINK_SERIAL_PENDING) {
        sl->pending[sl->n_pending++] = *e;
        return 0;
    }
    free(e->frame);
    return -1;
}

/** @brief A SYN into the stream.  The board restarts its own numbering
 *         when it takes one, so the unpaced stream is not counted for
 *         gaps until the SYN's own ACK says it starts at zero, or the
 *         board's next frame shows where it is. */
static int
place_syn(struct serial_link *sl)
{
    struct entry e;

    e.frame = build(TIKU_LINK_SERIAL_SYN | TIKU_LINK_SERIAL_WANT_ACK,
                    sl->tx_seq, NULL, 0u);
    if (e.frame == NULL) {
        return -1;
    }
    e.len = TIKU_LINK_SERIAL_OVERHEAD;
    e.seq = sl->tx_seq++;
    e.is_syn = 1;
    e.sent_at = 0;
    sl->need_syn = 0;
    sl->plain_seen = 0;
    return place(sl, &e);
}

/** @brief Free every frame in flight and behind the window. */
static void
drop_all(struct serial_link *sl)
{
    int i;

    for (i = 0; i < sl->in_flight; i++) {
        free(sl->flight[i].frame);
    }
    for (i = 0; i < sl->n_pending; i++) {
        free(sl->pending[i].frame);
    }
    sl->in_flight = 0;
    sl->n_pending = 0;
}

/** @brief The window is lost: what it held is counted and freed.  A
 *         board that once answered may have rebooted and gets a fresh
 *         SYN with the next send; one that never answered is marked dead,
 *         and sends to it are refused. */
static void
give_up(struct serial_link *sl)
{
    sl->stats.lost += (unsigned long)(sl->in_flight + sl->n_pending);
    drop_all(sl);
    sl->stats.faults++;
    sl->unread_faults++;
    sl->retries = 0;
    if (sl->ever_acked) {
        sl->need_syn = 1;
    } else {
        sl->dead = 1;
    }
}

/** @brief ACK(@p a) from the board: every frame in flight up to and
 *         including @p a is taken, and the window refills from behind.
 *         An @p a naming nothing in flight is ignored.  A SYN taken by
 *         its own ACK restarts the board's numbering at zero; one taken
 *         by the ACK of a later frame leaves the numbering unknown, since
 *         the board answered frames past the SYN before this ACK. */
static void
on_ack(struct serial_link *sl, unsigned char a)
{
    int n = sl->in_flight, popped, i;

    if (n == 0 || (unsigned char)(a - sl->flight[0].seq) >= (unsigned)n) {
        return;
    }
    popped = (int)(unsigned char)(a - sl->flight[0].seq) + 1;
    for (i = 0; i < popped; i++) {
        if (sl->flight[i].is_syn && sl->flight[i].seq == a) {
            sl->plain_expect = 0;
            sl->plain_seen = 1;
        } else if (sl->flight[i].is_syn) {
            sl->plain_seen = 0;
        }
        free(sl->flight[i].frame);
    }
    sl->in_flight = n - popped;
    memmove(sl->flight, sl->flight + popped,
            (size_t)sl->in_flight * sizeof sl->flight[0]);
    sl->stats.acked += (unsigned long)popped;
    sl->retries = 0;
    sl->ever_acked = 1;
    refill(sl);
}

/** @brief An ACK(@p seq) onto the wire, for a frame the board paced;
 *         counted as answered, apart from the acked of this link's own. */
static void
answer(struct serial_link *sl, unsigned char seq)
{
    unsigned char f[TIKU_LINK_SERIAL_OVERHEAD];
    unsigned crc;

    f[0] = TIKU_LINK_SERIAL_ACK;
    f[1] = seq;
    crc = tiku_link_serial_crc(0xFFFFu, f, 2u);
    f[2] = (unsigned char)(crc >> 8);
    f[3] = (unsigned char)crc;
    (void)tiku_session_send_frame(sl->sess, f, sizeof f);
    sl->stats.answered++;
}

/** @brief An unpaced DATA: delivered as it comes; a number other than
 *         the one expected counts one gap, whatever it skipped. */
static void
take_plain(struct serial_link *sl, unsigned char seq,
           const unsigned char *bytes, size_t len)
{
    if (sl->plain_seen && seq != sl->plain_expect) {
        sl->stats.gap++;
        sl->stats.faults++;
        sl->unread_faults++;
    }
    sl->plain_expect = (unsigned char)(seq + 1u);
    sl->plain_seen = 1;
    sl->stats.rx++;
    tiku_link_deliver(&sl->link, bytes + 2, len - TIKU_LINK_SERIAL_OVERHEAD);
}

/** @brief A paced DATA: the expected number is delivered and then
 *         acknowledged; a recent repeat is acknowledged again and not
 *         delivered; a stranger is dropped unanswered. */
static void
take_paced(struct serial_link *sl, unsigned char seq,
           const unsigned char *bytes, size_t len)
{
    unsigned char behind;

    if (!sl->paced_synced) {
        sl->paced_expect = seq;
        sl->paced_synced = 1;
    }
    if (seq == sl->paced_expect) {
        sl->paced_expect++;
        sl->stats.rx++;
        tiku_link_deliver(&sl->link, bytes + 2,
                          len - TIKU_LINK_SERIAL_OVERHEAD);
        answer(sl, seq);
        return;
    }
    behind = (unsigned char)(sl->paced_expect - seq);
    if (behind >= 1u && behind <= TIKU_LINK_SERIAL_DUP_SPAN) {
        sl->stats.dup++;
        answer(sl, (unsigned char)(sl->paced_expect - 1u));
        return;
    }
    sl->stats.reject++;
}

/** @brief Whether @p len bytes of @p f end in the CRC of what precedes. */
static int
crc_agrees(const unsigned char *f, size_t len)
{
    unsigned crc = tiku_link_serial_crc(0xFFFFu, f, len - 2u);

    return f[len - 2u] == (unsigned char)(crc >> 8) &&
           f[len - 1u] == (unsigned char)crc;
}

/** @brief The session's frame handler: one link frame, header, payload
 *         and CRC, sorted by its kind.  A short one or one whose CRC
 *         disagrees is dropped whole and counted bad; a kind this side
 *         does not know is dropped without a count.  A SYN from the
 *         board restarts this link's numbering at zero. */
static void
on_frame(const unsigned char *bytes, size_t len, void *ctx)
{
    struct serial_link *sl = ctx;
    unsigned char kind, seq;

    if (len < TIKU_LINK_SERIAL_OVERHEAD || !crc_agrees(bytes, len)) {
        sl->stats.bad++;
        return;
    }
    kind = bytes[0] & TIKU_LINK_SERIAL_KIND;
    seq = bytes[1];
    if (kind == TIKU_LINK_SERIAL_ACK) {
        on_ack(sl, seq);
    } else if (kind == TIKU_LINK_SERIAL_SYN) {
        sl->paced_expect = (unsigned char)(seq + 1u);
        sl->paced_synced = 1;
        sl->tx_seq = 0;
        answer(sl, seq);
    } else if (kind == TIKU_LINK_SERIAL_DATA) {
        if (bytes[0] & TIKU_LINK_SERIAL_WANT_ACK) {
            take_paced(sl, seq, bytes, len);
        } else {
            take_plain(sl, seq, bytes, len);
        }
    }
}

/** @brief A message as DATA wanting an ack, after a SYN when a stream
 *         must open first.  Out now when the window has room, else
 *         queued.  Refused, and counted so, on a link nothing ever
 *         answered or for a message larger than the board takes. */
static int
serial_send(tiku_link_t *l, const void *msg, size_t len)
{
    struct serial_link *sl = l->ctx;
    struct entry e;

    if (sl->dead || len > TIKU_LINK_SERIAL_MTU) {
        sl->stats.refused++;
        return -1;
    }
    if (msg == NULL) {
        return -1;
    }
    if (sl->need_syn && place_syn(sl) != 0) {
        return -1;
    }
    e.frame = build(TIKU_LINK_SERIAL_DATA | TIKU_LINK_SERIAL_WANT_ACK,
                    sl->tx_seq, msg, len);
    if (e.frame == NULL) {
        return -1;
    }
    e.len = len + TIKU_LINK_SERIAL_OVERHEAD;
    e.seq = sl->tx_seq++;
    e.is_syn = 0;
    e.sent_at = 0;
    return place(sl, &e);
}

/** @brief Whether a send would go out now: room in the window and
 *         nothing queued ahead of it. */
static int
serial_ready(tiku_link_t *l)
{
    const struct serial_link *sl = l->ctx;

    return !sl->dead && sl->in_flight < TIKU_LINK_SERIAL_WINDOW &&
           sl->n_pending == 0;
}

/** @brief The retransmit timer: an oldest frame unacknowledged past
 *         RETRY_MS sends the whole window again, in order, and after
 *         RETRIES rounds the window is given up on. */
static void
serial_pump(tiku_link_t *l)
{
    struct serial_link *sl = l->ctx;
    int i;

    if (sl->in_flight == 0 ||
        now_ms() - sl->flight[0].sent_at < TIKU_LINK_SERIAL_RETRY_MS) {
        return;
    }
    if (sl->retries >= TIKU_LINK_SERIAL_RETRIES) {
        give_up(sl);
        return;
    }
    for (i = 0; i < sl->in_flight; i++) {
        write_entry(sl, &sl->flight[i]);
    }
    sl->stats.retransmit += (unsigned long)sl->in_flight;
    sl->retries++;
}

/** @brief Faults since last asked: windows given up on and gaps in the
 *         board's stream, each one a reason to greet again. */
static int
serial_lost(tiku_link_t *l)
{
    struct serial_link *sl = l->ctx;
    int n = sl->unread_faults;

    sl->unread_faults = 0;
    return n;
}

/** @brief Detach from the session's frames and free what is in flight
 *         and behind the window. */
static void
serial_close(tiku_link_t *l)
{
    struct serial_link *sl = l->ctx;

    tiku_session_on_frame(sl->sess, NULL, NULL);
    drop_all(sl);
    free(sl);
}

/** @brief The largest message the board's session channel takes. */
static size_t
serial_mtu(tiku_link_t *l)
{
    (void)l;
    return TIKU_LINK_SERIAL_MTU;
}

static const tiku_link_ops_t serial_ops = {
    .send  = serial_send,
    .ready = serial_ready,
    .pump  = serial_pump,
    .lost  = serial_lost,
    .mtu   = serial_mtu,
    .close = serial_close,
};

tiku_link_t *
tiku_link_serial_open(tiku_session_t *s)
{
    struct serial_link *sl;

    if (s == NULL) {
        return NULL;
    }
    sl = calloc(1, sizeof *sl);
    if (sl == NULL) {
        return NULL;
    }
    sl->sess = s;
    sl->link.ops = &serial_ops;
    sl->link.ctx = sl;
    tiku_session_on_frame(s, on_frame, sl);
    sl->need_syn = 1;
    (void)place_syn(sl);
    return &sl->link;
}

const tiku_link_serial_stats_t *
tiku_link_serial_stats(const tiku_link_t *l)
{
    if (l == NULL || l->ops != &serial_ops) {
        return NULL;
    }
    return &((const struct serial_link *)l->ctx)->stats;
}
