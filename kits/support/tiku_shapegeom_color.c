/*
 * KintsugiDesktop v0.01 Support Kit.
 * Authors: Ambuj Varshney <ambuj@kinstugi-os.org>
 * tiku_shapegeom_color.c - DrawingML path lightening and darkening.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_shapegeom.h"
uint32_t
tiku_shapegeom_tone(uint32_t color, unsigned mode)
{
    double amount = mode == TIKU_SHAPEGEOM_PATH_LIGHTEN ||
                            mode == TIKU_SHAPEGEOM_PATH_DARKEN
                        ? .4
                    : mode == TIKU_SHAPEGEOM_PATH_LIGHTEN_LESS ||
                            mode == TIKU_SHAPEGEOM_PATH_DARKEN_LESS
                        ? .2
                        : 0;
    double target = mode == TIKU_SHAPEGEOM_PATH_LIGHTEN ||
                            mode == TIKU_SHAPEGEOM_PATH_LIGHTEN_LESS
                        ? 255
                        : 0;
    uint32_t out = color & 0xff000000u;
    for (unsigned k = 0; k < 24; k += 8) {
        double value = (color >> k) & 255;
        out |= (uint32_t)(value + (target - value) * amount + .5) << k;
    }
    return out;
}
