/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_switcher.h - the application switcher's state machine.
 *
 * What the switcher decides before anything is drawn: whether a Ctrl+Tab
 * is a switch or a request to browse, what order the applications are in
 * and when that order may change, what each key does while it is up, and
 * where the strip's animation stands at a given moment.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_SWITCHER_H_
#define TIKU_TRK_SWITCHER_H_

#include <stddef.h>
#include <stdint.h>

/** @brief Room for applications, windows per application, and a name. */
#define TIKU_TRK_SW_APPS 32
#define TIKU_TRK_SW_WINS 16
#define TIKU_TRK_SW_NAME 64

/** @brief Releasing Ctrl within this long of opening is a quick switch. */
#define TIKU_TRK_SW_HAIR_US 180000

/** @brief Idle time after which the order may be re-sorted. */
#define TIKU_TRK_SW_RESORT_US 400000

/** @brief The visible slots; more than this and the strip scrolls. */
#define TIKU_TRK_SW_SLOTS 7

/** @brief The two icon sizes the strip draws: the selected one is large. */
#define TIKU_TRK_SW_SMALL 24
#define TIKU_TRK_SW_LARGE 48

/**
 * @brief One inset step of the shrink and grow, in microseconds, and how
 *        many steps each way.
 */
#define TIKU_TRK_SW_STEP_US 20000
#define TIKU_TRK_SW_STEPS   2

/** @brief One application in the strip. */
typedef struct {
    char    name[TIKU_TRK_SW_NAME];
    char    win[TIKU_TRK_SW_WINS][TIKU_TRK_SW_NAME];
    int     nwin;
    int64_t used;                   /* when it was last switched to      */
    int     is_self;                /* the Tracker itself                */
} tiku_trk_sw_app_t;

/** @brief What a key did. */
typedef enum {
    TIKU_TRK_SW_NOTHING = 0,
    TIKU_TRK_SW_MOVED,
    TIKU_TRK_SW_COMMIT,
    TIKU_TRK_SW_CANCEL,
    TIKU_TRK_SW_QUIT_APP,
    TIKU_TRK_SW_HIDE_APP
} tiku_trk_sw_act_t;

/** @brief The switcher. */
typedef struct {
    tiku_trk_sw_app_t app[TIKU_TRK_SW_APPS];
    int               n;
    int               shown;        /* up on screen                      */
    int               sel;          /* selected application              */
    int               selwin;       /* selected window within it         */
    int               scroll;       /* first visible slot                */
    int               scroll_from;  /* the slot the strip slides from    */
    int64_t           opened;       /* when it came up                   */
    int64_t           last_switch;  /* when it was last put away         */
    /* The selection the strip is animating away from, and when it left;
     * with them, every slot's size is a function of the clock. */
    int               prev;
    int64_t           moved;
} tiku_trk_switcher_t;

/** @brief No applications, nothing chosen. */
void tiku_trk_switcher_init(tiku_trk_switcher_t *s);

/**
 * @brief Add an application, or find it by name.
 *
 * @return its index, or -1 when the strip is full.
 */
int tiku_trk_switcher_app(tiku_trk_switcher_t *s, const char *name,
                          int is_self);

/** @brief Give application @p i a window. */
void tiku_trk_switcher_window(tiku_trk_switcher_t *s, int i,
                              const char *title);

/**
 * @brief Ctrl+Tab arrived: show the switcher and step the selection, back
 *        when @p back, with the strip following it.
 *
 * Sorts most-recently-used first, but only after TIKU_TRK_SW_RESORT_US
 * idle, so repeated fast switching keeps a stable order (DB-055).
 */
void tiku_trk_switcher_open(tiku_trk_switcher_t *s, int64_t now, int back);

/**
 * @brief Ctrl was released at @p now: put the switcher away and mark the
 *        selection as used.
 *
 * @return 1 when Ctrl was released within TIKU_TRK_SW_HAIR_US of opening:
 *         a quick switch to the previous application (DB-054).
 */
int tiku_trk_switcher_release(tiku_trk_switcher_t *s, int64_t now);

/**
 * @brief A key while it is up (DB-056).
 *
 * Tab, Left and Right step the selection as tiku_trk_switcher_open()
 * does, with the strip following it.
 *
 * @param key The key's name: "Tab", "Left", "Right", "Up", "Down",
 *            "Escape", "Return", "space", "q" or "h".
 * @param now When the key arrived: a step's animation starts then.
 */
tiku_trk_sw_act_t tiku_trk_switcher_key(tiku_trk_switcher_t *s,
                                        const char *key, int shift,
                                        int64_t now);

/** @brief A click on slot @p slot, or -1 for outside (DB-059). */
tiku_trk_sw_act_t tiku_trk_switcher_click(tiku_trk_switcher_t *s, int slot);

/**
 * @brief The size icon @p i should draw at, at @p now (DB-058).
 *
 * The outgoing icon shrinks and the incoming one grows over the same two
 * steps, so at any instant the pair says which way the selection went.
 */
int tiku_trk_switcher_icon_size(const tiku_trk_switcher_t *s, int i,
                                int64_t now);

/** @brief Whether the strip is still animating at @p now. */
int tiku_trk_switcher_animating(const tiku_trk_switcher_t *s, int64_t now);

/**
 * @brief Whether a scroller arrow belongs at an end (DB-058).
 *
 * @param right Non-zero for the right-hand one.
 */
int tiku_trk_switcher_scroller(const tiku_trk_switcher_t *s, int right);

/**
 * @brief The strip's x offset in pixels at @p now (DB-058).
 *
 * The scroll slides to the new slot in the same steps as the icon sizes,
 * @p slot_w pixels per slot.
 */
int tiku_trk_switcher_scroll_px(const tiku_trk_switcher_t *s, int slot_w,
                                int64_t now);

/** @brief Where the panel sits on a screen of @p sw by @p sh (DB-060). */
void tiku_trk_switcher_place(int sw, int sh, int w, int h, int *x, int *y);

/** @brief The title for window @p w of application @p i: "App: Window",
 *         or just the app. */
void tiku_trk_switcher_title(const tiku_trk_switcher_t *s, int i, int w,
                             char *out, size_t max);

#endif /* TIKU_TRK_SWITCHER_H_ */
