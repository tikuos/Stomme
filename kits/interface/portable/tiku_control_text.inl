/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_control_text.inl - bounded control labels and decimal integer text.
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stddef.h>
#include <limits.h>

/** @brief Copy a terminated label with byte truncation and a trailing terminator. */
static inline void tiku_control_label(char *out, size_t capacity, const char *text)
{
    size_t n = 0;
    if (!capacity) return;
    while (n + 1 < capacity && text[n]) { out[n] = text[n]; ++n; }
    out[n] = 0;
}

/** @brief Format an int as decimal, terminating any nonempty output span. */
static inline void tiku_control_decimal(char *out, size_t capacity, int value)
{
    char reversed[sizeof(int) * CHAR_BIT + 1];
    unsigned magnitude = value < 0 ? 0u - (unsigned)value : (unsigned)value;
    size_t n = 0, used = 0;
    if (!capacity) return;
    do { reversed[n++] = (char)('0' + magnitude % 10); magnitude /= 10; } while (magnitude);
    if (value < 0 && used + 1 < capacity) out[used++] = '-';
    while (n && used + 1 < capacity) out[used++] = reversed[--n];
    out[used] = 0;
}
