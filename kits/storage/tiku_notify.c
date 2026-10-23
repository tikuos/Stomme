/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_notify.c - deriving change notices, and parking them.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_notify.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void
tiku_evlist_clear(tiku_evlist_t *l)
{
    if (l != NULL) {
        l->count = 0;
        l->overflowed = 0;
    }
}

/** @brief The directory part of @p path, into @p out. */
static void
parent_of(const char *path, char *out, size_t max)
{
    const char *slash = strrchr(path, '/');
    size_t n;

    if (slash == NULL) {
        snprintf(out, max, "/");
        return;
    }
    n = (size_t)(slash - path);
    if (n == 0u) {
        snprintf(out, max, "/");
        return;
    }
    if (n >= max) {
        n = max - 1u;
    }
    memcpy(out, path, n);
    out[n] = '\0';
}

int
tiku_evlist_add(tiku_evlist_t *l, tiku_evop_t op,
                    const tiku_model_t *m, const char *from, int64_t at)
{
    tiku_node_event_t *e;

    if (l == NULL || m == NULL) {
        return 0;
    }
    if (l->count >= TIKU_EVENTS_MAX) {
        /* The overflow mark tells the caller to list again. */
        l->overflowed = 1;
        return 0;
    }
    e = &l->ev[l->count++];
    memset(e, 0, sizeof *e);
    e->op = op;
    e->model = *m;
    e->at = at;
    snprintf(e->path, sizeof e->path, "%s", m->path);
    if (from != NULL) {
        snprintf(e->from, sizeof e->from, "%s", from);
    }
    parent_of(e->path, e->dir, sizeof e->dir);
    return 1;
}

int
tiku_evlist_add_fields(tiku_evlist_t *l, tiku_evop_t op,
                           const tiku_model_t *m, const char *from,
                           int64_t at, unsigned fields)
{
    if (!tiku_evlist_add(l, op, m, from, at)) {
        return 0;
    }
    l->ev[l->count - 1].fields = fields;
    return 1;
}

/** @brief Which displayed facts moved, as TIKU_EVF_* bits (MA-072). */
static unsigned
facts_fields(const tiku_model_t *a, const tiku_model_t *b)
{
    unsigned f = 0u;

    if (a->facts.size != b->facts.size) { f |= TIKU_EVF_SIZE; }
    if (a->facts.mtime != b->facts.mtime) { f |= TIKU_EVF_DATE; }
    if (a->facts.perm != b->facts.perm) { f |= TIKU_EVF_PERM; }
    /* A link losing its target, or a Trash filling or emptying, changes
     * nothing else about the entry, so these terms are all the diff sees
     * of it (AW-029).  All of them bear on the icon. */
    if (a->kind != b->kind ||
        a->facts.link_broken != b->facts.link_broken ||
        a->facts.trash_full != b->facts.trash_full ||
        strcmp(a->type, b->type) != 0) {
        f |= TIKU_EVF_KIND;
    }
    /* A volume's capacity or free space can move alone.  It is flagged
     * KIND for the space bar (AW-097) and SIZE for the Size a volume's row
     * reports. */
    if (a->facts.total != b->facts.total ||
        a->facts.avail != b->facts.avail) {
        f |= TIKU_EVF_KIND | TIKU_EVF_SIZE;
    }
    if (strcmp(a->facts.meta, b->facts.meta) != 0) {
        f |= TIKU_EVF_VALUE;
    }
    return f;
}

/** @brief Whether the facts a row displays have moved. */
static int
facts_differ(const tiku_model_t *a, const tiku_model_t *b)
{
    return facts_fields(a, b) != 0u;
}

/** @brief Index of @p path in a listing, or -1. */
static int
find_path(const tiku_model_t *set, int n, const char *path)
{
    int i;

    for (i = 0; i < n; i++) {
        if (strcmp(set[i].path, path) == 0) {
            return i;
        }
    }
    return -1;
}

int
tiku_notify_diff(const tiku_model_t *was, int nwas,
                     const tiku_model_t *now, int nnow, int64_t at,
                     tiku_evlist_t *out)
{
    unsigned char *paired;
    int i, j, before;

    if (out == NULL) {
        return 0;
    }
    before = out->count;
    /* One mark per entry of the new listing; without the memory, nothing
     * is paired and a move is reported as a removal and a creation. */
    paired = calloc((size_t)(nnow > 0 ? nnow : 1), 1u);

    /* A removal and a creation sharing a node id in one pass are reported
     * as one MOVED notice, so the row keeps its selection, its place in
     * the list and its saved position. */
    for (i = 0; i < nwas; i++) {
        if (find_path(now, nnow, was[i].path) >= 0) {
            continue;                      /* still there                */
        }
        for (j = 0; paired != NULL && j < nnow; j++) {
            if (paired[j] || was[i].node_id == 0 ||
                now[j].node_id != was[i].node_id) {
                continue;
            }
            if (find_path(was, nwas, now[j].path) >= 0) {
                continue;                  /* that one is not new         */
            }
            paired[j] = 1;
            (void)tiku_evlist_add(out, TIKU_EV_MOVED, &now[j],
                                      was[i].path, at);
            break;
        }
        if (paired == NULL || j >= nnow) {
            (void)tiku_evlist_add(out, TIKU_EV_REMOVED, &was[i],
                                      NULL, at);
        }
    }
    for (j = 0; j < nnow; j++) {
        int k;

        if (paired != NULL && paired[j]) {
            continue;
        }
        k = find_path(was, nwas, now[j].path);
        if (k < 0) {
            (void)tiku_evlist_add(out, TIKU_EV_CREATED, &now[j],
                                      NULL, at);
        } else if (facts_differ(&was[k], &now[j])) {
            (void)tiku_evlist_add_fields(out, TIKU_EV_CHANGED,
                                             &now[j], NULL, at,
                                             facts_fields(&was[k], &now[j]));
        }
    }
    free(paired);
    return out->count - before;
}

