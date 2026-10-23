/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_folder_window.c - drawing a folder window.
 *
 * The menu bar, the column strip, the rows or the icons, the scroll bar and
 * the count line.  The rows and icons are painted into pixels the window
 * keeps between frames, and a frame repaints only what its damage touches
 * or what looks different.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_attr.h"
#include "tiku_iconpaint.h"
#include "tiku_strings.h"
#include "tiku_trk_band_controller.h"
#include "tiku_trk_drag_target.h"
#include "tiku_trk_folder_edit.h"
#include "tiku_trk_folder_window.h"
#include "tiku_trk_gfx_dep.h"
#include "tiku_trk_titleview.h"
#include "tiku_volume.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Used before they are defined. */
static uint32_t sig_bytes(uint32_t h, const void *p, size_t n);
static uint32_t sig_text(uint32_t h, const char *t);
static void fw_draw_finish(tiku_window_t *window, tiku_surface_t *surface,
                           tiku_rect_t content, void *context,
                           tiku_trk_folder_window_t **folder, tiku_rect_t list,
                           tiku_surface_t *paint, int rows, int scroll,
                           int total, int icons);
static void fw_draw_icons(tiku_window_t *window, tiku_surface_t *surface,
                          tiku_rect_t content, void *context,
                          tiku_trk_folder_window_t *folder,
                          tiku_trk_folder_render_t *render, tiku_rect_t list,
                          tiku_surface_t *paint, tiku_rect_t *area, int *rows,
                          int *scroll, int *total, int icons);
static void fw_draw_rows(tiku_window_t *window, tiku_surface_t *surface,
                         tiku_rect_t content, void *context,
                         tiku_trk_folder_window_t *folder,
                         tiku_trk_folder_render_t *render,
                         tiku_surface_t *paint, tiku_rect_t area, int icons,
                         int slide, int slide_y);
/* End of the forward declarations. */

#define COUNT_HEIGHT 16     /* the count line along the bottom */
#define TITLE_HEIGHT TIKU_TRK_TITLE_HEIGHT
/* The list's mini icon follows the row height, which follows the face: at
 * the 12 px face the row is 20 and the icon 16 (IV-013). */
#define MINI_ICON_FOR(row_h) ((row_h) - 4)
#define VALUE_GAP   120     /* room left of a value, for fit_width */


/**
 * @brief The document ground from the theme; a query view's ground is set
 *        slightly off it, as a read-only cue on light and dark themes
 *        alike.
 */
static uint32_t
folder_background(const tiku_trk_poseview_t *v)
{
    return (v != NULL && tiku_trk_poseview_is_query(v))
               ? tiku_tint(TIKU_C_DOC,
                   (TIKU_C_DOC & 255) < 128 ? 0.92f : 1.06f)
               : TIKU_C_DOC;
}

/** @brief The width of a label in the plain face, for the pose view. */
static int
measure_label(const char *text, void *context)
{
    (void)context;
    return tiku_text_width(tiku_font_plain(), text);
}

/** @brief Whether the point is on the ink of the icon @p model draws with. */
static int
icon_hit(const tiku_model_t *model, int size, int x, int y,
         void *context)
{
    tiku_trk_folder_render_t *render = context;
    char art[TIKU_NAME_MAX];

    (void)tiku_trk_nodeicon_resolve(render->icons, render->store, model,
                                    art, sizeof art);
    return tiku_icon_hit(art, size, x, y);
}

void
tiku_trk_folder_window_configure(tiku_trk_folder_window_t *folder,
                                  tiku_trk_folder_render_t *render)
{
    const tiku_font_t *font = tiku_font_plain();

    folder->render = render;
    folder->count_last_total = -1;
    folder->count_last_selected = -1;
    tiku_trk_poseview_set_icons(folder->view, render->icons);
    tiku_trk_poseview_set_measure(folder->view, measure_label, render);
    tiku_trk_poseview_set_font_metrics(folder->view, font->height,
                                       font->ascent);
    tiku_trk_poseview_set_icon_hit(folder->view, icon_hit, render);
}

tiku_rect_t
tiku_trk_folder_window_content(const tiku_trk_folder_window_t *folder)
{
    tiku_trk_folder_render_t *render = folder->render;

    if (folder->win != NULL) {
        tiku_rect_t content = tiku_window_content(folder->win);

        if (!render->global_bar) {
            content.y += render->menus->bar.height;
            content.h -= render->menus->bar.height;
        }
        if (tiku_trk_poseview_mode(folder->view) == TIKU_TRK_MODE_LIST) {
            content.y += TITLE_HEIGHT;
            content.h -= TITLE_HEIGHT;
        }
        if (content.h < 0) {
            content.h = 0;
        }
        return content;
    }
    return (tiku_rect_t){ 0, 0, render->screen_w, render->screen_h };
}

tiku_rect_t
tiku_trk_folder_window_title(const tiku_trk_folder_window_t *folder)
{
    tiku_rect_t title = { 0, 0, 0, 0 };

    if (folder != NULL && folder->win != NULL &&
        tiku_trk_poseview_mode(folder->view) == TIKU_TRK_MODE_LIST) {
        title = tiku_window_content(folder->win);
        if (!folder->render->global_bar) {
            title.y += folder->render->menus->bar.height;
        }
        title.h = TITLE_HEIGHT;
    }
    return title;
}

tiku_rect_t
tiku_trk_folder_window_icon(const tiku_trk_folder_window_t *folder)
{
    tiku_rect_t bar = tiku_trk_folder_window_bar(folder);
    int size = bar.h - 4;

    if (size > 24) {
        size = 24;
    }
    if (size < 12) {
        size = 12;
    }
    return (tiku_rect_t){ bar.x + bar.w - size - 3, bar.y + 2,
                               size, size };
}

tiku_rect_t
tiku_trk_folder_window_bar(const tiku_trk_folder_window_t *folder)
{
    tiku_rect_t bar = { 0, 0, 0, 0 };

    if (folder != NULL && folder->win != NULL &&
        !folder->render->global_bar) {
        bar = tiku_window_content(folder->win);
        bar.h = folder->render->menus->bar.height;
    }
    return bar;
}

/**
 * @brief A volume's space bar beside its icon: used over free, in the
 *        colours the settings name, the warning one when space is short.
 */
static void
draw_space_bar(tiku_trk_folder_render_t *render, tiku_surface_t *surface,
               const tiku_model_t *model, int x, int y, int size,
               int vertical)
{
    tiku_rect_t bar;
    int used = 0;
    tiku_space_t space = tiku_volume_space_facts(model->facts.total,
                                                         model->facts.avail,
                                                         &used);
    const char *key = space == TIKU_SPACE_WARNING
                          ? "WarningSpaceColor" : "UsedSpaceColor";
    int32_t used_color = tiku_trk_prefs_get(render->prefs, key);
    uint32_t free_color = (uint32_t)tiku_trk_prefs_get(
        render->prefs, "FreeSpaceColor");
    int thickness = size >= 32 ? (7 * (size + 1)) / 32 : 3;

    if (thickness < 2) {
        thickness = 2;
    }
    if (vertical) {
        bar = (tiku_rect_t){ x + size - thickness, y,
                                  thickness, size };
    } else {
        bar = (tiku_rect_t){ x, y + size - 3, size, 3 };
    }
    tiku_fill(surface, bar,
        TIKU_RGB((free_color >> 16) & 0xffu,
                      (free_color >> 8) & 0xffu, free_color & 0xffu));
    if (vertical) {
        bar.h = size * used / 100;
    } else {
        bar.w = size * used / 100;
    }
    if (bar.w > 0 && bar.h > 0) {
        tiku_fill(surface, bar,
            TIKU_RGB(((uint32_t)used_color >> 16) & 0xffu,
                          ((uint32_t)used_color >> 8) & 0xffu,
                          (uint32_t)used_color & 0xffu));
    }
}

