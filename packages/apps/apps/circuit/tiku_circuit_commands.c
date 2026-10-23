/* tiku_circuit_commands.c -- the window's commands: their labels, when
 * they are enabled, the menus, and the one action path the menus,
 * toolbar and keys share.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#include "tiku_circuit_window_private.h"
#include <stdio.h>
#include <string.h>
#include "tiku_clipboard.h"

/* The published menus, in their order on the bar. */
enum {
    CV_MENU_FILE,           /* opening, reloading and closing */
    CV_MENU_EDIT,           /* copying and finding */
    CV_MENU_VIEW,           /* fit, zoom and the panels */
    CV_MENU_LAYERS,         /* the layer stack's commands */
    CV_MENU_TOOLS,          /* measuring and its units */
    CV_MENUS                /* how many there are */
};

const char *tiku_circuitview_label(int command)
{
    switch (command) {
    case CIRCUIT_MEASURE: return "Measure Distance";
    case CIRCUIT_MEASURE_CLEAR: return "Clear Measurement";
    case CIRCUIT_MEASURE_COPY: return "Copy Measurement";
    case CIRCUIT_UNIT_MM: return "Millimetres";
    case CIRCUIT_UNIT_INCH: return "Inches";
    case CIRCUIT_UNIT_MIL: return "Mils";
    case CIRCUIT_EXPORT: return "Export View as PNG...";
    case CIRCUIT_SAVE_VIEW: return "Save View Settings";
    case CIRCUIT_LAYER_STYLE: return "Layer Appearance...";
    case CIRCUIT_LAYER_ISOLATE: return "Isolate Layer";
    case CIRCUIT_LAYER_SHOW_ALL: return "Show All Layers";
    case CIRCUIT_ADD_LAYER:
        return "Add Layer...";
    case CIRCUIT_OPEN_JOB:
        return "Open Gerber Job...";
    case CIRCUIT_LAYER_VISIBLE:
        return "Layer Visible";
    case CIRCUIT_LAYER_UP:
        return "Raise Layer";
    case CIRCUIT_LAYER_DOWN:
        return "Lower Layer";
    case CIRCUIT_LAYER_COLOUR:
        return "Next Layer Colour";
    case CIRCUIT_LAYER_OPACITY:
        return "Cycle Layer Opacity";
    case CIRCUIT_LAYER_REMOVE:
        return "Remove Layer";
    }
    static const char *const labels[] = {"", "Open Example", "Fit Drawing",
        "Zoom Out", "Zoom In", "Cancel", "Close", "Clear Drawing", "Open...",
        "Reload", "Find...", "Find Next", "Show Sheets", "Previous Sheet",
        "Next Sheet", "", "Show Selection Details",
        "Copy Selection Details"};
    return command > 0 && command <= CIRCUIT_COPY ? labels[command] : "";
}

int tiku_circuitview_enabled(const tiku_circuitview_t *v, int id)
{
    if (v->style_open && id != CIRCUIT_CLOSE) return 0;
    if (id == CIRCUIT_EXPORT) return v->choose && v->document && v->image.px &&
        !v->exporting && !v->loading && !v->needs_load && !v->busy && !v->needs_render &&
        v->rendered_generation == v->generation;
    if (id == CIRCUIT_SAVE_VIEW)
        return v->document && v->path[0] && v->settings_dir[0] &&
        !v->saving_view && !v->loading && !v->needs_load;
    if (id == CIRCUIT_MEASURE_CLEAR) return v->measure_points > 0 || v->measure;
    if (id == CIRCUIT_MEASURE_COPY) return v->measure_points == 2;
    if (id == CIRCUIT_EXAMPLE || id == CIRCUIT_CLOSE) {
        return 1;
    }
    if (id == CIRCUIT_OPEN || id == CIRCUIT_OPEN_JOB) {
        return v->choose != NULL;
    }
    if (id == CIRCUIT_ADD_LAYER) {
        return v->choose && !v->loading && !v->needs_load &&
               tiku_circuitview_layer_room(v);
    }
    if ((id >= CIRCUIT_LAYER_VISIBLE && id <= CIRCUIT_LAYER_REMOVE) ||
        id == CIRCUIT_LAYER_STYLE || id == CIRCUIT_LAYER_ISOLATE ||
        id == CIRCUIT_LAYER_SHOW_ALL) {
        size_t n = tiku_circuit_layer_count(v->document);
        if (!n || v->sheet >= n || v->loading || v->needs_load) {
            return 0;
        }
        if (id == CIRCUIT_LAYER_UP) {
            return v->sheet > 0;
        }
        if (id == CIRCUIT_LAYER_DOWN) {
            return v->sheet + 1 < n;
        }
        return 1;
    }
    if (id == CIRCUIT_RELOAD) {
        return v->path[0] != 0;
    }
    if (id == CIRCUIT_CANCEL) {
        return v->busy || v->needs_render || v->loading ||
               v->needs_load || v->exporting;
    }
    if (id == CIRCUIT_EMPTY) {
        return v->project || v->document || v->loading || v->needs_load;
    }
    if (id == CIRCUIT_FIND || id == CIRCUIT_NEXT) {
        return v->project || v->document;
    }
    if (id == CIRCUIT_SHEETS) {
        return tiku_circuitview_rows(v) > 1;
    }
    if (id == CIRCUIT_PREV_SHEET) {
        return tiku_circuitview_rows(v) && v->sheet > 0;
    }
    if (id == CIRCUIT_NEXT_SHEET) {
        return v->sheet + 1 < tiku_circuitview_rows(v);
    }
    if (id == CIRCUIT_DETAILS) {
        return 1;
    }
    if (id == CIRCUIT_COPY) {
        return tiku_circuit_object(v->document, v->selected) != NULL;
    }
    return v->document != NULL;
}

