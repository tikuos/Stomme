/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_script_private.h - what the tiku_trk_script files share: the
 * includes, the types, the small accessors that read them, and the functions
 * one file defines for another.  Nothing outside this directory includes it.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_SCRIPT_PRIVATE_H_
#define TIKU_TRK_SCRIPT_PRIVATE_H_

/* split: public system/shared/shell/tiku_trk_script.h */

/* split: tag sc */

#include "tiku_font.h"
#include "tiku_trk_script.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

/** @brief The most steps a script holds; a script with more fails. */
#define SC_MAX_STEPS 512
/** @brief The room for a step's first string, and for a note. */
#define SC_TEXT_MAX  256

/** @brief What one line was asking for. */
typedef enum {
    OP_KEY = 0,
    OP_PRESS,
    OP_MOVE,
    OP_RELEASE,
    OP_CLICK,
    OP_CLICK_ROW,
    OP_CLICK_DESK,
    OP_PRESS_DESK,
    OP_MOVE_DESK,
    OP_RELEASE_DESK,
    OP_DRAG_WINDOW,
    OP_PRESS_ROW,
    OP_MOVE_ROW,
    OP_RELEASE_ROW,
    OP_CLICK_WINDOW,
    OP_CLICK_CELL,
    OP_WAIT,
    OP_REPORT,
    OP_SHOT,
    OP_RESIZE,
    OP_EXPECT_LOCATION,
    OP_EXPECT_NAV_BACK,
    OP_EXPECT_NAV_FORWARD,
    OP_EXPECT_NAV_UP,
    OP_EXPECT_NAV_VISIBLE,
    OP_EXPECT_VIEWPORT_ROWS,
    OP_EXPECT_STATUS,
    OP_EXPECT_TITLE,
    OP_EXPECT_ALERT,
    OP_EXPECT_SCROLL,
    OP_EXPECT_ROWS,
    OP_REMOVE,
    OP_EXPECT_PERM,
    OP_EXPECT_WINDOWS,
    OP_EXPECT_SEL,
    OP_EXPECT_SELNAME,
    OP_EXPECT_RENAMING,
    OP_EXPECT_FINDTEXT,
    OP_EXPECT_FINDTYPE,
    OP_EXPECT_FOLDERS,
    OP_EXPECT_WINAT,
    OP_EXPECT_COLS,
    OP_EXPECT_BARAT,
    OP_EXPECT_FILE,
    OP_EXPECT_NO_FILE,
    OP_EXPECT_ICONAT,
    OP_EXPECT_ICONSCREEN,
    OP_EXPECT_PIXEL,
    OP_EXPECT_INK,
    OP_EXPECT_FONT,
    OP_EXPECT_NO_TITLE,
    OP_DRAW_RESET,
    OP_EXPECT_DRAWN,
    OP_EXPECT_MENU,
    OP_EXPECT_NO_MENU,
    OP_EXPECT_NARRATE,
    OP_EXPECT_NO_NARRATE,
    OP_EXPECT_TOLD,
    OP_EXPECT_MOUNTS,
    OP_EXPECT_CONTROL,
    OP_EXPECT_SOUND,
    OP_EXPECT_NO_SOUND,
    OP_EXPECT_SOUND_ON,
    OP_EXPECT_SOUND_OFF,
    OP_EXPECT_VOLUME,
    OP_EXPECT_READ,
    OP_EXPECT_OPERATION,
    OP_EXPECT_COUNT,
    OP_EXPECT_LINK,
    OP_EXPECT_FILEHAS,
    OP_EXPECT_FILELACKS,
    OP_EXPECT_DESKSEL,
    OP_EXPECT_DESKICON,
    OP_EXPECT_NO_DESKICON,
    OP_EXPECT_DESKSELNAME,
    OP_NOTE,
    OP_MENU_PICK,
    OP_WRITE,
    OP_SAME_AS_REDRAW,
    OP_QUIT,
    OP_MENU_PRESS,
    OP_MENU_RELEASE,
    OP_EXPECT_UNDER,
    OP_EXPECT_OVER,
    OP_EXPECT_SAID
} sc_op_t;

