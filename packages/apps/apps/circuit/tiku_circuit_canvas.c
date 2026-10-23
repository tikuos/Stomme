/* tiku_circuit_canvas.c -- the window's face: where the toolbar, Find,
 * the list, the canvas and the status lines go, how they are drawn, and
 * the pointer and keys that pan, zoom and select on the canvas.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#include "tiku_app_product.h"
#include "tiku_circuit_window_private.h"
#include "tiku_font.h"
#include "tiku_iconpaint.h"
#include "tiku_text_layout_paint.h"
#include "tiku_ui.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CV_TOOLS         6      /* buttons on the toolbar */
#define CV_EDGE          6      /* the controls from the window's side */
#define CV_TOOLBAR_TOP   5      /* above the toolbar */
#define CV_SPACE         4      /* between two buttons, and two rows */
#define CV_ROW_GAP       5      /* between the toolbar and Find */
#define CV_BUTTON_LEAST  28     /* a button's least width */
#define CV_ICON_ROOM     22     /* what Open's icon adds to its button */
#define CV_FIND_LABEL_W  36     /* the "Find:" before the field */
#define CV_FIND_X        44     /* the field, from the window's left */
#define CV_FIND_GAP      4      /* between the field and Next */
#define CV_FIND_RIGHT    14     /* right of Next */
#define CV_FIND_LEAST    24     /* the field's least width */
#define CV_HINT_INSET    5      /* the empty field's hint, inside it */
#define CV_CANVAS_GAP    6      /* between Find and the canvas */
#define CV_STATUS_ROWS   3      /* the lines under the canvas */
#define CV_STATUS_PAD    6      /* added to the font for each */
#define CV_FOOTER_PAD    8      /* under the last of them */
#define CV_STATUS_INSET  8      /* the lines from the window's side */
#define CV_STATUS_GAP    4      /* between the canvas and the lines */
#define CV_LIST_MIN_W    520    /* the least window the list shows in */
#define CV_LIST_MIN_H    70     /* and the least canvas */
#define CV_LIST_SHARE    4      /* the list takes a quarter of the width */
#define CV_LIST_MAX_W    235    /* and at most this */
#define CV_LIST_INSET    3      /* its rows from its edge */
#define CV_LIST_HEAD     18     /* added to the font for its title */
#define CV_LIST_ROW_PAD  10     /* added to the font for a row */
#define CV_LIST_GAP      6      /* between the list and the canvas */
#define CV_ICON          16     /* Open's folder icon */
#define CV_ICON_X        4      /* the icon from the button's left */
#define CV_ICON_TEXT     25     /* Open's word from the button's left */
#define CV_ICON_TEXT_PAD 2      /* and from its right */
#define CV_NOTE_INSET    24     /* the empty canvas's text from its edge */
#define CV_NOTE_GAP      10     /* between that text's paragraphs */
#define CV_NOTE_MAX      768    /* an unavailable sheet's text */
#define CV_LINE_MAX      384    /* a status line, with its end */
#define CV_SELECTION     0xff3278b8u    /* the selected object's frame */
#define CV_WARNING       0xff865218u    /* the import's warning */
#define CV_FRAME_OUT     3      /* the frame outside the object's bounds */
#define CV_PREVIEW_LIMIT 1e7    /* the largest stretched preview drawn */
#define CV_FRAME_LIMIT   1e8    /* the largest selection frame drawn */
#define CV_PAN_STEP      24     /* pixels an arrow key pans */
#define CV_PAN_LIMIT     500000 /* the farthest the view pans, in pixels */
/* Pointer travel, in pixels, before a press on the canvas pans. */
#define CV_DRAG_SLOP     2

