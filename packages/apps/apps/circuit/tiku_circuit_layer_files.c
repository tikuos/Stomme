/* tiku_circuit_layer_files.c -- a layer stack's files read again on a
 * worker: one file added at the front, or every layer reread for Reload.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#include "tiku_circuit_window_private.h"
#include "tiku_circuit_source.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * @brief Read one layer's file into a scene: a job's entry relative to
 *        its job file, an added file by its own path.
 *
 * The bytes read are charged to @p budget.  The declared format must
 * agree with the content, and a schematic or a job is refused; a failure
 * names the file in @p diag.
 */
static tiku_circuit_err_t read_layer(const tiku_circuit_layer_t *l,
    const tiku_circuit_import_t *options, tiku_circuit_t **out,
    tiku_circuit_diagnostic_t *diag, size_t *budget)
{
    if (l->format == TIKU_CIRCUIT_LAYER_UNSUPPORTED) {
        return TIKU_CIRCUIT_UNSUPPORTED;
    }
    tiku_circuitview_local_t local = {.directory = -1,
                                      .cancel = options->cancel,
                                      .ctx = options->ctx};
    tiku_circuit_resolver_t resolver = tiku_circuitview_local_resolver(&local);
    tiku_circuit_source_t source = {0};
    char parent[sizeof source.path];
    tiku_circuit_err_t e;
    if (*l->root) {
        e = resolver.read(resolver.ctx, NULL, l->root, &source, diag);
        snprintf(parent, sizeof parent, "%s", source.path);
        resolver.release(resolver.ctx, &source);
        if (e) {
            goto done;
        }
        e = resolver.read(resolver.ctx, parent, l->path, &source, diag);
    } else {
        e = resolver.read(resolver.ctx, NULL, l->path, &source, diag);
    }
    if (!e) {
        if (source.bytes > TIKU_CIRCUIT_STACK_BYTES - *budget) {
            e = TIKU_CIRCUIT_LIMIT;
        } else {
            *budget += source.bytes;
            size_t at = 0;
            while (at < source.bytes && source.data[at] &&
                   strchr(" \t\r\n", source.data[at])) {
                at++;
            }
            int drill = at < source.bytes &&
                        (source.data[at] == ';' || source.data[at] == 'M');
            if ((l->format == TIKU_CIRCUIT_LAYER_GERBER && drill) ||
                (l->format == TIKU_CIRCUIT_LAYER_DRILL && !drill)) {
                e = TIKU_CIRCUIT_FORMAT;
                snprintf(diag->message, sizeof diag->message,
                         "Declared job format conflicts with file content");
            } else if (at < source.bytes && (source.data[at] == '(' ||
                                             source.data[at] == '{')) {
                e = TIKU_CIRCUIT_UNSUPPORTED;
            } else if (drill) {
                e = tiku_circuit_drill(source.data, source.bytes, options,
                                       out, diag);
            } else {
                e = tiku_circuit_gerber(source.data, source.bytes, options,
                                        out, diag);
            }
        }
    }
    resolver.release(resolver.ctx, &source);
done:
    tiku_circuitview_local_close(&local);
    if (e) {
        char message[sizeof diag->message];
        snprintf(message, sizeof message, "%.80s: %.160s", l->path,
                 *diag->message ? diag->message : tiku_circuit_error(e));
        diag->code = e;
        snprintf(diag->message, sizeof diag->message, "%s", message);
    }
    return e;
}

tiku_circuit_err_t tiku_circuitview_layer_load(const tiku_circuit_t *base,
    int add, const char *path, const tiku_circuit_import_t *options,
    tiku_circuit_t **out, tiku_circuit_diagnostic_t *diag)
{
    size_t old = tiku_circuit_layer_count(base);
    size_t count = old + (add ? 1 : 0);
    size_t budget = 0;
    size_t scene_bytes = 0;

    if (!old) {
        diag->code = TIKU_CIRCUIT_UNSUPPORTED;
        snprintf(diag->message, sizeof diag->message, "Add Layer needs an "
                 "open artwork, drill or job drawing");
        return diag->code;
    }
    if (count > TIKU_CIRCUIT_LAYERS_MAX) {
        diag->code = TIKU_CIRCUIT_LIMIT;
        snprintf(diag->message, sizeof diag->message,
                 "A layer stack holds at most %u layers",
                 TIKU_CIRCUIT_LAYERS_MAX);
        return diag->code;
    }
    tiku_circuit_layer_t *layers = calloc(count, sizeof *layers);
    if (!layers) {
        return TIKU_CIRCUIT_NOMEM;
    }
    for (size_t i = 0; i < old; i++) {
        tiku_circuit_layer_t *layer = &layers[i + (add ? 1 : 0)];
        *layer = *tiku_circuit_layer(base, i);
        layer->scene = add ? tiku_circuit_retain(layer->scene) : NULL;
    }
    if (add) {
        const char *name = strrchr(path, '/');
        name = name ? name + 1 : path;
        layers[0] = (tiku_circuit_layer_t){.path = path, .root = "",
            .label = strlen(name) < TIKU_CIRCUIT_LABEL_BYTES ?
                     name : "Added layer",
            .colour = tiku_circuit_layer_colour(old),
            .opacity = CV_OPAQUE, .visible = 1};
    }
    tiku_circuit_err_t e = TIKU_CIRCUIT_OK;
    for (size_t i = 0; i < count; i++) {
        if (!add || !i) {
            memset(&layers[i].diagnostic, 0, sizeof layers[i].diagnostic);
            e = read_layer(&layers[i], options, &layers[i].scene, diag,
                           &budget);
            if (e) {
                break;
            }
        }
        size_t n = tiku_circuit_storage_bytes(layers[i].scene);
        if (n > TIKU_CIRCUIT_STACK_BYTES - scene_bytes) {
            e = TIKU_CIRCUIT_LIMIT;
            break;
        }
        scene_bytes += n;
    }
    if (!e) {
        e = tiku_circuit_stack(layers, count, out);
    }
    for (size_t i = 0; i < count; i++) {
        tiku_circuit_release(layers[i].scene);
    }
    free(layers);
    return e;
}
