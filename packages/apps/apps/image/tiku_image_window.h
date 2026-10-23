/* tiku_image_window.h -- TikuImage's viewer: read-only PNG, JPEG and BMP
 * images, from a file or from bytes handed in, such as a camera snapshot.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#ifndef TIKU_IMAGE_WINDOW_H_
#define TIKU_IMAGE_WINDOW_H_

#include "tiku_window.h"
#include <stddef.h>

/** @brief The viewer: its window, the image shown, the view and the
 *         refresh. */
typedef struct tiku_image_window tiku_image_window_t;

/** @brief An empty viewer; @p choose asks the application for a file. */
tiku_image_window_t *tiku_image_window_new(
    tiku_workspace_t *workspace, int *dirty,
    void (*choose)(void *ctx), void *ctx);
/** @brief Open the window, or raise it; this loads nothing. */
void tiku_image_window_open(tiku_image_window_t *c);
/** @brief Close the window: the refresh stops, and the image and any read
 *         in flight are dropped. */
void tiku_image_window_close(tiku_image_window_t *c);
/** @brief Detach from the application; a read in flight finishes on its
 *         own, and the last hold frees the viewer. */
void tiku_image_window_destroy(tiku_image_window_t *c);
/** @brief The window, or NULL while it is closed. */
tiku_window_t *tiku_image_window_handle(tiku_image_window_t *c);
/** @brief Open the window and read @p path on a worker: a regular file of
 *         at most TIKU_IMAGE_BYTES_MAX bytes, decoded there.
 *  @return 1 when queued. */
int tiku_image_window_file(tiku_image_window_t *c, const char *path);
/** @brief Open the window and show @p len bytes of an encoded image under
 *         @p label, copied now and decoded on a worker.
 *  @return 1 when queued.
 *  @note The bytes are one whole PNG, JPEG or BMP image. */
int tiku_image_window_bytes(tiku_image_window_t *c,
    const char *label, const void *bytes, size_t len);
/** @brief Each turn: while Watch is on, read the file again every two
 *         seconds; @p now is monotonic microseconds. */
void tiku_image_window_tick(tiku_image_window_t *c, int64_t now);
/** @brief Maximize the window within @p area, the desk's usable area, or
 *         restore the frame it had.  @return 1 when it changed. */
int tiku_image_window_zoom(tiku_image_window_t *c, tiku_rect_t area);
/** @brief Put @p text in the status line; the image shown stays. */
void tiku_image_window_note(tiku_image_window_t *c, const char *text);
/** @brief The shown image's file, "" for bytes handed in or no image. */
const char *tiku_image_window_path(tiku_image_window_t *c);
/** @brief Whether @p path ends in .png, .jpg, .jpeg or .bmp, in any case;
 *         the decoder checks the content itself. */
int tiku_image_file_supported(const char *path);

#endif
