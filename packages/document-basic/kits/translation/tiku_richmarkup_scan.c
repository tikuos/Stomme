/*
 * TikuDesktop Translation Kit.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_richmarkup_scan.c - bounded HTML tags and character references.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richmarkup_private.h"
#include <ctype.h>
#include <string.h>

/** @brief HTML ASCII whitespace, independent of the current locale. */
static int
space(unsigned char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
}

/** @brief Lowercase one ASCII tag/name byte, leaving UTF-8 unchanged. */
static char
lower(char c)
{
    return c >= 'A' && c <= 'Z' ? (char)(c + 'a' - 'A') : c;
}

int
tiku_rm_tag(tiku_rimp_t *r, const char *data, size_t length,
             size_t *position, tiku_rm_tag_t *tag)
{
    size_t at = *position + 1, n = 0;

    memset(tag, 0, sizeof *tag);
    if (at < length && data[at] == '/') { tag->closing = 1; at++; }
    while (at < length && !space(data[at]) && data[at] != '>' &&
           data[at] != '/') {
        if (n + 1 >= sizeof tag->name) goto limit;
        tag->name[n++] = lower(data[at++]);
    }
    if (!n) goto damaged;
    while (at < length && tiku_rimp_active(r)) {
        while (at < length && space(data[at])) at++;
        if (at < length && data[at] == '/') {
            tag->empty = 1;
            at++;
        }
        if (at < length && data[at] == '>') {
            *position = at + 1;
            return 1;
        }
        if (tag->empty || tag->closing || at == length) goto damaged;
        if (tag->count == 64) goto limit;
        tiku_rm_attr_t *a = &tag->attributes[tag->count++];

        n = 0;
        while (at < length && !space(data[at]) && data[at] != '=' &&
               data[at] != '>' && data[at] != '/') {
            if (n + 1 >= sizeof a->name) goto limit;
            a->name[n++] = lower(data[at++]);
        }
        if (!n) goto damaged;
        for (size_t i = 0; i + 1 < tag->count; i++)
            if (!strcmp(tag->attributes[i].name, a->name)) goto damaged;
        while (at < length && space(data[at])) at++;
        a->value = "";
        if (at < length && data[at] == '=') {
            at++;
            while (at < length && space(data[at])) at++;
            char quote = at < length && (data[at] == '\'' || data[at] == '"') ?
                         data[at++] : 0;

            a->value = data + at;
            while (at < length && (quote ? data[at] != quote :
                   !space(data[at]) && data[at] != '>')) {
                if (data[at] == '<') goto damaged;
                if (!(at % 4096) && !tiku_rimp_active(r)) return 0;
                at++;
            }
            a->length = (size_t)(data + at - a->value);
            if (quote) {
                if (at == length) goto damaged;
                at++;
            }
        }
    }
damaged:
    if (r->status == TIKU_RICHIO_OK) r->status = TIKU_RICHIO_DAMAGED;
    return 0;
limit:
    r->status = TIKU_RICHIO_LIMIT;
    return 0;
}

const tiku_rm_attr_t *
tiku_rm_attr(const tiku_rm_tag_t *tag, const char *name)
{
    for (size_t i = 0; i < tag->count; i++)
        if (!strcmp(tag->attributes[i].name, name)) return &tag->attributes[i];
    return NULL;
}

/** @brief Encode one validated Unicode scalar in one to four bytes. */
static size_t
scalar(char *out, unsigned value)
{
    if (value < 128) { out[0] = (char)value; return 1; }
    if (value < 2048) {
        out[0] = (char)(0xc0 | value >> 6);
        out[1] = (char)(0x80 | (value & 63));
        return 2;
    }
    if (value < 65536) {
        out[0] = (char)(0xe0 | value >> 12);
        out[1] = (char)(0x80 | ((value >> 6) & 63));
        out[2] = (char)(0x80 | (value & 63));
        return 3;
    }
    out[0] = (char)(0xf0 | value >> 18);
    out[1] = (char)(0x80 | ((value >> 12) & 63));
    out[2] = (char)(0x80 | ((value >> 6) & 63));
    out[3] = (char)(0x80 | (value & 63));
    return 4;
}

char *
tiku_rm_decode(tiku_rimp_t *r, const char *text, size_t length)
{
    static const struct { const char *name; unsigned value; } entities[] = {
        {"amp", 38}, {"lt", 60}, {"gt", 62}, {"quot", 34}, {"apos", 39},
        {"nbsp", 160}, {"copy", 169}, {"reg", 174}, {"ndash", 8211},
        {"mdash", 8212}, {"hellip", 8230}, {"bull", 8226},
        {"lsquo", 8216}, {"rsquo", 8217}, {"ldquo", 8220}, {"rdquo", 8221}
    };
    char *out = tiku_rimp_string(r, text, length);
    size_t used = 0;

    if (!out) return NULL;
    for (size_t i = 0; i < length;) {
        if (!(i % 4096) && !tiku_rimp_active(r)) return NULL;
        if (text[i] != '&') { out[used++] = text[i++]; continue; }
        size_t end = i + 1;
        unsigned value = 0;

        while (end < length && end - i < 32 && text[end] != ';' &&
               !space(text[end]) && text[end] != '&') end++;
        if (end < length && text[end] == ';') {
            if (text[i + 1] == '#') {
                size_t at = i + 2;
                unsigned base = 10;

                if (at < end && (text[at] == 'x' || text[at] == 'X')) {
                    base = 16;
                    at++;
                }
                if (at == end) goto bad;
                for (; at < end; at++) {
                    char c = lower(text[at]);
                    unsigned digit = c >= '0' && c <= '9' ? (unsigned)(c-'0') :
                        c >= 'a' && c <= 'f' ? (unsigned)(c-'a'+10) : base;

                    if (digit >= base || value > (0x10ffff - digit) / base)
                        goto bad;
                    value = value * base + digit;
                }
                if (!value || (value >= 0xd800 && value <= 0xdfff)) goto bad;
            } else {
                for (size_t j = 0; j < sizeof entities / sizeof *entities; j++)
                    if (strlen(entities[j].name) == end-i-1 &&
                        !memcmp(text+i+1, entities[j].name, end-i-1))
                        value = entities[j].value;
            }
        }
        if (value) { used += scalar(out + used, value); i = end + 1; }
        else { r->notices |= TIKU_RICHMARKUP_SYNTAX; out[used++] = text[i++]; }
    }
    out[used] = 0;
    return out;
bad:
    r->status = TIKU_RICHIO_DAMAGED;
    return NULL;
}
