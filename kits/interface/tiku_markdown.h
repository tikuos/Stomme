/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_markdown.h - documentation Markdown turned into the help dialect
 * of HTML that tiku_doc.h reads.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_MARKDOWN_H_
#define TIKU_MARKDOWN_H_

#include <stddef.h>

/**
 * @brief Convert a documentation Markdown subset to the tiku_doc HTML dialect.
 *
 * Supports headings, paragraphs, emphasis, code, inline links and images,
 * lists, quotes, rules and pipe tables; raw HTML is escaped as text.  Not a
 * CommonMark implementation: docs/guides/tracker/help-reader.md lists the
 * subset.
 *
 * @return 1 on success; 0 when @p out is NULL, @p markdown is NULL, the
 *         result does not fit @p capacity or the work bound is exceeded,
 *         with @p out left empty when it has room for the NUL
 * @note @p out and @p markdown must not overlap.
 */
int tiku_markdown_html(char *out, size_t capacity, const char *markdown);

/**
 * @brief The bytes tiku_markdown_html() writes for @p markdown, NUL
 *        included, or 0 when @p markdown is NULL or exceeds the work bound.
 */
size_t tiku_markdown_html_size(const char *markdown);

/** @brief Optional semantic output and cooperative conversion cancellation. */
typedef struct {
    int document;              /**< retain six heading levels and list values */
    int (*cancel)(void *context);
    void *context;             /**< borrowed through conversion */
} tiku_markdown_options_t;

/** @brief Count or emit documentation-subset HTML with explicit options.
 *  @note NULL output counts bytes. A document conversion preserves six heading
 *        levels and each decimal item's written value. No resource loading.
 *  @return Required bytes including NUL; zero on refusal/cancellation, clearing
 *          a supplied buffer when capacity is nonzero. */
size_t tiku_markdown_convert(char *out, size_t capacity, const char *markdown,
                              const tiku_markdown_options_t *options);

#endif
