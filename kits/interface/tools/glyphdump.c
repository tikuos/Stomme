/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * glyphdump.c - what the kit's rasteriser makes of one glyph.
 *
 * A development tool, not part of the build or the suite: it prints a
 * font's metrics and each glyph's coverage, which
 * kits/interface/tools/compare_freetype.py compares with FreeType's
 * rendering of the same glyph.
 *
 *   cc -Ikits/interface -o build/tools/glyphdump \
 *      kits/interface/tools/glyphdump.c build/lib/libtiku_interface.a \
 *      build/lib/libtiku_support.a -lm
 *   build/tools/glyphdump <font-file> <px> <codepoint-hex> [...]
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tiku_ttf.h"

/**
 * @brief Print FAMILY and METRICS lines for the font, then a GLYPH line
 *        and the coverage rows of each code point named in hex.
 */
int
main(int argc, char **argv)
{
    tiku_ttf_t *ttf;
    int px, i;

    if (argc < 4) {
        fprintf(stderr, "usage: %s <font> <px> <cp-hex>...\n", argv[0]);
        return 2;
    }
    ttf = tiku_ttf_open(argv[1]);
    if (ttf == NULL) {
        printf("REFUSED %s\n", argv[1]);
        return 1;
    }
    px = atoi(argv[2]);
    printf("FAMILY %s\n", tiku_ttf_family(ttf));
    {
        int ascent = 0, height = 0;

        tiku_ttf_metrics(ttf, px, &ascent, &height);
        printf("METRICS %d %d\n", ascent, height);
    }
    for (i = 3; i < argc; i++) {
        unsigned cp = (unsigned)strtoul(argv[i], NULL, 16);
        tiku_ttf_glyph_t g;
        int x, y;

        memset(&g, 0, sizeof g);
        if (!tiku_ttf_render(ttf, cp, px, &g)) {
            printf("GLYPH %x MISSING\n", cp);
            continue;
        }
        printf("GLYPH %x %d %d %d %d %d\n", cp, g.adv, g.w, g.h, g.ox, g.oy);
        for (y = 0; y < g.h; y++) {
            for (x = 0; x < g.w; x++) {
                printf("%d%s", g.cover[y * g.w + x],
                       (x + 1 < g.w) ? " " : "");
            }
            printf("\n");
        }
        tiku_ttf_free_glyph(&g);
    }
    tiku_ttf_close(ttf);
    return 0;
}
