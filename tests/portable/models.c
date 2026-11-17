/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * models.c - bounded controls with a fixed-metrics test provider.
 * SPDX-License-Identifier: Apache-2.0
 */
#include <assert.h>
#include <string.h>
#include "tiku_slider.h"
#include "tiku_text_input.h"
#include "tiku_measure.h"
#include "tiku_font_subset.h"

/** @brief Check bounded text, slider input and subset lookup contracts. */
int main(void)
{
    tiku_slider_t slider;
    char text[8] = "abc";
    tiku_slider_init(&slider, 0, 100, 20, 5);
    assert(tiku_slider_step(&slider, 1) == 25);
    assert(tiku_slider_set(&slider, 200) == 100);
    assert(tiku_text_input_append(text, sizeof text, "de"));
    assert(!tiku_text_input_append(text, sizeof text, "fgh"));
    assert(strcmp(text, "abcde") == 0);
    assert(tiku_font_subset_face(0, 12)->count == 191);
    assert(tiku_font_subset_face(1, 12)->ascent == 12);
    assert(tiku_font_subset_face(0, 14) == NULL);
    assert(tiku_font_subset_face(2, 12) == NULL);
    assert(tiku_font_subset_glyph(0, 255)->cp == 255);
    assert(tiku_font_subset_glyph(1, 0xfffd)->cp == '?');
    assert(tiku_font_subset_glyph(-1, 65) == NULL);
    return 0;
}
