/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_window_app.c - publishing a private workspace's windows through
 * the application services: surfaces, titles, menus, the clipboard and
 * focus.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_window_app.h"
#include "tiku_remote.h"
#include "tiku_dl.h"
#include "tiku_ui.h"
#include "tiku_clipboard.h"
#include <stdlib.h>
#include <string.h>

/* One published window: the toolkit window, the desktop's id for it, and
 * what was last sent. */
typedef struct {
    tiku_window_t *window;
    uint32_t id;
    uint64_t generation;
    tiku_surface_t *surface;
    tiku_dl_t *list;
    tiku_menuset_t menus;
    char title[TIKU_WINDOW_TITLE_MAX];
    int width, height, resize, zoom, plain, have_menus;
} app_window_t;

struct tiku_window_app {
    tiku_app_services_t services;
    tiku_workspace_t *workspace;
    tiku_surface_t *canvas;
    app_window_t windows[TIKU_WINDOW_MAX];
    unsigned clipboard_stamp;
    unsigned host_stamp;        /* the host clipboard's stamp, as imported */
    int host_imported;          /* whether it has been imported at all */
    uint64_t focused_generation;
};

/** @brief Before a user action: take the host's clipboard into the
 *         toolkit's when its stamp has moved, and note the toolkit's
 *         stamp, so a large clipboard costs nothing on events that leave
 *         it alone. */
static void clipboard_begin(tiku_window_app_t *a)
{
    unsigned stamp = 0;
    if (a->services.clip_text) {
        const char *text = a->services.clip_text(a->services.ctx, &stamp);
        if (!a->host_imported || stamp != a->host_stamp) {
            if (strcmp(text, tiku_clipboard_text())) tiku_clipboard_set(text);
            a->host_stamp = stamp;
            a->host_imported = 1;
        }
    }
    a->clipboard_stamp = tiku_clipboard_stamp();
}

/** @brief After a user action: send the toolkit's clipboard to the host
 *         when the action changed it. */
static void clipboard_end(tiku_window_app_t *a)
{
    if (a->clipboard_stamp != tiku_clipboard_stamp() && a->services.clip_set)
        a->services.clip_set(a->services.ctx, tiku_clipboard_text());
}

/** @brief Set @p w's frame so its content starts at 0,0 and is @p width
 *         by @p height. */
static void geometry(tiku_window_t *w, int width, int height)
{
    int border = w->plain ? 1 : TIKU_WINDOW_BORDER;
    int tab = w->plain ? 0 : tiku_window_tab_h();
    w->frame = (tiku_rect_t){-border, -border - tab,
                           width + 2 * border, height + 2 * border + tab};
}

tiku_window_app_t *tiku_window_app_new(const tiku_app_services_t *services)
{
    if (!services || !services->open || !services->frame) return NULL;
    tiku_window_app_t *a = calloc(1, sizeof *a);
    if (!a) return NULL;
    a->services = *services;
    a->canvas = tiku_surface_new(1, 1, TIKU_C_PANEL);
    a->workspace = a->canvas ? tiku_workspace_new(a->canvas) : NULL;
    if (!a->workspace) { tiku_window_app_free(a); return NULL; }
    return a;
}

void tiku_window_app_free(tiku_window_app_t *a)
{
    if (!a) return;
    if (a->workspace) tiku_workspace_free(a->workspace);
    for (int i = 0; i < TIKU_WINDOW_MAX; i++) {
        tiku_surface_free(a->windows[i].surface);
        tiku_dl_free(a->windows[i].list);
    }
    tiku_surface_free(a->canvas);
    free(a);
}

tiku_workspace_t *tiku_window_app_workspace(tiku_window_app_t *a)
{ return a ? a->workspace : NULL; }

uint32_t tiku_window_app_id(tiku_window_app_t *a, tiku_window_t *w)
{
    for (int i = 0; a && i < TIKU_WINDOW_MAX; i++)
        if (a->windows[i].window == w) return a->windows[i].id;
    return 0;
}

/**
 * @brief Publish one window: open it at the desktop when new, send a
 *        changed title, size or menu set, and draw and send a frame when
 *        @p dirty or its surface is new.
 *
 * A new surface is allocated before the old one is freed.
 * @return 0 when its size is out of range, or it could not be opened or
 *         its surface made.
 */
