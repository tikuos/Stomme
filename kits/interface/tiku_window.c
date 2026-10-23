/*
 * KintsugiOS v0.01 kits -- the foundation KintsugiDesktop and KintsugiTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@kinstugi-os.org>
 *
 * tiku_window.c - composited in-process windows.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_window.h"
#include "tiku_ui.h"
#include "tiku_dl.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int
tiku_window_tab_h(void)
{
    int want = tiku_font_bold()->height + 6;   /* 21 at 12 px */
    int least = tiku_window_tab_button() + 6;

    return (want > least) ? want : least;
}

int
tiku_window_tab_button(void)
{
    int side = tiku_font_plain()->height - 3;  /* 12 at 12 px */

    return (side > 8) ? side : 8;
}

#define MIN_WIDTH 180
#define MIN_HEIGHT 90

struct tiku_workspace {
    uint64_t next_generation;
    tiku_surface_t *surface;
    tiku_window_t windows[TIKU_WINDOW_MAX];
    int order[TIKU_WINDOW_MAX];
    int count;
    int focused;
    tiku_rect_t reserved;
    tiku_hit_t track_what;
    int track_window;
    int track_x;
    int track_y;
    void (*backdrop)(tiku_surface_t *surface, void *context);
    void *backdrop_context;
    struct {
        tiku_surface_t *pixels;
        int valid, active;
    } cache[TIKU_WINDOW_MAX];
    int64_t now;                /* the clock, 0 when unset             */
    int animating;              /* the last draw left motion behind    */
    tiku_rect_t motion;         /* where that motion is, this frame and next */
    /* Ghosts: the last picture of a window just closed, shrinking and
     * fading where it was, or of one just minimized, moving to where it
     * went. */
    struct {
        tiku_surface_t *pixels;
        tiku_rect_t from, to;
        int64_t since;
        int64_t length;         /* how long it takes, microseconds     */
    } ghost[4];
    int cache_mode;
    size_t cache_bytes;
    uint64_t content_draws, cache_hits;
};

/** @brief Retire a slot's pixel allocation before it is reused. */
static void cache_free(tiku_workspace_t *ws, int slot)
{
    tiku_surface_t *s = ws->cache[slot].pixels;
    if (s) {
        ws->cache_bytes -= (size_t)s->w*s->h*s->scale*s->scale*sizeof *s->px;
        tiku_surface_free(s);
    }
    memset(&ws->cache[slot], 0, sizeof ws->cache[slot]);
}

void tiku_workspace_cache_forget(tiku_workspace_t *ws, tiku_window_t *w)
{
    if (!ws) return;
    for (int i = 0; i < TIKU_WINDOW_MAX; i++)
        if (!w || w == &ws->windows[i]) ws->cache[i].valid = 0;
}

void tiku_workspace_cache_mode(tiku_workspace_t *ws, int mode)
{
    if (!ws) return;
    ws->cache_mode = mode;
    if (mode != TIKU_CACHE_REUSE) tiku_workspace_cache_forget(ws, NULL);
    if (mode == TIKU_CACHE_OFF)
        for (int i = 0; i < TIKU_WINDOW_MAX; i++) cache_free(ws, i);
}

void tiku_workspace_cache_stats(tiku_workspace_t *ws,
    uint64_t *draws, uint64_t *hits, size_t *bytes)
{
    if (draws) *draws = ws ? ws->content_draws : 0;
    if (hits) *hits = ws ? ws->cache_hits : 0;
    if (bytes) *bytes = ws ? ws->cache_bytes : 0;
}

/** @brief Whether @p x, @p y lies inside @p rect. */
static int
inside(tiku_rect_t rect, int x, int y)
{
    return x >= rect.x && x < rect.x + rect.w &&
           y >= rect.y && y < rect.y + rect.h;
}

/** @brief Where slot @p slot stands in the stacking order, or -1. */
static int
order_index(tiku_workspace_t *workspace, int slot)
{
    int i;

    for (i = 0; i < workspace->count; i++) {
        if (workspace->order[i] == slot) {
            return i;
        }
    }
    return -1;
}

/**
 * @brief Where @p window's tab sits: flush with the frame's top-left, as
 *        wide as its parts, at most the frame's width and at least 60.
 */
static tiku_rect_t
tab_rect(const tiku_window_t *window)
{
    tiku_rect_t tab;
    /* The parts: the close button, the title in the bold face, the zoom
     * button when there is one, and the insets the draw uses. */
    int btn = tiku_window_tab_button();
    int width = btn + 16 +
                tiku_text_width(tiku_font_bold(), window->title) +
                (window->zoomable ? btn + 8 : 0) + 4;

    if (width > window->frame.w) { width = window->frame.w; }
    if (width < 60) { width = 60; }
    tab.x = window->frame.x;
    tab.y = window->frame.y;
    tab.w = width;
    tab.h = tiku_window_tab_h();
    return tab;
}

