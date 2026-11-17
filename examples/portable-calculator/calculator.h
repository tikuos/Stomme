/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * calculator.h - static word calculator state and lifecycle.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef PORTABLE_CALCULATOR_H_
#define PORTABLE_CALCULATOR_H_
#include "tiku_static_app.h"
#include "tiku_word_model.h"
/** @brief Hex input and four bases from the shared developer calculator model. */
typedef struct {
    char entry[17], output[4][65];
    uint64_t value;
    unsigned length;
    int valid;
} portable_calculator_t;
/** @brief Static word calculator; key digits edit hex, b erases and c clears. */
extern const tiku_static_app_ops_t portable_calculator_ops;
#endif
