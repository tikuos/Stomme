/* tiku_circuit_layers.c -- the Layers menu: each command builds a new
 * stack from changed copies of the layer descriptors, so a render still
 * running keeps the stack it was given.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#include "tiku_circuit_window_private.h"
#include <stdio.h>
#include <string.h>

#define CV_OPACITY_75 192u     /* Cycle Layer Opacity's steps, after */
#define CV_OPACITY_50 128u     /* which it returns to CV_OPAQUE */
#define CV_OPACITY_25 64u

size_t tiku_circuitview_rows(const tiku_circuitview_t *v)
{
    return v->project ? tiku_circuit_project_count(v->project) :
                        tiku_circuit_layer_count(v->document);
}

int tiku_circuitview_layer_room(const tiku_circuitview_t *v)
{
    size_t n = tiku_circuit_layer_count(v->document);

    return n > 0 && n < TIKU_CIRCUIT_LAYERS_MAX;
}

/** @brief The palette's colour after @p colour, wrapping at its end; a
 *         colour the palette does not hold is followed by its second. */
static uint32_t next_colour(uint32_t colour)
{
    size_t i = 0;

    while (tiku_circuit_layer_colour(i) != colour) {
        i++;
        if (tiku_circuit_layer_colour(i) == tiku_circuit_layer_colour(0)) {
            break;
        }
    }
    return tiku_circuit_layer_colour(i + 1);
}

/** @brief Show a new stack made from @p layers, row @p selected chosen;
 *         with @p named, the front layer's path becomes the document's.
 *  @return 1, or 0 with the error said when the stack cannot be made. */
static int publish(tiku_circuitview_t *v, tiku_circuit_layer_t *layers,
                   size_t count, size_t selected, int named)
{
    tiku_circuit_t *replacement = NULL;
    tiku_circuit_err_t e = tiku_circuit_stack(layers, count, &replacement);
    if (e) { tiku_circuitview_note(v,tiku_circuit_error(e)); return 0; }
    tiku_circuit_release(v->document);
    v->document = replacement;
    if (named) snprintf(v->path,sizeof v->path,"%s",
                        tiku_circuit_layer(replacement,0)->path);
    v->sheet=selected; v->selected=SIZE_MAX;
    tiku_image_free(&v->image);
    tiku_list_set_count(&v->sheets,(int)count);
    tiku_list_select_only(&v->sheets,(int)selected);
    tiku_circuitview_changed(v);
    tiku_circuitview_ready(v);
    tiku_circuitview_menus(v);
    return 1;
}

int cv_layer_style(tiku_circuitview_t *v, uint32_t colour, unsigned opacity)
{
    size_t count=tiku_circuit_layer_count(v->document);
    if (!count || v->sheet>=count || opacity>255 ||
        v->loading || v->needs_load) return 0;
    tiku_circuit_layer_t layers[TIKU_CIRCUIT_LAYERS_MAX];
    for (size_t i=0;i<count;i++) layers[i]=*tiku_circuit_layer(v->document,i);
    layers[v->sheet].colour=0xff000000u|(colour&0xffffffu);
    layers[v->sheet].opacity=opacity;
    return publish(v,layers,count,v->sheet,0);
}

void tiku_circuitview_layers_action(tiku_circuitview_t *v, int command)
{
    size_t count = tiku_circuit_layer_count(v->document);
    size_t selected = v->sheet;

    if (!count || selected >= count) {
        return;
    }
    tiku_circuit_layer_t layers[TIKU_CIRCUIT_LAYERS_MAX];
    for (size_t i = 0; i < count; i++) {
        layers[i] = *tiku_circuit_layer(v->document, i);
    }
    tiku_circuit_layer_t *l = &layers[selected];
    /* A stack opened from one file is named after it; when that layer is
     * removed, the stack takes the name of its new front layer. */
    int named = command == CIRCUIT_LAYER_REMOVE && !*l->root &&
                !strcmp(l->path, v->path);
    if (command == CIRCUIT_LAYER_ISOLATE ||
        command == CIRCUIT_LAYER_SHOW_ALL) {
        for (size_t i=0;i<count;i++)
            layers[i].visible=command==CIRCUIT_LAYER_SHOW_ALL || i==selected;
    } else if (command == CIRCUIT_LAYER_VISIBLE) {
        l->visible = !l->visible;
    } else if (command == CIRCUIT_LAYER_COLOUR) {
        l->colour = next_colour(l->colour);
    } else if (command == CIRCUIT_LAYER_OPACITY) {
        l->opacity = l->opacity > CV_OPACITY_75 ? CV_OPACITY_75 :
                     l->opacity > CV_OPACITY_50 ? CV_OPACITY_50 :
                     l->opacity > CV_OPACITY_25 ? CV_OPACITY_25 :
                     CV_OPAQUE;
    } else if (command == CIRCUIT_LAYER_UP ||
               command == CIRCUIT_LAYER_DOWN) {
        size_t next = command == CIRCUIT_LAYER_UP ? selected - 1 :
                                                    selected + 1;
        if (next >= count) {
            return;
        }
        tiku_circuit_layer_t tmp = layers[next];
        layers[next] = *l;
        *l = tmp;
        selected = next;
    } else if (command == CIRCUIT_LAYER_REMOVE) {
        if (count == 1) {
            tiku_circuitview_action(v, CIRCUIT_EMPTY);
            return;
        }
        for (size_t i = selected; i + 1 < count; i++) {
            layers[i] = layers[i + 1];
        }
        count--;
        if (selected >= count) {
            selected = count - 1;
        }
    } else {
        return;
    }
    (void)publish(v,layers,count,selected,named);
}