tiku_workspace_t *
tiku_workspace_new(tiku_surface_t *surface)
{
    tiku_workspace_t *workspace = calloc(1u, sizeof *workspace);

    if (workspace != NULL) {
        workspace->surface = surface;
        workspace->focused = -1;
        workspace->track_window = -1;
    }
    return workspace;
}

void
tiku_workspace_free(tiku_workspace_t *workspace)
{
    if (workspace != NULL) {
        int g;

        for (g = 0; g < 4; g++) {
            if (workspace->ghost[g].pixels != NULL) {
                tiku_surface_free(workspace->ghost[g].pixels);
                workspace->ghost[g].pixels = NULL;
            }
        }
    }
    while (workspace != NULL && workspace->count > 0) {
        tiku_workspace_close(workspace,
            &workspace->windows[workspace->order[workspace->count - 1]]);
    }
    free(workspace);
}

void
tiku_workspace_set_reserved(tiku_workspace_t *workspace,
                                 tiku_rect_t reserved)
{
    if (workspace != NULL) { workspace->reserved = reserved; }
}

int
tiku_workspace_count(const tiku_workspace_t *workspace)
{
    return (workspace != NULL) ? workspace->count : 0;
}

tiku_window_t *
tiku_workspace_at(tiku_workspace_t *workspace, int index)
{
    if (workspace == NULL || index < 0 || index >= workspace->count) {
        return NULL;
    }
    return &workspace->windows[workspace->order[index]];
}

tiku_window_t *
tiku_workspace_focused(tiku_workspace_t *workspace)
{
    if (workspace == NULL || workspace->focused < 0) { return NULL; }
    return &workspace->windows[workspace->focused];
}

void
tiku_workspace_activate(tiku_workspace_t *workspace,
                             tiku_window_t *window)
{
    int slot, at, i;

    if (workspace == NULL || window == NULL) { return; }
    slot = (int)(window - workspace->windows);
    at = order_index(workspace, slot);
    if (at < 0) { return; }
    for (i = at; i < workspace->count - 1; i++) {
        workspace->order[i] = workspace->order[i + 1];
    }
    workspace->order[workspace->count - 1] = slot;
    workspace->focused = slot;
}

void
tiku_workspace_deactivate(tiku_workspace_t *workspace, tiku_window_t *window)
{
    if (workspace && tiku_workspace_focused(workspace) == window)
        workspace->focused = -1;
}

tiku_window_t *
tiku_workspace_find(tiku_workspace_t *workspace, void *tag)
{
    int i;

    for (i = 0; workspace != NULL && i < TIKU_WINDOW_MAX; i++) {
        if (workspace->windows[i].open && workspace->windows[i].tag == tag) {
            return &workspace->windows[i];
        }
    }
    return NULL;
}

tiku_window_t *
tiku_workspace_open(tiku_workspace_t *workspace, const char *title,
                         tiku_rect_t frame,
                         tiku_window_draw_fn draw, void *context,
                         void *tag)
{
    tiku_window_t *window;
    int slot;

    if (workspace == NULL) { return NULL; }
    window = (tag != NULL) ? tiku_workspace_find(workspace, tag) : NULL;
    if (window != NULL) {
        tiku_workspace_activate(workspace, window);
        return window;
    }
    for (slot = 0; slot < TIKU_WINDOW_MAX; slot++) {
        if (!workspace->windows[slot].open) { break; }
    }
    if (slot == TIKU_WINDOW_MAX) { return NULL; }
    window = &workspace->windows[slot];
    memset(window, 0, sizeof *window);
    window->generation = ++workspace->next_generation;
    snprintf(window->title, sizeof window->title, "%s",
             (title != NULL) ? title : "");
    window->frame = frame;
    window->draw = draw;
    window->context = context;
    window->tag = tag;
    window->open = 1;
    window->opened_at = workspace->now;
    window->resizable = 1;
    window->zoomable = 1;
    workspace->order[workspace->count++] = slot;
    workspace->focused = slot;
    return window;
}

static float ease_out(float t);

#define GHOST_CLOSE_US    140000
#define GHOST_MINIMIZE_US 200000

/** @brief The band a window paints: its frame and the shadow past it. */
static tiku_rect_t
window_reach(const tiku_window_t *window)
{
    tiku_rect_t r = window->frame;

    r.x -= TIKU_WINDOW_SHADOW; r.y -= TIKU_WINDOW_SHADOW;
    r.w += 2 * TIKU_WINDOW_SHADOW; r.h += 2 * TIKU_WINDOW_SHADOW;
    return r;
}

