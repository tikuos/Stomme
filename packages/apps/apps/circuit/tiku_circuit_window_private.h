/* tiku_circuit_window_private.h -- TikuCircuit's window state, which only
 * the UI thread touches, and the calls its modules make to one another.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#ifndef TIKU_CIRCUIT_WINDOW_PRIVATE_H_
#define TIKU_CIRCUIT_WINDOW_PRIVATE_H_
#include "tiku_circuit_window.h"
#include "tiku_circuit.h"
#include "tiku_circuit_project.h"
#include "tiku_circuit_stack.h"
#include "tiku_list.h"
#include "tiku_scroll.h"
#include "tiku_dialog.h"
#include "tiku_work.h"
#include "tiku_field_event.h"
#include "tiku_colors.h"
#include "tiku_slider.h"

#define CV_STATUS_MAX  512     /* the status line, with its end */
#define CV_PATH_MAX    4096    /* a document's path, with its end */
#define CV_INSPECT_MAX 4097    /* Selection Details, with its end */
#define CV_BAR         14      /* a scrollbar's width */
#define CV_FOCUS_INSET 2       /* a focus ring inside its control */
#define CV_WHEEL_ROWS  3       /* rows one wheel notch scrolls */
#define CV_ZOOM_IN     1.25    /* one zoom step in: menu, key or wheel */
#define CV_ZOOM_OUT    .8      /* one zoom step out */
#define CV_HIT_SLOP    5       /* pixels a click may miss an object by */
#define CV_OPAQUE      255u    /* a layer's full opacity */
#define CV_PAPER       0xfff7f6efu /* the canvas behind the drawing */

/* Commands, one number each for the menus, the toolbar and the keys; the
 * dialog's controls use the same numbers. */
enum {
    CIRCUIT_EXAMPLE = 1,    /* File > Open Example: the authored circuit */
    CIRCUIT_FIT,            /* View > Fit Drawing */
    CIRCUIT_OUT,            /* View > Zoom Out */
    CIRCUIT_IN,             /* View > Zoom In */
    CIRCUIT_CANCEL,         /* stop the render, import and export */
    CIRCUIT_CLOSE,          /* File > Close */
    CIRCUIT_EMPTY,          /* File > Clear Drawing */
    CIRCUIT_OPEN,           /* File > Open... */
    CIRCUIT_RELOAD,         /* File > Reload */
    CIRCUIT_FIND,           /* Edit > Find...: focus the Find field */
    CIRCUIT_NEXT,           /* Edit > Find Next, and the button for it */
    CIRCUIT_SHEETS,         /* View > Show Sheets: the sheet or layer list */
    CIRCUIT_PREV_SHEET,     /* the sheet or layer above the shown one */
    CIRCUIT_NEXT_SHEET,     /* the sheet or layer below it */
    CIRCUIT_SHEET_LIST,     /* the list's focus; not a command */
    CIRCUIT_DETAILS,        /* View > Show Selection Details */
    CIRCUIT_COPY,           /* Edit > Copy Selection Details */
    CIRCUIT_INSPECTOR,      /* Selection Details' focus; not a command */
    CIRCUIT_ADD_LAYER,      /* Layers > Add Layer... */
    CIRCUIT_OPEN_JOB,       /* File > Open Gerber Job... */
    CIRCUIT_LAYER_VISIBLE,  /* show or hide the selected layer */
    CIRCUIT_LAYER_UP,       /* move the selected layer towards the front */
    CIRCUIT_LAYER_DOWN,     /* move it towards the back */
    CIRCUIT_LAYER_COLOUR,   /* give it the palette's next colour */
    CIRCUIT_LAYER_OPACITY,  /* give it the next opacity step */
    CIRCUIT_LAYER_REMOVE,   /* take it off the stack */
    /* Tools > Measure Distance, Clear Measurement, Copy Measurement */
    CIRCUIT_MEASURE, CIRCUIT_MEASURE_CLEAR, CIRCUIT_MEASURE_COPY,
    /* Tools > Millimetres, Inches, Mils: the unit measurements are in */
    CIRCUIT_UNIT_MM, CIRCUIT_UNIT_INCH, CIRCUIT_UNIT_MIL,
    /* File > Export View as PNG...; Layers > Layer Appearance..., Isolate
     * Layer, Show All Layers; File > Save View Settings */
    CIRCUIT_EXPORT, CIRCUIT_LAYER_STYLE, CIRCUIT_LAYER_ISOLATE,
    CIRCUIT_LAYER_SHOW_ALL, CIRCUIT_SAVE_VIEW
};

