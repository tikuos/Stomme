/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_app_private.h - what the tiku_trk_app files share: the includes,
 * the window's metrics and state, and the functions one file defines for
 * another.  Outside this directory, only a test includes it.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_APP_PRIVATE_H_
#define TIKU_TRK_APP_PRIVATE_H_

/* split: tag browser */

#include "tiku_attr.h"
#include "tiku_dl.h"
#include "tiku_field.h"
#include "tiku_fs.h"
#include "tiku_host.h"
#include "tiku_iconpaint.h"
#include "tiku_model.h"
#include "tiku_motion.h"
#include "tiku_scroll.h"
#include "tiku_sound.h"
#include "tiku_strings.h"
#include "tiku_text_input.h"
#include "tiku_trk_application.h"
#include "tiku_trk_clip.h"
#include "tiku_trk_cwmenu.h"
#include "tiku_trk_drag.h"
#include "tiku_trk_folder_file_actions.h"
#include "tiku_trk_gfx_dep.h"
#include "tiku_trk_navigator.h"
#include "tiku_trk_panel.h"
#include "tiku_trk_poseview.h"
#include "tiku_trk_prefs.h"
#include "tiku_trk_script.h"
#include "tiku_trk_status_overlay.h"
#include "tiku_trk_task.h"
#include "tiku_trk_titleview.h"
#include "tiku_volume.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define WIN_W 760               /* the window's first width, px       */
#define WIN_H 520               /* and its first height               */
#define TAB_H 21                /* the title tab's height             */
#define BORDER 5                /* the frame around the content       */
#define TITLE_H 18              /* the column title strip             */
#define NAV_H 26                /* the navigator strip                */
#define COUNT_H 16              /* the count strip under the list     */
#define SCROLL_W 15             /* the vertical scroll bar            */

/** @brief The largest icon in a list row, however tall the row is. */
#define MINI_ICON_MAX 16