/**
 * @brief Keep the last picture of @p window as a ghost bound for @p to.
 *
 * Taken off the surface as it stands, which holds the frame last shown,
 * since the compositor is called between frames.  Nothing is kept with
 * the clock at 0, for a minimized window, or when all four ghost slots
 * are in use.
 */
static void
ghost_take(tiku_workspace_t *ws, tiku_window_t *window, tiku_rect_t to,
           int64_t length)
{
    tiku_rect_t from = window_reach(window);
    int i;

    if (ws->now == 0 || ws->surface == NULL || window->minimized) {
        return;
    }
    for (i = 0; i < 4; i++) {
        if (ws->ghost[i].pixels == NULL) { break; }
    }
    if (i == 4) {
        return;
    }
    ws->ghost[i].pixels = tiku_grab(ws->surface, from);
    if (ws->ghost[i].pixels == NULL) {
        return;
    }
    /* The grab may have been trimmed at the screen's edge. */
    if (from.x < 0) { from.w += from.x; from.x = 0; }
    if (from.y < 0) { from.h += from.y; from.y = 0; }
    if (from.x + from.w > ws->surface->w) { from.w = ws->surface->w - from.x; }
    if (from.y + from.h > ws->surface->h) { from.h = ws->surface->h - from.y; }
    ws->ghost[i].from = from;
    ws->ghost[i].to = to;
    ws->ghost[i].since = ws->now;
    ws->ghost[i].length = length;
}

/** @brief Note motion under @p r: this frame drew it there, the next may. */
static void
motion_add(tiku_workspace_t *ws, tiku_rect_t r)
{
    ws->motion = ws->animating ? tiku_rect_union(ws->motion, r) : r;
    ws->animating = 1;
}

/** @brief Every ghost on its way, laid over the windows; done ones let go. */
static void
ghosts_draw(tiku_workspace_t *ws)
{
    int i;

    for (i = 0; i < 4; i++) {
        int64_t age;
        float t;
        tiku_rect_t at;

        if (ws->ghost[i].pixels == NULL) { continue; }
        age = ws->now - ws->ghost[i].since;
        if (ws->now == 0 || age < 0 || age >= ws->ghost[i].length) {
            tiku_surface_free(ws->ghost[i].pixels);
            ws->ghost[i].pixels = NULL;
            continue;
        }
        t = ease_out((float)age / (float)ws->ghost[i].length);
        at.x = ws->ghost[i].from.x +
               (int)((ws->ghost[i].to.x - ws->ghost[i].from.x) * t);
        at.y = ws->ghost[i].from.y +
               (int)((ws->ghost[i].to.y - ws->ghost[i].from.y) * t);
        at.w = ws->ghost[i].from.w +
               (int)((ws->ghost[i].to.w - ws->ghost[i].from.w) * t);
        at.h = ws->ghost[i].from.h +
               (int)((ws->ghost[i].to.h - ws->ghost[i].from.h) * t);
        if (at.w < 1) { at.w = 1; }
        if (at.h < 1) { at.h = 1; }
        tiku_blend_scaled(ws->surface, at, ws->ghost[i].pixels, 1.0f - t);
        motion_add(ws, tiku_rect_union(ws->ghost[i].from, ws->ghost[i].to));
    }
}

void
tiku_workspace_minimize(tiku_workspace_t *workspace, tiku_window_t *window,
                        tiku_rect_t to)
{
    if (workspace == NULL || window == NULL || !window->open) { return; }
    ghost_take(workspace, window, to, GHOST_MINIMIZE_US);
    window->minimized = 1;
}

void
tiku_workspace_restore(tiku_workspace_t *workspace, tiku_window_t *window)
{
    if (workspace == NULL || window == NULL || !window->open) { return; }
    if (window->minimized) {
        window->minimized = 0;
        window->opened_at = workspace->now;   /* it rises, as if new */
    }
}