/** @brief Whether (@p x, @p y) lies inside @p r. */
static inline int cv_inside(tiku_rect_t r, int x, int y)
{
    return x >= r.x && y >= r.y && x - r.x < r.w && y - r.y < r.h;
}

/** @brief The part of @p a inside @p b, for a clip within a clip. */
static inline tiku_rect_t cv_intersect(tiku_rect_t a, tiku_rect_t b)
{
    int x = a.x > b.x ? a.x : b.x;
    int y = a.y > b.y ? a.y : b.y;
    int right = a.x + a.w < b.x + b.w ? a.x + a.w : b.x + b.w;
    int bottom = a.y + a.h < b.y + b.h ? a.y + a.h : b.y + b.h;

    return (tiku_rect_t){x, y, right > x ? right - x : 0,
                         bottom > y ? bottom - y : 0};
}

/** @brief The window's state; only the UI thread touches it. */
struct tiku_circuitview {
    tiku_workspace_t *workspace;    /**< borrowed until destroy */
    tiku_window_t *window;          /**< NULL once the window has closed */
    int *dirty;                     /**< the application's redraw flag */
    int refs;                       /**< the owner and each job; UI thread */
    int busy;                       /**< a render is in flight */
    int needs_render;               /**< the newest view waits for one */
    int fit;                        /**< refit the drawing when resized */
    int dragging;                   /**< a press on the canvas is held */
    int dragged;                    /**< it has moved: a pan, not a click */
    int drag_x;                     /**< the pointer at the last pan step */
    int drag_y;                     /**< the same, down */
    int hover;                      /**< the control under the pointer */
    uint64_t generation;            /**< moves with every change of view */
    uint64_t rendered_generation;   /**< the generation image was made for */
    size_t selected;                /**< object index, or SIZE_MAX */
    tiku_circuit_t *document;       /**< the shown scene; one reference */
    tiku_circuit_view_t view;       /**< world to canvas pixels */
    tiku_circuit_view_t cached_view; /**< the view image was rendered at:
                                        view's, or the same scaled down */
    tiku_image_t image;             /**< the last view rendered */
    tiku_rect_t canvas;             /**< where the drawing is shown */
    tiku_rect_t area;               /**< the canvas before Selection Details
                                       and the list take their share */
    tiku_rect_t details;            /**< the status lines under the canvas */
    tiku_dialog_t dialog;           /**< the controls' focus, press and keys */
    char key[TIKU_WORK_KEY];        /**< the render jobs' work key */
    char status[CV_STATUS_MAX];     /**< the status line: the last result */
    void (*choose)(void *);         /**< asks for a file; NULL for none */
    void *choose_ctx;               /**< choose()'s argument */
    int loading;                    /**< an import is in flight */
    int needs_load;                 /**< requested waits for the next turn */
    uint64_t load_generation;       /**< moves with every import asked */
    char load_key[TIKU_WORK_KEY];   /**< the import jobs' work key */
    char path[CV_PATH_MAX];         /**< the shown document's file, or "" */
    char requested[CV_PATH_MAX];    /**< the file to import next */
    tiku_field_t query;             /**< the Find text */
    tiku_field_input_t query_input; /**< the Find field's pointer and scroll */
    tiku_rect_t find_rect;          /**< the Find field */
    tiku_circuit_project_t *project; /**< a schematic's sheets, or NULL */
    size_t sheet;                   /**< the shown sheet, or selected layer */
    tiku_list_t sheets;             /**< the list's rows, selection and keys */
    tiku_rect_t navigator;          /**< the sheet or layer list, or empty */
    tiku_rect_t sheet_rows;         /**< the list's rows */
    int show_sheets;                /**< View > Show Sheets */
    tiku_scroll_t sheet_scroll;     /**< the list's scrollbar */
    tiku_rect_t sheet_bar;          /**< where that is, or empty */
    int show_details;               /**< View > Show Selection Details */
    tiku_rect_t inspector;          /**< Selection Details, or empty */
    tiku_rect_t inspect_rows;       /**< its text */
    tiku_rect_t inspect_bar;        /**< its scrollbar, or empty */
    tiku_scroll_t inspect_scroll;   /**< that scrollbar's state */
    char inspect_text[CV_INSPECT_MAX]; /**< the details shown, copied */
    int picker_action;              /**< the command the last ask for a file
                                       was for, or 0 once answered */
    int load_action;                /**< the command that asked for
                                       requested, or 0 for an open from
                                       outside the window */
    int measure;                   /**< measuring: a click picks a point */
    int measure_points;            /**< zero, one preview point, or two */
    int cursor_valid;              /**< pointer currently inside the canvas */
    int units;                     /**< 0: mm, 1: inches, 2: mils */
    double measure_x[2], measure_y[2]; /**< endpoints in scene millimetres */
    double cursor_x, cursor_y;     /**< last pointer position in scene units */
    int style_open;                /**< modal layer appearance panel */
    size_t style_layer;            /**< layer the panel was opened for */
    uint64_t style_generation;     /**< import generation at panel opening */
    uint64_t picker_generation;    /**< view generation at export request */
    tiku_dialog_t style_dialog;    /**< appearance panel controls and focus */
    tiku_colors_t palette;         /**< suggested display colours */
    tiku_slider_t opacity;         /**< opacity, in whole percentages */
    tiku_field_t colour_text, opacity_text; /**< unapplied appearance values */
    tiku_field_input_t colour_input, opacity_input; /**< field interaction */
    tiku_rect_t style_rect, palette_rect, opacity_rect; /**< panel geometry */
    char style_error[128];         /**< inline validation message */
    int exporting, saving_view;    /**< workers holding a viewer reference */
    char export_key[TIKU_WORK_KEY]; /**< export cancellation key */
    char settings_dir[CV_PATH_MAX]; /**< host-provided directory; empty off */
};

