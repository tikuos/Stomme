/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_richdoc_utf8.c - bounded UTF-8 and paragraph text validation.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richdoc_private.h"

#include <string.h>

tiku_rich_status_t
tiku_rd_utf8(const char *text, size_t length, int paragraph)
{
    size_t at = 0, need, k;
    uint32_t cp, minimum;
    unsigned lead;

    if (length && !text)
        return TIKU_RICH_INVALID;
    while (at < length) {
        lead = (unsigned char)text[at++];
        if (lead < 0x80) {
            cp = lead;
            need = 0;
            minimum = 0;
        } else if (lead >= 0xc2 && lead <= 0xdf) {
            cp = lead & 31;
            need = 1;
            minimum = 0x80;
        } else if (lead >= 0xe0 && lead <= 0xef) {
            cp = lead & 15;
            need = 2;
            minimum = 0x800;
        } else if (lead >= 0xf0 && lead <= 0xf4) {
            cp = lead & 7;
            need = 3;
            minimum = 0x10000;
        } else {
            return TIKU_RICH_INVALID_TEXT;
        }
        if (need > length - at)
            return TIKU_RICH_INVALID_TEXT;
        for (k = 0; k < need; k++) {
            unsigned b = (unsigned char)text[at++];

            if ((b & 0xc0) != 0x80)
                return TIKU_RICH_INVALID_TEXT;
            cp = (cp << 6) | (b & 63);
        }
        if (!cp || cp < minimum || cp > 0x10ffff ||
            (cp >= 0xd800 && cp <= 0xdfff))
            return TIKU_RICH_INVALID_TEXT;
        if (paragraph && (cp == '\r' ||
            (cp < 0x20 && cp != '\t' && cp != '\n')))
            return TIKU_RICH_INVALID_TEXT;
        if (paragraph == 1 && cp == 0xfffc)
            return TIKU_RICH_UNSUPPORTED;
    }
    return TIKU_RICH_OK;
}

int
tiku_rd_boundary(const char *text, size_t length, size_t offset)
{
    return offset <= length && (!offset || offset == length ||
                                ((unsigned char)text[offset] & 0xc0) != 0x80);
}

int
tiku_rd_name(const char name[64])
{
    size_t n = 0, k;

    while (n < 64 && name[n])
        n++;
    if (!n || n == 64 || tiku_rd_utf8(name, n, 0) != TIKU_RICH_OK)
        return 0;
    for (k = 0; k < n; k++)
        if ((unsigned char)name[k] < 0x20 || name[k] == 0x7f)
            return 0;
    return 1;
}
