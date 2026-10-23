/* tiku_circuit_details.c -- Selection Details: the selected object's
 * description, wrapped and scrolled in a panel under the canvas, and
 * copied whole to the clipboard.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#include "tiku_circuit_window_private.h"
#include "tiku_clipboard.h"
#include "tiku_font.h"
#include "tiku_text_layout_paint.h"
#include "tiku_ui.h"
#include <stdio.h>
#include <string.h>

#define CV_PANEL_MIN_W    340   /* the least canvas the panel shows on */
#define CV_PANEL_MIN_H    240
#define CV_PANEL_LEADING  4     /* added to the font for a line of text */
#define CV_PANEL_HEAD     18    /* the title row's padding */
#define CV_PANEL_TEXT_MIN 44    /* the least room for the text */
#define CV_PANEL_SHARE    3     /* the panel takes a third of the canvas */
#define CV_PANEL_MAX_H    170   /* and at most this */
#define CV_PANEL_KEEP     120   /* the least canvas it leaves above it */
#define CV_PANEL_GAP      6     /* between the canvas and the panel */
#define CV_PANEL_INSET    7     /* the text from the panel's edge */
#define CV_PANEL_GUTTER   18    /* room kept beside the text for the bar */
#define CV_PANEL_BAR_X    17    /* the bar's left, from the panel's right */
#define CV_COPY_LEAST     20    /* Copy Details' least width */
#define CV_COPY_RIGHT     5     /* the button from the panel's right edge */
#define CV_COPY_TOP       4     /* and from its top */
#define CV_TITLE_X        8     /* the title from the panel's corner */
#define CV_TITLE_Y        4
#define CV_TITLE_ROOM     160   /* the width the title leaves the button */

/** @brief Write the panel's text afresh from the scene, so a reload or a
 *         clear leaves nothing pointing into a scene let go. */
static void refresh(tiku_circuitview_t *v)
{
    char text[sizeof v->inspect_text];
    const tiku_circuit_object_t *o =
        tiku_circuit_object(v->document, v->selected);
    const char *details = tiku_circuit_details(v->document, v->selected);
    const tiku_circuit_layer_t *layer =
        tiku_circuit_layer(v->document, v->sheet);

    if (!o && layer) {
        snprintf(text, sizeof text, "Layer: %.255s\nPath: %.1600s\n%s\n%s",
                 layer->label, layer->path, layer->attributes,
                 layer->scene ? "Click an object to inspect geometry." :
                                layer->diagnostic.message);
    } else if (!o) {
        snprintf(text, sizeof text, "Click an object to inspect it. Find "
                 "accepts references, values, labels, drill tools and "
                 "apertures.");
    } else if (*details) {
        snprintf(text, sizeof text, "%s", details);
    } else {
        snprintf(text, sizeof text, "Name: %s\nValue: %s\nPainted bounds "
                 "(mm): %.4g, %.4g to %.4g, %.4g", o->name,
                 *o->value ? o->value : "Not specified", o->bounds.x0,
                 o->bounds.y0, o->bounds.x1, o->bounds.y1);
    }
    if (strcmp(text, v->inspect_text)) {
        snprintf(v->inspect_text, sizeof v->inspect_text, "%s", text);
        tiku_scroll_release(&v->inspect_scroll);
        tiku_scroll_to(&v->inspect_scroll, 0);
    }
}

int tiku_circuitview_details_take(const tiku_circuitview_t *v,
                                  tiku_rect_t *canvas)
{
    if (!v->show_details || canvas->h < CV_PANEL_MIN_H ||
        canvas->w < CV_PANEL_MIN_W) {
        return 0;
    }
    int bh = tiku_ui_button_h();
    int line_h = tiku_font_plain()->height + CV_PANEL_LEADING;
    int minimum = bh + CV_PANEL_HEAD + (2 * line_h > CV_PANEL_TEXT_MIN ?
                                        2 * line_h : CV_PANEL_TEXT_MIN);
    int h = canvas->h / CV_PANEL_SHARE;
    if (h > CV_PANEL_MAX_H) {
        h = CV_PANEL_MAX_H;
    }
    if (h < minimum) {
        h = minimum;
    }
    if (canvas->h - h < CV_PANEL_KEEP) {
        return 0;
    }
    canvas->h -= h + CV_PANEL_GAP;
    return h;
}

