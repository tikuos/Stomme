/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_panel.h - the Open and Save panel.
 *
 * A browser that answers a question and closes: which file, or where and
 * under what name.  It shows a folder window's pose view in list mode, with
 * one choice or several and positions never saved, plus a name field, a
 * default button that tracks what is selected, and the checks a save makes
 * before anything is written.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_PANEL_H_
#define TIKU_TRK_PANEL_H_

#include "tiku_model.h"
#include "tiku_state.h"

/** @brief What the panel is asking. */
typedef enum {
    TIKU_TRK_PANEL_OPEN = 0,
    TIKU_TRK_PANEL_SAVE
} tiku_trk_panel_mode_t;

/** @brief What may be chosen.  A folder is always enterable either way. */
#define TIKU_TRK_PANEL_FILE 0x1u
#define TIKU_TRK_PANEL_DIR  0x2u

/** @brief How many names each shared recent list, and the favourites, keep. */
#define TIKU_TRK_RECENT_MAX 10

/** @brief Which recent list; they are counted and capped separately. */
typedef enum {
    TIKU_TRK_RECENT_DOCS = 0,
    TIKU_TRK_RECENT_FOLDERS,
    TIKU_TRK_RECENT_APPS
} tiku_trk_recent_t;

/** @brief Why a save destination was refused (AW-052). */
typedef enum {
    TIKU_TRK_SAVE_OK = 0,
    TIKU_TRK_SAVE_EMPTY,        /* no name typed                        */
    TIKU_TRK_SAVE_ROOT,         /* the system root itself               */
    TIKU_TRK_SAVE_DOTS,         /* "." or ".."                          */
    TIKU_TRK_SAVE_UNWRITABLE,   /* the folder refuses to be written in  */
    TIKU_TRK_SAVE_IS_FOLDER,    /* a folder already has that name       */
    TIKU_TRK_SAVE_REPLACE       /* a file does: ask before overwriting  */
} tiku_trk_save_t;

/** @brief What a double-click or the default button does with a row. */
typedef enum {
    TIKU_TRK_PANEL_ACT_NOTHING = 0,
    TIKU_TRK_PANEL_ACT_ENTER,   /* navigate into it                     */
    TIKU_TRK_PANEL_ACT_CHOOSE   /* it is the answer                     */
} tiku_trk_panel_act_t;

/** @brief What a panel key chord asked for (AW-057, AW-058). */
typedef enum {
    TIKU_TRK_PANEL_KEY_NONE = 0,
    TIKU_TRK_PANEL_KEY_CANCEL,
    TIKU_TRK_PANEL_KEY_PARENT,
    TIKU_TRK_PANEL_KEY_ENTER,
    TIKU_TRK_PANEL_KEY_DESKTOP,
    TIKU_TRK_PANEL_KEY_HOME,
    TIKU_TRK_PANEL_KEY_DUPLICATE,
    TIKU_TRK_PANEL_KEY_DEFAULT,   /* the default button                 */
    TIKU_TRK_PANEL_KEY_FOCUS      /* Tab: move between the controls     */
} tiku_trk_panel_key_t;

/** @brief The room for a name in the save field. */
#define TIKU_TRK_PANEL_NAME_MAX 128

/** @brief Which control has the keyboard (PVK-022). */
#define TIKU_TRK_PANEL_FOCUS_LIST 0
#define TIKU_TRK_PANEL_FOCUS_NAME 1

/** @brief One panel. */
typedef struct {
    tiku_trk_panel_mode_t mode;
    unsigned              flavors;        /* TIKU_TRK_PANEL_FILE, _DIR  */
    int                   multiple;       /* several may be chosen      */
    char                  dir[TIKU_PATH_MAX];
    char                  name[TIKU_TRK_PANEL_NAME_MAX];
    int                   name_len;
    int                   focus;
    char                  client[48];     /* whose panel it is          */
    /* With this on, the Desktop is the top of the tree and Up from the
     * root lands there (AW-061). */
    int                   desktop_is_root;
    char                  home[TIKU_PATH_MAX];
    char                  desktop[TIKU_PATH_MAX];
} tiku_trk_panel_t;

