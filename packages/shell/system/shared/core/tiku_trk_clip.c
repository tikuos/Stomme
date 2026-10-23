/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_clip.c - the file clipboard.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_trk_clip.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

void
tiku_trk_clip_init(tiku_trk_clip_t *c)
{
    if (c != NULL) {
        memset(c, 0, sizeof *c);
    }
}

/** @brief The entry for @p id, or -1. */
static int
find_id(const tiku_trk_clip_t *c, uint64_t id)
{
    int i;

    if (id == 0) {
        return -1;                  /* no identity, no entry to find     */
    }
    for (i = 0; i < c->count; i++) {
        if (c->item[i].id == id) {
            return i;
        }
    }
    return -1;
}

/** @brief Whether the model may go on the clipboard at all (FS-052). */
static int
clippable(const tiku_model_t *m)
{
    /* A volume, the root, the Trash and the desktop cannot be pasted
     * anywhere; the things in them can. */
    return m != NULL && m->node_id != 0 &&
           m->kind != TIKU_KIND_VOLUME &&
           m->kind != TIKU_KIND_ROOT &&
           m->kind != TIKU_KIND_TRASH &&
           m->kind != TIKU_KIND_DESKTOP &&
           strcmp(m->path, "/") != 0;
}

int
tiku_trk_clip_add(tiku_trk_clip_t *c, const tiku_model_t *models, int n,
                  tiku_trk_clip_mode_t mode, int replace)
{
    int i;

    if (c == NULL || models == NULL) {
        return 0;
    }
    if (replace) {
        c->count = 0;
    }
    for (i = 0; i < n; i++) {
        int at;

        if (!clippable(&models[i])) {
            continue;
        }
        /* Keyed by identity: a second Cut of the same file rewrites the
         * entry it already has (FS-053), mode included, so copying
         * something that was cut makes it a copy. */
        at = find_id(c, models[i].node_id);
        if (at < 0) {
            if (c->count >= TIKU_TRK_CLIP_MAX) {
                break;
            }
            at = c->count++;
            c->item[at].id = models[i].node_id;
        }
        snprintf(c->item[at].path, sizeof c->item[at].path, "%s",
                 models[i].path);
        c->item[at].mode = mode;
    }
    c->serial++;
    return c->count;
}

tiku_trk_clip_mode_t
tiku_trk_clip_mode_of(const tiku_trk_clip_t *c, uint64_t id)
{
    int at;

    if (c == NULL) {
        return TIKU_TRK_CLIP_NONE;
    }
    at = find_id(c, id);
    return (at < 0) ? TIKU_TRK_CLIP_NONE : c->item[at].mode;
}

int
tiku_trk_clip_moved(tiku_trk_clip_t *c, uint64_t id, const char *to)
{
    int at;

    if (c == NULL || to == NULL) {
        return 0;
    }
    at = find_id(c, id);
    if (at < 0) {
        return 0;
    }
    /* The path follows and the mode stays: an item cut and then renamed is
     * still cut, and the paste uses the name it has now (FS-060). */
    snprintf(c->item[at].path, sizeof c->item[at].path, "%s", to);
    c->serial++;
    return 1;
}

int
tiku_trk_clip_moved_path(tiku_trk_clip_t *c, const char *from, const char *to,
                         uint64_t now_id)
{
    size_t n;
    int i, hit = 0;

    if (c == NULL || from == NULL || to == NULL || from[0] == '\0' ||
        to[0] == '\0') {
        return 0;
    }
    n = strlen(from);
    for (i = 0; i < c->count; i++) {
        char rest[TIKU_PATH_MAX];

        if (strcmp(c->item[i].path, from) == 0) {
            if (!tiku_trk_clip_moved(c, c->item[i].id, to)) {
                continue;
            }
            if (now_id != 0u) {
                /* Across a device a move is a copy and a delete, so what
                 * arrives is a different node with the same contents; the
                 * entry follows the file, not the inode. */
                c->item[i].id = now_id;
            }
            hit++;
        } else if (strncmp(c->item[i].path, from, n) == 0 &&
                   c->item[i].path[n] == '/') {
            /* A folder that moved took its contents with it. */
            snprintf(rest, sizeof rest, "%s%s", to, c->item[i].path + n);
            if (tiku_trk_clip_moved(c, c->item[i].id, rest)) {
                hit++;
            }
        }
    }
    return hit;
}

int
tiku_trk_clip_removed(tiku_trk_clip_t *c, uint64_t id)
{
    int at, i;

    if (c == NULL) {
        return 0;
    }
    at = find_id(c, id);
    if (at < 0) {
        return 0;
    }
    for (i = at; i < c->count - 1; i++) {
        c->item[i] = c->item[i + 1];
    }
    c->count--;
    c->serial++;
    return 1;
}

int
tiku_trk_clip_prune(tiku_trk_clip_t *c)
{
    int i, w = 0, dropped = 0;

    if (c == NULL) {
        return 0;
    }
    for (i = 0; i < c->count; i++) {
        struct stat st;

        /* An entry whose file has gone is dropped, so a paste is never
         * handed a missing file (FS-058). */
        if (stat(c->item[i].path, &st) != 0) {
            dropped++;
            continue;
        }
        c->item[w++] = c->item[i];
    }
    c->count = w;
    if (dropped > 0) {
        c->serial++;
    }
    return dropped;
}

int
tiku_trk_clip_spend_cuts(tiku_trk_clip_t *c)
{
    int i, spent = 0;

    if (c == NULL) {
        return 0;
    }
    for (i = 0; i < c->count; i++) {
        if (c->item[i].mode == TIKU_TRK_CLIP_MOVE) {
            c->item[i].mode = TIKU_TRK_CLIP_COPY;
            spent++;
        }
    }
    if (spent > 0) {
        c->serial++;
    }
    return spent;
}

void
tiku_trk_clip_clear(tiku_trk_clip_t *c)
{
    if (c != NULL) {
        c->count = 0;
        c->serial++;
    }
}

int
tiku_trk_clip_remove(tiku_trk_clip_t *c, const tiku_model_t *models,
                     int n)
{
    int i, gone = 0;

    for (i = 0; c != NULL && models != NULL && i < n; i++) {
        gone += tiku_trk_clip_removed(c, models[i].node_id);
    }
    return gone;
}

int
tiku_trk_clip_count(const tiku_trk_clip_t *c)
{
    return (c != NULL) ? c->count : 0;
}

const tiku_trk_clip_entry_t *
tiku_trk_clip_at(const tiku_trk_clip_t *c, int i)
{
    if (c == NULL || i < 0 || i >= c->count) {
        return NULL;
    }
    return &c->item[i];
}
