/* tiku_circuit_sheets.c -- the list beside the canvas: a schematic's
 * sheet instances or a stack's layers, the sheet shown, and Find Next
 * across every loaded sheet.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#include "tiku_circuit_window_private.h"
#include "tiku_font.h"
#include "tiku_text_layout_paint.h"
#include "tiku_ui.h"
#include <stdio.h>
#include <string.h>

#define CV_ROW_LABEL      384   /* a row's text, with its end */
#define CV_LIST_TITLE_IN  7     /* the list's title from its corner */
#define CV_LIST_TITLE_PAD 8     /* added to the font for the title row */
#define CV_SWATCH         6     /* a layer row's colour square */
#define CV_SWATCH_RIGHT   10    /* the square's left, from the row's right */
#define CV_SWATCH_TOP     5     /* and its top, from the row's top */
#define CV_ALPHA          0xff000000u /* a colour's opaque alpha byte */
#define CV_INDENT_LEVELS  6     /* sheet levels the list indents */
#define CV_INDENT         2     /* spaces for each of them */
/* A layer row's visibility box, at its left. */
#define CV_CHECK_W        26

void tiku_circuitview_clear_project(tiku_circuitview_t *v)
{
    tiku_circuit_project_free(v->project);
    v->project = NULL;
    v->sheet = 0;
    tiku_list_set_count(&v->sheets, 0);
    tiku_scroll_init(&v->sheet_scroll, 1);
}

void tiku_circuitview_sheet(tiku_circuitview_t *v, size_t index)
{
    if (!v || !v->window || index >= tiku_circuitview_rows(v)) {
        return;
    }
    if (!v->project) {
        v->sheet = index;
        v->selected = SIZE_MAX;
        tiku_list_select_only(&v->sheets, (int)index);
        tiku_list_reveal(&v->sheets, v->sheet_rows);
        tiku_circuitview_ready(v);
        tiku_circuitview_menus(v);
        return;
    }
    tiku_circuit_t *doc = tiku_circuit_project_scene(v->project, index);
    tiku_circuit_release(v->document);
    v->document = tiku_circuit_retain(doc);
    cv_measure_reset(v);
    tiku_image_free(&v->image);
    v->sheet = index;
    v->selected = SIZE_MAX;
    v->fit = 1;
    v->dragging = 0;
    v->view = (tiku_circuit_view_t){v->canvas.w, v->canvas.h, 1, 0, 0};
    if (doc) {
        (void)tiku_circuitview_fit(doc, v->canvas.w, v->canvas.h, &v->view);
    }
    tiku_list_select_only(&v->sheets, (int)index);
    tiku_list_reveal(&v->sheets, v->sheet_rows);
    tiku_circuitview_changed(v);
    tiku_circuitview_ready(v);
    tiku_circuitview_menus(v);
}

void tiku_circuitview_search(tiku_circuitview_t *v)
{
    size_t count = tiku_circuit_project_count(v->project);

    if (!count) {
        count = 1;
    }
    size_t start = v->selected == SIZE_MAX ? 0 : v->selected + 1;
    size_t found = SIZE_MAX, index = v->sheet;
    for (size_t i = 0; i <= count; i++) {
        if (i == count && !start) {
            break;
        }
        index = (v->sheet + i) % count;
        tiku_circuit_t *d = v->project ?
            tiku_circuit_project_scene(v->project, index) : v->document;
        found = tiku_circuit_find(d, v->query.text, i ? 0 : start);
        if (found != SIZE_MAX) {
            break;
        }
    }
    if (found == SIZE_MAX) {
        tiku_circuitview_note(v, !*v->query.text ?
            "Enter a reference, value, label, tool or aperture, then "
            "press Enter." :
            tiku_circuit_project_incomplete(v->project) ?
            "No match in loaded sheets. Unavailable sheets could not be "
            "searched." :
            v->project ? "No matching object in any sheet." :
            tiku_circuit_layer_count(v->document) ?
            "No match in visible layers. Hidden and unavailable layers are "
            "not searched." :
            "No matching object in this drawing.");
    } else {
        if (v->project && index != v->sheet) {
            tiku_circuitview_sheet(v, index);
        }
        v->selected = found;
        v->fit = 0;
        if (!v->project && tiku_circuit_layer_count(v->document)) {
            v->sheet = tiku_circuit_object_layer(v->document, found);
            tiku_list_select_only(&v->sheets, (int)v->sheet);
            tiku_list_reveal(&v->sheets, v->sheet_rows);
        }
        tiku_circuit_box_t b = tiku_circuit_object(v->document, found)->bounds;
        v->view.x = v->canvas.w / 2.0 - (b.x0 + b.x1) * v->view.scale / 2;
        v->view.y = v->canvas.h / 2.0 - (b.y0 + b.y1) * v->view.scale / 2;
        tiku_circuitview_changed(v);
        tiku_circuitview_note(v,
            tiku_circuit_project_incomplete(v->project) ?
            "Match selected. Find Next searches loaded sheets only; "
            "project is incomplete." :
            v->project ?
            "Match selected. Enter or Find Next searches all sheets and "
            "wraps." :
            "Match selected. Enter or Find Next searches this drawing and "
            "wraps.");
    }
    tiku_circuitview_menus(v);
}

