/*
 * KintsugiOS v0.01 kits -- the foundation KintsugiDesktop and KintsugiTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@kinstugi-os.org>
 *
 * tiku_window.h - composited in-process windows.
 *
 * A workspace keeps up to TIKU_WINDOW_MAX windows in stacking order on one
 * surface: each with a tab, a frame and content its owner draws through a
 * callback.  It hit-tests and tracks moves and resizes, draws the windows
 * with their shadows and the motion of opening, closing and minimizing,
 * and records a window's content for narration.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_WINDOW_H_
#define TIKU_WINDOW_H_

#include "tiku_event.h"
#include "tiku_gfx.h"

#define TIKU_WINDOW_MAX       16   /**< windows in one workspace          */
#define TIKU_WINDOW_BORDER     5   /**< pixels between frame and content  */
/* The shadow's softness, how far it reaches past the frame (two pixels
 * more, since it falls a little lower than the window), and the corners'
 * radius.  The reach is what a repaint around a window has to cover. */
#define TIKU_WINDOW_SHADOW_RADIUS 12
#define TIKU_WINDOW_SHADOW        (TIKU_WINDOW_SHADOW_RADIUS + 2)
#define TIKU_WINDOW_RADIUS         5
#define TIKU_WINDOW_TITLE_MAX 160  /**< bytes of a title, NUL included   */

/**
 * @brief The height of a window's tab: the bold face's height plus 6, and
 *        at least a tab button's side plus 6; 21 at 12 px.
 *
 * @note It changes with the interface size, so every layout that reserves
 *       room for the tab, or adds it up to size a frame, calls this.
 */
int tiku_window_tab_h(void);

/**
 * @brief The side of a tab's close or zoom button: the plain face's height
 *        less 3, at least 8; 12 at 12 px.
 */
int tiku_window_tab_button(void);

/** @brief The part of a window a point lands on. */
typedef enum {
    TIKU_HIT_NONE = 0,
    TIKU_HIT_CONTENT,
    TIKU_HIT_TAB,
    TIKU_HIT_CLOSE,
    TIKU_HIT_ZOOM,
    TIKU_HIT_RESIZE,
    TIKU_HIT_BORDER
} tiku_hit_t;

typedef struct tiku_window tiku_window_t;
typedef struct tiku_workspace tiku_workspace_t;

/** @brief Draws a window's content into @p content of @p surface. */
typedef void (*tiku_window_draw_fn)(tiku_window_t *window,
    tiku_surface_t *surface, tiku_rect_t content, void *context);
/** @brief Handles an event sent to a window; nonzero when it was used. */
typedef int (*tiku_window_event_fn)(tiku_window_t *window,
    const tiku_event_t *event, void *context);
/** @brief Asked before a requested close; 0 keeps the window open. */
typedef int (*tiku_window_close_fn)(tiku_window_t *window,
                                         void *context);
/** @brief Called once as a window closes, after it has left the stack. */
typedef void (*tiku_window_destroy_fn)(tiku_window_t *window,
                                             void *context);

/*
 * The published-menu protocol: a window describes its menus as plain
 * data, and whoever owns the top of the screen draws the description the
 * focused window published.  The publisher draws nothing, and the bar
 * owner reads nothing of the publisher but the description.  One level of
 * submenu is carried: an entry at level 1 belongs to the submenu of the
 * nearest level-0 entry above it.
 */
#define TIKU_MENUSET_MENUS 6   /**< menus one window publishes        */
#define TIKU_MENUSET_ITEMS 28  /**< entries in one menu               */
#define TIKU_MENUSET_LABEL 44  /**< bytes of a label, NUL included    */

/** @brief One published menu entry. */
typedef struct {
    char          label[TIKU_MENUSET_LABEL];
    int           command;
    char          sc;           /**< shortcut character, 0 for none    */
    unsigned      mods;         /**< TIKU_MENU_MOD_* of the shortcut   */
    unsigned char enabled;
    unsigned char marked;
    unsigned char separator;
    unsigned char level;        /**< 0 top, 1 inside the submenu above */
} tiku_menu_entry_t;

/** @brief One published menu: its title and entries. */
typedef struct {
    char                   title[TIKU_MENUSET_LABEL];
    int                    nitem;
    tiku_menu_entry_t item[TIKU_MENUSET_ITEMS];
} tiku_menu_list_t;

