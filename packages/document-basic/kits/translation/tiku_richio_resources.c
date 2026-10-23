/*
 * TikuDesktop Translation Kit.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_richio_resources.c - bounded content indexing for repeated image pixels.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richio_private.h"
#include "tiku_crypto.h"
#include <stdlib.h>
#include <string.h>

/** @brief A borrowed image, content key and original occurrence number. */
typedef struct {
    const tiku_rich_image_t *image;
    unsigned char hash[32];
    size_t ordinal;
} image_key_t;

/** @brief Preserve the first failure and poll cooperative cancellation. */
static int
active(tiku_rio_writer_t *w)
{
    if (w->status != TIKU_RICHIO_OK) return 0;
    if (w->options.cancel && w->options.cancel(w->options.ctx)) {
        w->status = TIKU_RICHIO_CANCELLED;
        return 0;
    }
    return 1;
}

/** @brief Order equal digest candidates by source occurrence. */
static int
order(const void *left, const void *right)
{
    const image_key_t *a = left, *b = right;
    int result = memcmp(a->hash, b->hash, sizeof a->hash);

    return result ? result :
        (a->ordinal > b->ordinal) - (a->ordinal < b->ordinal);
}

/** @brief Hash pixel dimensions and data with cancellation between chunks. */
static void
digest(tiku_rio_writer_t *w, image_key_t *key)
{
    const tiku_rich_image_t *image = key->image;
    unsigned dimensions[2] = {image->pixel_width, image->pixel_height};
    size_t count = (size_t)dimensions[0] * dimensions[1];
    tiku_hash_t hash;

    tiku_hash_init(&hash, TIKU_HASH_SHA256);
    tiku_hash_update(&hash, dimensions, sizeof dimensions);
    for (size_t at = 0; at < count && active(w); ) {
        size_t n = count - at > 4096 ? 4096 : count - at;

        tiku_hash_update(&hash, image->pixels + at, n * sizeof *image->pixels);
        at += n;
    }
    tiku_hash_final(&hash, key->hash);
}

/** @brief Compare dimensions and pixel bytes independently of the digest. */
static int
equal(tiku_rio_writer_t *w, const tiku_rich_image_t *a,
      const tiku_rich_image_t *b)
{
    size_t count;

    if (a->pixel_width != b->pixel_width || a->pixel_height != b->pixel_height)
        return 0;
    count = (size_t)a->pixel_width * a->pixel_height;
    for (size_t at = 0; at < count && active(w); ) {
        size_t n = count - at > 4096 ? 4096 : count - at;

        if (memcmp(a->pixels + at, b->pixels + at, n * sizeof *a->pixels))
            return 0;
        at += n;
    }
    return w->status == TIKU_RICHIO_OK;
}

void
tiku_rio_prepare_images(tiku_rio_writer_t *w,
                         const tiku_richsnapshot_t *snapshot)
{
    tiku_rich_paragraph_t p;
    size_t paragraphs = tiku_richsnapshot_paragraphs(snapshot), runs = 0;
    size_t budget = w->options.memory_bytes / 2;
    size_t count = 0, at = 0;
    image_key_t *keys;

    if (paragraphs > 250000) { w->status = TIKU_RICHIO_LIMIT; return; }
    for (size_t i = 0; i < paragraphs && active(w); i++) {
        tiku_richsnapshot_paragraph(snapshot, i, &p);
        if (p.run_count > 1000000 - runs) {
            w->status = TIKU_RICHIO_LIMIT;
            return;
        }
        runs += p.run_count;
        for (size_t j = 0; j < p.run_count && active(w); j++)
            if (p.runs[j].image) count++;
    }
    if (!count || !active(w)) return;
    w->image_count = count;
    if (count == 1) return;
    budget -= w->count * sizeof *w->styles;
    if (count > budget / (sizeof *keys + sizeof *w->image_refs)) {
        w->status = TIKU_RICHIO_LIMIT;
        return;
    }
    keys = calloc(count, sizeof *keys);
    w->image_refs = calloc(count, sizeof *w->image_refs);
    if (!keys || !w->image_refs) {
        w->status = TIKU_RICHIO_NO_MEMORY;
        free(keys);
        return;
    }
    for (size_t i = 0; i < paragraphs && active(w); i++) {
        tiku_richsnapshot_paragraph(snapshot, i, &p);
        for (size_t j = 0; j < p.run_count && active(w); j++) {
            if (!p.runs[j].image) continue;
            keys[at].image = p.runs[j].image;
            keys[at].ordinal = at + 1;
            digest(w, &keys[at++]);
        }
    }
    if (active(w)) {
        size_t first = 0;

        qsort(keys, count, sizeof *keys, order);
        for (size_t i = 1; i < count && active(w); i++) {
            if (memcmp(keys[first].hash, keys[i].hash, sizeof keys[i].hash))
                first = i;
            else if (equal(w, keys[first].image, keys[i].image)) {
                w->image_refs[keys[i].ordinal - 1] = keys[first].ordinal;
                w->reference_feature = 1;
            }
        }
    }
    free(keys);
}
