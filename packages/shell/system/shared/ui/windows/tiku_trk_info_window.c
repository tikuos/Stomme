/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_info_window.c - the Get Info windows: each shows one entry's
 * icon, its name or the rename editor, and the Information, Permissions and
 * Attributes tabs.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_attr.h"
#include "tiku_iconpaint.h"
#include "tiku_tabs_paint.h"
#include "tiku_trk_gfx_dep.h"
#include "tiku_trk_info_window.h"
#include <stdio.h>
#include <string.h>

/* The window's width, fixed; a value too long for its row is
 * middle-truncated and shown whole on hover (AW-073). */
#define INFO_WIDTH    360
#define INFO_PAD       10
#define INFO_ICON      32
#define CASCADE_STEP   22

/* The row, tab strip and name heights, from the face. */
static int row_h(void)   { return tiku_font_plain()->height + 3; }
static int tab_h(void)   { return tiku_font_plain()->height + 5; }
static int name_h(void)  { return tiku_font_plain()->height + 5; }

/**
 * @brief How tall the window has to be for the longest of its pages.
 *
 * 300 at the 12 px face, moved by how far the tab strip, the eight rows of
 * the Information page and the window's title tab have grown from their
 * 12 px heights (20, 18 and 21).  The margins within the 300 stay fixed.
 */
static int info_height(void)
{
    return 300 + (tab_h() - 20) + 8 * (row_h() - 18)
               + (tiku_window_tab_h() - 21);
}

/** @brief Set the shell's redraw flag, when it has one. */
static void
mark_dirty(tiku_trk_info_windows_t *controller)
{
    if (controller->dirty != NULL) {
        *controller->dirty = 1;
    }
}

/** @brief Where the icon, the name, the tabs and the body go in @p content. */
static void
layout(tiku_rect_t content, tiku_rect_t *icon,
       tiku_rect_t *name, tiku_rect_t *tabs,
       tiku_rect_t *body)
{
    *icon = (tiku_rect_t){ content.x + INFO_PAD,
                                content.y + INFO_PAD,
                                INFO_ICON, INFO_ICON };
    *name = (tiku_rect_t){ icon->x + INFO_ICON + INFO_PAD,
                                icon->y + 6,
                                content.w - (icon->x + INFO_ICON + INFO_PAD
                                             - content.x) - INFO_PAD,
                                name_h() };
    *tabs = (tiku_rect_t){ content.x + INFO_PAD,
                                icon->y + INFO_ICON + INFO_PAD,
                                content.w - 2 * INFO_PAD, tab_h() };
    *body = (tiku_rect_t){ tabs->x, tabs->y + tab_h(), tabs->w,
                                content.y + content.h - INFO_PAD
                                    - (tabs->y + tab_h()) };
    if (body->h < 20) {
        body->h = 20;
    }
}

/** @brief The width of @p text in the face @p ctx names. */
static int
measure_info_text(const char *text, void *ctx)
{
    return tiku_text_width((const tiku_font_t *)ctx, text);
}

/** @brief The @p index-th permission checkbox, down the body's left side. */
static tiku_rect_t
permission_rect(tiku_rect_t body, int index)
{
    return (tiku_rect_t){ body.x + 8,
        body.y + 10 + index * (row_h() + 4),
        tiku_ui_button_w("Read", 120), row_h() };
}


/**
 * @brief Where the values start: past the widest label in the face, and at
 *        least 96.
 */
static int
value_column(tiku_trk_info_t *info, tiku_store_t *store)
{
    const tiku_font_t *font = tiku_font_plain();
    int widest = 0;
    int i;

    for (i = 0; i < TIKU_TRK_INFO_FIELDS; i++) {
        char value[TIKU_TRK_INFO_VAL_MAX];
        const char *label = "";
        int w;

        if (tiku_trk_info_field(info, store, i, &label, value,
                                sizeof value) == TIKU_TRK_INFO_F_NONE) {
            break;
        }
        w = tiku_text_width(font, label);
        if (w > widest) {
            widest = w;
        }
    }
    widest += 8 + 16;                   /* the indent, and a gutter */
    return (widest > 96) ? widest : 96;
}