void tiku_circuitview_details_layout(tiku_circuitview_t *v,
                                     tiku_rect_t *canvas)
{
    tiku_rect_t whole = *canvas;

    refresh(v);
    v->inspector = v->inspect_rows = v->inspect_bar = (tiku_rect_t){0};
    int h = v->document ? tiku_circuitview_details_take(v, canvas) : 0;
    if (!h) {
        tiku_scroll_release(&v->inspect_scroll);
        return;
    }
    int bh = tiku_ui_button_h();
    v->inspector = (tiku_rect_t){whole.x, whole.y + whole.h - h, whole.w, h};
    int bw = tiku_ui_button_w("Copy Details", CV_COPY_LEAST);
    tiku_rect_t button = {v->inspector.x + v->inspector.w - bw -
                              CV_COPY_RIGHT,
                          v->inspector.y + CV_COPY_TOP, bw, bh};
    v->inspect_rows = tiku_inset(v->inspector, CV_PANEL_INSET);
    v->inspect_rows.y += bh + CV_COPY_TOP;
    v->inspect_rows.h -= bh + CV_COPY_TOP;
    /* The text is measured with the bar's room kept, so the bar coming or
     * going does not move where the lines wrap. */
    v->inspect_rows.w -= CV_PANEL_GUTTER;
    const char *p = v->inspect_text;
    char line[TIKU_TEXT_LINE_MAX];
    int total = 0;
    while (tiku_text_wrap_next(TIKU_MEASURE_PLAIN, &p, v->inspect_rows.w,
                               line, sizeof line, 0)) {
        total++;
    }
    int visible = v->inspect_rows.h /
                  (tiku_font_plain()->height + CV_PANEL_LEADING);
    tiku_scroll_set_range(&v->inspect_scroll, total,
                          visible > 0 ? visible : 1);
    if (total > visible) {
        v->inspect_bar = (tiku_rect_t){v->inspector.x + v->inspector.w -
                                           CV_PANEL_BAR_X,
                                       v->inspect_rows.y, CV_BAR,
                                       v->inspect_rows.h};
    } else {
        tiku_scroll_release(&v->inspect_scroll);
    }
    tiku_dialog_add(&v->dialog, CIRCUIT_INSPECTOR, v->inspect_rows,
                    TIKU_DIALOG_INPUT, 1);
    tiku_dialog_add(&v->dialog, CIRCUIT_COPY, button, TIKU_DIALOG_BUTTON,
                    tiku_circuitview_enabled(v, CIRCUIT_COPY));
}

void tiku_circuitview_details_draw(tiku_circuitview_t *v, tiku_surface_t *s)
{
    if (!v->inspector.w) {
        return;
    }
    tiku_fill(s, v->inspector, TIKU_C_DOC);
    tiku_frame(s, v->inspector, tiku_tint(TIKU_C_PANEL, TIKU_DARKEN_4));
    tiku_rect_t title = {v->inspector.x + CV_TITLE_X,
                         v->inspector.y + CV_TITLE_Y,
                         v->inspector.w - CV_TITLE_ROOM, tiku_ui_button_h()};
    tiku_text_line_draw(s, title, TIKU_MEASURE_BOLD, "Selection Details",
                        TIKU_C_TEXT);
    for (int i = 0; i < v->dialog.count; i++) {
        const tiku_dialog_control_t *c = &v->dialog.control[i];
        if (c->id != CIRCUIT_COPY) {
            continue;
        }
        unsigned flags = c->enabled ? 0 : TIKU_S_DISABLED;
        if (v->hover == CIRCUIT_COPY) {
            flags |= TIKU_S_HOT;
        }
        if (tiku_dialog_pressed(&v->dialog, CIRCUIT_COPY)) {
            flags |= TIKU_S_PRESSED;
        }
        if (tiku_dialog_focused(&v->dialog, CIRCUIT_COPY)) {
            flags |= TIKU_S_FOCUS;
        }
        tiku_ui_button(s, c->rect, "Copy Details", flags);
    }
    const char *p = v->inspect_text;
    char line[TIKU_TEXT_LINE_MAX];
    int at = 0;
    tiku_rect_t row = v->inspect_rows;
    row.h = tiku_font_plain()->height + CV_PANEL_LEADING;
    while (tiku_text_wrap_next(TIKU_MEASURE_PLAIN, &p, row.w, line,
                               sizeof line, 0)) {
        if (at++ < v->inspect_scroll.value) {
            continue;
        }
        if (row.y + row.h > v->inspect_rows.y + v->inspect_rows.h) {
            break;
        }
        tiku_text_line_draw(s, row, TIKU_MEASURE_PLAIN, line, TIKU_C_TEXT);
        row.y += row.h;
    }
    if (v->inspect_bar.w) {
        int track = v->inspect_bar.h - 2 * v->inspect_bar.w;
        int pos = 0;
        int thick = track;
        tiku_scroll_thumb(&v->inspect_scroll, track, &pos, &thick);
        tiku_ui_scrollbar_held(s, v->inspect_bar,
                               track > thick ?
                                   (float)pos / (track - thick) : 0,
                               track > 0 ? (float)thick / track : 1, 0,
                               v->inspect_scroll.arrow_dir);
    }
    if (tiku_dialog_focused(&v->dialog, CIRCUIT_INSPECTOR)) {
        tiku_frame(s, tiku_inset(v->inspector, CV_FOCUS_INSET),
                   TIKU_C_SELECT);
    }
}

