/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_circuit_doc.c - a scene's life: made, shared and released, grown
 * one checked object at a time and described; and a small authored
 * circuit to try the viewer on without a file.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_circuit_private.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* The example's colours for wires and for components, and its stroke
 * width in millimetres. */
#define EXAMPLE_WIRE   0xff27846fu
#define EXAMPLE_PART   0xff9c4935u
#define EXAMPLE_STROKE 0.35

tiku_circuit_t *
circuit_new(void)
{
    tiku_circuit_t *d = calloc(1, sizeof *d);

    if (d) {
        atomic_init(&d->refs, 1);
        d->bytes = sizeof *d;
    }
    return d;
}

tiku_circuit_t *
tiku_circuit_retain(tiku_circuit_t *d)
{
    if (d) {
        atomic_fetch_add_explicit(&d->refs, 1, memory_order_relaxed);
    }
    return d;
}

void
tiku_circuit_release(tiku_circuit_t *d)
{
    if (!d || atomic_fetch_sub_explicit(&d->refs, 1,
                                        memory_order_acq_rel) != 1) {
        return;
    }
    if (d->layers) {
        for (size_t i = 0; i < d->layer_count; i++) {
            tiku_circuit_release(d->layers[i].scene);
            free((void *)d->layers[i].path);
            free((void *)d->layers[i].root);
            free((void *)d->layers[i].label);
        }
        free(d->layers);
        free(d);
        return;
    }
    for (size_t i = 0; i < d->count; i++) {
        free((void *)d->shapes[i].info.name);
        free((void *)d->shapes[i].info.value);
        free(d->shapes[i].points);
        free(d->shapes[i].text);
        free(d->shapes[i].details);
        tiku_circuit_release(d->shapes[i].group);
    }
    tiku_ttf_close(d->font);
    free(d->shapes);
    free(d);
}

void
circuit_bounds_add(tiku_circuit_t *d, tiku_circuit_box_t box)
{
    if (!d->count) {
        d->bounds = box;
    } else {
        d->bounds.x0 = fmin(d->bounds.x0, box.x0);
        d->bounds.y0 = fmin(d->bounds.y0, box.y0);
        d->bounds.x1 = fmax(d->bounds.x1, box.x1);
        d->bounds.y1 = fmax(d->bounds.y1, box.y1);
    }
}

tiku_circuit_err_t
circuit_add(tiku_circuit_t *d, const char *name, const char *value,
            const circuit_point_t *p, size_t n, double width,
            uint32_t colour, int closed, int fill)
{
    if (!d || d->layers || !p || !name || !value || n < 2 ||
        !isfinite(width) || width < 0 || width > CIRCUIT_WIDTH_MAX) {
        return TIKU_CIRCUIT_BAD;
    }
    if (atomic_load_explicit(&d->refs, memory_order_relaxed) != 1) {
        return TIKU_CIRCUIT_BAD;
    }
    size_t a = strnlen(name, CIRCUIT_NAME_BYTES);
    size_t b = strnlen(value, CIRCUIT_NAME_BYTES);

    if (a == CIRCUIT_NAME_BYTES || b == CIRCUIT_NAME_BYTES ||
        n > CIRCUIT_POINTS_MAX - d->points ||
        d->count >= CIRCUIT_OBJECTS_MAX) {
        return TIKU_CIRCUIT_LIMIT;
    }
    size_t bytes = n * sizeof *p + a + b + 2 + sizeof(circuit_shape_t);

    if (bytes > CIRCUIT_BYTES_MAX - d->bytes) {
        return TIKU_CIRCUIT_LIMIT;
    }
    tiku_circuit_box_t box = {p[0].x, p[0].y, p[0].x, p[0].y};

    for (size_t i = 0; i < n; i++) {
        if (!isfinite(p[i].x) || !isfinite(p[i].y) ||
            fabs(p[i].x) > CIRCUIT_COORD_MAX ||
            fabs(p[i].y) > CIRCUIT_COORD_MAX) {
            return TIKU_CIRCUIT_BAD;
        }
        box.x0 = fmin(box.x0, p[i].x);
        box.x1 = fmax(box.x1, p[i].x);
        box.y0 = fmin(box.y0, p[i].y);
        box.y1 = fmax(box.y1, p[i].y);
    }
    if (!fill) {
        box.x0 -= width / 2;
        box.y0 -= width / 2;
        box.x1 += width / 2;
        box.y1 += width / 2;
    }
    circuit_shape_t item = {.info = {NULL, NULL, box}, .count = n,
                            .width = width, .colour = colour,
                            .closed = !!closed, .fill = !!fill};

    item.info.name = strdup(name);
    item.info.value = strdup(value);
    item.points = malloc(n * sizeof *p);
    circuit_shape_t *items = NULL;

    if (item.info.name && item.info.value && item.points) {
        items = realloc(d->shapes, (d->count + 1) * sizeof *items);
    }
    if (!items) {
        free((void *)item.info.name);
        free((void *)item.info.value);
        free(item.points);
        return TIKU_CIRCUIT_NOMEM;
    }
    memcpy(item.points, p, n * sizeof *p);
    d->shapes = items;
    d->shapes[d->count] = item;
    circuit_bounds_add(d, box);
    d->count++;
    d->points += n;
    d->bytes += bytes;
    return TIKU_CIRCUIT_OK;
}

