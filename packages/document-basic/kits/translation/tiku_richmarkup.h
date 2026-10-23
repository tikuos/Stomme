/*
 * TikuDesktop Translation Kit.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_richmarkup.h - editable markup conversion with explicit loss reports.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_RICHMARKUP_H_
#define TIKU_RICHMARKUP_H_
#include "tiku_richio.h"

/** @brief Conversion differences, returned only with a successful result. */
enum {
    TIKU_RICHMARKUP_STYLE = 1u << 0, /**< unsupported CSS or named-style loss */
    TIKU_RICHMARKUP_CONTENT = 1u << 1, /**< active or non-document content omitted */
    TIKU_RICHMARKUP_IMAGES = 1u << 2, /**< pictures represented by alternate text */
    TIKU_RICHMARKUP_STRUCTURE = 1u << 3, /**< structural approximation */
    TIKU_RICHMARKUP_SYNTAX = 1u << 4, /**< documentation Markdown subset */
    TIKU_RICHMARKUP_PAGES = 1u << 5, /**< page geometry is not transferred */
    TIKU_RICHMARKUP_BOOKMARKS = 1u << 6 /**< named point targets are omitted */
};

/** @brief Read UTF-8 HTML into a new editable document.
 *  @note Supports paragraphs, headings, inline formatting, links, lists and
 *        rectangular nested tables and embedded base64 PNG pictures.
 *        No script execution or external resource loading.
 *        Unsupported content is reported; spans and irregular grids refuse.
 *        Limits and cancellation follow richio. Failure clears both outputs.
 *  @return OK with an owned clean document and optional conversion notices. */
tiku_richio_status_t tiku_richmarkup_read_html(const void *data, size_t length,
    const tiku_richio_options_t *options, tiku_richdoc_t **out,
    unsigned *notices);

/** @brief Import the documentation Markdown subset as editable rich content.
 *  @note Raw HTML remains text. Unsupported Markdown syntax can remain literal;
 *        the SYNTAX notice identifies this subset. External images use alt text.
 *        Failure clears outputs; the caller owns a successful document. */
tiku_richio_status_t tiku_richmarkup_read_markdown(const void *data,
    size_t length, const tiku_richio_options_t *options, tiku_richdoc_t **out,
    unsigned *notices);

/** @brief Export an editable document to UTF-8 documentation Markdown.
 *  @note Formatting without Markdown equivalents is reported, not embedded as
 *        HTML. Output is malloc-owned and NUL-terminated. Failure clears outputs. */
tiku_richio_status_t tiku_richmarkup_write_markdown(
    const tiku_richsnapshot_t *snapshot, const tiku_richio_options_t *options,
    unsigned char **out, size_t *length, unsigned *notices);

/** @brief Describe conversion notices in a bounded NUL-terminated string. */
void tiku_richmarkup_report(unsigned notices, char *out, size_t capacity);
#endif
