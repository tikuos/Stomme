/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_fs_text.c - one path as one piece of text, read through the backend
 * that serves it: a node's value, or a folder's children by name.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_fs_private.h"

/** @brief Where backend_text_entry() appends names. */
typedef struct {
    char  *out;                     /**< the caller's buffer            */
    size_t max, used;               /**< its size, and the length of the
                                       names so far, which can pass max */
} backend_text_ctx_t;

/** @brief tiku_backend_text()'s listing half: append one child's name and
 *         a '|'; stop when the buffer is full. */
static int
backend_text_entry(const tiku_model_t *m, void *ctx)
{
    backend_text_ctx_t *t = ctx;

    t->used += (size_t)snprintf(t->out + t->used,
                                (t->used < t->max) ? t->max - t->used : 0u,
                                "%s|", m->name);
    return (t->used < t->max) ? 0 : 1;      /* full: stop the walk */
}

int
tiku_backend_text(tiku_backend_t *b, const char *path,
                  char *out, size_t max)
{
    tiku_model_t m;

    if (out == NULL || max == 0u) {
        return 0;
    }
    out[0] = '\0';
    if (b == NULL || b->ops == NULL || path == NULL || path[0] != '/' ||
        b->ops->stat(b, path, &m) != 0) {
        return 0;               /* nothing serves it                    */
    }
    if (tiku_model_is_container(&m)) {
        backend_text_ctx_t t = { out, max, 0u };

        if (b->ops->list != NULL) {
            (void)b->ops->list(b, path, backend_text_entry, &t);
        }
        return 1;
    }
    if (b->ops->read != NULL) {
        int n = b->ops->read(b, path, out, max - 1u);

        if (n < 0) {
            n = 0;
        }
        if ((size_t)n >= max) {
            n = (int)max - 1;
        }
        out[n] = '\0';
    }
    return 1;
}