void tiku_circuitview_sheets_draw(tiku_circuitview_t *v, tiku_surface_t *s)
{
    if (!v->navigator.w) {
        return;
    }
    tiku_fill(s, v->navigator, TIKU_C_PANEL);
    tiku_rect_t title = tiku_inset(v->navigator, CV_LIST_TITLE_IN);
    title.h = tiku_font_plain()->height + CV_LIST_TITLE_PAD;
    char label[CV_ROW_LABEL];
    snprintf(label, sizeof label, "%s (%zu)",
             v->project ? "Sheets" : "Layers", tiku_circuitview_rows(v));
    tiku_text_line_draw(s, title, TIKU_MEASURE_PLAIN, label, TIKU_C_TEXT);
    /* The rows clip to the caller's clip as well, which a small window
     * makes smaller than the list. */
    tiku_rect_t old = s->clip, r = v->sheet_rows;
    tiku_clip_set(s, cv_intersect(r, old));
    tiku_fill(s, r, TIKU_C_DOC);
    int end = v->sheets.top + tiku_list_visible_rows(&v->sheets, r) + 1;
    for (int i = v->sheets.top; i < v->sheets.count && i < end; i++) {
        if (!v->project) {
            const tiku_circuit_layer_t *l =
                tiku_circuit_layer(v->document, (size_t)i);
            snprintf(label, sizeof label, "%s %.220s",
                     !l->scene ? "!" : l->visible ? "[x]" : "[ ]",
                     l->label);
            tiku_rect_t row = tiku_list_row_rect(&v->sheets, r, i);
            tiku_ui_list_row(s, row, label, (size_t)i == v->sheet);
            tiku_fill(s, (tiku_rect_t){row.x + row.w - CV_SWATCH_RIGHT,
                                       row.y + CV_SWATCH_TOP, CV_SWATCH,
                                       CV_SWATCH},
                      l->colour | CV_ALPHA);
            continue;
        }
        const tiku_circuit_sheet_t *sheet =
            tiku_circuit_project_sheet(v->project, (size_t)i);
        int indent = (int)(sheet->depth < CV_INDENT_LEVELS ?
                           sheet->depth : CV_INDENT_LEVELS) * CV_INDENT;
        snprintf(label, sizeof label, "%*s%s%s%s%s", indent, "",
                 sheet->diagnostic->code ? "! " : "", sheet->page,
                 *sheet->page ? "  " : "", sheet->name);
        tiku_ui_list_row(s, tiku_list_row_rect(&v->sheets, r, i), label,
                         (size_t)i == v->sheet);
    }
    tiku_clip_set(s, old);
    if (v->sheet_bar.w) {
        int track = v->sheet_bar.h - 2 * v->sheet_bar.w;
        int pos = 0;
        int thick = track;
        tiku_scroll_thumb(&v->sheet_scroll, track, &pos, &thick);
        float fraction = track > 0 ? (float)thick / track : 1;
        float position = track > thick ? (float)pos / (track - thick) : 0;
        tiku_ui_scrollbar_held(s, v->sheet_bar, position, fraction, 0,
                               v->sheet_scroll.arrow_dir);
    }
    tiku_frame(s, v->navigator, tiku_tint(TIKU_C_PANEL, TIKU_DARKEN_4));
    if (tiku_dialog_focused(&v->dialog, CIRCUIT_SHEET_LIST)) {
        tiku_frame(s, tiku_inset(v->navigator, CV_FOCUS_INSET),
                   TIKU_C_SELECT);
    }
}

