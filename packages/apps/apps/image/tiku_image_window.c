/* tiku_image_window.c -- TikuImage's viewer: an image read and decoded on
 * a worker, shown fitted, zoomed, panned or turned, its file read again
 * every two seconds while Watch is on.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#include "tiku_image_window.h"
#include "tiku_dialog.h"
#include "tiku_dl.h"
#include "tiku_font.h"
#include "tiku_iconpaint.h"
#include "tiku_app_product.h"
#include "tiku_image.h"
#include "tiku_model.h"
#include "tiku_text_layout_paint.h"
#include "tiku_ui.h"
#include "tiku_work.h"
#include <fcntl.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

enum { IV_OPEN = 1, IV_RELOAD, IV_FIT, IV_ACTUAL, IV_OUT, IV_IN,
       IV_ROTATE, IV_WATCH, IV_CLOSE };
typedef struct image_job image_job_t;
/** @brief The viewer's state; only the loop touches it. */
struct tiku_image_window {
    tiku_workspace_t *workspace;
    tiku_window_t *window;
    int *dirty, refs, busy, generation, fit, rotation, watching;
    int dragging, drag_x, drag_y, hover, cache_valid;
    double zoom, pan_x, pan_y;
    int64_t next;
    char path[TIKU_PATH_MAX], label[TIKU_PATH_MAX], status[256];
    tiku_image_t image, cache;
    tiku_rect_t viewport, zoom_frame;
    tiku_dialog_t dialog;
    struct stat stamp;
    size_t bytes;
    void (*choose)(void *ctx);
    void *ctx;
};
/** @brief One read and decode on a worker: what was asked, and what came
 *         back. */
struct image_job {
    tiku_image_window_t *owner;
    int generation, same, unchanged, watch;
    char path[TIKU_PATH_MAX], label[TIKU_PATH_MAX], error[256];
    unsigned char *data;
    size_t len;
    struct stat before, stamp;
    tiku_image_t image;
};

static void image_menus(tiku_image_window_t *c);
static void image_action(tiku_image_window_t *c, int id);

/** @brief Whether (@p x, @p y) lies in @p r, its right and bottom edges
 *         outside. */
static int image_contains(tiku_rect_t r, int x, int y)
{
    return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h;
}

/** @brief The view changed: drop the drawn cache and ask for a redraw. */
static void image_changed(tiku_image_window_t *c)
{
    c->cache_valid = 0;
    if (c->dirty) *c->dirty = 1;
}

void tiku_image_window_note(tiku_image_window_t *c, const char *text)
{
    if (!c) return;
    snprintf(c->status, sizeof c->status, "%s", text ? text : "");
    if (c->dirty) *c->dirty = 1;
}

/** @brief Drop a hold on the viewer; the last one frees it and its
 *         images. */
static void image_unref(tiku_image_window_t *c)
{
    if (--c->refs) return;
    tiku_image_free(&c->image);
    tiku_image_free(&c->cache);
    free(c);
}

#include "tiku_image_io.inl"
#include "tiku_image_paint.inl"

/** @brief Set the zoom to @p scale, kept between 1/16 and 16, and scale
 *         the pan with it so the view's centre stays; Fit goes off. */
static void image_scale(tiku_image_window_t *c, double scale)
{
    double old = image_zoom(c);
    c->fit = 0;
    c->zoom = fmax(0.0625, fmin(16.0, scale));
    c->pan_x *= c->zoom / old;
    c->pan_y *= c->zoom / old;
    image_changed(c);
}

/** @brief Carry out command @p id, from a button, a menu or a key. */
static void image_action(tiku_image_window_t *c, int id)
{
    if (id == IV_OPEN) {
        if (c->choose && !c->busy) c->choose(c->ctx);
    } else if (id == IV_CLOSE) tiku_image_window_close(c);
    else if (id == IV_RELOAD) {
        if (c->path[0]) (void)image_request(c, c->path, NULL, 0, 0);
    } else if (c->image.px) {
        if (id == IV_FIT) { c->fit = 1; c->pan_x = c->pan_y = 0; }
        if (id == IV_ACTUAL) image_scale(c, 1);
        if (id == IV_IN) image_scale(c, image_zoom(c) * 1.25);
        if (id == IV_OUT) image_scale(c, image_zoom(c) / 1.25);
        if (id == IV_ROTATE) {
            c->rotation = (c->rotation + 1) % 4;
            c->pan_x = c->pan_y = 0;
        }
        if (id == IV_WATCH && c->path[0]) {
            c->watching = !c->watching; c->next = 0;
            tiku_image_window_note(c, c->watching ?
                "Watching this local snapshot every 2 seconds." :
                "Snapshot refresh paused.");
        }
        image_changed(c);
    }
    image_menus(c);
}