/** @brief A pick from the published menus, run on the one action path. */
static void menu_pick(tiku_window_t *w, int command, void *ctx)
{
    (void)w;
    tiku_circuitview_action(ctx, command);
}

/** @brief Add @p id to @p list with its shortcut, shown as @p sc with
 *         @p mods, and its mark; a stack's sheet commands say Layer. */
static void menu_add(const tiku_circuitview_t *v, tiku_menu_list_t *list,
                     int id, char sc, unsigned mods, int marked)
{
    tiku_menu_entry_t *e = &list->item[list->nitem++];

    snprintf(e->label, sizeof e->label, "%s", tiku_circuitview_label(id));
    if (!v->project && tiku_circuit_layer_count(v->document)) {
        if (id == CIRCUIT_SHEETS) {
            snprintf(e->label, sizeof e->label, "Show Layers");
        }
        if (id == CIRCUIT_PREV_SHEET) {
            snprintf(e->label, sizeof e->label, "Previous Layer");
        }
        if (id == CIRCUIT_NEXT_SHEET) {
            snprintf(e->label, sizeof e->label, "Next Layer");
        }
    }
    e->command = id;
    e->enabled = (unsigned char)tiku_circuitview_enabled(v, id);
    e->sc = sc;
    e->mods = sc ? mods : 0;
    e->marked = (unsigned char)marked;
}

