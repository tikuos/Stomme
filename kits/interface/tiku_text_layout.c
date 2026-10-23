/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_text_layout.c - fitting text to a width: elision and wrapping.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_text_layout.h"
#include <string.h>

/**
 * @brief Copy the codepoint at @p s into @p out and return the source bytes
 *        it took: a control byte as a space, a malformed sequence as '?'.
 */
static size_t unit(const char *s, char out[5])
{
    const unsigned char *p = (const unsigned char *)s;
    unsigned cp;
    size_t n;
    if (*p < 0x80) { out[0] = *p < 32 ? ' ' : (char)*p; out[1] = 0; return 1; }
    if (*p >= 0xc2 && *p <= 0xdf) { n = 2; cp = *p & 31; }
    else if (*p >= 0xe0 && *p <= 0xef) { n = 3; cp = *p & 15; }
    else if (*p >= 0xf0 && *p <= 0xf4) { n = 4; cp = *p & 7; }
    else goto invalid;
    for (size_t i = 1; i < n; i++) {
        if ((p[i] & 0xc0) != 0x80) goto invalid;
        cp = (cp << 6) | (p[i] & 63);
    }
    if ((n == 3 && cp < 0x800) || (n == 4 && cp < 0x10000) ||
        cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) goto invalid;
    memcpy(out, s, n); out[n] = 0; return n;
invalid:
    out[0] = '?'; out[1] = 0; return 1;
}

/** @brief Start of the codepoint before byte @p n of @p s. */
static size_t previous(const char *s, size_t n)
{
    if (n) n--;
    while (n && ((unsigned char)s[n] & 0xc0) == 0x80) n--;
    return n;
}

/**
 * @brief Cut @p out back until it and the ellipsis fit @p width and @p cap,
 *        then append the ellipsis; empty when the ellipsis alone does not.
 */
static void ellipsis(int face, char *out, size_t cap, int width)
{
    size_t n = strlen(out);
    const char *mark = TIKU_TEXT_ELLIPSIS;
    int mark_w = tiku_measure_width(face, mark);
    if (cap < 4 || width < mark_w) { out[0] = 0; return; }
    while (n && (n + 4 > cap || tiku_measure_width(face, out) + mark_w > width)) {
        n = previous(out, n); out[n] = 0;
    }
    memcpy(out + n, mark, 4);
}

/**
 * @brief Copy into @p out the longest start of the line at @p s that fits
 *        @p width and @p cap.
 *
 * @return The source bytes taken, which differ from the bytes written
 *         when the input is malformed
 */
static size_t prefix(int face, const char *s, int width, char *out, size_t cap)
{
    size_t used = 0, n = 0;
    out[0] = 0;
    while (s[used] && s[used] != '\n' && s[used] != '\r') {
        char ch[5];
        size_t bytes = unit(s + used, ch), add = strlen(ch);
        if (n + add >= cap) break;
        memcpy(out + n, ch, add + 1);
        if (tiku_measure_width(face, out) > width) { out[n] = 0; break; }
        n += add; used += bytes;
    }
    return used;
}

int tiku_text_elide(int face, const char *text, int width, char *out, size_t cap)
{
    if (!text) text = "";
    if (!out || !cap) return *text != 0;
    if (cap > TIKU_TEXT_LINE_MAX) cap = TIKU_TEXT_LINE_MAX;
    if (width < 0) width = 0;
    size_t used = prefix(face, text, width, out, cap);
    if (text[used]) ellipsis(face, out, cap, width);
    return text[used] != 0;
}

int tiku_text_wrap_next(int face, const char **text, int width,
                        char *out, size_t cap, int last)
{
    if (!out || !cap || !text || !*text) return 0;
    out[0] = 0;
    const char *s = *text;
    if (!*s) return 0;
    if (cap > TIKU_TEXT_LINE_MAX) cap = TIKU_TEXT_LINE_MAX;
    size_t used = prefix(face, s, width, out, cap);
    if (last) {
        if (s[used]) ellipsis(face, out, cap, width);
        *text += strlen(s);
        return 1;
    }
    if (s[used] && s[used] != '\n' && s[used] != '\r') {
        /* Find a word boundary in the source, not in sanitized output. */
        size_t split = used;
        while (split && s[split] != ' ' && s[split] != '\t') split--;
        if (split) used = prefix(face, s, width, out, split + 1);
        /* A box too narrow for one codepoint still takes one, so the
         * caller always advances. */
        if (!used) { char ch[5]; used = unit(s, ch); }
        while (s[used] == ' ' || s[used] == '\t') used++;
    } else if (s[used] == '\r' || s[used] == '\n') {
        if (s[used++] == '\r' && s[used] == '\n') used++;
    }
    *text += used;
    return 1;
}
