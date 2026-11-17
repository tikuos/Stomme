/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * font_reference.c - compare compiled subsets with the frozen desktop tables.
 * SPDX-License-Identifier: Apache-2.0
 */
#include <string.h>
#include "tiku_font_subset.h"
#include "tiku_font_data.h"

/** @brief Compare metrics and coverage using the C compiler's source tables. */
int main(void)
{
    const tiku_font_t *original[2] = {&plain_font, &bold_font};
    int face, cp;
    for (face = 0; face < 2; ++face) {
        const tiku_font_t *subset = tiku_font_subset_face(face, 12);
        if (subset->height != original[face]->height ||
            subset->ascent != original[face]->ascent) {
            return 1;
        }
        for (cp = 32; cp <= 255; ++cp) {
            const tiku_glyph_t *a, *b;
            if (cp > 126 && cp < 160) {
                continue;
            }
            a = &original[face]->glyphs[cp - 32];
            b = tiku_font_subset_glyph(face, (unsigned)cp);
            if (a->cp != b->cp || a->adv != b->adv || a->w != b->w ||
                a->h != b->h || a->ox != b->ox || a->oy != b->oy ||
                memcmp(original[face]->bits + a->off,
                       subset->bits + b->off, (size_t)a->w * a->h)) {
                return 2;
            }
        }
    }
    return 0;
}