/**
 * @brief Where column @p which of the attributes page starts: 0 the type,
 *        1 the value.  Each column is as wide as its widest entry, with a
 *        floor.
 */
static int
attr_column(const tiku_trk_info_t *info, int which)
{
    const tiku_font_t *font = tiku_font_plain();
    int name_w = 0, type_w = 0, i;

    for (i = 0; i < info->nattr; i++) {
        int w = tiku_text_width(font, info->attr[i].name);

        if (w > name_w) {
            name_w = w;
        }
        w = tiku_text_width(font, info->attr[i].type);
        if (w > type_w) {
            type_w = w;
        }
    }
    if (name_w + 24 < 142) {
        name_w = 142 - 24;              /* type column at least 142 in */
    }
    if (type_w + 16 < 60) {
        type_w = 60 - 16;
    }
    return (which == 0) ? 8 + name_w + 16 : 8 + name_w + 16 + type_w + 16;
}

/**
 * @brief Grow or shrink the window's height to what the face needs.
 *
 * Checked on every draw: the face can change while the window is open.
 * The corner stays where it is.
 */
static void
refit(tiku_window_t *window, int *dirty)
{
    int want_h = info_height();

    if (window == NULL || !window->open || window->frame.h == want_h) {
        return;
    }
    window->frame.h = want_h;
    if (dirty != NULL) {
        *dirty = 1;
    }
}

/**
 * @brief Draw one Get Info window: icon, name or its editor, the tabs and
 *        the current tab's body, refitting the window to the face first.
 */
static void
draw(tiku_window_t *window, tiku_surface_t *surface,
     tiku_rect_t content, void *context)
{
    tiku_trk_info_window_context_t *window_context = context;
    tiku_trk_info_windows_t *controller = window_context->owner;
    tiku_trk_info_t *info = &controller->model[window_context->slot];
    const tiku_font_t *font = tiku_font_plain();
    tiku_rect_t icon, name, tabs, body;

    refit(window, controller->dirty);
    content = tiku_window_content(window);
    int i;

    (void)window;
    tiku_ui_panel(surface, content);
    layout(content, &icon, &name, &tabs, &body);
    if (controller->drop_hot == window_context->slot) {
        (void)tiku_icon_paint_selected(surface,
            tiku_model_icon_name(&info->model), icon.x, icon.y,
            INFO_ICON);
    } else {
        (void)tiku_icon_paint(surface,
            tiku_model_icon_name(&info->model), icon.x, icon.y,
            INFO_ICON);
    }
    if (controller->renaming == window_context->slot) {
        /* The focus ring and the caret show only in the focused window:
         * the key handler finds its slot from the focused window, so a
         * renaming Get Info in the background receives no keys. */
        int owns = (window ==
                    tiku_workspace_focused(controller->workspace));

        tiku_ui_textfield_sel(surface, name, controller->name.text,
            owns ? controller->name.caret : -1,
            controller->name.sel_a,
            controller->name.sel_b, owns ? TIKU_S_FOCUS : 0u);
    } else {
        tiku_text(surface, font, name.x, name.y + font->height,
                       info->model.name, TIKU_C_TEXT);
    }
    tiku_tabs_draw(&controller->tabs[window_context->slot], surface, tabs);
    tiku_ui_sunken(surface, body, TIKU_C_PANEL);
    if (tiku_tabs_current(&controller->tabs[window_context->slot]) ==
            TIKU_TRK_INFO_TAB_INFO) {
        for (i = 0; i < TIKU_TRK_INFO_FIELDS; i++) {
            char value[TIKU_TRK_INFO_VAL_MAX];
            const char *label = "";
            int y = body.y + 6 + i * row_h() + font->height;

            if (tiku_trk_info_field(info, controller->store, i, &label,
                                    value, sizeof value) ==
                    TIKU_TRK_INFO_F_NONE) {
                break;
            }
            tiku_text(surface, font, body.x + 8, y, label,
                           TIKU_C_TEXT);
            {
                /* Middle-truncated to the row; the hover pop-up shows the
                 * whole value (AW-073). */
                char fitted[TIKU_TRK_INFO_VAL_MAX];
                int at = value_column(info, controller->store);
                int room = body.w - at - 12;

                if (tiku_text_width(font, value) > room &&
                    tiku_text_fit(value, room, TIKU_TRUNC_MIDDLE,
                                      measure_info_text, (void *)font,
                                      fitted, sizeof fitted) >= 0) {
                    tiku_text(surface, font, body.x + at, y, fitted,
                                   TIKU_C_TEXT);
                } else {
                    tiku_text(surface, font, body.x + at, y, value,
                                   TIKU_C_TEXT);
                }
            }
        }
    } else if (tiku_tabs_current(&controller->tabs[window_context->slot]) ==
                   TIKU_TRK_INFO_TAB_PERMISSIONS) {
        static const char *const labels[] = { "Read", "Write", "Execute" };

        for (i = 0; i < 3; i++) {
            tiku_ui_checkbox(surface, permission_rect(body, i), labels[i],
                (unsigned)(tiku_trk_info_perm(info, i) ? TIKU_S_ON : 0u)
                | (unsigned)(info->perm_editable ? 0u
                                                 : TIKU_S_DISABLED));
        }
        if (!info->perm_editable) {
            tiku_text(surface, font, body.x + 8,
                body.y + 14 + 3 * (row_h() + 4) + font->height,
                "This item's permissions are not yours to set.",
                TIKU_C_TEXT);
        }
    } else {
        for (i = 0; i < info->nattr; i++) {
            int y = body.y + 6 + i * row_h() + font->height;

            if (y > body.y + body.h) {
                break;
            }
            tiku_text(surface, font, body.x + 8, y,
                           info->attr[i].name, TIKU_C_TEXT);
            tiku_text(surface, font, body.x + attr_column(info, 0), y,
                           info->attr[i].type, TIKU_C_TEXT);
            tiku_text(surface, font, body.x + attr_column(info, 1), y,
                           info->attr[i].value, TIKU_C_TEXT);
        }
        if (info->nattr == 0) {
            tiku_text(surface, font, body.x + 8,
                           body.y + 6 + font->height, "No attributes.",
                           TIKU_C_TEXT);
        }
    }
}

