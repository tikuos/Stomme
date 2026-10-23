/*
 * KintsugiOS v0.01 kits -- the foundation KintsugiDesktop and KintsugiTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@kinstugi-os.org>
 *
 * tiku_text_input.h - a UTF-8 text commit carried whole over the input
 * event ABI: cut into TIKU_EVENT_TEXT_INPUT events by the host and put
 * back together by the window that has focus.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TEXT_INPUT_H_
#define TIKU_TEXT_INPUT_H_
#include <stddef.h>
#include "tiku_input.h"
/** @brief The longest commit, in bytes with the NUL. */
#define TIKU_TEXT_INPUT_MAX 1024
/** @brief Negotiated version-2 transfer capacity, including NUL. */
#define TIKU_TEXT_INPUT_V2_MAX 65536
/* Flags in a text event's button field. */
#define TIKU_TEXT_BEGIN 1u      /**< the commit's first event              */
#define TIKU_TEXT_END 2u        /**< the commit's last event               */
#define TIKU_TEXT_REJECT 4u     /**< the host could not deliver the commit */
/** @brief A commit being sent or put back together. */
typedef struct {
    char text[TIKU_TEXT_INPUT_MAX];
    /* Bytes held, bytes sent, whether a commit is in progress, and the
     * commit's length. */
    int length, offset, active, total;
} tiku_text_input_t;
/** @brief Separate version-2 staging; the legacy record keeps its ABI. */
typedef struct {
    char text[TIKU_TEXT_INPUT_V2_MAX];
    int length, offset, active, total;
} tiku_text_input_v2_t;
/** @brief Empty a version-2 commit or incomplete transfer. */
void tiku_text_input_reset_v2(tiku_text_input_v2_t *input);
/** @brief Emit one piece of a staged version-2 commit. */
int tiku_text_input_next_v2(tiku_text_input_v2_t *input,
                            tiku_input_event_t *event);
/**
 * @brief What tiku_text_input_event() made of an event: not a text event,
 *        part of a commit, the whole commit, or a malformed or
 *        out-of-order part, which drops the commit.
 */
enum { TIKU_TEXT_IGNORED, TIKU_TEXT_PENDING, TIKU_TEXT_COMPLETE,
       TIKU_TEXT_REJECTED };

/** @brief Stage a negotiated version-2 atomic commit, up to 65535 bytes. */
int tiku_text_input_set_v2(tiku_text_input_v2_t *input, const char *text,
                           size_t bytes);
/** @brief Assemble version-2 text events with the 65535-byte ceiling. */
int tiku_text_input_event_v2(tiku_text_input_v2_t *input,
                             const tiku_input_event_t *event);
/** @brief Drop any commit in progress and empty @p input. */
void tiku_text_input_reset(tiku_text_input_t *input);
/**
 * @brief Append @p text to the NUL-terminated string in @p buffer, which
 *        holds @p capacity bytes.
 *
 * Text that is not valid UTF-8, holds a control character (a tab or a
 * newline included) or does not fit whole is refused, and the buffer is
 * left unchanged.
 *
 * @return 1, or 0 when refused
 */
int tiku_text_input_append(char *buffer, size_t capacity, const char *text);
/**
 * @brief Remove the last UTF-8 codepoint from the NUL-terminated string in
 *        @p buffer, which holds @p capacity bytes.
 */
void tiku_text_input_backspace(char *buffer, size_t capacity);
/**
 * @brief Stage @p bytes of @p text as a commit for tiku_text_input_next().
 *
 * @return 1, or 0 for text too long or not valid UTF-8, leaving @p input
 *         empty
 */
int tiku_text_input_set(tiku_text_input_t *input, const char *text,
    size_t bytes);
/**
 * @brief Fill @p event with the next piece of the staged commit, cut on a
 *        codepoint boundary.
 *
 * The event is a TIKU_EVENT_TEXT_INPUT: its button carries TIKU_TEXT_BEGIN
 * and TIKU_TEXT_END, dx the piece's byte offset and dy the commit's
 * length; key and modifiers are zero.
 *
 * @return 1, or 0 when the whole commit has been sent
 */
int tiku_text_input_next(tiku_text_input_t *input, tiku_input_event_t *event);
/**
 * @brief Take @p event into the commit being put back together, without
 *        exposing part of it.
 *
 * A key press, a pointer press or a focus change drops an incomplete
 * commit.
 *
 * @return TIKU_TEXT_IGNORED, _PENDING, _COMPLETE or _REJECTED
 * @note Insert the text only on TIKU_TEXT_COMPLETE, and keep one
 *       tiku_text_input_t per focus owner.
 */
int tiku_text_input_event(tiku_text_input_t *input,
    const tiku_input_event_t *event);
#endif