void
tiku_workspace_close(tiku_workspace_t *workspace,
                          tiku_window_t *window)
{
    tiku_window_destroy_fn destroy;
    void *context;
    int slot, at, i;

    if (workspace == NULL || window == NULL || !window->open) { return; }
    {
        /* On its way out: the picture shrinks a little where it was. */
        tiku_rect_t to = window_reach(window);
        int dx = to.w / 24, dy = to.h / 24;

        to.x += dx; to.y += dy; to.w -= 2 * dx; to.h -= 2 * dy;
        ghost_take(workspace, window, to, GHOST_CLOSE_US);
    }
    slot = (int)(window - workspace->windows);
    at = order_index(workspace, slot);
    window->open = 0;
    cache_free(workspace, slot);
    if (at >= 0) {
        for (i = at; i < workspace->count - 1; i++) {
            workspace->order[i] = workspace->order[i + 1];
        }
        workspace->count--;
    }
    workspace->focused = (workspace->count > 0)
                             ? workspace->order[workspace->count - 1] : -1;
    destroy = window->destroy;
    context = window->context;
    window->destroy = NULL;
    if (destroy != NULL) { destroy(window, context); }
}

void
tiku_window_set_plain(tiku_window_t *window, int plain)
{
    if (window != NULL) {
        window->plain = plain;
    }
}

void
tiku_window_set_handlers(tiku_window_t *window,
                              tiku_window_event_fn event,
                              tiku_window_close_fn close_requested,
                              tiku_window_destroy_fn destroy)
{
    if (window != NULL) {
        window->event = event;
        window->close_requested = close_requested;
        window->destroy = destroy;
    }
}

int
tiku_window_send(tiku_window_t *window,
                      const tiku_event_t *event)
{
    if (window == NULL || !window->open || event == NULL ||
        window->event == NULL) {
        return 0;
    }
    return window->event(window, event, window->context);
}

int
tiku_workspace_request_close(tiku_workspace_t *workspace,
                                  tiku_window_t *window)
{
    if (workspace == NULL || window == NULL || !window->open) { return 0; }
    if (window->close_requested != NULL &&
        !window->close_requested(window, window->context)) {
        return 0;
    }
    tiku_workspace_close(workspace, window);
    return 1;
}

tiku_rect_t
tiku_window_content(const tiku_window_t *window)
{
    tiku_rect_t content;

    if (window->plain) {
        /* A plain window's content is inside its hairline. */
        content = tiku_inset(window->frame, 1);
        if (content.w < 0) { content.w = 0; }
        if (content.h < 0) { content.h = 0; }
        return content;
    }
    content.x = window->frame.x + TIKU_WINDOW_BORDER;
    content.y = window->frame.y + tiku_window_tab_h() +
                TIKU_WINDOW_BORDER;
    content.w = window->frame.w - 2 * TIKU_WINDOW_BORDER;
    content.h = window->frame.h - tiku_window_tab_h() -
                2 * TIKU_WINDOW_BORDER;
    if (content.w < 0) { content.w = 0; }
    if (content.h < 0) { content.h = 0; }
    return content;
}

void
tiku_window_set_size_controls(tiku_window_t *window,
                                   int resizable, int zoomable)
{
    if (window != NULL) {
        window->resizable = resizable != 0;
        window->zoomable = zoomable != 0;
    }
}

void
tiku_window_set_tab_auto_hide(tiku_window_t *window, int enabled)
{
    if (window) window->auto_hide_tab = !!enabled;
}

tiku_hit_t
tiku_workspace_hit(tiku_workspace_t *workspace, int x, int y,
                        tiku_window_t **out)
{
    int i;

    if (out != NULL) { *out = NULL; }
    if (workspace == NULL) { return TIKU_HIT_NONE; }
    for (i = workspace->count - 1; i >= 0; i--) {
        tiku_window_t *window =
            &workspace->windows[workspace->order[i]];
        tiku_rect_t tab, content;

        if (!window->open || window->minimized) { continue; }
        if (window->plain) {
            /* A plain window is all content. */
            if (!inside(window->frame, x, y)) { continue; }
            if (out != NULL) { *out = window; }
            return TIKU_HIT_CONTENT;
        }
        int show_tab = !window->auto_hide_tab ||
                       window == tiku_workspace_focused(workspace);
        if (!show_tab && y < window->frame.y + tiku_window_tab_h() - 1)
            continue;
        tab = tab_rect(window);
        if (show_tab && inside(tab, x, y)) {
            if (out != NULL) { *out = window; }
            int btn = tiku_window_tab_button();

            if (x < tab.x + btn + 6) {
                return TIKU_HIT_CLOSE;
            }
            if (window->zoomable && x > tab.x + tab.w - btn - 6) {
                return TIKU_HIT_ZOOM;
            }
            return TIKU_HIT_TAB;
        }
        if (!inside(window->frame, x, y)) { continue; }
        if (out != NULL) { *out = window; }
        if (window->resizable &&
            x > window->frame.x + window->frame.w - 16 &&
            y > window->frame.y + window->frame.h - 16) {
            return TIKU_HIT_RESIZE;
        }
        content = tiku_window_content(window);
        return inside(content, x, y) ? TIKU_HIT_CONTENT
                                     : TIKU_HIT_BORDER;
    }
    return TIKU_HIT_NONE;
}

