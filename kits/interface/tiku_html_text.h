/*
 * TikuOS Interface Kit.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_html_text.h - shared document text, selection and search geometry.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_HTML_TEXT_H_
#define TIKU_HTML_TEXT_H_
#include "tiku_doc.h"

/** @brief Flatten visible runs with block separators; starts has at least nrun entries.
 *  @return Bytes copied, excluding NUL; insufficient capacity retains a terminated prefix. */
int tiku_html_text(const tiku_doc_t *doc, char *out, size_t capacity, int *starts);
/** @brief Whether text starts with query, ignoring ASCII letter case. */
int tiku_html_text_match(const char *text, const char *query);
/** @brief Measure a bounded UTF-8 prefix using the document's font metrics. */
int tiku_html_text_width(int face, const char *text, int bytes);
/** @brief Flattened byte offset nearest a point, or -1 between text lines.
 *  @note Coordinates are document-relative; block receives the selected line's block. */
int tiku_html_text_offset(const tiku_doc_t *doc, const int *starts, int length,
                            int x, int y, int *block);
/** @brief The selected part of a piece, in document coordinates; empty if disjoint. */
tiku_rect_t tiku_html_text_range(const tiku_doc_t *doc, const int *starts,
                                  int piece, int first, int last);
#endif