/** @brief One pose's icon and label where it sits, counted as a row drawn. */
static void
draw_pose_icon(tiku_trk_folder_window_t *folder,
               tiku_surface_t *surface, tiku_trk_pose_t *pose,
               int origin_x, int origin_y)
{
    tiku_trk_folder_render_t *render = folder->render;

    /* rows_drawn, which the scripts read, counts the poses painted in
     * folder windows; the backdrop's poses and a recording surface are
     * not counted. */
    if (folder->win != NULL && surface->record == NULL) {
        render->rows_drawn++;
    }
    const tiku_font_t *font = tiku_font_plain();
    int ix, iy, iw, ih, lx, ly, lw, lh;
    int selected = tiku_trk_poseview_draws_selected(folder->view, pose);
    char art[TIKU_NAME_MAX];
    tiku_rect_t label;

    tiku_trk_poseview_pose_parts(folder->view, pose, &ix, &iy, &iw, &ih,
                                 &lx, &ly, &lw, &lh);
    ix -= origin_x;
    iy -= origin_y;
    lx -= origin_x;
    ly -= origin_y;
    const char *app_art = (render->services != NULL &&
                           render->services->art_for != NULL)
        ? render->services->art_for(render->services->ctx, pose->model.path)
        : NULL;

    if (app_art != NULL) {
        snprintf(art, sizeof art, "%s", app_art);
    } else {
        (void)tiku_trk_nodeicon_resolve(render->icons, render->store,
                                        &pose->model, art, sizeof art);
    }
    /* An icon under a held button is drawn darkened. */
    int held = render->press != NULL && render->press->down &&
               render->press->folder == folder && render->press->row >= 0 &&
               tiku_trk_poseview_at(folder->view, render->press->row) ==
                   pose;
    if (held) {
        (void)tiku_icon_paint_dim(surface, art, ix, iy, iw, 0.45f,
                                      TIKU_RGB(0, 0, 0));
    } else if (selected) {
        (void)tiku_icon_paint_selected(surface, art, ix, iy, iw);
    } else if (pose->model.facts.link_broken) {
        (void)tiku_icon_paint_dim(surface, art, ix, iy, iw, 0.55f,
                                      TIKU_C_DOC);
    } else {
        (void)tiku_icon_paint(surface, art, ix, iy, iw);
    }
    if ((pose->model.kind == TIKU_KIND_VOLUME ||
         pose->model.kind == TIKU_KIND_ROOT) &&
        tiku_trk_prefs_get(render->prefs, "ShowVolumeSpaceBar")) {
        draw_space_bar(render, surface, &pose->model, ix, iy, iw, 1);
    }
    label = (tiku_rect_t){ lx - 2, ly, lw + 4,
                                 lh > 0 ? lh : font->height };
    if (selected) {
        tiku_rgb_t band = TIKU_C_SELECT;

        if (folder->win != NULL &&
            folder->win !=
                tiku_workspace_focused(render->workspace)) {
            /* An inactive window's selection is grey (IV-022). */
            band = TIKU_RGB(154, 154, 154);
        }
        if (folder->win == NULL) {
            /* On the Desktop's backdrop, which can be the selection
             * blue itself, a selected label takes SelectedLabelColor
             * from the preferences. */
            band = (tiku_rgb_t)tiku_trk_prefs_get(render->prefs,
                       "SelectedLabelColor") & 0xffffffu;
        }
        tiku_fill_round(surface, label, 3, TIKU_CORNER_ALL, band);
    }
    {
        tiku_rgb_t ink = TIKU_C_TEXT;

        if (folder->win == NULL) {
            tiku_rgb_t background = TIKU_C_BACKDROP;
            int luminance = ((int)((background >> 16) & 0xffu) * 299 +
                             (int)((background >> 8) & 0xffu) * 587 +
                             (int)(background & 0xffu) * 114) / 1000;

            ink = luminance <= 127 ? TIKU_RGB(255, 255, 255)
                                   : TIKU_RGB(0, 0, 0);
        }
        char fit[TIKU_NAME_MAX * 2];

        (void)tiku_trk_poseview_fit_label(folder->view, pose, fit,
                                          sizeof fit);
        if (folder->bg_drawn && !selected) {
            /* Over a picture a label carries a halo of the opposite
             * colour (PVL-071); a selected label has its filled box. */
            tiku_paint_text_halo(surface, font, lx, ly + font->ascent,
                                     fit, ink);
        } else {
            tiku_text(surface, font, lx, ly + font->ascent, fit,
                           selected ? TIKU_C_SELTEXT : ink);
        }
    }
}

int
tiku_trk_folder_window_draw_background(tiku_trk_folder_window_t *folder,
    tiku_surface_t *surface, tiku_rect_t content)
{
    tiku_trk_folder_render_t *render = folder->render;
    tiku_store_t *store;
    tiku_trk_bg_t bg;
    int i, dx, dy, dw, dh;

    /* The picture record is in the backend's store, with the folder's
     * other per-node state. */
    if (render == NULL || render->backend == NULL ||
        render->backend->ops->state_store == NULL) {
        return 0;
    }
    store = render->backend->ops->state_store(render->backend);
    if (store == NULL) {
        return 0;
    }
    /* A record that keeps only a layout mode draws no picture, so the
     * template's picture applies as it does to a node with no record. */
    if (!tiku_trk_bg_load_picture(store, folder->path, &bg) &&
        !tiku_trk_bg_load_picture(store, TIKU_TRK_BG_TEMPLATE_NODE, &bg)) {
        return 0;
    }
    if (strcmp(bg.path, folder->bg_shown) != 0) {
        tiku_image_free(&folder->bg_image);
        folder->bg_shown[0] = '\0';
        if (tiku_image_load(bg.path, &folder->bg_image) !=
            TIKU_IMG_OK) {
            return 0;
        }
        snprintf(folder->bg_shown, sizeof folder->bg_shown, "%s", bg.path);
    }
    for (i = 0; tiku_trk_bg_place(&bg, content.w, content.h,
                                  folder->bg_image.w, folder->bg_image.h,
                                  i, &dx, &dy, &dw, &dh); i++) {
        tiku_rect_t dst = { content.x + dx, content.y + dy, dw, dh };

        tiku_paint_image(surface, &folder->bg_image, dst, content);
    }
    return 1;
}

static uint32_t sig_bytes(uint32_t h, const void *p, size_t n);
static uint32_t sig_text(uint32_t h, const char *t);

/** @brief The whole footprint of one pose, in the paint's coordinates. */
static tiku_rect_t
pose_footprint(tiku_trk_folder_window_t *folder, tiku_trk_pose_t *pose,
               int origin_x, int origin_y)
{
    int ix, iy, iw, ih, lx, ly, lw, lh;
    tiku_rect_t r;
    int x1, y1;

    tiku_trk_poseview_pose_parts(folder->view, pose, &ix, &iy, &iw, &ih,
                                 &lx, &ly, &lw, &lh);
    ix -= origin_x;
    iy -= origin_y;
    lx -= origin_x;
    ly -= origin_y;
    r.x = (ix < lx - 3) ? ix : lx - 3;
    r.y = (iy < ly) ? iy : ly;
    x1 = (ix + iw > lx + lw + 3) ? ix + iw : lx + lw + 3;
    y1 = (iy + ih > ly + lh + 2) ? iy + ih : ly + lh + 2;
    /* Two pixels of margin each way: the halo and the selection band
     * both reach past the ink. */
    r.x -= 2;
    r.y -= 2;
    r.w = x1 - r.x + 2;
    r.h = y1 - r.y + 2;
    return r;
}

