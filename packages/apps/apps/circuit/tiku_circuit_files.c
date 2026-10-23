/* tiku_circuit_files.c -- opening documents on a worker: the scene and its
 * first view are published together, and a failure keeps what is shown.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#include "tiku_circuit_window_private.h"
#include "tiku_fontset.h"
#include "tiku_circuit_source.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define CV_FONT_WEIGHT 400      /* the regular face of a family */

/** @brief One import: what was asked, and what the worker made of it. */
typedef struct {
    tiku_circuitview_t *owner;      /* the window; one reference */
    uint64_t generation;            /* the load generation when asked */
    char path[CV_PATH_MAX];         /* the file asked for */
    tiku_circuit_t *document;       /* the scene shown first, until taken */
    tiku_circuit_project_t *project; /* a schematic's sheets, until taken */
    int reload;                     /* Reload of the shown project */
    int layers;                     /* Add Layer, or Reload of a stack */
    int action;                     /* the command that asked, or 0 */
    char settings_dir[CV_PATH_MAX]; /* saved view settings, or "" */
    int units, sheets, details;     /* their values; -1 for none */
    tiku_circuit_t *base;           /* the stack added to or reread */
    char *instance;                 /* for a reload, the shown sheet's
                                       instance, to show it again */
    size_t sheet;                   /* the sheet or layer shown first */
    int list;                       /* what the sheet list takes of the
                                       canvas's width when it is shown */
    tiku_image_t image;             /* the first view, until taken */
    tiku_circuit_view_t view;       /* that view: the canvas, fitted */
    tiku_circuit_view_t render;     /* the view the image is rendered at */
    tiku_circuit_err_t result;      /* TIKU_CIRCUIT_OK or why it failed */
    tiku_circuit_diagnostic_t diag; /* where and why, for the status line */
} import_job_t;

/** @brief Whether this job was cancelled; the job's state, not the
 *         window's, which the loop changes meanwhile. */
static int cancelled(void *ctx)
{
    return tiku_work_cancelled(ctx);
}

/**
 * @brief On the worker: read the file, then fit and render its first view.
 *
 * Add Layer and a stack's Reload read the stack's files; otherwise the
 * content decides: a Gerber job, a KiCad or EAGLE schematic, or one Gerber
 * or drill file, which becomes a stack of one layer.  Saved view settings
 * apply, except for Reload and Add Layer.  The first view is the canvas
 * the document will be shown in, narrower by the list when the document
 * has more than one sheet or layer.
 */