void
tiku_workspace_begin_track(tiku_workspace_t *workspace,
                                tiku_window_t *window,
                                tiku_hit_t what, int x, int y)
{
    if (workspace == NULL || window == NULL) { return; }
    workspace->track_what = what;
    workspace->track_window = (int)(window - workspace->windows);
    workspace->track_x = x;
    workspace->track_y = y;
}

int
tiku_workspace_tracking(const tiku_workspace_t *workspace)
{
    return workspace != NULL && workspace->track_window >= 0;
}

tiku_window_t *tiku_workspace_moving(tiku_workspace_t *ws)
{
    return ws && ws->track_window >= 0 && ws->track_what == TIKU_HIT_TAB
        ? &ws->windows[ws->track_window] : NULL;
}

int
tiku_workspace_track(tiku_workspace_t *workspace, int x, int y)
{
    tiku_window_t *window;
    int dx, dy;

    if (workspace == NULL || workspace->track_window < 0) { return 0; }
    window = &workspace->windows[workspace->track_window];
    dx = x - workspace->track_x;
    dy = y - workspace->track_y;
    if (dx == 0 && dy == 0) { return 0; }
    workspace->track_x = x;
    workspace->track_y = y;
    if (workspace->track_what == TIKU_HIT_TAB) {
        window->frame.x += dx;
        window->frame.y += dy;
        if (window->frame.y < 0) { window->frame.y = 0; }
        if (window->frame.x + 40 > workspace->surface->w) {
            window->frame.x = workspace->surface->w - 40;
        }
        if (window->frame.x + window->frame.w < 40) {
            window->frame.x = 40 - window->frame.w;
        }
        if (window->frame.y + tiku_window_tab_h() >
            workspace->surface->h) {
            window->frame.y = workspace->surface->h -
                              tiku_window_tab_h();
        }
    } else if (workspace->track_what == TIKU_HIT_RESIZE) {
        window->frame.w += dx;
        window->frame.h += dy;
        if (window->frame.w < MIN_WIDTH) { window->frame.w = MIN_WIDTH; }
        if (window->frame.h < MIN_HEIGHT) { window->frame.h = MIN_HEIGHT; }
    } else {
        return 0;
    }
    return 1;
}

void
tiku_workspace_end_track(tiku_workspace_t *workspace)
{
    if (workspace != NULL) {
        workspace->track_window = -1;
        workspace->track_what = TIKU_HIT_NONE;
    }
}

int
tiku_workspace_zoom(tiku_workspace_t *workspace,
                         tiku_window_t *window, int fit_width,
                         int fit_height)
{
    tiku_rect_t was, wanted, content;
    int max_width, max_height;

    if (workspace == NULL || window == NULL) { return 0; }
    was = window->frame;
    wanted = was;
    content = tiku_window_content(window);
    wanted.w = fit_width + (was.w - content.w);
    wanted.h = fit_height + (was.h - content.h);
    max_width = workspace->surface->w - 10 - wanted.x;
    max_height = workspace->surface->h - 10 - wanted.y -
                 tiku_window_tab_h();
    if (workspace->reserved.w > 0 && wanted.x < workspace->reserved.x) {
        int room = workspace->reserved.x - wanted.x - 5;

        if (room < max_width) { max_width = room; }
    }
    if (wanted.w > max_width) { wanted.w = max_width; }
    if (wanted.h > max_height) { wanted.h = max_height; }
    if (wanted.w < 120) { wanted.w = 120; }
    if (wanted.h < 80) { wanted.h = 80; }
    if (wanted.w == was.w && wanted.h == was.h) {
        if (!window->zoom_valid) { return 0; }
        window->frame = window->saved_zoom;
        window->zoom_valid = 0;
        return 1;
    }
    window->saved_zoom = was;
    window->zoom_valid = 1;
    window->frame = wanted;
    return 1;
}

/** @brief Draw a tab's close or (@p zoom) zoom button at @p x, @p y. */
static void
draw_button(tiku_surface_t *surface, int x, int y, int zoom, int active,
            int down)
{
    int side = tiku_window_tab_button();
    tiku_rect_t button = { x, y, side, side };
    tiku_rgb_t face = active ? TIKU_C_TAB : TIKU_C_TAB_IDLE;

    tiku_ui_tab_widget(surface, button,
        zoom ? TIKU_TAB_ZOOM : TIKU_TAB_CLOSE, face, down);
}