/** @brief The slot already showing @p path, or -1. */
static int
slot_for_path(const tiku_trk_info_windows_t *controller, const char *path)
{
    int i;

    for (i = 0; i < TIKU_TRK_INFO_WINDOW_MAX; i++) {
        if (controller->model[i].used &&
            strcmp(controller->model[i].path, path) == 0) {
            return i;
        }
    }
    return -1;
}

/**
 * @brief End the name edit, renaming the node when @p apply and the name
 *        changed; a rename the filesystem refuses is reported in an alert.
 */
static void
finish_rename(tiku_trk_info_windows_t *controller, int apply)
{
    int slot = controller->renaming;
    char error[160];

    if (slot < 0 || slot >= TIKU_TRK_INFO_WINDOW_MAX) {
        return;
    }
    controller->renaming = -1;
    if (!apply || controller->name.len == 0 ||
        strcmp(controller->name.text,
               controller->model[slot].model.name) == 0) {
        mark_dirty(controller);
        return;
    }
    if (tiku_fs_rename(controller->fs, controller->model[slot].path,
                           controller->name.text, error, sizeof error) != 0) {
        tiku_alert_open(controller->alert, TIKU_ALERT_WARN,
                            TIKU_TRK_ALERT_TAG_ERROR, error, "Cancel");
    } else {
        char parent[TIKU_PATH_MAX], destination[TIKU_PATH_MAX];

        if (tiku_trk_info_location(&controller->model[slot], parent,
                                   sizeof parent, NULL, 0)) {
            snprintf(destination, sizeof destination, "%.*s/%.*s",
                (int)(sizeof destination - sizeof controller->name.text - 2u),
                strcmp(parent, "/") == 0 ? "" : parent,
                (int)(sizeof controller->name.text - 1u),
                controller->name.text);
            (void)tiku_trk_info_moved(&controller->model[slot],
                controller->backend, controller->model[slot].path,
                destination);
            if (controller->window[slot] != NULL) {
                tiku_trk_info_title(&controller->model[slot],
                    controller->window[slot]->title,
                    sizeof controller->window[slot]->title);
            }
        }
    }
    mark_dirty(controller);
}

