/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_select_window.h - the Select window: selects the items of a
 * folder whose names match a pattern.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_SELECT_WINDOW_H_
#define TIKU_TRK_SELECT_WINDOW_H_

#include "tiku_press.h"
#include "tiku_trk_select.h"
#include "tiku_window.h"

/**
 * @brief Selects by @p selection in the folder @p target.
 * @return the folder's window, to be activated, or NULL when nothing was
 *         applied.
 */
typedef tiku_window_t *(*tiku_trk_select_apply_fn)(
    void *context, void *target, const tiku_trk_select_t *selection);

/** @brief The Select window and the form behind it. */
typedef struct {
    tiku_trk_select_t       model;
    tiku_workspace_t  *workspace;
    tiku_window_t     *window;
    void                   *target;   /**< the folder selected in */
    int                     field;    /**< the control with the keys */
    int                    *dirty;
    int                     screen_w;
    int                     screen_h;
    tiku_trk_select_apply_fn apply;
    void                   *apply_context;
    tiku_press_t press;     /* the Select button, while held */
} tiku_trk_select_window_t;

/**
 * @brief Bind the controller to its workspace and the shell's apply
 *        callback.  Opens no window.
 */
void tiku_trk_select_window_init(tiku_trk_select_window_t *controller,
    tiku_workspace_t *workspace, int screen_w, int screen_h,
    int *dirty, tiku_trk_select_apply_fn apply, void *apply_context);
/** @brief The form behind the window. */
tiku_trk_select_t *tiku_trk_select_window_model(
    tiku_trk_select_window_t *controller);
/** @brief The window while it is open, else NULL. */
tiku_window_t *tiku_trk_select_window_handle(
    tiku_trk_select_window_t *controller);
/**
 * @brief Open the window at @p x, @p y for @p target, the folder the
 *        selection is made in.
 */
void tiku_trk_select_window_open(tiku_trk_select_window_t *controller,
                                 void *target, int x, int y);

/**
 * @brief A key for the focused Select window: Return applies, Escape
 *        closes, Tab steps the rule, and the rest edit the pattern.
 * @return nonzero when the window is focused, which takes every key.
 */
int tiku_trk_select_window_key(tiku_trk_select_window_t *controller,
    int key, unsigned modifiers, char character);

/**
 * @brief Insert committed text into the focused Select window's pattern.
 * @return 0 when the window is not focused, -1 when the pattern field
 *         rejects the text, else 1.
 */
int tiku_trk_select_window_text(tiku_trk_select_window_t *controller,
    const char *text);

#endif /* TIKU_TRK_SELECT_WINDOW_H_ */
