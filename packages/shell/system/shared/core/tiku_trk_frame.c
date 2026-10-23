/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_frame.c - reading and writing window frames and the session.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_trk_frame.h"
#include <stdio.h>
#include <string.h>

int
tiku_trk_frame_load(tiku_store_t *s, const char *path,
                    tiku_trk_frame_t *out)
{
    tiku_trk_frame_t f;

    if (s == NULL || path == NULL || out == NULL) {
        return 0;
    }
    if (tiku_state_read(s, path, TIKU_ATTR_FRAME, &f, sizeof f) !=
            (int)sizeof f) {
        return 0;
    }
    if (f.w < TIKU_TRK_FRAME_MIN_W || f.h < TIKU_TRK_FRAME_MIN_H) {
        /* A record smaller than the minimum reads as none. */
        return 0;
    }
    *out = f;
    return 1;
}

int
tiku_trk_frame_save(tiku_store_t *s, const char *path,
                    const tiku_trk_frame_t *now, const tiku_trk_frame_t *was)
{
    if (s == NULL || path == NULL || now == NULL) {
        return 0;
    }
    if (was != NULL && was->x == now->x && was->y == now->y &&
        was->w == now->w && was->h == now->h) {
        /* Nothing moved, so nothing is written (CW-002). */
        return 0;
    }
    return (tiku_state_write(s, path, TIKU_ATTR_FRAME, now,
                                 sizeof *now) == 0) ? 1 : 0;
}

void
tiku_trk_frame_clamp(tiku_trk_frame_t *f, int sw, int sh)
{
    if (f == NULL || sw <= 0 || sh <= 0) {
        return;
    }
    if (f->w > sw) { f->w = (int32_t)sw; }
    if (f->h > sh) { f->h = (int32_t)sh; }
    if (f->w < TIKU_TRK_FRAME_MIN_W) { f->w = TIKU_TRK_FRAME_MIN_W; }
    if (f->h < TIKU_TRK_FRAME_MIN_H) { f->h = TIKU_TRK_FRAME_MIN_H; }
    /*
     * Only enough to grab stays on screen: the top edge, and
     * TIKU_TRK_FRAME_GRAB pixels of the width.  A window may hang off the
     * bottom where the user put it.
     */
    if (f->y < 0) { f->y = 0; }
    if (f->y > sh - TIKU_TRK_FRAME_TAB) {
        f->y = (int32_t)(sh - TIKU_TRK_FRAME_TAB);
    }
    if (f->x + TIKU_TRK_FRAME_GRAB > sw) {
        f->x = (int32_t)(sw - TIKU_TRK_FRAME_GRAB);
    }
    if (f->x + f->w < TIKU_TRK_FRAME_GRAB) {
        f->x = (int32_t)(TIKU_TRK_FRAME_GRAB - f->w);
    }
}

int
tiku_trk_frame_default(tiku_store_t *s, tiku_trk_frame_t *out)
{
    tiku_trk_frame_t f;

    if (s == NULL || out == NULL) {
        return 0;
    }
    if (tiku_state_read(s, TIKU_KEY_SESSION, TIKU_ATTR_DEFAULTS,
                            &f, sizeof f) != (int)sizeof f) {
        return 0;
    }
    if (f.w < TIKU_TRK_FRAME_MIN_W || f.h < TIKU_TRK_FRAME_MIN_H) {
        return 0;
    }
    *out = f;
    return 1;
}

void
tiku_trk_frame_set_default(tiku_store_t *s, const tiku_trk_frame_t *f)
{
    if (s == NULL || f == NULL) {
        return;
    }
    (void)tiku_state_write(s, TIKU_KEY_SESSION,
                               TIKU_ATTR_DEFAULTS, f, sizeof *f);
}

/** @brief The folder @p path is in, or "" for the root. */
static void
parent_of(const char *path, char *out, size_t max)
{
    const char *slash;

    out[0] = '\0';
    if (path == NULL || strcmp(path, "/") == 0) {
        return;                 /* the root has no parent */
    }
    slash = strrchr(path, '/');
    if (slash == NULL) {
        return;
    }
    if (slash == path) {
        snprintf(out, max, "/");
        return;
    }
    {
        size_t n = (size_t)(slash - path);

        if (n >= max) {
            n = max - 1u;
        }
        memcpy(out, path, n);
        out[n] = '\0';
    }
}

