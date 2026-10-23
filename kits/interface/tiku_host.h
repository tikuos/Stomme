/*
 * KintsugiOS v0.01 kits -- the foundation KintsugiDesktop and KintsugiTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@kinstugi-os.org>
 *
 * tiku_host.h - native display host behind a portable event boundary.
 *
 * One window, its pixels and its input, with two backends: tiku_x11.c
 * for X11 and tiku_cocoa.m for AppKit.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_HOST_H_
#define TIKU_HOST_H_

#include "tiku_access.h"
#include "tiku_event.h"
#include "tiku_gfx.h"
#include "tiku_print_native.h"

typedef struct tiku_host tiku_host_t;

/**
 * @brief Create a native window, revealed by the first present that
 *        draws into it.
 * @return NULL when no display is present or the window cannot be made.
 */
tiku_host_t *tiku_host_open(int width, int height,
                                      const char *title);

/** @brief Release the native window and every platform resource it owns. */
void tiku_host_close(tiku_host_t *host);

/** @brief Discard an uncommitted composition and any text commit not yet
 *         polled; for when the text target changes. */
void tiku_host_cancel_composition(tiku_host_t *host);
/** @brief Supply the focused editor caret in host logical coordinates. */
void tiku_host_text_rect(tiku_host_t *host, tiku_rect_t rect);

/** @brief Native pixels per logical pixel of the display (1..4). */
int tiku_host_scale(const tiku_host_t *host);

/** @brief Copy the software surface into the native window.
 *  @return 0, or -1 when the surface is not the window's logical size. */
int tiku_host_present(tiku_host_t *host,
                           const tiku_surface_t *surface);
/** @brief The same, when only a logical rectangle of the surface changed
 *  since the last present; NULL is the whole surface.  The AppKit host
 *  compares the whole surface either way. */
int tiku_host_present_in(tiku_host_t *host, const tiku_surface_t *surface,
                         const tiku_rect_t *bounds);
/** @brief Wait for native input or @p timeout_us, at most a minute;
 *  returns at once while input is queued, and never takes it.  A NULL
 *  host sleeps. */
void tiku_host_wait(tiku_host_t *host, int64_t timeout_us);
/** @brief Cumulative native presentation counters (bytes are pixel payload). */
void tiku_host_present_stats(tiku_host_t *host, uint64_t *frames,
                             uint64_t *bytes, uint64_t *rectangles);

/** @brief Change the native window title. */
void tiku_host_set_title(tiku_host_t *host, const char *title);

/**
 * @brief Make the window the whole screen, or a window again.
 *
 * Under X it is asked of the window manager through _NET_WM_STATE, and
 * the window is also sized to the screen for a server with no manager.
 * Under AppKit it is the window's full-screen mode, entered at the first
 * frame when asked for before it.  The new size arrives as
 * TIKU_EVENT_RESIZE, as for any other resize.
 */
void tiku_host_set_fullscreen(tiku_host_t *host, int on);

/**
 * @brief Take one normalized event into @p event, if one is waiting.
 *
 * Local text and optional HTML are published to the host clipboard; native
 * content is imported on activation, paste and left click. For an
 * activation or a paste, a TIKU_EVENT_CLIPBOARD with the TIKU_CLIPBOARD_*
 * status in event.key comes before that input; under X the input waits
 * behind the import until it ends or makes no progress for a second.
 * @return 1 when @p event was filled, 0 when nothing is waiting.
 * @note A shell mirrors text before forwarding keys; rich clients fetch
 *       the current typed representation through the clipboard protocol.
 */
int tiku_host_poll(tiku_host_t *host, tiku_event_t *event);

/** @brief Pointer shapes: the arrow at rest, the others while what is
 *         under the pointer asks for them. */
enum {
    TIKU_CURSOR_ARROW = 0,
    TIKU_CURSOR_TEXT,           /* over text a caret could land in       */
    TIKU_CURSOR_RESIZE_H,       /* a divider that moves left and right   */
    TIKU_CURSOR_RESIZE_V,       /* one that moves up and down            */
    TIKU_CURSOR_RESIZE_DIAG,    /* a window's corner                     */
    TIKU_CURSOR_HAND            /* a link, a thing that can be pressed   */
};

/** @brief Show pointer shape @p kind, a TIKU_CURSOR_* value. */
void tiku_host_set_cursor(tiku_host_t *host, int kind);

/** @brief The horizontal-resize pointer when @p enabled, the arrow when
 *         not: for a divider under the pointer. */
void tiku_host_set_resize_cursor(tiku_host_t *host, int enabled);

/** @brief Read the pointer's logical position and the modifier state.
 *  @return 1, or 0 when they cannot be read. */
int tiku_host_pointer(tiku_host_t *host, int *x, int *y,
                           unsigned *modifiers);

/**
 * @brief Open a window showing @p s and pump events until it is closed.
 *
 * For a sample that has one picture to show: a key, a click or the
 * close box ends it.
 * @return 0 on a clean exit, -1 if no display was available.
 */
int tiku_host_show(const tiku_surface_t *s, const char *title);

/** @brief Publish logical content for one virtual window; NULL removes it.
 * Coordinates are host logical pixels. No callback occurs after removal. */
int tiku_host_access(tiku_host_t *host, uint32_t window,
                     tiku_access_tree_t *tree, int x, int y, int focused,
                     tiku_access_action_fn action, void *context);
/** @brief Queue a native dialog for a captured final PDF; callback runs from
 * poll. */
int tiku_host_print(tiku_host_t *host, uint64_t request, const void *pdf,
                    size_t bytes, const char *title, tiku_print_done_fn done,
                    void *context);
/** @brief Cancel an outstanding native print request. */
void tiku_host_print_cancel(tiku_host_t *host, uint64_t request);
#endif /* TIKU_HOST_H_ */
