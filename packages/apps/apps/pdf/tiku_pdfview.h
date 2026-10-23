/* tiku_pdfview.h -- TikuPDF's viewer: datasheets, manuals and papers,
 * drawn by the PDF kit.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#ifndef TIKU_PDFVIEW_H_
#define TIKU_PDFVIEW_H_

#include "tiku_window.h"

/** @brief The viewer: its window, the document shown and its jobs. */
typedef struct tiku_pdfview tiku_pdfview_t;

/** @brief An empty viewer; choosing a file stays the application's:
 *         @p choose asks for a document, or with @p key for the key file
 *         a document sealed to certificates is opened with. */
tiku_pdfview_t *tiku_pdfview_new(tiku_workspace_t *workspace, int *dirty,
                                 void (*choose)(void *ctx, int key),
                                 void *ctx);
/** @brief Open or raise the window without loading anything. */
void tiku_pdfview_open(tiku_pdfview_t *c);
/** @brief Close the window and let the document go. */
void tiku_pdfview_close(tiku_pdfview_t *c);
/** @brief Detach from the application; workers finish on their own. */
void tiku_pdfview_destroy(tiku_pdfview_t *c);
/** @brief The window, or NULL while it is closed. */
tiku_window_t *tiku_pdfview_handle(tiku_pdfview_t *c);
/** @brief Open @p path on a worker; the shown document stays until the
 *         new one has opened, and stays if it cannot be. */
int tiku_pdfview_file(tiku_pdfview_t *c, const char *path);
/** @brief The key file chosen for the document waiting to be opened with
 *         one; its password is asked for next.  @return 0 when none is
 *         waiting. */
int tiku_pdfview_key(tiku_pdfview_t *c, const char *keyfile);
/** @brief The shown document's path, "" when there is none. */
const char *tiku_pdfview_path(tiku_pdfview_t *c);

#endif
