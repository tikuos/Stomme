/* tiku_reader_document.h -- a local document read whole and parsed, as
 * Markdown or HTML, into a page whose tables are sized to it.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#ifndef TIKU_READER_DOCUMENT_H_
#define TIKU_READER_DOCUMENT_H_

#include "tiku_doc.h"

/* The desktop's budgets; the document kit keeps its own limits. */
/** @brief The largest document file read. */
#define TIKU_READER_FILE_MAX (8u * 1024u * 1024u)
/** @brief The most memory one document's conversion or tables may take. */
#define TIKU_READER_MEMORY_MAX (64u * 1024u * 1024u)

#pragma GCC visibility push(hidden)
/** @brief Whether @p name is Markdown by its suffix, .md or .markdown;
 *         anything else, a generated report among them, is HTML. */
int tiku_reader_markdown(const char *name);
/** @brief Free a page tiku_reader_document() made, and its tables. */
void tiku_reader_doc_free(tiku_doc_t *doc);
/** @brief Parse @p source, named @p name, into a page laid out at a nominal
 *         width, its tables grown until it fits within
 *         TIKU_READER_MEMORY_MAX.  @return the page, or NULL. */
tiku_doc_t *tiku_reader_document(const char *name, const char *source);
/** @brief Parse as tiku_reader_document() does, without laying out.
 *  @note It measures nothing with the fonts: a search on a worker calls
 *        it.
 *  @return the page, or NULL. */
tiku_doc_t *tiku_reader_document_text(const char *name, const char *source);
/** @brief Lay *@p doc out at @p width, growing its line and piece tables
 *         within the desktop budget until it fits; a grown copy replaces
 *         the page without rereading the file. */
void tiku_reader_document_layout(tiku_doc_t **doc, int width);
/** @brief Read the regular file @p path whole, NUL-ended.  A file over
 *         TIKU_READER_FILE_MAX, one that changes while it is read, or one
 *         holding a NUL byte is refused.
 *  @return the text, to free(), or NULL with the reason in *@p why. */
char *tiku_reader_source(const char *path, const char **why);

/** @brief The allocator the parser and the folder search draw from: the
 *         C library's unless a test substitutes one that fails.  free()
 *         releases either. */
typedef struct {
    void *(*malloc_fn)(size_t n);
    void *(*calloc_fn)(size_t n, size_t size);
} tiku_reader_allocator_t;
/** @brief Substitute the allocator for a test; NULL restores the C
 *         library's. */
void tiku_reader_test_allocator(const tiku_reader_allocator_t *allocator);
/** @brief malloc() from the allocator in force. */
void *tiku_reader_malloc(size_t n);
/** @brief calloc() from the allocator in force. */
void *tiku_reader_calloc(size_t n, size_t size);

#pragma GCC visibility pop

#endif /* TIKU_READER_DOCUMENT_H_ */