/** @brief The window's keys: Ctrl+O, Ctrl+R and Ctrl+W; + and - zoom,
 *         0 or f fits, 1 is actual size, r turns, the arrows pan.
 *  @return 1 when taken. */
static int image_key(tiku_image_window_t *c, const tiku_event_t *ev)
{
    unsigned key = ev->key;
    int cmd = !!(ev->modifiers & TIKU_MOD_CMD);
    if (key == 'o' && cmd) image_action(c, IV_OPEN);
    else if (key == 'r' && cmd) image_action(c, IV_RELOAD);
    else if ((key == 'w' || key == 'q') && cmd) image_action(c, IV_CLOSE);
    else if (key == '+' || key == '=') image_action(c, IV_IN);
    else if (key == '-') image_action(c, IV_OUT);
    else if (key == '0' || key == 'f') image_action(c, IV_FIT);
    else if (key == '1') image_action(c, IV_ACTUAL);
    else if (key == 'r' && !cmd) image_action(c, IV_ROTATE);
    else if (key >= TIKU_KEY_UP && key <= TIKU_KEY_RIGHT && c->image.px) {
        if (key == TIKU_KEY_LEFT) c->pan_x += 32;
        if (key == TIKU_KEY_RIGHT) c->pan_x -= 32;
        if (key == TIKU_KEY_UP) c->pan_y += 32;
        if (key == TIKU_KEY_DOWN) c->pan_y -= 32;
        image_changed(c);
    } else return 0;
    return 1;
}

/** @brief Every window event: keys first, then the wheel and drags on the
 *         image, the toolbar's hover, and the dialog's buttons.
 *  @return 1 when taken. */
static int image_event(tiku_window_t *w, const tiku_event_t *ev, void *ctx)
{
    tiku_image_window_t *c = ctx;
    image_layout(c, tiku_window_content(w));
    if (ev->type == TIKU_EVENT_KEY_DOWN && image_key(c, ev)) return 1;
    if (ev->type == TIKU_EVENT_DEACTIVATED) {
        c->dragging = 0; c->hover = 0;
    }
    if (ev->type == TIKU_EVENT_WHEEL && c->image.px &&
        image_contains(c->viewport, ev->x, ev->y)) {
        if (ev->modifiers & TIKU_MOD_CMD)
            image_action(c, ev->dy < 0 ? IV_IN : IV_OUT);
        else if (ev->modifiers & TIKU_MOD_SHIFT) c->pan_x -= ev->dy * 16;
        else c->pan_y -= ev->dy * 16;
        image_changed(c); return 1;
    }
    if (ev->type == TIKU_EVENT_POINTER_DOWN && ev->button == 1 &&
        c->image.px && image_contains(c->viewport, ev->x, ev->y)) {
        c->dragging = 1; c->drag_x = ev->x; c->drag_y = ev->y;
        return 1;
    }
    if (ev->type == TIKU_EVENT_POINTER_UP) c->dragging = 0;
    if (ev->type == TIKU_EVENT_POINTER_MOVE && c->dragging) {
        c->pan_x += ev->x - c->drag_x; c->pan_y += ev->y - c->drag_y;
        c->drag_x = ev->x; c->drag_y = ev->y;
        image_changed(c); return 1;
    }
    if (ev->type == TIKU_EVENT_POINTER_MOVE) {
        int hover = 0;
        for (int i = 0; i < c->dialog.count; i++)
            if (image_contains(c->dialog.control[i].rect, ev->x, ev->y))
                hover = c->dialog.control[i].id;
        if (hover != c->hover) {
            c->hover = hover;
            if (c->dirty) *c->dirty = 1;
        }
    }
    tiku_dialog_result_t r = tiku_dialog_event(&c->dialog, ev);
    if (r.activated >= 0) image_action(c, r.activated);
    if (r.taken && c->dirty) *c->dirty = 1;
    return r.taken;
}

/** @brief The window went away: forget it and the image, and stop the
 *         refresh. */
static void image_window_gone(tiku_window_t *w, void *ctx)
{
    tiku_image_window_t *c = ctx;
    (void)w;
    c->window = NULL; c->generation++; c->watching = 0;
    c->dragging = 0; c->path[0] = c->label[0] = c->status[0] = 0;
    c->fit = 1; c->rotation = 0; c->pan_x = c->pan_y = 0;
    tiku_image_free(&c->image); tiku_image_free(&c->cache);
    image_changed(c);
}

