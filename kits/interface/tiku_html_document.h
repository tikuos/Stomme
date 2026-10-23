/*
 * TikuOS Interface Kit.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_html_document.h - bounded owned document models and transactional reflow.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_HTML_DOCUMENT_H_
#define TIKU_HTML_DOCUMENT_H_
#include "tiku_doc.h"

/** @brief Default source-byte limit for an owned HTML model. */
#define TIKU_HTML_SOURCE_MAX (8u*1024u*1024u)
/** @brief Default table-storage limit for an owned HTML model. */
#define TIKU_HTML_MEMORY_MAX (64u*1024u*1024u)
/** @brief Optional bounded format conversion; return zero to abort loading. */
typedef int (*tiku_html_parse_fn)(tiku_doc_t *doc, const char *source, void *ctx);

/** @brief Model budgets, allocator and optional parse adapter; zero fields use defaults.
 *  @note Supply allocate, zero and release together as a matched allocator. */
typedef struct {
    size_t source_bytes, memory_bytes;
    void *(*allocate)(size_t);
    void *(*zero)(size_t, size_t);
    void (*release)(void *);
    tiku_html_parse_fn parse;
    void *parse_ctx;
} tiku_html_options_t;

/** @brief Copy source into a new model, growing tables within the options' budget.
 *  @param width Positive nominal layout width, or zero for parsing without layout.
 *  @return An owned model, or NULL; source and options are not retained. */
tiku_doc_t *tiku_html_document_load(const char *source, int width, const tiku_html_options_t *options);
/** @brief Free an owned model with its creation allocator; NULL is ignored. */
void tiku_html_document_free(tiku_doc_t *doc, const tiku_html_options_t *options);
/** @brief Reflow and grow line/piece tables within budget, preserving text and indices.
 *  @return 1 when line/piece capacity fits, or 0 with a usable capacity-limited model. */
int tiku_html_document_reflow(tiku_doc_t **doc, int width, const tiku_html_options_t *options);

#endif
