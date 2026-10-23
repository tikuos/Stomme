/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_panel_window.h - the Open and Save panel: one folder's list
 * under a row for its parent, Favorites, and in a save panel the name field
 * and New folder.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_PANEL_WINDOW_H_
#define TIKU_TRK_PANEL_WINDOW_H_

#include "tiku_field.h"
#include "tiku_press.h"
#include "tiku_trk_alert.h"
#include "tiku_trk_panel.h"
#include "tiku_trk_poseview.h"
#include "tiku_window.h"
#include <stddef.h>

/**
 * @brief Hands the panel's answer, @p destination, to the shell for the
 *        caller's @p target and @p command; the panel never looks inside
 *        @p target.
 */
typedef void (*tiku_trk_panel_window_commit_fn)(void *context, void *target,
    int command, const char *destination);

/** @brief One reusable destination panel and all of its transient UI state. */
typedef struct {
    tiku_trk_panel_t       model;
    tiku_field_t        name;
    tiku_workspace_t *workspace;
    tiku_window_t    *window;
    tiku_trk_poseview_t   *view;
    tiku_backend_t    *backend;
    tiku_store_t      *store;
    tiku_trk_nodeicon_t   *icons;
    void                  *target;
    int                    command;
    int                    row_h;
    int                    screen_w;
    int                    screen_h;
    int                   *dirty;
    int64_t                press_at;
    int                    press_row;
    tiku_trk_panel_window_commit_fn commit;
    void                  *commit_context;
    /* The shell's alert, for a save's refusals and its Replace question;
     * the checked path waits here while the question is on screen. */
    tiku_alert_t      *alert;
    char                   pending_save[TIKU_PATH_MAX];
    /* The save name the caller offered, put back in the field when the
     * New folder button takes the panel into the folder it made. */
    char                   offered[64];
    /* The rows shown, by pose index: every pose except the dot-named and
     * the invisible ones.  scroll counts in this space. */
    int                    vis[1024];
    /* Beside each shown row: whether a folder row is a project, holding a
     * project.tiku; such a row shows the developer's folder icon. */
    unsigned char          proj[1024];
    /* When set, the one file name this panel may answer with: every other
     * file is greyed and cannot be picked, while folders can still be
     * entered.  Empty means any file. */
    char                   only[64];
    int                    nvis;
    int                    scroll;
    /* The Favorites menu: favourites, then recent folders, dropped up
     * from the bottom-left button (AW-062, CW-058). */
    int                    fav_open;
    int                    fav_count;
    char                   fav_path[12][TIKU_PATH_MAX];
    char                   fav_label[12][48];
    int                    refused;   /* an edit the name would not take */
    tiku_press_t           press;     /* a button held: its PANEL_HIT_ value */
} tiku_trk_panel_window_t;

/** @brief Whether the last key refused an edit, clearing the mark. */
int tiku_trk_panel_window_refused(tiku_trk_panel_window_t *controller);

/**
 * @brief Bind the panel to its workspace, backend, store and icons, and to
 *        the shell's commit callback.  Opens no window.
 */
void tiku_trk_panel_window_init(tiku_trk_panel_window_t *controller,
    tiku_workspace_t *workspace, tiku_backend_t *backend,
    tiku_store_t *store, tiku_trk_nodeicon_t *icons, int row_h,
    int screen_w, int screen_h, int *dirty,
    tiku_trk_panel_window_commit_fn commit, void *commit_context);

/**
 * @brief Open the panel to choose a folder for @p target and @p command,
 *        at its remembered frame, listing @p initial_directory; a panel
 *        already up takes the new question.
 */
void tiku_trk_panel_window_open(tiku_trk_panel_window_t *controller,
    void *target, int command, const char *initial_directory);

/** @brief Raise the panel as a save panel, its name field filled and
 *         focused. */
void tiku_trk_panel_window_open_save(tiku_trk_panel_window_t *controller,
    void *target, int command, const char *initial_directory,
    const char *initial_name);

/**
 * @brief Open the panel to choose a file, and with @p only set, only the
 *        file of that name.  A folder is entered by double-clicking it and
 *        is not an answer.
 */
void tiku_trk_panel_window_open_file(tiku_trk_panel_window_t *controller,
    void *target, int command, const char *initial_directory,
    const char *only);

/**
 * @brief Open the panel to choose a file or a folder: a file is chosen by
 *        its row, and a folder by opening it and pressing Open with no row
 *        selected.
 */
void tiku_trk_panel_window_open_any(tiku_trk_panel_window_t *controller,
    void *target, int command, const char *initial_directory);

/**
 * @brief The Replace question said replace: answer with the checked path
 *        and close (AW-052).
 */
void tiku_trk_panel_window_confirmed(tiku_trk_panel_window_t *controller);

/**
 * @brief A drop landed on the panel (AW-059).
 *
 * One entry navigates the panel; a file is selected in its parent and, in
 * a save panel, named in the field.  Every drop on the panel is consumed.
 */
void tiku_trk_panel_window_drop(tiku_trk_panel_window_t *controller,
                                const char *path);

/** @brief Give the panel the shell's alert for its save questions. */
void tiku_trk_panel_window_set_alert(tiku_trk_panel_window_t *controller,
                                     tiku_alert_t *alert);
/** @brief Close the panel, remembering its frame. */
void tiku_trk_panel_window_close(tiku_trk_panel_window_t *controller);
/** @brief Close the panel if open; nothing else is held. */
void tiku_trk_panel_window_destroy(tiku_trk_panel_window_t *controller);

/** @brief The window while it is open, else NULL. */
tiku_window_t *tiku_trk_panel_window_handle(
    tiku_trk_panel_window_t *controller);
/** @brief The panel's model: its mode, directory and question. */
tiku_trk_panel_t *tiku_trk_panel_window_model(
    tiku_trk_panel_window_t *controller);

/**
 * @brief A key for the focused panel: Escape, Return, Tab, the arrows and
 *        letters go through the panel's key map, and a save panel's name
 *        field takes text, Backspace, Cmd-Space and Cmd+X, C, V and A.
 * @return nonzero when taken; 0 while the alert's question is up.
 */
int tiku_trk_panel_window_key(tiku_trk_panel_window_t *controller,
    unsigned key, unsigned modifiers, char character);

/**
 * @brief Pass change notices to the panel's list; a folder shown that went
 *        away moves the panel elsewhere, and one that moved is followed.
 * @return nonzero when the panel's view changed.
 */
int tiku_trk_panel_window_poll(tiku_trk_panel_window_t *controller,
    int64_t now, const tiku_model_change_t *changes, int count, int complete);

/**
 * @brief Move the panel out of @p mount, the volume or board that went,
 *        when it was showing a folder there.
 * @return nonzero when the panel moved.
 */
int tiku_trk_panel_window_device_gone(tiku_trk_panel_window_t *controller,
                                      const char *mount);

/** @brief The first row shown, counted in the shown rows; 0 with no list. */
int tiku_trk_panel_window_scroll(const tiku_trk_panel_window_t *controller);
/** @brief How many rows the list shows (nvis), 0 with no list. */
int tiku_trk_panel_window_rows(const tiku_trk_panel_window_t *controller);

/**
 * @brief Insert committed text into the focused save panel's name field.
 * @return 0 when the panel is not focused, -1 when the field rejects the
 *         text or the name would not fit, else 1; the text is dropped
 *         unless the name field has the keys.
 */
int tiku_trk_panel_window_text(tiku_trk_panel_window_t *controller,
    const char *text);

#endif /* TIKU_TRK_PANEL_WINDOW_H_ */