int
tiku_trk_frame_inherit(tiku_store_t *s, const char *path,
                       tiku_trk_frame_t *out)
{
    char parent[TIKU_PATH_MAX];

    if (s == NULL || path == NULL || out == NULL) {
        return 0;
    }
    if (strcmp(path, "/") == 0) {
        /* The root inherits nothing and takes no default (CW-069). */
        return 0;
    }
    parent_of(path, parent, sizeof parent);
    if (parent[0] != '\0' && tiku_trk_frame_load(s, parent, out)) {
        /* Offset from the parent's frame, so the new window does not cover
         * it exactly (CW-005). */
        out->x += 22;
        out->y += 22;
        return 1;
    }
    return tiku_trk_frame_default(s, out);
}

/*---------------------------------------------------------------------------*/
/* The session                                                               */
/*---------------------------------------------------------------------------*/

int
tiku_trk_session_save(tiku_store_t *s,
                      const char paths[][TIKU_PATH_MAX], int n)
{
    tiku_trk_session_entry_t entries[TIKU_TRK_SESSION_MAX];
    int i;

    if (paths == NULL || n <= 0) {
        return tiku_trk_session_save_entries(s, NULL, 0);
    }
    for (i = 0; i < n && i < TIKU_TRK_SESSION_MAX; i++) {
        snprintf(entries[i].path, sizeof entries[i].path, "%s", paths[i]);
        entries[i].minimized = 0;
    }
    return tiku_trk_session_save_entries(s, entries, i);
}

int
tiku_trk_session_load(tiku_store_t *s, char out[][TIKU_PATH_MAX],
                      int max)
{
    tiku_trk_session_entry_t entries[TIKU_TRK_SESSION_MAX];
    int i, n;

    if (out == NULL || max <= 0) {
        return 0;
    }
    n = tiku_trk_session_load_entries(s, entries,
        (max < TIKU_TRK_SESSION_MAX) ? max : TIKU_TRK_SESSION_MAX);
    for (i = 0; i < n; i++) {
        snprintf(out[i], TIKU_PATH_MAX, "%.*s",
                 (int)(TIKU_PATH_MAX - 1), entries[i].path);
    }
    return n;
}

int
tiku_trk_session_save_entries(tiku_store_t *s,
                              const tiku_trk_session_entry_t *entries,
                              int n)
{
    char buf[TIKU_PATH_MAX * TIKU_TRK_SESSION_MAX];
    size_t at = 0u;
    int i;

    if (s == NULL) {
        return 0;
    }
    if (entries == NULL || n <= 0) {
        tiku_trk_session_clear(s);
        return 0;
    }
    for (i = 0; i < n && i < TIKU_TRK_SESSION_MAX && at + 1u < sizeof buf;
         i++) {
        int w = snprintf(buf + at, sizeof buf - at, "%c %.*s\n",
                         entries[i].minimized ? 'M' : 'O',
                         (int)(TIKU_PATH_MAX - 1), entries[i].path);

        if (w <= 0 || (size_t)w >= sizeof buf - at) {
            break;
        }
        at += (size_t)w;
    }
    return (tiku_state_write(s, TIKU_KEY_SESSION,
                                 TIKU_ATTR_SESSION, buf, at) == 0)
               ? i : 0;
}

int
tiku_trk_session_load_entries(tiku_store_t *s,
                              tiku_trk_session_entry_t *out, int max)
{
    char buf[TIKU_PATH_MAX * TIKU_TRK_SESSION_MAX];
    int n = 0, len;
    char *line, *save;

    if (s == NULL || out == NULL || max <= 0) {
        return 0;
    }
    len = tiku_state_read(s, TIKU_KEY_SESSION, TIKU_ATTR_SESSION,
                              buf, sizeof buf - 1u);
    if (len <= 0) {
        return 0;
    }
    buf[len] = '\0';
    for (line = strtok_r(buf, "\n", &save); line != NULL && n < max;
         line = strtok_r(NULL, "\n", &save)) {
        tiku_trk_session_entry_t *entry;
        const char *path = line;

        if (line[0] == '\0') {
            continue;
        }
        entry = &out[n];
        entry->minimized = 0;
        /* A line is 'M ' or 'O ' and the path; a line with neither prefix
         * is a shown window. */
        if ((line[0] == 'M' || line[0] == 'O') && line[1] == ' ') {
            entry->minimized = (line[0] == 'M');
            path = line + 2;
        }
        if (path[0] != '\0') {
            snprintf(entry->path, sizeof entry->path, "%.*s",
                     (int)(TIKU_PATH_MAX - 1), path);
            n++;
        }
    }
    return n;
}

void
tiku_trk_session_clear(tiku_store_t *s)
{
    if (s != NULL) {
        (void)tiku_state_write(s, TIKU_KEY_SESSION,
                                   TIKU_ATTR_SESSION, "", 0);
    }
}
