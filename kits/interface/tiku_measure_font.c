/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_measure_font.c - text measured on the desktop, from its baked
 * faces.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_measure.h"

#include "tiku_font.h"
#include "tiku_ui.h"

/** @brief The baked face for face number @p face. */
static const tiku_font_t *
face_of(int face)
{
    return tiku_font_for_measure(face);
}

int tiku_measure_font_size(void) { return tiku_font_size(); }
unsigned tiku_measure_font_stamp(void) { return tiku_font_generation(); }

int tiku_measure_font_face(int family, int pixels, unsigned cut)
{
    if (family < 0 || family > TIKU_MEASURE_FAMILY_MONO) family = 0;
    cut &= TIKU_MEASURE_FONT_BOLD | TIKU_MEASURE_FONT_ITALIC;
    int smallest = family == TIKU_MEASURE_FAMILY_MONO ? 9 : 10;
    int largest = family == TIKU_MEASURE_FAMILY_MONO ? 23 : 16;
    int chosen = smallest;
    for (int size = smallest; size <= largest; size += 2) {
        if (size <= pixels && size > chosen) chosen = size;
        if (size*2 <= pixels && size*2 > chosen) chosen = size*2;
    }
    pixels = chosen;
    if (family != TIKU_MEASURE_FAMILY_MONO) cut &= ~TIKU_MEASURE_FONT_ITALIC;
    return TIKU_MEASURE_FONT_BASE | (family << 10) | (int)(cut << 8) | pixels;
}

int tiku_measure_heading(int level)
{
    int body = tiku_font_size();
    int size = body + (level == 1 ? 4 : level == 2 ? 2 : 0);
    /* 16 is the largest size the baked ladder holds. */
    if (size > 16) size = 16;
    return size > body ? size : TIKU_MEASURE_BOLD;
}

int
tiku_measure_width(int face, const char *text)
{
    return tiku_text_width(face_of(face), text);
}

int
tiku_measure_height(int face)
{
    return face_of(face)->height;
}

int
tiku_measure_ascent(int face)
{
    return face_of(face)->ascent;
}

int
tiku_measure_row_h(void)
{
    return tiku_ui_row_h();
}

int
tiku_measure_step_w(void)
{
    return tiku_ui_step_w();
}
