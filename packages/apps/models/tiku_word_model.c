/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_word_model.c - shared word conversion, bit extraction and checksums.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_word_model.h"
#include "tiku_calculator_model.inl"

/** @brief Supported developer-calculator word widths. */
static int valid_width(int width)
{ return width == 8 || width == 16 || width == 32 || width == 64; }
/** @brief Supported developer-calculator bases. */
static int valid_base(unsigned base)
{ return base == 2 || base == 8 || base == 10 || base == 16; }

int tiku_word_parse(const char *text, unsigned base, int width, uint64_t *out)
{ return out && valid_width(width) && valid_base(base) && calc_parse(text, base, width, out); }
int tiku_word_format(uint64_t word, unsigned base, char out[65])
{
    if (!out || !valid_base(base)) return 0;
    calc_format(word, base, out);
    return 1;
}
uint64_t tiku_word_swap(uint64_t word, int width)
{ return valid_width(width) ? calc_swap(word, width) : 0; }
uint64_t tiku_word_extract(uint64_t word, unsigned high, unsigned low)
{ return high < 64 && low <= high ? calc_extract(word, high, low) : 0; }
uint32_t tiku_word_crc32(const unsigned char *data, size_t bytes)
{ return calc_crc32(data, bytes); }
uint16_t tiku_word_crc16(const unsigned char *data, size_t bytes)
{ return calc_crc16(data, bytes); }
int tiku_word_bytes(const char *text, unsigned char out[64], size_t *bytes)
{
    size_t length = 0;
    if (!text || !out || !bytes) return 0;
    while (length <= 128 && text[length]) ++length;
    return length <= 128 && calc_bytes(text, out, bytes);
}

int tiku_word_checksum_bytes(const char *text,int hex,unsigned char out[255],size_t *bytes)
{
    if(!text||!out||!bytes||(hex!=0&&hex!=1))return 0;
    size_t n=0;while(n<=255&&text[n])++n;
    if(n>255)return 0;
    if(hex){if(!calc_bytes(text,out,&n))return 0;}
    else for(size_t i=0;i<n;++i)out[i]=(unsigned char)text[i];
    *bytes=n;return 1;
}
