/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_clipboard.c - atomic text and typed-data clipboard representations.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tiku_clipboard.h"

static char    *held;              /* NULL when empty                   */
static size_t   held_len;          /* its bytes, the NUL apart          */
static unsigned stamp;
/** @brief Owned binary representations, ordered by consumer preference. */
typedef struct {
    char mime[128];
    void *data;
    size_t bytes;
} held_format_t;
static held_format_t held_formats[TIKU_CLIPBOARD_FORMATS];

/** @brief Remove the binary representation without advancing the stamp. */
static void
clear_data(void)
{
    for (size_t i = 0; i < TIKU_CLIPBOARD_FORMATS; i++)
        free(held_formats[i].data);
    memset(held_formats, 0, sizeof held_formats);
}

const char *
tiku_clipboard_status_text(int status)
{
    static char too_large[96];

    switch (status) {
    case TIKU_CLIPBOARD_OK: return "";
    case TIKU_CLIPBOARD_TOO_LARGE:
        snprintf(too_large, sizeof too_large, "Host clipboard exceeds the "
                 "%u MB transfer limit.", TIKU_CLIPBOARD_LIMIT / (1024u * 1024u));
        return too_large;
    case TIKU_CLIPBOARD_TIMEOUT:
        return "Host clipboard did not respond. Try copying again.";
    case TIKU_CLIPBOARD_INVALID:
        return "Host clipboard contains invalid data; nothing was pasted.";
    default: return "Host clipboard has no supported text to paste.";
    }
}

/** @brief Hold @p n bytes of @p text in place of what was held, and move
 *         the stamp; empty when @p n is 0 or there is no memory for them.
 *  @return whether it holds them. */
static int
hold(const char *text, size_t n)
{
    char *copy = n != 0u ? malloc(n + 1u) : NULL;

    if (copy != NULL) {
        memcpy(copy, text, n);
        copy[n] = '\0';
    }
    free(held);
    clear_data();
    held = copy;
    held_len = copy != NULL ? n : 0u;
    stamp++;
    return n == 0u || copy != NULL;
}

void
tiku_clipboard_set(const char *text)
{
    size_t n = (text != NULL) ? strlen(text) : 0u;

    if (n > TIKU_CLIPBOARD_LIMIT) {
        n = TIKU_CLIPBOARD_LIMIT;
        /* Cut before a character, not inside one. */
        while (n && ((unsigned char)text[n] & 0xc0) == 0x80) n--;
    }
    (void)hold(text, n);
}

/** @brief Check complete UTF-8 without changing the clipboard. */
static int
valid_text(const char *text, size_t length)
{
    if ((!text && length) || length > TIKU_CLIPBOARD_LIMIT) return 0;
    for (size_t i = 0; i < length;) {
        unsigned char c = (unsigned char)text[i++];
        unsigned cp, minimum;
        int extra;
        if (!c) return 0;
        if (c < 0x80) continue;
        if (c >= 0xc2 && c <= 0xdf) { cp = c & 31; extra = 1; minimum = 0x80; }
        else if (c >= 0xe0 && c <= 0xef) { cp = c & 15; extra = 2; minimum = 0x800; }
        else if (c >= 0xf0 && c <= 0xf4) { cp = c & 7; extra = 3; minimum = 0x10000; }
        else return 0;
        while (extra--) {
            if (i == length || ((unsigned char)text[i] & 0xc0) != 0x80) return 0;
            cp = (cp << 6) | ((unsigned char)text[i++] & 63);
        }
        if (cp < minimum || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) return 0;
    }
    return 1;
}

/** @brief Check a bounded MIME token pair without parameters or whitespace. */
static int
valid_mime(const char *mime)
{
    size_t n = 0, slash = 0;

    if (!mime) return 1;
    while (n < sizeof held_formats[0].mime && mime[n]) {
        unsigned char c = (unsigned char)mime[n];

        if (c == '/') {
            if (!n || slash) return 0;
            slash = n;
        } else if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                     (c >= '0' && c <= '9') || strchr("!#$&^_.+-", c)))
            return 0;
        n++;
    }
    return n < sizeof held_formats[0].mime && slash && slash + 1 < n;
}

int
tiku_clipboard_import_formats(const char *text, size_t length,
    const tiku_clipboard_format_t *formats, size_t count)
{
    held_format_t prepared[TIKU_CLIPBOARD_FORMATS] = {0};
    char *copy;
    size_t total = length, i, j;

    if (count > TIKU_CLIPBOARD_FORMATS || (count && !formats) ||
        !valid_text(text, length))
        return 0;
    for (i = 0; i < count; i++) {
        if (!formats[i].mime || !valid_mime(formats[i].mime) || !formats[i].data ||
            !formats[i].bytes || formats[i].bytes > TIKU_CLIPBOARD_LIMIT - total)
            return 0;
        for (j = 0; j < i; j++)
            if (!strcmp(formats[i].mime, formats[j].mime)) return 0;
        total += formats[i].bytes;
    }
    for (i = 0; i < count; i++) {
        strcpy(prepared[i].mime, formats[i].mime);
        prepared[i].bytes = formats[i].bytes;
        prepared[i].data = malloc(formats[i].bytes);
        if (!prepared[i].data) goto fail;
        memcpy(prepared[i].data, formats[i].data, formats[i].bytes);
    }
    copy = malloc(length + 1);
    if (!copy) goto fail;
    if (length) memcpy(copy, text, length);
    copy[length] = 0;
    free(held);
    clear_data();
    held = copy;
    held_len = length;
    memcpy(held_formats, prepared, sizeof prepared);
    stamp++;
    return 1;
fail:
    for (i = 0; i < count; i++) free(prepared[i].data);
    return 0;
}

int
tiku_clipboard_import_typed(const char *text, size_t length,
    const char *mime, const void *data, size_t bytes)
{
    tiku_clipboard_format_t format = {mime, data, bytes};

    if (!mime && bytes) return 0;
    return tiku_clipboard_import_formats(text, length, &format, mime ? 1 : 0);
}

int
tiku_clipboard_import(const char *text, size_t length)
{
    return tiku_clipboard_import_typed(text, length, NULL, NULL, 0);
}

const void *
tiku_clipboard_data(const char **mime, size_t *bytes)
{
    if (mime) *mime = held_formats[0].data ? held_formats[0].mime : NULL;
    if (bytes) *bytes = held_formats[0].bytes;
    return held_formats[0].data;
}

const void *
tiku_clipboard_find(const char *mime, size_t *bytes)
{
    if (bytes) *bytes = 0;
    if (!mime) return NULL;
    for (size_t i = 0; i < TIKU_CLIPBOARD_FORMATS; i++)
        if (held_formats[i].data && !strcmp(mime, held_formats[i].mime)) {
            if (bytes) *bytes = held_formats[i].bytes;
            return held_formats[i].data;
        }
    return NULL;
}

const char *
tiku_clipboard_text(void)
{
    return held != NULL ? held : "";
}

size_t
tiku_clipboard_length(void)
{
    return held_len;
}

unsigned
tiku_clipboard_stamp(void)
{
    return stamp;
}
