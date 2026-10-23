/*
 * TikuDesktop Translation Kit.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_richhtml_private.h - escaped HTML and resolved CSS emitters.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_RICHHTML_PRIVATE_H_
#define TIKU_RICHHTML_PRIVATE_H_
#include "tiku_richhtml.h"
#include "tiku_richio_private.h"

/** @brief Escape HTML text/attributes without interpreting percent escapes. */
void tiku_rhtml_escape(tiku_rio_writer_t *w, const char *text, size_t length);
/** @brief Emit a locale-independent decimal rounded to 0.001 units. */
void tiku_rhtml_number(tiku_rio_writer_t *w, double value);
/** @brief Emit an ARGB colour as a CSS hexadecimal RGBA value. */
void tiku_rhtml_color(tiku_rio_writer_t *w, uint32_t color);
/** @brief Emit resolved character CSS; defaults match the document viewer. */
void tiku_rhtml_chars(tiku_rio_writer_t *w, const tiku_rich_chars_t *chars);
/** @brief Emit resolved paragraph margins, alignment and break hints. */
void tiku_rhtml_props(tiku_rio_writer_t *w, const tiku_rich_para_t *props);
/** @brief Emit an inline PNG picture with escaped alt and point dimensions. */
void tiku_rhtml_image(tiku_rio_writer_t *w, const tiku_rich_image_t *image);
/** @brief Emit one styled paragraph's contents and embedded pictures. */
void tiku_rhtml_paragraph(tiku_rio_writer_t *w,
    const tiku_richsnapshot_t *snapshot, size_t index);
#endif
