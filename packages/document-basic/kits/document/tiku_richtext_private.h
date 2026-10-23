/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_richtext_private.h - Unicode property lookup and segmentation state.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_RICHTEXT_PRIVATE_H_
#define TIKU_RICHTEXT_PRIVATE_H_

#include "tiku_richtext.h"

/** @brief Grapheme_Cluster_Break property indices in the generated table. */
enum {
    G_OTHER, G_CR, G_LF, G_CONTROL, G_EXTEND, G_ZWJ, G_RI, G_PREPEND,
    G_SPACING, G_L, G_V, G_T, G_LV, G_LVT
};
/** @brief Word_Break property indices in the generated table. */
enum {
    W_OTHER, W_CR, W_LF, W_NEWLINE, W_EXTEND, W_ZWJ, W_RI, W_FORMAT,
    W_KATAKANA, W_HEBREW, W_LETTER, W_SINGLE, W_DOUBLE, W_MIDNUMLET,
    W_MIDLETTER, W_MIDNUM, W_NUMERIC, W_EXTENDNUMLET, W_SPACE
};
/** @brief Indic_Conjunct_Break property indices in the generated table. */
enum { I_NONE, I_CONSONANT, I_EXTEND, I_LINKER };
/** @brief Packed properties used by the two boundary algorithms. */
typedef struct {
    unsigned grapheme, word, indic; /**< property indices */
    int pictographic;              /**< Extended_Pictographic */
} tiku_rt_props_t;
/** @brief Constant-space context for an extended grapheme scan. */
typedef struct {
    unsigned previous;       /**< preceding grapheme property */
    unsigned regional;       /**< parity of preceding RI sequence */
    unsigned indic;          /**< zero, consonant or consonant-plus-linker */
    int pictographic;        /**< preceding EP Extend* sequence */
    int zwj_pictographic;    /**< preceding EP Extend* ZWJ sequence */
} tiku_rt_grapheme_t;
/** @brief Constant-space context for default word boundaries. */
typedef struct {
    unsigned raw;            /**< preceding property including ignored ones */
    unsigned previous;       /**< preceding non-ignored property */
    unsigned before;         /**< non-ignored property before previous */
    unsigned regional;       /**< parity of RI sequence ignoring WB4 */
} tiku_rt_word_t;

/** @brief Decode one scalar; INVALID_TEXT leaves the offset unchanged. */
tiku_rich_status_t tiku_rt_decode(const char *text, size_t length,
                                  size_t *offset, uint32_t *scalar);
/** @brief Look up packed properties; unassigned values use default classes. */
tiku_rt_props_t tiku_rt_properties(uint32_t scalar);
/** @brief Decide a grapheme boundary and consume the current property. */
int tiku_rt_grapheme(tiku_rt_grapheme_t *state, tiku_rt_props_t current);
/** @brief Decide a word boundary and consume the current property.
 *  @note Text after offset is already validated UTF-8. */
int tiku_rt_word(tiku_rt_word_t *state, tiku_rt_props_t current,
                 const char *text, size_t length, size_t offset);

#endif /* TIKU_RICHTEXT_PRIVATE_H_ */