/** @brief The single-window browser's state. */
typedef struct {
    tiku_host_t    *host;            /**< the window on the display, or
                                      *   NULL in a displayless run       */
    tiku_surface_t *surf;            /**< the frame, drawn then blitted   */
    tiku_trk_poseview_t *view;       /**< the one list                    */
    tiku_backend_t  *backend;        /**< the namespace the view lists    */
    char                 title[160]; /**< the window's title: the folder  */
    int                  row_h;      /**< a list row in pixels, from the
                                      *   face                            */
    int                  running;    /**< 0 once quitting was asked for   */
    int                  dirty;      /**< a frame is owed                 */
    int                  reduce_motion; /**< the settings file's
                                         *   ReduceMotion                 */
    tiku_motion_t        activation_motion; /**< the opened pose's outline */
    tiku_rect_t          activation_rect;   /**< the pose it grows out of  */
    int                  band_active; /**< a rubber band is being swept   */
    int                  band_x0, band_y0, band_x1, band_y1; /**< its
                                      *   corners, in window coordinates  */
    int                  drag_active; /**< an icon drag is under way      */
    int                  drag_x, drag_y; /**< where the drag began, in
                                      *   list coordinates                */
    tiku_fs_t       *fs;             /**< the file operation engine       */
    char                 status[220]; /**< the status line's text         */
    struct browser_image_job *image_job; /**< open request in flight     */
    int                  editing;    /**< the inline rename editor is open */
    tiku_field_t      ed;            /**< the rename editor               */
    tiku_text_input_t commit;        /**< one whole committed text
                                      *   sequence                        */
    char                 edit_path[TIKU_PATH_MAX]; /**< the entry being
                                      *   renamed                         */
    int                  edit_row;   /**< its row                         */
    /** The window's activation state, which decides whether a selection
     *  is painted. */
    int                  active;
    int64_t              press_at;   /**< when the button went down, us   */
    int                  press_x, press_y; /**< where it went down        */
    int                  press_row;  /**< the row under that press, or -1 */
    int                  press_down; /**< the button is still held        */
    int                  read_only;  /**< the store refuses writes        */
    tiku_trk_titleview_t tv;         /**< the column strip's tracking state */
    int                  cursor_ew;  /**< the resize cursor is showing    */
    /** The list row to select once a delete has finished; -1 when the last
     *  operation was not one that removes rows (FS-050). */
    int                  select_next;
    /** The scroll bar as an object: a range that can be held steady while
     *  the user drags it, and detached while a bulk re-layout runs
     *  (PVL-051, PVL-052, PVL-060). */
    tiku_scroll_t    sb;
    tiku_scroll_t    hsb;            /**< the horizontal bar (TS-044)     */
    tiku_rect_t     hsb_rect;        /**< where it is drawn, or empty     */
    tiku_rect_t     sb_rect;         /**< where the vertical bar is drawn */
    /** The container-window menus the desktop shell presents too. */
    tiku_trk_cwmenu_t    menus;
    tiku_menu_t     *popup;          /**< the open menu, or NULL          */
    tiku_menu_t     *popup_sub;      /**< its open submenu, or NULL       */
    tiku_rect_t     popup_frame;     /**< where the menu is drawn         */
    tiku_rect_t     popup_sub_frame; /**< where the submenu is drawn      */
    int                 script_menu_held; /**< a script's menu-press is
                                           *   waiting for its release */
    int                 script_menu_x;    /**< x of that press          */
    int                 script_menu_y;    /**< y of that press          */
    tiku_rect_t     viewbtn_rect;    /**< the gutter's view-mode button   */
    int                  theme_mode; /**< DarkMode in force: 0 light,
                                      *   1 dark, 2 no hue                */
    int                  bar_open;   /**< the menu bar title open, or -1  */
    /** The window's own Back/Forward history and location field. */
    tiku_trk_navigator_t nav;
    tiku_field_t      nav_edit;      /**< the location field's editor     */
    int                  nav_editing; /**< the location field has the keys */
    int                  show_nav;   /**< the navigator strip is shown    */
    /** The settings file, watched for changes. */
    char                 prefs_path[TIKU_PATH_MAX];
    int                  prefs_present; /**< it existed at the last look  */
    /** TS-052: the preference that swaps type-to-select for type-to-filter,
     *  applied live when the settings file changes and again when the
     *  view returns to list mode. */
    int                  filter_pref;
    time_t               prefs_mtime; /**< the settings file's mtime, as
                                       *   last seen                     */
    off_t                prefs_size;  /**< its size, as last seen         */
    int64_t              prefs_checked; /**< when it was last looked at, us */
    /** The command layer the multi-window shell uses too.  The folder is a
     *  wrapper over this window's own view, so Delete, Paste and the rest
     *  have one implementation for both shells (rule 6 of
     *  docs/contracts/tracker.md). */
    tiku_trk_folder_window_t         folder;
    tiku_trk_clip_t                  clip;         /**< the clipboard     */
    tiku_trk_folder_file_actions_t   file_actions; /**< Delete, Paste, kin */
    /** Deferred and periodic work; browser_turn_end() runs the tasks that
     *  are due once per turn (AW-083). */
    tiku_trk_loop_t                 *loop;
    int                  ptr_x, ptr_y;   /**< the pointer, as last seen   */
    /** The modal question: the desktop shell's overlay, over this window's
     *  one surface, so the delete question and a collision are put the
     *  same way in both shells (FS-046). */
    tiku_trk_status_overlay_t        status_overlay;
} app_t;




/** @brief The strip the barber pole occupies at the right of the count. */
#define POLE_W 10

/** @brief What the command line asked for. */
typedef struct {
    const char *port;               /**< -p: the board's serial port      */
    const char *start;              /**< the folder to open; when unset,
                                         / on a board, else $HOME or / */
    int         baud;               /**< -b: the serial port's baud rate  */
    int         use_device;         /**< -d, or -p: that board is the root */
    int64_t     deadline_us;        /**< -seconds: stop at this time, or 0 */
    const char *shot;               /**< -shot: the PNG file for one frame */
    const char *script_path;        /**< -script: the script to run       */
    int         mute;               /**< -mute: no sound                  */
} browser_options_t;

/** @brief What the shell's loop keeps between one event and the next. */
typedef struct {
    tiku_trk_click_t    click;      /**< the click before this one       */
    tiku_trk_script_t  *sc;         /**< the script driving the run, or
                                     *   NULL                            */
    int64_t             shot_at_us; /**< when the -shot frame is due, 0
                                     *   never                           */
} browser_loop_t;

/* Shared across the family: defined in one file, used in another. */

/* tiku_trk_app.c */
int browser_paint_menu_icon(tiku_surface_t *s, const char *icon, int x, int y,
                            int size, int dimmed);

/* tiku_trk_app_commands.c */
int browser_menubar_press(app_t *a, int x, int y, unsigned state);
int browser_navigator_press(app_t *a, int x, int y);
int browser_popup_press(app_t *a, int x, int y);
void browser_menu_invoke(app_t *a, int command);
void browser_open_context_menu(app_t *a, int row, int x, int y, unsigned state);