/** @brief One parsed script line. */
typedef struct {
    sc_op_t  op;                        /**< the verb                    */
    unsigned key;                       /**< KEY: logical key or char    */
    unsigned modifiers;                 /**< TIKU_MOD_* bits held        */
    int      button_x, button_y;        /**< a window op's tab point     */
    /** A point, a size for resize, or 1 in x for an "at most" or "at
     *  least" check. */
    int      x, y;
    unsigned button;                    /**< the mouse button, 1 unless
                                             the line names another      */
    /** WAIT turns, a row, the number a check expects, or the turns a
     *  repeated check may take. */
    int      count;
    char     text[SC_TEXT_MAX];         /**< path, substring or note     */
    char     text2[512];                /**< a second string, room for a
                                             whole path                  */
    int      line;                      /**< the script line, for reports */
} sc_step_t;

/** @brief A script being run. */
struct tiku_trk_script {
    sc_step_t step[SC_MAX_STEPS];       /**< the lines, parsed, in order */
    int       count;                    /**< how many of step[] are used */
    int       cursor;                   /**< next step to run in step[]  */
    int       waiting;                  /**< turns still owed to a WAIT  */
    int       pending_release;          /**< a click's release to send   */
    int       dragging_window;          /**< a tab drag: 1 owes the move,
                                             2 the release               */
    int       drag_to_x, drag_to_y;          /**< where a tab drag ends  */
    char      note[SC_TEXT_MAX];        /**< what the next check is for  */
    int       passes, fails;            /**< checks passed and failed    */
    int       finished;                 /**< quit, or the last step, ran */
    int       unread;                   /**< the first line past the
                                             table, or 0                 */
};


/* Shared across the family: defined in one file, used in another. */

/* tiku_trk_script.c */
int sc_there(const char *path);
unsigned sc_key_of(const char *name);
unsigned sc_mods_of(char **w, int n, int from);
void sc_rest(char **w, int n, int from, char *out, size_t max);
void sc_verdict(tiku_trk_script_t *s, int ok, const char *what,
                const char *arg);

/* tiku_trk_script_expect.c */
int sc_parse_desktop(sc_step_t *st, char **w, int n);
int sc_parse_file_contents(sc_step_t *st, char **w, int n);
int sc_parse_file_state(sc_step_t *st, char **w, int n);
int sc_parse_said(sc_step_t *st, char **w, int n);
int sc_parse_window(sc_step_t *st, char **w, int n);
int sc_pump_desktop(tiku_trk_script_t *s, const tiku_trk_script_state_t *st,
                    tiku_trk_script_step_t *out, sc_step_t *cur);
int sc_pump_file_contents(tiku_trk_script_t *s,
                          const tiku_trk_script_state_t *st,
                          tiku_trk_script_step_t *out, sc_step_t *cur);
int sc_pump_file_state(tiku_trk_script_t *s, const tiku_trk_script_state_t *st,
                       tiku_trk_script_step_t *out, sc_step_t *cur);
int sc_pump_pixels(tiku_trk_script_t *s, const tiku_trk_script_state_t *st,
                   tiku_trk_script_step_t *out, sc_step_t *cur);
int sc_pump_said(tiku_trk_script_t *s, const tiku_trk_script_state_t *st,
                 tiku_trk_script_step_t *out, sc_step_t *cur);
int sc_pump_window(tiku_trk_script_t *s, const tiku_trk_script_state_t *st,
                   tiku_trk_script_step_t *out, sc_step_t *cur);

/* tiku_trk_script_gesture.c */
int sc_parse_gesture(sc_step_t *st, char **w, int n, int lineno);
int sc_pump_gesture(tiku_trk_script_t *s, const tiku_trk_script_state_t *st,
                    tiku_trk_script_step_t *out, sc_step_t *cur);

/* End of the shared declarations. */

#endif /* TIKU_TRK_SCRIPT_PRIVATE_H_ */
