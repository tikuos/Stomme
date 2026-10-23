/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_find_window.h - the Find window: the query form in a window,
 * with its query icon and its Search and Save actions.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_FIND_WINDOW_H_
#define TIKU_TRK_FIND_WINDOW_H_

#include "tiku_state.h"
#include "tiku_trk_find.h"
#include "tiku_window.h"
#include <stddef.h>

/**
 * @brief Runs the form's query in the shell and shows the results.
 * @return nonzero when the search ran, which closes the window.
 */
typedef int (*tiku_trk_find_window_search_fn)(void *context,
                                               tiku_trk_find_t *model);

/** @brief The Find window and the form behind it. */
typedef struct {
    tiku_trk_find_t       model;
    tiku_workspace_t *workspace;
    tiku_window_t    *window;      /**< while open, else NULL */
    tiku_store_t      *store;      /**< where saved queries go */
    int                    screen_w;
    int                    screen_h;
    int                   *dirty;      /**< set when a redraw is owed */
    char                  *status;     /**< the shell's status line */
    size_t                 status_size;
    tiku_trk_find_window_search_fn search;
    /** Called when Save has no query attached to save into: the shell puts
     *  up the save panel (AW-052). */
    void                 (*save_as)(void *context);
    /** Passed to save_as and to select_folder. */
    void                  *save_as_context;
    /** Called for 'Select folders...': the shell raises its folder panel
     *  (Q-019). */
    void                 (*select_folder)(void *context);
    void                  *search_context;
} tiku_trk_find_window_t;

/**
 * @brief Bind the controller to its workspace, the shell's dirty flag and
 *        status line, and the search it runs.  Opens no window.
 */
void tiku_trk_find_window_init(tiku_trk_find_window_t *controller,
    tiku_workspace_t *workspace, int screen_w, int screen_h,
    int *dirty, char *status, size_t status_size,
    tiku_trk_find_window_search_fn search, void *search_context);
/** @brief The store saved queries go to, for the form and the window. */
void tiku_trk_find_window_set_store(tiku_trk_find_window_t *controller,
                                    tiku_store_t *store);
/** @brief The type registry the form offers its type menu from. */
void tiku_trk_find_window_set_types(tiku_trk_find_window_t *controller,
                                    const tiku_trk_types_t *types);

/** @brief The form behind the window. */
tiku_trk_find_t *tiku_trk_find_window_model(
    tiku_trk_find_window_t *controller);
/** @brief The window while it is open, else NULL. */
tiku_window_t *tiku_trk_find_window_handle(
    tiku_trk_find_window_t *controller);

/**
 * @brief Open the window centred on the screen, sized to the form; an open
 *        window is brought up to date and activated instead.
 */
void tiku_trk_find_window_open(tiku_trk_find_window_t *controller);
/** @brief Close the window, if open, and ask for a redraw. */
void tiku_trk_find_window_close(tiku_trk_find_window_t *controller);

/**
 * @brief A key for the Find window: Return runs the default action, Escape
 *        closes, Backspace deletes and a character is typed.
 * @return nonzero when the Find window is focused, which takes every key.
 */
int tiku_trk_find_window_key(tiku_trk_find_window_t *controller,
    unsigned key, char character);

/** @brief Whether (x, y) is on the draggable query icon (Q-034). */
int tiku_trk_find_window_icon_at(tiku_trk_find_window_t *controller,
                                 int x, int y);

/**
 * @brief Whether the open window has one of its popups showing.
 * @return nonzero while it does.
 */
int tiku_trk_find_window_popup_open(
    const tiku_trk_find_window_t *controller);

/**
 * @brief Insert committed text into the focused Find window's form.
 * @return 0 when the window is not focused, -1 when the form rejects the
 *         text, else 1; a popup that is up drops the text.
 */
int tiku_trk_find_window_text(tiku_trk_find_window_t *controller,
    const char *text);

#endif /* TIKU_TRK_FIND_WINDOW_H_ */