/* tiku_trk_app_context.c */
int browser_col_x(const app_t *a, const tiku_trk_column_t *c);
int browser_first_selected_index(app_t *a);
int browser_index_of_path(app_t *a, const char *path);
int browser_measure_prefix_px(const char *text, int n, void *ctx);
int browser_measure_text(const char *s, void *ctx);
int browser_navigator_height(const app_t *a);
int browser_next_edit_row(app_t *a, int row, int backwards);
int browser_pose_index(app_t *a, const tiku_trk_pose_t *wanted);
int browser_visible_rows(const app_t *a);
tiku_rect_t browser_list_rect(const app_t *a);
tiku_rect_t browser_menubar_rect(const app_t *a);
tiku_rect_t browser_navigator_rect(const app_t *a);
tiku_trk_sc_ctx_t browser_menu_ctx(app_t *a, unsigned mods);
void browser_app_alert_answer(void *context, const tiku_alert_t *alert);
void browser_app_alert_stop(void *context);
void browser_draw_folder_icon(app_t *a, tiku_rect_t bar);
void browser_draw_title(app_t *a, tiku_rect_t strip, const tiku_trk_column_t *c,
                        int pressed, int drag_dx);
void browser_navigator_layout(const app_t *a, tiku_rect_t *back,
                              tiku_rect_t *forward, tiku_rect_t *up,
                              tiku_rect_t *field);

/* tiku_trk_app_draw.c */
void browser_blit(app_t *a);
void browser_count_label(app_t *a, int width, char *label, size_t max);
void browser_draw(app_t *a);

/* tiku_trk_app_events.c */
void browser_turn_begin(app_t *app, browser_loop_t *lp);
void browser_turn_end(app_t *app, const browser_options_t *o,
                      browser_loop_t *lp);

/* tiku_trk_app_fileops.c */
void browser_after_op(app_t *a);
void browser_drain_fs(tiku_fs_t *fs);

/* tiku_trk_app_keys.c */
int browser_title_hit(app_t *a, int px, int py, int *cx, int *cy);
void browser_handle_key(app_t *a, const tiku_event_t *event);
void browser_menu_key(app_t *a, unsigned key, unsigned modifiers);

/* tiku_trk_app_layout.c */
int browser_selection_paths(app_t *a, char paths[][TIKU_PATH_MAX],
                            const char *out[], int max);
void browser_retitle(app_t *a);
void browser_settle_layout(app_t *a);
void browser_settle_writability(app_t *a);
void browser_sync_navigator_preference(app_t *a, int force);

/* tiku_trk_app_lifecycle.c */
int browser_boot(app_t *app, browser_options_t *o, browser_loop_t *lp);
int browser_parse_args(int argc, char **argv, browser_options_t *o);
int browser_teardown(app_t *app, browser_loop_t *lp);
int browser_open_image(app_t *app, const tiku_model_t *model);
void browser_image_detach(app_t *app);

/* tiku_trk_app_navigate.c */
int browser_navigate_to(app_t *a, const char *path, int record);
void browser_begin_location(app_t *a);
void browser_draw_activation(app_t *a);
void browser_end_location(app_t *a, int apply);
void browser_navigate_back(app_t *a);
void browser_navigate_forward(app_t *a);
void browser_navigate_up(app_t *a);
void browser_open_animation(app_t *a, tiku_rect_t from);
void browser_open_selection(app_t *a);

/* tiku_trk_app_rename.c */
void browser_begin_rename(app_t *a);
void browser_begin_rename_row(app_t *a, int row);
void browser_end_rename(app_t *a, int commit);

/* tiku_trk_app_script.c */
char browser_key_char(const tiku_event_t *event);
int browser_count_at(app_t *a, int px, int py);
int browser_row_at(app_t *a, int px, int py);
int browser_script_pose_at(void *ctx, const char *name, int *x, int *y);
int browser_script_pose_screen(void *ctx, const char *name, int *x, int *y);
int browser_script_row_point(void *ctx, int row, int *x, int *y);
int browser_script_say(void *ctx, const char *noun, char *out, size_t max);
size_t browser_script_narrate(void *ctx, char *out, size_t max);
unsigned browser_script_pixel_at(void *ctx, int x, int y);

/* End of the shared declarations. */

#endif /* TIKU_TRK_APP_PRIVATE_H_ */
