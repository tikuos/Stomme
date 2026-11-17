/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * main.c - deterministic host panel with optional PPM output.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "panel.h"
#include "tiku_test_pool.h"
#include "tiku_memory_presenter.h"
#include <stdio.h>

static _Alignas(64) unsigned char arena[2*1024*1024], pixels[800*480*2];
static tiku_test_pool_slot_t slots[32];

/** @brief Run the local panel and optionally write its RGB565 preview as PPM. */
int main(int argc, char **argv)
{
    tiku_test_pool_t pool;
    tiku_portable_panel_t panel;
    tiku_memory_presenter_t sink = {pixels, sizeof pixels, 1600, NULL, 0, TIKU_PRESENT_OK};
    if (!tiku_test_pool_init(&pool, arena, sizeof arena, slots, 32) ||
        !tiku_portable_panel_init(&panel, tiku_test_pool_allocator(&pool))) return 1;
    if (tiku_portable_panel_render(&panel, tiku_memory_presenter(&sink)) != TIKU_PRESENT_OK ||
        !tiku_memory_presenter_finish(&sink, panel.frame)) return 2;
    if (argc == 2) {
        FILE *file = fopen(argv[1], "wb");
        if (!file) return 3;
        fprintf(file, "P6\n800 480\n255\n");
        for (size_t i = 0; i < 800*480; ++i) {
            unsigned rgb = pixels[2*i] | ((unsigned)pixels[2*i+1] << 8);
            unsigned char output[3] = {(unsigned char)(((rgb >> 11) & 31) * 255 / 31),
                (unsigned char)(((rgb >> 5) & 63) * 255 / 63), (unsigned char)((rgb & 31) * 255 / 31)};
            if (fwrite(output, 1, 3, file) != 3) { fclose(file); return 4; }
        }
        if (fclose(file)) return 5;
    }
    printf("panel=800x480 frames=%zu peak_bytes=%zu\n", sink.frames, pool.peak);
    if (!tiku_portable_panel_stop(&panel) || pool.live || pool.faults) return 6;
    return 0;
}