/** @brief The scrollbar moved: scroll the rows with it and redraw. */
static void scrolled(tiku_circuitview_t *v)
{
    tiku_list_scroll_to(&v->sheets, v->sheet_scroll.value, v->sheet_rows);
    if (v->dirty) {
        *v->dirty = 1;
    }
}

int tiku_circuitview_sheets_event(tiku_circuitview_t *v,
                                  const tiku_event_t *e)
{
    if (e->type == TIKU_EVENT_DEACTIVATED) {
        tiku_scroll_release(&v->sheet_scroll);
    }
    if (!v->navigator.w) {
        return 0;
    }
    tiku_scroll_t *bar = &v->sheet_scroll;
    int at = e->y - v->sheet_bar.y, arrow = v->sheet_bar.w;
    int track = v->sheet_bar.h - 2 * arrow;
    if (e->type == TIKU_EVENT_POINTER_UP && bar->tracking) {
        tiku_scroll_release(bar);
        scrolled(v);
        return 1;
    }
    if (e->type == TIKU_EVENT_POINTER_MOVE && bar->tracking) {
        if (!bar->arrow_dir) {
            tiku_scroll_drag(bar, track, at - arrow);
        }
        scrolled(v);
        return 1;
    }
    if (e->type == TIKU_EVENT_POINTER_DOWN && e->button == 1 &&
        cv_inside(v->sheet_bar, e->x, e->y)) {
        tiku_dialog_focus(&v->dialog, CIRCUIT_SHEET_LIST);
        tiku_scroll_to(bar, v->sheets.top);
        if (at < arrow || at >= v->sheet_bar.h - arrow) {
            int direction = at < arrow ? -1 : 1;
            tiku_scroll_arrow_press(bar, direction);
            bar->tracking = 1;
            tiku_scroll_to(bar, bar->value + direction);
        } else {
            tiku_scroll_press(bar, track, at - arrow);
        }
        scrolled(v);
        return 1;
    }
    if (e->type == TIKU_EVENT_WHEEL && cv_inside(v->navigator, e->x, e->y)) {
        tiku_scroll_to(bar, v->sheets.top + (e->dy > 0 ? CV_WHEEL_ROWS :
                                             e->dy < 0 ? -CV_WHEEL_ROWS : 0));
        scrolled(v);
        return 1;
    }
    if (e->type == TIKU_EVENT_POINTER_DOWN && e->button == 1 &&
        cv_inside(v->sheet_rows, e->x, e->y)) {
        tiku_dialog_focus(&v->dialog, CIRCUIT_SHEET_LIST);
        int row = tiku_list_at(&v->sheets, v->sheet_rows, e->x, e->y);
        if (row >= 0) {
            tiku_circuitview_sheet(v, (size_t)row);
        }
        if (row >= 0 && !v->project && e->x < v->sheet_rows.x + CV_CHECK_W) {
            tiku_circuitview_action(v, CIRCUIT_LAYER_VISIBLE);
        }
        return 1;
    }
    if (e->type == TIKU_EVENT_KEY_DOWN &&
        tiku_dialog_focused(&v->dialog, CIRCUIT_SHEET_LIST) &&
        tiku_list_key(&v->sheets, e->key, 0, v->sheet_rows)) {
        int row = tiku_list_chosen(&v->sheets);
        if (row >= 0) {
            tiku_circuitview_sheet(v, (size_t)row);
        }
        return 1;
    }
    return 0;
}

void tiku_circuitview_sheets_tick(tiku_circuitview_t *v)
{
    int direction = tiku_scroll_arrow_repeat(&v->sheet_scroll);

    if (direction) {
        tiku_scroll_to(&v->sheet_scroll, v->sheet_scroll.value + direction);
        scrolled(v);
    }
}
