/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_folder_window.h - a folder window: its state, the drawing
 * services the shell shares with it, and the calls that draw and measure
 * it.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_FOLDER_WINDOW_H_
#define TIKU_TRK_FOLDER_WINDOW_H_

#include "tiku_field.h"
#include "tiku_query.h"
#include "tiku_scroll.h"
#include "tiku_thumb.h"
#include "tiku_trk_background.h"
#include "tiku_trk_clip.h"
#include "tiku_trk_cwmenu.h"
#include "tiku_trk_frame.h"
#include "tiku_trk_nodeicon.h"
#include "tiku_trk_poseview.h"
#include "tiku_trk_prefs.h"
#include "tiku_trk_services.h"
#include "tiku_window.h"
#include <stddef.h>

/**
 * @brief Row slots whose signatures a window keeps.
 *
 * A row past the last slot is painted on every frame.
 */
#define TIKU_TRK_ROWS_KEPT 160
/** @brief Icon poses whose signature and place a window keeps. */
#define TIKU_TRK_ICONS_KEPT 192

typedef struct tiku_trk_folder_window tiku_trk_folder_window_t;
/** @brief Height of the list view's column-title strip, in logical pixels. */
#define TIKU_TRK_TITLE_HEIGHT 20
struct tiku_trk_folder_edit;
struct tiku_trk_band_controller;
struct tiku_trk_drag_target;

/**
 * @brief A press on a pose, kept from the press to the release: when and
 *        where, the row and its folder, the window and who was in front,
 *        whether the button is still down and whether the long press has
 *        already offered its menu.
 */
typedef struct {
    int64_t                   at;       /* microseconds                 */
    int                       x, y;
    int                       row;      /* the pose row hit, or -1      */
    tiku_trk_folder_window_t *folder;   /* the folder the press is in   */
    tiku_window_t            *window;   /* deferred content raise       */
    tiku_window_t            *focus;    /* who was in front when it began */
    int                       down;     /* a primary button is held     */
    int                       menu;     /* the long press already fired */
} tiku_trk_press_t;

/** @brief Brings the menus up to date for a window about to draw. */
typedef void (*tiku_trk_folder_prepare_menu_fn)(void *context,
                                                 tiku_trk_folder_window_t *);

/** @brief Shared drawing services and live shell presentation state. */
typedef struct {
    tiku_workspace_t *workspace;
    tiku_trk_cwmenu_t     *menus;
    tiku_trk_prefs_t      *prefs;
    tiku_trk_clip_t       *clip;
    tiku_backend_t    *backend;
    tiku_trk_nodeicon_t   *icons;
    tiku_store_t      *store;
    tiku_thumb_q_t    *thumbs;
    int                    row_h;
    /* How many rows and icon poses have been painted since a script last
     * reset the count; the scripts read it to check how much a repaint
     * touched (PVL-045, PVL-048, PVL-075). */
    int                    rows_drawn;
    /* What this frame repaints, taken once per frame and read by both the
     * ground and the rows. */
    tiku_trk_repaint_rect_t damage[TIKU_TRK_REPAINT_MAX];
    int                    ndamage;
    int                    screen_w;
    int                    screen_h;
    char                  *status;       /* the shell's status line */
    char                  *count_text;   /* the front window's count line */
    size_t                 count_text_size;
    int                   *finding;      /* a type-ahead find is open */
    char                  *find_text;    /* the text typed into it */
    /* The shell's live state a row is drawn against, each by its owner:
     * the inline editor, the band, the drop target, the press. */
    const struct tiku_trk_folder_edit    *edit;
    const struct tiku_trk_band_controller *band;
    const struct tiku_trk_drag_target     *drop;
    const tiku_trk_press_t                *press;
    /* The shell's services; art_for gives an application's icon art. */
    const tiku_trk_services_t             *services;
    tiku_trk_folder_prepare_menu_fn prepare_menu;
    void                  *prepare_context;
    /* The column strip's live gesture, when one is running: the window it
     * belongs to draws the dragged title sliding and the resize guide
     * (TS-008, TS-063).  Both NULL when nothing is being tracked. */
    const struct tiku_trk_titleview *strip;
    const tiku_trk_folder_window_t *strip_owner;
    /* Set: one menu bar at the top of the screen, and the windows carry
     * no bar or bar icon of their own; their content takes the strip. */
    int                    global_bar;
} tiku_trk_folder_render_t;

