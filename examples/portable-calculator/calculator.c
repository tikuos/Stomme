/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * calculator.c - bounded hex input and shared base conversion.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "calculator.h"
#include <string.h>
/** @brief Recompute bases with the shared fixed-width calculator operations. */
static void update(portable_calculator_t *calculator)
{
    static const unsigned bases[] = {16, 10, 8, 2};
    calculator->valid = tiku_word_parse(calculator->entry, 16, 64, &calculator->value);
    for (unsigned i = 0; i < 4; ++i) {
        if (calculator->valid) tiku_word_format(calculator->value, bases[i], calculator->output[i]);
        else calculator->output[i][0] = 0;
    }
}
/** @brief Start with an unsigned zero. */
static tiku_resource_error_t start(void *state, tiku_static_app_t *app, void *argument)
{
    portable_calculator_t *calculator = state;
    (void)app; (void)argument;
    calculator->entry[0] = '0';
    calculator->length = 1;
    update(calculator);
    return TIKU_RESOURCE_OK;
}
/** @brief Consume one key; the bounded entry holds sixteen hex digits. */
static void dispatch(void *state, tiku_queue_result_t result, const tiku_input_event_t *event)
{
    portable_calculator_t *calculator = state;
    unsigned key;
    if (result != TIKU_QUEUE_EVENT || event->type != TIKU_EVENT_KEY_DOWN) return;
    key = event->key;
    if (key == 'c') calculator->length = 0;
    else if (key == 'b' && calculator->length) --calculator->length;
    else if (((key >= '0' && key <= '9') || (key >= 'A' && key <= 'F')) && calculator->length < 16) {
        if (calculator->length == 1 && calculator->entry[0] == '0') calculator->length = 0;
        calculator->entry[calculator->length++] = (char)key;
    }
    calculator->entry[calculator->length] = 0;
    update(calculator);
}
/** @brief The word calculator has no asynchronous work. */
static void stop(void *state) { (void)state; }
/** @brief Erase application state before its allocation is released. */
static void dispose(void *state) { memset(state, 0, sizeof(portable_calculator_t)); }
const tiku_static_app_ops_t portable_calculator_ops = {
    sizeof(portable_calculator_t), start, dispatch, NULL, stop, dispose
};