void
tiku_trk_info_windows_init(tiku_trk_info_windows_t *controller,
    tiku_workspace_t *workspace, tiku_backend_t *backend,
    tiku_store_t *store, tiku_fs_t *fs, tiku_alert_t *alert,
    int *dirty)
{
    int i;

    memset(controller, 0, sizeof *controller);
    controller->drop_hot = -1;
    controller->hover_slot = -1;
    controller->hover_field = -1;
    controller->workspace = workspace;
    controller->backend = backend;
    controller->store = store;
    controller->fs = fs;
    controller->alert = alert;
    controller->dirty = dirty;
    controller->renaming = -1;
    for (i = 0; i < TIKU_TRK_INFO_WINDOW_MAX; i++) {
        controller->context[i].owner = controller;
        controller->context[i].slot = i;
    }
}

int
tiku_trk_info_windows_open(tiku_trk_info_windows_t *controller,
                           const char *path)
{
    tiku_rect_t frame;
    char title[TIKU_WINDOW_TITLE_MAX];
    int slot = slot_for_path(controller, path);

    if (slot >= 0) {
        if (controller->window[slot] != NULL) {
            tiku_workspace_activate(controller->workspace,
                                          controller->window[slot]);
        }
        return slot;
    }
    for (slot = 0; slot < TIKU_TRK_INFO_WINDOW_MAX; slot++) {
        if (!controller->model[slot].used) {
            break;
        }
    }
    if (slot >= TIKU_TRK_INFO_WINDOW_MAX ||
        tiku_trk_info_open(&controller->model[slot], controller->backend,
                           path) != 0) {
        return -1;
    }
    (void)tiku_trk_info_attrs(&controller->model[slot], controller->store);
    frame = (tiku_rect_t){ 120 + slot * CASCADE_STEP,
                                80 + slot * CASCADE_STEP,
                                INFO_WIDTH, info_height() };
    tiku_trk_info_title(&controller->model[slot], title, sizeof title);
    controller->window[slot] = tiku_workspace_open(controller->workspace,
        title, frame, draw, &controller->context[slot],
        &controller->model[slot]);
    tiku_tabs_init(&controller->tabs[slot]);
    {
        int ti;

        for (ti = 0; ti < TIKU_TRK_INFO_TABS; ti++) {
            (void)tiku_tabs_add(&controller->tabs[slot],
                tiku_trk_info_tab_name((tiku_trk_info_tab_t)ti));
        }
    }
    mark_dirty(controller);
    return slot;
}

void
tiku_trk_info_windows_close(tiku_trk_info_windows_t *controller, int slot)
{
    if (slot < 0 || slot >= TIKU_TRK_INFO_WINDOW_MAX) {
        return;
    }
    if (controller->window[slot] != NULL) {
        tiku_workspace_close(controller->workspace,
                                  controller->window[slot]);
        controller->window[slot] = NULL;
    }
    if (controller->renaming == slot) {
        controller->renaming = -1;
    }
    memset(&controller->model[slot], 0, sizeof controller->model[slot]);
    mark_dirty(controller);
}

int
tiku_trk_info_windows_slot(const tiku_trk_info_windows_t *controller,
                           const tiku_window_t *window)
{
    int i;

    for (i = 0; i < TIKU_TRK_INFO_WINDOW_MAX; i++) {
        if (controller->window[i] == window && window != NULL &&
            window->open) {
            return i;
        }
    }
    return -1;
}

tiku_trk_info_t *
tiku_trk_info_windows_model(tiku_trk_info_windows_t *controller, int slot)
{
    if (slot < 0 || slot >= TIKU_TRK_INFO_WINDOW_MAX) {
        return NULL;
    }
    return &controller->model[slot];
}