/**
 * @brief Open a panel on @p want, or on a fallback.
 *
 * The requested folder if it is one, else the user's home, else the root
 * (AW-050).  An open panel may choose several entries; a save panel starts
 * with the name field focused.
 *
 * @param flavors TIKU_TRK_PANEL_FILE and TIKU_TRK_PANEL_DIR; 0 means files.
 */
void tiku_trk_panel_open(tiku_trk_panel_t *p, tiku_trk_panel_mode_t mode,
                         unsigned flavors, const char *want,
                         tiku_backend_t *b, const char *client);

/**
 * @brief Whether @p m is an answer this panel accepts.
 *
 * A panel asked for a file does not accept the folder the file is in.
 */
int tiku_trk_panel_choosable(const tiku_trk_panel_t *p,
                             const tiku_model_t *m);

/** @brief The window title: "Open" or "Save" (AW-064). */
void tiku_trk_panel_title(const tiku_trk_panel_t *p, char *out, size_t max);

/**
 * @brief The default button's label for the current selection (AW-053).
 *
 * A single selected folder relabels it Open whatever the panel is for: the
 * button then enters the folder.
 */
const char *tiku_trk_panel_button_label(const tiku_trk_panel_t *p,
                                        const tiku_model_t *sel, int n);

/**
 * @brief Whether the default button may be pressed (AW-053).
 *
 * A save panel follows the name field being non-empty; an open panel
 * follows the selection matching what was asked for, and a lone folder
 * always enables it.  Nothing selected in an open panel enables it only
 * when a folder is a legal answer: the folder being shown is then the
 * answer.
 */
int tiku_trk_panel_button_enabled(const tiku_trk_panel_t *p,
                                  const tiku_model_t *sel, int n);

/**
 * @brief Take a single selected file's name into the save field (AW-054).
 *
 * Only in a save panel and only for a lone non-directory.  The keyboard
 * focus stays where it was.
 *
 * @return 1 when the field changed.
 */
int tiku_trk_panel_pick_name(tiku_trk_panel_t *p,
                             const tiku_model_t *sel, int n);

/** @brief What activating @p m does: enter it, or choose it (AW-055). */
tiku_trk_panel_act_t tiku_trk_panel_activate(const tiku_trk_panel_t *p,
                                             const tiku_model_t *m);

/**
 * @brief Check the save destination before anything is written (AW-052).
 *
 * @param out Receives the full path the save would use.
 * @return the refusal, or TIKU_TRK_SAVE_OK.
 */
tiku_trk_save_t tiku_trk_panel_save_check(const tiku_trk_panel_t *p,
                                          tiku_backend_t *b, char *out,
                                          size_t max);

/** @brief The words a refusal is shown with; NULL when there is none. */
const char *tiku_trk_panel_save_text(tiku_trk_save_t why);

/**
 * @brief Where a drop of @p path takes the panel (AW-059).
 *
 * A folder is navigated into; anything else navigates to its parent and is
 * selected there, and in a save panel its name is taken into the field.
 *
 * @param select Receives the entry to select afterwards, or "".
 * @return 1 when the panel moved.
 */
int tiku_trk_panel_drop(tiku_trk_panel_t *p, tiku_backend_t *b,
                        const char *path, char *select, size_t max);

/**
 * @brief The folder being shown was renamed or moved (AW-060, PVN-029).
 *
 * The panel follows it, and follows a folder above it that moved, keeping
 * the name being typed.
 *
 * @return 1 when the panel was re-pointed.
 */
int tiku_trk_panel_dir_moved(tiku_trk_panel_t *p, const char *from,
                             const char *to);