/** @brief Forget the measurement's points and the pointer, and close the
 *         Layer Appearance panel. */
void cv_measure_reset(tiku_circuitview_t *v);
/** @brief While measuring, track the pointer in scene units and take a
 *         click as a point; a click after two starts again.
 *  @return 1 when it took a click. */
int cv_measure_event(tiku_circuitview_t *v, const tiku_event_t *e);
/** @brief The measuring status text: the distance and signed deltas in
 *         the chosen unit, the pointer's position, or how to measure. */
void cv_measure_text(const tiku_circuitview_t *v, char *out, size_t size);
/** @brief Draw clipped measurement marks over a viewport or export image. */
void cv_measure_draw(const tiku_circuitview_t *v, tiku_surface_t *s,
                     tiku_rect_t canvas, const tiku_circuit_view_t *view);
/** @brief Open Layer Appearance on the selected layer, its fields filled
 *         from the layer's colour and opacity. */
void cv_style_open(tiku_circuitview_t *v);
/** @brief Paint the appearance panel over the normal window content. */
void cv_style_draw(tiku_circuitview_t *v, tiku_surface_t *s);
/** @brief Take every event while the panel is open: its fields, palette,
 *         slider and buttons; Apply checks the values and restyles the
 *         layer.  @return 1 while the panel is open. */
int cv_style_event(tiku_circuitview_t *v, const tiku_event_t *e);
/** @brief Restyle the selected layer: a new stack in which it has
 *         @p colour (RGB) and @p opacity (0-255).  @return 1 when made. */
