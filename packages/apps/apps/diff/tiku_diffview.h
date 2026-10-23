/* tiku_diffview.h -- TikuDiff's viewer: two files side by side, two
 * folders by what they hold, and two archives as the folders they hold,
 * compared through the diff kit.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#ifndef TIKU_DIFFVIEW_H_
#define TIKU_DIFFVIEW_H_

#include "tiku_window.h"

/** @brief The viewer: its window, the comparison shown and its jobs. */
typedef struct tiku_diffview tiku_diffview_t;

/** @brief What a question put to the person asks for. */
#define TIKU_DIFFVIEW_ASK_FILE   0      /**< a file to compare           */
#define TIKU_DIFFVIEW_ASK_FOLDER 1      /**< a folder to compare         */
#define TIKU_DIFFVIEW_ASK_SAVE   2      /**< where to save a patch       */

/** @brief What the window asks of the application around it. */
typedef struct {
    /* One of the questions above put to the person, starting in @p start,
     * offering @p name to save as; the answer comes to
     * tiku_diffview_picked(), or never when the person cancels. */
    void (*pick)(void *ctx, int what, const char *start, const char *name);
    /* Open @p path as the desktop opens a double-clicked item: a folder
     * as a window, a document in its program.  @return nonzero when it
     * was. */
    int (*open_path)(void *ctx, const char *path);
    void *ctx;
} tiku_diffview_host_t;

/** @brief An empty comparer; asking the person for things is @p host's. */
tiku_diffview_t *tiku_diffview_new(tiku_workspace_t *workspace, int *dirty,
                                   const tiku_diffview_host_t *host);
/** @brief Open or raise the window; this starts no comparison. */
void tiku_diffview_open(tiku_diffview_t *c);
/** @brief Close the window; the comparison stays until the viewer is
 *         destroyed. */
void tiku_diffview_close(tiku_diffview_t *c);
/** @brief Detach from the application; workers finish on their own, and
 *         the folder archives were extracted into is removed once the last
 *         of them has. */
void tiku_diffview_destroy(tiku_diffview_t *c);
/** @brief The window, or NULL while it is closed. */
tiku_window_t *tiku_diffview_handle(tiku_diffview_t *c);
/** @brief The answer to the question last put to the person. */
void tiku_diffview_picked(tiku_diffview_t *c, const char *path);
/** @brief Compare @p left with @p right on a worker: two files, two
 *         folders, or archives as the folders or files they hold. */
int tiku_diffview_compare(tiku_diffview_t *c, const char *left,
                          const char *right);
/** @brief Ask for the file, or the folder, to compare @p left with: how
 *         TikuDiff starts when it is opened on a document. */
void tiku_diffview_start_with(tiku_diffview_t *c, const char *left);
/** @brief Each turn: a redraw while a job runs, so its gauge moves. */
void tiku_diffview_tick(tiku_diffview_t *c);

#endif