tiku_window_t *
tiku_trk_info_windows_handle(tiku_trk_info_windows_t *controller, int slot)
{
    if (slot < 0 || slot >= TIKU_TRK_INFO_WINDOW_MAX) {
        return NULL;
    }
    return controller->window[slot];
}

tiku_trk_info_hit_t
tiku_trk_info_windows_hit(tiku_trk_info_windows_t *controller, int slot,
                          int x, int y, int *which)
{
    tiku_rect_t content, icon, name, tabs, body;
    int i;

    if (which != NULL) {
        *which = -1;
    }
    if (slot < 0 || slot >= TIKU_TRK_INFO_WINDOW_MAX ||
        controller->window[slot] == NULL) {
        return TIKU_TRK_INFO_HIT_NONE;
    }
    content = tiku_window_content(controller->window[slot]);
    layout(content, &icon, &name, &tabs, &body);
#define INSIDE(rect) (x >= (rect).x && x < (rect).x + (rect).w && \
                      y >= (rect).y && y < (rect).y + (rect).h)
    if (INSIDE(icon)) { return TIKU_TRK_INFO_HIT_ICON; }
    if (INSIDE(name)) { return TIKU_TRK_INFO_HIT_NAME; }
    {
        int hit = tiku_tabs_hit(&controller->tabs[slot], tabs, x, y);

        if (hit >= 0) {
            if (which != NULL) { *which = hit; }
            return TIKU_TRK_INFO_HIT_TAB;
        }
    }
    if (tiku_tabs_current(&controller->tabs[slot]) ==
            TIKU_TRK_INFO_TAB_PERMISSIONS) {
        for (i = 0; i < 3; i++) {
            tiku_rect_t rect = permission_rect(body, i);

            if (INSIDE(rect)) {
                if (which != NULL) { *which = i; }
                return TIKU_TRK_INFO_HIT_PERMISSION;
            }
        }
    }
    if (tiku_tabs_current(&controller->tabs[slot]) ==
            TIKU_TRK_INFO_TAB_INFO && INSIDE(body)) {
        i = (y - body.y - 6) / row_h();
        if (i >= 0 && i < TIKU_TRK_INFO_FIELDS) {
            if (which != NULL) { *which = i; }
            return TIKU_TRK_INFO_HIT_FIELD;
        }
    }
#undef INSIDE
    return TIKU_TRK_INFO_HIT_NONE;
}

int
tiku_trk_info_windows_field_rect(tiku_trk_info_windows_t *controller,
                                 int slot, int i, int *x, int *y, int *w,
                                 int *h)
{
    tiku_rect_t content, icon, name, tabs, body;
    int at;

    if (slot < 0 || slot >= TIKU_TRK_INFO_WINDOW_MAX ||
        controller->window[slot] == NULL ||
        !controller->window[slot]->open) {
        return 0;
    }
    content = tiku_window_content(controller->window[slot]);
    layout(content, &icon, &name, &tabs, &body);
    /* The value's column as draw() places it. */
    at = value_column(&controller->model[slot], controller->store);
    if (x != NULL) { *x = body.x + at; }
    if (y != NULL) { *y = body.y + 6 + i * row_h(); }
    if (w != NULL) { *w = body.w - at - 12; }
    if (h != NULL) { *h = row_h(); }
    return 1;
}

void
tiku_trk_info_windows_begin_rename(tiku_trk_info_windows_t *controller,
                                   int slot)
{
    if (slot < 0 || slot >= TIKU_TRK_INFO_WINDOW_MAX ||
        !controller->model[slot].used) {
        return;
    }
    controller->renaming = slot;
    tiku_field_start(&controller->name,
                        controller->model[slot].model.name);
    tiku_field_deny(&controller->name, tiku_fs_name_deny());
    mark_dirty(controller);
}