size_t
tiku_circuit_count(const tiku_circuit_t *d)
{
    return d ? d->count : 0;
}

size_t
tiku_circuit_storage_bytes(const tiku_circuit_t *d)
{
    return d ? d->bytes : 0;
}

const tiku_circuit_object_t *
tiku_circuit_object(const tiku_circuit_t *d, size_t i)
{
    return circuit_stack_child(&d, &i) ? &d->shapes[i].info : NULL;
}

const char *
tiku_circuit_details(const tiku_circuit_t *d, size_t i)
{
    return circuit_stack_child(&d, &i) && d->shapes[i].details
           ? d->shapes[i].details : "";
}

tiku_circuit_err_t
circuit_details(tiku_circuit_t *d, size_t i, const char *text)
{
    if (!d || d->layers || i >= d->count || !text ||
        atomic_load_explicit(&d->refs, memory_order_relaxed) != 1) {
        return TIKU_CIRCUIT_BAD;
    }
    size_t n = strnlen(text, CIRCUIT_DETAILS_MAX + 1);
    size_t old = d->shapes[i].details ? strlen(d->shapes[i].details) + 1
                                      : 0;

    if (n > CIRCUIT_DETAILS_MAX ||
        n + 1 > CIRCUIT_BYTES_MAX - (d->bytes - old)) {
        return TIKU_CIRCUIT_LIMIT;
    }
    char *copy = strdup(text);

    if (!copy) {
        return TIKU_CIRCUIT_NOMEM;
    }
    free(d->shapes[i].details);
    d->shapes[i].details = copy;
    d->bytes = d->bytes - old + n + 1;
    return TIKU_CIRCUIT_OK;
}

tiku_circuit_box_t
tiku_circuit_bounds(const tiku_circuit_t *d)
{
    return d ? d->bounds : (tiku_circuit_box_t){0};
}

const char *
tiku_circuit_warning(const tiku_circuit_t *d)
{
    return d ? d->warning : "";
}

const char *
tiku_circuit_error(tiku_circuit_err_t e)
{
    static const char *const errors[] = {
        "Ready", "Not a supported circuit document", "Invalid drawing data",
        "This feature is not implemented yet", "A required file is missing",
        "Drawing exceeds the configured limit", "Not enough memory",
        "The file could not be read", "Cancelled"
    };

    return (unsigned)e < sizeof errors / sizeof *errors
           ? errors[e] : "Unknown drawing error";
}

tiku_circuit_err_t
tiku_circuit_example(tiku_circuit_t **out)
{
    if (!out) {
        return TIKU_CIRCUIT_BAD;
    }
    tiku_circuit_t *d = circuit_new();

    if (!d) {
        return TIKU_CIRCUIT_NOMEM;
    }
    const circuit_point_t resistor[] = {{18, 12}, {20, 9}, {24, 15},
                                        {28, 9}, {32, 15}, {36, 9},
                                        {38, 12}};
    const circuit_point_t wire[] = {{8, 28}, {8, 12}, {18, 12}};
    const circuit_point_t upper[] = {{38, 12}, {54, 12}, {54, 24}};
    const circuit_point_t lower[] = {{54, 29}, {54, 40}, {8, 40}, {8, 32}};
    const circuit_point_t cap1[] = {{48, 24}, {60, 24}};
    const circuit_point_t cap2[] = {{48, 29}, {60, 29}};
    const circuit_point_t power1[] = {{3, 28}, {13, 28}};
    const circuit_point_t power2[] = {{5, 32}, {11, 32}};
    const struct {
        const char *name, *value;
        const circuit_point_t *p;
        size_t n;
        uint32_t c;
    } objects[] = {
        {"V1", "3.3 V supply", wire, 3, EXAMPLE_WIRE},
        {"OUT", "Output wire", upper, 3, EXAMPLE_WIRE},
        {"GND", "Return wire", lower, 4, EXAMPLE_WIRE},
        {"R1", "1 kohm", resistor, 7, EXAMPLE_PART},
        {"C1", "100 nF", cap1, 2, EXAMPLE_PART},
        {"C1", "100 nF", cap2, 2, EXAMPLE_PART},
        {"V1", "Positive terminal", power1, 2, EXAMPLE_PART},
        {"V1", "Negative terminal", power2, 2, EXAMPLE_PART}
    };

    for (size_t i = 0; i < sizeof objects / sizeof *objects; i++) {
        tiku_circuit_err_t e = circuit_add(d, objects[i].name,
                                           objects[i].value, objects[i].p,
                                           objects[i].n, EXAMPLE_STROKE,
                                           objects[i].c, 0, 0);

        if (e) {
            tiku_circuit_release(d);
            return e;
        }
    }
    *out = d;
    return TIKU_CIRCUIT_OK;
}
