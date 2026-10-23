/* tiku_calculator_model.inl -- part of tiku_calculator.c: fixed-width
 * unsigned arithmetic, base conversion, and the two named CRC variants.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */

/** @brief The mask of a @p width-bit word; 64 bits gives all ones without
 *         shifting by 64. */
static uint64_t calc_mask(int width)
{ return width == 64 ? UINT64_MAX : (UINT64_C(1) << width) - 1; }

/** @brief Parse @p text as an unsigned @p width-bit value in @p base, an
 *         optional 0x or 0b prefix allowed.  @return 1, or 0 for empty,
 *         malformed or too large a value. */
static int calc_parse(const char *text, unsigned base, int width, uint64_t *out)
{
    uint64_t value = 0, limit = calc_mask(width);
    if (!text || !*text || (base != 2 && base != 8 && base != 10 && base != 16)) return 0;
    if (text[0] == '0' && ((base == 16 && (text[1] == 'x' || text[1] == 'X')) ||
        (base == 2 && (text[1] == 'b' || text[1] == 'B')))) text += 2;
    if (!*text) return 0;
    for (; *text; text++) {
        unsigned digit = *text >= '0' && *text <= '9' ? (unsigned)(*text - '0') :
            *text >= 'a' && *text <= 'f' ? (unsigned)(*text - 'a' + 10) :
            *text >= 'A' && *text <= 'F' ? (unsigned)(*text - 'A' + 10) : 99;
        if (digit >= base || value > (limit - digit) / base) return 0;
        value = value * base + digit;
    }
    *out = value; return 1;
}

/** @brief Write @p value in @p base into @p out, which holds 65 bytes. */
static void calc_format(uint64_t value, unsigned base, char *out)
{
    const char *digits = "0123456789ABCDEF"; char reversed[65]; int n = 0;
    do { reversed[n++] = digits[value % base]; value /= base; } while (value);
    for (int i = 0; i < n; i++) out[i] = reversed[n - 1 - i];
    out[n] = 0;
}

/** @brief @p value with the bytes of its @p width-bit word reversed; the
 *         bits inside a byte keep their order. */
static uint64_t calc_swap(uint64_t value, int width)
{
    uint64_t out = 0;
    for (int i = 0; i < width / 8; i++) { out = (out << 8) | (value & 255); value >>= 8; }
    return out;
}

/** @brief Bits @p high down to @p low of @p value, inclusive. */
static uint64_t calc_extract(uint64_t value, unsigned high, unsigned low)
{ return (value >> low) & calc_mask((int)(high - low + 1)); }

/** @brief CRC-32/ISO-HDLC of @p n bytes: reflected 0x04C11DB7, init and
 *         xorout FFFFFFFF. */
static uint32_t calc_crc32(const unsigned char *data, size_t n)
{
    uint32_t crc = UINT32_MAX;
    for (size_t i = 0; i < n; i++) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; bit++) crc = (crc >> 1) ^ ((crc & 1) ? 0xEDB88320u : 0);
    }
    return crc ^ UINT32_MAX;
}

/** @brief CRC-16/CCITT-FALSE of @p n bytes: poly 1021, init FFFF, no
 *         reflection or final XOR. */
static uint16_t calc_crc16(const unsigned char *data, size_t n)
{
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < n; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (int bit = 0; bit < 8; bit++) crc = (uint16_t)((crc << 1) ^ ((crc & 0x8000) ? 0x1021 : 0));
    }
    return crc;
}

/** @brief Hex byte pairs into @p out, spaces or tabs allowed between
 *         bytes.  @return 1, or 0 for a lone nibble or a non-hex
 *         character. */
static int calc_bytes(const char *text, unsigned char *out, size_t *n)
{
    int high = -1; *n = 0;
    for (; *text; text++) {
        if (*text == ' ' || *text == '\t') { if (high >= 0) return 0; continue; }
        char digit[2] = {*text, 0}; uint64_t value;
        if (!calc_parse(digit, 16, 8, &value)) return 0;
        if (high < 0) high = (int)value;
        else { out[(*n)++] = (unsigned char)(high * 16 + value); high = -1; }
    }
    return high < 0;
}
