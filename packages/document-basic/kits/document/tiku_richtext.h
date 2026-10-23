/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_richtext.h - Unicode 17 extended grapheme and default word boundaries.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_RICHTEXT_H_
#define TIKU_RICHTEXT_H_

#include "tiku_richdoc.h"

/** @brief Boundary before the byte at this offset. */
enum {
    TIKU_RICH_GRAPHEME = 1,
    TIKU_RICH_WORD = 2
};

/** @brief Compute Unicode 17.0.0 extended-grapheme/default-word boundaries.
 *  @note Flags needs length+1 bytes and must not overlap text. Start/end are
 *        boundaries; UTF-8 continuation bytes have no flags. Embedded NUL is
 *        accepted. Invalid input leaves flags unchanged. No allocation,
 *        normalization, locale tailoring or dictionary segmentation occurs.
 *        Calls use immutable tables and may run concurrently. */
tiku_rich_status_t tiku_richtext_boundaries(const char *text, size_t length,
                                            uint8_t *flags, size_t capacity);

#endif /* TIKU_RICHTEXT_H_ */