/** @brief The menus a window publishes. */
typedef struct {
    int                   nmenu;
    tiku_menu_list_t menu[TIKU_MENUSET_MENUS];
} tiku_menuset_t;

struct tiku_window;
/** @brief Called with the command of the published entry a user picked. */
typedef void (*tiku_menu_pick_fn)(struct tiku_window *window,
                                       int command, void *context);

/** @brief One window of a workspace. */
struct tiku_window {
    uint64_t                     generation; /**< tells reused slots apart */
    int64_t                      opened_at;  /**< the clock, for the rise */
    char                         title[TIKU_WINDOW_TITLE_MAX];
    tiku_rect_t             frame;
    int                          z;
    int                          open;
    int                          minimized;
    int                          resizable;
    int                          zoomable;
    tiku_window_draw_fn     draw;
    tiku_window_event_fn    event;
    tiku_window_close_fn    close_requested;
    tiku_window_destroy_fn  destroy;
    void                        *context;
    void                        *tag;        /**< tiku_workspace_find() key */
    /* What the window says its menus are (the publish protocol above);
     * has_menus 0 means it says nothing and the bar shows only its own. */
    tiku_menuset_t          menus;
    int                          has_menus;
    tiku_menu_pick_fn       menu_pick;
    void                        *menu_pick_context;
    tiku_rect_t             saved_zoom;      /**< frame before the zoom */
    int                          zoom_valid; /**< saved_zoom holds one */
    /* The tab button a press is holding down, TIKU_HIT_CLOSE or
     * TIKU_HIT_ZOOM, drawn pressed until the pointer lets go or leaves
     * it; 0 for none.  The button acts on the release, so a press that
     * leaves it does nothing. */
    int                          pressed;
    /* A plain window has no tab and no buttons, only a hairline around
     * its content, as a splash screen or an About panel has.  It cannot
     * be dragged or closed by hand; whoever opened it closes it. */
    int                          plain;
    /* Left out of the workspace's drawing while set: a screenshot of the
     * desk without the window that asked for it.  Nothing else about the
     * window changes; whoever sets it clears it before the next frame. */
    int                          hidden;
    /* The pointer shape its content asks for, TIKU_CURSOR_*: shown while
     * the pointer is over the content and nothing of the desktop's own is
     * in hand.  0, the arrow, by default. */
    int                          cursor;
    int                          auto_hide_tab; /**< hide tab while unfocused */
};

/**
 * @brief An empty workspace drawing onto @p surface, or NULL when out of
 *        memory.
 */
tiku_workspace_t *tiku_workspace_new(
    tiku_surface_t *surface);

/** @brief Close every window, running each destroy handler, and free it. */
void tiku_workspace_free(tiku_workspace_t *workspace);

/**
 * @brief Open a window titled @p title at @p frame, its content drawn by
 *        @p draw, focused and on top.
 *
 * When a window with the same non-NULL @p tag is open, that window is
 * activated and returned.
 *
 * @return The window, or NULL when TIKU_WINDOW_MAX are open.
 */
tiku_window_t *tiku_workspace_open(
    tiku_workspace_t *workspace, const char *title,
    tiku_rect_t frame, tiku_window_draw_fn draw, void *context,
    void *tag);

/** @brief The open window carrying @p tag, or NULL. */
tiku_window_t *tiku_workspace_find(
    tiku_workspace_t *workspace, void *tag);

/**
 * @brief Close @p window without asking: with the clock set it fades out
 *        where it was, focus passes to the window below, and its destroy
 *        handler runs last.
 */
void tiku_workspace_close(tiku_workspace_t *workspace,
                               tiku_window_t *window);

/**
 * @brief Ask @p window's close handler, and close the window unless the
 *        handler returns 0.
 *
 * @return 1 when the window closed.
 */
int tiku_workspace_request_close(tiku_workspace_t *workspace,
                                      tiku_window_t *window);

/** @brief Raise @p window to the top and give it focus. */
void tiku_workspace_activate(tiku_workspace_t *workspace,
                                  tiku_window_t *window);
/** @brief Relinquish keyboard focus without changing window order. */
void tiku_workspace_deactivate(tiku_workspace_t *workspace,
                                tiku_window_t *window);
