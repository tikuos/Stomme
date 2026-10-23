/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_circuit_stack.c - layer stacks built and read: descriptors copied
 * whenever a stack changes, so a background render keeps an unchanging
 * snapshot, and a stack's object indices mapped onto its visible layers.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_circuit_private.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Bytes of a layer's path or job root, its terminating zero included. */
#define LAYER_PATH_BYTES  4096

uint32_t
tiku_circuit_layer_colour(size_t i)
{
    const uint32_t palette[] = {CIRCUIT_COPPER, 0xff2975b6, 0xff349366,
                                0xffad4b75, 0xff7160aa, 0xff30343b};

    return palette[i % (sizeof palette / sizeof *palette)];
}

size_t
tiku_circuit_layer_count(const tiku_circuit_t *d)
{
    return d ? d->layer_count : 0;
}

const tiku_circuit_layer_t *
tiku_circuit_layer(const tiku_circuit_t *d, size_t i)
{
    return d && i < d->layer_count ? &d->layers[i] : NULL;
}

int
tiku_circuit_stack_incomplete(const tiku_circuit_t *d)
{
    if (d) {
        for (size_t i = 0; i < d->layer_count; i++) {
            if (!d->layers[i].scene) {
                return 1;
            }
        }
    }
    return 0;
}

size_t
tiku_circuit_object_layer(const tiku_circuit_t *d, size_t object)
{
    if (d) {
        for (size_t i = 0; i < d->layer_count; i++) {
            const tiku_circuit_layer_t *l = &d->layers[i];
            size_t n = l->scene && l->visible && l->opacity
                       ? l->scene->count : 0;

            if (object < n) {
                return i;
            }
            object -= n;
        }
    }
    return SIZE_MAX;
}

int
circuit_stack_child(const tiku_circuit_t **doc, size_t *index)
{
    const tiku_circuit_t *d = *doc;

    if (!d) {
        return 0;
    }
    if (!d->layer_count) {
        return *index < d->count;
    }
    for (size_t i = 0; i < d->layer_count; i++) {
        const tiku_circuit_layer_t *l = &d->layers[i];
        size_t n = l->scene && l->visible && l->opacity ? l->scene->count
                                                        : 0;

        if (*index < n) {
            *doc = l->scene;
            return 1;
        }
        *index -= n;
    }
    return 0;
}

tiku_circuit_err_t
tiku_circuit_stack(const tiku_circuit_layer_t *layers, size_t count,
                   tiku_circuit_t **out)
{
    if (!layers || !out || !count) {
        return TIKU_CIRCUIT_BAD;
    }
    if (count > TIKU_CIRCUIT_LAYERS_MAX) {
        return TIKU_CIRCUIT_LIMIT;
    }
    size_t bytes = sizeof(tiku_circuit_t) + count * sizeof *layers;

    for (size_t i = 0; i < count; i++) {
        const tiku_circuit_layer_t *l = &layers[i];

        if (!l->path || !l->root || !l->label || l->opacity > 255 ||
            (unsigned)l->format > TIKU_CIRCUIT_LAYER_UNSUPPORTED ||
            (!l->scene && !l->diagnostic.code) ||
            (l->scene && (l->scene->layer_count || l->scene->font))) {
            return TIKU_CIRCUIT_BAD;
        }
        if (strnlen(l->path, LAYER_PATH_BYTES) == LAYER_PATH_BYTES ||
            strnlen(l->root, LAYER_PATH_BYTES) == LAYER_PATH_BYTES ||
            strnlen(l->label, TIKU_CIRCUIT_LABEL_BYTES) ==
            TIKU_CIRCUIT_LABEL_BYTES) {
            return TIKU_CIRCUIT_LIMIT;
        }
        if (strnlen(l->attributes, sizeof l->attributes) ==
            sizeof l->attributes ||
            strnlen(l->diagnostic.message, sizeof l->diagnostic.message) ==
            sizeof l->diagnostic.message) {
            return TIKU_CIRCUIT_BAD;
        }
        size_t add = strlen(l->path) + strlen(l->root) + strlen(l->label) +
                     3 + (l->scene ? l->scene->bytes : 0);

        if (add > TIKU_CIRCUIT_STACK_BYTES - bytes) {
            return TIKU_CIRCUIT_LIMIT;
        }
        bytes += add;
    }
    tiku_circuit_t *d = circuit_new();

    if (!d) {
        return TIKU_CIRCUIT_NOMEM;
    }
    d->layers = calloc(count, sizeof *d->layers);
    if (!d->layers) {
        tiku_circuit_release(d);
        return TIKU_CIRCUIT_NOMEM;
    }
    d->layer_count = count;
    int bounded = 0;

    for (size_t i = 0; i < count; i++) {
        tiku_circuit_layer_t *l = &d->layers[i];

        *l = layers[i];
        l->scene = tiku_circuit_retain(l->scene);
        l->path = strdup(l->path);
        l->root = strdup(l->root);
        l->label = strdup(l->label);
        if (!l->path || !l->root || !l->label) {
            tiku_circuit_release(d);
            return TIKU_CIRCUIT_NOMEM;
        }
        if (l->scene && l->visible && l->opacity) {
            if (l->scene->count) {
                if (!bounded) {
                    d->bounds = l->scene->bounds;
                } else {
                    size_t saved = d->count;

                    d->count = 1;
                    circuit_bounds_add(d, l->scene->bounds);
                    d->count = saved;
                }
                bounded = 1;
            }
            d->count += l->scene->count;
        }
    }
    d->bytes = bytes;
    snprintf(d->warning, sizeof d->warning,
             "Layer stack: %zu entries%s. Shared source origin; drill "
             "overlays do not remove copper. Inspection only.",
             count, tiku_circuit_stack_incomplete(d)
                    ? " (incomplete: unavailable layers)" : "");
    if (count == 1 && layers[0].scene) {
        snprintf(d->warning, sizeof d->warning, "%s",
                 tiku_circuit_warning(layers[0].scene));
    }
    *out = d;
    return TIKU_CIRCUIT_OK;
}
