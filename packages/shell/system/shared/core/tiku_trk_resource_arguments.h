/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_resource_arguments.h - exact native action arguments and byte
 * chunks.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_RESOURCE_ARGUMENTS_H_
#define TIKU_TRK_RESOURCE_ARGUMENTS_H_
#include "tiku_trk_resource_values.h"
#include <stdint.h>
#define TRK_TEXT(o, k, v)                                                     \
    trk_value_member(o, k, tiku_resource_value_string(TIKU_RESOURCE_TEXT, v))
#define TRK_INT(o, k, v)                                                      \
    trk_value_member(o, k,                                                    \
                     tiku_resource_value_integer(TIKU_RESOURCE_INTEGER, v))
#define TRK_BOOL(o, k, v)                                                     \
    trk_value_member(                                                         \
        o, k, tiku_resource_value_integer(TIKU_RESOURCE_BOOLEAN, (v) != 0))
/** @brief Append a canonical unsigned counter as typed text. */
static inline int trk_counter(tiku_msg_t *out,const char *key,uint64_t value)
{
    char text[32];
    snprintf(text, sizeof text, "%llu", (unsigned long long)value);
    return TRK_TEXT(out, key, text);
}
/** @brief Reject unknown or duplicate members and require a typed object. */
int trk_arguments_keys(const tiku_msg_t *,const char *const *keys,size_t count);
/** @brief Copy one exact text member; absent or oversized values return zero.
 */
int trk_arguments_text(const tiku_msg_t *, const char *key, char *out,
                       size_t capacity);
/** @brief Read a required bounded integer. */
int trk_arguments_integer(const tiku_msg_t *, const char *key, int64_t low,
                          int64_t high, int64_t *out);
/** @brief Read a required canonical unsigned text counter. */
int trk_arguments_counter(const tiku_msg_t *,const char *key,uint64_t *out);
/** @brief Whether an object contains a named member. */
int trk_arguments_has(const tiku_msg_t *,const char *key);
/** @brief Decode canonical padded base64 without whitespace; return byte
 * count or -1.
 */
int trk_base64_decode(const char *text,void *out,size_t capacity);
/** @brief Encode bytes with padding and NUL; return text length or -1. */
int trk_base64_encode(const void *bytes,size_t size,char *out,size_t capacity);
#endif
