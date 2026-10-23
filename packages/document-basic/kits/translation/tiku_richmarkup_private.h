/*
 * TikuDesktop Translation Kit.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_richmarkup_private.h - bounded HTML tokens and inherited formatting.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_RICHMARKUP_PRIVATE_H_
#define TIKU_RICHMARKUP_PRIVATE_H_
#include "tiku_richimport_private.h"

/** @brief Borrowed HTML attribute, with a lowercase ASCII name. */
typedef struct {
    char name[48];             /**< lowercase ASCII attribute name */
    const char *value;         /**< borrowed encoded source value */
    size_t length;             /**< value byte count, excluding delimiters */
} tiku_rm_attr_t;

/** @brief One bounded start/end tag; values still contain entity references. */
typedef struct {
    char name[32];             /**< lowercase ASCII element name */
    int closing, empty;
    tiku_rm_attr_t attributes[64]; /**< bounded borrowed attributes */
    size_t count;              /**< initialized attribute count */
} tiku_rm_tag_t;

/** @brief Formatting inherited through an HTML element stack. */
typedef struct {
    tiku_rich_chars_t chars;   /**< inherited character properties */
    tiku_rich_para_t paragraph; /**< inherited paragraph properties */
    tiku_rich_list_t list;     /**< current list membership */
    const char *link;          /**< stable decoded target, or NULL */
    unsigned heading;         /**< named heading style index, or zero */
    int pre;                  /**< preserve text whitespace */
    int image;                /**< width/height CSS applies to an IMG element */
    float image_width, image_height;
} tiku_rm_style_t;

/** @brief Parse a tag beginning at *position; advance on success. */
int tiku_rm_tag(tiku_rimp_t *r, const char *data, size_t length,
                size_t *position, tiku_rm_tag_t *tag);
/** @brief Find a borrowed attribute by lowercase name. */
const tiku_rm_attr_t *tiku_rm_attr(const tiku_rm_tag_t *tag, const char *name);
/** @brief Decode HTML character references to an owned UTF-8 string. */
char *tiku_rm_decode(tiku_rimp_t *r, const char *text, size_t length);
/** @brief Apply supported inline CSS declarations, reporting other properties. */
void tiku_rm_css(tiku_rimp_t *r, const char *text, tiku_rm_style_t *style);
/** @brief Parse a finite point/px length without consulting the process locale. */
int tiku_rm_length(const char *text, float *value);
/** @brief Parse an HTML/CSS RGB or RGBA hex colour and basic named colours. */
int tiku_rm_color(const char *text, uint32_t *value);
/** @brief Decode an embedded PNG and append it to the open paragraph.
 *  @return One for an appended image; zero for unsupported content or refusal.
 *          Unsupported sources leave status OK so the caller can use alt text. */
int tiku_rm_picture(tiku_rimp_t *r, const tiku_rm_tag_t *tag,
                     const tiku_rm_style_t *style, const char *alt);
#endif