int
tiku_evlist_coalesce(const tiku_evlist_t *in,
                         tiku_evlist_t *out)
{
    int i, j;

    if (in == NULL || out == NULL || in == out) {
        return 0;
    }
    tiku_evlist_clear(out);
    for (i = 0; i < in->count; i++) {
        const tiku_node_event_t *src = &in->ev[i];
        tiku_node_event_t *dst = NULL;

        for (j = 0; j < out->count; j++) {
            if (strcmp(out->ev[j].path, src->path) == 0) {
                dst = &out->ev[j];
                break;
            }
        }
        if (dst == NULL) {
            if (out->count >= TIKU_EVENTS_MAX) {
                out->overflowed = 1;
                continue;
            }
            out->ev[out->count++] = *src;
            continue;
        }
        /* Creation survives attribute updates, but not removal. A move
         * keeps its first source path. */
        if (dst->op == TIKU_EV_CREATED && src->op != TIKU_EV_REMOVED) {
            dst->model = src->model;
            dst->at = src->at;
            continue;
        }
        if (dst->op == TIKU_EV_MOVED && src->op != TIKU_EV_REMOVED) {
            dst->model = src->model;
            dst->at = src->at;
            continue;
        }
        if (src->op == TIKU_EV_REMOVED ||
            src->op == TIKU_EV_MOVED ||
            src->op == TIKU_EV_CHANGED ||
            src->op == TIKU_EV_ATTR) {
            dst->op = src->op;
            dst->model = src->model;
            dst->at = src->at;
            if (src->from[0] != '\0' && dst->from[0] == '\0') {
                snprintf(dst->from, sizeof dst->from, "%s", src->from);
            }
            snprintf(dst->dir, sizeof dst->dir, "%s", src->dir);
        }
    }
    out->overflowed = in->overflowed || out->overflowed;
    return out->count;
}

/*---------------------------------------------------------------------------*/
/* Parking                                                                   */
/*---------------------------------------------------------------------------*/

void
tiku_park_clear(tiku_park_t *p)
{
    if (p != NULL) {
        p->count = 0;
    }
}

int
tiku_park_add(tiku_park_t *p, const tiku_node_event_t *e)
{
    if (p == NULL || e == NULL) {
        return 0;
    }
    if (p->count >= (int)(sizeof p->ev / sizeof p->ev[0])) {
        /* Full: the oldest notice gives way, as in the device's own ring.
         * The newest is the state the row needs when it appears. */
        int i;

        for (i = 0; i < p->count - 1; i++) {
            p->ev[i] = p->ev[i + 1];
        }
        p->count--;
    }
    p->ev[p->count++] = *e;
    return 1;
}

int
tiku_park_take(tiku_park_t *p, const char *path,
                   tiku_node_event_t *out, int max)
{
    int i, n = 0, w = 0;

    if (p == NULL || path == NULL) {
        return 0;
    }
    /* Walked forwards and compacted in place, so notices come out in
     * arrival order. */
    for (i = 0; i < p->count; i++) {
        if (strcmp(p->ev[i].path, path) == 0 && n < max) {
            /* Only what is handed over is removed: notices for @p path
             * past @p max stay parked for the next take. */
            if (out != NULL) {
                out[n] = p->ev[i];
            }
            n++;
            continue;
        }
        p->ev[w++] = p->ev[i];
    }
    p->count = w;
    return (n > max) ? max : n;
}

int
tiku_park_expire(tiku_park_t *p, int64_t now)
{
    int i, w = 0, dropped = 0;

    if (p == NULL) {
        return 0;
    }
    for (i = 0; i < p->count; i++) {
        if (now - p->ev[i].at >= TIKU_PARK_LIFETIME_US) {
            dropped++;
            continue;
        }
        p->ev[w++] = p->ev[i];
    }
    p->count = w;
    return dropped;
}

int
tiku_park_count(const tiku_park_t *p)
{
    return (p != NULL) ? p->count : 0;
}