/**
 * @brief Copy the content just drawn in @p r into slot @p slot's cache,
 *        when all of @p r is inside the clip and the caches stay within
 *        64 MiB.
 */
static void cache_capture(tiku_workspace_t *ws, int slot, tiku_rect_t r,
                           int active)
{
    tiku_surface_t *s = ws->surface, *p = ws->cache[slot].pixels;
    int sc = s->scale > 0 ? s->scale : 1;
    size_t bytes = (size_t)r.w*r.h*sc*sc*sizeof *s->px;
    ws->cache[slot].valid = 0;
    if (r.x < s->clip.x || r.y < s->clip.y ||
        r.x+r.w > s->clip.x+s->clip.w ||
        r.y+r.h > s->clip.y+s->clip.h) return;
    if (p && (p->w != r.w || p->h != r.h || p->scale != sc)) {
        cache_free(ws, slot); p = NULL;
    }
    if (!p) {
        if (bytes > 64u*1024u*1024u - ws->cache_bytes) return;
        p = tiku_surface_new(r.w, r.h, 0);
        if (!p) return;
        if (sc > 1 && tiku_surface_rescale(p, sc, 0)) {
            tiku_surface_free(p); return;
        }
        p->scale = sc;
        ws->cache[slot].pixels = p; ws->cache_bytes += bytes;
    }
    for (int y = 0; y < r.h*sc; y++)
        memcpy(p->px+(size_t)y*r.w*sc,
            s->px+((size_t)r.y*sc+y)*s->w*sc+(size_t)r.x*sc,
            (size_t)r.w*sc*sizeof *p->px);
    ws->cache[slot].valid = 1; ws->cache[slot].active = active;
}

/**
 * @brief Draw window @p w's content in @p r: from its cache under
 *        TIKU_CACHE_REUSE when the cache matches, otherwise through its
 *        draw callback, caching the result unless caching is off.  A
 *        recording surface always draws.
 */
static void draw_content(tiku_workspace_t *ws, tiku_window_t *w,
                          tiku_rect_t r, int active)
{
    tiku_surface_t *s = ws->surface;
    int slot = (int)(w-ws->windows);
    tiku_surface_t *p = ws->cache[slot].pixels;
    if (!w->draw || r.w <= 0 || r.h <= 0) return;
    tiku_clip_set(s, r);
    if (s->clip.w <= 0 || s->clip.h <= 0) { tiku_clip_reset(s); return; }
    if (!s->record && ws->cache_mode == TIKU_CACHE_REUSE &&
        ws->cache[slot].valid && ws->cache[slot].active == active && p &&
        p->w == r.w && p->h == r.h &&
        p->scale == (s->scale > 0 ? s->scale : 1)) {
        tiku_blit(s, r.x, r.y, p); ws->cache_hits++;
    } else {
        ws->content_draws++;
        w->draw(w, s, r, w->context);
        tiku_clip_set(s, r);
        if (!s->record && ws->cache_mode != TIKU_CACHE_OFF)
            cache_capture(ws, slot, r, active);
    }
    tiku_clip_reset(s);
}

#define RISE_US 150000          /* a window opening lifts into place  */
#define RISE_PX 8

/** @brief Cubic ease-out of @p t. */
static float
ease_out(float t)
{
    float u = 1.0f - t;

    return 1.0f - u * u * u;
}

/**
 * @brief Draw @p window's shadow, frame, tab and content: rounded corners
 *        where no tab stands and a hairline edge.  @p lift scales the
 *        shadow's strength.
 */