/** @brief What one pose would paint, folded to a word (PVL-062). */
static uint32_t
pose_signature(tiku_trk_folder_window_t *folder, tiku_trk_pose_t *pose)
{
    tiku_trk_folder_render_t *render = folder->render;
    char art[TIKU_NAME_MAX];
    uint32_t h = UINT32_C(2166136261);

    (void)tiku_trk_nodeicon_resolve(render->icons, render->store,
                                    &pose->model, art, sizeof art);
    if (render->services != NULL && render->services->art_for != NULL) {
        const char *provided = render->services->art_for(
            render->services->ctx, pose->model.path);
        if (provided != NULL) snprintf(art, sizeof art, "%s", provided);
    }
    int held = render->press && render->press->down &&
               render->press->folder == folder && render->press->row >= 0 &&
               tiku_trk_poseview_at(folder->view, render->press->row) == pose;
    h = sig_bytes(h, &held, sizeof held);
    h = sig_text(h, art);
    h = sig_text(h, pose->model.name);
    h = sig_bytes(h, &pose->loc_x, sizeof pose->loc_x);
    h = sig_bytes(h, &pose->loc_y, sizeof pose->loc_y);
    h = sig_bytes(h, &pose->selected, sizeof pose->selected);
    if (pose->selected && folder->win != NULL) {
        int act = (folder->win ==
                   tiku_workspace_focused(render->workspace));

        h = sig_bytes(h, &act, sizeof act);
    }
    h = sig_bytes(h, &pose->model.facts.link_broken,
                  sizeof pose->model.facts.link_broken);
    h = sig_bytes(h, &pose->model.facts.trash_full,
                  sizeof pose->model.facts.trash_full);
    h = sig_bytes(h, &pose->model.facts.avail,
                  sizeof pose->model.facts.avail);
    h = sig_bytes(h, &pose->model.facts.total,
                  sizeof pose->model.facts.total);
    return h;
}

/** @brief Whether two rectangles share any pixel. */
static int
rects_touch(tiku_rect_t a, tiku_rect_t b)
{
    return a.x < b.x + b.w && b.x < a.x + a.w &&
           a.y < b.y + b.h && b.y < a.y + a.h;
}

void
tiku_trk_folder_window_draw_icons(tiku_trk_folder_window_t *folder,
    tiku_surface_t *surface, tiku_rect_t content)
{
    int first = -1, last = -1, i, origin_x = 0, origin_y = 0;

    tiku_trk_poseview_origin(folder->view, &origin_x, &origin_y);
    origin_x -= content.x + 2;
    origin_y -= content.y + 2;
    if (!tiku_trk_poseview_visible_range(folder->view, &first, &last)) {
        return;
    }
    for (i = first; i <= last; i++) {
        tiku_trk_pose_t *pose = tiku_trk_poseview_at(folder->view, i);

        if (pose != NULL && pose->placed) {
            draw_pose_icon(folder, surface, pose, origin_x, origin_y);
        }
    }
}

/**
 * @brief Queue a thumbnail for this row when its icon comes from one and
 *        the stored thumbnail is missing or stale.
 */
static void
queue_thumbnail(tiku_trk_folder_window_t *folder, tiku_trk_pose_t *pose)
{
    tiku_trk_folder_render_t *render = folder->render;
    char art[TIKU_NAME_MAX];
    int mini = render->row_h - 2;

    if (mini > MINI_ICON_FOR(render->row_h)) {
        mini = MINI_ICON_FOR(render->row_h);
    }
    if (tiku_trk_nodeicon_resolve(render->icons, render->store, &pose->model,
                                  art, sizeof art) != TIKU_TRK_ICON_FROM_THUMB) {
        return;
    }
    if (!tiku_thumb_fresh(render->store, pose->model.path,
                              pose->model.facts.mtime)) {
        (void)tiku_thumb_queue(render->thumbs, pose->model.path,
                                   pose->model.facts.size, mini);
    }
}

/** @brief Fold @p n bytes into a running hash (FNV-1a). */
static uint32_t
sig_bytes(uint32_t h, const void *p, size_t n)
{
    const unsigned char *b = p;
    size_t i;

    for (i = 0u; i < n; i++) {
        h = (h ^ b[i]) * UINT32_C(16777619);
    }
    return h;
}

/** @brief Fold a string, terminator included, into a running hash. */
static uint32_t
sig_text(uint32_t h, const char *t)
{
    return sig_bytes(h, t, strlen(t) + 1u);
}

/**
 * @brief What the row of @p pose would draw, hashed: everything the row's
 *        appearance depends on.  draw_list() skips a row whose hash has
 *        not changed.
 * @note A field draw_list() reads must be hashed here, or a change to it
 *       is never painted.
 */
static uint32_t
row_signature(tiku_trk_folder_window_t *folder, tiku_trk_pose_t *pose,
              int slot, int selected, int cut)
{
    tiku_trk_folder_render_t *render = folder->render;
    const tiku_trk_column_t *cols = NULL;
    uint32_t h = UINT32_C(2166136261);
    char art[TIKU_NAME_MAX];
    int ncol = tiku_trk_poseview_columns(folder->view, &cols);
    int hscroll = tiku_trk_poseview_hscroll(folder->view);
    int j;

    h = sig_bytes(h, &pose->model.node_id, sizeof pose->model.node_id);
    h = sig_bytes(h, &selected, sizeof selected);
    h = sig_bytes(h, &cut, sizeof cut);
    if (selected) {
        /* The selection's colour follows the window's activity when it
         * shows while inactive (IV-022). */
        int act = (folder->win == NULL ||
                   folder->win ==
                       tiku_workspace_focused(render->workspace));

        h = sig_bytes(h, &act, sizeof act);
    }
    h = sig_bytes(h, &pose->model.kind, sizeof pose->model.kind);
    h = sig_bytes(h, &pose->model.facts.link_broken,
                  sizeof pose->model.facts.link_broken);
    h = sig_bytes(h, &pose->model.facts.total, sizeof pose->model.facts.total);
    h = sig_bytes(h, &pose->model.facts.avail, sizeof pose->model.facts.avail);
    h = sig_bytes(h, &hscroll, sizeof hscroll);
    h = sig_bytes(h, &render->row_h, sizeof render->row_h);
    /* The rename editor is drawn over its row, so the hash marks the
     * row it is on. */
    if (render->edit->renaming && render->edit->rename_folder == folder &&
        render->edit->rename_row == pose->index) {
        h = sig_bytes(h, "edit", 5u);
    }
    (void)tiku_trk_nodeicon_resolve(render->icons, render->store,
                                    &pose->model, art, sizeof art);
    h = sig_text(h, art);
    if (tiku_thumb_fresh(render->store, pose->model.path,
                             pose->model.facts.mtime)) {
        h = sig_bytes(h, "thumb", 6u);
    }
    for (j = 0; j < ncol; j++) {
        char tmp[160];
        const char *txt = tiku_trk_poseview_cell_text(folder->view, pose,
                              cols[j].field, tmp, sizeof tmp);

        h = sig_text(h, txt != NULL ? txt : "");
        h = sig_bytes(h, &cols[j].offset, sizeof cols[j].offset);
        h = sig_bytes(h, &cols[j].width, sizeof cols[j].width);
    }
    (void)slot;
    return h;
}

/**
 * @brief Where a list row's selection runs, for a row from @p row_x that
 *        is @p row_w wide: the icon and the Name cell when Name is the
 *        first column, the Name cell alone when it is not, and the whole
 *        row when there is no Name column.
 * @return The Name column's index, or -1.
 */
static int
name_span(const tiku_trk_column_t *cols, int ncol, int row_x, int row_w,
          int hscroll, int *x0, int *x1)
{
    int i;

    *x0 = row_x;
    *x1 = row_x + row_w;
    for (i = 0; i < ncol; i++) {
        if (cols[i].field == TIKU_TRK_COL_NAME) {
            int left = row_x + cols[i].offset - hscroll;

            *x0 = (i == 0 || left < row_x) ? row_x : left;
            *x1 = left + cols[i].width;
            return i;
        }
    }
    return -1;
}

/**
 * @brief The rows on screen: all of them, or, when the frame has damage,
 *        the rows it touches and those that look different.  Each painted
 *        row's hash is kept, except on a recording surface.
 */
