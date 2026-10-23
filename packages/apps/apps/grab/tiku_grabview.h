/* tiku_grabview.h -- TikuGrab's window: a window, or the whole desk, as a
 * picture, taken by the desktop's grab service and shown here.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#ifndef TIKU_GRABVIEW_H_
#define TIKU_GRABVIEW_H_

#include <stdint.h>
#include "tiku_window.h"

struct tiku_msg;
/** @brief The window's state: what can be grabbed, the delay, the last
 *         picture. */
typedef struct tiku_grabview tiku_grabview_t;

/** @brief What the window asks of the application around it. */
typedef struct {
    /* A request to the desktop's grab service.  @return nonzero when it
     * went; the answer comes to tiku_grabview_message(). */
    int (*send)(void *ctx, const struct tiku_msg *msg);
    /* Where to keep a copy, put to the person, starting at @p start and
     * offering @p name; the answer comes to tiku_grabview_picked(). */
    void (*ask_save)(void *ctx, const char *start, const char *name);
    /* Open @p path as the desktop opens a double-clicked item: a folder
     * as a window, a document in its program.  @return nonzero when it
     * was. */
    int (*open_path)(void *ctx, const char *path);
    void *ctx;
} tiku_grabview_host_t;

/** @brief A window that grabs; asking the desktop is @p host's. */
tiku_grabview_t *tiku_grabview_new(tiku_workspace_t *workspace, int *dirty,
                                   const tiku_grabview_host_t *host);
/** @brief Open or raise the window, and ask what windows there are. */
void tiku_grabview_open(tiku_grabview_t *c);
/** @brief Close the window. */
void tiku_grabview_close(tiku_grabview_t *c);
/** @brief Let go of everything; the window is closed first. */
void tiku_grabview_destroy(tiku_grabview_t *c);
/** @brief The window, or NULL while it is closed. */
tiku_window_t *tiku_grabview_handle(tiku_grabview_t *c);
/** @brief The desktop's answer: the windows there are, or the picture's
 *         file, or why there is none. */
void tiku_grabview_message(tiku_grabview_t *c, const struct tiku_msg *msg);
/** @brief Where the person chose to keep a copy of the picture. */
void tiku_grabview_picked(tiku_grabview_t *c, const char *path);
/** @brief Each turn, at @p now_us: the delay counts down, and the list
 *         of windows is kept fresh. */
void tiku_grabview_tick(tiku_grabview_t *c, int64_t now_us);

#endif
