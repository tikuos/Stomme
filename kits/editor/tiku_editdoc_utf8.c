/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_editdoc_utf8.c - the UTF-8 the document accepts: well formed, no
 * overlong forms or surrogates, nothing past U+10FFFF, and no NUL.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_editdoc_private.h"

int
tiku_ed_utf8_length(unsigned char lead)
{
    if (lead < 0x80)
        return 1;
    if (lead >= 0xc2 && lead <= 0xdf)
        return 2;
    if (lead >= 0xe0 && lead <= 0xef)
        return 3;
    if (lead >= 0xf0 && lead <= 0xf4)
        return 4;
    return 0;
}

int
tiku_ed_utf8_valid(const char *s, size_t n)
{
    const unsigned char *p = (const unsigned char *)s;
    size_t i = 0;

    while (i < n) {
        unsigned char c = p[i];
        int len = tiku_ed_utf8_length(c);
        unsigned lo = 0x80, hi = 0xbf;
        int k;

        if (c == 0 || len == 0 || (size_t)len > n - i)
            return 0;
        if (len == 1) {
            i++;
            continue;
        }
        /* The second byte's range is what rules out overlong forms,
         * surrogates and code points past U+10FFFF. */
        if (c == 0xe0)
            lo = 0xa0;
        else if (c == 0xed)
            hi = 0x9f;
        else if (c == 0xf0)
            lo = 0x90;
        else if (c == 0xf4)
            hi = 0x8f;
        if (p[i + 1] < lo || p[i + 1] > hi)
            return 0;
        for (k = 2; k < len; k++)
            if ((p[i + k] & 0xc0) != 0x80)
                return 0;
        i += (size_t)len;
    }
    return 1;
}