static void
draw_list(tiku_trk_folder_window_t *folder, tiku_surface_t *surface,
          tiku_rect_t list, int rows, int scroll, int total)
{
    tiku_trk_folder_render_t *render = folder->render;
    const tiku_font_t *font = tiku_font_plain();
    const tiku_trk_repaint_rect_t *damage = folder->render->damage;
    int ndamage = folder->render->ndamage;
    /* On a recording surface every row is painted (ndamage is 0 there)
     * and no hash is written back, since the kept pixels are not
     * painted. */
    int observing = (surface->record != NULL);
    int win_active = (folder->win == NULL ||
                      folder->win ==
                          tiku_workspace_focused(render->workspace));
    int i;

    for (i = 0; i < rows && scroll + i < total; i++) {
        tiku_trk_pose_t *pose = tiku_trk_poseview_at(folder->view,
                                                     scroll + i);
        tiku_rect_t row = { list.x + 2,
            list.y + 2 + i * render->row_h, list.w - 4, render->row_h };
        int text_y = row.y + (render->row_h - font->height) / 2 +
                     font->ascent;
        int selected, cut;

        if (pose == NULL) {
            break;
        }
        selected = tiku_trk_poseview_draws_selected(folder->view, pose);
        cut = tiku_trk_clip_mode_of(render->clip, pose->model.node_id) ==
              TIKU_TRK_CLIP_MOVE;
        if (render->drop->folder == folder && render->drop->row == i) {
            selected = 1;
        }
        if (ndamage > 0 && i < TIKU_TRK_ROWS_KEPT) {
            /* A row is painted when the damage touches it or its hash
             * changed. */
            uint32_t sig = row_signature(folder, pose, i, selected, cut);
            int d, hit = 0;

            for (d = 0; d < ndamage && !hit; d++) {
                int top = row.y - list.y - 2;

                hit = damage[d].y < top + render->row_h &&
                      damage[d].y + damage[d].h > top;
            }
            if (!observing && !hit && folder->row_sig[i] == sig) {
                /* A skipped row still queues its thumbnail, which is
                 * otherwise queued only while a row is drawn. */
                queue_thumbnail(folder, pose);
                continue;
            }
            if (!observing) {
                folder->row_sig[i] = sig;
            }
        } else if (!observing && i < TIKU_TRK_ROWS_KEPT) {
            folder->row_sig[i] = row_signature(folder, pose, i, selected,
                                               cut);
        }
        if (!observing) {
            render->rows_drawn++;
        }
        tiku_fill(surface, row, TIKU_C_DOC);
        {
            const tiku_trk_column_t *cols = NULL;
            int ncol = tiku_trk_poseview_columns(folder->view, &cols);
            int hscroll = tiku_trk_poseview_hscroll(folder->view);
            int mini = render->row_h - 2;
            int name_x, name_end, name_col;
            int thumbnail = 0, j;
            char art[TIKU_NAME_MAX];
            tiku_trk_icon_from_t source = tiku_trk_nodeicon_resolve(
                render->icons, render->store, &pose->model, art, sizeof art);

            if (mini > MINI_ICON_FOR(render->row_h)) {
                mini = MINI_ICON_FOR(render->row_h);
            }
            /* The selection covers the Name cell, and the icon too when
             * Name is the first column; the other columns stay plain
             * (IV-024). */
            name_col = name_span(cols, ncol, row.x, row.w, hscroll,
                                 &name_x, &name_end);
            if (selected) {
                tiku_rect_t name = { name_x, row.y,
                                          name_end - name_x, row.h };

                if (name.w < 0) {
                    name.w = 0;
                }
                /* An inactive window's selection is drawn in grey
                 * (IV-022). */
                tiku_fill_round(surface, name, 3, TIKU_CORNER_ALL,
                    win_active ? TIKU_C_SELECT
                               : TIKU_RGB(154, 154, 154));
            }
            if ((pose->model.kind == TIKU_KIND_VOLUME ||
                 pose->model.kind == TIKU_KIND_ROOT) &&
                tiku_trk_prefs_get(render->prefs, "ShowVolumeSpaceBar")) {
                draw_space_bar(render, surface, &pose->model, row.x + 2,
                    row.y + (render->row_h - mini) / 2 + 2, mini, 0);
            }
            if (source == TIKU_TRK_ICON_FROM_THUMB) {
                tiku_image_t image;

                if (tiku_thumb_fresh(render->store, pose->model.path,
                                         pose->model.facts.mtime) &&
                    tiku_thumb_load(render->store, pose->model.path,
                                        &image) == 0) {
                    tiku_thumb_paint(surface, &image, row.x + 2,
                        row.y + (render->row_h - mini) / 2, mini);
                    tiku_image_free(&image);
                    thumbnail = 1;
                } else {
                    (void)tiku_thumb_queue(render->thumbs,
                        pose->model.path, pose->model.facts.size, mini);
                }
            }
            if (!thumbnail) {
                (void)((pose->model.facts.link_broken || cut)
                    ? tiku_icon_paint_dim(surface, art, row.x + 2,
                        row.y + (render->row_h - mini) / 2, mini, 0.55f,
                        TIKU_C_DOC)
                    : selected
                    ? tiku_icon_paint_selected(surface, art, row.x + 2,
                        row.y + (render->row_h - mini) / 2, mini)
                    : tiku_icon_paint(surface, art, row.x + 2,
                        row.y + (render->row_h - mini) / 2, mini));
            }
            /* Each cell's text comes from tiku_trk_poseview_cell_text(),
             * the formatter column fitting also uses, so a column is
             * fitted to the text the row draws (PVL-032, TS-005). */
            for (j = 0; j < ncol; j++) {
                char tmp[160], fitted[96];
                const char *txt = tiku_trk_poseview_cell_text(folder->view,
                    pose, cols[j].field, tmp, sizeof tmp);
                int cell_x = list.x + 2 + cols[j].offset - hscroll;
                tiku_rect_t cell = { cell_x, row.y, cols[j].width,
                                          row.h };
                int tw;

                {   /* The cell is clipped to the list's inside, so a
                     * cell scrolled left of the view does not paint over
                     * the border and the window frame. */
                    tiku_rect_t inner = tiku_inset(list, 2);
                    int x0 = (cell.x > inner.x) ? cell.x : inner.x;
                    int x1 = (cell.x + cell.w < inner.x + inner.w)
                             ? cell.x + cell.w : inner.x + inner.w;

                    cell.x = x0;
                    cell.w = (x1 > x0) ? x1 - x0 : 0;
                    tiku_clip_set(surface, cell);
                }
                (void)tiku_text_fit(txt, cols[j].width,
                    tiku_trk_col_trunc_end(cols[j].field)
                        ? TIKU_TRUNC_END : TIKU_TRUNC_MIDDLE,
                    measure_label, NULL, fitted, sizeof fitted);
                tw = tiku_text_width(font, fitted);
                if (cols[j].alignment == TIKU_TRK_ALIGN_RIGHT) {
                    cell_x += cols[j].width - tw;
                }
                tiku_text(surface, font, cell_x, text_y, fitted,
                    (selected && j == name_col)
                        /* A cut label is greyed even while selected
                         * (IV-023); an inactive window's selection reads
                         * in plain ink over its grey. */
                        ? (!win_active ? TIKU_C_TEXT
                           : cut ? tiku_tint(TIKU_C_SELTEXT,
                                                  1.45f)
                                 : TIKU_C_SELTEXT)
                        : (cut ? tiku_dim(TIKU_C_TEXT, TIKU_C_DOC)
                               : TIKU_C_TEXT));
                if (cols[j].field == TIKU_TRK_COL_NAME &&
                    pose->model.kind == TIKU_KIND_SYMLINK) {
                    /* A link's name is underlined under its text only,
                     * in the Name column (MA-076, IV-043). */
                    tiku_hline(surface, cell_x, text_y + 2, tw,
                                    TIKU_C_TEXT);
                }
            }
            tiku_clip_set(surface, tiku_inset(list, 2));
        }
    }
}

/** @brief The first @p n bytes' width in the UI face (MA-049). */
static int
fw_measure_prefix(const char *text, int n, void *ctx)
{
    char tmp[TIKU_FIELD_MAX];

    (void)ctx;
    if (n < 0) { n = 0; }
    if (n >= (int)sizeof tmp) { n = (int)sizeof tmp - 1; }
    memcpy(tmp, text, (size_t)n);
    tmp[n] = '\0';
    return tiku_text_width(tiku_font_plain(), tmp);
}

/**
 * @brief The column title strip, in a face three-quarters of the body's,
 *        with a column being dragged drawn displaced.
 */
