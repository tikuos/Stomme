/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_sounds_window.h - the Sounds preferences: a row per moment
 * with its on/off, which sound it plays and a Test button, and one
 * volume under them.
 *
 * Every change is saved to the settings file and applied to the kit at
 * once; there is no OK button.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_SOUNDS_WINDOW_H_
#define TIKU_TRK_SOUNDS_WINDOW_H_

#include "tiku_press.h"
#include "tiku_trk_soundprefs.h"
#include "tiku_window.h"

#define TIKU_TRK_SOUNDS_W 500    /**< the window's frame width */

/** @brief Asks the shell to put up a file panel for @p event's sound. */
typedef void (*tiku_trk_sounds_choose_fn)(void *context,
                                          tiku_sound_event_t event);

/** @brief The Sounds preferences window and what it opened on. */
typedef struct {
    tiku_trk_prefs_t *prefs;        /* the settings, owned elsewhere */
    tiku_trk_sounds_choose_fn choose; /* the shell's file panel, or NULL */
    void             *choose_ctx;
    int               tracking;     /* the volume knob is held */
    char              path[512];    /* the file they are saved to, or "" */
    tiku_workspace_t *workspace;
    tiku_window_t    *window;
    int               screen_w;
    int               screen_h;
    int              *dirty;
    tiku_trk_soundprefs_offer_t offer;      /* the sounds folder at open */
    int               opened[TIKU_SOUND_EVENTS]; /* the enables at open */
    char              opened_clip[TIKU_SOUND_EVENTS][TIKU_TRK_PREF_TEXT_MAX];
    int               opened_volume;             /* the volume at open */
    /* The button held: 3i+1 clip, 3i+2 choose, 3i+3 test, 100 Defaults,
     * 101 Revert. */
    tiku_press_t      press;
} tiku_trk_sounds_window_t;

/**
 * @brief Bind the controller to its workspace and to the settings it
 *        edits, saved to @p path when one is given.  Opens no window.
 */
void tiku_trk_sounds_window_init(tiku_trk_sounds_window_t *controller,
                                 tiku_workspace_t *workspace,
                                 tiku_trk_prefs_t *prefs, const char *path,
                                 int screen_w, int screen_h, int *dirty,
                                 tiku_trk_sounds_choose_fn choose,
                                 void *choose_ctx);
/**
 * @brief The file panel answered @p path for @p event: the file is
 *        copied into the sounds folder, chosen, and played.
 */
void tiku_trk_sounds_window_chosen(tiku_trk_sounds_window_t *controller,
                                   tiku_sound_event_t event,
                                   const char *path);
/** @brief Whether the volume knob is held; while it is, the shell sends
 *         the pointer's moves and release here. */
int tiku_trk_sounds_window_tracking(
    const tiku_trk_sounds_window_t *controller);
/** @brief Close the window if open; nothing else is held. */
void tiku_trk_sounds_window_destroy(tiku_trk_sounds_window_t *controller);
/** @brief Open the window on the settings as they are; an open one is
 *         activated instead. */
void tiku_trk_sounds_window_open(tiku_trk_sounds_window_t *controller);
/** @brief Close the window and ask for a redraw. */
void tiku_trk_sounds_window_close(tiku_trk_sounds_window_t *controller);
/** @brief The window while it is open, else NULL. */
tiku_window_t *tiku_trk_sounds_window_handle(
    tiku_trk_sounds_window_t *controller);

#endif /* TIKU_TRK_SOUNDS_WINDOW_H_ */
