/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_desktop_prefs_window.c - the Desktop preferences window.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_font.h"
#include "tiku_gfx.h"
#include "tiku_strings.h"
#include "tiku_trk_desktop_prefs_window.h"
#include "tiku_trk_poseview.h"
#include "tiku_ui.h"
#include "tiku_text_layout_paint.h"
#include <stdio.h>
#include <string.h>

#include "tiku_iconpaint.h"
#include "tiku_trk_task.h"
#include <stdlib.h>

#define PAD 12
#define GAP 6
#define STOCK_MAX 4
#define THUMB_W 96
#define THUMB_H 56
enum {
    NONE_TILE = 1,
    STOCK_TILE,
    CUSTOM_TILE = STOCK_TILE + STOCK_MAX,
    MODE_TILE,
    SIZE_TILE = MODE_TILE + 4,
    DEFAULTS = SIZE_TILE + 4,
    REVERT,
    DIGIT,
    ERASE = DIGIT + 10,
    ACCEPT,
    CANCEL
};

static const int sizes[] = {32, 48, 64};

typedef struct {
    tiku_rect_t pictures, modes, sizes, status, defaults, revert;
} desk_layout_t;

/** @brief Lay out the gallery, placement and size rows above the action
 * buttons. */
static desk_layout_t
layout(tiku_rect_t content)
{
    desk_layout_t r;
    int text = tiku_font_bold()->height, h = tiku_ui_button_h();
    int x = content.x + PAD, width = content.w - 2 * PAD;
    int y = content.y + PAD + text + 4;
    r.pictures = (tiku_rect_t){x, y, width, 56};
    y += r.pictures.h + PAD + text + 4;
    r.modes = (tiku_rect_t){x, y, width, 42};
    y += r.modes.h + PAD + text + 4;
    r.sizes = (tiku_rect_t){x, y, width, 46};
    r.status = (tiku_rect_t){x, y + r.sizes.h + 4, width,
                             tiku_font_plain()->height * 2 + 2};
    y = r.status.y + r.status.h + 4;
    r.defaults = (tiku_rect_t){
        x, y, tiku_ui_button_w(tiku_str("button.defaults"), 95), h};
    r.revert =
        (tiku_rect_t){x + r.defaults.w + GAP, y,
                      tiku_ui_button_w(tiku_str("button.revert"), 90), h};
    return r;
}

/** @brief Divide a row into equal hit targets separated by gutters. */
static tiku_rect_t
cell(tiku_rect_t row, int i, int count)
{
    int stride = row.w + GAP;
    int x = stride * i / count;
    return (tiku_rect_t){row.x + x, row.y, stride * (i + 1) / count - x - GAP,
                         row.h};
}

/** @brief Desktop space available between the menu bar, Deskbar and panel edge.
 */
static tiku_rect_t
work_area(const tiku_trk_desktop_prefs_window_t *c)
{
    tiku_trk_rect_t area = tiku_trk_desktop_view_area(c->desktop_view);
    int top = c->desktop_view ? c->desktop_view->top_inset : 0;
    if (area.w <= 0 || area.h <= 0)
        return (tiku_rect_t){0, top, c->screen_w, c->screen_h - top};
    if (area.y < top) {
        area.h -= top - area.y;
        area.y = top;
    }
    if (area.h > c->screen_h - area.y)
        area.h = c->screen_h - area.y;
    return (tiku_rect_t){area.x, area.y, area.w, area.h};
}

/** @brief Keep the leading edge reachable when a frame exceeds the available
 * span. */
static int
fit_origin(int position, int start, int space, int size)
{
    if (position > start + space - size)
        position = start + space - size;
    return position < start ? start : position;
}

/** @brief Fit the controls to the work area and keep an open window's position
 * when it fits. */
static tiku_rect_t
fitted_frame(tiku_trk_desktop_prefs_window_t *c, const tiku_rect_t *previous)
{
    tiku_rect_t area = work_area(c), frame;
    desk_layout_t r = layout((tiku_rect_t){0, 0, TIKU_TRK_DESK_PREFS_W, 0});
    frame.h = r.defaults.y + r.defaults.h + PAD + tiku_window_tab_h() +
              TIKU_WINDOW_BORDER;
    frame.w = area.w < TIKU_TRK_DESK_PREFS_W ? area.w : TIKU_TRK_DESK_PREFS_W;
    frame.x = previous ? previous->x : (c->screen_w - frame.w) / 2;
    frame.y = previous ? previous->y : area.y + (area.h - frame.h) / 3;
    frame.x = fit_origin(frame.x, area.x, area.w, frame.w);
    frame.y = fit_origin(frame.y, area.y, area.h, frame.h);
    return frame;
}