static void
draw_title(tiku_trk_folder_window_t *folder, tiku_surface_t *surface,
           tiku_rect_t title)
{
    const tiku_trk_column_t *columns;
    /* Three-quarters of the body face (TS-019). */
    const tiku_font_t *font =
        tiku_font_at(tiku_font_size() * 3 / 4);
    int n, i;

    int dragged = -1;
    int drag_dx = 0;

    if (folder->render->strip != NULL &&
        folder->render->strip_owner == folder) {
        dragged = tiku_trk_titleview_dragged(folder->render->strip,
                                             &drag_dx);
    }
    tiku_fill(surface, title, TIKU_C_PANEL);
    n = tiku_trk_poseview_columns(folder->view, &columns);
    tiku_clip_set(surface, title);
    for (i = 0; i < n; i++) {
        const tiku_trk_column_t *c = &columns[i];
        tiku_rect_t r = { title.x + c->offset - TIKU_TRK_COL_LEFT_MARGIN,
                               title.y, c->width + TIKU_TRK_COL_MARGIN,
                               title.h };
        char fit[64];
        int tw, tx;

        if (c->field == dragged) {
            /* The dragged title follows the pointer and is drawn after
             * its neighbours, on top of them (TS-005). */
            continue;
        }
        tiku_fill(surface, r, TIKU_C_PANEL);
        tiku_bevel(surface, r,
                        tiku_tint(TIKU_C_PANEL, 0.0f),
                        tiku_tint(TIKU_C_PANEL, 1.06f));
        (void)tiku_text_fit(c->title, c->width, TIKU_TRUNC_END,
                                measure_label, NULL, fit, sizeof fit);
        tw = tiku_text_width(font, fit);
        if (c->alignment == TIKU_TRK_ALIGN_RIGHT) {
            tx = title.x + c->offset + c->width - tw;
        } else if (c->alignment == TIKU_TRK_ALIGN_CENTER) {
            tx = r.x + (r.w - tw) / 2;
        } else {
            tx = title.x + c->offset + 1;
        }
        tiku_text(surface, font, tx,
                       title.y + (title.h - font->height) / 2 + font->ascent,
                       fit, TIKU_C_TEXT);
    }
    if (dragged >= 0) {
        for (i = 0; i < n; i++) {
            const tiku_trk_column_t *c = &columns[i];

            if (c->field == dragged) {
                tiku_rect_t r = { title.x + c->offset -
                                           TIKU_TRK_COL_LEFT_MARGIN +
                                           drag_dx,
                                       title.y,
                                       c->width + TIKU_TRK_COL_MARGIN,
                                       title.h };
                char fit[64];
                int tw, tx;

                tiku_fill(surface, r,
                    tiku_tint(TIKU_C_PANEL, 0.94f));
                tiku_bevel(surface, r,
                                tiku_tint(TIKU_C_PANEL, 1.06f),
                                tiku_tint(TIKU_C_PANEL, 0.0f));
                (void)tiku_text_fit(c->title, c->width,
                                        TIKU_TRUNC_END, measure_label,
                                        NULL, fit, sizeof fit);
                tw = tiku_text_width(font, fit);
                if (c->alignment == TIKU_TRK_ALIGN_RIGHT) {
                    tx = r.x + TIKU_TRK_COL_LEFT_MARGIN + c->width - tw;
                } else if (c->alignment == TIKU_TRK_ALIGN_CENTER) {
                    tx = r.x + (r.w - tw) / 2;
                } else {
                    tx = r.x + TIKU_TRK_COL_LEFT_MARGIN + 1;
                }
                tiku_text(surface, font, tx,
                    title.y + (title.h - font->height) / 2 + font->ascent,
                    fit, TIKU_C_TEXT);
            }
        }
    }
    tiku_clip_reset(surface);
    tiku_hline(surface, title.x, title.y + title.h - 1, title.w,
                    tiku_tint(TIKU_C_PANEL, TIKU_DARKEN_2));
    if (folder->render->strip != NULL &&
        folder->render->strip_owner == folder &&
        folder->render->strip->line_x >= 0) {
        /* The resize guide: one vertical line where the new edge will
         * land, the full height of the listing (TS-004). */
        tiku_rect_t content = tiku_trk_folder_window_content(folder);
        tiku_rect_t line = { title.x + folder->render->strip->line_x,
                                  title.y, 1,
                                  title.h + content.h };

        tiku_fill(surface, line, TIKU_C_TEXT);
    }
}

/** @brief The folder's own icon in the bar's corner, from its node's facts. */
static void
draw_folder_icon(tiku_trk_folder_window_t *folder,
                 tiku_surface_t *surface)
{
    tiku_trk_folder_render_t *render = folder->render;
    tiku_model_t model;
    char art[TIKU_NAME_MAX];
    tiku_rect_t icon;

    if (render->backend == NULL || render->backend->ops == NULL ||
        render->backend->ops->stat(render->backend, folder->path,
                                   &model) != 0) {
        return;
    }
    (void)tiku_trk_nodeicon_resolve(render->icons, render->store, &model,
                                    art, sizeof art);
    icon = tiku_trk_folder_window_icon(folder);
    (void)tiku_icon_paint(surface, art, icon.x, icon.y, icon.w);
}

/**
 * @brief The window's own pixels, sized to the rows it shows.
 *
 * A new or resized surface holds nothing, so the frame that makes one
 * paints all of it.
 * @return 1 when the pixels are there, 0 when they cannot be had.
 */
static int
keep_pixels(tiku_trk_folder_window_t *folder, int w, int h, int scale)
{
    if (w <= 0 || h <= 0) {
        return 0;
    }
    if (scale < 1) {
        scale = 1;
    }
    if (folder->kept.px == NULL || folder->kept.w != w ||
        folder->kept.h != h || folder->kept.scale != scale) {
        tiku_rgb_t *px = realloc(folder->kept.px,
                                      (size_t)w * (size_t)scale *
                                      (size_t)h * (size_t)scale * sizeof *px);

        if (px == NULL) {
            return 0;
        }
        folder->kept.px = px;
        folder->kept.w = w;
        folder->kept.h = h;
        folder->kept.scale = scale;
        folder->kept_ready = 0;
    }
    folder->kept.clip = (tiku_rect_t){ 0, 0, w, h };
    return 1;
}

void
tiku_trk_folder_window_forget(tiku_trk_folder_window_t *folder)
{
    if (folder != NULL) {
        free(folder->kept.px);
        folder->kept.px = NULL;
        folder->kept.w = 0;
        folder->kept.h = 0;
        folder->kept_ready = 0;
    }
}

/**
 * @brief What is drawn over the rows and does not stay: the selection
 *        band and, in list mode, the inline rename field.
 */
static void
draw_transients(tiku_trk_folder_window_t *folder,
                tiku_surface_t *surface, tiku_rect_t list,
                int off_x, int off_y, int icons, int rows, int scroll)
{
    tiku_trk_folder_render_t *render = folder->render;

    if (render->band->folder == folder) {
        tiku_rect_t band;
        int x0 = render->band->x0, y0 = render->band->y0;
        int x1 = render->band->x1, y1 = render->band->y1;

        /* The band is in screen coordinates; off_x and off_y move it
         * onto the surface being painted. */
        x0 -= off_x;
        x1 -= off_x;
        y0 -= off_y;
        y1 -= off_y;
        band = (tiku_rect_t){ x0 < x1 ? x0 : x1,
                                   y0 < y1 ? y0 : y1,
                                   x0 > x1 ? x0 - x1 : x1 - x0,
                                   y0 > y1 ? y0 - y1 : y1 - y0 };
        if (band.w > 0 && band.h > 0) {
            tiku_paint_band(surface, band,
                tiku_trk_prefs_get(render->prefs, "TransparentSelection"));
        }
    }
    if (render->edit->renaming && render->edit->rename_folder == folder &&
        !icons) {
        const tiku_field_t *edit = &render->edit->rename_edit;
        int at = render->edit->rename_row - scroll;

        if (at >= 0 && at < rows) {
            /* Over the part of the row the selection covers, grown with
             * the text up to the Name column's end, slid to keep the
             * caret visible past that cap (MA-049). */
            const tiku_trk_column_t *cols = NULL;
            int ncol = tiku_trk_poseview_columns(folder->view, &cols);
            int x0, x1, cap, bw, bs = 0;
            tiku_rect_t box;

            (void)name_span(cols, ncol, list.x + 2, list.w - 4,
                            tiku_trk_poseview_hscroll(folder->view), &x0,
                            &x1);
            if (x1 > list.x + list.w - 2) {
                x1 = list.x + list.w - 2;
            }
            cap = (x1 > x0) ? x1 - x0 : 0;
            bw = cap;
            tiku_field_metrics(edit, 60, cap, fw_measure_prefix, NULL, &bw,
                               &bs);
            box = (tiku_rect_t){ x0,
                list.y + 2 + at * render->row_h, bw, render->row_h };
            tiku_ui_textfield_scroll(surface, box, edit->text, edit->caret,
                                     edit->sel_a, edit->sel_b, TIKU_S_FOCUS,
                                     bs);
        }
    }
}

