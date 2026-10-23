/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_richio_xml.c - strict UTF-8 XML profile and schema string escapes.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richio_private.h"
#include <string.h>

/** @brief Hexadecimal digit value, or 16 for another character. */
static unsigned
digit(unsigned c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 16;
}

size_t
tiku_rio_string(tiku_rio_reader_t *r, const char *s, char *out, size_t cap)
{
    size_t n = 0;

    if (!s) { r->status = TIKU_RICHIO_DAMAGED; return 0; }
    while (*s) {
        unsigned c = (unsigned char)*s++;

        if (c == '%') {
            if (!s[0] || !s[1] || digit(s[0]) > 15 || digit(s[1]) > 15) {
                r->status = TIKU_RICHIO_DAMAGED;
                return 0;
            }
            c = digit(s[0]) * 16 + digit(s[1]);
            s += 2;
            if (!c) { r->status = TIKU_RICHIO_DAMAGED; return 0; }
        }
        if (out) {
            if (n + 1 >= cap) { r->status = TIKU_RICHIO_DAMAGED; return 0; }
            out[n] = (char)c;
        }
        n++;
    }
    if (out && cap) out[n] = 0;
    return n;
}

/** @brief Whether a scalar is an XML 1.0 character. */
static int
xml_character(uint32_t c)
{
    return c == 9 || c == 10 || c == 13 ||
        (c >= 32 && c <= 0x10ffff && c != 0xfffe && c != 0xffff &&
         (c < 0xd800 || c > 0xdfff));
}

/** @brief Consume a strict UTF-8 scalar, returning zero on malformed input. */
static size_t
scalar(const unsigned char *s, size_t length)
{
    uint32_t c = s[0], minimum = 0;
    size_t count = 1, i;

    if (c >= 0xc2 && c <= 0xdf) count = 2, minimum = 0x80, c &= 31;
    else if (c >= 0xe0 && c <= 0xef) count = 3, minimum = 0x800, c &= 15;
    else if (c >= 0xf0 && c <= 0xf4)
        count = 4, minimum = 0x10000, c &= 7;
    else if (c >= 0x80) return 0;
    if (length < count) return 0;
    for (i = 1; i < count; i++) {
        if ((s[i] & 0xc0) != 0x80) return 0;
        c = (c << 6) | (s[i] & 63);
    }
    return c >= minimum && xml_character(c) ? count : 0;
}

/** @brief Validate a numeric or predefined entity and return its length. */
static size_t
reference(const unsigned char *s, size_t length)
{
    static const char *const named[] = {
        "&amp;", "&lt;", "&gt;", "&quot;", "&apos;"};
    size_t i = 2, start;
    uint32_t value = 0, base = 10;

    if (length < 3) return 0;
    if (s[1] != '#') {
        for (i = 0; i < 5; i++)
            if (length >= strlen(named[i]) &&
                !memcmp(s, named[i], strlen(named[i])))
                return strlen(named[i]);
        return 0;
    }
    if (s[i] == 'x') base = 16, i++;
    start = i;
    while (i < length && s[i] != ';') {
        unsigned d = digit(s[i]);

        if (i - start >= 8 || d >= base || value > (0x10ffffu - d) / base)
            return 0;
        value = value * base + d;
        i++;
    }
    return i > start && i < length && xml_character(value) ? i + 1 : 0;
}

tiku_richio_status_t
tiku_rio_preflight(const unsigned char *s, size_t length,
    const tiku_richio_options_t *options)
{
    static const char declaration[] =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>";
    size_t i = 0, step, poll = 0;
    int tag = 0, quote = 0;

    if (length >= 3 && !memcmp(s, "\xef\xbb\xbf", 3)) i = 3;
    if (length - i >= sizeof declaration - 1 &&
        !memcmp(s + i, declaration, sizeof declaration - 1))
        i += sizeof declaration - 1;
    while (i < length) {
        if (i >= poll) {
            if (options->cancel && options->cancel(options->ctx))
                return TIKU_RICHIO_CANCELLED;
            poll = i + 4096;
        }
        step = scalar(s + i, length - i);
        if (!step) return TIKU_RICHIO_DAMAGED;
        if (s[i] == '&') {
            step = reference(s + i, length - i);
            if (!step) return TIKU_RICHIO_DAMAGED;
        } else if (!quote && s[i] == '<') {
            if (i + 1 >= length) return TIKU_RICHIO_DAMAGED;
            if (s[i + 1] == '!' || s[i + 1] == '?')
                return TIKU_RICHIO_UNSUPPORTED;
            tag = 1;
        } else if (tag && (s[i] == '\'' || s[i] == '"')) {
            if (!quote) quote = s[i];
            else if (quote == s[i]) quote = 0;
        } else if (tag && !quote) {
            if (s[i] == ':' || (length - i >= 5 &&
                !memcmp(s + i, "xmlns", 5))) return TIKU_RICHIO_UNSUPPORTED;
            if (s[i] == '>') tag = 0;
        }
        i += step;
    }
    return tag || quote ? TIKU_RICHIO_DAMAGED : TIKU_RICHIO_OK;
}