void tiku_circuitview_layout(tiku_circuitview_t *v, tiku_rect_t r)
{
    int bh = tiku_ui_button_h(), x = r.x + CV_EDGE, y = r.y + CV_TOOLBAR_TOP;
    const int ids[CV_TOOLS] = {CIRCUIT_OPEN, CIRCUIT_RELOAD, CIRCUIT_FIT,
                               CIRCUIT_OUT, CIRCUIT_IN, CIRCUIT_CANCEL};
    const char *const labels[CV_TOOLS] = {"Open", "Reload", "Fit", "-", "+",
                                          "Cancel"};

    tiku_dialog_begin(&v->dialog, -1, -1);
    for (int i = 0; i < CV_TOOLS; i++) {
        int width = tiku_ui_button_w(labels[i], CV_BUTTON_LEAST) +
                    (i == 0 ? CV_ICON_ROOM : 0);
        if (x > r.x + CV_EDGE && x + width > r.x + r.w - CV_EDGE) {
            x = r.x + CV_EDGE;
            y += bh + CV_SPACE;
        }
        tiku_dialog_add(&v->dialog, ids[i], (tiku_rect_t){x, y, width, bh},
                        TIKU_DIALOG_BUTTON,
                        tiku_circuitview_enabled(v, ids[i]));
        x += width + CV_SPACE;
    }
    y += bh + CV_ROW_GAP;
    int next = tiku_ui_button_w("Next", CV_BUTTON_LEAST);
    int fw = r.w - (CV_FIND_X + CV_FIND_GAP + CV_FIND_RIGHT) - next;
    if (fw < CV_FIND_LEAST) {
        fw = CV_FIND_LEAST;
    }
    v->find_rect = (tiku_rect_t){r.x + CV_FIND_X, y, fw, bh};
    tiku_dialog_add(&v->dialog, CIRCUIT_FIND, v->find_rect,
                    TIKU_DIALOG_INPUT, !!v->document || !!v->project);
    tiku_dialog_add(&v->dialog, CIRCUIT_NEXT,
                    (tiku_rect_t){r.x + CV_FIND_X + CV_FIND_GAP + fw, y,
                                  next, bh},
                    TIKU_DIALOG_BUTTON, !!v->document || !!v->project);
    int footer = CV_STATUS_ROWS * (tiku_font_plain()->height + CV_STATUS_PAD) +
                 CV_FOOTER_PAD;
    tiku_rect_t canvas = {r.x + CV_EDGE, y + bh + CV_CANVAS_GAP,
                          r.w - 2 * CV_EDGE,
                          r.y + r.h - footer - y - bh - CV_CANVAS_GAP};
    if (canvas.w < 1) {
        canvas.w = 1;
    }
    if (canvas.h < 1) {
        canvas.h = 1;
    }
    v->details = (tiku_rect_t){r.x + CV_STATUS_INSET,
                               canvas.y + canvas.h + CV_STATUS_GAP,
                               r.w - 2 * CV_STATUS_INSET,
                               footer - CV_STATUS_GAP};
    v->area = canvas;
    tiku_circuitview_details_layout(v, &canvas);
    v->navigator = (tiku_rect_t){0};
    v->sheet_rows = (tiku_rect_t){0};
    int nw = tiku_circuitview_rows(v) > 1 ?
             tiku_circuitview_list_width(v, r, canvas) : 0;
    if (nw) {
        v->navigator = (tiku_rect_t){canvas.x, canvas.y, nw, canvas.h};
        v->sheet_rows = tiku_inset(v->navigator, CV_LIST_INSET);
        int header = tiku_font_plain()->height + CV_LIST_HEAD;
        v->sheet_rows.y += header;
        v->sheet_rows.h -= header;
        tiku_list_set_row_h(&v->sheets,
                            tiku_font_plain()->height + CV_LIST_ROW_PAD);
        int bar = v->sheets.count >
                  tiku_list_visible_rows(&v->sheets, v->sheet_rows) ?
                  CV_BAR : 0;
        v->sheet_rows.w -= bar;
        v->sheet_bar = (tiku_rect_t){v->sheet_rows.x + v->sheet_rows.w,
                                     v->sheet_rows.y, bar, v->sheet_rows.h};
        if (!bar) {
            tiku_scroll_release(&v->sheet_scroll);
        }
        tiku_scroll_set_range(&v->sheet_scroll, v->sheets.count,
            tiku_list_visible_rows(&v->sheets, v->sheet_rows));
        tiku_list_scroll_to(&v->sheets, v->sheets.top, v->sheet_rows);
        if (!v->sheet_scroll.tracking) {
            tiku_scroll_to(&v->sheet_scroll, v->sheets.top);
        }
        tiku_dialog_add(&v->dialog, CIRCUIT_SHEET_LIST, v->navigator,
                        TIKU_DIALOG_INPUT, 1);
        canvas.x += nw + CV_LIST_GAP;
        canvas.w -= nw + CV_LIST_GAP;
    } else {
        tiku_scroll_release(&v->sheet_scroll);
    }
    tiku_dialog_end(&v->dialog);
    /* The view is sized to the canvas: an import's first view already
     * is, and a view of another size is refitted, or resized when Fit is
     * off. */
    if (canvas.w != v->view.width || canvas.h != v->view.height) {
        if (v->document && v->fit) {
            if (!tiku_circuitview_fit(v->document, canvas.w, canvas.h,
                                      &v->view)) {
                v->view.width = canvas.w;
                v->view.height = canvas.h;
            }
        } else {
            v->view.width = canvas.w;
            v->view.height = canvas.h;
        }
        tiku_circuitview_changed(v);
    }
    v->canvas = canvas;
}

