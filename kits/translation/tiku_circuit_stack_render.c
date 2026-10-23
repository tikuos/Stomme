/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_circuit_stack_render.c - a layer stack drawn back to front: each
 * layer's own dark and clear coverage resolved first, then composited in
 * the layer's colour at its opacity.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_circuit_private.h"

#include <stdlib.h>

tiku_circuit_err_t
circuit_stack_render(const tiku_circuit_t *d, const tiku_circuit_view_t *v,
                     int (*cancel)(void *), void *ctx, tiku_image_t *out)
{
    /* An empty scene checks the view as a flat render does and gives the
     * paper the layers are composited onto. */
    tiku_circuit_t empty = {0};
    tiku_image_t im = {0};
    tiku_circuit_err_t e = circuit_render_flat(&empty, v, cancel, ctx, &im,
                                               0, 0);

    if (e) {
        return e;
    }
    for (size_t i = d->layer_count; i > 0; i--) {
        const tiku_circuit_layer_t *l = &d->layers[i - 1];

        if (!l->scene || !l->visible || !l->opacity) {
            continue;
        }
        tiku_image_t layer = {0};

        e = circuit_render_flat(l->scene, v, cancel, ctx, &layer, 1,
                                l->colour);
        if (e) {
            break;
        }
        for (int y = 0; y < im.h && !e; y++) {
            if (cancel && cancel(ctx)) {
                e = TIKU_CIRCUIT_CANCELLED;
                break;
            }
            for (int x = 0; x < im.w; x++) {
                size_t k = (size_t)y * im.w + x;
                unsigned a = ((layer.px[k] >> 24) * l->opacity + 127) / 255;
                uint32_t pixel = CIRCUIT_OPAQUE;

                for (int s = 0; s <= 16; s += 8) {
                    pixel |= ((((im.px[k] >> s) & 255) * (255 - a) +
                               ((layer.px[k] >> s) & 255) * a + 127) / 255)
                             << s;
                }
                im.px[k] = pixel;
            }
        }
        free(layer.px);
        if (e) {
            break;
        }
    }
    if (!e && cancel && cancel(ctx)) {
        e = TIKU_CIRCUIT_CANCELLED;
    }
    if (e) {
        free(im.px);
    } else {
        *out = im;
    }
    return e;
}
