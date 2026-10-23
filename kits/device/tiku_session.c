/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_session.c - prompt discipline, echo removal, notify demux.
 *
 * It strips ANSI escapes, matches the prompt with or without a working
 * directory in it, and takes what the device says before the prompt as
 * the answer.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_session.h"

#include <pthread.h>

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

/* The text buffer starts small and grows for a long reply, such as a
 * BASIC LIST or a whole manifest. */
#define SESSION_BUF_INIT  (16u * 1024u)
#define SESSION_BUF_MAX   (512u * 1024u)

#define FRAME_END    0xC0u
#define FRAME_ESC    0xDBu
#define FRAME_ESC_END 0xDCu
#define FRAME_ESC_ESC 0xDDu
#define FRAME_ESC_NUL 0xDEu
#define FRAME_MARK   0xF1u

struct tiku_session {
    tiku_tx_t     *tx;
    unsigned char      *buf;
    size_t              len;
    size_t              cap;
    tiku_notify_fn notify;
    void               *notify_ctx;
    tiku_frame_fn       frame;          /* where whole frames go, or NULL */
    void               *frame_ctx;
    tiku_text_fn        text;           /* who owns the text, or NULL     */
    void               *text_ctx;
    unsigned char       fbuf[TIKU_SESSION_FRAME_MAX];
    size_t              flen;
    int                 armed;          /* an END was seen; next byte decides */
    int                 in_frame;
    int                 esc;
    int                 overflow;       /* the frame outgrew fbuf: dropped */
    /* One hand on the wire at a time: a worker in the middle of a
     * command and the loop polling for frames share this line, and the
     * prompt matching breaks with two readers. */
    pthread_mutex_t     hand;
    /* The pen is for writing only, so two writers never interleave.  A
     * frame sent while a worker holds the hand goes between the
     * command's bytes, which the board's decoder takes whole. */
    pthread_mutex_t     pen;
};

/** @brief Every byte out goes through here, one writer at a time. */
static int
put(tiku_session_t *s, const void *bytes, size_t n)
{
    int rc;

    pthread_mutex_lock(&s->pen);
    rc = tiku_tx_write(s->tx, bytes, n);
    pthread_mutex_unlock(&s->pen);
    return rc;
}

/** @brief Milliseconds on the monotonic clock. */
static long
now_ms(void)
{
    struct timespec ts;
    (void)clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long)ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

/** @brief Room for @p extra more bytes and a NUL in the text buffer,
 *         doubling it up to SESSION_BUF_MAX.  @return 0, or -1. */
static int
buf_reserve(tiku_session_t *s, size_t extra)
{
    size_t need = s->len + extra + 1u;
    unsigned char *p;

    if (need <= s->cap) {
        return 0;
    }
    if (need > SESSION_BUF_MAX) {
        return -1;
    }
    while (s->cap < need) {
        s->cap *= 2u;
    }
    p = realloc(s->buf, s->cap);
    if (p == NULL) {
        return -1;
    }
    s->buf = p;
    return 0;
}

/**
 * @brief Remove CSI escape sequences in place.
 *
 * The shell wraps its prompt in colour when built with TIKU_SHELL_COLOR=1;
 * stripping here lets one matcher serve both builds.
 */
static void
strip_ansi(unsigned char *b, size_t *len)
{
    size_t r = 0, w = 0;

    while (r < *len) {
        if (b[r] == 0x1bu && r + 1u < *len && b[r + 1u] == '[') {
            size_t k = r + 2u;
            while (k < *len && !((b[k] >= 'A' && b[k] <= 'Z') ||
                                 (b[k] >= 'a' && b[k] <= 'z'))) {
                k++;
            }
            if (k < *len) {
                r = k + 1u;            /* whole sequence consumed */
                continue;
            }
            break;                     /* truncated: keep for the next read */
        }
        b[w++] = b[r++];
    }
    /* Preserve any partial escape at the tail. */
    while (r < *len) {
        b[w++] = b[r++];
    }
    *len = w;
}

/**
 * @brief Find the prompt: "tikuOS> " or "tikuOS:<cwd>> ".
 *
 * @return Offset of its first byte, or -1.  @p end_out receives the offset
 *         just past it.
 */