static void run(void *ctx)
{
    import_job_t *j = ctx;
    unsigned char *font = NULL;
    size_t font_bytes = 0;
    tiku_circuitview_local_t local = {.directory = -1, .cancel = cancelled,
                                      .ctx = j};
    tiku_circuit_resolver_t resolver = tiku_circuitview_local_resolver(&local);
    tiku_circuit_import_t flat_options = {.cancel = cancelled, .ctx = j};

    if (j->layers) {
        j->result = tiku_circuitview_layer_load(j->base,
            j->action == CIRCUIT_ADD_LAYER, j->path, &flat_options,
            &j->document, &j->diag);
        goto render;
    }
    tiku_circuit_source_t source = {0};
    j->result = resolver.read(resolver.ctx, NULL, j->path, &source,
                              &j->diag);
    if (j->result) {
        resolver.release(resolver.ctx, &source);
        goto finished;
    }
    size_t first = 0;
    while (first < source.bytes &&
           (source.data[first] == ' ' || source.data[first] == '\t' ||
            source.data[first] == '\r' || source.data[first] == '\n')) {
        first++;
    }
    int schematic = first < source.bytes && source.data[first] == '(';
    int eagle = first < source.bytes &&
                (source.data[first] == '<' || source.data[first] == 0xef);
    const char *suffix = strrchr(j->path, '.');
    if (suffix && !strcasecmp(suffix, ".sch") && !eagle) {
        j->result = TIKU_CIRCUIT_FORMAT;
        j->diag.code = j->result;
        snprintf(j->diag.message, sizeof j->diag.message,
                 "This .sch is not EAGLE XML. Convert legacy binary EAGLE "
                 "or legacy KiCad schematics with a compatible CAD tool; "
                 "the original is unchanged");
        resolver.release(resolver.ctx, &source);
        goto finished;
    }
    schematic = schematic || eagle;
    int job = first < source.bytes && source.data[first] == '{';
    if (j->action == CIRCUIT_OPEN_JOB && !job) {
        j->result = TIKU_CIRCUIT_FORMAT;
        resolver.release(resolver.ctx, &source);
        goto finished;
    }
    if (job) {
        resolver.release(resolver.ctx, &source);
        j->result = tiku_circuit_job_open(j->path, &flat_options, &resolver,
                                          &j->document, &j->diag);
        goto render;
    }
    if (!schematic) {
        tiku_circuit_import_t drill_options = {.cancel = cancelled,
                                               .ctx = j};
        int drill = first < source.bytes &&
                    (source.data[first] == ';' || source.data[first] == 'M');
        j->result = drill ?
            tiku_circuit_drill(source.data, source.bytes, &drill_options,
                               &j->document, &j->diag) :
            tiku_circuit_gerber(source.data, source.bytes, &drill_options,
                                &j->document, &j->diag);
    }
    resolver.release(resolver.ctx, &source);
    if (!schematic && !j->result) {
        const char *name = strrchr(j->path, '/');
        name = name ? name + 1 : j->path;
        tiku_circuit_layer_t layer = {.scene = j->document,
            .path = j->path, .root = "",
            .label = strlen(name) < TIKU_CIRCUIT_LABEL_BYTES ? name : "Drawing",
            .colour = tiku_circuit_layer_colour(0),
            .opacity = CV_OPAQUE, .visible = 1};
        tiku_circuit_t *stack = NULL;
        j->result = tiku_circuit_stack(&layer, 1, &stack);
        tiku_circuit_release(j->document);
        j->document = stack;
    }
    if (!schematic) {
        goto render;
    }
    /* The font comes from the host's font set; no path the schematic
     * names is read for it. */
    const char *families[] = {"DejaVu Sans", "Liberation Sans", "Arial"};
    for (size_t i = 0; i < sizeof families / sizeof *families && !font;
         i++) {
        tiku_fontset_face_t face;
        if (cancelled(j)) {
            j->result = TIKU_CIRCUIT_CANCELLED;
            goto finished;
        }
        if (!tiku_fontset_find(families[i], CV_FONT_WEIGHT, 0, &face) &&
            !face.cff && !face.index) {
            (void)tiku_circuitview_local_font(face.path, &local, &font,
                                              &font_bytes);
        }
    }
    tiku_circuit_import_t options = {font, font_bytes, cancelled, j};
    if (eagle) {
        j->result = resolver.read(resolver.ctx, NULL, j->path, &source,
                                  &j->diag);
        if (!j->result) {
            j->result = tiku_circuit_eagle(source.data, source.bytes,
                                           j->path, &options, &j->project,
                                           &j->diag);
        }
        resolver.release(resolver.ctx, &source);
    } else {
        j->result = tiku_circuit_project_open(j->path, &options, &resolver,
                                              &j->project, &j->diag);
    }
    if (!j->result && j->reload &&
        tiku_circuit_project_incomplete(j->project)) {
        j->result = TIKU_CIRCUIT_MISSING;
        j->diag.code = j->result;
        snprintf(j->diag.message, sizeof j->diag.message,
                 "Reload failed: one or more linked sheets are unavailable");
    }
    if (!j->result) {
        if (j->reload) {
            for (size_t i = 0; i < tiku_circuit_project_count(j->project);
                 i++) {
                const tiku_circuit_sheet_t *sheet =
                    tiku_circuit_project_sheet(j->project, i);
                if (!strcmp(sheet->instance, j->instance)) {
                    j->sheet = i;
                    break;
                }
            }
        }
        j->document = tiku_circuit_retain(
            tiku_circuit_project_scene(j->project, j->sheet));
    }
render:
    if (!j->result && j->action != CIRCUIT_RELOAD && j->action != CIRCUIT_ADD_LAYER)
        j->result = cv_restore_view(j->settings_dir,j->path,&j->document,
            &j->units,&j->sheets,&j->details);
    if (!j->result) {
        size_t rows = j->project ? tiku_circuit_project_count(j->project) :
                                   tiku_circuit_layer_count(j->document);
        if (rows > 1) {
            j->view.width -= j->list;
        }
        (void)tiku_circuitview_fit(j->document, j->view.width,
                                   j->view.height, &j->view);
        j->render = tiku_circuitview_render_view(&j->view);
        j->result = tiku_circuit_render(j->document, &j->render, cancelled,
                                        j, &j->image);
    }
finished:
    tiku_circuitview_local_close(&local);
    free(font);
}

