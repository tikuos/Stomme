/*
 * TikuDesktop Translation Kit.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_richhtml.h - bounded document HTML export and conversion notices.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_RICHHTML_H_
#define TIKU_RICHHTML_H_
#include "tiku_richio.h"

/** @brief Export differences requiring a caller-visible notice. */
enum {
    TIKU_RICHHTML_PAGINATION = 1u << 0, /**< browser controls page layout */
    TIKU_RICHHTML_STYLES = 1u << 1, /**< named styles become resolved CSS */
    TIKU_RICHHTML_IMAGES = 1u << 2, /**< reserved; pictures are embedded */
    TIKU_RICHHTML_BOOKMARKS = 1u << 3 /**< named point targets are omitted */
};

/** @brief Export UTF-8 HTML with resolved formatting, lists and nested tables.
 *  @note No scripts, external resources or file I/O. Links keep validated
 *        targets. Pictures embed RGBA PNG data. Page/keep rules are CSS hints.
 *        options.xml_bytes limits HTML bytes; memory and cancellation follow
 *        the native codec. No round-trip import or exact pagination promise.
 *  @return OK with malloc-owned NUL-terminated output and notice bits;
 *          failure clears output, length and optional notices. */
tiku_richio_status_t tiku_richhtml_write(
    const tiku_richsnapshot_t *snapshot, const tiku_richio_options_t *options,
    unsigned char **out, size_t *length, unsigned *notices);
#endif