int tiku_circuitview_details_event(tiku_circuitview_t *v,
                                   const tiku_event_t *e)
{
    tiku_scroll_t *bar = &v->inspect_scroll;

    if (e->type == TIKU_EVENT_DEACTIVATED) {
        tiku_scroll_release(bar);
    }
    if (!v->inspector.w) {
        return 0;
    }
    int arrow = v->inspect_bar.w;
    int track = v->inspect_bar.h - 2 * arrow;
    int at = e->y - v->inspect_bar.y;
    if (e->type == TIKU_EVENT_POINTER_UP && bar->tracking) {
        tiku_scroll_release(bar);
    } else if (e->type == TIKU_EVENT_POINTER_MOVE && bar->tracking) {
        if (!bar->arrow_dir) {
            tiku_scroll_drag(bar, track, at - arrow);
        }
    } else if (e->type == TIKU_EVENT_POINTER_DOWN && e->button == 1 &&
               cv_inside(v->inspect_bar, e->x, e->y)) {
        tiku_dialog_focus(&v->dialog, CIRCUIT_INSPECTOR);
        if (at < arrow || at >= v->inspect_bar.h - arrow) {
            int direction = at < arrow ? -1 : 1;
            tiku_scroll_arrow_press(bar, direction);
            bar->tracking = 1;
            tiku_scroll_to(bar, bar->value + direction);
        } else {
            tiku_scroll_press(bar, track, at - arrow);
        }
    } else if (e->type == TIKU_EVENT_WHEEL &&
               cv_inside(v->inspector, e->x, e->y)) {
        tiku_scroll_to(bar, bar->value + (e->dy > 0 ? CV_WHEEL_ROWS :
                                          e->dy < 0 ? -CV_WHEEL_ROWS : 0));
    } else if (e->type == TIKU_EVENT_POINTER_DOWN && e->button == 1 &&
               cv_inside(v->inspect_rows, e->x, e->y)) {
        tiku_dialog_focus(&v->dialog, CIRCUIT_INSPECTOR);
    } else if (e->type == TIKU_EVENT_KEY_DOWN &&
               tiku_dialog_focused(&v->dialog, CIRCUIT_INSPECTOR)) {
        int value = bar->value;
        if (e->key == TIKU_KEY_UP) {
            value--;
        } else if (e->key == TIKU_KEY_DOWN) {
            value++;
        } else if (e->key == TIKU_KEY_PAGE_UP) {
            value -= bar->visible;
        } else if (e->key == TIKU_KEY_PAGE_DOWN) {
            value += bar->visible;
        } else if (e->key == TIKU_KEY_HOME) {
            value = 0;
        } else if (e->key == TIKU_KEY_END) {
            value = tiku_scroll_max(bar);
        } else {
            return 0;
        }
        tiku_scroll_to(bar, value);
    } else {
        return 0;
    }
    if (v->dirty) {
        *v->dirty = 1;
    }
    return 1;
}

void tiku_circuitview_details_tick(tiku_circuitview_t *v)
{
    int step = tiku_scroll_arrow_repeat(&v->inspect_scroll);

    if (step) {
        tiku_scroll_to(&v->inspect_scroll, v->inspect_scroll.value + step);
        if (v->dirty) {
            *v->dirty = 1;
        }
    }
}

void tiku_circuitview_details_copy(tiku_circuitview_t *v)
{
    if (!tiku_circuit_object(v->document, v->selected)) {
        return;
    }
    refresh(v);
    int result = tiku_clipboard_import(v->inspect_text,
                                       strlen(v->inspect_text));
    tiku_circuitview_note(v, result ? "Selection details copied." :
        "Could not copy selection details; the clipboard is unchanged.");
}
