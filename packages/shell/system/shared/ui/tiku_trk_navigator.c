/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_navigator.c - a browser window's Back and Forward history, and
 * the path helpers for a folder's parents and a drop's destination.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_trk_navigator.h"
#include <stdio.h>
#include <string.h>

/** @brief Append @p path, dropping the oldest entry when the stack is
 *         full. */
static void
append(char stack[][TIKU_PATH_MAX], int *count, const char *path)
{
    if (*count >= TIKU_TRK_NAV_HISTORY_MAX) {
        memmove(stack[0], stack[1],
                (TIKU_TRK_NAV_HISTORY_MAX - 1u) * sizeof stack[0]);
        *count = TIKU_TRK_NAV_HISTORY_MAX - 1;
    }
    snprintf(stack[*count], TIKU_PATH_MAX, "%s", path);
    (*count)++;
}

void
tiku_trk_navigator_init(tiku_trk_navigator_t *n, const char *path)
{
    if (n == NULL) {
        return;
    }
    memset(n, 0, sizeof *n);
    append(n->back, &n->nback, (path != NULL && path[0] != '\0')
                                  ? path : "/");
}

const char *
tiku_trk_navigator_current(const tiku_trk_navigator_t *n)
{
    if (n == NULL || n->nback <= 0) {
        return "";
    }
    return n->back[n->nback - 1];
}

void
tiku_trk_navigator_set(tiku_trk_navigator_t *n, const char *path)
{
    if (n == NULL || path == NULL || path[0] == '\0' ||
        strcmp(tiku_trk_navigator_current(n), path) == 0) {
        return;
    }
    append(n->back, &n->nback, path);
    n->nforward = 0;
}

int
tiku_trk_navigator_can_back(const tiku_trk_navigator_t *n)
{
    return n != NULL && n->nback > 1;
}

int
tiku_trk_navigator_can_forward(const tiku_trk_navigator_t *n)
{
    return n != NULL && n->nforward > 0;
}

int
tiku_trk_navigator_back(tiku_trk_navigator_t *n, char *out, size_t max)
{
    if (out == NULL || max == 0u || !tiku_trk_navigator_can_back(n)) {
        return 0;
    }
    append(n->forward, &n->nforward, n->back[n->nback - 1]);
    n->nback--;
    snprintf(out, max, "%s", tiku_trk_navigator_current(n));
    return 1;
}

int
tiku_trk_navigator_forward(tiku_trk_navigator_t *n, char *out, size_t max)
{
    char path[TIKU_PATH_MAX];

    if (out == NULL || max == 0u || !tiku_trk_navigator_can_forward(n)) {
        return 0;
    }
    snprintf(path, sizeof path, "%s", n->forward[n->nforward - 1]);
    n->nforward--;
    append(n->back, &n->nback, path);
    snprintf(out, max, "%s", path);
    return 1;
}

int
tiku_trk_navigator_parent(const char *path, char *out, size_t max)
{
    char work[TIKU_PATH_MAX];
    char *slash;
    size_t len;

    if (path == NULL || out == NULL || max == 0u) {
        return 0;
    }
    snprintf(work, sizeof work, "%s", path);
    len = strlen(work);
    while (len > 1u && work[len - 1u] == '/') {
        work[--len] = '\0';
    }
    if (strcmp(work, "/") == 0) {
        return 0;
    }
    slash = strrchr(work, '/');
    if (slash == NULL || slash == work) {
        snprintf(out, max, "/");
    } else {
        *slash = '\0';
        snprintf(out, max, "%s", work);
    }
    return 1;
}

int
tiku_trk_navigator_hierarchy(const char *path,
                             char out[][TIKU_PATH_MAX], int max)
{
    char current[TIKU_PATH_MAX];
    int n = 0;

    if (path == NULL || out == NULL || max <= 0) {
        return 0;
    }
    snprintf(current, sizeof current, "%s", path[0] != '\0' ? path : "/");
    while (n < max && tiku_trk_navigator_parent(current,
                                                 out[n], TIKU_PATH_MAX)) {
        snprintf(current, sizeof current, "%s", out[n]);
        n++;
    }
    return n;
}

int
tiku_trk_navigator_drop_path(const tiku_model_t *model, char *out,
                             size_t max)
{
    if (model == NULL || out == NULL || max == 0u ||
        model->path[0] == '\0') {
        return 0;
    }
    if (tiku_model_is_container(model)) {
        snprintf(out, max, "%s", model->path);
        return 1;
    }
    return tiku_trk_navigator_parent(model->path, out, max);
}