/**
 * @brief The count line along the bottom: the type-ahead find or a value
 *        being edited in the focused window, else the status line, the
 *        text typed ahead, or the item and selection count.
 */
static void
draw_count(tiku_trk_folder_window_t *folder, tiku_surface_t *surface,
           tiku_rect_t content, int total)
{
    tiku_trk_folder_render_t *render = folder->render;
    const tiku_font_t *font = tiku_font_plain();
    tiku_rect_t count = { content.x,
        content.y + content.h - COUNT_HEIGHT, content.w, COUNT_HEIGHT };
    const char *typed = tiku_trk_poseview_type_string(folder->view);
    char label[256];
    int focused = folder->win ==
                  tiku_workspace_focused(render->workspace);

    if (*render->finding && focused) {
        tiku_ui_sunken(surface, count, TIKU_C_DOC);
        snprintf(label, sizeof label, "find: %.60s_", render->find_text);
    } else if (render->edit->editing && focused) {
        const char *slash = strrchr(render->edit->edit_path, '/');

        tiku_ui_sunken(surface, count, TIKU_C_DOC);
        snprintf(label, sizeof label, "%.100s = %.64s_",
                 slash != NULL ? slash + 1 : render->edit->edit_path,
                 render->edit->value_edit.text);
    } else if (render->status[0] != '\0') {
        tiku_fill(surface, count, TIKU_C_PANEL);
        snprintf(label, sizeof label, "%s", render->status);
    } else if (typed[0] != '\0') {
        tiku_fill(surface, count, TIKU_C_PANEL);
        snprintf(label, sizeof label, "%s", typed);
    } else {
        int selected = tiku_trk_poseview_selection_count(folder->view);

        tiku_fill(surface, count, TIKU_C_PANEL);
        if (total == folder->count_last_total &&
            selected == folder->count_last_selected &&
            folder->count_text[0] != '\0') {
            snprintf(label, sizeof label, "%s", folder->count_text);
        } else {
            if (total == 0) {
                snprintf(label, sizeof label, "%s",
                             tiku_str("status.empty_folder"));
            } else if (selected > 0) {
                snprintf(label, sizeof label, tiku_str("status.selected"),
                         selected, total);
            } else {
                snprintf(label, sizeof label, tiku_str("status.items"), total,
                         total == 1 ? "" : "s");
            }
            folder->count_last_total = total;
            folder->count_last_selected = selected;
            snprintf(folder->count_text, sizeof folder->count_text, "%s",
                     label);
        }
        if (folder->live) {
            size_t used = strlen(label);
            snprintf(label + used, sizeof label - used, " — %s",
                folder->run && !tiku_query_done(folder->run)
                    ? "searching…" : "live");
        }
        /* The shell reads one count line, and the one it means is the
         * front window's: draw order is z-order, so the last window to
         * write here is the one on top. */
        snprintf(render->count_text, render->count_text_size, "%s", label);
    }
    tiku_text(surface, font, count.x + 4,
        count.y + (COUNT_HEIGHT - font->height) / 2 + font->ascent,
        label, TIKU_C_TEXT);
}

void
tiku_trk_folder_window_draw(tiku_window_t *window,
    tiku_surface_t *surface, tiku_rect_t content, void *context)
{
    tiku_trk_folder_window_t *folder = context;
    tiku_trk_folder_render_t *render = folder->render;
    tiku_rect_t list = content;
    tiku_rect_t bar = content;
    tiku_rect_t title;
    tiku_surface_t *paint;
    tiku_rect_t area;
    int rows, scroll, total, icons;
    int slide, slide_y = 0;

    (void)window;
    if (render->prepare_menu != NULL) {
        render->prepare_menu(render->prepare_context, folder);
    }
    /* Read on every draw: whether a selection shows in an inactive
     * window at all (IV-022). */
    tiku_trk_poseview_show_selection_when_inactive(folder->view,
        tiku_trk_prefs_get(render->prefs, "ShowSelectionWhenInactive"));
    bar.h = render->global_bar ? 0 : render->menus->bar.height;
    if (!render->global_bar) {
        tiku_menubar_draw(&render->menus->bar, surface, bar);
        draw_folder_icon(folder, surface);
    }
    list.y += bar.h;
    list.h -= bar.h;
    title = (tiku_rect_t){ list.x, list.y, list.w, TITLE_HEIGHT };
    if (tiku_trk_poseview_mode(folder->view) == TIKU_TRK_MODE_LIST) {
        draw_title(folder, surface, title);
        list.y += TITLE_HEIGHT;
        list.h -= TITLE_HEIGHT;
        /* Room for the scroll bar, in list mode only (PVL-051). */
        list.w -= 15;
    }
    list.h -= COUNT_HEIGHT;
    icons = tiku_trk_poseview_mode(folder->view) != TIKU_TRK_MODE_LIST;
    /* Where the rows go: the window's own kept pixels in every mode,
     * drawn at their own origin and blitted to the frame at the end, or
     * the frame itself when the kept pixels cannot be had. */
    paint = surface;
    area = list;
    if (folder->kept_icons != icons) {
        /* Kept pixels painted in the other mode are not reused. */
        folder->kept_ready = 0;
        folder->kept_icons = icons;
    }
    /* A recording surface is painted directly: its reader gets the rows
     * themselves, and the kept pixels and their hashes stay as the shown
     * frames left them. */
    if (surface->record == NULL &&
        keep_pixels(folder, list.w, list.h, surface->scale)) {
        paint = &folder->kept;
        area = (tiku_rect_t){ 0, 0, list.w, list.h };
    }
    /* A recording surface leaves the pending slide for the next shown
     * frame. */
    slide = (surface->record == NULL)
        ? tiku_trk_poseview_repaint_take_slide(folder->view, &slide_y)
        : 0;
    render->ndamage = (!icons && paint != surface && folder->kept_ready)
                          ? tiku_trk_poseview_repaint_take(folder->view,
                                render->damage, TIKU_TRK_REPAINT_MAX)
                          : 0;
    fw_draw_rows(window, surface, content, context, folder, render, paint, area,
                 icons, slide, slide_y);
    tiku_trk_poseview_set_viewport(folder->view, list.w - 4, list.h - 4);
    tiku_trk_poseview_set_row_height(folder->view, render->row_h);
    fw_draw_icons(window, surface, content, context, folder, render, list,
                  paint, &area, &rows, &scroll, &total, icons);
    draw_transients(folder, paint, area, list.x - area.x, list.y - area.y,
                    icons, rows, scroll);
    tiku_clip_reset(paint);
    fw_draw_finish(window, surface, content, context, &folder, list, paint,
                   rows, scroll, total, icons);
    draw_count(folder, surface, content, total);
}

tiku_rect_t
tiku_trk_folder_window_scrollbar(const tiku_trk_folder_window_t *folder)
{
    tiku_rect_t none = { 0, 0, 0, 0 };
    tiku_rect_t content;

    if (folder == NULL || folder->win == NULL || !folder->win->open ||
        tiku_trk_poseview_mode(folder->view) != TIKU_TRK_MODE_LIST) {
        return none;
    }
    content = tiku_trk_folder_window_content(folder);
    return (tiku_rect_t){ content.x + content.w - 15, content.y,
                               15, content.h - COUNT_HEIGHT };
}