int cv_layer_style(tiku_circuitview_t *v, uint32_t colour, unsigned opacity);
/** @brief Export View as PNG to @p path: the rendered view with its
 *         measurement and a caption, written on a worker; refused when the
 *         view changed since the ask or the name does not end in .png. */
void cv_export(tiku_circuitview_t *v, const char *path);
/** @brief Save the document's units, panels and layer looks on a worker. */
void cv_save_view(tiku_circuitview_t *v);
/**
 * @brief Apply the settings saved for @p path to *@p document: each saved
 *        layer found in the stack takes its colour, opacity and visibility
 *        and its saved place in the order; units and panels are set.  A
 *        missing or invalid settings file changes nothing.
 * @return TIKU_CIRCUIT_OK, or the error making the new stack
 * @note Runs on a worker, inside the import.
 */
tiku_circuit_err_t cv_restore_view(const char *directory, const char *path,
    tiku_circuit_t **document, int *units, int *sheets, int *details);

/** @brief Place Selection Details at the foot of @p canvas, shortening the
 *         canvas, and add the panel's controls to the dialog. */
void tiku_circuitview_details_layout(tiku_circuitview_t *v,
                                     tiku_rect_t *canvas);
/**
 * @brief The height Selection Details takes at the foot of @p canvas when
 *        a drawing is shown, with @p canvas shortened by it and the gap.
 *
 * @return The panel's height, or 0 with @p canvas unchanged when the panel
 *         is hidden or the canvas is too small for it
 */
int tiku_circuitview_details_take(const tiku_circuitview_t *v,
                                  tiku_rect_t *canvas);
/** @brief Draw Selection Details: title, Copy Details, text and bar. */
void tiku_circuitview_details_draw(tiku_circuitview_t *v, tiku_surface_t *s);
/** @brief Scroll or focus Selection Details.
 *  @return 1 when the panel took @p e */
int tiku_circuitview_details_event(tiku_circuitview_t *v,
                                   const tiku_event_t *e);
/** @brief Repeat a held arrow of Selection Details' scrollbar. */
void tiku_circuitview_details_tick(tiku_circuitview_t *v);
/** @brief Copy the selection's whole description to the clipboard. */
void tiku_circuitview_details_copy(tiku_circuitview_t *v);
/** @brief Let the shown project go and empty the sheet list. */
void tiku_circuitview_clear_project(tiku_circuitview_t *v);
/** @brief Show sheet @p index, fitted to the canvas, or select layer
 *         @p index of a stack. */
void tiku_circuitview_sheet(tiku_circuitview_t *v, size_t index);
/** @brief Find Next: select and centre the next object matching the Find
 *         text, across every loaded sheet, wrapping at the end. */
void tiku_circuitview_search(tiku_circuitview_t *v);
/** @brief Draw the sheet or layer list, when it is shown. */
void tiku_circuitview_sheets_draw(tiku_circuitview_t *v, tiku_surface_t *s);
/** @brief Pointer, wheel and keys for the sheet or layer list.
 *  @return 1 when the list took @p e */
int tiku_circuitview_sheets_event(tiku_circuitview_t *v,
                                  const tiku_event_t *e);
/** @brief Repeat a held arrow of the list's scrollbar. */
void tiku_circuitview_sheets_tick(tiku_circuitview_t *v);
/** @brief Post the import asked for last, when none is in flight. */
void tiku_circuitview_files_tick(tiku_circuitview_t *v);
/** @brief Rows of the list: the project's sheets or the stack's layers. */
size_t tiku_circuitview_rows(const tiku_circuitview_t *v);
/** @brief Whether the shown drawing is a layer stack with room for one more
 *         layer: what Add Layer needs. */
int tiku_circuitview_layer_room(const tiku_circuitview_t *v);
/** @brief Apply a Layers command, to the selected layer or, for Isolate
 *         Layer and Show All Layers, to every layer, by building a new
 *         stack from changed copies of the layer descriptors. */
