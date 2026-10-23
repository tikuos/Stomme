/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_office.h - word-processor documents and slide decks, as PDF.
 *
 * A .docx or .odt is laid out into pages -- its fonts, or faces drawn to
 * their widths; its paragraphs, lists, tables, pictures, headers and
 * footers, footnotes -- and a .pptx is drawn a slide a page.  Either way
 * what comes out is a PDF, which tiku_pdf_view.h shows and a file keeps.
 * Everything here reads files from anyone: parts are read with ceilings,
 * and nothing outside the file is fetched.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_OFFICE_H_
#define TIKU_OFFICE_H_

#include <stddef.h>

/** @brief What a file is, by its contents. */
typedef enum {
    TIKU_OFFICE_NONE = 0,           /* nothing read here                 */
    TIKU_OFFICE_DOCX,               /* Word, .docx .docm .dotx           */
    TIKU_OFFICE_ODT,                /* OpenDocument text, .odt .ott      */
    TIKU_OFFICE_PPTX                /* PowerPoint, .pptx .ppsx .potx     */
} tiku_office_kind_t;

/** @brief Whether @p path is a document read here, and which kind: by the
 *         package's own declarations, not the file's name. */
tiku_office_kind_t tiku_office_kind(const char *path);

/**
 * @brief @p path laid out and written as a PDF, in memory.  It fits
 *        tiku_pdf_view_open_as().
 * @return the PDF's bytes, with their length in @p len; NULL with a
 *         sentence in @p why (@p max bytes) saying why not.
 * @note It takes as long as the document is long: call it on a worker.
 *       The caller frees the bytes.
 */
unsigned char *tiku_office_pdf(const char *path, size_t *len, char *why,
                               size_t max);

/** @brief The same, written to the file @p pdf: Export as PDF.
 *  @return 0, or -1 with @p why said. */
int tiku_office_export(const char *path, const char *pdf, char *why,
                       size_t max);

#endif /* TIKU_OFFICE_H_ */
