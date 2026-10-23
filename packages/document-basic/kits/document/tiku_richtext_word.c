/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_richtext_word.c - UAX 29 default word boundary rules.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richtext_private.h"

/** @brief Whether the property is ignored by the word context rules. */
static int
ignored(unsigned value)
{
    return value == W_EXTEND || value == W_FORMAT || value == W_ZWJ;
}

/** @brief Whether the property represents a word-separating newline. */
static int
newline(unsigned value)
{
    return value == W_CR || value == W_LF || value == W_NEWLINE;
}

/** @brief Whether the word property is an alphabetic or Hebrew letter. */
static int
letter(unsigned value)
{
    return value == W_LETTER || value == W_HEBREW;
}

/** @brief Whether punctuation can join letters or numbers. */
static int
middle(unsigned value, int numeric)
{
    return value == W_MIDNUMLET || value == W_SINGLE ||
           value == (numeric ? W_MIDNUM : W_MIDLETTER);
}

/** @brief Whether the property may join an ExtendNumLet character. */
static int
extensible(unsigned value)
{
    return letter(value) || value == W_NUMERIC || value == W_KATAKANA;
}

/** @brief Find the next non-ignored word property in validated text. */
static unsigned
following(const char *text, size_t length, size_t at)
{
    uint32_t scalar;
    unsigned property;

    while (at < length) {
        tiku_rt_decode(text, length, &at, &scalar);
        property = tiku_rt_properties(scalar).word;
        if (!ignored(property))
            return property;
    }
    return W_OTHER;
}

/** @brief Apply ordered word rules using raw and significant predecessors. */
static int
boundary(const tiku_rt_word_t *state, tiku_rt_props_t current,
         const char *text, size_t length, size_t at)
{
    unsigned a = state->previous, b = current.word, before = state->before;

    if (state->raw == W_CR && b == W_LF)
        return 0;
    if (newline(state->raw) || newline(b))
        return 1;
    if (state->raw == W_ZWJ && current.pictographic)
        return 0;
    if (state->raw == W_SPACE && b == W_SPACE)
        return 0;
    if (ignored(b))
        return 0;
    if (letter(a) && letter(b))
        return 0;
    if (letter(a) && middle(b, 0) && letter(following(text, length, at)))
        return 0;
    if (letter(before) && middle(a, 0) && letter(b))
        return 0;
    if (a == W_HEBREW && b == W_SINGLE)
        return 0;
    if (a == W_HEBREW && b == W_DOUBLE &&
        following(text, length, at) == W_HEBREW)
        return 0;
    if (before == W_HEBREW && a == W_DOUBLE && b == W_HEBREW)
        return 0;
    if ((a == W_NUMERIC && (b == W_NUMERIC || letter(b))) ||
        (letter(a) && b == W_NUMERIC))
        return 0;
    if (before == W_NUMERIC && middle(a, 1) && b == W_NUMERIC)
        return 0;
    if (a == W_NUMERIC && middle(b, 1) &&
        following(text, length, at) == W_NUMERIC)
        return 0;
    if (a == W_KATAKANA && b == W_KATAKANA)
        return 0;
    if ((extensible(a) || a == W_EXTENDNUMLET) && b == W_EXTENDNUMLET)
        return 0;
    if (a == W_EXTENDNUMLET && extensible(b))
        return 0;
    return !(a == W_RI && b == W_RI && state->regional);
}

int
tiku_rt_word(tiku_rt_word_t *state, tiku_rt_props_t current,
               const char *text, size_t length, size_t offset)
{
    int result = boundary(state, current, text, length, offset);

    state->raw = current.word;
    if (!ignored(current.word)) {
        state->before = state->previous;
        state->previous = current.word;
        state->regional = current.word == W_RI ? !state->regional : 0;
    }
    return result;
}