int
tiku_trk_info_windows_key(tiku_trk_info_windows_t *controller,
    int key, unsigned modifiers, char character)
{
    int slot = tiku_trk_info_windows_slot(controller,
        tiku_workspace_focused(controller->workspace));

    if (slot < 0) {
        return 0;
    }
    if (controller->renaming == slot) {
        if (key == TIKU_KEY_RETURN) {
            finish_rename(controller, 1);
        } else if (key == TIKU_KEY_ESCAPE) {
            finish_rename(controller, 0);
        } else if (key == TIKU_KEY_BACKSPACE) {
            (void)tiku_field_key(&controller->name,
                                    TIKU_FIELD_BACKSPACE, 0, '\0');
        } else if (character != '\0') {
            (void)tiku_field_key(&controller->name,
                                    TIKU_FIELD_CHAR, 0, character);
        }
        mark_dirty(controller);
        return 1;
    }
    if ((modifiers & TIKU_MOD_CMD) != 0u &&
        (key == 'e' || key == 'E')) {
        tiku_trk_info_windows_begin_rename(controller, slot);
        return 1;
    }
    if ((modifiers & TIKU_MOD_CMD) != 0u &&
        (key == 'w' || key == 'W')) {
        tiku_trk_info_windows_close(controller, slot);
        return 1;
    }
    return 0;
}

int
tiku_trk_info_windows_calc_step(tiku_trk_info_windows_t *controller)
{
    int i;

    for (i = 0; i < TIKU_TRK_INFO_WINDOW_MAX; i++) {
        if (controller->model[i].used &&
            tiku_trk_info_calc_step(&controller->model[i],
                                    controller->backend)) {
            mark_dirty(controller);
            return 1;
        }
    }
    return 0;
}

int
tiku_trk_info_windows_device_gone(tiku_trk_info_windows_t *controller,
                                  uint64_t device)
{
    int changed = 0, i;

    for (i = 0; i < TIKU_TRK_INFO_WINDOW_MAX; i++) {
        if (controller->model[i].used &&
            controller->model[i].model.facts.dev == device) {
            tiku_trk_info_windows_close(controller, i);
            changed++;
        }
    }
    return changed;
}

int
tiku_trk_info_windows_refresh(tiku_trk_info_windows_t *controller,
                              int64_t now)
{
    int changed = 0, i;

    for (i = 0; i < TIKU_TRK_INFO_WINDOW_MAX; i++) {
        tiku_model_t model;

        if (!controller->model[i].used) {
            continue;
        }
        if (tiku_trk_info_refresh(&controller->model[i],
                                  controller->backend, now)) {
            changed++;
        }
        if (controller->backend->ops->stat(controller->backend,
                controller->model[i].path, &model) != 0) {
            if (tiku_trk_info_refind(&controller->model[i],
                                     controller->backend)) {
                tiku_trk_info_title(&controller->model[i],
                    controller->window[i]->title,
                    sizeof controller->window[i]->title);
            } else {
                tiku_trk_info_windows_close(controller, i);
            }
            changed++;
        }
    }
    if (changed) {
        mark_dirty(controller);
    }
    return changed;
}

void
tiku_trk_info_windows_select_tab(tiku_trk_info_windows_t *controller,
                                 int slot, int which)
{
    if (controller == NULL || slot < 0 ||
        slot >= TIKU_TRK_INFO_WINDOW_MAX) {
        return;
    }
    (void)tiku_tabs_select(&controller->tabs[slot], which);
    if (tiku_tabs_current(&controller->tabs[slot]) ==
            TIKU_TRK_INFO_TAB_ATTRIBUTES) {
        /* Re-read when the tab is selected, so attributes written since
         * the window opened appear. */
        (void)tiku_trk_info_attrs(&controller->model[slot],
                                  controller->store);
    }
    mark_dirty(controller);
}

int
tiku_trk_info_windows_text(tiku_trk_info_windows_t *controller,
    const char *text)
{
    int slot = tiku_trk_info_windows_slot(controller,
        tiku_workspace_focused(controller->workspace));
    if (slot < 0) return 0;
    if (controller->renaming != slot ||
        (controller->alert && controller->alert->open)) return 1;
    int result = tiku_field_commit(&controller->name, text);
    mark_dirty(controller);
    return result == TIKU_FIELD_REJECTED ? -1 : 1;
}
