/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_state_pipeline.c - the attribute copy, one value at a time.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_trk_state_pipeline.h"
#include <string.h>

typedef struct {
    tiku_store_t *store;
    const char *dest;
    tiku_trk_state_accept_fn accept;
    tiku_trk_state_transform_fn transform;
    void *ctx;
    int copied;
    int failed;
} pipeline_t;

/**
 * @brief Copy one attribute through the pipeline: skipped when the accept
 *        hook refuses it, transformed when there is a transform, then
 *        written to the destination.
 *
 * @return 1 to stop the walk after a failure, else 0.
 */
static int
copy_one(const char *attr, const void *value, size_t len, void *opaque)
{
    pipeline_t *p = opaque;
    unsigned char transformed[4096];
    size_t out_len = len;
    const void *src = value;

    if (p->accept != NULL && !p->accept(attr, p->ctx)) {
        return 0;
    }
    if (p->transform != NULL) {
        if (p->transform(attr, value, len, transformed, sizeof transformed,
                         &out_len,
                         p->ctx) != 0 || out_len > sizeof transformed) {
            p->failed = 1;
            return 1;
        }
        src = transformed;
    }
    if (tiku_state_write(p->store, p->dest, attr, src, out_len) != 0) {
        p->failed = 1;
        return 1;
    }
    p->copied++;
    return 0;
}

int
tiku_trk_state_pipeline_copy(tiku_store_t *store, const char *source,
                             const char *dest, tiku_trk_state_accept_fn accept,
                             tiku_trk_state_transform_fn transform, void *ctx)
{
    pipeline_t p;
    int visited;

    if (store == NULL || source == NULL || dest == NULL) {
        return -1;
    }
    p = (pipeline_t){ store, dest, accept, transform, ctx, 0, 0 };
    visited = tiku_state_each(store, source, copy_one, &p);
    if (visited < 0 || p.failed) {
        return -1;
    }
    return p.copied;
}