int tiku_circuitview_list_width(const tiku_circuitview_t *v,
                                tiku_rect_t content, tiku_rect_t canvas)
{
    if (!v->show_sheets || content.w < CV_LIST_MIN_W ||
        canvas.h < CV_LIST_MIN_H) {
        return 0;
    }
    int nw = content.w / CV_LIST_SHARE;
    return nw > CV_LIST_MAX_W ? CV_LIST_MAX_W : nw;
}

tiku_rect_t tiku_circuitview_next_canvas(const tiku_circuitview_t *v,
                                         tiku_rect_t content, int *list)
{
    tiku_rect_t canvas = v->area;

    (void)tiku_circuitview_details_take(v, &canvas);
    int nw = tiku_circuitview_list_width(v, content, canvas);
    *list = nw ? nw + CV_LIST_GAP : 0;
    return canvas;
}

/** @brief Draw the last image rendered, stretched to the view while one
 *         for the view is rendered, or when the canvas is larger than the
 *         kit renders at once; the selection frame uses the same map. */
static void preview(tiku_circuitview_t *v, tiku_surface_t *s)
{
    tiku_rect_t clip = s->clip;

    tiku_clip_set(s, cv_intersect(clip, v->canvas));
    if (v->rendered_generation == v->generation &&
        v->image.w == v->canvas.w && v->image.h == v->canvas.h) {
        tiku_picture(s, v->canvas.x, v->canvas.y, v->image.w, v->image.h,
                     v->image.px);
    } else {
        double scale = v->view.scale / v->cached_view.scale;
        double x = v->canvas.x + v->view.x - v->cached_view.x * scale;
        double y = v->canvas.y + v->view.y - v->cached_view.y * scale;
        double w = v->image.w * scale, h = v->image.h * scale;
        if (isfinite(x) && isfinite(y) && fabs(x) < CV_PREVIEW_LIMIT &&
            fabs(y) < CV_PREVIEW_LIMIT && w >= 1 && h >= 1 &&
            w < CV_PREVIEW_LIMIT && h < CV_PREVIEW_LIMIT) {
            tiku_surface_t src = {.px = v->image.px, .w = v->image.w,
                                  .h = v->image.h,
                                  .clip = {0, 0, v->image.w, v->image.h}};
            tiku_blend_scaled(s, (tiku_rect_t){(int)lround(x),
                                               (int)lround(y),
                                               (int)lround(w),
                                               (int)lround(h)},
                              &src, 1);
        }
    }
    tiku_clip_set(s, clip);
}