int
tiku_trk_folder_window_fit_width(const tiku_trk_folder_window_t *folder)
{
    const tiku_font_t *font = tiku_font_plain();
    int widest = 0, i, count = tiku_trk_poseview_count(folder->view);
    int query = tiku_trk_poseview_is_query(folder->view);

    for (i = 0; i < count; i++) {
        tiku_trk_pose_t *pose = tiku_trk_poseview_at(folder->view, i);
        char value[64];
        int width;

        if (pose == NULL) {
            continue;
        }
        width = MINI_ICON_FOR(folder->render->row_h) + 6 +
                tiku_text_width(font,
            query ? pose->model.path : pose->model.name);
        if (!tiku_model_is_container(&pose->model)) {
            int needed;

            (void)tiku_attr_format(&pose->desc,
                pose->value_valid ? pose->value : NULL, value, sizeof value);
            needed = tiku_text_width(font, value) + VALUE_GAP + 8;
            if (needed > width) {
                width = needed;
            }
        }
        if (width > widest) {
            widest = width;
        }
    }
    return widest < 200 ? 200 : widest + 12;
}

int
tiku_trk_folder_window_fit_height(const tiku_trk_folder_window_t *folder)
{
    int title = tiku_trk_poseview_mode(folder->view) == TIKU_TRK_MODE_LIST
                    ? TITLE_HEIGHT : 0;

    return tiku_trk_poseview_count(folder->view) * folder->render->row_h +
           4 + COUNT_HEIGHT + title;
}

int
tiku_trk_folder_window_row_at(const tiku_trk_folder_window_t *folder,
                              int x, int y)
{
    tiku_rect_t content = tiku_trk_folder_window_content(folder);
    int relative;

    if (x < content.x || x > content.x + content.w || y < content.y + 2 ||
        y > content.y + content.h - COUNT_HEIGHT) {
        return -1;
    }
    if (tiku_trk_poseview_mode(folder->view) != TIKU_TRK_MODE_LIST) {
        int origin_x = 0, origin_y = 0;
        tiku_trk_pose_t *pose;

        tiku_trk_poseview_origin(folder->view, &origin_x, &origin_y);
        pose = tiku_trk_poseview_pose_at(folder->view,
            x - (content.x + 2) + origin_x,
            y - (content.y + 2) + origin_y);
        return pose != NULL ? pose->index : -1;
    }
    relative = (y - content.y - 2) / folder->render->row_h;
    relative += tiku_trk_poseview_scroll(folder->view);
    return relative < tiku_trk_poseview_count(folder->view) ? relative : -1;
}

int
tiku_trk_folder_window_count_at(const tiku_trk_folder_window_t *folder,
                                int x, int y)
{
    tiku_rect_t content;

    if (folder == NULL) {
        return 0;
    }
    content = tiku_trk_folder_window_content(folder);
    return x >= content.x && x <= content.x + content.w &&
           y >= content.y + content.h - COUNT_HEIGHT &&
           y <= content.y + content.h;
}

int
tiku_trk_folder_window_column_at(const tiku_trk_folder_window_t *folder,
                                 int x)
{
    const tiku_trk_column_t *cols = NULL;
    tiku_rect_t content;
    int ncol, j, hs;

    if (folder == NULL || folder->view == NULL ||
        tiku_trk_poseview_mode(folder->view) != TIKU_TRK_MODE_LIST) {
        return -1;
    }
    content = tiku_trk_folder_window_content(folder);
    ncol = tiku_trk_poseview_columns(folder->view, &cols);
    hs = tiku_trk_poseview_hscroll(folder->view);
    for (j = 0; j < ncol; j++) {
        int cx = content.x + 2 + cols[j].offset - hs;

        if (x >= cx && x < cx + cols[j].width) {
            return cols[j].field;
        }
    }
    return -1;
}

int
tiku_trk_folder_window_name_at(const tiku_trk_folder_window_t *folder,
                               int row, int x, int y)
{
    tiku_rect_t content = tiku_trk_folder_window_content(folder);

    if (row < 0) {
        return 0;
    }
    if (tiku_trk_poseview_mode(folder->view) == TIKU_TRK_MODE_LIST) {
        const tiku_trk_column_t *cols = NULL;
        int ncol = tiku_trk_poseview_columns(folder->view, &cols);
        int x0, x1;

        (void)name_span(cols, ncol, content.x + 2, content.w - 4,
                        tiku_trk_poseview_hscroll(folder->view), &x0, &x1);
        return x >= x0 && x < x1;
    }
    {
        tiku_trk_pose_t *pose = tiku_trk_poseview_at(folder->view, row);
        int origin_x = 0, origin_y = 0;
        int ix, iy, iw, ih, lx, ly, lw, lh;
        int view_x, view_y;

        if (pose == NULL) {
            return 0;
        }
        tiku_trk_poseview_origin(folder->view, &origin_x, &origin_y);
        view_x = x - (content.x + 2) + origin_x;
        view_y = y - (content.y + 2) + origin_y;
        tiku_trk_poseview_pose_parts(folder->view, pose, &ix, &iy, &iw, &ih,
                                     &lx, &ly, &lw, &lh);
        return view_x >= lx && view_x < lx + lw &&
               view_y >= ly && view_y < ly + lh;
    }
}

/**
 * @brief Blit the kept pixels to the frame and, in list mode, draw the
 *        scroll bar with its focus frame.
 */
static void
fw_draw_finish(tiku_window_t *window, tiku_surface_t *surface,
               tiku_rect_t content, void *context,
               tiku_trk_folder_window_t **folder, tiku_rect_t list,
               tiku_surface_t *paint, int rows, int scroll, int total,
               int icons)
{
    (void)content;
    (void)context;
    if (paint != surface) {
        /* The frame gets the kept pixels whole, whichever frame painted
         * each row. */
        tiku_blit(surface, list.x, list.y, &(*folder)->kept);
        (*folder)->kept_ready = 1;
    }
    if (!icons) {
        /* The scroll bar is drawn from its own state in sbar, which
         * holds a grip across frames (PVL-051). */
        tiku_rect_t sb = { list.x + list.w, list.y, 15, list.h };
        int tpos = 0, tlen = 0;
        float frac, pos;

        (void)tiku_scroll_set_range(&(*folder)->sbar, total, rows);
        if (!(*folder)->sbar.tracking) {
            (void)tiku_scroll_to(&(*folder)->sbar, scroll);
        }
        if (tiku_scroll_thumb(&(*folder)->sbar, sb.h, &tpos, &tlen)) {
            frac = (float)tlen / (float)sb.h;
            pos = (sb.h > tlen) ? (float)tpos / (float)(sb.h - tlen)
                                : 0.0f;
        } else {
            frac = 1.0f;
            pos = 0.0f;
        }
        tiku_ui_scrollbar_held(surface, sb, pos, frac, 0,
                               (*folder)->sbar.arrow_dir);
        /* The bar takes the focus frame while the window has the
         * keyboard (CW-019); a query window's bar never does. */
        if (!tiku_trk_poseview_is_query((*folder)->view) &&
            ((*folder)->win == NULL ||
             (*folder)->win ==
                 tiku_workspace_focused((*folder)->render->workspace))) {
            tiku_frame(surface, sb, TIKU_C_FOCUS);
        }
    }
}

/**
 * @brief Measure the visible rows, then paint the poses: in an icon mode
 *        only those whose signature changed, in list mode the rows
 *        draw_list() repaints.
 */
