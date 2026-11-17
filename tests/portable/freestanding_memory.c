/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * freestanding_memory.c - memory operations for the service link probe.
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stddef.h>

/** @brief Copy bytes between disjoint ranges and return destination. */
void *memcpy(void *destination, const void *source, size_t bytes)
{
    unsigned char *to = destination;
    const unsigned char *from = source;
    size_t i;
    for (i = 0; i < bytes; ++i) to[i] = from[i];
    return destination;
}
/** @brief Fill a range with the low byte of value and return destination. */
void *memset(void *destination, int value, size_t bytes)
{
    unsigned char *to = destination;
    size_t i;
    for (i = 0; i < bytes; ++i) to[i] = (unsigned char)value;
    return destination;
}