/** @brief A menu pick, carried out as its command. */
static void image_menu_pick(tiku_window_t *w, int id, void *ctx)
{
    (void)w; image_action(ctx, id);
}

/** @brief Publish the File and View menus, each item enabled as its
 *         button is. */
static void image_menus(tiku_image_window_t *c)
{
    if (!c->window) return;
    tiku_menuset_t menus = {0};
    menus.nmenu = 2;
    snprintf(menus.menu[0].title, sizeof menus.menu[0].title, "File");
    snprintf(menus.menu[1].title, sizeof menus.menu[1].title, "View");
    for (int id = IV_OPEN; id <= IV_CLOSE; id++) {
        int view = id >= IV_FIT && id <= IV_WATCH;
        tiku_menu_list_t *m = &menus.menu[view];
        tiku_menu_entry_t *e = &m->item[m->nitem++];
        snprintf(e->label, sizeof e->label, "%s", image_label(id));
        e->command = id; e->enabled = image_enabled(c, id);
        e->sc = id == IV_OPEN ? 'o' : id == IV_RELOAD ? 'r' :
                id == IV_CLOSE ? 'w' : 0;
        e->mods = e->sc ? TIKU_MOD_CMD : 0;
        e->marked = (id == IV_FIT && c->fit) ||
                    (id == IV_WATCH && c->watching);
    }
    tiku_window_publish_menus(c->window, &menus, image_menu_pick, c);
}

tiku_image_window_t *tiku_image_window_new(
    tiku_workspace_t *workspace, int *dirty,
    void (*choose)(void *ctx), void *ctx)
{
    tiku_image_window_t *c = calloc(1, sizeof *c);
    if (!c) return NULL;
    c->workspace = workspace; c->dirty = dirty; c->refs = 1;
    c->choose = choose; c->ctx = ctx; c->fit = 1; c->zoom = 1;
    tiku_dialog_init(&c->dialog);
    (void)tiku_app_product_register_icons();
    return c;
}

void tiku_image_window_open(tiku_image_window_t *c)
{
    if (!c || !c->workspace) return;
    if (!c->window) {
        c->window = tiku_workspace_open(c->workspace, TIKU_PRODUCT_IMAGE_NAME,
            (tiku_rect_t){95, 76, 740, 520}, image_draw, c, c);
        if (!c->window) return;
        tiku_window_set_handlers(c->window, image_event, NULL,
                                 image_window_gone);
        tiku_window_set_size_controls(c->window, 1, 1);
    }
    tiku_workspace_activate(c->workspace, c->window);
    image_menus(c);
    image_changed(c);
}

void tiku_image_window_close(tiku_image_window_t *c)
{
    if (c && c->window) tiku_workspace_close(c->workspace, c->window);
}

void tiku_image_window_destroy(tiku_image_window_t *c)
{
    if (!c) return;
    tiku_image_window_close(c);
    c->generation++; c->workspace = NULL; c->dirty = NULL;
    c->choose = NULL; c->ctx = NULL;
    image_unref(c);
}

tiku_window_t *tiku_image_window_handle(tiku_image_window_t *c)
{
    return c ? c->window : NULL;
}

const char *tiku_image_window_path(tiku_image_window_t *c)
{
    return c ? c->path : "";
}

int tiku_image_file_supported(const char *path)
{
    const char *ext = path ? strrchr(path, '.') : NULL;
    return ext && (!strcasecmp(ext, ".png") || !strcasecmp(ext, ".jpg") ||
                   !strcasecmp(ext, ".jpeg") || !strcasecmp(ext, ".bmp"));
}

int tiku_image_window_zoom(tiku_image_window_t *c, tiku_rect_t area)
{
    if (!c || !c->window || area.w < 180 || area.h < 120) return 0;
    tiku_window_t *w = c->window;
    if (w->zoom_valid && !memcmp(&w->frame, &c->zoom_frame, sizeof area)) {
        w->frame = w->saved_zoom; w->zoom_valid = 0;
    } else {
        w->saved_zoom = w->frame; w->zoom_valid = 1;
        w->frame = c->zoom_frame = tiku_inset(area, 2);
    }
    image_changed(c);
    return 1;
}

void tiku_image_window_tick(tiku_image_window_t *c, int64_t now)
{
    if (!c || !c->window || c->window->minimized || !c->watching ||
        c->busy || now < c->next || !c->path[0]) return;
    c->next = now + 2000000;
    (void)image_request(c, c->path, NULL, 0, 1);
}