/** @brief The focused window, or NULL. */
tiku_window_t *tiku_workspace_focused(
    tiku_workspace_t *workspace);

/** @brief How many windows are open. */
int tiku_workspace_count(const tiku_workspace_t *workspace);

/**
 * @brief The window at @p index in stacking order, 0 the bottom, or NULL
 *        out of range.
 */
tiku_window_t *tiku_workspace_at(
    tiku_workspace_t *workspace, int index);

/** @brief Dress @p window plain: no tab, only the hairline. */
void tiku_window_set_plain(tiku_window_t *window, int plain);

/**
 * @brief Set @p window's event, close-request and destroy handlers; each
 *        may be NULL.
 */
void tiku_window_set_handlers(tiku_window_t *window,
    tiku_window_event_fn event,
    tiku_window_close_fn close_requested,
    tiku_window_destroy_fn destroy);

/**
 * @brief Hand @p event to @p window's event handler.
 *
 * @return The handler's result, or 0 when the window is closed or has no
 *         handler.
 */
int tiku_window_send(tiku_window_t *window,
                          const tiku_event_t *event);

/**
 * @brief Where @p window's content is drawn: inside the border below the
 *        tab, or inside the hairline of a plain window.
 */
tiku_rect_t tiku_window_content(const tiku_window_t *window);

/** @brief Publish (or clear, with NULL) the window's menu description. */
void tiku_window_publish_menus(struct tiku_window *window,
                                    const tiku_menuset_t *menus,
                                    tiku_menu_pick_fn pick,
                                    void *context);

/** @brief The published description, or NULL when there is none. */
const tiku_menuset_t *tiku_window_menus(
    struct tiku_window *window);

/**
 * @brief Whether @p window can be resized from its corner and zoomed from
 *        its tab.
 */
void tiku_window_set_size_controls(tiku_window_t *window,
                                        int resizable, int zoomable);

/** @brief Hide an unfocused window's tab without moving its content.
 *  @note The owner schedules repaint after changing this policy. */
void tiku_window_set_tab_auto_hide(tiku_window_t *window, int enabled);

/**
 * @brief Which window, topmost first, and which part of it, the point
 *        @p x, @p y lands on; @p window gets the window, or NULL.
 */
tiku_hit_t tiku_workspace_hit(tiku_workspace_t *workspace,
    int x, int y, tiku_window_t **window);

/**
 * @brief Start following the pointer from @p x, @p y: a press on the tab
 *        (TIKU_HIT_TAB) moves @p window, one on the corner
 *        (TIKU_HIT_RESIZE) resizes it.
 */
void tiku_workspace_begin_track(tiku_workspace_t *workspace,
    tiku_window_t *window, tiku_hit_t what, int x, int y);

/**
 * @brief Follow the pointer to @p x, @p y: a move keeps the tab on the
 *        screen, a resize keeps the window at least 180 by 90.
 *
 * @return 1 when the frame changed.
 */
int tiku_workspace_track(tiku_workspace_t *workspace, int x, int y);

/** @brief Stop following the pointer. */
void tiku_workspace_end_track(tiku_workspace_t *workspace);

/** @brief Whether a move or a resize is being followed. */
int tiku_workspace_tracking(const tiku_workspace_t *workspace);
/** @brief The window being moved (not resized), or NULL. */
tiku_window_t *tiku_workspace_moving(tiku_workspace_t *workspace);

/** @brief The modes of tiku_workspace_cache_mode(). */
enum { TIKU_CACHE_OFF, TIKU_CACHE_REFRESH, TIKU_CACHE_REUSE };
/**
 * @brief Set how window content is cached, up to 64 MiB a workspace.
 *
 * TIKU_CACHE_OFF, the default, draws every frame and frees the caches;
 * TIKU_CACHE_REFRESH draws and caches; TIKU_CACHE_REUSE draws a window
 * from its cache when the cache matches.  A recording surface always
 * draws.
 *
 * @note Use REUSE only for frames where windows merely moved, and REFRESH
 *       after any change of content, style or focus and before the next
 *       input gesture, so coordinates kept by controllers are current.
 */