void tiku_circuitview_menus(tiku_circuitview_t *v)
{
    static const char *const titles[CV_MENUS] = {"File", "Edit", "View",
                                                 "Layers", "Tools"};

    if (!v->window) {
        return;
    }
    tiku_menuset_t menus = {0};
    menus.nmenu = CV_MENUS;
    for (int i = 0; i < CV_MENUS; i++) {
        snprintf(menus.menu[i].title, sizeof menus.menu[i].title, "%s",
                 titles[i]);
    }
    tiku_menu_list_t *file = &menus.menu[CV_MENU_FILE];
    tiku_menu_list_t *edit = &menus.menu[CV_MENU_EDIT];
    tiku_menu_list_t *view = &menus.menu[CV_MENU_VIEW];
    tiku_menu_list_t *layers = &menus.menu[CV_MENU_LAYERS];
    tiku_menu_list_t *tools = &menus.menu[CV_MENU_TOOLS];
    const tiku_circuit_layer_t *l = tiku_circuit_layer(v->document, v->sheet);
    menu_add(v, file, CIRCUIT_OPEN, 'o', TIKU_MOD_CMD, 0);
    menu_add(v, file, CIRCUIT_OPEN_JOB, 0, 0, 0);
    menu_add(v, file, CIRCUIT_RELOAD, 'r', TIKU_MOD_CMD, 0);
    menu_add(v, file, CIRCUIT_EXPORT, 0, 0, 0);
    menu_add(v, file, CIRCUIT_SAVE_VIEW, 0, 0, 0);
    menu_add(v, file, CIRCUIT_EXAMPLE, 0, 0, 0);
    menu_add(v, file, CIRCUIT_EMPTY, 0, 0, 0);
    menu_add(v, file, CIRCUIT_CLOSE, 'w', TIKU_MOD_CMD, 0);
    menu_add(v, edit, CIRCUIT_COPY, 'c', TIKU_MOD_CMD, 0);
    menu_add(v, edit, CIRCUIT_FIND, 'f', TIKU_MOD_CMD, 0);
    menu_add(v, edit, CIRCUIT_NEXT, 'g', TIKU_MOD_CMD, 0);
    /* Fit and zoom take their keys alone, without a modifier. */
    menu_add(v, view, CIRCUIT_FIT, '0', 0, v->fit);
    menu_add(v, view, CIRCUIT_IN, '+', 0, 0);
    menu_add(v, view, CIRCUIT_OUT, '-', 0, 0);
    menu_add(v, view, CIRCUIT_CANCEL, 0, 0, 0);
    menu_add(v, view, CIRCUIT_SHEETS, 0, 0, v->show_sheets);
    menu_add(v, view, CIRCUIT_PREV_SHEET, '[', TIKU_MOD_CMD, 0);
    menu_add(v, view, CIRCUIT_NEXT_SHEET, ']', TIKU_MOD_CMD, 0);
    menu_add(v, view, CIRCUIT_DETAILS, 0, 0, v->show_details);
    menu_add(v, layers, CIRCUIT_ADD_LAYER, 0, 0, 0);
    menu_add(v, layers, CIRCUIT_LAYER_VISIBLE, 0, 0, l && l->visible);
    menu_add(v, layers, CIRCUIT_LAYER_UP, 0, 0, 0);
    menu_add(v, layers, CIRCUIT_LAYER_DOWN, 0, 0, 0);
    menu_add(v, layers, CIRCUIT_LAYER_STYLE, 0, 0, 0);
    menu_add(v, layers, CIRCUIT_LAYER_ISOLATE, 0, 0, 0);
    menu_add(v, layers, CIRCUIT_LAYER_SHOW_ALL, 0, 0, 0);
    menu_add(v, layers, CIRCUIT_LAYER_REMOVE, 0, 0, 0);
    menu_add(v, tools, CIRCUIT_MEASURE, 'm', 0, v->measure);
    menu_add(v, tools, CIRCUIT_MEASURE_CLEAR, 0, 0, 0);
    menu_add(v, tools, CIRCUIT_MEASURE_COPY, 0, 0, 0);
    menu_add(v, tools, CIRCUIT_UNIT_MM, 0, 0, v->units == 0);
    menu_add(v, tools, CIRCUIT_UNIT_INCH, 0, 0, v->units == 1);
    menu_add(v, tools, CIRCUIT_UNIT_MIL, 0, 0, v->units == 2);
    tiku_window_publish_menus(v->window, &menus, menu_pick, v);
}

