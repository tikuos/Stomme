/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_jpeg.h - JPEG decoding, sequential and progressive.
 *
 * Huffman-coded 8-bit JPEGs, sequential or progressive: grey, YCbCr, and
 * four-ink CMYK and YCCK.  A file cut short shows the scans that came,
 * except one coded in a single sequential scan, which is refused.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_JPEG_H_
#define TIKU_JPEG_H_

#include <stdint.h>

/**
 * @brief Decode a JPEG into 0xAARRGGBB pixels.
 *
 * @param out  Receives a malloc'd buffer of w*h pixels.
 * @return 0 on success, 1 when the file is a JPEG this cannot decode
 *         (arithmetic, lossless, hierarchical, 12-bit), -1 when it is
 *         malformed or too large.
 * @note The caller frees @p *out.
 */
int tiku_jpeg_decode(const unsigned char *src, long n, int *w, int *h,
                         uint32_t **out);

/**
 * @brief Decode as tiku_jpeg_decode() does, reduced to the size
 *        tiku_image_fit_size() gives for @p max_w by @p max_h.
 *
 * The component planes keep 1/2, 1/4 or 1/8 of each block's samples, each
 * the mean of the samples it covers: the smallest planes still at least the
 * reduced size.  Each output pixel is the plane sample under it.
 */
int tiku_jpeg_decode_fit(const unsigned char *src, long n, int max_w,
                         int max_h, int *w, int *h, uint32_t **out);

#endif /* TIKU_JPEG_H_ */