/**
 * @brief The folder being shown went away (AW-060, PVN-029, PVN-043).
 *
 * The panel moves to home, else the root, and stays open with its
 * question.
 */
void tiku_trk_panel_dir_gone(tiku_trk_panel_t *p, tiku_backend_t *b);

/**
 * @brief The folder one level up (AW-061).
 *
 * With "Desktop is root" on, up from the root is the Desktop and there is
 * nothing above that; without it the root is the top.
 *
 * @return 1 when there is somewhere to go.
 */
int tiku_trk_panel_parent(const tiku_trk_panel_t *p, char *out, size_t max);

/**
 * @brief Interpret a key chord (AW-057, AW-058).
 *
 * @param pose_focus    Nonzero when the list has the keyboard.
 * @param can_duplicate Nonzero when the selection could be duplicated; with
 *                      @p pose_focus, Cmd+D then duplicates rather than
 *                      going to the Desktop.
 */
tiku_trk_panel_key_t tiku_trk_panel_key(const tiku_trk_panel_t *p, int key,
                                        unsigned mods, int pose_focus,
                                        int can_duplicate);

/*---------------------------------------------------------------------------*/
/* Recent lists and favourites (AW-056, AW-062, CW-058)                      */
/*---------------------------------------------------------------------------*/

/**
 * @brief Note that @p path was just used.
 *
 * Most recent first, no duplicates, oldest dropped past the cap.
 */
void tiku_trk_recent_add(tiku_store_t *s, tiku_trk_recent_t which,
                         const char *path);

/**
 * @brief The recent list, most recent first.
 *
 * @param cap How many the user asked to see; the list holds one fewer
 *            (CW-058).
 * @return how many were written.
 */
int tiku_trk_recent_list(tiku_store_t *s, tiku_trk_recent_t which,
                         int cap, char out[][TIKU_PATH_MAX], int max);

/**
 * @brief Add the folder being shown to the favourites (AW-062).
 *
 * @return 1 when added; 0 when it is there already or the list is full.
 */
int tiku_trk_panel_add_favorite(const tiku_trk_panel_t *p,
                                tiku_store_t *s);

/** @brief The favourites, in the order they were added. */
int tiku_trk_panel_favorites(tiku_store_t *s,
                             char out[][TIKU_PATH_MAX], int max);

/*---------------------------------------------------------------------------*/
/* Geometry (AW-063) and name completion                                     */
/*---------------------------------------------------------------------------*/

/** @brief The limits of a panel's size. */
#define TIKU_TRK_PANEL_MIN_W 360
#define TIKU_TRK_PANEL_MAX_W 1000
#define TIKU_TRK_PANEL_MIN_H 200
#define TIKU_TRK_PANEL_MAX_H 1000

/**
 * @brief Restore the shared panel frame, clamped to the limits.
 *
 * One frame shared by every panel of every application: a size the user
 * chose once is the size pickers open with from then on.
 *
 * @return 1 when a saved frame was found; @p x, @p y, @p w and @p h are
 *         left alone otherwise.
 */
int tiku_trk_panel_restore_frame(tiku_store_t *s, int *x, int *y,
                                 int *w, int *h);

/** @brief Save the shared panel frame, its size clamped to the limits. */
void tiku_trk_panel_save_frame(tiku_store_t *s, int x, int y, int w,
                               int h);

/**
 * @brief Complete a partial file name against @p dir (NeXT UIG ch3:
 *        file-name completion wherever a file name may be typed).
 *
 * @p out receives the longest common extension of every entry in @p dir
 * whose name begins with @p prefix, which is at least the prefix itself.
 *
 * @return how many entries matched: 0 leaves @p out alone, 1 means the
 *         completion is a whole name.
 */
int tiku_trk_panel_complete(tiku_backend_t *b, const char *dir,
                            const char *prefix, char *out, size_t max);

#endif /* TIKU_TRK_PANEL_H_ */