void tiku_circuitview_layers_action(tiku_circuitview_t *v, int command);
/**
 * @brief Read the files of the layer stack @p base again into a new stack,
 *        or with @p add, read @p path into a new layer at its front.
 *
 * Each layer keeps its descriptor: visibility, colour, opacity and
 * declared format.  One file that cannot be read fails the whole stack,
 * and @p diag names it; so does a @p base that is no stack or one already
 * full.
 * @return TIKU_CIRCUIT_OK with the stack in @p out, or the first error
 * @note Call on a worker: it reads files.
 */
tiku_circuit_err_t tiku_circuitview_layer_load(const tiku_circuit_t *base,
    int add, const char *path, const tiku_circuit_import_t *options,
    tiku_circuit_t **out, tiku_circuit_diagnostic_t *diag);
/** @brief Cancel the import in flight and forget the one asked for. */
void tiku_circuitview_files_cancel(tiku_circuitview_t *v);
/** @brief Say in the status line what the window shows: the selected
 *         layer, the sheet, the drawing, or what can be opened. */
void tiku_circuitview_ready(tiku_circuitview_t *v);
/** @brief The view has changed: cancel the render in flight and ask for
 *         another when a drawing is shown. */
void tiku_circuitview_changed(tiku_circuitview_t *v);
/** @brief Drop one reference; the last frees the window's state. */
void tiku_circuitview_unref(tiku_circuitview_t *v);
/** @brief Place the toolbar, Find, the list, the canvas, Selection Details
 *         and the status lines in @p content, and make the view the
 *         canvas's size, refitting it while Fit holds. */
void tiku_circuitview_layout(tiku_circuitview_t *v,
                             tiku_rect_t content);
/** @brief The width the sheet or layer list takes beside @p canvas in
 *         @p content when it has more than one row; 0 when it is hidden. */
int tiku_circuitview_list_width(const tiku_circuitview_t *v,
                                tiku_rect_t content, tiku_rect_t canvas);
/**
 * @brief The canvas a document about to be shown will have, before it
 *        exists: Selection Details shown as for any drawing, and @p list
 *        set to the width the list then takes, with its gap, when the
 *        document has more than one sheet or layer.
 *
 * @note Uses the layout last made for @p content.
 */
tiku_rect_t tiku_circuitview_next_canvas(const tiku_circuitview_t *v,
                                         tiku_rect_t content, int *list);
/**
 * @brief Fit @p doc to a @p w by @p h canvas, as tiku_circuit_fit() does,
 *        for a canvas of any size: one larger than the kit renders at once
 *        is fitted at the size it is rendered at and scaled up.
 *
 * @return 1 with @p view set, or 0 with it unchanged
 */
int tiku_circuitview_fit(const tiku_circuit_t *doc, int w, int h,
                         tiku_circuit_view_t *view);
/** @brief The view to render for canvas view @p view: the same, or for a
 *         canvas larger than the kit renders at once, the same drawing at
 *         the largest image it renders, to be drawn scaled up. */
tiku_circuit_view_t tiku_circuitview_render_view(
    const tiku_circuit_view_t *view);
/** @brief Run @p command when it is enabled, from any route. */
void tiku_circuitview_action(tiku_circuitview_t *v, int command);
/** @brief Publish the menus with each command's enabled state, mark and
 *         shortcut, as the window stands. */
void tiku_circuitview_menus(tiku_circuitview_t *v);
/** @brief Whether @p command can do anything in the window as it is. */
int tiku_circuitview_enabled(const tiku_circuitview_t *v,
                             int command);
/** @brief The menu label of @p command; "" for one without a label. */
const char *tiku_circuitview_label(int command);
/** @brief Draw the window into @p r: controls, list, canvas and status. */
void tiku_circuitview_draw(tiku_window_t *w, tiku_surface_t *s,
                           tiku_rect_t r, void *ctx);
/** @brief Route one event to the controls, the list, the details panel
 *         and the canvas.  @return 1 when the window took @p event */
int tiku_circuitview_event(tiku_window_t *w, const tiku_event_t *event,
                           void *ctx);
#endif