void tiku_circuitview_draw(tiku_window_t *window, tiku_surface_t *s,
                           tiku_rect_t r, void *ctx)
{
    tiku_circuitview_t *v = ctx;
    tiku_rect_t clip = s->clip;

    (void)window;
    tiku_clip_set(s, cv_intersect(clip, r));
    tiku_ui_panel(s, r);
    tiku_circuitview_layout(v, r);
    const char *const short_names[] = {"", "Example", "Fit", "-", "+",
        "Cancel", "Close", "Clear", "Open", "Reload", "Find", "Next"};
    for (int i = 0; i < v->dialog.count; i++) {
        const tiku_dialog_control_t *d = &v->dialog.control[i];
        if (d->id == CIRCUIT_SHEET_LIST || d->id == CIRCUIT_INSPECTOR ||
            d->id == CIRCUIT_COPY) {
            continue;
        }
        unsigned flags = d->enabled ? 0 : TIKU_S_DISABLED;
        if (d->id == v->hover) {
            flags |= TIKU_S_HOT;
        }
        if (tiku_dialog_pressed(&v->dialog, d->id)) {
            flags |= TIKU_S_PRESSED;
        }
        if (tiku_dialog_focused(&v->dialog, d->id)) {
            flags |= TIKU_S_FOCUS;
        }
        if (d->id == CIRCUIT_FIND) {
            int focus = tiku_dialog_focused(&v->dialog, CIRCUIT_FIND);
            tiku_ui_textfield_scroll(s, d->rect, v->query.text,
                                     focus ? v->query.caret : -1,
                                     focus ? v->query.sel_a : 0,
                                     focus ? v->query.sel_b : 0, flags,
                                     v->query_input.scroll);
            tiku_text_line_draw(s, (tiku_rect_t){r.x + CV_EDGE, d->rect.y,
                                                 CV_FIND_LABEL_W, d->rect.h},
                                TIKU_MEASURE_PLAIN, "Find:", TIKU_C_TEXT);
            if (!v->query.text[0] && !focus) {
                tiku_text_line_draw(s, tiku_inset(d->rect, CV_HINT_INSET),
                    TIKU_MEASURE_PLAIN,
                    "Reference, value, label, tool or aperture",
                    tiku_ink(TIKU_INK_FAINT));
            }
        } else if (d->id == CIRCUIT_OPEN) {
            tiku_ui_button(s, d->rect, "Open", flags | TIKU_S_ICON);
            tiku_icon_paint(s, "folder", d->rect.x + CV_ICON_X,
                            d->rect.y + (d->rect.h - CV_ICON) / 2, CV_ICON);
            tiku_text_line_draw(s, (tiku_rect_t){d->rect.x + CV_ICON_TEXT,
                                    d->rect.y,
                                    d->rect.w - CV_ICON_TEXT -
                                        CV_ICON_TEXT_PAD,
                                    d->rect.h},
                                TIKU_MEASURE_PLAIN, "Open", TIKU_C_TEXT);
        } else {
            tiku_ui_button(s, d->rect, short_names[d->id], flags);
        }
    }
    tiku_circuitview_sheets_draw(v, s);
    tiku_circuitview_details_draw(v, s);
    tiku_fill(s, v->canvas, CV_PAPER);
    if (v->document && v->image.px) {
        preview(v, s);
    } else {
        tiku_rect_t note = tiku_inset(v->canvas, CV_NOTE_INSET);
        char unavailable[CV_NOTE_MAX] = "";
        if (v->project && !v->document) {
            const tiku_circuit_sheet_t *sheet =
                tiku_circuit_project_sheet(v->project, v->sheet);
            snprintf(unavailable, sizeof unavailable,
                     "Sheet unavailable\n\n%.255s\n\n%s\n\nNo drawing is "
                     "shown for this sheet. Correct the source or link and "
                     "choose Reload.", sheet->path,
                     sheet->diagnostic->message);
        }
        tiku_text_block_draw(s, note, TIKU_MEASURE_PLAIN, v->document ?
            (v->busy || v->needs_render ? "Rendering the drawing..." :
                                          v->status) :
            v->project ? unavailable :
            TIKU_PRODUCT_CIRCUIT_TIKUCIRCUIT_N_NOPEN_A_KICAD_KICAD_SCH_OR_EAGLE_XML_SCH
            "schematic, Gerber layer (.gbr), drill file (.drl, .xnc, .xln), "
            "Gerber job (.gbrjob), or choose File > Open Example.\n\n"
            "This read-only preview uses embedded symbol libraries and "
            "supports decimal-coordinate drills. Sheets are listed "
            "separately. Use Layers > Add Layer to combine artwork and "
            "drills. Unsupported drawing features are reported without "
            "replacing the current view.", TIKU_C_TEXT, CV_NOTE_GAP);
    }
    tiku_frame(s, v->canvas, tiku_tint(TIKU_C_PANEL, TIKU_DARKEN_4));
    char line[CV_LINE_MAX];
    const tiku_circuit_object_t *object =
        tiku_circuit_object(v->document, v->selected);
    if (object) {
        snprintf(line, sizeof line, "%s  -  %s", object->name,
                 object->value);
        /* The selection is drawn over the image; the scene is unchanged. */
        tiku_circuit_box_t b = object->bounds;
        double x = b.x0 * v->view.scale + v->view.x + v->canvas.x;
        double y = b.y0 * v->view.scale + v->view.y + v->canvas.y;
        double w = (b.x1 - b.x0) * v->view.scale;
        double h = (b.y1 - b.y0) * v->view.scale;
        tiku_rect_t saved = s->clip;
        tiku_clip_set(s, cv_intersect(saved, v->canvas));
        if (fabs(x) < CV_FRAME_LIMIT && fabs(y) < CV_FRAME_LIMIT &&
            w < CV_FRAME_LIMIT && h < CV_FRAME_LIMIT) {
            tiku_frame(s, (tiku_rect_t){(int)x - CV_FRAME_OUT,
                                        (int)y - CV_FRAME_OUT,
                                        (int)w + 2 * CV_FRAME_OUT + 1,
                                        (int)h + 2 * CV_FRAME_OUT + 1},
                       CV_SELECTION);
        }
        tiku_clip_set(s, saved);
    } else if (v->project) {
        const tiku_circuit_sheet_t *sheet =
            tiku_circuit_project_sheet(v->project, v->sheet);
        snprintf(line, sizeof line, "%zu/%zu  %.120s  |  %.210s",
                 v->sheet + 1, tiku_circuit_project_count(v->project),
                 sheet->name, sheet->path);
    } else if (v->document) {
        snprintf(line, sizeof line, "%.255s  |  %.1f pixels/mm",
                 v->path[0] ? (strrchr(v->path, '/') ?
                               strrchr(v->path, '/') + 1 : v->path) :
                              "Example circuit",
                 v->view.scale);
    } else {
        snprintf(line, sizeof line,
                 "Read-only circuit viewer - development preview");
    }
    cv_measure_draw(v, s, v->canvas, &v->view);
    if (v->measure) cv_measure_text(v, line, sizeof line);
    tiku_rect_t row = v->details;
    row.h = tiku_font_plain()->height + CV_STATUS_PAD;
    tiku_text_line_draw(s, row, TIKU_MEASURE_PLAIN, line, TIKU_C_TEXT);
    row.y += row.h;
    tiku_text_line_draw(s, row, TIKU_MEASURE_PLAIN,
        tiku_circuit_project_incomplete(v->project) ?
        "Incomplete preview: ! marks unavailable sheets. Font and theme "
        "are substituted." : tiku_circuit_warning(v->document),
        CV_WARNING);
    row.y += row.h;
    tiku_text_line_draw(s, row, TIKU_MEASURE_PLAIN,
                        v->loading || v->needs_load ? "Opening drawing..." :
                        v->busy ? "Rendering..." : v->status, TIKU_C_TEXT);
    cv_style_draw(v, s);
    tiku_clip_set(s, clip);
}