static void
draw_window_at(tiku_workspace_t *workspace, tiku_window_t *window,
               int active, float lift)
{
    tiku_surface_t *surface = workspace->surface;
    const tiku_font_t *font = tiku_font_bold();
    tiku_rect_t tab = tab_rect(window);
    int tab_h = tiku_window_tab_h();
    int btn = tiku_window_tab_button();
    int show_tab = !window->auto_hide_tab || active;
    tiku_rect_t body = { window->frame.x,
        window->frame.y + tab_h - 1, window->frame.w,
        window->frame.h - tab_h + 1 };
    tiku_rect_t content = tiku_window_content(window);
    tiku_rgb_t tab_color = active ? TIKU_C_TAB
                                       : TIKU_C_TAB_IDLE;
    tiku_rgb_t dark = tiku_tint(TIKU_C_PANEL, 1.40f);
    int radius = TIKU_WINDOW_RADIUS;

    if (window->plain) {
        /* A plain window: a shadow, the panel and a hairline. */
        (void)active;
        tiku_shadow(surface, window->frame, TIKU_WINDOW_SHADOW_RADIUS,
                    radius + 1, 0.45f * lift);
        tiku_fill_round(surface, window->frame, radius + 1, TIKU_CORNER_ALL,
                        TIKU_C_PANEL);
        tiku_frame_round(surface, window->frame, radius + 1, TIKU_CORNER_ALL,
                         dark);
        draw_content(workspace, window, content, active);
        return;
    }
    /* The shadow is the body's; the tab stands in the band above it. */
    tiku_shadow(surface, body, TIKU_WINDOW_SHADOW_RADIUS, radius,
                (active ? 0.45f : 0.30f) * lift);
    tiku_fill_round(surface, body, radius,
                    show_tab ? TIKU_CORNER_ALL & ~TIKU_CORNER_TL :
                               TIKU_CORNER_ALL, TIKU_C_PANEL);
    tiku_frame_round(surface, body, radius,
                     show_tab ? TIKU_CORNER_ALL & ~TIKU_CORNER_TL :
                                TIKU_CORNER_ALL, dark);
    if (show_tab) {
        tiku_fill_round(surface, tab, radius, TIKU_CORNER_TOP, tab_color);
        tiku_frame_round(surface, tab, radius, TIKU_CORNER_TOP, dark);
        /* The tab meets the body along one row of its fill. */
        tiku_hline(surface, tab.x + 1, tab.y + tab.h - 1, tab.w - 2,
                        tab_color);
        draw_button(surface, tab.x + 4, tab.y + (tab_h - btn) / 2, 0, active,
                    window->pressed == TIKU_HIT_CLOSE);
        if (window->zoomable) {
            draw_button(surface, tab.x + tab.w - btn - 4,
                        tab.y + (tab_h - btn) / 2, 1, active,
                        window->pressed == TIKU_HIT_ZOOM);
        }
        tiku_rect_t label = { tab.x + btn + 8, tab.y,
            tab.w - btn - 16 - (window->zoomable ? btn + 8 : 0), tab_h };

        tiku_clip_set(surface, label);
        (void)tiku_text_centered(surface, font, label, window->title,
                                      TIKU_C_TABTEXT);
        tiku_clip_reset(surface);
    }
    draw_content(workspace, window, content, active);
    if (window->resizable) {
        /* The resize grip, inside the rounded corner. */
        int gx = window->frame.x + window->frame.w - 16;
        int gy = window->frame.y + window->frame.h - 16;
        int i;

        for (i = 0; i < 3; i++) {
            tiku_hline(surface, gx + i * 3, gy + 10, 10 - i * 3,
                tiku_tint(TIKU_C_PANEL, TIKU_DARKEN_2));
            tiku_hline(surface, gx + i * 3, gy + 11, 10 - i * 3,
                tiku_tint(TIKU_C_PANEL, TIKU_LIGHTEN_MAX));
        }
    }
}

/**
 * @brief Draw @p window; one opened within RISE_US rises the last
 *        RISE_PX pixels into place as its shadow deepens.  Only the
 *        picture moves: the frame that hit tests use stays put.
 */
static void
draw_window(tiku_workspace_t *workspace, tiku_window_t *window,
            int active)
{
    int64_t age = workspace->now - window->opened_at;

    if (workspace->now != 0 && window->opened_at != 0 &&
        age >= 0 && age < RISE_US) {
        tiku_rect_t kept = window->frame;
        float t = ease_out((float)age / (float)RISE_US);

        window->frame.y += (int)((1.0f - t) * RISE_PX + 0.5f);
        draw_window_at(workspace, window, active, t);
        window->frame = kept;
        kept.h += RISE_PX;
        motion_add(workspace, kept);
        return;
    }
    draw_window_at(workspace, window, active, 1.0f);
}

void
tiku_workspace_set_now(tiku_workspace_t *workspace, int64_t now_us)
{
    if (workspace != NULL) {
        workspace->now = now_us;
    }
}

int
tiku_workspace_animating(const tiku_workspace_t *workspace)
{
    return workspace != NULL && workspace->animating;
}

tiku_rect_t
tiku_workspace_motion(const tiku_workspace_t *workspace)
{
    return (workspace != NULL && workspace->animating)
        ? workspace->motion : (tiku_rect_t){ 0, 0, 0, 0 };
}

void
tiku_workspace_set_backdrop(tiku_workspace_t *workspace,
    void (*draw)(tiku_surface_t *surface, void *context), void *context)
{
    if (workspace != NULL) {
        workspace->backdrop = draw;
        workspace->backdrop_context = context;
    }
}

