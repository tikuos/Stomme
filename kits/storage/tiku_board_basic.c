/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_board_basic.c - run a BASIC program on a board, and watch it.
 *
 * The interpreter prompts "ok> " and takes a program a line at a time; the
 * first prompt after RUN arrives only once the program has ended, and marks
 * the end of the run.  TikuBench's basic-wire test holds boards to these
 * cues.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define _GNU_SOURCE
#include "tiku_board_basic.h"

#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>

#include "tiku_model.h"

#define PROMPT      "ok>"
#define OUT_MAX     4096u
#define LINE_MAX    512u
/* How long the handover may take before the run fails: the line may be
 * held by something else, whose prompt never comes. */
#define START_MS    12000L

/** @brief A monotonic clock in milliseconds. */
static long
now_ms(void)
{
    struct timespec ts;

    (void)clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

struct tiku_board_basic {
    struct tiku_backend      *b;
    char                     *program;   /* the program, copied        */
    const char               *at;        /* where in it                */
    char                      buf[OUT_MAX + 1u];
    size_t                    len;
    char                      line[LINE_MAX];
    size_t                    line_len;
    tiku_board_basic_state_t  state;
    long                      began_ms;  /* when the handover started  */
    int                       mode;      /* MODE_RUN / SAVE / LIST     */
    char                      name[16];  /* the slot, for SAVE / LIST  */
    int                       newed;     /* NEW has gone out           */
    int                       started;   /* the last command went out  */
    /* The board echoes what it is told, and an echo is not output: the
     * next whole line is dropped when it is this. */
    char                      echo[LINE_MAX];
    int                       echo_pending;
    char                      prompt[LINE_MAX];  /* what INPUT asked  */
        int                       held;      /* the shell is held          */
    /* A board with a stream of its own: nothing is held, and the bytes
     * go and come on it rather than through the shell's line. */
    tiku_board_basic_stream_t stream;
    int                       streamed;
    int                       debug;     /* explicit DEBUG ON upload preamble */
    int                       debug_ready; /* version acknowledged before RUN */
};

#define MODE_RUN    0
#define MODE_SAVE   1
#define MODE_LIST   2

/** @brief Append the board's text, as the lease hands it over; past
 *         OUT_MAX bytes it is dropped. */
static void
took_text(const unsigned char *bytes, size_t n, void *ctx)
{
    tiku_board_basic_t *r = ctx;
    size_t i;

    for (i = 0u; i < n; i++) {
        if (r->len < OUT_MAX) {
            r->buf[r->len++] = (char)bytes[i];
            r->buf[r->len] = '\0';
        }
    }
}

/** @brief Whether the interpreter's prompt is in what has arrived, and
 *         where it ends.  The prompt is the only cue that means "ready". */
static int
prompt_at(const tiku_board_basic_t *r, size_t *end)
{
    const char *p = strstr(r->buf, PROMPT);

    if (p == NULL) {
        return 0;
    }
    *end = (size_t)(p - r->buf) + strlen(PROMPT);
    return 1;
}

/**
 * @brief The last prompt in what has arrived, and where it ends.
 *
 * On a stream, entering the interpreter answers with a prompt of its own,
 * so more prompts can be waiting than lines were sent; one line per batch
 * of prompts keeps the handover in step.
 */
static int
last_prompt_at(const tiku_board_basic_t *r, size_t *end)
{
    const char *p = r->buf;
    const char *found = NULL;

    for (;;) {
        const char *at = strstr(p, PROMPT);

        if (at == NULL) {
            break;
        }
        found = at;
        p = at + strlen(PROMPT);
    }
    if (found == NULL) {
        return 0;
    }
    *end = (size_t)(found - r->buf) + strlen(PROMPT);
    return 1;
}

/** @brief Drop the first @p n bytes of what has arrived. */
static void
consume(tiku_board_basic_t *r, size_t n)
{
    if (n >= r->len) {
        r->len = 0u;
    } else {
        memmove(r->buf, r->buf + n, r->len - n);
        r->len -= n;
    }
    r->buf[r->len] = '\0';
}

/** @brief Hand whole lines of @p text to the caller, keeping a partial
 *         one for the next pump; a line that is the pending echo is
 *         dropped. */
static void
say_lines(tiku_board_basic_t *r, const char *text, size_t n,
          tiku_board_basic_line_fn fn, void *ctx)
{
    size_t i;

    for (i = 0u; i < n; i++) {
        char c = text[i];

        if (c == '\r') {
            continue;
        }
        if (c == '\n') {
            r->line[r->line_len] = '\0';
            if (r->echo_pending) {
                const char *l = r->line;

                /* The echo follows the prompt's own trailing space. */
                while (*l == ' ') {
                    l++;
                }
                r->echo_pending = 0;
                if (strcmp(l, r->echo) == 0) {
                    r->line_len = 0u;
                    continue;
                }
            }
            if (fn != NULL) {
                fn(r->line, ctx);
            }
            r->line_len = 0u;
            continue;
        }
        if (r->line_len + 1u < LINE_MAX) {
            r->line[r->line_len++] = c;
        }
    }
}

/** @brief Send @p s on the run's stream or its held shell.  0, or -1. */
static int
send_text(tiku_board_basic_t *r, const char *s)
{
    if (r->streamed) {
        return (r->stream.send(r->stream.ctx, s, strlen(s)) >= 0) ? 0 : -1;
    }
    return tiku_backend_tiku_lease_send(r->b, s, strlen(s));
}

/** @brief Move the medium and take whatever the board has said. */
static void
draw_in(tiku_board_basic_t *r, int timeout_ms)
{
    if (!r->streamed) {
        (void)r->b->ops->pump(r->b, timeout_ms);
        return;
    }
    if (r->stream.pump != NULL) {
        r->stream.pump(r->stream.ctx);
    }
    for (;;) {
        unsigned char got[256];
        int n = r->stream.recv(r->stream.ctx, got, sizeof got);

        if (n <= 0) {
            break;
        }
        took_text(got, (size_t)n, r);
    }
}

/**
 * @brief Hold @p b's shell, drain what is still arriving and enter the
 *        interpreter, for a run, a save or a listing (@p mode).
 * @return the run, or NULL when the shell cannot be held.
 */
static tiku_board_basic_t *
start(struct tiku_backend *b, const char *program, int mode,
      const char *name)
{
    tiku_board_basic_t *r;

    if (b == NULL || program == NULL) {
        return NULL;
    }
    r = calloc(1, sizeof *r);
    if (r == NULL) {
        return NULL;
    }
    r->b = b;
    r->mode = mode;
    (void)snprintf(r->name, sizeof r->name, "%s",
                   (name != NULL) ? name : "");
    r->program = strdup(program);
    if (r->program == NULL) {
        free(r);
        return NULL;
    }
    r->at = r->program;
    if (tiku_backend_tiku_lease(b, took_text, r) != 0) {
        free(r->program);
        free(r);
        return NULL;
    }
    r->held = 1;
    /* What still arrives belongs to whoever had the line before, and a
     * trailing prompt in it would be taken for this run's: it is let land
     * and dropped. */
    {
        int quiet = 0, rounds = 0;

        /* At most twenty rounds as well as three quiet ones: this runs
         * inside the shell's loop. */
        while (quiet < 3 && rounds < 20) {
            size_t before = r->len;

            draw_in(r, 30);
            quiet = (r->len == before) ? quiet + 1 : 0;
            rounds++;
        }
    }
    r->len = 0u;
    r->buf[0] = '\0';
    r->state = TIKU_BOARD_BASIC_SENDING;
    r->began_ms = now_ms();
    /* A newline first ends any half-typed command left on the line, so
     * "basic" arrives as a command of its own. */
    (void)send_text(r, "\n");
    /* Entering answers with the banner and the prompt; the pump waits for
     * that prompt before it hands the first line over. */
    if (send_text(r, "basic\n") != 0) {
        r->state = TIKU_BOARD_BASIC_FAILED;
    }
    return r;
}

tiku_board_basic_t *
tiku_board_basic_start(struct tiku_backend *b, const char *program)
{
    return start(b, program, MODE_RUN, NULL);
}

tiku_board_basic_t *
tiku_board_basic_start_stream(const tiku_board_basic_stream_t *stream,
                              const char *program)
{
    tiku_board_basic_t *r;

    if (stream == NULL || stream->send == NULL || stream->recv == NULL ||
        program == NULL) {
        return NULL;
    }
    r = calloc(1, sizeof *r);
    if (r == NULL) {
        return NULL;
    }
    r->program = strdup(program);
    if (r->program == NULL) {
        free(r);
        return NULL;
    }
    r->at = r->program;
    r->stream = *stream;
    r->streamed = 1;
    r->mode = MODE_RUN;
    r->state = TIKU_BOARD_BASIC_SENDING;
    r->began_ms = now_ms();
    /* The board enters its interpreter on the first byte of this stream
     * and answers with the prompt the handover waits for; no "basic"
     * command is sent. */
    if (send_text(r, "\n") != 0) {
        r->state = TIKU_BOARD_BASIC_FAILED;
    }
    return r;
}

tiku_board_basic_t *
tiku_board_basic_save(struct tiku_backend *b, const char *name,
                      const char *program)
{
    if (name == NULL || name[0] == '\0' || strlen(name) > 7u ||
        strchr(name, '"') != NULL) {
        return NULL;            /* the board's own limit on a slot name */
    }
    return start(b, program, MODE_SAVE, name);
}

tiku_board_basic_t *
tiku_board_basic_list(struct tiku_backend *b, const char *name)
{
    char load[32];

    if (name == NULL || name[0] == '\0' || strlen(name) > 7u ||
        strchr(name, '"') != NULL) {
        return NULL;
    }
    (void)snprintf(load, sizeof load, "LOAD \"%s\"\n", name);
    return start(b, load, MODE_LIST, name);
}

const char *
tiku_board_basic_prompt(const tiku_board_basic_t *r)
{
    return (r != NULL && r->state == TIKU_BOARD_BASIC_ASKING) ? r->prompt
                                                              : "";
}

/** @brief Hand over the next line of the program, or start it. */
static void
send_next(tiku_board_basic_t *r)
{
    char line[LINE_MAX];
    const char *nl;
    size_t n;

    if (!r->newed) {
        /* NEW first: the interpreter keeps the last program's lines by
         * number, and an old line the new program does not overwrite
         * would run with it. */
        r->newed = 1;
        r->debug = r->at && strncmp(r->at, "DEBUG ON\n", 9) == 0;
        (void)send_text(r, "NEW\n");
        return;
    }
    while (r->at != NULL && *r->at != '\0') {
        nl = strchr(r->at, '\n');
        n = (nl != NULL) ? (size_t)(nl - r->at) : strlen(r->at);
        if (n + 2u > LINE_MAX) {
            r->state = TIKU_BOARD_BASIC_FAILED;
            return;
        }
        memcpy(line, r->at, n);
        line[n] = '\0';
        r->at = (nl != NULL) ? nl + 1 : r->at + strlen(r->at);
        /* A blank line at the prompt only reprints it; skipping keeps the
         * handover one line, one prompt. */
        if (line[0] != '\0') {
            size_t k = strlen(line);

            line[k] = '\n';
            line[k + 1u] = '\0';
            (void)send_text(r, line);
            return;
        }
    }
    if (r->mode == MODE_SAVE) {
        (void)snprintf(r->echo, sizeof r->echo, "SAVE \"%s\"", r->name);
    } else {
        if (r->debug && !r->debug_ready) {
            r->state = TIKU_BOARD_BASIC_FAILED;
            return; /* DEBUG ON was never acknowledged */
        }
        (void)snprintf(r->echo, sizeof r->echo, "%s",
                       (r->mode == MODE_LIST) ? "LIST" : "RUN");
    }
    (void)snprintf(line, sizeof line, "%s\n", r->echo);
    (void)send_text(r, line);
    r->started = 1;
    r->echo_pending = 1;
    r->state = TIKU_BOARD_BASIC_RUNNING;
}

tiku_board_basic_state_t
tiku_board_basic_pump(tiku_board_basic_t *r, int timeout_ms,
                      tiku_board_basic_line_fn fn, void *ctx)
{
    size_t end;

    if (r == NULL) {
        return TIKU_BOARD_BASIC_FAILED;
    }
    if (r->state != TIKU_BOARD_BASIC_SENDING &&
        r->state != TIKU_BOARD_BASIC_RUNNING &&
        r->state != TIKU_BOARD_BASIC_ASKING) {
        return r->state;
    }
        /* Drawing the medium is what moves the board's bytes to took_text. */
    draw_in(r, timeout_ms);

        if (r->state == TIKU_BOARD_BASIC_SENDING) {
        if (r->debug && strstr(r->buf, "[TDBG VERSION 1]")) { r->debug_ready = 1; }
        if (r->debug && strstr(r->buf, "[TDBG ERROR ")) {
            if (fn) { fn("Debugger setup rejected by firmware", ctx); }
            r->state = TIKU_BOARD_BASIC_FAILED;
            return r->state;
        }
        /* One prompt, one line: anything the board says while taking the
         * program is its own echo and not the program's output. */
        if (r->streamed) {
            if (last_prompt_at(r, &end)) {
                consume(r, end);
                send_next(r);
            }
        } else {
            while (r->state == TIKU_BOARD_BASIC_SENDING &&
                   prompt_at(r, &end)) {
                consume(r, end);
                send_next(r);
            }
        }
        if (r->state == TIKU_BOARD_BASIC_SENDING &&
            now_ms() - r->began_ms > START_MS) {
            r->state = TIKU_BOARD_BASIC_FAILED;
        }
        return r->state;
    }
    /* Running: everything up to the next prompt is the program talking,
     * and that prompt is the end of the run. */
    if (prompt_at(r, &end)) {
        size_t said = end - strlen(PROMPT);

        say_lines(r, r->buf, said, fn, ctx);
        if (r->line_len > 0u) {          /* a last line with no newline */
            r->line[r->line_len] = '\0';
            if (fn != NULL) {
                fn(r->line, ctx);
            }
            r->line_len = 0u;
        }
        consume(r, end);
        if (r->state == TIKU_BOARD_BASIC_RUNNING ||
            r->state == TIKU_BOARD_BASIC_ASKING) {
            r->state = TIKU_BOARD_BASIC_ENDED;
        }
        return r->state;
    }
    /* Whole lines go out; what follows the last newline stays, as it may
     * be the front of a prompt whose rest is in the next read. */
    {
        const char *last = memrchr(r->buf, '\n', r->len);

        if (last != NULL) {
            size_t upto = (size_t)(last - r->buf) + 1u;

            say_lines(r, r->buf, upto, fn, ctx);
            consume(r, upto);
        }
    }
    if (r->state == TIKU_BOARD_BASIC_RUNNING && r->len >= 2u &&
        r->buf[r->len - 2u] == '?' && r->buf[r->len - 1u] == ' ') {
        /* The line stopped at "? ": the program is waiting for a person.
         * The prompt is kept for whoever answers and taken off the line,
         * so what the program prints next starts a row of its own. */
        (void)snprintf(r->prompt, sizeof r->prompt, "%s", r->buf);
        consume(r, r->len);
        r->state = TIKU_BOARD_BASIC_ASKING;
    }
    return r->state;
}

int
tiku_board_basic_input(tiku_board_basic_t *r, const char *text)
{
    char line[LINE_MAX];

    if (r == NULL || text == NULL ||
        (r->state != TIKU_BOARD_BASIC_RUNNING &&
         r->state != TIKU_BOARD_BASIC_ASKING)) {
        return -1;
    }
    (void)snprintf(line, sizeof line, "%s\n", text);
    if (send_text(r, line) != 0) {
        return -1;
    }
    /* The board echoes the answer on the prompt's line; neither is what
     * the program printed. */
    (void)snprintf(r->echo, sizeof r->echo, "%s", text);
    r->echo_pending = 1;
    r->line_len = 0u;
    r->state = TIKU_BOARD_BASIC_RUNNING;
    return 0;
}

void
tiku_board_basic_stop(tiku_board_basic_t *r)
{
    if (r == NULL || (!r->held && !r->streamed)) {
        return;
    }
        if (r->started && (r->state == TIKU_BOARD_BASIC_RUNNING ||
                       r->state == TIKU_BOARD_BASIC_ASKING)) {
        (void)send_text(r, "\x03");
        r->state = TIKU_BOARD_BASIC_STOPPED;
    }
}

void
tiku_board_basic_free(tiku_board_basic_t *r)
{
    if (r == NULL) {
        return;
    }
        if (r->streamed) {
        /* Out of the interpreter, so the next run enters it afresh; the
         * shell was never held, so there is nothing to give back. */
        (void)send_text(r, "\x03");
        draw_in(r, 200);
        (void)send_text(r, "BYE\n");
        draw_in(r, 200);
    } else if (r->held) {
        /* Out of the interpreter before the shell is anybody else's: a
         * board left in BASIC answers no command the namespace asks. */
        (void)tiku_backend_tiku_lease_send(r->b, "\x03", 1u);
        (void)r->b->ops->pump(r->b, 200);
        (void)tiku_backend_tiku_lease_send(r->b, "BYE\n", 4u);
        (void)r->b->ops->pump(r->b, 200);
        tiku_backend_tiku_unlease(r->b);
    }
    free(r->program);
    free(r);
}

int
tiku_board_basic_streamed(const tiku_board_basic_t *r)
{
    return r != NULL && r->streamed;
}
