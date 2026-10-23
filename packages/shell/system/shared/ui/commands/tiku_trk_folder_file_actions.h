/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_folder_file_actions.h - the folder commands that change the
 * filesystem, and the shell services they use.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_FOLDER_FILE_ACTIONS_H_
#define TIKU_TRK_FOLDER_FILE_ACTIONS_H_

#include "tiku_fs.h"
#include "tiku_trk_alert.h"
#include "tiku_trk_clip.h"
#include "tiku_trk_drag.h"
#include "tiku_trk_folder_window.h"
#include <stddef.h>

/** @brief Close every window open on @p path or below it. */
typedef void (*tiku_trk_folder_close_under_fn)(void *context,
                                                const char *path);

/**
 * @brief Show the preferences in the shell's own way; one of the shells has
 *        no window stack.
 */
typedef void (*tiku_trk_folder_prefs_fn)(void *context);

/** @brief Services shared by folder commands that mutate filesystem state. */
typedef struct {
    tiku_fs_t                    *fs;        /**< the copy engine          */
    tiku_backend_t               *backend;   /**< for stat                 */
    tiku_trk_clip_t                  *clip;  /**< the file clipboard       */
    tiku_trk_drag_t                  *pending;   /**< a drop's items      */
    char                             *drop_destination; /**< and target  */
    tiku_alert_t                 *alert;     /**< for questions, or NULL   */
    tiku_trk_folder_prefs_fn          open_prefs;   /**< or NULL          */
    void                             *prefs_context;
    char                             *status;   /**< the status line      */
    size_t                            status_size;
    tiku_trk_folder_close_under_fn    close_under;  /**< or NULL          */
    void                             *close_context;
    /* A paste of a mixed clipboard is a move and a copy, and the engine
     * takes one job at a time: the copy waits here until it is idle
     * (FS-055). */
    char   deferred[64][TIKU_PATH_MAX];
    const char *deferred_paths[64];
    int    deferred_count;
    char   deferred_dest[TIKU_PATH_MAX];
    /* The path tiku_trk_folder_file_actions_invoke_path() acts on in
     * place of a window's selection, or NULL (AW-076). */
    const char *only_path;
    /** The path a delete question was asked about when only_path was set,
     *  answered in place of a window's selection; empty otherwise. */
    char   asked_path[TIKU_PATH_MAX];
} tiku_trk_folder_file_actions_t;

/**
 * @brief Bind filesystem, clipboard, dialog, and shell-close services; the
 *        clipboard then follows the files the engine moves (FS-060).
 */
void tiku_trk_folder_file_actions_init(
    tiku_trk_folder_file_actions_t *controller, tiku_fs_t *fs,
    tiku_backend_t *backend,
    tiku_trk_clip_t *clip, tiku_trk_drag_t *pending,
    char *drop_destination, tiku_alert_t *alert,
    tiku_trk_folder_prefs_fn open_prefs, void *prefs_context,
    char *status, size_t status_size,
    tiku_trk_folder_close_under_fn close_under, void *close_context);

/** @brief Collect selected paths into stable caller-owned storage. */
int tiku_trk_folder_file_actions_selection(
    const tiku_trk_folder_window_t *folder,
    char paths[][TIKU_PATH_MAX], const char **selection, int maximum);

/** @brief What the three-way delete question can be answered with. */
typedef enum {
    TIKU_TRK_DELETE_ANSWER_CANCEL = 0,
    TIKU_TRK_DELETE_ANSWER_TRASH,
    TIKU_TRK_DELETE_ANSWER_DELETE
} tiku_trk_delete_answer_t;

/** @brief The Empty Trash question said yes. */
void tiku_trk_folder_file_actions_answer_empty(
    tiku_trk_folder_file_actions_t *controller);

/**
 * @brief Carry out the delete the question was asked about (FS-046): to
 *        the Trash, outright, or nothing.
 *
 * The selection is read again; the question is modal, so it cannot have
 * changed.
 */
void tiku_trk_folder_file_actions_answer_delete(
    tiku_trk_folder_file_actions_t *controller,
    tiku_trk_folder_window_t *folder, tiku_trk_delete_answer_t answer);

/**
 * @brief Start whatever waited for the engine to go idle (FS-055).
 *
 * Does nothing while the engine is busy or when nothing waits.
 *
 * @return 1 when a waiting job was started.
 * @note Call it from the shell's step, where the engine is pumped.
 */
int tiku_trk_folder_file_actions_pump(
    tiku_trk_folder_file_actions_t *controller);

/**
 * @brief Apply a command to one named path (AW-076).
 *
 * The same commands the menu carries out on a window's selection, for a
 * caller that has a path and no window: the selection every action reads
 * answers with this path while it runs.
 *
 * @return whether the command was handled.
 */
int tiku_trk_folder_file_actions_invoke_path(
    tiku_trk_folder_file_actions_t *controller, const char *path,
    int command);

/** @brief Apply a filesystem-facing command, returning whether handled. */
int tiku_trk_folder_file_actions_invoke(
    tiku_trk_folder_file_actions_t *controller,
    tiku_trk_folder_window_t *folder, int command);

#endif /* TIKU_TRK_FOLDER_FILE_ACTIONS_H_ */