/** @brief One folder window and the query or frame state it retains. */
struct tiku_trk_folder_window {
    tiku_trk_poseview_t       *view;       /* the rows, owned by the window */
    char                       path[TIKU_PATH_MAX];     /* what it shows */
    /* The path the window's frame is saved under: the folder's own, or
     * the query's record when it shows results. */
    char                       state_path[TIKU_PATH_MAX];
    tiku_window_t        *win;             /* the host; NULL once closed */
    tiku_qrun_t           *run;            /* the query feeding it, else NULL */
    int                        live;       /* fed by a live query run */
    /* Its folder or volume vanished: the window is closing, and its frame
     * is not saved. */
    int                        gone;
    tiku_trk_frame_t           opened_with; /* baseline for the next frame save */
    tiku_scroll_t          sbar;    /* its scroll bar (PVL-051) */
    int                        trash_union; /* every volume's trash, one
                                             * window (PVN-027) */
    int                        had_frame;  /* a saved frame was found for it */
    /* What the count line was last made from, so it is rebuilt only
     * when a number moved; -1 until the first frame. */
    int                        count_last_total;
    int                        count_last_selected;
    /* What this window's count line says; kept per window, like the
     * numbers above. */
    char                       count_text[256];
    /* The saved query it shows, by name; "" for a folder. */
    char                       qname[TIKU_NAME_MAX];
    /* Where its search looks, in the words the panel used, kept with the
     * window for the line that reports the search after the panel that
     * started it has closed. */
    char                       qscope[64];
    /* The picture behind it, decoded once and kept with the window, so a
     * frame does not decode the file again. */
    tiku_image_t           bg_image;       /* the decoded picture */
    char                       bg_shown[TIKU_PATH_MAX]; /* its file */
    tiku_trk_bg_t               bg_applied; /**< background used by the last image draw */
    int                        bg_drawn;   /* labels need a halo over it */
    /* The rows' or icons' pixels, kept between frames and blitted to the
     * frame whole, since the shell redraws the screen from the backdrop
     * up every frame (PVL-045).  kept.px is NULL until the first frame. */
    tiku_surface_t        kept;            /* the rows' pixels */
    int                        kept_ready; /* they hold a whole frame */
    /* The hash of what each row slot in those pixels was last painted
     * from; a row whose hash differs is painted again, damaged or not.
     * Slots past the end hold 0. */
    uint32_t                   row_sig[TIKU_TRK_ROWS_KEPT];
    /* For the icon modes: each visible pose's hash and where it was
     * painted; an icon frame repaints the poses that changed and the
     * places they left (PVL-062). */
    uint32_t                   icon_sig[TIKU_TRK_ICONS_KEPT];  /* per pose  */
    tiku_rect_t           icon_rect[TIKU_TRK_ICONS_KEPT]; /* where each was */
    int                        icon_kept_n;   /* how many of those are held */
    uint32_t                   icon_view_sig; /* icon size, area, mode */
    int                        icon_origin_x, icon_origin_y;
    int                        kept_icons;     /* kept pixels are icons */
    tiku_trk_folder_render_t  *render;     /* the shell's draw state */
};

/** @brief Free the pixels the window keeps; its next frame paints whole. */
void tiku_trk_folder_window_forget(tiku_trk_folder_window_t *folder);

/**
 * @brief Bind the window to the shared render context and give its view
 *        the icons, the measuring and hit callbacks, and the face metrics.
 */
void tiku_trk_folder_window_configure(tiku_trk_folder_window_t *folder,
                                      tiku_trk_folder_render_t *render);

/**
 * @brief The window's draw callback: the rows or the icons, the strips,
 *        and what is drawn over them.
 */
void tiku_trk_folder_window_draw(tiku_window_t *window,
    tiku_surface_t *surface, tiku_rect_t content, void *context);
/** @brief Draw the visible placed poses, each at its own place. */
void tiku_trk_folder_window_draw_icons(tiku_trk_folder_window_t *folder,
    tiku_surface_t *surface, tiku_rect_t content);

/**
 * @brief Draw the picture behind @p folder, its own or the template
 *        folder's (CW-020).
 * @note For the icon modes only.
 * @return 1 when a picture was drawn, which tells the labels to carry a
 *         halo.
 */
int tiku_trk_folder_window_draw_background(tiku_trk_folder_window_t *folder,
    tiku_surface_t *surface, tiku_rect_t content);

/**
 * @brief The area under the menu bar and the title strip: the rows, and the
 *        count line under them.
 */
tiku_rect_t tiku_trk_folder_window_content(
    const tiku_trk_folder_window_t *folder);
/** @brief The window's own menu bar, or w=0 under the global bar. */
tiku_rect_t tiku_trk_folder_window_bar(
    const tiku_trk_folder_window_t *folder);
/** @brief The vertical scroll bar's rectangle (list mode), or w=0. */
tiku_rect_t tiku_trk_folder_window_scrollbar(
    const tiku_trk_folder_window_t *folder);

/** @brief The column title strip in list mode, or w=0. */
tiku_rect_t tiku_trk_folder_window_title(
    const tiku_trk_folder_window_t *folder);
/** @brief The folder's own icon at the bar's right end. */
tiku_rect_t tiku_trk_folder_window_icon(
    const tiku_trk_folder_window_t *folder);
/** @brief The width that shows the widest row whole. */
int tiku_trk_folder_window_fit_width(
    const tiku_trk_folder_window_t *folder);
/** @brief The height that shows every row, strips included. */
int tiku_trk_folder_window_fit_height(
    const tiku_trk_folder_window_t *folder);
/** @brief The row under a screen point, in either mode, or -1. */
int tiku_trk_folder_window_row_at(const tiku_trk_folder_window_t *folder,
                                  int x, int y);
/** @brief Whether a point is in the bottom count/navigation strip (TS-043). */
int tiku_trk_folder_window_count_at(
    const tiku_trk_folder_window_t *folder, int x, int y);
/** @brief The list-mode column field under screen @p x, or -1 (PVS-011). */
int tiku_trk_folder_window_column_at(const tiku_trk_folder_window_t *folder,
                                     int x);

/**
 * @brief Whether a point on @p row counts as on its name: in list mode the
 *        part of the row the selection covers, in an icon mode the label.
 */
int tiku_trk_folder_window_name_at(const tiku_trk_folder_window_t *folder,
                                   int row, int x, int y);

#endif /* TIKU_TRK_FOLDER_WINDOW_H_ */
