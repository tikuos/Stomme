/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_script_api.c - parsing and running scripting requests.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_trk_script_api.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *
tiku_trk_script_suite(int i)
{
    /* What a discovery request lists: the properties and the specifiers
     * each takes, as the text a client reads to learn what it may ask
     * (PVK-048). */
    static const char *props[] = {
        "Path: get [direct]",
        "Entry: count [direct]; get [direct|index|previous|next]; "
        "execute [index|path]; delete [index|path]",
        "Selection: count [direct]; get [direct|previous|next]; "
        "set [range|paths]; create [paths]; delete [direct]",
        NULL
    };

    if (i < 0 || i >= (int)(sizeof props / sizeof props[0]) - 1) {
        return NULL;
    }
    return props[i];
}

/** @brief Whether @p w names @p name, case-blind. */
static int
word_is(const char *w, const char *name)
{
    size_t i;

    for (i = 0; w[i] != '\0' && name[i] != '\0'; i++) {
        char a = w[i], b = name[i];

        if (a >= 'A' && a <= 'Z') { a = (char)(a - 'A' + 'a'); }
        if (b >= 'A' && b <= 'Z') { b = (char)(b - 'A' + 'a'); }
        if (a != b) {
            return 0;
        }
    }
    return (w[i] == '\0' && name[i] == '\0');
}

int
tiku_trk_script_parse(const char *line, tiku_trk_req_t *req,
                      char (*store)[TIKU_PATH_MAX])
{
    char buf[TIKU_PATH_MAX * 2];
    char *w[8];
    int n = 0, i;
    char *p;

    if (line == NULL || req == NULL) {
        return -1;
    }
    memset(req, 0, sizeof *req);
    snprintf(buf, sizeof buf, "%s", line);
    p = buf;
    while (*p != '\0' && n < 8) {
        while (*p == ' ' || *p == '\t' || *p == '\n') { p++; }
        if (*p == '\0') { break; }
        w[n++] = p;
        while (*p != '\0' && *p != ' ' && *p != '\t' && *p != '\n') { p++; }
        if (*p != '\0') { *p++ = '\0'; }
    }
    if (n < 2) {
        return -1;
    }
    if      (word_is(w[0], "get"))     { req->verb = TIKU_TRK_VERB_GET; }
    else if (word_is(w[0], "set"))     { req->verb = TIKU_TRK_VERB_SET; }
    else if (word_is(w[0], "count"))   { req->verb = TIKU_TRK_VERB_COUNT; }
    else if (word_is(w[0], "create"))  { req->verb = TIKU_TRK_VERB_CREATE; }
    else if (word_is(w[0], "delete"))  { req->verb = TIKU_TRK_VERB_DELETE; }
    else if (word_is(w[0], "execute")) { req->verb = TIKU_TRK_VERB_EXECUTE; }
    else { return -1; }

    if      (word_is(w[1], "path"))      { req->prop = TIKU_TRK_PROP_PATH; }
    else if (word_is(w[1], "entry"))     { req->prop = TIKU_TRK_PROP_ENTRY; }
    else if (word_is(w[1], "selection")) {
        req->prop = TIKU_TRK_PROP_SELECTION;
    } else { return -1; }

    req->spec = TIKU_TRK_SPEC_DIRECT;
    if (n >= 3) {
        if (word_is(w[2], "previous") || word_is(w[2], "next")) {
            req->spec = word_is(w[2], "next") ? TIKU_TRK_SPEC_NEXT
                                              : TIKU_TRK_SPEC_PREVIOUS;
            /* Both name the path to walk from. */
            if (n >= 4 && store != NULL) {
                snprintf(store[0], TIKU_PATH_MAX, "%s", w[3]);
                req->path = store[0];
                req->npath = 1;
            }
        } else if (w[2][0] >= '0' && w[2][0] <= '9') {
            req->a = atoi(w[2]);
            req->spec = TIKU_TRK_SPEC_INDEX;
            if (n >= 4 && w[3][0] >= '0' && w[3][0] <= '9') {
                req->b = atoi(w[3]);
                req->spec = TIKU_TRK_SPEC_RANGE;
            }
        } else {
            req->spec = TIKU_TRK_SPEC_PATH;
            req->path = NULL;
            if (store != NULL) {
                for (i = 2; i < n && i - 2 < 8; i++) {
                    snprintf(store[i - 2], TIKU_PATH_MAX, "%s", w[i]);
                    req->pathv[i - 2] = store[i - 2];
                }
                req->npath = n - 2;
                req->path = store[0];
                req->paths = req->pathv;
            }
        }
    }
    return 0;
}