static void
fw_draw_icons(tiku_window_t *window, tiku_surface_t *surface,
              tiku_rect_t content, void *context,
              tiku_trk_folder_window_t *folder,
              tiku_trk_folder_render_t *render, tiku_rect_t list,
              tiku_surface_t *paint, tiku_rect_t *area, int *rows, int *scroll,
              int *total, int icons)
{
    (void)window;
    (void)content;
    (void)context;
    if (icons) {
        (void)tiku_trk_poseview_place_unplaced(folder->view, list.w - 4);
    }
    (*rows) = (list.h - 4) / render->row_h;
    tiku_trk_poseview_set_visible_rows(folder->view, (*rows));
    (*scroll) = tiku_trk_poseview_scroll(folder->view);
    (*total) = tiku_trk_poseview_count(folder->view);
    tiku_clip_set(paint, tiku_inset((*area), 2));
    if (icons) {
        /* An icon frame paints only what changed: every visible pose is
         * signed, a pose whose signature moved is repainted where it is
         * and where it was, and a frame in which nothing moved paints
         * nothing (PVL-062).  A change of icon size, area, mode, ground or
         * origin, a band in progress or a recording surface repaints the
         * whole area. */
        tiku_rect_t inner = tiku_inset((*area), 2);
        uint32_t vsig = UINT32_C(2166136261);
        tiku_rect_t dmg[24];
        uint32_t cur_sig[TIKU_TRK_ICONS_KEPT];
        tiku_rect_t cur_rect[TIKU_TRK_ICONS_KEPT];
        int cur_at[TIKU_TRK_ICONS_KEPT];
        int cur_n = 0, ndmg = 0, full, first = -1, last = -1, k, j;
        int origin_x = 0, origin_y = 0, isz, band_up;

        tiku_trk_poseview_origin(folder->view, &origin_x, &origin_y);
        isz = tiku_trk_poseview_icon_size(folder->view);
        vsig = sig_bytes(vsig, &isz, sizeof isz);
        vsig = sig_bytes(vsig, &(*area).w, sizeof (*area).w);
        vsig = sig_bytes(vsig, &(*area).h, sizeof (*area).h);
        {
            int m = tiku_trk_poseview_mode(folder->view);
            tiku_rgb_t bg = folder_background(folder->view);

            vsig = sig_bytes(vsig, &m, sizeof m);
            vsig = sig_bytes(vsig, &bg, sizeof bg);
        }
        band_up = (render->band->folder == folder);
        full = !folder->kept_ready || vsig != folder->icon_view_sig ||
               origin_x != folder->icon_origin_x ||
               origin_y != folder->icon_origin_y || band_up ||
               surface->record != NULL;
        if (tiku_trk_poseview_visible_range(folder->view, &first, &last)) {
            int ox = origin_x - ((*area).x + 2);
            int oy = origin_y - ((*area).y + 2);

            for (k = first; k <= last; k++) {
                tiku_trk_pose_t *pose = tiku_trk_poseview_at(folder->view,
                                                             k);

                if (pose == NULL || !pose->placed) {
                    continue;
                }
                if (cur_n >= TIKU_TRK_ICONS_KEPT) {
                    full = 1;
                    break;
                }
                cur_sig[cur_n] = pose_signature(folder, pose);
                cur_rect[cur_n] = pose_footprint(folder, pose, ox, oy);
                cur_at[cur_n] = k;
                cur_n++;
            }
        }
        if (!full) {
            int most = (cur_n > folder->icon_kept_n)
                           ? cur_n : folder->icon_kept_n;

            for (k = 0; k < most; k++) {
                int changed = (k >= cur_n || k >= folder->icon_kept_n ||
                               cur_sig[k] != folder->icon_sig[k]);

                if (!changed) {
                    continue;
                }
                if (k < folder->icon_kept_n && ndmg < 24) {
                    dmg[ndmg++] = folder->icon_rect[k];
                }
                if (k < cur_n && ndmg < 24) {
                    dmg[ndmg++] = cur_rect[k];
                }
                if (ndmg >= 24) {
                    full = 1;
                    break;
                }
            }
        }
        if (full) {
            /* The frame's border lies outside the pose clip, so the clip
             * is reset to draw it. */
            tiku_clip_reset(paint);
            tiku_ui_sunken(paint, (*area),
                                folder_background(folder->view));
            tiku_clip_set(paint, inner);
            folder->bg_drawn = tiku_trk_folder_window_draw_background(
                folder, paint, inner);
            tiku_trk_folder_window_draw_icons(folder, paint, (*area));
        } else if (ndmg > 0) {
            for (k = 0; k < ndmg; k++) {
                tiku_rect_t r = dmg[k];

                if (r.x < inner.x) { r.w -= inner.x - r.x; r.x = inner.x; }
                if (r.y < inner.y) { r.h -= inner.y - r.y; r.y = inner.y; }
                if (r.x + r.w > inner.x + inner.w) {
                    r.w = inner.x + inner.w - r.x;
                }
                if (r.y + r.h > inner.y + inner.h) {
                    r.h = inner.y + inner.h - r.y;
                }
                if (r.w <= 0 || r.h <= 0) {
                    continue;
                }
                tiku_clip_set(paint, r);
                tiku_fill(paint, r,
                    folder_background(folder->view));
                (void)tiku_trk_folder_window_draw_background(folder, paint,
                                                             inner);
                for (j = 0; j < cur_n; j++) {
                    if (rects_touch(cur_rect[j], r)) {
                        tiku_trk_pose_t *pose = tiku_trk_poseview_at(
                            folder->view, cur_at[j]);

                        if (pose != NULL) {
                            draw_pose_icon(folder, paint, pose,
                                origin_x - ((*area).x + 2),
                                origin_y - ((*area).y + 2));
                        }
                    }
                }
                tiku_clip_reset(paint);
            }
        }
        tiku_clip_set(paint, inner);
        if (surface->record == NULL) {
            memcpy(folder->icon_sig, cur_sig,
                   sizeof cur_sig[0] * (size_t)cur_n);
            memcpy(folder->icon_rect, cur_rect,
                   sizeof cur_rect[0] * (size_t)cur_n);
            folder->icon_kept_n = cur_n;
            folder->icon_view_sig = vsig;
            folder->icon_origin_x = origin_x;
            folder->icon_origin_y = origin_y;
        }
    } else {
        draw_list(folder, paint, (*area), (*rows), (*scroll), (*total));
    }
}

/**
 * @brief Lay the list's ground, whole or only the damaged strips, after
 *        sliding the rows that only moved; the icon modes lay their own.
 *
 * Only the damaged strips are filled, and draw_list() repaints every row a
 * strip touches, so no row is left as bare ground.
 */
static void
fw_draw_rows(tiku_window_t *window, tiku_surface_t *surface,
             tiku_rect_t content, void *context,
             tiku_trk_folder_window_t *folder, tiku_trk_folder_render_t *render,
             tiku_surface_t *paint, tiku_rect_t area, int icons, int slide,
             int slide_y)
{
    (void)window;
    (void)surface;
    (void)content;
    (void)context;
    if (icons) {
        /* The icon modes paint their ground in fw_draw_icons(); filling
         * it here would erase poses no frame repaints (PVL-062). */
    } else if (render->ndamage > 0) {
        tiku_rect_t inner = tiku_inset(area, 2);
        int d;

        if (slide != 0) {
            /* Rows that an insertion or removal only moved are copied to
             * their new place; only the rows the change touched are
             * painted. */
            int rh = render->row_h > 0 ? render->row_h : 1;
            int by = slide / rh;
            int from = slide_y / rh;
            int k;

            /* Clipped to the rows, so the border below them is not slid
             * over. */
            tiku_clip_set(paint, inner);
            tiku_copy_bits(paint,
                                (tiku_rect_t){ inner.x,
                                                    inner.y + slide_y,
                                                    inner.w,
                                                    inner.h - slide_y },
                                0, slide);
            tiku_clip_reset(paint);
            /* The slots' signatures move with the pixels. */
            if (by > 0) {
                for (k = TIKU_TRK_ROWS_KEPT - 1; k >= from; k--) {
                    folder->row_sig[k] = (k - by >= from)
                        ? folder->row_sig[k - by] : 0u;
                }
            } else {
                for (k = from + by; k < TIKU_TRK_ROWS_KEPT; k++) {
                    folder->row_sig[k] = (k - by < TIKU_TRK_ROWS_KEPT)
                        ? folder->row_sig[k - by] : 0u;
                }
            }
        }
        for (d = 0; d < render->ndamage; d++) {
            tiku_rect_t strip = { inner.x, inner.y + render->damage[d].y,
                                       inner.w, render->damage[d].h };

            if (strip.y < inner.y) {
                strip.h -= inner.y - strip.y;
                strip.y = inner.y;
            }
            if (strip.y + strip.h > inner.y + inner.h) {
                strip.h = inner.y + inner.h - strip.y;
            }
            if (strip.h > 0) {
                tiku_fill(paint, strip,
                               folder_background(folder->view));
            }
        }
    } else {
        tiku_ui_sunken(paint, area,
                            folder_background(folder->view));
    }
}