int tiku_circuitview_event(tiku_window_t *window, const tiku_event_t *e,
                           void *ctx)
{
    tiku_circuitview_t *v = ctx;

    tiku_circuitview_layout(v, tiku_window_content(window));
    if (e->type == TIKU_EVENT_DEACTIVATED) {
        v->dragging = 0;
    }
    int key = e->type == TIKU_EVENT_KEY_DOWN;
    if (key && (e->modifiers & TIKU_MOD_CMD)) {
        int cmd = e->key == 'o' ? CIRCUIT_OPEN :
                  e->key == 'r' ? CIRCUIT_RELOAD :
                  e->key == 'f' ? CIRCUIT_FIND :
                  e->key == 'g' ? CIRCUIT_NEXT :
                  e->key == '[' ? CIRCUIT_PREV_SHEET :
                  e->key == ']' ? CIRCUIT_NEXT_SHEET :
                  e->key == 'w' || e->key == 'q' ? CIRCUIT_CLOSE : 0;
        if (cmd) {
            tiku_circuitview_action(v, cmd);
            return 1;
        }
    }
    if (cv_style_event(v, e)) return 1;
    if (tiku_circuitview_sheets_event(v, e)) {
        return 1;
    }
    if (tiku_circuitview_details_event(v, e)) {
        return 1;
    }
    if (e->type == TIKU_EVENT_POINTER_DOWN && e->button == 1 &&
        cv_inside(v->find_rect, e->x, e->y)) {
        tiku_dialog_focus(&v->dialog, CIRCUIT_FIND);
    }
    if ((v->dialog.focus == CIRCUIT_FIND || v->query_input.dragging) &&
        !(key && (e->key == TIKU_KEY_TAB || e->key == TIKU_KEY_ESCAPE))) {
        char old[TIKU_FIELD_MAX];
        snprintf(old, sizeof old, "%s", v->query.text);
        tiku_field_result_t result = tiku_field_event(&v->query,
            &v->query_input, e, v->find_rect,
            v->dialog.focus == CIRCUIT_FIND);
        if (strcmp(old, v->query.text)) {
            v->selected = SIZE_MAX;
            tiku_circuitview_menus(v);
        }
        if (result == TIKU_FIELD_COMMITTED) {
            tiku_circuitview_action(v, CIRCUIT_NEXT);
        }
        if (result == TIKU_FIELD_REJECTED) {
            tiku_circuitview_note(v,
                "Search text is invalid or too long.");
        }
        if (result != TIKU_FIELD_IGNORED) {
            if (v->dirty) {
                *v->dirty = 1;
            }
            return 1;
        }
        if (key || e->type == TIKU_EVENT_TEXT_INPUT) {
            return 1;
        }
    }
    if (cv_measure_event(v, e)) return 1;
    if (e->type == TIKU_EVENT_KEY_DOWN) {
        int cmd = 0;
        if ((e->modifiers & TIKU_MOD_CMD) && e->key == 'c') {
            cmd = CIRCUIT_COPY;
        } else if ((e->modifiers & TIKU_MOD_CMD) &&
                   (e->key == 'w' || e->key == 'q')) {
            cmd = CIRCUIT_CLOSE;
        } else if (e->key == '+' || e->key == '=') {
            cmd = CIRCUIT_IN;
        } else if (e->key == '-') {
            cmd = CIRCUIT_OUT;
        } else if (e->key == '0' || e->key == 'f') {
            cmd = CIRCUIT_FIT;
        } else if (e->key == 'm') {
            cmd = CIRCUIT_MEASURE;
        } else if (e->key == TIKU_KEY_ESCAPE) {
            cmd = v->measure ? CIRCUIT_MEASURE_CLEAR : CIRCUIT_CANCEL;
        }
        if (cmd) {
            tiku_circuitview_action(v, cmd);
            return 1;
        }
        if (v->document && e->key >= TIKU_KEY_UP &&
            e->key <= TIKU_KEY_RIGHT) {
            v->view.x += e->key == TIKU_KEY_LEFT ? CV_PAN_STEP :
                         e->key == TIKU_KEY_RIGHT ? -CV_PAN_STEP : 0;
            v->view.y += e->key == TIKU_KEY_UP ? CV_PAN_STEP :
                         e->key == TIKU_KEY_DOWN ? -CV_PAN_STEP : 0;
            v->view.x = fmax(-CV_PAN_LIMIT, fmin(CV_PAN_LIMIT, v->view.x));
            v->view.y = fmax(-CV_PAN_LIMIT, fmin(CV_PAN_LIMIT, v->view.y));
            v->fit = 0;
            tiku_circuitview_changed(v);
            return 1;
        }
    }
    if (e->type == TIKU_EVENT_WHEEL && v->document &&
        cv_inside(v->canvas, e->x, e->y)) {
        if (e->dy && tiku_circuit_zoom(&v->view,
                                       e->dy < 0 ? CV_ZOOM_IN : CV_ZOOM_OUT,
                                       e->x - v->canvas.x,
                                       e->y - v->canvas.y)) {
            v->fit = 0;
            tiku_circuitview_changed(v);
            tiku_circuitview_menus(v);
        }
        return 1;
    }
    if (e->type == TIKU_EVENT_POINTER_DOWN && e->button == 1 &&
        v->document && cv_inside(v->canvas, e->x, e->y)) {
        v->dialog.focus = -1;
        v->dragging = 1;
        v->dragged = 0;
        v->drag_x = e->x;
        v->drag_y = e->y;
        return 1;
    }
    if (e->type == TIKU_EVENT_POINTER_MOVE && v->dragging) {
        int dx = e->x - v->drag_x, dy = e->y - v->drag_y;
        if (abs(dx) + abs(dy) > CV_DRAG_SLOP || v->dragged) {
            v->view.x = fmax(-CV_PAN_LIMIT,
                             fmin(CV_PAN_LIMIT, v->view.x + dx));
            v->view.y = fmax(-CV_PAN_LIMIT,
                             fmin(CV_PAN_LIMIT, v->view.y + dy));
            v->drag_x = e->x;
            v->drag_y = e->y;
            v->dragged = 1;
            v->fit = 0;
            tiku_circuitview_changed(v);
        }
        return 1;
    }
    if (e->type == TIKU_EVENT_POINTER_UP && v->dragging) {
        v->dragging = 0;
        double x, y;
        if (!v->dragged && cv_inside(v->canvas, e->x, e->y) &&
            tiku_circuit_world(&v->view, e->x - v->canvas.x,
                               e->y - v->canvas.y, &x, &y)) {
            v->selected = tiku_circuit_hit(v->document, x, y,
                                           CV_HIT_SLOP / v->view.scale);
            size_t layer = tiku_circuit_object_layer(v->document,
                                                     v->selected);
            if (layer != SIZE_MAX) {
                v->sheet = layer;
                tiku_list_select_only(&v->sheets, (int)layer);
                tiku_list_reveal(&v->sheets, v->sheet_rows);
                tiku_circuitview_ready(v);
            }
        }
        if (v->dirty) {
            *v->dirty = 1;
        }
        tiku_circuitview_menus(v);
        return 1;
    }
    if (e->type == TIKU_EVENT_POINTER_MOVE) {
        int hover = 0;
        for (int i = 0; i < v->dialog.count; i++) {
            if (cv_inside(v->dialog.control[i].rect, e->x, e->y)) {
                hover = v->dialog.control[i].id;
            }
        }
        if (v->hover != hover) {
            v->hover = hover;
            if (v->dirty) {
                *v->dirty = 1;
            }
        }
    }
    tiku_dialog_result_t result = tiku_dialog_event(&v->dialog, e);
    if (result.activated >= 0) {
        tiku_circuitview_action(v, result.activated);
    }
    if (result.taken && v->dirty) {
        *v->dirty = 1;
    }
    return result.taken;
}
