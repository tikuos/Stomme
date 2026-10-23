/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_circuit_job.c - a Gerber job opened: its file list read, each file
 * fetched through the caller's resolver, which keeps reads confined, and
 * decoded, and the scenes stacked in the job's order.  A file that cannot
 * be read or decoded is an unavailable layer; allocation failure,
 * cancellation and the job's own budgets fail the whole job.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_circuit_job_private.h"
#include "tiku_circuit_private.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define JOB_LABEL_MAX  256          /* a label's bytes; a longer one reads
                                       "Job file (see path)"             */
#define JOB_PATH_SHOWN 128          /* path bytes in a job's diagnostic  */
#define LAYER_OPAQUE   255

/** @brief Set @p d, when not NULL, to error @p e with message @p s.
 *  @return @p e. */
static tiku_circuit_err_t
diagnostic(tiku_circuit_diagnostic_t *d, tiku_circuit_err_t e, const char *s)
{
    if (d) {
        d->code = e;
        snprintf(d->message, sizeof d->message, "%s", s);
    }
    return e;
}

/** @brief Set @p diag, when not NULL, to entry @p path's failure @p ld:
 *         its code, and a message of the path (its first JOB_PATH_SHOWN
 *         bytes at most, cut between characters), the line when there is
 *         one, and @p ld's message. */
static void
entry_failed(tiku_circuit_diagnostic_t *diag, const char *path,
             const tiku_circuit_diagnostic_t *ld)
{
    size_t shown = strlen(path);

    if (!diag) {
        return;
    }
    if (shown > JOB_PATH_SHOWN) {
        shown = JOB_PATH_SHOWN;
        while (shown && ((unsigned char)path[shown] & 0xc0) == 0x80) {
            shown--;                /* back to a character's first byte */
        }
    }
    diag->code = ld->code;
    if (ld->line) {
        snprintf(diag->message, sizeof diag->message, "%.*s, line %u: %s",
                 (int)shown, path, ld->line, ld->message);
    } else {
        snprintf(diag->message, sizeof diag->message, "%.*s: %s",
                 (int)shown, path, ld->message);
    }
}

