/*
 * KintsugiDesktop v0.01 Interface Kit.
 * Authors: Ambuj Varshney <ambuj@kinstugi-os.org>
 * tiku_ime.h - bounded native marked-text staging in Unicode scalar offsets.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_IME_H_
#define TIKU_IME_H_
#include "tiku_text_input.h"
/** @brief A native host's marked text and pending complete update. */
typedef struct {
    char text[TIKU_TEXT_INPUT_V2_MAX]; /**< logical UTF-8 marked string */
    tiku_text_input_v2_t pending;      /**< staged atomic preedit transfer */
    size_t selected; /**< selected caret as a UTF-8 byte offset */
    unsigned token,
        sequence; /**< nonzero composition episode and high-water mark */
    int active;   /**< marked updates belong to the current episode */
    int cancel;   /**< one cancellation event awaits delivery */
} tiku_ime_t;
/** @brief Begin a distinct marked episode; zero after token exhaustion. */
int tiku_ime_begin(tiku_ime_t *ime);
/** @brief Replace the complete marked string while retaining its episode. */
int tiku_ime_set(tiku_ime_t *ime, const char *text, size_t bytes,
                 size_t selected);
/** @brief Replace scalar-indexed marked text and stage its complete value.
 * @return Nonzero on success; invalid ranges/text stage cancellation. */
int tiku_ime_replace(tiku_ime_t *ime, size_t first, size_t removed,
                     const char *text, size_t bytes, size_t selected);
/** @brief Stage an explicit cancellation without committing text. */
void tiku_ime_cancel(tiku_ime_t *ime);
/** @brief End outgoing marked state while retaining native delta text. */
void tiku_ime_submitted(tiku_ime_t *ime);
/** @brief Clear pending marked state after a committed composition. */
void tiku_ime_committed(tiku_ime_t *ime);
/** @brief Emit one PREEDIT event; zero when no update is pending. */
int tiku_ime_next(tiku_ime_t *ime, tiku_input_event_t *event);
#endif