/** @brief Refit after the work area or face changes while the window is open.
 */
static void
refit(tiku_trk_desktop_prefs_window_t *c)
{
    tiku_rect_t old = c->window->frame;
    tiku_rect_t frame = fitted_frame(c, &old);
    if (old.x != frame.x || old.y != frame.y || old.w != frame.w ||
        old.h != frame.h) {
        c->window->frame = frame;
        if (c->dirty)
            *c->dirty = 1;
    }
}

/** @brief Whether the point lies within @p r. */
static int
inside(tiku_rect_t r, int x, int y)
{
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

/** @brief Submit the selected picture, placement and icon size. */
static void
apply(tiku_trk_desktop_prefs_window_t *controller)
{
    tiku_trk_folder_window_t *desk =
        tiku_trk_desktop_view_folder(controller->desktop_view);

    if (controller->submit_save) {
        (void)controller->submit_save(controller->save_context);
    } else {
        (void)tiku_trk_deskprefs_save(&controller->model, controller->store,
                                      controller->node);
        (void)tiku_store_flush(controller->store);
    }
    if (!controller->submit_save && desk != NULL && desk->view != NULL) {
        tiku_trk_poseview_set_icon_size(desk->view,
                                        controller->model.value.icon_size);
        /* Dropping the kept pixels makes the next frame paint the
         * Desktop whole, from the changed record. */
        tiku_trk_folder_window_forget(desk);
    }
    if (controller->dirty != NULL) {
        *controller->dirty = 1;
    }
}

/** @brief Request a desktop repaint after an edit or focus change. */
static void
redraw(tiku_trk_desktop_prefs_window_t *c)
{
    if (c->dirty)
        *c->dirty = 1;
}

/** @brief Find the stock thumbnail for a stored path, or return -1 for a custom
 * file. */
static int
stock_index(const tiku_trk_desktop_prefs_window_t *c, const char *path)
{
    for (int i = 0; i < c->pictures.count; i++)
        if (!strcmp(c->pictures.picture[i], path))
            return i;
    return -1;
}

/** @brief Release the decoded thumbnails owned by the open window. */
static void
free_previews(tiku_trk_desktop_prefs_window_t *c)
{
    for (int i = 0; i <= STOCK_MAX; i++)
        tiku_image_free(&c->previews[i]);
}

void
tiku_trk_desktop_prefs_window_chosen(tiku_trk_desktop_prefs_window_t *c,
                                     const char *path)
{
    if (!c->window || !c->window->open || !path)
        return;
    tiku_image_t thumb = {0};
    if (strlen(path) >= sizeof c->model.value.bg.path ||
        tiku_image_load_fit(path, THUMB_W, THUMB_H, &thumb) != TIKU_IMG_OK) {
        c->notice_key = "prefs.desktop.invalid_picture";
        redraw(c);
        return;
    }
    tiku_image_free(&c->previews[STOCK_MAX]);
    c->previews[STOCK_MAX] = thumb;
    snprintf(c->custom_path, sizeof c->custom_path, "%s", path);
    tiku_trk_deskprefs_set_picture(&c->model, path);
    c->notice_key = NULL;
    apply(c);
}

/** @brief Centre a rectangle of the requested dimensions in the tile. */
static tiku_rect_t
center(tiku_rect_t box, int w, int h)
{
    return (tiku_rect_t){box.x + (box.w - w) / 2, box.y + (box.h - h) / 2, w,
                         h};
}

/** @brief Draw the selection border and the pressed face of a choice. */
static void
tile(tiku_surface_t *s, tiku_rect_t r, int selected, int held)
{
    tiku_fill(s, r, selected ? TIKU_C_SELECT : TIKU_C_PANEL);
    tiku_frame(
        s, r, selected ? TIKU_C_FOCUS : tiku_tint(TIKU_C_PANEL, TIKU_DARKEN_2));
    tiku_fill(s, tiku_inset(r, 3), held ? TIKU_C_PANEL : TIKU_C_DOC);
}

/** @brief Mark the file-picker tile, including when it shows a custom
 * thumbnail. */
static void
plus(tiku_surface_t *s, tiku_rect_t box)
{
    tiku_rect_t r = center(box, 13, 3);
    tiku_fill(s, r, TIKU_C_TEXT);
    tiku_fill(s, center(box, 3, 13), TIKU_C_TEXT);
}

/** @brief Centre a thumbnail at its original aspect ratio. */
static void
picture(tiku_surface_t *s, tiku_rect_t r, const tiku_image_t *im)
{
    if (!im->px || r.w <= 0 || r.h <= 0)
        return;
    int w = r.w, h = w * im->h / im->w;
    if (h > r.h) {
        h = r.h;
        w = h * im->w / im->h;
    }
    tiku_paint_image(s, im, center(r, w, h), r);
}

/** @brief Draw the sample using the same placement calculation as the desktop.
 */
static void
placement(tiku_surface_t *s, tiku_rect_t box, int mode,
          const tiku_image_t *thumb)
{
    tiku_rect_t screen = center(box, box.w < 60 ? box.w - 10 : 50, 28);
    tiku_fill(s, screen, TIKU_C_BACKDROP);
    tiku_frame(s, screen, TIKU_C_TEXT);
    screen = tiku_inset(screen, 2);
    tiku_trk_bg_t bg = {.mode = (tiku_trk_bg_mode_t)mode};
    int x, y, w, h;
    for (int i = 0;
         tiku_trk_bg_place(&bg, screen.w, screen.h, 17, 10, i, &x, &y, &w, &h);
         i++) {
        tiku_rect_t image = {screen.x + x, screen.y + y, w, h};
        if (thumb->px)
            tiku_paint_image(s, thumb, image, screen);
        else {
            if (image.w > screen.w - x)
                image.w = screen.w - x;
            if (image.h > screen.h - y)
                image.h = screen.h - y;
            tiku_fill(s, image, TIKU_C_SELECT);
            tiku_frame(s, image, TIKU_C_TEXT);
        }
    }
}

/** @brief Place the section heading just above its controls. */
static void
heading(tiku_surface_t *s, tiku_rect_t row, const char *label)
{
    tiku_text(s, tiku_font_bold(), row.x,
              row.y - 4 - tiku_font_bold()->height + tiku_font_bold()->ascent,
              label, TIKU_C_TEXT);
}

/** @brief Return the preset tile for a pixel size, or the ruler tile for a
 * custom size. */
static int
preset(int size)
{
    for (int i = 0; i < 3; i++)
        if (size == sizes[i])
            return i;
    return 3;
}

/** @brief Position the numeric field, keypad and action buttons. */
static tiku_rect_t
editor_rect(tiku_rect_t content, int id)
{
    int h = tiku_ui_button_h() > 32 ? tiku_ui_button_h() : 32;
    int width = content.w < 270 ? content.w - 2 * PAD : 246;
    tiku_rect_t row = {content.x + (content.w - width) / 2,
                       content.y + PAD + tiku_font_bold()->height + 6, width,
                       h};
    if (!id)
        return row;
    if (id == CANCEL || id == ACCEPT) {
        row.y += 5 * (h + 4) + 4;
        return cell(row, id == ACCEPT, 2);
    }
    int k = id == ERASE ? 11 : id == DIGIT ? 10 : id - DIGIT - 1;
    row.y += (k / 3 + 1) * (h + 4);
    return cell(row, k % 3, 3);
}

/** @brief Whether the numeric field holds a size the desktop view accepts. */
static int
valid_size(const char *s)
{
    int n = atoi(s);
    return n >= 16 && n <= 128;
}

/** @brief Draw the selected text and touch keypad for a custom icon size. */
static void
draw_editor(tiku_trk_desktop_prefs_window_t *c, tiku_surface_t *s,
            tiku_rect_t content)
{
    tiku_rect_t field = editor_rect(content, 0);
    int n = (int)strlen(c->digits);
    heading(s, field, tiku_str("prefs.desktop.pixels"));
    tiku_ui_textfield_sel(s, field, c->digits, n, c->replace_size ? 0 : n, n,
                          TIKU_S_FOCUS);
    for (int id = DIGIT; id <= CANCEL; id++) {
        char digit[] = {(char)('0' + id - DIGIT), 0};
        const char *label = id == ERASE    ? "<"
                            : id == ACCEPT ? tiku_str("prefs.desktop.apply")
                            : id == CANCEL ? tiku_str("button.cancel")
                                           : digit;
        unsigned state = tiku_press_held(&c->press, id) ? TIKU_S_PRESSED : 0;
        if (id == ACCEPT && !valid_size(c->digits))
            state |= TIKU_S_DISABLED;
        tiku_ui_button(s, editor_rect(content, id), label, state);
    }
}

/** @brief Paint the preference controls or the custom size editor. */
static void
draw(tiku_window_t *window, tiku_surface_t *s, tiku_rect_t content,
     void *context)
{
    tiku_trk_desktop_prefs_window_t *c = context;
    const tiku_trk_deskprefs_values_t *v = &c->model.value;
    refit(c);
    content = tiku_window_content(window);
    desk_layout_t r = layout(content);
    tiku_fill(s, content, TIKU_C_PANEL);
    if (c->editing_size) {
        draw_editor(c, s, content);
        return;
    }
    heading(s, r.pictures, tiku_str("prefs.desktop.picture"));
    heading(s, r.modes, tiku_str("prefs.desktop.justification"));
    char size_label[80];
    snprintf(size_label, sizeof size_label, "%s   %d px",
             tiku_str("label.icon_size"), v->icon_size);
    heading(s, r.sizes, size_label);
    int n = c->pictures.count, selected = stock_index(c, v->bg.path);
    for (int i = 0; i < n + 2; i++) {
        int custom = i == n + 1;
        int chosen =
            i == 0 ? !v->have_bg
                   : v->have_bg && (custom ? selected < 0 : selected == i - 1);
        tiku_rect_t box = cell(r.pictures, i, n + 2);
        tile(s, box, chosen,
             tiku_press_held(&c->press, custom ? CUSTOM_TILE : NONE_TILE + i));
        tiku_rect_t art = tiku_inset(box, 5);
        if (!i)
            tiku_fill(s, art, TIKU_C_BACKDROP);
        else if (!custom)
            picture(s, art, &c->previews[i - 1]);
        else {
            picture(s, art, &c->previews[STOCK_MAX]);
            if (c->previews[STOCK_MAX].px) {
                art = (tiku_rect_t){art.x + art.w - 18, art.y + art.h - 18, 18,
                                    18};
                tiku_fill(s, art, TIKU_C_DOC);
            }
            plus(s, art);
        }
    }
    const tiku_image_t *sample = &c->previews[0];
    if (v->have_bg) {
        const tiku_image_t *active =
            &c->previews[selected < 0 ? STOCK_MAX : selected];
        if (active->px)
            sample = active;
    }
    for (int i = 0; i < 4; i++) {
        tiku_rect_t box = cell(r.modes, i, 4);
        tile(s, box, (int)v->bg.mode == i,
             tiku_press_held(&c->press, MODE_TILE + i));
        placement(s, box, i, sample);
        box = cell(r.sizes, i, 4);
        tile(s, box, preset(v->icon_size) == i,
             tiku_press_held(&c->press, SIZE_TILE + i));
        if (i < 3) {
            int side = 18 + 9 * i;
            tiku_rect_t icon = center(box, side, side);
            (void)tiku_icon_paint(s, "folder", icon.x, icon.y, side);
        } else {
            tiku_rect_t ruler = center(box, 34, 16);
            tiku_fill(s, ruler, TIKU_C_PANEL);
            tiku_frame(s, ruler, TIKU_C_TEXT);
            for (int j = 1; j <= 7; j++)
                tiku_vline(s, ruler.x + j * 4, ruler.y, j % 2 ? 5 : 9,
                           TIKU_C_TEXT);
        }
    }
    const char *status =
        c->save_status ? c->save_status(c->save_context) : NULL;
    if (!status || !*status)
        status =
            tiku_str(c->notice_key ? c->notice_key : "prefs.desktop.browse");
    tiku_text_block_draw(s, r.status, TIKU_MEASURE_PLAIN, status, TIKU_C_TEXT,
                         2);
    tiku_ui_button(
        s, r.defaults, tiku_str("button.defaults"),
        (!tiku_trk_deskprefs_defaultable(&c->model) ? TIKU_S_DISABLED : 0) |
            (tiku_press_held(&c->press, DEFAULTS) ? TIKU_S_PRESSED : 0));
    tiku_ui_button(
        s, r.revert, tiku_str("button.revert"),
        (!tiku_trk_deskprefs_revertable(&c->model) ? TIKU_S_DISABLED : 0) |
            (tiku_press_held(&c->press, REVERT) ? TIKU_S_PRESSED : 0));
}

/** @brief Return the control under the pointer, or zero for a gutter or
 * disabled action. */
static int
hit(tiku_trk_desktop_prefs_window_t *c, tiku_rect_t content, int x, int y)
{
    if (c->editing_size) {
        for (int id = DIGIT; id <= CANCEL; id++)
            if (inside(editor_rect(content, id), x, y))
                return id;
        return 0;
    }
    desk_layout_t r = layout(content);
    int n = c->pictures.count;
    for (int i = 0; i < n + 2; i++)
        if (inside(cell(r.pictures, i, n + 2), x, y))
            return i == n + 1 ? CUSTOM_TILE : NONE_TILE + i;
    for (int i = 0; i < 4; i++) {
        if (inside(cell(r.modes, i, 4), x, y))
            return MODE_TILE + i;
        if (inside(cell(r.sizes, i, 4), x, y))
            return SIZE_TILE + i;
    }
    if (inside(r.defaults, x, y) && tiku_trk_deskprefs_defaultable(&c->model))
        return DEFAULTS;
    if (inside(r.revert, x, y) && tiku_trk_deskprefs_revertable(&c->model))
        return REVERT;
    return 0;
}

/** @brief Update the numeric field, cancel it or submit its validated size. */
static void
edit(tiku_trk_desktop_prefs_window_t *c, int id)
{
    size_t n = strlen(c->digits);
    if (id == CANCEL)
        c->editing_size = 0;
    else if (id == ACCEPT) {
        if (valid_size(c->digits)) {
            c->model.value.icon_size = atoi(c->digits);
            c->editing_size = 0;
            apply(c);
        }
    } else {
        if (c->replace_size) {
            n = 0;
            c->replace_size = 0;
        }
        if (id == ERASE) {
            if (n)
                n--;
        } else if (n < sizeof c->digits - 1)
            c->digits[n++] = (char)('0' + id - DIGIT);
        c->digits[n] = 0;
    }
    redraw(c);
}

/** @brief Apply the released tile or button and dispatch a double-click to the
 * file panel. */
static void
activate(tiku_trk_desktop_prefs_window_t *c, int id, int64_t now)
{
    c->notice_key = NULL;
    if (c->editing_size) {
        edit(c, id);
        return;
    }
    if (id == CUSTOM_TILE) {
        if (c->custom_click && now >= c->custom_click &&
            now - c->custom_click <= 500000) {
            c->custom_click = 0;
            if (c->pick_file)
                c->pick_file(c->pick_context);
        } else {
            c->custom_click = now;
            if (*c->custom_path) {
                tiku_trk_deskprefs_set_picture(&c->model, c->custom_path);
                apply(c);
            }
        }
        return;
    }
    c->custom_click = 0;
    if (id == NONE_TILE)
        tiku_trk_deskprefs_clear_picture(&c->model);
    else if (id >= STOCK_TILE && id < CUSTOM_TILE)
        tiku_trk_deskprefs_set_picture(&c->model,
                                       c->pictures.picture[id - STOCK_TILE]);
    else if (id >= MODE_TILE && id < SIZE_TILE)
        tiku_trk_deskprefs_set_mode(&c->model,
                                    (tiku_trk_bg_mode_t)(id - MODE_TILE));
    else if (id >= SIZE_TILE && id < SIZE_TILE + 3)
        c->model.value.icon_size = sizes[id - SIZE_TILE];
    else if (id == SIZE_TILE + 3) {
        c->editing_size = c->replace_size = 1;
        snprintf(c->digits, sizeof c->digits, "%d", c->model.value.icon_size);
        return;
    } else if (id == DEFAULTS)
        tiku_trk_deskprefs_defaults(&c->model);
    else if (id == REVERT)
        tiku_trk_deskprefs_revert(&c->model);
    else
        return;
    apply(c);
}

/** @brief Route pointer capture and the numeric editor’s keys to their
 * controls. */
static int
event(tiku_window_t *window, const tiku_event_t *e, void *context)
{
    tiku_trk_desktop_prefs_window_t *c = context;
    if (e->type == TIKU_EVENT_KEY_DOWN && c->editing_size) {
        unsigned key = e->key;
        if (key >= '0' && key <= '9')
            edit(c, DIGIT + (int)(key - '0'));
        else if (key == TIKU_KEY_BACKSPACE || key == TIKU_KEY_DELETE)
            edit(c, ERASE);
        else if (key == TIKU_KEY_RETURN)
            edit(c, ACCEPT);
        else if (key == TIKU_KEY_ESCAPE)
            edit(c, CANCEL);
        else
            return 0;
        return 1;
    }
    if (e->type == TIKU_EVENT_DEACTIVATED) {
        memset(&c->press, 0, sizeof c->press);
        c->custom_click = 0;
        return 0;
    }
    int id = hit(c, tiku_window_content(window), e->x, e->y);
    if (e->type == TIKU_EVENT_POINTER_DOWN && e->button == 1 && id) {
        tiku_window_t *under = NULL;
        if (tiku_workspace_hit(c->workspace, e->x, e->y, &under) !=
                TIKU_HIT_CONTENT ||
            under != window)
            return 0;
        (void)tiku_press_arm(&c->press, id);
    } else if (e->type == TIKU_EVENT_POINTER_UP && c->press.id) {
        int action = tiku_press_release(&c->press, id);
        if (action)
            activate(c, action, e->time_us ? e->time_us : tiku_trk_now());
    } else if (e->type == TIKU_EVENT_POINTER_MOVE && c->press.id) {
        (void)tiku_press_track(&c->press, id);
    } else
        return 0;
    redraw(c);
    return 1;
}

/** @brief Release gallery resources and forget input state when the window
 * leaves the workspace. */
static void
closed(tiku_window_t *w, void *context)
{
    tiku_trk_desktop_prefs_window_t *c = context;
    (void)w;
    free_previews(c);
    c->window = NULL;
    c->custom_click = 0;
    c->editing_size = 0;
    c->notice_key = NULL;
    memset(&c->press, 0, sizeof c->press);
    redraw(c);
}

void
tiku_trk_desktop_prefs_window_init(tiku_trk_desktop_prefs_window_t *c,
                                   tiku_workspace_t *workspace,
                                   tiku_trk_desktop_view_t *view,
                                   tiku_store_t *store, int screen_w,
                                   int screen_h, int *dirty)
{
    memset(c, 0, sizeof *c);
    c->workspace = workspace;
    c->desktop_view = view;
    c->store = store;
    c->screen_w = screen_w;
    c->screen_h = screen_h;
    c->dirty = dirty;
    (void)tiku_desktop_dir(c->node, sizeof c->node);
    tiku_trk_deskprefs_init(&c->model);
}

void
tiku_trk_desktop_prefs_window_close(tiku_trk_desktop_prefs_window_t *c)
{
    if (c->window && c->window->open)
        tiku_workspace_close(c->workspace, c->window);
}

void
tiku_trk_desktop_prefs_window_destroy(tiku_trk_desktop_prefs_window_t *c)
{
    tiku_trk_desktop_prefs_window_close(c);
}

void
tiku_trk_desktop_prefs_window_open(tiku_trk_desktop_prefs_window_t *c)
{
    if (c->window && c->window->open) {
        tiku_workspace_activate(c->workspace, c->window);
        return;
    }
    tiku_trk_folder_window_t *desk =
        tiku_trk_desktop_view_folder(c->desktop_view);
    (void)tiku_trk_deskprefs_load(
        &c->model, c->store, c->node,
        desk && desk->view ? tiku_trk_poseview_icon_size(desk->view) : 0);
    tiku_trk_deskprefs_mark_opened(&c->model);
    c->window =
        tiku_workspace_open(c->workspace, tiku_str("title.desktop_prefs"),
                            fitted_frame(c, NULL), draw, c, NULL);
    if (!c->window)
        return;
    tiku_window_set_size_controls(c->window, 0, 0);
    tiku_window_set_handlers(c->window, event, NULL, closed);
    const char *dir = c->pictures_dir;
    (void)tiku_trk_deskprefs_pictures(&c->pictures, &dir, 1);
    if (c->pictures.count > STOCK_MAX)
        c->pictures.count = STOCK_MAX;
    for (int i = 0; i < c->pictures.count; i++)
        (void)tiku_image_load_fit(c->pictures.picture[i], THUMB_W, THUMB_H,
                                  &c->previews[i]);
    if (c->model.value.have_bg && stock_index(c, c->model.value.bg.path) < 0)
        snprintf(c->custom_path, sizeof c->custom_path, "%s",
                 c->model.value.bg.path);
    if (*c->custom_path &&
        tiku_image_load_fit(c->custom_path, THUMB_W, THUMB_H,
                            &c->previews[STOCK_MAX]) != TIKU_IMG_OK)
        c->custom_path[0] = 0;
    redraw(c);
}

tiku_window_t *
tiku_trk_desktop_prefs_window_handle(tiku_trk_desktop_prefs_window_t *c)
{
    return c->window;
}