static long
find_prompt(const unsigned char *b, size_t len, size_t *end_out)
{
    static const char pfx[] = "tikuOS";
    size_t i;

    for (i = 0; i + 8u <= len; i++) {
        size_t k;
        if (memcmp(b + i, pfx, 6u) != 0) {
            continue;
        }
        k = i + 6u;
        if (b[k] == ':') {
            while (k < len && b[k] != '>' && b[k] != '\n') {
                k++;
            }
        }
        if (k + 1u < len && b[k] == '>' && b[k + 1u] == ' ') {
            *end_out = k + 2u;
            return (long)i;
        }
    }
    return -1;
}

/**
 * @brief Pull complete `~<path>` lines out of the buffer and dispatch them.
 *
 * A subscription line can arrive anywhere in a command's output; each
 * complete `~` line is cut out of the buffer wherever it starts a line.
 *
 * @return Lines dispatched.
 */
static int
drain_notifies(tiku_session_t *s)
{
    size_t r = 0, w = 0;
    int fired = 0;
    int at_line_start = 1;

    while (r < s->len) {
        if (at_line_start && s->buf[r] == '~') {
            size_t e = r;
            while (e < s->len && s->buf[e] != '\n') {
                e++;
            }
            if (e >= s->len) {
                break;                 /* incomplete: leave it for next read */
            }
            if (s->notify != NULL) {
                char path[256];
                size_t n = e - r - 1u;
                if (n > 0u && s->buf[e - 1u] == '\r') {
                    n--;
                }
                if (n >= sizeof path) {
                    n = sizeof path - 1u;
                }
                memcpy(path, s->buf + r + 1u, n);
                path[n] = '\0';
                s->notify(path, s->notify_ctx);
            }
            fired++;
            r = e + 1u;
            at_line_start = 1;
            continue;
        }
        at_line_start = (s->buf[r] == '\n');
        s->buf[w++] = s->buf[r++];
    }
    /* Keep whatever tail was left unexamined. */
    while (r < s->len) {
        s->buf[w++] = s->buf[r++];
    }
    s->len = w;
    return fired;
}

/**
 * @brief Sort one received byte: into a frame, or into the text.
 *
 * An END arms the decision and the byte after it makes it: the marker
 * opens a frame, anything else was a stray END and the byte is text.
 * A frame that outgrows the buffer is dropped whole.
 */
static void
sort_byte(tiku_session_t *s, unsigned char b)
{
    if (s->in_frame) {
        if (b == FRAME_END) {
            if (s->frame != NULL && s->flen > 0u && !s->overflow) {
                s->frame(s->fbuf, s->flen, s->frame_ctx);
            }
            s->in_frame = 0;
            s->armed = 1;
            s->flen = 0;
            s->esc = 0;
            s->overflow = 0;
            return;
        }
        if (s->esc) {
            s->esc = 0;
            b = (b == FRAME_ESC_END) ? FRAME_END
              : (b == FRAME_ESC_ESC) ? FRAME_ESC
              : (b == FRAME_ESC_NUL) ? 0u : b;
        } else if (b == FRAME_ESC) {
            s->esc = 1;
            return;
        }
        if (s->flen < sizeof s->fbuf) {
            s->fbuf[s->flen++] = b;
        } else {
            s->overflow = 1;
        }
        return;
    }
    if (s->armed) {
        s->armed = 0;
        if (b == FRAME_MARK) {
            s->in_frame = 1;
            s->flen = 0;
            s->esc = 0;
            return;
        }
    }
    if (b == FRAME_END) {
        s->armed = 1;
        return;
    }
    s->buf[s->len++] = b;
}

/** @brief One transport read sorted into frames and text, then normalised. */
static int
pump(tiku_session_t *s, int timeout_ms, int *notified)
{
    unsigned char chunk[4096];
    int n = tiku_tx_read(s->tx, chunk, sizeof chunk, timeout_ms);
    int f, i;

    if (n < 0) {
        return -1;
    }
    if (n > 0) {
        if (buf_reserve(s, (size_t)n) != 0) {
            return -1;
        }
        for (i = 0; i < n; i++) {
            sort_byte(s, chunk[i]);
        }
        strip_ansi(s->buf, &s->len);
    }
    f = drain_notifies(s);
    if (notified != NULL) {
        *notified += f;
    }
    /* A client holding the line takes the text as it comes, after the
     * `~` lines have gone to the notify callback. */
    if (s->text != NULL && s->len > 0u) {
        s->text(s->buf, s->len, s->text_ctx);
        s->len = 0;
    }
    return n;
}

