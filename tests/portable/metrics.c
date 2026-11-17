/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * metrics.c - fixed metrics provider for portable model link probes.
 * SPDX-License-Identifier: Apache-2.0
 */
#include <string.h>
#include "tiku_measure.h"

/** @brief Supply deterministic metrics independently of font rasterization. */
int tiku_measure_width(int face, const char *text)
{ (void)face; return (int)strlen(text) * 7; }
/** @brief Supply the line height. */
int tiku_measure_height(int face) { (void)face; return 15; }
/** @brief Supply the baseline ascent. */
int tiku_measure_ascent(int face) { (void)face; return 12; }
/** @brief Map headings to the bold face. */
int tiku_measure_heading(int level) { (void)level; return 1; }
/** @brief Supply the row height. */
int tiku_measure_row_h(void) { return 20; }
/** @brief Supply stepper width. */
int tiku_measure_step_w(void) { return 8; }