void
tiku_workspace_draw(tiku_workspace_t *workspace)
{
    tiku_rect_t all;
    int i;

    if (workspace == NULL || workspace->surface == NULL) { return; }
    all = (tiku_rect_t){ 0, 0, workspace->surface->w,
                              workspace->surface->h };
    tiku_fill(workspace->surface, all, TIKU_C_BACKDROP);
    if (workspace->backdrop != NULL) {
        workspace->backdrop(workspace->surface,
                            workspace->backdrop_context);
    }
    workspace->animating = 0;
    for (i = 0; i < workspace->count; i++) {
        tiku_window_t *window =
            &workspace->windows[workspace->order[i]];

        tiku_rect_t clip = workspace->surface->clip;
        tiku_rect_t f = window->frame;
        /* The shadow reaches past the frame, so a window just outside
         * the clip may still lay something inside it. */
        f.x -= TIKU_WINDOW_SHADOW; f.y -= TIKU_WINDOW_SHADOW;
        f.w += 2 * TIKU_WINDOW_SHADOW; f.h += 2 * TIKU_WINDOW_SHADOW;
        if (window->open && !window->minimized && !window->hidden &&
            f.x < clip.x+clip.w && f.y < clip.y+clip.h &&
            f.x+f.w > clip.x && f.y+f.h > clip.y) {
            draw_window(workspace, window,
                        workspace->order[i] == workspace->focused);
        }
    }
    ghosts_draw(workspace);
}

void
tiku_workspace_draw_window(tiku_workspace_t *workspace,
                           tiku_window_t *window, tiku_surface_t *out)
{
    tiku_surface_t *shown;

    if (workspace == NULL || window == NULL || out == NULL || !window->open)
        return;
    shown = workspace->surface;
    workspace->surface = out;       /* drawn as shown, onto out */
    draw_window_at(workspace, window, 1, 0.0f);
    workspace->surface = shown;
}

/**
 * @brief Draw @p window's content once onto a recording surface of the
 *        workspace's size, never the shown one, and return the list, or
 *        NULL when there is nothing to record.
 */
static tiku_dl_t *
workspace_record(tiku_workspace_t *workspace, tiku_window_t *window)
{
    tiku_surface_t *scratch;
    tiku_dl_t *dl;
    tiku_rect_t content;

    if (workspace == NULL || workspace->surface == NULL ||
        window == NULL || !window->open || window->draw == NULL) {
        return NULL;
    }
    content = tiku_window_content(window);
    if (content.w <= 0 || content.h <= 0) {
        return NULL;
    }
    /* A scratch surface of the workspace's size: recording leaves the
     * shown pixels alone. */
    scratch = tiku_surface_new(workspace->surface->w,
                                    workspace->surface->h, TIKU_C_PANEL);
    if (scratch == NULL) {
        return NULL;
    }
    dl = tiku_dl_new();
    if (dl == NULL) {
        tiku_surface_free(scratch);
        return NULL;
    }
    scratch->record = dl;
    tiku_clip_set(scratch, content);
    window->draw(window, scratch, content, window->context);
    tiku_clip_reset(scratch);
    scratch->record = NULL;
    tiku_surface_free(scratch);
    return dl;
}

size_t
tiku_workspace_narrate(tiku_workspace_t *workspace,
                            tiku_window_t *window, char *out, size_t max)
{
    tiku_dl_t *dl;
    size_t used;

    if (out == NULL || max == 0u) {
        return 0u;
    }
    out[0] = '\0';
    dl = workspace_record(workspace, window);
    if (dl == NULL) {
        return 0u;
    }
    used = tiku_dl_say(dl, out, max);
    tiku_dl_free(dl);
    return used;
}

size_t
tiku_workspace_controls(tiku_workspace_t *workspace,
                             tiku_window_t *window, char *out, size_t max)
{
    tiku_dl_t *dl;
    size_t used;

    if (out == NULL || max == 0u) {
        return 0u;
    }
    out[0] = '\0';
    dl = workspace_record(workspace, window);
    if (dl == NULL) {
        return 0u;
    }
    used = tiku_dl_facts(dl, out, max);
    tiku_dl_free(dl);
    return used;
}

void
tiku_window_publish_menus(struct tiku_window *window,
                               const tiku_menuset_t *menus,
                               tiku_menu_pick_fn pick, void *context)
{
    if (window == NULL) {
        return;
    }
    if (menus == NULL) {
        window->has_menus = 0;
        window->menu_pick = NULL;
        window->menu_pick_context = NULL;
        return;
    }
    window->menus = *menus;
    window->has_menus = 1;
    window->menu_pick = pick;
    window->menu_pick_context = context;
}

const tiku_menuset_t *
tiku_window_menus(struct tiku_window *window)
{
    return (window != NULL && window->has_menus) ? &window->menus : NULL;
}
