/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_board_basic.h - run a BASIC program on a board, and watch it.
 *
 * The board's interpreter is entered from its shell with "basic", or on a
 * stream of its own; it is fed the program a line at a time, started with
 * RUN, and done when its prompt comes back.  The caller hands over a
 * program and takes the output as lines.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_BOARD_BASIC_H_
#define TIKU_BOARD_BASIC_H_

#include <stddef.h>

struct tiku_backend;

/** @brief One run, save or listing on a board. */
typedef struct tiku_board_basic tiku_board_basic_t;

/** @brief Where a run has got to. */
typedef enum {
    TIKU_BOARD_BASIC_SENDING = 0, /**< still handing the program over    */
    TIKU_BOARD_BASIC_RUNNING,     /**< started; its output is coming     */
    TIKU_BOARD_BASIC_ASKING,      /**< at an INPUT, waiting for a line   */
    TIKU_BOARD_BASIC_ENDED,       /**< the program finished by itself    */
    TIKU_BOARD_BASIC_STOPPED,     /**< a person stopped it               */
    TIKU_BOARD_BASIC_FAILED       /**< the board would not take it       */
} tiku_board_basic_state_t;

/**
 * @brief A board's own way to its interpreter, apart from its shell.
 *
 * A board that has one is not held while a program runs: its console
 * stays the shell's, so the namespace answers throughout.
 */
typedef struct {
    int  (*send)(void *ctx, const void *bytes, size_t len);
    int  (*recv)(void *ctx, void *buf, size_t max);
    void (*pump)(void *ctx);
    void  *ctx;
} tiku_board_basic_stream_t;

/**
 * @brief Start @p program over a board's own interpreter stream.
 *
 * Nothing is held: the board's shell is untouched, and the run is
 * driven and heard on @p stream alone.
 */
tiku_board_basic_t *tiku_board_basic_start_stream(
        const tiku_board_basic_stream_t *stream, const char *program);

/** @brief One line the program printed.  No newline on it. */
typedef void (*tiku_board_basic_line_fn)(const char *line, void *ctx);

/**
 * @brief Take the board's shell and start handing @p program over.
 *
 * @param b       a board backend, which must not already be held
 * @param program the whole program, lines separated by newlines
 * @return the run, or NULL when the shell could not be taken
 */
tiku_board_basic_t *tiku_board_basic_start(struct tiku_backend *b,
                                           const char *program);

/**
 * @brief Move the run along and hand over whatever the program printed.
 *
 * Waits at most @p timeout_ms; call it as often as the caller likes.
 *
 * @return the state the run is now in
 */
tiku_board_basic_state_t tiku_board_basic_pump(tiku_board_basic_t *r,
                                               int timeout_ms,
                                               tiku_board_basic_line_fn fn,
                                               void *ctx);

/**
 * @brief Keep @p program on the board under @p name instead of running it.
 *
 * The interpreter's own SAVE: the board keeps it in a named slot and
 * shows it in its namespace as /data/<name>.bas.  Pumped and freed like a
 * run; ENDED means saved.
 *
 * @return the run, or NULL for a name longer than seven characters or with
 *         a '"' in it, the board's limits.
 */
tiku_board_basic_t *tiku_board_basic_save(struct tiku_backend *b,
                                          const char *name,
                                          const char *program);

/**
 * @brief Read back what the board keeps under @p name: LOAD then LIST,
 *        the listing coming out as lines.  Pumped and freed like a run.
 */
tiku_board_basic_t *tiku_board_basic_list(struct tiku_backend *b,
                                          const char *name);

/**
 * @brief What the program is asking, while the run is ASKING: the text
 *        its INPUT printed, "NAME? " for INPUT "NAME"; A$.
 */
const char *tiku_board_basic_prompt(const tiku_board_basic_t *r);

/** @brief Answer the program's INPUT with @p text.
 *  @return 0, or -1 when the program is not running or the send fails. */
int tiku_board_basic_input(tiku_board_basic_t *r, const char *text);

/** @brief Break a running program, as Ctrl-C at its console does. */
void tiku_board_basic_stop(tiku_board_basic_t *r);

/** @brief Leave the interpreter, give the shell back, and free the run. */
void tiku_board_basic_free(tiku_board_basic_t *r);

/**
 * @brief Whether the run rides the board's own stream rather than a held
 *        shell.
 *
 * @note Freeing a streamed run pumps the channel, which the loop pumps too,
 *       so only a held run may be freed on a worker.
 */
int tiku_board_basic_streamed(const tiku_board_basic_t *r);

#endif /* TIKU_BOARD_BASIC_H_ */