/** @brief A session on @p tx with an empty text buffer; its mutexes are
 *         the caller's to set up. */
static tiku_session_t *
session_new(tiku_tx_t *tx)
{
    tiku_session_t *s;

    if (tx == NULL) {
        return NULL;
    }
    s = calloc(1, sizeof *s);
    if (s == NULL) {
        return NULL;
    }
    s->buf = malloc(SESSION_BUF_INIT);
    if (s->buf == NULL) {
        free(s);
        return NULL;
    }
    s->cap = SESSION_BUF_INIT;
    s->tx  = tx;
    return s;
}

/** @brief Close the transport and free the session's memory. */
static void
session_free(tiku_session_t *s)
{
    if (s == NULL) {
        return;
    }
    tiku_tx_close(s->tx);
    free(s->buf);
    free(s);
}

/** @brief Set where whole frames go. */
static void
session_on_frame(tiku_session_t *s, tiku_frame_fn fn, void *ctx)
{
    if (s != NULL) {
        s->frame = fn;
        s->frame_ctx = ctx;
    }
}

/** @brief Hand the text to @p fn from now on, dropping what is held. */
static void
session_on_text(tiku_session_t *s, tiku_text_fn fn, void *ctx)
{
    if (s == NULL) {
        return;
    }
    /* Text that arrived before the handover is dropped. */
    s->len = 0;
    s->text = fn;
    s->text_ctx = ctx;
}

/** @brief Write @p n bytes as they are.  @return 0, or -1. */
static int
session_send_raw(tiku_session_t *s, const void *bytes, size_t n)
{
    if (s == NULL || bytes == NULL || n == 0u) {
        return -1;
    }
    return (put(s, bytes, n) < 0) ? -1 : 0;
}

/** @brief Read and discard until nothing has arrived for @p quiet_ms. */
static void
session_drain(tiku_session_t *s, int quiet_ms)
{
    long quiet_until;

    if (s == NULL) {
        return;
    }
    quiet_until = now_ms() + quiet_ms;
    while (now_ms() < quiet_until) {
        int n = pump(s, 10, NULL);

        if (n > 0) {
            quiet_until = now_ms() + quiet_ms;   /* still talking */
        } else if (n < 0) {
            break;
        }
    }
    s->len = 0;
}

/** @brief Escape @p bytes and write them as one frame: END, the marker,
 *         the payload, END.  @return 0, or -1. */
static int
session_send_frame(tiku_session_t *s, const void *bytes, size_t len)
{
    const unsigned char *p = bytes;
    unsigned char *out;
    size_t i, n = 0;
    int rc;

    if (s == NULL || bytes == NULL || len > TIKU_SESSION_FRAME_MAX) {
        return -1;
    }
    out = malloc(2u * len + 3u);
    if (out == NULL) {
        return -1;
    }
    out[n++] = FRAME_END;
    out[n++] = FRAME_MARK;
    for (i = 0; i < len; i++) {
        if (p[i] == FRAME_END) {
            out[n++] = FRAME_ESC;
            out[n++] = FRAME_ESC_END;
        } else if (p[i] == FRAME_ESC) {
            out[n++] = FRAME_ESC;
            out[n++] = FRAME_ESC_ESC;
        } else if (p[i] == 0u) {
            out[n++] = FRAME_ESC;
            out[n++] = FRAME_ESC_NUL;
        } else {
            out[n++] = p[i];
        }
    }
    out[n++] = FRAME_END;
    rc = put(s, out, n);
    free(out);
    return rc < 0 ? -1 : 0;
}

/** @brief Set the subscription-line handler. */
static void
session_on_notify(tiku_session_t *s, tiku_notify_fn fn,
                            void *ctx)
{
    if (s != NULL) {
        s->notify = fn;
        s->notify_ctx = ctx;
    }
}

