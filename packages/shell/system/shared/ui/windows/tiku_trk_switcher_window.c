/*
 * Desktop shell and file manager.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_switcher_window.c - the application switcher: the running
 * applications in a row, the chosen one highlighted and named.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_shell_product.h"
#include "tiku_attr.h"
#include "tiku_iconpaint.h"
#include "tiku_trk_gfx_dep.h"
#include "tiku_trk_switcher_window.h"
#include "tiku_trk_task.h"
#include <stdio.h>
#include <string.h>

#define SWITCHER_SLOT  56
#define SWITCHER_PAD   12
#define SWITCHER_ARROW 14

/**
 * @brief The height of the strip below the icons that holds the chosen
 *        one's name, from the face.
 */
static int title_h(void)
{
    return tiku_font_plain()->height + 7;        /* 22 at 12 px */
}

/**
 * @brief The window, sized to the slots it shows, centred across the
 *        screen a little above the middle.
 */
static tiku_rect_t
frame(tiku_trk_switcher_window_t *controller)
{
    tiku_rect_t result;
    int slots = controller->model.n < TIKU_TRK_SW_SLOTS
              ? controller->model.n : TIKU_TRK_SW_SLOTS;

    if (slots < 1) {
        slots = 1;
    }
    result.w = SWITCHER_PAD * 2 + slots * SWITCHER_SLOT + 2 * SWITCHER_ARROW;
    result.h = SWITCHER_PAD * 2 + TIKU_TRK_SW_LARGE + title_h();
    tiku_trk_switcher_place(controller->screen_w, controller->screen_h,
                            result.w, result.h, &result.x, &result.y);
    return result;
}

/**
 * @brief Draw the applications in a row, the chosen one on the selection
 *        colour, the scroll arrows, and the chosen one's title.
 */
static void
draw(tiku_window_t *window, tiku_surface_t *surface,
     tiku_rect_t content, void *context)
{
    tiku_trk_switcher_window_t *controller = context;
    const tiku_font_t *font = tiku_font_plain();
    int64_t now = tiku_trk_now();
    char title[160];
    int i, slots;

    (void)window;
    tiku_fill(surface, content, TIKU_C_PANEL);
    slots = controller->model.n < TIKU_TRK_SW_SLOTS
          ? controller->model.n : TIKU_TRK_SW_SLOTS;
    {
        /* The icons are drawn relative to where the scroll is on the
         * animation clock, so the strip slides (DB-058). */
        int spx = tiku_trk_switcher_scroll_px(&controller->model,
                                              SWITCHER_SLOT, now);
        int base = controller->model.scroll * SWITCHER_SLOT - spx;

    for (i = -1; i <= slots; i++) {
        int which = controller->model.scroll + i;
        int size, center_x, center_y;
        tiku_rect_t box;

        if (which < 0) {
            continue;
        }
        if (which >= controller->model.n) {
            break;
        }
        size = tiku_trk_switcher_icon_size(&controller->model, which, now);
        center_x = content.x + SWITCHER_PAD + SWITCHER_ARROW
                 + i * SWITCHER_SLOT + SWITCHER_SLOT / 2 + base;
        center_y = content.y + SWITCHER_PAD + TIKU_TRK_SW_LARGE / 2;
        box.x = center_x - size / 2;
        box.y = center_y - size / 2;
        box.w = size;
        box.h = size;
        if (which == controller->model.sel) {
            tiku_fill(surface, box, TIKU_C_SELECT);
        }
        if (!tiku_icon_paint(surface, "app", box.x, box.y, size)) {
            tiku_frame(surface, box, TIKU_C_TEXT);
        }
    }
    }
    if (tiku_trk_switcher_scroller(&controller->model, 0)) {
        tiku_text(surface, font, content.x + 4,
                       content.y + SWITCHER_PAD + 28, "<", TIKU_C_TEXT);
    }
    if (tiku_trk_switcher_scroller(&controller->model, 1)) {
        tiku_text(surface, font, content.x + content.w - 12,
                       content.y + SWITCHER_PAD + 28, ">", TIKU_C_TEXT);
    }
    tiku_trk_switcher_title(&controller->model, controller->model.sel,
                            controller->model.selwin, title, sizeof title);
    tiku_text(surface, font, content.x + SWITCHER_PAD,
                   content.y + content.h - (title_h() - font->ascent),
                   title, TIKU_C_TEXT);
}

void
tiku_trk_switcher_window_init(tiku_trk_switcher_window_t *controller,
    tiku_workspace_t *workspace, tiku_trk_deskbar_t *deskbar,
    int screen_w, int screen_h, int *dirty, char *status,
    size_t status_size)
{
    memset(controller, 0, sizeof *controller);
    controller->workspace = workspace;
    controller->deskbar = deskbar;
    controller->screen_w = screen_w;
    controller->screen_h = screen_h;
    controller->dirty = dirty;
    controller->status = status;
    controller->status_size = status_size;
    tiku_trk_switcher_init(&controller->model);
}

int
tiku_trk_switcher_window_shown(
    const tiku_trk_switcher_window_t *controller)
{
    return controller->model.shown;
}

void
tiku_trk_switcher_window_open(tiku_trk_switcher_window_t *controller,
                              int backwards)
{
    int i;

    if (!controller->model.shown) {
        tiku_trk_switcher_init(&controller->model);
        for (i = 0; i < tiku_trk_deskbar_team_count(controller->deskbar); i++) {
            const char *name = tiku_trk_deskbar_team_name(controller->deskbar,
                                                          i);

            if (name != NULL) {
                (void)tiku_trk_switcher_app(&controller->model, name,
                          strcmp(name, TIKU_SHELL_TRACKER_NAME) == 0);
            }
        }
    }
    tiku_trk_switcher_open(&controller->model, tiku_trk_now(), backwards);
    if (controller->window == NULL || !controller->window->open) {
        controller->window = tiku_workspace_open(controller->workspace,
            "Switcher", frame(controller), draw, controller, NULL);
    }
}

tiku_trk_sw_act_t
tiku_trk_switcher_window_key(tiku_trk_switcher_window_t *controller,
                             const char *key, int shift)
{
    return tiku_trk_switcher_key(&controller->model, key, shift,
                                 tiku_trk_now());
}

tiku_trk_sw_act_t
tiku_trk_switcher_window_release(tiku_trk_switcher_window_t *controller,
                                 int64_t now)
{
    return tiku_trk_switcher_release(&controller->model, now);
}

void
tiku_trk_switcher_window_close(tiku_trk_switcher_window_t *controller,
                               int commit)
{
    if (controller->window != NULL) {
        tiku_workspace_close(controller->workspace, controller->window);
        controller->window = NULL;
    }
    if (commit && controller->model.sel >= 0 &&
        controller->model.sel < controller->model.n &&
        controller->status != NULL && controller->status_size > 0) {
        snprintf(controller->status, controller->status_size,
                 "switched to %.60s",
                 controller->model.app[controller->model.sel].name);
    }
    controller->model.shown = 0;
    if (controller->dirty != NULL) {
        *controller->dirty = 1;
    }
}

const char *
tiku_trk_switcher_window_selected(
    const tiku_trk_switcher_window_t *controller)
{
    if (controller->model.sel < 0 ||
        controller->model.sel >= controller->model.n) {
        return NULL;
    }
    return controller->model.app[controller->model.sel].name;
}
