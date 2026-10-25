/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_open_with_window.h - the Open With window (AW-045, AW-047).
 *
 * Single selection, a list only, no menus, Escape cancels: the window asks
 * which application opens the selection.  Every document-capable
 * application is listed with its relation to the selection in words
 * (MA-081); the buttons follow the selection, and an application that does
 * not claim the type asks first (AW-046).
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_OPEN_WITH_WINDOW_H_
#define TIKU_TRK_OPEN_WITH_WINDOW_H_

#include "tiku_list.h"
#include "tiku_trk_catalog_settings.h"
#include "tiku_model.h"
#include "tiku_press.h"
#include "tiku_trk_alert.h"
#include "tiku_trk_launch.h"
#include "tiku_window.h"

#define TIKU_TRK_OW_PATHS 16   /**< documents one question can carry */

/** @brief Opens the @p count documents in @p paths with @p app. */
typedef void (*tiku_trk_ow_open_fn)(void *context,
    const tiku_trk_app_t *app, const char *const *paths, int count);

/** @brief The Open With window and the question it asks. */
typedef struct {
    tiku_workspace_t *workspace;
    tiku_window_t    *window;
    int                    screen_w;
    int                    screen_h;
    int                   *dirty;
    tiku_trk_ow_open_fn    open;
    void                  *open_context;
    tiku_alert_t      *alert;       /* the shell's, for the confirm  */
    struct tiku_store *store;       /* where preferred lives         */
    tiku_trk_catalog_settings_t *settings; /**< borrowed preference owner */
    tiku_trk_app_t
        pending_app; /**< captured launch target after a saved choice */
    uint64_t
        pending_generation; /**< originating workspace window generation */
    int pending;            /**< waits for its accepted preference save */
    char status[96]; /**< save progress or failure shown above the list */
    /* The question being asked. */
    tiku_trk_cand_t        cand[TIKU_TRK_APP_MAX];
    int                    count;
    char                   paths[TIKU_TRK_OW_PATHS][TIKU_PATH_MAX];
    const char            *path_ptr[TIKU_TRK_OW_PATHS];
    int                    path_count;
    char                   types[TIKU_TRK_APP_TYPES_MAX][TIKU_TYPE_MAX];
    int                    type_count;
    int                    generic_only; /* every document is untyped     */
    /* The rows, the one picked and the one shown first. */
    tiku_list_t       list;
    tiku_press_t press;     /* the button held: 1 open, 2 prefer */
} tiku_trk_open_with_window_t;

/**
 * @brief Bind the controller to its workspace, the launcher it opens
 *        through, the alert and the store.  Opens no window.
 */
void tiku_trk_open_with_window_init(tiku_trk_open_with_window_t *controller,
    tiku_workspace_t *workspace, int screen_w, int screen_h,
    int *dirty, tiku_trk_ow_open_fn open, void *open_context,
    tiku_alert_t *alert, struct tiku_store *store);

/** @brief Bind the shared association owner; it must outlive this controller.
 *  @note Rebinding detaches an outstanding launch completion. */
void tiku_trk_open_with_window_bind_settings(tiku_trk_open_with_window_t *,
    tiku_trk_catalog_settings_t *);
/** @brief Detach accepted save completions and close before freeing the
 * controller.
 */
void tiku_trk_open_with_window_destroy(tiku_trk_open_with_window_t *);

/**
 * @brief Ask which application, for @p paths of @p types; an open window
 *        takes the new question.
 */
void tiku_trk_open_with_window_open(tiku_trk_open_with_window_t *controller,
    const tiku_trk_apps_t *apps, const char *const *types, int ntypes,
    const char *const *paths, int npaths);

/** @brief The window while it is open, else NULL. */
tiku_window_t *tiku_trk_open_with_window_handle(
    tiku_trk_open_with_window_t *controller);

/**
 * @brief A key for the focused Open With window: Escape closes it, the
 *        arrows, Page Up and Down, Home and End walk the list, Return
 *        opens, and every other key is kept.
 * @return nonzero when taken; 0 while the confirm alert is up.
 */
int tiku_trk_open_with_window_key(tiku_trk_open_with_window_t *controller,
                                  int key, unsigned modifiers);

/**
 * @brief The confirm alert said Open anyway: open with the selected
 *        application and close the window (AW-046).
 */
void tiku_trk_open_with_window_confirmed(
    tiku_trk_open_with_window_t *controller);

#endif /* TIKU_TRK_OPEN_WITH_WINDOW_H_ */