void tiku_circuitview_action(tiku_circuitview_t *v, int command)
{
    if (!v || !v->window || !tiku_circuitview_enabled(v, command)) {
        return;
    }
    if (command == CIRCUIT_CLOSE) {
        tiku_workspace_close(v->workspace, v->window);
        return;
    }
    if (command == CIRCUIT_LAYER_STYLE) { cv_style_open(v); return; }
    if (command == CIRCUIT_SAVE_VIEW) { cv_save_view(v); return; }
    if (command >= CIRCUIT_MEASURE && command <= CIRCUIT_UNIT_MIL) {
        if (command == CIRCUIT_MEASURE) { v->measure = !v->measure; v->dragging = 0; }
        else if (command == CIRCUIT_MEASURE_CLEAR) {
            if (!v->measure_points) v->measure = 0;
            cv_measure_reset(v);
        } else if (command == CIRCUIT_MEASURE_COPY) {
            char text[512];
            cv_measure_text(v,text,sizeof text);
            tiku_clipboard_set(text);
        } else v->units = command - CIRCUIT_UNIT_MM;
        if (v->dirty) *v->dirty = 1;
        tiku_circuitview_menus(v); return;
    }
    if (command == CIRCUIT_OPEN || command == CIRCUIT_OPEN_JOB ||
        command == CIRCUIT_ADD_LAYER || command == CIRCUIT_EXPORT) {
        v->picker_action = command;
        v->picker_generation = v->generation;
        v->choose(v->choose_ctx);
        tiku_circuitview_menus(v);
        return;
    }
    if ((command >= CIRCUIT_LAYER_VISIBLE && command <= CIRCUIT_LAYER_REMOVE) ||
        command == CIRCUIT_LAYER_ISOLATE || command == CIRCUIT_LAYER_SHOW_ALL) {
        tiku_circuitview_layers_action(v, command);
        return;
    }
    if (command == CIRCUIT_RELOAD) {
        tiku_circuitview_file(v, v->path);
        v->load_action = CIRCUIT_RELOAD;
        return;
    }
    if (command == CIRCUIT_FIND) {
        tiku_dialog_focus(&v->dialog, CIRCUIT_FIND);
        tiku_field_key(&v->query, TIKU_FIELD_SELECT_ALL, 0, 0);
        if (v->dirty) {
            *v->dirty = 1;
        }
        return;
    }
    if (command == CIRCUIT_NEXT) {
        tiku_circuitview_search(v);
        return;
    }
    if (command == CIRCUIT_COPY) {
        tiku_circuitview_details_copy(v);
        return;
    }
    if (command == CIRCUIT_SHEETS || command == CIRCUIT_DETAILS) {
        if (command == CIRCUIT_SHEETS) {
            v->show_sheets = !v->show_sheets;
        } else {
            v->show_details = !v->show_details;
        }
        tiku_circuitview_layout(v, tiku_window_content(v->window));
        if (v->dirty) {
            *v->dirty = 1;
        }
        tiku_circuitview_menus(v);
        return;
    }
    if (command == CIRCUIT_PREV_SHEET || command == CIRCUIT_NEXT_SHEET) {
        tiku_circuitview_sheet(v, command == CIRCUIT_PREV_SHEET ?
                                         v->sheet - 1 : v->sheet + 1);
        return;
    }
    if (command == CIRCUIT_CANCEL) {
        tiku_work_cancel(v->export_key);
        tiku_work_cancel(v->key);
        v->generation++;
        v->needs_render = 0;
        tiku_circuitview_files_cancel(v);
        tiku_circuitview_note(v, "Operation cancelled. Drawing "
            "unchanged; navigate or Fit to render again.");
    } else if (command == CIRCUIT_EMPTY) {
        cv_measure_reset(v);
        tiku_circuitview_files_cancel(v);
        v->path[0] = 0;
        tiku_circuitview_clear_project(v);
        tiku_circuit_release(v->document);
        v->document = NULL;
        tiku_image_free(&v->image);
        v->selected = SIZE_MAX;
        tiku_circuitview_changed(v);
        tiku_circuitview_ready(v);
    } else if (command == CIRCUIT_EXAMPLE) {
        cv_measure_reset(v);
        tiku_circuit_t *d = NULL;
        tiku_circuit_err_t e = tiku_circuit_example(&d);
        if (e) {
            tiku_circuitview_note(v, tiku_circuit_error(e));
            return;
        }
        tiku_circuitview_files_cancel(v);
        v->path[0] = 0;
        tiku_circuitview_clear_project(v);
        tiku_circuit_release(v->document);
        v->document = d;
        tiku_image_free(&v->image);
        v->selected = SIZE_MAX;
        v->fit = 1;
        tiku_circuitview_layout(v, tiku_window_content(v->window));
        tiku_circuitview_fit(d, v->canvas.w, v->canvas.h, &v->view);
        tiku_circuitview_changed(v);
        tiku_circuitview_ready(v);
    } else if (command == CIRCUIT_FIT) {
        if (tiku_circuitview_fit(v->document, v->canvas.w, v->canvas.h,
                                 &v->view)) {
            v->fit = 1;
            tiku_circuitview_changed(v);
            tiku_circuitview_ready(v);
        }
    } else if (command == CIRCUIT_IN || command == CIRCUIT_OUT) {
        if (tiku_circuit_zoom(&v->view, command == CIRCUIT_IN ?
                                  CV_ZOOM_IN : CV_ZOOM_OUT,
                              v->canvas.w / 2.0, v->canvas.h / 2.0)) {
            v->fit = 0;
            tiku_circuitview_changed(v);
        }
    }
    tiku_circuitview_menus(v);
}
