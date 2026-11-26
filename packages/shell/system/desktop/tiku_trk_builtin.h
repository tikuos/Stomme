/*
 * Desktop shell and file manager.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_builtin.h - applications linked into the desktop, whose windows
 * open in the desktop's own workspace.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_BUILTIN_H_
#define TIKU_TRK_BUILTIN_H_

#include "tiku_window.h"
#include <stdint.h>

/** @brief One application linked into the desktop. */
typedef struct {
    const char *id;         /**< its id, as a manifest's                */
    const char *name;       /**< its name in the Deskbar and Open With  */
    const char *types;      /**< document types it opens, comma-separated */
    const char *icon;       /**< its artwork's name, or NULL            */
    const char *category;   /**< its Deskbar category, or NULL          */
    /**
     * Open document @p path, or the application alone when @p path is NULL,
     * in a window of @p workspace placed within @p area; the application
     * sets @p *dirty to ask for a redraw.  Returns the window, or NULL.
     */
    tiku_window_t *(*open)(tiku_workspace_t *workspace, tiku_rect_t area,
                           int *dirty, const char *path);
    /** Each desktop turn, before its frame is drawn, with the monotonic
     *  time in microseconds; may be NULL.  A turn comes at least every
     *  10 ms. */
    void (*tick)(int64_t now);
    /** Start a registered program which publishes its own window, returning
     * nonzero on admission. Used when open is NULL. */
    int (*start)(void);
} tiku_trk_builtin_t;

/**
 * @brief The applications linked into the desktop, @p *count of them.
 *
 * The product defines it; a desktop linked without a definition has none.
 */
const tiku_trk_builtin_t *tiku_trk_builtins(int *count);
/**
 * @brief Draw @p rect of built-in window @p window again in the desktop's
 *        next frame, through the window's draw callback; the rest of the
 *        screen keeps what it shows.  Nothing outside the window's frame is
 *        drawn for it.
 */
void tiku_trk_builtin_damage(tiku_window_t *window, tiku_rect_t rect);

#endif /* TIKU_TRK_BUILTIN_H_ */