/** @brief On the loop: show the new document and its first view, or say
 *         why it failed and keep the one shown. */
static void done(void *ctx)
{
    import_job_t *j = ctx;
    tiku_circuitview_t *v = j->owner;

    if (!v->window || v->load_generation != j->generation) {
        return;
    }
    if (j->result) {
        char message[CV_STATUS_MAX];
        if (j->diag.line) {
            snprintf(message, sizeof message, "Line %u:%u: %s.%s",
                     j->diag.line, j->diag.column, j->diag.message,
                     v->project || v->document ?
                         " Previous drawing is still displayed." : "");
        } else {
            snprintf(message, sizeof message, "%s.%s",
                     *j->diag.message ? j->diag.message :
                                        tiku_circuit_error(j->result),
                     v->project || v->document ?
                         " Previous drawing is still displayed." : "");
        }
        tiku_circuitview_note(v, message);
        return;
    }
    tiku_circuitview_changed(v);
    cv_measure_reset(v);
    if (j->units >= 0) {
        v->units = j->units; v->show_sheets = j->sheets; v->show_details = j->details;
    }
    tiku_circuit_release(v->document);
    tiku_image_free(&v->image);
    tiku_circuit_project_free(v->project);
    v->project = j->project;
    j->project = NULL;
    v->sheet = j->sheet;
    tiku_list_set_count(&v->sheets, (int)(v->project ?
        tiku_circuit_project_count(v->project) :
        tiku_circuit_layer_count(j->document)));
    tiku_list_select_only(&v->sheets, (int)v->sheet);
    v->sheets.top = 0;
    tiku_scroll_release(&v->sheet_scroll);
    v->document = j->document;
    j->document = NULL;
    v->image = j->image;
    memset(&j->image, 0, sizeof j->image);
    v->view = j->view;
    v->cached_view = j->render;
    v->rendered_generation = v->generation;
    v->selected = SIZE_MAX;
    v->fit = 1;
    v->needs_render = 0;
    v->dragging = 0;
    if (j->action != CIRCUIT_ADD_LAYER) {
        snprintf(v->path, sizeof v->path, "%s", j->path);
    }
    /* A window resized while the import ran gets a canvas of another size,
     * which the layout refits and renders again. */
    tiku_circuitview_layout(v, tiku_window_content(v->window));
    tiku_list_reveal(&v->sheets, v->sheet_rows);
    tiku_circuitview_ready(v);
}

/** @brief After done, or in place of it when cancelled: let go of what the
 *         job still holds and drop its reference to the window. */
static void dispose(void *ctx)
{
    import_job_t *j = ctx;
    tiku_circuitview_t *v = j->owner;

    v->loading = 0;
    tiku_circuit_release(j->document);
    tiku_image_free(&j->image);
    tiku_circuit_release(j->base);
    free(j->instance);
    tiku_circuit_project_free(j->project);
    if (v->window) {
        tiku_circuitview_menus(v);
    }
    if (v->dirty) {
        *v->dirty = 1;
    }
    tiku_circuitview_unref(v);
    free(j);
}