/** @brief Drop what is held, send a line break and wait up to
 *         @p timeout_ms for a prompt.  @return 0, or -1. */
static int
session_sync(tiku_session_t *s, int timeout_ms)
{
    long deadline;

    if (s == NULL) {
        return -1;
    }
    s->len = 0;
    if (put(s, "\r\n", 2u) < 0) {
        return -1;
    }
    deadline = now_ms() + timeout_ms;
    for (;;) {
        size_t end;
        if (find_prompt(s->buf, s->len, &end) >= 0) {
            s->len = 0;
            return 0;
        }
        if (now_ms() >= deadline) {
            return -1;
        }
        if (pump(s, 50, NULL) < 0) {
            return -1;
        }
    }
}

/** @brief Send @p line and gather what the device says before the next
 *         prompt, the echo removed.  @return the bytes in @p out, or -1. */
static int
session_cmd(tiku_session_t *s, const char *line, char *out,
                      size_t max, int timeout_ms)
{
    long deadline;
    size_t echo;

    if (s == NULL || line == NULL) {
        return -1;
    }
    /* Discard anything still in flight (a second prompt from sync, a late
     * banner) so this command's frame starts at a known point. */
    s->len = 0;
    while (pump(s, 5, NULL) > 0) {
        s->len = 0;
    }
    if (put(s, line, strlen(line)) < 0 ||
        put(s, "\n", 1u) < 0) {
        return -1;
    }
    echo = strlen(line);
    deadline = now_ms() + timeout_ms;

    for (;;) {
        size_t end;
        long at = find_prompt(s->buf, s->len, &end);

        if (at >= 0) {
            size_t start = 0, n, eol = 0;


            /* The shell echoes the typed line.  It is matched at the end
             * of the first line, since a stale prompt fragment can precede
             * it; dropping through the newline removes both. */
            while (eol < (size_t)at && s->buf[eol] != '\n') {
                eol++;
            }
            if (eol <= (size_t)at && echo > 0u) {
                size_t line_len = eol;
                while (line_len > 0u && s->buf[line_len - 1u] == '\r') {
                    line_len--;
                }
                if (line_len >= echo &&
                    memcmp(s->buf + line_len - echo, line, echo) == 0) {
                    start = (eol < (size_t)at) ? eol + 1u : (size_t)at;
                }
            }
            n = (size_t)at - start;
            /* Trim the blank line the shell prints before its prompt. */
            while (n > 0u && (s->buf[start + n - 1u] == '\n' ||
                              s->buf[start + n - 1u] == '\r')) {
                n--;
            }
            if (out != NULL && max > 0u) {
                size_t cp = (n < max - 1u) ? n : max - 1u;
                memcpy(out, s->buf + start, cp);
                out[cp] = '\0';
                n = cp;
            }
            /* Anything after the prompt belongs to the next read. */
            memmove(s->buf, s->buf + end, s->len - end);
            s->len -= end;
            return (int)n;
        }
        if (now_ms() >= deadline) {
            if (out != NULL && max > 0u) {
                out[0] = '\0';
            }
            return -1;
        }
        if (pump(s, 50, NULL) < 0) {
            return -1;
        }
    }
}

/** @brief One read of up to @p timeout_ms.
 *  @return the `~` lines dispatched, or -1 on a broken link. */
static int
session_poll(tiku_session_t *s, int timeout_ms)
{
    int fired = 0;

    if (s == NULL) {
        return -1;
    }
    if (pump(s, timeout_ms, &fired) < 0) {
        return -1;
    }
    return fired;
}

/** @brief Reopen the transport and forget any half-read text or frame.
 *  @return 0, or -1. */
static int
session_reopen(tiku_session_t *s)
{
    if (s == NULL || tiku_tx_reopen(s->tx) != 0) {
        return -1;
    }
    s->len = 0;
    s->flen = 0;
    s->armed = 0;
    s->in_frame = 0;
    s->esc = 0;
    s->overflow = 0;
    return 0;
}

/** @brief Reopen, then wait up to four seconds for a prompt.
 *  @return 0, or -1. */