void tiku_workspace_cache_mode(tiku_workspace_t *workspace, int mode);
/** @brief Invalidate one window (NULL invalidates all). */
void tiku_workspace_cache_forget(tiku_workspace_t *workspace,
                                  tiku_window_t *window);
/** @brief Cumulative content callback/cache-hit counts and current bytes. */
void tiku_workspace_cache_stats(tiku_workspace_t *workspace,
    uint64_t *draws, uint64_t *hits, size_t *bytes);

/**
 * @brief Draw the backdrop, every shown window from the bottom up, and
 *        the ghosts of windows closing and minimizing, within the clip.
 */
void tiku_workspace_draw(tiku_workspace_t *workspace);
/**
 * @brief Draw @p window alone onto @p out where it stands on the desk,
 *        whatever covers it: its tab, frame and content as the active
 *        window, without its shadow.
 *
 * @note @p out has the size and scale of the workspace's surface.
 */
void tiku_workspace_draw_window(tiku_workspace_t *workspace,
                                tiku_window_t *window, tiku_surface_t *out);

/**
 * @brief Narrate @p window into @p out as the plain lines of tiku_dl_say().
 *
 * The content is drawn once onto a recording surface of the workspace's
 * size, never the shown one, and the list is narrated with no pixel
 * consulted; when the content drew pixels the list cannot carry, a
 * closing line says so.
 *
 * @return the bytes written, 0 (and an empty @p out) when there is
 *         nothing to narrate: no window, no draw callback or no room.
 */
size_t tiku_workspace_narrate(tiku_workspace_t *workspace,
                                   tiku_window_t *window,
                                   char *out, size_t max);

/**
 * @brief The window's controls as tiku_dl_facts() lines: kind, label,
 *        rectangle, state.  Recorded as tiku_workspace_narrate() records,
 *        without touching the shown surface.
 *
 * @return the bytes written, 0 when there is nothing to list.
 */
size_t tiku_workspace_controls(tiku_workspace_t *workspace,
                                    tiku_window_t *window,
                                    char *out, size_t max);

/**
 * @brief Draw the desk under the windows with @p draw, over the backdrop
 *        colour; NULL draws only the colour.
 */
void tiku_workspace_set_backdrop(tiku_workspace_t *workspace,
    void (*draw)(tiku_surface_t *surface, void *context), void *context);

/**
 * @brief Size @p window to fit content @p fit_width by @p fit_height, on
 *        the screen and left of the reserved strip, at least 120 by 80.
 *
 * When the window already has that size it returns to the frame it had
 * before the last zoom.
 *
 * @return 1 when the frame changed.
 */
int tiku_workspace_zoom(tiku_workspace_t *workspace,
    tiku_window_t *window, int fit_width, int fit_height);
/**
 * @brief Set the clock, in microseconds, that drives motion: a window
 *        rising as it opens, and the ghosts of closed and minimized
 *        windows.  At 0, the default, nothing moves.
 */
void tiku_workspace_set_now(tiku_workspace_t *workspace, int64_t now_us);

/** @brief Whether the last draw left something still moving. */
int tiku_workspace_animating(const tiku_workspace_t *workspace);
/**
 * @brief Where that motion is: the union of the rectangles every ghost and
 *        every rising window covers this frame and next.  Empty when
 *        nothing moves.
 *
 * @note A rising window's rectangle leaves out its shadow, so a repaint
 *       widens it by TIKU_WINDOW_SHADOW.
 */
tiku_rect_t tiku_workspace_motion(const tiku_workspace_t *workspace);

/**
 * @brief Minimize @p window at once; a ghost of the last frame it was
 *        seen in shrinks toward @p to as it fades.
 *
 * With the clock at 0 there is no ghost.
 */
void tiku_workspace_minimize(tiku_workspace_t *workspace,
                             tiku_window_t *window, tiku_rect_t to);

/** @brief Bring a minimized window back; it rises as one just opened. */
void tiku_workspace_restore(tiku_workspace_t *workspace,
                            tiku_window_t *window);

/**
 * @brief Keep zoomed windows left of @p reserved, such as the Deskbar's
 *        strip; a rectangle of no width reserves nothing.
 */
void tiku_workspace_set_reserved(tiku_workspace_t *workspace,
                                      tiku_rect_t reserved);

#endif /* TIKU_WINDOW_H_ */
