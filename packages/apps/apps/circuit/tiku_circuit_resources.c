/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_circuit_resources.c - native drawing observations and validated view settings.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_circuit_window_private.h"
#include "tiku_viewer_resources.h"
#include <stdio.h>
#include <string.h>
static const char *const options[] = {"view/details", "view/sheets",
                                      "view/units", "view/layer"};
/** @brief Resolve one registered positional layer setting from its canonical path. */
static int layer_index(tiku_circuitview_t *v, const char *path)
{
    size_t count = tiku_circuit_layer_count(v->document);
    for (size_t i = 0; i < count; i++) {
        char name[80];
        snprintf(name, sizeof name, "view/layer/%zu", i);
        if (!strcmp(path, name))
            return i;
    }
    return -1;
}
/** @brief Copy a native layer's visible properties and its view-generation precondition. */
static tiku_msg_t *layer_value(tiku_circuitview_t *v, size_t index)
{
    const tiku_circuit_layer_t *layer = tiku_circuit_layer(v->document, index);
    if (!layer)
        return NULL;
    tiku_msg_t *value = tiku_app_resource_object(0);
    int bad = VR_COUNTER(value, "generation", v->generation) |
              VR_INT(value, "color", layer->colour) |
              VR_INT(value, "opacity", layer->opacity) |
              VR_BOOL(value, "visible", layer->visible);
    if (bad) {
        tiku_msg_free(value);
        return NULL;
    }
    return value;
}
/** @brief Capture native drawing selection, viewport, objects or runtime view settings. */
static tiku_resource_error_t observe(void *state, const char *field,
                                     tiku_msg_t **out)
{
    tiku_circuitview_t *v = state;
    tiku_msg_t *value = NULL;
    int bad = 0, index = layer_index(v, field);
    if (index >= 0)
        value = layer_value(v, index);
    else if (!strcmp(field, "view/details"))
        value = tiku_resource_value_integer(TIKU_RESOURCE_BOOLEAN,
                                            v->show_details);
    else if (!strcmp(field, "view/sheets"))
        value =
            tiku_resource_value_integer(TIKU_RESOURCE_BOOLEAN, v->show_sheets);
    else if (!strcmp(field, "view/units"))
        value = tiku_resource_value_integer(TIKU_RESOURCE_INTEGER, v->units);
    else if (!strcmp(field, "view/layer")) {
        value = tiku_app_resource_object(0);
        bad |= VR_INT(value, "count", tiku_circuit_layer_count(v->document));
        bad |= VR_TEXT(value, "scope", "native_layer_appearance");
    } else if (!strcmp(field, "selection")) {
        value = tiku_app_resource_object(0);
        bad |= VR_BOOL(value, "selected", v->selected != SIZE_MAX);
        if (v->selected != SIZE_MAX)
            bad |= VR_COUNTER(value, "object", v->selected);
        bad |= VR_COUNTER(value, "generation", v->generation);
        bad |= VR_COUNTER(value, "sheet", v->sheet);
    } else if (!strcmp(field, "view")) {
        value = tiku_app_resource_object(0);
        bad |= VR_NUMBER(value, "scale", v->view.scale);
        bad |= VR_NUMBER(value, "x", v->view.x);
        bad |= VR_NUMBER(value, "y", v->view.y);
        bad |= VR_INT(value, "units", v->units);
        bad |= VR_BOOL(value, "sheets", v->show_sheets);
        bad |= VR_BOOL(value, "details", v->show_details);
    } else if (!strcmp(field, "objects")) {
        value = tiku_app_resource_object(0);
        bad |= VR_COUNTER(value, "count", tiku_circuit_count(v->document));
        bad |= VR_INT(value, "layers", tiku_circuit_layer_count(v->document));
    } else
        return TIKU_RESOURCE_UNSUPPORTED;
    if (bad || !value) {
        tiku_msg_free(value);
        return TIKU_RESOURCE_UNAVAILABLE;
    }
    *out = value;
    return 0;
}
/** @brief Enumerate native scalar options and separately bounded layer settings. */
static int option(void *state, size_t index, tiku_resource_initial_t *out)
{
    tiku_circuitview_t *v = state;
    size_t count = tiku_circuit_layer_count(v->document);
    if (index >= 4 + count)
        return 0;
    static char path[80];
    if (index < 4)
        snprintf(path, sizeof path, "%s", options[index]);
    else
        snprintf(path, sizeof path, "view/layer/%zu", index - 4);
    tiku_msg_t *value = NULL;
    if (observe(v, path, &value))
        return -1;
    int result = tiku_app_resource_initial(
        out, path,
        index == 3 ? TIKU_RESOURCE_COLLECTION : TIKU_RESOURCE_SETTING,
        index < 2    ? TIKU_RESOURCE_BOOLEAN
        : index == 2 ? TIKU_RESOURCE_INTEGER
                     : TIKU_RESOURCE_OBJECT,
        value);
    if (index == 2) {
        out->spec.schema.integer_min = 0;
        out->spec.schema.integer_max = 2;
    }
    return result;
}
/** @brief Apply runtime view choices through native actions and checked layer replacement. */
static tiku_resource_error_t change(void *state, const char *path,
                                    const tiku_msg_t *value, int invoke,
                                    tiku_resource_snapshot_t *out,
                                    tiku_resource_effect_t *effect)
{
    tiku_circuitview_t *v = state;
    if (invoke)
        return TIKU_RESOURCE_UNSUPPORTED;
    if (v->loading || v->needs_load || v->style_open)
        return TIKU_RESOURCE_BUSY;
    int index = layer_index(v, path);
    int command = 0;
    int64_t number = 0;
    if (index >= 0) {
        const char *keys[] = {"generation", "color", "opacity", "visible"};
        uint64_t generation;
        int64_t colour, opacity, visible;
        tiku_msg_t *shown = tiku_app_resource_find(value, "visible");
        int valid =
            tiku_app_resource_scalar(shown, TIKU_RESOURCE_BOOLEAN, &visible);
        tiku_msg_free(shown);
        if (!valid || !tiku_app_resource_keys(value, keys, 4) ||
            !tiku_app_resource_counter(value, "generation", &generation) ||
            !tiku_app_resource_integer(value, "color", 0, UINT32_MAX,
                                       &colour) ||
            !tiku_app_resource_integer(value, "opacity", 0, 255, &opacity))
            return TIKU_RESOURCE_VALUE;
        if (generation != v->generation)
            return TIKU_RESOURCE_STALE;
        if (!cv_layer_replace(v, index, generation, (uint32_t)colour,
                              (unsigned)opacity, (int)visible))
            return TIKU_RESOURCE_APPLY_FAILED;
    } else if (!strcmp(path, "view/details") || !strcmp(path, "view/sheets")) {
        if (!tiku_app_resource_scalar(value, TIKU_RESOURCE_BOOLEAN, &number))
            return TIKU_RESOURCE_VALUE;
        int details = !strcmp(path, "view/details"),
            current = details ? v->show_details : v->show_sheets;
        command = details ? CIRCUIT_DETAILS : CIRCUIT_SHEETS;
        if (!tiku_circuitview_enabled(v, command))
            return TIKU_RESOURCE_UNSUPPORTED;
        if (current != number)
            tiku_circuitview_action(v, command);
    } else if (!strcmp(path, "view/units")) {
        if (!tiku_app_resource_scalar(value, TIKU_RESOURCE_INTEGER, &number) ||
            number < 0 || number > 2)
            return TIKU_RESOURCE_VALUE;
        command = CIRCUIT_UNIT_MM + (int)number;
        if (!tiku_circuitview_enabled(v, command))
            return TIKU_RESOURCE_UNSUPPORTED;
        tiku_circuitview_action(v, command);
    } else
        return TIKU_RESOURCE_UNSUPPORTED;
    tiku_msg_t *observed = NULL;
    tiku_resource_error_t error = observe(v, path, &observed);
    if (error)
        return error;
    *effect = TIKU_RESOURCE_EFFECT_PARTIAL;
    return tiku_app_resource_snapshot(out, observed, "native_runtime_view")
               ? TIKU_RESOURCE_NO_MEMORY
               : 0;
}
vr_document_t tiku_circuitview_resource_document(tiku_circuitview_t *v)
{
    return (vr_document_t){.identity = v->resource_document + 1,
                           .generation = v->generation,
                           .path = v->path,
                           .type = "circuit",
                           .available = v->document != NULL,
                           .busy = v->loading || v->needs_load,
                           .read_only = 1,
                           .controller = v,
                           .observe = observe,
                           .option = option,
                           .change = change};
}
