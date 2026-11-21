/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_word_model.h - validated developer-calculator model operations.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_WORD_MODEL_H_
#define TIKU_WORD_MODEL_H_
#include <stdint.h>
#include <stddef.h>
/** @brief Parse an unsigned 8/16/32/64-bit word in base 2/8/10/16; preserve out on error. */
int tiku_word_parse(const char *, unsigned base, int width, uint64_t *out);
/** @brief Format a word in base 2/8/10/16 into 65 bytes; reject invalid arguments. */
int tiku_word_format(uint64_t, unsigned base, char out[65]);
/** @brief Reverse bytes of a word at width 8/16/32/64; zero for invalid width. */
uint64_t tiku_word_swap(uint64_t, int width);
/** @brief Extract bits high through low; zero if low > high or high > 63. */
uint64_t tiku_word_extract(uint64_t, unsigned high, unsigned low);
/** @brief CRC-32/ISO-HDLC; data must contain bytes bytes, NULL allowed for zero. */
uint32_t tiku_word_crc32(const unsigned char *data, size_t bytes);
/** @brief CRC-16/CCITT-FALSE; data must contain bytes bytes, NULL allowed for zero. */
uint16_t tiku_word_crc16(const unsigned char *data, size_t bytes);
/** @brief Parse at most 128 hex characters into 64 bytes; output unspecified on error. */
int tiku_word_bytes(const char *, unsigned char out[64], size_t *bytes);
/** @brief Decode at most 255 source bytes as hex pairs or literal bytes into 255 bytes.
 * @return 1 on success; 0 for invalid arguments/input, preserving *bytes on failure.
 * Output contents are unspecified on failure; hex must be 0 or 1. */
int tiku_word_checksum_bytes(const char *, int hex, unsigned char out[255], size_t *bytes);
#endif
