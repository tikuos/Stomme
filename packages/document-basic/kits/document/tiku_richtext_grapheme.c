/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_richtext_grapheme.c - UAX 29 extended grapheme boundary rules.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richtext_private.h"

/** @brief Whether a grapheme property is a control or newline. */
static int
control(unsigned value)
{
    return value == G_CONTROL || value == G_CR || value == G_LF;
}

/** @brief Decide the boundary before the current scalar using prior context. */
static int
boundary(const tiku_rt_grapheme_t *state, tiku_rt_props_t current)
{
    unsigned left = state->previous, right = current.grapheme;

    if (left == G_CR && right == G_LF)
        return 0;
    if (control(left) || control(right))
        return 1;
    if (left == G_L && (right == G_L || right == G_V ||
                        right == G_LV || right == G_LVT))
        return 0;
    if ((left == G_LV || left == G_V) && (right == G_V || right == G_T))
        return 0;
    if ((left == G_LVT || left == G_T) && right == G_T)
        return 0;
    if (right == G_EXTEND || right == G_ZWJ || right == G_SPACING ||
        left == G_PREPEND)
        return 0;
    if (current.indic == I_CONSONANT && state->indic == 2)
        return 0;
    if (current.pictographic && left == G_ZWJ && state->zwj_pictographic)
        return 0;
    if (left == G_RI && right == G_RI && state->regional)
        return 0;
    return 1;
}

int
tiku_rt_grapheme(tiku_rt_grapheme_t *state, tiku_rt_props_t current)
{
    int result = boundary(state, current);

    state->zwj_pictographic = current.grapheme == G_ZWJ && state->pictographic;
    if (current.pictographic)
        state->pictographic = 1;
    else if (current.grapheme != G_EXTEND)
        state->pictographic = 0;
    if (current.indic == I_CONSONANT)
        state->indic = 1;
    else if (current.indic == I_LINKER && state->indic)
        state->indic = 2;
    else if (current.indic != I_EXTEND)
        state->indic = 0;
    state->regional = current.grapheme == G_RI ? !state->regional : 0;
    state->previous = current.grapheme;
    return result;
}
