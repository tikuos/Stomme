/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_font_utf8.inl - shared baked-font text operations.
 * SPDX-License-Identifier: Apache-2.0
 */
/**
 * @brief The next code point in @p text, stepping @p text past it.
 *
 * Text is UTF-8 (a file called "caf\u00e9" is five bytes and four
 * letters), so the text path walks code points, not bytes.  A malformed
 * sequence ends at the first byte that does not continue it, and never
 * past the terminator.
 */
static unsigned
utf8_next(const char **text)
{
    const unsigned char *p = (const unsigned char *)*text;
    unsigned cp = *p;
    int extra;

    if (cp < 0x80u) {
        extra = 0;
    } else if ((cp & 0xE0u) == 0xC0u) {
        cp &= 0x1Fu; extra = 1;
    } else if ((cp & 0xF0u) == 0xE0u) {
        cp &= 0x0Fu; extra = 2;
    } else if ((cp & 0xF8u) == 0xF0u) {
        cp &= 0x07u; extra = 3;
    } else {
        extra = 0;              /* a stray continuation byte stands alone */
    }
    while (extra-- > 0 && (p[1] & 0xC0u) == 0x80u) {
        cp = (cp << 6) | (unsigned)(*++p & 0x3Fu);
    }
    *text = (const char *)(p + 1);
    return cp;
}