tiku_circuit_err_t
tiku_circuit_job_open(const char *root, const tiku_circuit_import_t *options,
                      const tiku_circuit_resolver_t *resolver,
                      tiku_circuit_t **out, tiku_circuit_diagnostic_t *diag)
{
    if (diag) {
        memset(diag, 0, sizeof *diag);
    }
    if (!root || !out || !resolver || !resolver->read || !resolver->release) {
        return diagnostic(diag, TIKU_CIRCUIT_BAD,
                          "Missing job resolver or output");
    }
    circuit_job_list_t *list = calloc(1, sizeof *list);
    tiku_circuit_layer_t *layers = calloc(TIKU_CIRCUIT_LAYERS_MAX,
                                          sizeof *layers);

    if (!list || !layers) {
        free(list);
        free(layers);
        return diagnostic(diag, TIKU_CIRCUIT_NOMEM,
                          "Cannot allocate job metadata");
    }
    tiku_circuit_source_t source = {0};
    char parent[sizeof source.path] = {0};
    tiku_circuit_err_t error = resolver->read(resolver->ctx, NULL, root,
                                              &source, diag);

    if (!error) {
        if (strnlen(source.path, sizeof source.path) == sizeof source.path
            || !source.path[0]) {
            error = TIKU_CIRCUIT_BAD;
        } else {
            snprintf(parent, sizeof parent, "%s", source.path);
            error = circuit_job_parse(source.data, source.bytes, options,
                                      list, diag);
        }
    }
    size_t budget = source.bytes, scene_bytes = 0;

    resolver->release(resolver->ctx, &source);
    for (size_t i = 0; !error && i < list->count; i++) {
        circuit_job_entry_t *entry = &list->entries[i];
        tiku_circuit_layer_t *layer = &layers[i];

        layer->path = entry->path;
        layer->root = root;
        layer->label = *entry->function ? entry->function : entry->path;
        if (strlen(layer->label) >= JOB_LABEL_MAX) {
            layer->label = "Job file (see path)";
        }
        layer->colour = tiku_circuit_layer_colour(i);
        layer->opacity = LAYER_OPAQUE;
        layer->visible = 1;
        layer->format = !*entry->format ? TIKU_CIRCUIT_LAYER_DETECT
            : !strcmp(entry->format, "Gerber") ? TIKU_CIRCUIT_LAYER_GERBER
            : (!strcmp(entry->format, "XNC") || !strcmp(entry->format, "NC"))
              ? TIKU_CIRCUIT_LAYER_DRILL
            : TIKU_CIRCUIT_LAYER_UNSUPPORTED;
        snprintf(layer->attributes, sizeof layer->attributes,
                 "Job function: %s\n"
                 "Job polarity: %s\n"
                 "Job format: %s",
                 *entry->function ? entry->function : "not specified",
                 *entry->polarity ? entry->polarity : "not specified",
                 *entry->format ? entry->format
                                : "not specified (detected from content)");
        if (options && options->cancel && options->cancel(options->ctx)) {
            error = TIKU_CIRCUIT_CANCELLED;
            break;
        }
        if (*entry->format && strcmp(entry->format, "Gerber")
            && strcmp(entry->format, "XNC") && strcmp(entry->format, "NC")) {
            diagnostic(&layer->diagnostic, TIKU_CIRCUIT_UNSUPPORTED,
                       "Unsupported job FileFormat");
            continue;
        }
        tiku_circuit_diagnostic_t *ld = &layer->diagnostic;
        int fatal = 0;              /* a job budget is spent */
        tiku_circuit_err_t e;

        /* Cleared, so a read that fails without filling it hands release
         * nothing stale. */
        memset(&source, 0, sizeof source);
        e = resolver->read(resolver->ctx, parent, entry->path, &source, ld);
        if (!e) {
            if (source.bytes > TIKU_CIRCUIT_STACK_BYTES - budget) {
                e = diagnostic(ld, TIKU_CIRCUIT_LIMIT,
                               "Job files exceed the 64 MiB read budget");
                fatal = 1;
            } else {
                budget += source.bytes;
                size_t at = 0;

                /* A drill file opens with a comment or M48. */
                while (at < source.bytes
                       && strchr(" \t\r\n", source.data[at])
                       && source.data[at]) {
                    at++;
                }
                int drill = at < source.bytes && (source.data[at] == ';'
                                                  || source.data[at] == 'M');

                if ((*entry->format && !strcmp(entry->format, "Gerber")
                     && drill)
                    || (*entry->format && strcmp(entry->format, "Gerber")
                        && !drill)) {
                    e = diagnostic(ld, TIKU_CIRCUIT_FORMAT,
                                   "Declared job format conflicts with file "
                                   "content");
                } else {
                    e = drill ? tiku_circuit_drill(source.data, source.bytes,
                                                   options, &layer->scene, ld)
                              : tiku_circuit_gerber(source.data, source.bytes,
                                                    options, &layer->scene,
                                                    ld);
                }
            }
        }
        resolver->release(resolver->ctx, &source);
        if (layer->scene) {
            size_t n = tiku_circuit_storage_bytes(layer->scene);

            if (n > TIKU_CIRCUIT_STACK_BYTES - scene_bytes) {
                e = diagnostic(ld, TIKU_CIRCUIT_LIMIT,
                               "Job scenes exceed the 64 MiB budget");
                fatal = 1;
            } else {
                scene_bytes += n;
            }
        }
        if (e && !ld->code) {
            diagnostic(ld, e, tiku_circuit_error(e));
        }
        /* A file over its own limits is an unavailable layer, as is one
         * that will not read or decode. */
        if (fatal || e == TIKU_CIRCUIT_NOMEM || e == TIKU_CIRCUIT_CANCELLED) {
            error = e;
            entry_failed(diag, entry->path, ld);
        }
    }
    tiku_circuit_t *result = NULL;

    if (!error) {
        error = tiku_circuit_stack(layers, list->count, &result);
    }
    if (!error && options && options->cancel
        && options->cancel(options->ctx)) {
        error = TIKU_CIRCUIT_CANCELLED;
    }
    if (!error) {
        *out = result;
    } else {
        tiku_circuit_release(result);
    }
    for (size_t i = 0; i < list->count; i++) {
        tiku_circuit_release(layers[i].scene);
    }
    free(layers);
    free(list);
    if (error && diag && !diag->code) {
        diagnostic(diag, error, tiku_circuit_error(error));
    }
    return error;
}
