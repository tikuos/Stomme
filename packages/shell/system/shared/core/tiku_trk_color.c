/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_color.c - reading a dropped colour.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_trk_color.h"
#include <string.h>

int
tiku_trk_color_parse(const void *data, unsigned size, tiku_trk_color_t *out)
{
    if (data == NULL || out == NULL || size < sizeof *out) return -1;
    memcpy(out, data, sizeof *out);
    if (out->a == 0) out->a = 255;
    return 0;
}

void
tiku_trk_color_set(tiku_trk_color_t *desktop, const tiku_trk_color_t *color)
{ if (desktop != NULL && color != NULL) *desktop = *color; }
