/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_jpx.h - JPEG 2000 decoding (ISO/IEC 15444-1): a picture from a
 * codestream, bare or in its JP2 file.
 *
 * The scans and photographs some PDFs carry (JPXDecode): wavelets of
 * each component, coded a bitplane at a time in small blocks.  Each
 * channel comes out on the finest component's grid, eight bits a
 * sample; a picture too big comes without its finest resolutions.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_JPX_H_
#define TIKU_JPX_H_

#define TIKU_JPX_COMPS 16           /* components given back, at most   */

/** @brief How the components are to be seen, as the file says. */
enum {
    TIKU_JPX_UNKNOWN,               /* none named: decode picks by count */
    TIKU_JPX_GRAY,
    TIKU_JPX_SRGB,
    TIKU_JPX_SYCC,                  /* Y, Cb, Cr, left unconverted       */
    TIKU_JPX_CMYK,
    TIKU_JPX_OTHER                  /* an ICC profile or a space unread  */
};

/** @brief A decoded picture. */
typedef struct {
    int            w, h;            /* its size in samples               */
    int            ncomp;           /* components, a palette's applied   */
    int            prec[TIKU_JPX_COMPS];  /* each one's bits, as coded   */
    unsigned char *comp[TIKU_JPX_COMPS];  /* w * h samples, 0 to 255 or,
                                              under 8 bits, as coded     */
    int            space;           /* TIKU_JPX_GRAY ...                 */
    int            alpha;           /* the opacity component, or -1      */
    int            premultiplied;   /* the colours are times the alpha   */
} tiku_jpx_t;

/**
 * @brief Decode a JP2 file or a bare codestream: its first codestream,
 *        every tile, each component at full resolution unless one would
 *        have more than @p max_pixels samples, or all of them more than
 *        four times that, then as many resolutions down as make them fit.
 * @return 0, or -1 when it will not read or cannot come down that far.
 * @note After a 0, the caller gives @p out back with tiku_jpx_free().
 */
int tiku_jpx_decode(const unsigned char *data, long n, long max_pixels,
                    tiku_jpx_t *out);

/** @brief What tiku_jpx_decode() gave, given back. */
void tiku_jpx_free(tiku_jpx_t *img);

#endif /* TIKU_JPX_H_ */