/** @brief Append one path to the reply. */
static void
add(tiku_trk_rep_t *rep, const char *path)
{
    if (rep->nresult < (int)(sizeof rep->result / sizeof rep->result[0])) {
        snprintf(rep->result[rep->nresult], TIKU_PATH_MAX, "%s", path);
        rep->nresult++;
    }
}

/** @brief The index of the pose at @p path, or -1. */
static int
index_of(tiku_trk_poseview_t *v, const char *path)
{
    int i, n = tiku_trk_poseview_count(v);

    if (path == NULL) {
        return -1;
    }
    for (i = 0; i < n; i++) {
        const tiku_trk_pose_t *p = tiku_trk_poseview_at(v, i);

        if (p != NULL && strcmp(p->model.path, path) == 0) {
            return i;
        }
    }
    return -1;
}

void
tiku_trk_script_run(tiku_trk_poseview_t *v, const tiku_trk_req_t *req,
                    tiku_trk_rep_t *rep)
{
    int n, i;

    if (rep == NULL) {
        return;
    }
    memset(rep, 0, sizeof *rep);
    if (v == NULL || req == NULL) {
        rep->error = TIKU_TRK_ERR_UNHANDLED;
        return;
    }
    /* Every count and index is against the current list, with its filter
     * and sort order, so an index holds only until the list changes
     * (PVK-055). */
    n = tiku_trk_poseview_count(v);

    if (req->prop == TIKU_TRK_PROP_PATH) {
        const char *path;

        if (req->verb != TIKU_TRK_VERB_GET ||
            req->spec != TIKU_TRK_SPEC_DIRECT) {
            rep->error = TIKU_TRK_ERR_UNHANDLED;
            return;
        }
        path = tiku_trk_poseview_path(v);
        if (path == NULL || path[0] == '\0') {
            rep->error = TIKU_TRK_ERR_NOT_DIRECTORY;
            return;
        }
        add(rep, path);
        return;
    }

    if (req->verb == TIKU_TRK_VERB_COUNT) {
        if (req->prop == TIKU_TRK_PROP_SELECTION) {
            rep->count = tiku_trk_poseview_selection_count(v);
        } else {
            rep->count = n;
        }
        return;
    }

    if (req->prop == TIKU_TRK_PROP_ENTRY) {
        switch (req->verb) {
        case TIKU_TRK_VERB_GET:
            if (req->spec == TIKU_TRK_SPEC_DIRECT) {
                for (i = 0; i < n; i++) {
                    const tiku_trk_pose_t *p = tiku_trk_poseview_at(v, i);

                    if (p != NULL) { add(rep, p->model.path); }
                }
                return;
            }
            if (req->spec == TIKU_TRK_SPEC_INDEX) {
                const tiku_trk_pose_t *p;

                if (req->a < 0 || req->a >= n) {
                    rep->error = TIKU_TRK_ERR_BAD_INDEX;
                    return;
                }
                p = tiku_trk_poseview_at(v, req->a);
                if (p != NULL) { add(rep, p->model.path); }
                return;
            }
            if (req->spec == TIKU_TRK_SPEC_PREVIOUS ||
                req->spec == TIKU_TRK_SPEC_NEXT) {
                int at = index_of(v, req->path);
                int to;

                if (at < 0) {
                    rep->error = TIKU_TRK_ERR_NOT_FOUND;
                    return;
                }
                to = at + ((req->spec == TIKU_TRK_SPEC_NEXT) ? 1 : -1);
                if (to < 0 || to >= n) {
                    rep->error = TIKU_TRK_ERR_BAD_INDEX;
                    return;
                }
                add(rep, tiku_trk_poseview_at(v, to)->model.path);
                return;
            }
            rep->error = TIKU_TRK_ERR_UNHANDLED;
            return;
        case TIKU_TRK_VERB_EXECUTE:
        case TIKU_TRK_VERB_DELETE: {
            /* Both are resolved to paths here; the caller acts on them,
             * and a delete moves them to the Trash (PVK-052). */
            if (req->spec == TIKU_TRK_SPEC_INDEX) {
                if (req->a < 0 || req->a >= n) {
                    rep->error = TIKU_TRK_ERR_BAD_INDEX;
                    return;
                }
                add(rep, tiku_trk_poseview_at(v, req->a)->model.path);
                return;
            }
            for (i = 0; i < req->npath; i++) {
                const char *pp = (req->paths != NULL) ? req->paths[i]
                                                      : req->path;

                if (index_of(v, pp) < 0) {
                    rep->error = TIKU_TRK_ERR_NOT_FOUND;
                    return;
                }
                add(rep, pp);
            }
            if (rep->nresult == 0) {
                rep->error = TIKU_TRK_ERR_NOT_FOUND;
            }
            return;
        }
        default:
            rep->error = TIKU_TRK_ERR_UNHANDLED;
            return;
        }
    }

    /* Selection */
    switch (req->verb) {
    case TIKU_TRK_VERB_GET:
        for (i = 0; i < n; i++) {
            const tiku_trk_pose_t *p = tiku_trk_poseview_at(v, i);

            if (p != NULL && p->selected) { add(rep, p->model.path); }
        }
        return;
    case TIKU_TRK_VERB_DELETE:
        tiku_trk_poseview_select_none(v);
        return;
    case TIKU_TRK_VERB_SET:
    case TIKU_TRK_VERB_CREATE:
        if (req->spec == TIKU_TRK_SPEC_RANGE) {
            /* Both bounds must be valid indices, and the end is exclusive,
             * as in the original's `index < end` loop.  They are checked
             * before anything is cleared, so a refused request leaves the
             * selection alone. */
            if (req->a < 0 || req->a >= n || req->b < 0 || req->b >= n) {
                rep->error = TIKU_TRK_ERR_BAD_INDEX;
                return;
            }
            if (req->verb == TIKU_TRK_VERB_SET) {
                tiku_trk_poseview_select_none(v);
            }
            for (i = req->a; i < req->b; i++) {
                tiku_trk_pose_t *p = tiku_trk_poseview_at(v, i);

                if (p != NULL) { p->selected = 1; }
            }
            rep->count = (req->b > req->a) ? req->b - req->a : 0;
            return;
        }
        /* Resolved first, then applied, so a set naming a missing path
         * leaves the selection alone. */
        for (i = 0; i < req->npath; i++) {
            if (index_of(v, (req->paths != NULL) ? req->paths[i]
                                                 : req->path) < 0) {
                rep->error = TIKU_TRK_ERR_NOT_FOUND;
                return;
            }
        }
        if (req->verb == TIKU_TRK_VERB_SET) {
            tiku_trk_poseview_select_none(v);
        }
        for (i = 0; i < req->npath; i++) {
            int at = index_of(v, (req->paths != NULL) ? req->paths[i]
                                                      : req->path);

            tiku_trk_poseview_at(v, at)->selected = 1;
            rep->count++;
        }
        return;
    default:
        rep->error = TIKU_TRK_ERR_UNHANDLED;
        return;
    }
}
