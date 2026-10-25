/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_desktop_prefs_window.c - the Desktop preferences window.
 *
 * The picture behind the Desktop, how it is laid, the icon size and Reduce
 * motion.  The pictures offered are the images in the Desktop folder and
 * in the wallpapers folder beside it, so a picture is added by dropping
 * its file into either.
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

#define PAD       16
#define LIST_ROWS  6

/*
 * Where the controls sit, in content coordinates, computed from the face
 * on every call; the drawing and the click both use them.
 */
static int row_h(void)   { return tiku_ui_row_h(); }   /* 22 at 12 px */
static int list_y(void)  { return tiku_font_bold()->height + 27; }
static int mode_y(void)  { return list_y() + LIST_ROWS * row_h() +
                                  row_h() + 12; }
static int icon_y(void)  { return mode_y() + 4 * row_h() + row_h() + 4; }
static int motion_y(void){ return icon_y() + tiku_ui_button_h() + 12; }
static int button_y(void){ return motion_y() + row_h() + 18 + 2 * tiku_font_plain()->height; }

/** @brief The Reduce motion tick box's row, in @p content's space. */
static tiku_rect_t
motion_rect(tiku_rect_t content)
{
    tiku_rect_t r;

    r.x = content.x + PAD;
    r.y = content.y + motion_y();
    r.w = content.w - 2 * PAD;
    r.h = row_h();
    return r;
}

/** @brief The side of the tick box beside a laying mode. */
static int box_side(void)
{
    return tiku_font_plain()->height - 3;              /* 12 at 12 px */
}

/**
 * @brief Row @p i of the laying-mode choices, in @p content's space.  The
 *        drawing and the click both read it.
 */
static tiku_rect_t
mode_row(tiku_rect_t content, int i)
{
    tiku_rect_t r;

    r.x = content.x + PAD;
    r.y = content.y + mode_y() + i * row_h();
    r.w = content.w - 2 * PAD;
    r.h = row_h();
    return r;
}

/**
 * @brief The icon-size stepper's plus, at the row's right edge.
 *
 * The stepper is laid out from the right edge inward; the drawing and the
 * press both read these rectangles.
 */
static tiku_rect_t
plus_rect(tiku_rect_t content)
{
    tiku_rect_t r;

    r.w = tiku_ui_step_w();
    r.h = tiku_ui_button_h();
    r.x = content.x + content.w - PAD - r.w;
    r.y = content.y + icon_y();
    return r;
}

/** @brief Room for the widest size the stepper shows, in the face in force. */
static int
number_w(void)
{
    return tiku_text_width(tiku_font_plain(), "000 px") + 20;
}

/** @brief The stepper's minus, left of the number it changes. */
static tiku_rect_t
minus_rect(tiku_rect_t content)
{
    tiku_rect_t r = plus_rect(content);

    r.x -= number_w() + 8 + r.w;
    return r;
}

/** @brief The Defaults button, first in the row of buttons. */
static tiku_rect_t
defaults_rect(tiku_rect_t content)
{
    tiku_rect_t r;

    r.x = content.x + PAD;
    r.y = content.y + button_y();
    r.w = tiku_ui_button_w(tiku_str("button.defaults"), 112);
    r.h = tiku_ui_button_h();
    return r;
}

/** @brief The Revert button, beside Defaults. */
static tiku_rect_t
revert_rect(tiku_rect_t content)
{
    tiku_rect_t d = defaults_rect(content);
    tiku_rect_t r = d;

    r.x = d.x + d.w + 10;
    r.w = tiku_ui_button_w(tiku_str("button.revert"), 100);
    return r;
}

/** @brief The window's frame height: content, title tab and border. */
static int
window_height(void)
{
    return button_y() + tiku_ui_button_h() + PAD +
           tiku_window_tab_h() + TIKU_WINDOW_BORDER;
}

/**
 * @brief Grow or shrink the window to what the contents need.
 *
 * Checked on every draw: the face can change while the window is open.
 * The top-left corner stays where it is.
 */