void tiku_circuitview_files_cancel(tiku_circuitview_t *v)
{
    v->load_generation++;
    v->needs_load = 0;
    v->requested[0] = 0;
    tiku_work_cancel(v->load_key);
}

void tiku_circuitview_file(tiku_circuitview_t *v, const char *path)
{
    if (!v || !v->window || !path || !*path) {
        return;
    }
    if (strlen(path) >= sizeof v->requested) {
        tiku_circuitview_note(v,
            "Path is too long. The drawing was not changed.");
        return;
    }
    /* Reload passes v->path, which changes only when an import lands. */
    tiku_circuitview_files_cancel(v);
    v->load_action = 0;
    snprintf(v->requested, sizeof v->requested, "%s", path);
    v->needs_load = 1;
    tiku_circuitview_note(v, "Opening drawing...");
    tiku_circuitview_menus(v);
}

void tiku_circuitview_picked(tiku_circuitview_t *v, const char *path)
{
    if (!v || !v->window) {
        return;
    }
    int action = v->picker_action;
    v->picker_action = 0;
    if (path && *path) {
        if (action == CIRCUIT_EXPORT) {
            cv_export(v,path);
        } else if (action == CIRCUIT_ADD_LAYER && !tiku_circuitview_layer_room(v)) {
            tiku_circuitview_note(v, tiku_circuit_layer_count(v->document) ?
                "The layer stack is full; the chosen file was not added." :
                "Add Layer needs an open artwork, drill or job drawing; the "
                "chosen file was not added.");
        } else {
            tiku_circuitview_file(v, path);
            v->load_action = action;
        }
    }
    tiku_circuitview_menus(v);
}

void tiku_circuitview_files_tick(tiku_circuitview_t *v)
{
    if (v->loading || !v->needs_load) {
        return;
    }
    tiku_rect_t content = tiku_window_content(v->window);
    tiku_circuitview_layout(v, content);
    import_job_t *j = calloc(1, sizeof *j);
    int stack = tiku_circuit_layer_count(v->document) > 0;
    if (j && v->load_action == CIRCUIT_RELOAD && v->project) {
        j->reload = 1;
        j->instance = strdup(
            tiku_circuit_project_sheet(v->project, v->sheet)->instance);
    }
    if (!j || (j->reload && !j->instance)) {
        free(j);
        v->needs_load = 0;
        tiku_circuitview_note(v,
            "Not enough memory to open the drawing. Drawing unchanged.");
        return;
    }
    j->owner = v;
    j->units = j->sheets = j->details = -1;
    snprintf(j->settings_dir,sizeof j->settings_dir,"%s",v->settings_dir);
    j->generation = v->load_generation;
    j->action = v->load_action;
    j->layers = j->action == CIRCUIT_ADD_LAYER ||
                (j->action == CIRCUIT_RELOAD && stack);
    if (j->action == CIRCUIT_RELOAD && stack) {
        j->sheet = v->sheet;
    }
    if (j->layers) {
        j->base = tiku_circuit_retain(v->document);
    }
    tiku_rect_t canvas = tiku_circuitview_next_canvas(v, content, &j->list);
    j->view = (tiku_circuit_view_t){canvas.w, canvas.h, 1, 0, 0};
    snprintf(j->path, sizeof j->path, "%s", v->requested);
    tiku_work_job_t work = {.run = run, .done = done, .free = dispose,
                            .ctx = j};
    memcpy(work.key, v->load_key, sizeof work.key);
    v->refs++;
    v->loading = 1;
    v->needs_load = 0;
    if (tiku_work_post(&work) < 0) {
        dispose(j);
        tiku_circuitview_note(v,
            "The worker queue is full. Try Open again. Drawing unchanged.");
    }
}