static int publish(tiku_window_app_t *a, app_window_t *slot, int dirty)
{
    tiku_window_t *w = slot->window;
    tiku_rect_t content = tiku_window_content(w);
    tiku_app_services_t *s = &a->services;
    int resized = content.w != slot->width || content.h != slot->height;
    if (content.w < 1 || content.h < 1 || content.w > TIKU_REMOTE_MAX_DIM ||
        content.h > TIKU_REMOTE_MAX_DIM) return 0;
    if (!slot->id) {
        slot->id = s->open(s->ctx, w->title, content.w, content.h);
        if (!slot->id) return 0;
        if (w->plain && s->place)
            s->place(s->ctx, slot->id, TIKU_APP_PLACE_ANNOUNCE);
    }
    if (resized || strcmp(slot->title, w->title) ||
        slot->resize != w->resizable || slot->zoom != w->zoomable) {
        if (s->configure)
            s->configure(s->ctx, slot->id, w->title,
                         resized ? content.w : 0, resized ? content.h : 0,
                         w->resizable, w->zoomable);
        else if (w->resizable && s->sizeable) s->sizeable(s->ctx, slot->id);
        memcpy(slot->title, w->title, sizeof slot->title);
        slot->resize = w->resizable; slot->zoom = w->zoomable;
    }
    if (!slot->surface || slot->surface->w != content.w ||
        slot->surface->h != content.h) {
        tiku_surface_t *next = tiku_surface_new(content.w, content.h, TIKU_C_PANEL);
        if (!next) return 0;
        tiku_surface_free(slot->surface); slot->surface = next;
        dirty = 1;
    }
    slot->width = content.w; slot->height = content.h;
    geometry(w, content.w, content.h);
    if (w->has_menus && s->menus && (!slot->have_menus ||
        memcmp(&slot->menus, &w->menus, sizeof w->menus))) {
        s->menus(s->ctx, slot->id, &w->menus);
        slot->menus = w->menus; slot->have_menus = 1;
    }
    if (dirty && w->draw) {
        if (!slot->list && s->present) slot->list = tiku_dl_new();
        if (slot->list) tiku_dl_clear(slot->list);
        slot->surface->record = slot->list;
        tiku_clip_reset(slot->surface);
        w->draw(w, slot->surface, tiku_window_content(w), w->context);
        slot->surface->record = NULL;
        if (s->present)
            s->present(s->ctx, slot->id, slot->list, slot->surface->px,
                       content.w, content.h);
        else s->frame(s->ctx, slot->id, slot->surface->px, content.w, content.h);
    }
    return 1;
}

int tiku_window_app_sync(tiku_window_app_t *a, int dirty)
{
    if (!a) return 0;
    for (int i = 0; i < TIKU_WINDOW_MAX; i++) {
        app_window_t *slot = &a->windows[i];
        if (slot->window && (!slot->window->open ||
            slot->generation != slot->window->generation)) {
            if (slot->id && a->services.close)
                a->services.close(a->services.ctx, slot->id);
            tiku_surface_free(slot->surface);
            tiku_dl_free(slot->list);
            memset(slot, 0, sizeof *slot);
        }
    }
    for (int i = 0; i < tiku_workspace_count(a->workspace); i++) {
        tiku_window_t *w = tiku_workspace_at(a->workspace, i);
        app_window_t *slot = NULL;
        for (int j = 0; j < TIKU_WINDOW_MAX; j++)
            if (a->windows[j].window == w) { slot = &a->windows[j]; break; }
        if (!slot) for (int j = 0; j < TIKU_WINDOW_MAX; j++)
            if (!a->windows[j].window) { slot = &a->windows[j]; break; }
        if (!slot) return 0;
        slot->window = w;
        slot->generation = w->generation;
        if (!publish(a, slot, dirty)) return 0;
    }
    tiku_window_t *focused = tiku_workspace_focused(a->workspace);
    if (focused && focused->generation != a->focused_generation) {
        a->focused_generation = focused->generation;
        if (a->services.raise_window)
            a->services.raise_window(a->services.ctx, tiku_window_app_id(a, focused));
    }
    return tiku_workspace_count(a->workspace) > 0;
}

int tiku_window_app_event(tiku_window_app_t *a, uint32_t id,
                          const tiku_event_t *event)
{
    for (int i = 0; a && i < TIKU_WINDOW_MAX; i++) {
        app_window_t *slot = &a->windows[i];
        if (slot->id != id || !slot->window || !slot->window->open) continue;
        if (event->type == TIKU_EVENT_ACTIVATED ||
            event->type == TIKU_EVENT_POINTER_DOWN ||
            event->type == TIKU_EVENT_KEY_DOWN)
            tiku_workspace_activate(a->workspace, slot->window);
        if (event->type == TIKU_EVENT_DEACTIVATED)
            tiku_workspace_deactivate(a->workspace, slot->window);
        if (event->type == TIKU_EVENT_KEY_DOWN &&
            (event->modifiers & TIKU_MOD_CMD) &&
            (event->key == 'w' || event->key == 'q')) {
            tiku_workspace_request_close(a->workspace, slot->window);
            return 1;
        }
        if (event->type == TIKU_EVENT_RESIZE && event->width > 0 &&
            event->height > 0 && event->width <= TIKU_REMOTE_MAX_DIM &&
            event->height <= TIKU_REMOTE_MAX_DIM) {
            geometry(slot->window, event->width, event->height);
            slot->width = event->width; slot->height = event->height;
        }
        clipboard_begin(a);
        int result = tiku_window_send(slot->window, event);
        clipboard_end(a);
        return result;
    }
    return 0;
}

void tiku_window_app_pick(tiku_window_app_t *a, uint32_t id, int command)
{
    for (int i = 0; a && i < TIKU_WINDOW_MAX; i++) {
        app_window_t *s = &a->windows[i];
        if (s->id == id && s->window && s->window->open && s->window->menu_pick) {
            clipboard_begin(a);
            s->window->menu_pick(s->window, command, s->window->menu_pick_context);
            clipboard_end(a);
        }
    }
}

void tiku_window_app_closed(tiku_window_app_t *a, uint32_t id)
{
    for (int i = 0; a && i < TIKU_WINDOW_MAX; i++) {
        app_window_t *s = &a->windows[i];
        if (s->id != id || !s->window) continue;
        s->id = 0;
        tiku_workspace_close(a->workspace, s->window);
    }
}
