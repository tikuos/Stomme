/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_jbig2.h - JBIG2 decoding (ITU-T T.88): a page of a scan from the
 * segments that code it.
 *
 * JBIG2 codes black-and-white pages, such as scanned datasheets: regions
 * of bits coded against their neighbours, and text as glyphs coded once
 * and placed many times.  Rows of bits come out, one bit a pixel, 1
 * black, as a PDF's JBIG2Decode reads them before inverting.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_JBIG2_H_
#define TIKU_JBIG2_H_

/**
 * @brief The page of an embedded stream (ISO 32000 7.4.7): the segments
 *        of @p glob (JBIG2Globals; NULL for none), then those of
 *        @p data.  A segment that will not read ends the page there.
 * @param out  Receives rows of (*w + 7) / 8 bytes, malloc'd.
 * @return the bytes made, or -1 when no page reads or @p omax is short.
 */
long tiku_jbig2_decode(const unsigned char *glob, long gn,
                       const unsigned char *data, long n,
                       unsigned char **out, long omax, int *w, int *h);

/** @brief The same for page @p page (from 1) of a JBIG2 file, its
 *         header first, laid out sequentially or random-access. */
long tiku_jbig2_file(const unsigned char *data, long n, int page,
                     unsigned char **out, long omax, int *w, int *h);

#endif /* TIKU_JBIG2_H_ */