static void
refit(tiku_trk_desktop_prefs_window_t *controller)
{
    int want;

    if (controller->window == NULL || !controller->window->open) {
        return;
    }
    want = window_height();
    if (controller->window->frame.h != want) {
        controller->window->frame.h = want;
        if (controller->dirty != NULL) {
            *controller->dirty = 1;
        }
    }
}

/* The catalogue keys of the four laying modes' labels, looked up when a
 * row is drawn. */
static const char *const kModeKeys[4] = {
    "prefs.desktop.mode.corner", "prefs.desktop.mode.centred",
    "prefs.desktop.mode.scaled", "prefs.desktop.mode.tiled"
};

/** @brief Whether the point lies within @p r. */
static int
inside(tiku_rect_t r, int x, int y)
{
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

/** @brief The name a picture is listed under: the last part of its path. */
static const char *
leaf_of(const char *path)
{
    const char *slash = strrchr(path, '/');

    return (slash != NULL) ? slash + 1 : path;
}

/**
 * @brief Save the record and apply it to the Desktop: the icon size, the
 *        picture, and Reduce motion when it changed.
 */
static void
apply(tiku_trk_desktop_prefs_window_t *controller)
{
    tiku_trk_folder_window_t *desk =
        tiku_trk_desktop_view_folder(controller->desktop_view);

    if (controller->submit_save) {
        (void)controller->submit_save(controller->save_context);
    } else {
        (void)tiku_trk_deskprefs_save(&controller->model, controller->store, controller->node);
        (void)tiku_store_flush(controller->store);
    }
    /* Only a change to Reduce motion writes the settings file, which every
     * running Tracker then reads again. */
    if (controller->shared_prefs != NULL &&
        (tiku_trk_prefs_get(controller->shared_prefs, "ReduceMotion") != 0) !=
            (controller->model.value.reduce_motion != 0)) {
        tiku_trk_prefs_set(controller->shared_prefs, "ReduceMotion",
                           controller->model.value.reduce_motion != 0);
        if (controller->shared_prefs_path[0] != '\0') {
            (void)tiku_trk_prefs_submit(controller->shared_prefs,
                                      controller->shared_prefs_path);
            controller->model.value.reduce_motion =
                tiku_trk_prefs_get(controller->shared_prefs, "ReduceMotion") != 0;
        }
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

/**
 * @brief Draw the picture list, the laying modes, the icon-size stepper,
 *        Reduce motion and the buttons, refitting the window to the face
 *        first.
 */
static void
draw(tiku_window_t *window, tiku_surface_t *surface,
     tiku_rect_t content, void *context)
{
    tiku_trk_desktop_prefs_window_t *controller = context;
    const tiku_trk_deskprefs_values_t *v = &controller->model.value;
    const tiku_font_t *font = tiku_font_plain();
    const tiku_font_t *bold = tiku_font_bold();
    char number[32];
    int i;

    (void)window;
    refit(controller);
    tiku_fill(surface, content, TIKU_C_PANEL);
    tiku_text(surface, bold, content.x + PAD,
                   content.y + bold->ascent + 10,
                   tiku_str("prefs.desktop.picture"),
                   TIKU_C_TEXT);

    /* The list: what is there to choose, and which one is showing. */
    {
        tiku_rect_t list = { content.x + PAD, content.y + list_y(),
                                  content.w - 2 * PAD,
                                  LIST_ROWS * row_h() };

        tiku_fill(surface, list, TIKU_C_DOC);
        tiku_frame(surface, list,
                        tiku_tint(TIKU_C_PANEL,
                                       TIKU_DARKEN_2));
        for (i = 0; i < LIST_ROWS; i++) {
            tiku_rect_t row = { list.x + 1, list.y + 1 + i * row_h(),
                                     list.w - 2, row_h() };
            const char *label;
            int chosen;

            if (i == 0) {
                label = tiku_str("prefs.desktop.no_picture");
                chosen = !v->have_bg;
            } else if (i - 1 < controller->pictures.count) {
                label = leaf_of(controller->pictures.picture[i - 1]);
                chosen = v->have_bg &&
                         strcmp(controller->pictures.picture[i - 1],
                                v->bg.path) == 0;
            } else {
                continue;
            }
            if (chosen) {
                tiku_fill(surface, row, TIKU_C_SELECT);
            }
            tiku_text(surface, font, row.x + 8,
                           row.y + (row_h() - font->height) / 2 + font->ascent,
                           label, chosen ? TIKU_C_SELTEXT
                                         : TIKU_C_TEXT);
        }
        if (controller->pictures.count == 0) {
            tiku_text(surface, font, list.x + 8,
                           list.y + row_h() + 15,
                           tiku_str("prefs.desktop.drop_hint"),
                           tiku_tint(TIKU_C_PANEL, 1.45f));
        }
    }

    tiku_text(surface, bold, content.x + PAD,
                   content.y + mode_y() - 12,
                   tiku_str("prefs.desktop.how_laid"), TIKU_C_TEXT);
    for (i = 0; i < 4; i++) {
        tiku_rect_t row = mode_row(content, i);
        int y = row.y;
        tiku_rect_t box = { row.x + 2,
                                 y + (row_h() - box_side()) / 2,
                                 box_side(), box_side() };

        tiku_fill(surface, box, TIKU_C_DOC);
        tiku_frame(surface, box, TIKU_C_TEXT);
        if ((int)v->bg.mode == i) {
            tiku_fill(surface, tiku_inset(box, 3),
                           TIKU_C_TEXT);
        }
        tiku_text(surface, font, row.x + 24,
                       y + 4 + font->ascent, tiku_str(kModeKeys[i]),
                       v->have_bg ? TIKU_C_TEXT
                                  : tiku_tint(TIKU_C_PANEL,
                                                   1.45f));
    }

    tiku_text(surface, bold, content.x + PAD,
                   content.y + icon_y() +
                       (tiku_ui_button_h() - bold->height) / 2 +
                       bold->ascent,
                   tiku_str("label.icon_size"), TIKU_C_TEXT);
    {
        tiku_rect_t minus = minus_rect(content);
        tiku_rect_t plus = plus_rect(content);
        tiku_rect_t shown = { minus.x + minus.w + 4, minus.y,
                                   number_w(), minus.h };

        tiku_ui_button(surface, minus, "-",
                       tiku_press_held(&controller->press, 1)
                           ? TIKU_S_PRESSED : 0u);
        tiku_ui_button(surface, plus, "+",
                       tiku_press_held(&controller->press, 2)
                           ? TIKU_S_PRESSED : 0u);
        snprintf(number, sizeof number, "%d px", v->icon_size);
        (void)tiku_text_centered(surface, font, shown, number,
                                      TIKU_C_TEXT);
    }

    tiku_ui_checkbox(surface, motion_rect(content),
        tiku_str("prefs.desktop.reduce_motion"),
        (v->reduce_motion ? TIKU_S_ON : 0u) |
        (controller->shared_prefs ? 0u : TIKU_S_DISABLED));

    if (controller->save_status) {
        const char *status = controller->save_status(controller->save_context);
        tiku_text_block_draw(surface, (tiku_rect_t){content.x + PAD, content.y + motion_y() + row_h() + 6,
            content.w - 2 * PAD, 2 * font->height}, TIKU_MEASURE_PLAIN, status ? status : "", TIKU_C_TEXT, 2);
    }

    tiku_ui_button(surface, defaults_rect(content),
                        tiku_str("button.defaults"),
                        (tiku_trk_deskprefs_defaultable(&controller->model)
                            ? 0u : TIKU_S_DISABLED) |
                        (tiku_press_held(&controller->press, 3)
                            ? TIKU_S_PRESSED : 0u));
    tiku_ui_button(surface, revert_rect(content),
                        tiku_str("button.revert"),
                        (tiku_trk_deskprefs_revertable(&controller->model)
                            ? 0u : TIKU_S_DISABLED) |
                        (tiku_press_held(&controller->press, 4)
                            ? TIKU_S_PRESSED : 0u));
}

/**
 * @brief A press picks a picture or a laying mode or toggles Reduce
 *        motion; the stepper's arrows and the buttons act on a release
 *        over them.  Each change is applied at once.
 * @return 1 when taken.
 */
static int
event(tiku_window_t *window, const tiku_event_t *event,
      void *context)
{
    tiku_trk_desktop_prefs_window_t *controller = context;
    tiku_rect_t content = tiku_window_content(window);
    int x = event->x - content.x, y = event->y - content.y;

    {
        /* The stepper's arrows and the two buttons show pressed while
         * held and act on a release over them; the list, the modes and
         * the tick box act on the press. */
        int under = inside(minus_rect(content), event->x, event->y) ? 1
                  : inside(plus_rect(content), event->x, event->y) ? 2
                  : (inside(defaults_rect(content), event->x, event->y) &&
                     tiku_trk_deskprefs_defaultable(&controller->model)) ? 3
                  : (inside(revert_rect(content), event->x, event->y) &&
                     tiku_trk_deskprefs_revertable(&controller->model)) ? 4
                  : 0;

        if (event->type == TIKU_EVENT_POINTER_MOVE) {
            if (tiku_press_track(&controller->press, under)) {
                if (controller->dirty != NULL) { *controller->dirty = 1; }
            }
            return controller->press.id != 0;
        }
        if (event->type == TIKU_EVENT_POINTER_UP) {
            int id = tiku_press_release(&controller->press, under);

            if (controller->dirty != NULL) { *controller->dirty = 1; }
            if (id == 1 || id == 2) {
                if (tiku_trk_deskprefs_step_icon(&controller->model, id == 2)) {
                    apply(controller);
                }
            } else if (id == 3) {
                tiku_trk_deskprefs_defaults(&controller->model);
                apply(controller);
            } else if (id == 4) {
                tiku_trk_deskprefs_revert(&controller->model);
                apply(controller);
            }
            return 1;
        }
        if (event->type == TIKU_EVENT_POINTER_DOWN && under != 0) {
            (void)tiku_press_arm(&controller->press, under);
            if (controller->dirty != NULL) { *controller->dirty = 1; }
            return 1;
        }
    }
    if (event->type != TIKU_EVENT_POINTER_DOWN ||
        !inside(content, event->x, event->y)) {
        return 0;
    }
    if (event->button == 1u && controller->shared_prefs != NULL &&
        inside(motion_rect(content), event->x, event->y)) {
        controller->model.value.reduce_motion =
            !controller->model.value.reduce_motion;
        apply(controller);
        return 1;
    }
    if (y >= list_y() && y < list_y() + LIST_ROWS * row_h() &&
        x >= PAD && x < content.w - PAD) {
        int row = (y - list_y() - 1) / row_h();

        if (row == 0) {
            tiku_trk_deskprefs_clear_picture(&controller->model);
            apply(controller);
        } else if (row - 1 < controller->pictures.count) {
            tiku_trk_deskprefs_set_picture(&controller->model,
                controller->pictures.picture[row - 1]);
            apply(controller);
        }
        return 1;
    }
    {
        /* The mode rows as mode_row() lays them out, in content-relative
         * coordinates, tested on x as well as y. */
        tiku_rect_t zero = { 0, 0, TIKU_TRK_DESK_PREFS_W, 0 };
        int i;

        for (i = 0; i < 4; i++) {
            tiku_rect_t row = mode_row(zero, i);

            if (x >= row.x && x < row.x + row.w &&
                y >= row.y && y < row.y + row.h) {
                /* Only while a picture is chosen; without one the modes
                 * are greyed. */
                if (controller->model.value.have_bg) {
                    tiku_trk_deskprefs_set_mode(&controller->model,
                        (tiku_trk_bg_mode_t)i);
                    apply(controller);
                }
                return 1;
            }
        }
    }
    return 0;
}

void
tiku_trk_desktop_prefs_window_init(
    tiku_trk_desktop_prefs_window_t *controller,
    tiku_workspace_t *workspace, tiku_trk_desktop_view_t *desktop_view,
    tiku_store_t *store, int screen_w, int screen_h, int *dirty)
{
    memset(controller, 0, sizeof *controller);
    controller->workspace = workspace;
    controller->desktop_view = desktop_view;
    controller->store = store;
    controller->screen_w = screen_w;
    controller->screen_h = screen_h;
    controller->dirty = dirty;
    (void)tiku_desktop_dir(controller->node, sizeof controller->node);
    snprintf(controller->pictures_dir, sizeof controller->pictures_dir,
             "%s/../wallpapers", controller->node);
    tiku_trk_deskprefs_init(&controller->model);
}

void
tiku_trk_desktop_prefs_window_bind_settings(
    tiku_trk_desktop_prefs_window_t *controller,
    tiku_trk_prefs_t *prefs, const char *path)
{
    controller->shared_prefs = prefs;
    snprintf(controller->shared_prefs_path,
             sizeof controller->shared_prefs_path, "%s",
             (path != NULL) ? path : "");
}

void
tiku_trk_desktop_prefs_window_close(
    tiku_trk_desktop_prefs_window_t *controller)
{
    if (controller->window != NULL && controller->window->open) {
        tiku_workspace_close(controller->workspace, controller->window);
    }
    controller->window = NULL;
    if (controller->dirty != NULL) {
        *controller->dirty = 1;
    }
}

void
tiku_trk_desktop_prefs_window_destroy(
    tiku_trk_desktop_prefs_window_t *controller)
{
    tiku_trk_desktop_prefs_window_close(controller);
}

void
tiku_trk_desktop_prefs_window_open(
    tiku_trk_desktop_prefs_window_t *controller)
{
    tiku_trk_folder_window_t *desk;
    const char *dirs[2];
    tiku_rect_t frame;

    if (controller->window != NULL && controller->window->open) {
        tiku_workspace_activate(controller->workspace,
                                     controller->window);
        return;
    }
    /* Read afresh on every open: the Desktop's record may have changed
     * elsewhere since, and a stale model would undo that change. */
    desk = tiku_trk_desktop_view_folder(controller->desktop_view);
    (void)tiku_trk_deskprefs_load(&controller->model, controller->store,
        controller->node,
        (desk != NULL && desk->view != NULL)
            ? tiku_trk_poseview_icon_size(desk->view) : 0);
    controller->model.value.reduce_motion =
        tiku_trk_prefs_get(controller->shared_prefs, "ReduceMotion") != 0;
    dirs[0] = controller->node;
    dirs[1] = controller->pictures_dir;
    (void)tiku_trk_deskprefs_pictures(&controller->pictures, dirs, 2);
    tiku_trk_deskprefs_mark_opened(&controller->model);

    frame.w = TIKU_TRK_DESK_PREFS_W;
    frame.h = window_height();
    frame.x = (controller->screen_w - frame.w) / 2;
    frame.y = (controller->screen_h - frame.h) / 3;
    controller->window = tiku_workspace_open(controller->workspace,
        tiku_str("title.desktop_prefs"), frame, draw, controller,
        NULL);
    if (controller->window != NULL) {
        tiku_window_set_size_controls(controller->window, 0, 0);
        tiku_window_set_handlers(controller->window, event, NULL, NULL);
    }
    if (controller->dirty != NULL) {
        *controller->dirty = 1;
    }
}

tiku_window_t *
tiku_trk_desktop_prefs_window_handle(
    tiku_trk_desktop_prefs_window_t *controller)
{
    return controller->window;
}
