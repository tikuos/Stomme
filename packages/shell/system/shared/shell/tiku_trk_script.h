/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_script.h - a scripted, displayless driver for the two shells.
 *
 * A script is a sequence of keystrokes, pointer gestures and checks.  It
 * drives either shell, TikuDesktop or the harness-only TikuTracker, with no
 * X server: the window is drawn into memory as it is on screen.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_SCRIPT_H_
#define TIKU_TRK_SCRIPT_H_

#include "tiku_event.h"
#include <stddef.h>

/** @brief What the driver should do for one step. */
typedef enum {
    TIKU_TRK_SC_IDLE = 0,   /* nothing to inject; run one loop turn      */
    TIKU_TRK_SC_KEY,
    TIKU_TRK_SC_PRESS,
    TIKU_TRK_SC_MOVE,
    TIKU_TRK_SC_RELEASE,
    TIKU_TRK_SC_MENU_PICK,
    TIKU_TRK_SC_MENU_PRESS,
    TIKU_TRK_SC_MENU_RELEASE,
    TIKU_TRK_SC_SHOT,
    TIKU_TRK_SC_QUIT
} tiku_trk_script_do_t;

/** @brief One instruction, filled by the pump. */
typedef struct {
    tiku_trk_script_do_t what;
    unsigned    key;        /* KEY: logical key or printable character */
    unsigned    modifiers;  /* platform-neutral modifier mask          */
    int         x, y;       /* PRESS, MOVE and RELEASE                  */
    unsigned    button;     /* the mouse button                         */
    const char *path;       /* SHOT: output path; MENU_*: label         */
} tiku_trk_script_step_t;

/** @brief What the driver can tell the script about itself. */
typedef struct {
    const char *status;     /* the shell's status line, or NULL         */
    const char *title;      /* the focused window's title, or NULL      */
    int         scroll;     /* the focused view's first visible row     */
    int         rows;       /* how many rows it holds                   */
    const char *alert;      /* the open alert's text, or NULL           */
    int         windows;    /* how many windows are open                */
    int         selected;   /* how many poses the focused view selects   */
    const char *sel_name;   /* the first selected pose's name, or NULL   */
    int         renaming;   /* an inline name editor is active           */
    const char *find_text;  /* what the Find panel is asking for         */
    const char *find_type;  /* the type it is narrowed to                */
    const char *location;   /* the browser navigator's current path      */
    int         nav_back;   /* whether the browser can step backward     */
    int         nav_forward;
    int         nav_up;
    int         nav_visible;
    int         viewport_rows; /* rows that fit in the window            */
    int         win_x, win_y;   /* the focused window's frame            */
    int         columns;        /* how many its list shows               */
    int         bar_x, bar_y;   /* where the Deskbar has put itself      */
    /* Bit 0: a row whose type is not the one the Find panel asked for.
     * Bit 1: at least one row was looked at.  Together they say whether a
     * search was narrowed. */
    int         kinds;
    /* The labels of the menu that is showing, each followed by '|'. */
    const char *menu;
    /* What the count view under the list says. */
    const char *count;
    /* The Desktop's own selection: it has no window to be focused, so the
     * focused-window fields do not cover it. */
    int         desk_selected;
    const char *desk_sel_name;
    /* Where a named pose sits in view pixels: the focused view's poses
     * first, then the Desktop's. */
    int       (*pose_at)(void *ctx, const char *name, int *x, int *y);
    /* The Desktop's poses only: what the *-desk gestures aim at, so a
     * same-named row in the focused window cannot shadow the icon.  NULL
     * falls back to pose_at. */
    int       (*pose_desk_at)(void *ctx, const char *name, int *x, int *y);
    /* Where a named pose's icon is in screen pixels, searched as
     * pose_at searches. */
    int       (*pose_screen)(void *ctx, const char *name, int *x, int *y);
    /* Where a visible list row can be clicked, in shell coordinates. */
    int       (*row_point)(void *ctx, int row, int *x, int *y);
    /* Where a row's cell under the column whose title holds @p title can
     * be clicked. */
    int       (*cell_point)(void *ctx, int row, const char *title, int *x,
                            int *y);
    /* How many rows were painted since the last reset; nonzero @p reset
     * starts the count again. */
    int       (*rows_drawn)(void *ctx, int reset);
    /* Where the tab of the window titled @p title can be clicked. */
    int       (*tab_point)(void *ctx, const char *title, int *x, int *y);
    /* Any named fact the shell can say, from the noun table the
     * conductor's QUERY also answers from.  0 for a noun this shell cannot
     * say. */
    int       (*say)(void *ctx, const char *noun, char *out, size_t max);
    void       *say_ctx;
    /* The focused window narrated from its display list, the lines the
     * conductor's "narrate" noun answers with.  Computed only when a step
     * asks: a narration is a full content draw. */
    size_t    (*narrate)(void *ctx, char *out, size_t max);
    /* One pixel of what is on screen, 0xRRGGBB. */
    unsigned  (*pixel_at)(void *ctx, int x, int y);
    /* How many pixels a full redraw puts differently from what the frames
     * painted. */
    long      (*redraw_diff)(void *ctx);
    void       *redraw_ctx;
    /* Resize the shell's screen, as a window manager would.  0 when the
     * shell cannot. */
    int       (*resize)(void *ctx, int width, int height);
    void       *resize_ctx;
    void       *pose_ctx;   /* the context the pose, row, tab and pixel
                               callbacks take */
} tiku_trk_script_state_t;

/** @brief A script being run: its parsed lines and the cursor into them. */
typedef struct tiku_trk_script tiku_trk_script_t;

/**
 * @brief Read a script.  @return NULL if it cannot be read.
 *
 * The instructions a script may hold are listed in
 * docs/guides/tracker/scripting.md.
 */
tiku_trk_script_t *tiku_trk_script_open(const char *path);

/**
 * @brief Whether the script is in the middle of a wait.
 *
 * A wait is counted in loop turns.  A caller with work outstanding in
 * another process uses this to know when to give that process time.
 *
 * @return the turns still owed, or 0.
 */
int tiku_trk_script_waiting(const tiku_trk_script_t *s);

/** @brief Release a script; NULL is fine. */
void tiku_trk_script_free(tiku_trk_script_t *s);

/**
 * @brief Advance one instruction.
 *
 * Checks and waits are settled inside the pump and reported as IDLE, so the
 * driver handles only what it alone can do: inject an event, pick a menu
 * item, take a picture, or stop.
 *
 * @return 0 when the script has run out, 1 otherwise.
 */
int tiku_trk_script_pump(tiku_trk_script_t *s,
                         const tiku_trk_script_state_t *st,
                         tiku_trk_script_step_t *out);

/** @brief How many checks have failed so far. */
int tiku_trk_script_failures(const tiku_trk_script_t *s);

/**
 * @brief Print the tally.
 *
 * @return the exit code the driver should use: 1 when a check failed, when
 *         no check was made, or when steps went unreached; else 0.
 */
int tiku_trk_script_report(const tiku_trk_script_t *s, const char *who);

#endif /* TIKU_TRK_SCRIPT_H_ */
