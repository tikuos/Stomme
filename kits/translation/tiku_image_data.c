/*
 * TikuOS Translation Kit.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_image_data.c - bounded embedded PNG resources without file or network I/O.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_image.h"
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/** @brief Base64 digit value, or -1 outside the alphabet. */
static int data_digit(unsigned char c)
{
    if (c >= 'A' && c <= 'Z') return c-'A';
    if (c >= 'a' && c <= 'z') return c-'a'+26;
    if (c >= '0' && c <= '9') return c-'0'+52;
    return c == '+' ? 62 : c == '/' ? 63 : -1;
}

/** @brief Read one PNG big-endian integer. */
static uint32_t data_u32(const unsigned char *p)
{
    return (uint32_t)p[0]<<24 | (uint32_t)p[1]<<16 | (uint32_t)p[2]<<8 | p[3];
}

/** @brief Validate base64 and the PNG header, optionally copying decoded bytes. */
static size_t data_bytes(const char *uri, size_t length, unsigned char *out,
                           int *wide, int *high)
{
    if (!uri || length < 22 || length > TIKU_IMAGE_DATA_URI_MAX ||
        strncasecmp(uri,"data:image/png;base64,",22)) return 0;
    int group[4], count = 0, ended = 0;
    unsigned char header[24] = {0}; size_t at = 0;
    for (size_t i = 22; i < length; i++) {
        unsigned char c = (unsigned char)uri[i];
        if (c && strchr(" \t\r\n\f",c)) continue;
        if (ended) return 0;
        group[count++] = c == '=' ? -2 : data_digit(c);
        if (count != 4) continue;
        if (group[0] < 0 || group[1] < 0 || group[2] == -1 || group[3] == -1 ||
            (group[2] == -2 && (group[3] != -2 || (group[1] & 15))) ||
            (group[2] >= 0 && group[3] == -2 && (group[2] & 3))) return 0;
        unsigned char bytes[3] = {(unsigned char)((group[0]<<2) | (group[1]>>4)),0,0};
        int n = 1;
        if (group[2] >= 0) {
            bytes[n++] = (unsigned char)((group[1]<<4) | (group[2]>>2));
            if (group[3] >= 0) bytes[n++] = (unsigned char)((group[2]<<6) | group[3]);
        }
        for (int k = 0; k < n; k++, at++) {
            if (at < sizeof header) header[at] = bytes[k];
            if (out) out[at] = bytes[k];
        }
        ended = group[3] == -2; count = 0;
    }
    if (count || at < sizeof header || memcmp(header,"\211PNG\r\n\032\n",8) ||
        data_u32(header+8) != 13 || memcmp(header+12,"IHDR",4)) return 0;
    uint32_t w = data_u32(header+16), h = data_u32(header+20);
    if (!w || !h || w > 2048 || h > 2048 || (uint64_t)w*h > TIKU_IMAGE_DATA_PIXELS_MAX) return 0;
    *wide = (int)w; *high = (int)h; return at;
}

int tiku_image_data_png_size(const char *uri, size_t length, int *w, int *h)
{
    return w && h && data_bytes(uri,length,NULL,w,h) != 0;
}

tiku_image_err_t tiku_image_data_png(const char *uri, size_t length, tiku_image_t *out)
{
    int w, h;
    if (!out || !data_bytes(uri,length,NULL,&w,&h)) return TIKU_IMG_BAD;
    unsigned char *bytes = malloc(length/4*3+3);
    if (!bytes) return TIKU_IMG_NO_MEMORY;
    size_t n = data_bytes(uri,length,bytes,&w,&h);
    tiku_image_t decoded = {0};
    tiku_image_err_t status = tiku_image_decode(bytes,n,&decoded);
    free(bytes);
    if (status != TIKU_IMG_OK) return status;
    if (decoded.w != w || decoded.h != h) { tiku_image_free(&decoded); return TIKU_IMG_BAD; }
    *out = decoded; return TIKU_IMG_OK;
}