static int
session_reconnect(tiku_session_t *s)
{
    if (tiku_session_reopen(s) != 0) {
        return -1;
    }
    return tiku_session_sync(s, 4000);
}

const char *
tiku_session_name(const tiku_session_t *s)
{
    return (s != NULL) ? tiku_tx_name(s->tx) : "(none)";
}

/*---------------------------------------------------------------------------*/
/* THE HAND                                                                  */
/*---------------------------------------------------------------------------*/

tiku_session_t *
tiku_session_new(tiku_tx_t *tx)
{
    tiku_session_t *s = session_new(tx);
    pthread_mutexattr_t attr;

    if (s == NULL) {
        return NULL;
    }
    (void)pthread_mutexattr_init(&attr);
    (void)pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    (void)pthread_mutex_init(&s->hand, &attr);
    (void)pthread_mutex_init(&s->pen, &attr);
    (void)pthread_mutexattr_destroy(&attr);
    return s;
}

void
tiku_session_free(tiku_session_t *s)
{
    if (s == NULL) {
        return;
    }
    (void)pthread_mutex_destroy(&s->hand);
    (void)pthread_mutex_destroy(&s->pen);
    session_free(s);
}

/*
 * Every call that reads the line or changes a handler takes the hand; a
 * send holds only the pen (put()).  A poll that finds the hand taken
 * returns 0 at once, so the loop that polls never waits behind a
 * worker's command.
 */
#define WITH_HAND(s, call) do { \
        pthread_mutex_lock(&(s)->hand); call; \
        pthread_mutex_unlock(&(s)->hand); } while (0)

void
tiku_session_on_notify(tiku_session_t *s, tiku_notify_fn fn, void *ctx)
{
    if (s != NULL) {
        WITH_HAND(s, session_on_notify(s, fn, ctx));
    }
}

int
tiku_session_sync(tiku_session_t *s, int timeout_ms)
{
    int rc = -1;

    if (s != NULL) {
        WITH_HAND(s, rc = session_sync(s, timeout_ms));
    }
    return rc;
}

int
tiku_session_cmd(tiku_session_t *s, const char *line, char *out, size_t max,
                 int timeout_ms)
{
    int rc = -1;

    if (s != NULL) {
        WITH_HAND(s, rc = session_cmd(s, line, out, max, timeout_ms));
    }
    return rc;
}

int
tiku_session_poll(tiku_session_t *s, int timeout_ms)
{
    int rc;

    if (s == NULL) {
        return 0;
    }
    if (pthread_mutex_trylock(&s->hand) != 0) {
        return 0;               /* a worker has the wire: nothing now */
    }
    rc = session_poll(s, timeout_ms);
    pthread_mutex_unlock(&s->hand);
    return rc;
}

int
tiku_session_reconnect(tiku_session_t *s)
{
    int rc = -1;

    if (s != NULL) {
        WITH_HAND(s, rc = session_reconnect(s));
    }
    return rc;
}

int
tiku_session_reopen(tiku_session_t *s)
{
    int rc = -1;

    if (s != NULL) {
        WITH_HAND(s, rc = session_reopen(s));
    }
    return rc;
}

void
tiku_session_on_frame(tiku_session_t *s, tiku_frame_fn fn, void *ctx)
{
    if (s != NULL) {
        WITH_HAND(s, session_on_frame(s, fn, ctx));
    }
}

void
tiku_session_on_text(tiku_session_t *s, tiku_text_fn fn, void *ctx)
{
    if (s != NULL) {
        WITH_HAND(s, session_on_text(s, fn, ctx));
    }
}

int
tiku_session_send_raw(tiku_session_t *s, const void *bytes, size_t n)
{
    int rc = -1;

    if (s != NULL) {
        rc = session_send_raw(s, bytes, n);     /* put() holds the pen */
    }
    return rc;
}

void
tiku_session_drain(tiku_session_t *s, int quiet_ms)
{
    if (s != NULL) {
        WITH_HAND(s, session_drain(s, quiet_ms));
    }
}

int
tiku_session_send_frame(tiku_session_t *s, const void *bytes, size_t len)
{
    int rc = -1;

    if (s != NULL) {
        rc = session_send_frame(s, bytes, len); /* put() holds the pen */
    }
    return rc;
}
