/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_image.h - reading a raster image into pixels, scaling it into a
 * thumbnail, and writing pixels as a PNG.
 *
 * BMP (uncompressed, 24 or 32 bits), PNG (1 to 8 bits a sample, Adam7
 * included) and JPEG (sequential or progressive), told apart by content.
 * Both file and memory entry points bound encoded bytes and decoded pixels.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_IMAGE_H_
#define TIKU_IMAGE_H_

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/** @brief The most encoded bytes a load or a decode takes. */
#define TIKU_IMAGE_BYTES_MAX (32u * 1024u * 1024u)
/** @brief The most pixels an image may decode to, or a PNG be written
 *         from. */
#define TIKU_IMAGE_PIXELS_MAX (16u * 1024u * 1024u)

/** @brief Decoded pixels, 32-bit, 0xAARRGGBB with alpha not
 *         premultiplied, top row first. */
typedef struct {
    int       w, h;
    uint32_t *px;                   /* w*h, owned; tiku_image_free()    */
} tiku_image_t;

/** @brief Why a load did not produce pixels. */
typedef enum {
    TIKU_IMG_OK = 0,
    TIKU_IMG_NOT_IMAGE,         /* not a recognised format           */
    TIKU_IMG_UNSUPPORTED,       /* recognised, a variant not decoded */
    TIKU_IMG_BAD,               /* malformed, too big, unreadable or
                                   non-PNG allocation failure        */
    TIKU_IMG_NO_MEMORY          /* PNG allocation failure            */
} tiku_image_err_t;

/**
 * @brief Whether @p path is a regular file holding a PNG, JPEG or BMP, by
 *        its first bytes.
 *
 * The name is not consulted: a .txt file holding a PNG is a PNG, and an
 * empty .jpg file is not a picture.
 */
int tiku_image_is_image(const char *path);

/**
 * @brief Read @p path, at most TIKU_IMAGE_BYTES_MAX bytes, into @p out.
 *
 * @return TIKU_IMG_OK on success; @p out is untouched otherwise.
 * @note An image already in @p out is overwritten, not freed.  A path
 *       that is not a regular file, such as a FIFO or a device, is
 *       refused without waiting.
 */
tiku_image_err_t tiku_image_load(const char *path,
                                         tiku_image_t *out);

/**
 * @brief Decode @p len bytes of an image into @p out, under the same
 *        limits as tiku_image_load(); the bytes stay the caller's.
 * @return TIKU_IMG_OK with @p out holding pixels it owns; otherwise @p out
 *         is untouched.
 * @note An image already in @p out is overwritten, not freed.
 */
tiku_image_err_t tiku_image_decode(const void *bytes, size_t len,
                                  tiku_image_t *out);

/** @brief Encoded data-URI budget, including its MIME prefix and whitespace. */
#define TIKU_IMAGE_DATA_URI_MAX (1024u * 1024u)
/** @brief Pixel budget for embedded PNG resources. */
#define TIKU_IMAGE_DATA_PIXELS_MAX (1024u * 1024u)
/** @brief Validate a bounded base64 PNG URI and read its dimensions without allocation.
 *  @return 1 with dimensions at most 2048 each and within the pixel budget; 0 otherwise. */
int tiku_image_data_png_size(const char *uri, size_t length, int *w, int *h);
/** @brief Decode a base64 PNG URI within its encoded, dimension and pixel budgets.
 *  @return An image status; failure leaves @p out untouched. */
tiku_image_err_t tiku_image_data_png(const char *uri, size_t length, tiku_image_t *out);

/** @brief Free @p im's pixels and empty it; NULL is ignored. */
void tiku_image_free(tiku_image_t *im);

/**
 * @brief @p w by @p h pixels, 0xAARRGGBB, written to @p path as a PNG:
 *        RGBA when @p alpha is set, else RGB with the top byte ignored,
 *        each row under the filter whose bytes, read as signed, have the
 *        smallest absolute sum.
 * @return 0, or -1 when it could not be made or written.
 */
int tiku_png_write(const char *path, const uint32_t *px, int w, int h,
                   int alpha);
/** @brief @p w by @p h pixels written as tiku_png_write() writes them,
 *         to the open @p stream, which is neither flushed nor closed.
 *  @return 0, or -1 as tiku_png_write(). */
int tiku_png_write_stream(FILE *stream, const uint32_t *px, int w, int h,
                          int alpha);

/** @brief Exact byte count for the allocation-free RGBA PNG stream, or zero.
 *  @note Dimensions are positive and bounded by TIKU_IMAGE_PIXELS_MAX. */
size_t tiku_png_rgba_size(unsigned width, unsigned height);
/** @brief Stream straight-alpha ARGB pixels as an 8-bit RGBA PNG.
 *  @note Uses uncompressed DEFLATE blocks and fixed-size stack storage;
 *        no heap allocation or file I/O. emit returns zero to continue,
 *        nonzero to stop; supplied bytes are borrowed during the callback.
 *        The caller owns any partial output on refusal and must discard it.
 *        Input pixels must remain unchanged during callbacks.
 *  @return Zero on success, -1 for invalid input or callback refusal. */
int tiku_png_rgba_stream(const uint32_t *pixels, unsigned width,
    unsigned height, int (*emit)(void *, const void *, size_t), void *context);

/**
 * @brief Scale @p src into a @p size square, keeping its aspect ratio.
 *
 * The longer side fills the square and the picture is centred, with the
 * margins left fully transparent.  Each pixel copies one source pixel,
 * without averaging.
 *
 * @return 0 on success, -1 for a bad argument or a failed allocation.
 */
int tiku_image_thumb(const tiku_image_t *src, int size,
                         tiku_image_t *out);

#endif /* TIKU_IMAGE_H_ */
