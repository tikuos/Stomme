/*
 * KintsugiOS v0.01 kits -- the foundation KintsugiDesktop and KintsugiTracker.
 * Authors: Ambuj Varshney <ambuj@kinstugi-os.org>
 * tiku_window_alloc.c - native workspace allocation and display-list recording.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_window.h"
#include "tiku_dl.h"
#include <stdlib.h>

/** @brief Allocate a native owned surface at the requested logical scale. */
static tiku_surface_t *create_surface(void *context, int w, int h, int scale, tiku_rgb_t bg)
{
    (void)context;
    tiku_surface_t *surface = tiku_surface_new(w, h, bg);
    if (!surface) return NULL;
    if (scale > 1 && tiku_surface_rescale(surface, scale, bg)) {
        tiku_surface_free(surface); return NULL;
    }
    surface->scale = scale;
    return surface;
}
/** @brief Capture an owned native snapshot for a closing or minimizing window. */
static tiku_surface_t *capture_surface(void *context, const tiku_surface_t *surface, tiku_rect_t rect)
{ (void)context; return tiku_grab(surface, rect); }
/** @brief Release a native owned surface. */
static void release_surface(void *context, tiku_surface_t *surface)
{ (void)context; tiku_surface_free(surface); }
tiku_workspace_t *tiku_workspace_new(tiku_surface_t *surface)
{
    void *storage = malloc(tiku_workspace_size());
    const tiku_workspace_surfaces_t operations = {NULL,create_surface,capture_surface,release_surface};
    tiku_workspace_t *workspace = tiku_workspace_init(storage, tiku_workspace_size(), surface, &operations);
    if (!workspace) free(storage);
    return workspace;
}
void tiku_workspace_free(tiku_workspace_t *workspace)
{ tiku_workspace_deinit(workspace); free(workspace); }

/**
 * @brief Draw @p window's content once onto a recording surface of the
 *        workspace's size, never the shown one, and return the list, or
 *        NULL when there is nothing to record.
 */
static tiku_dl_t *
workspace_record(tiku_workspace_t *workspace, tiku_window_t *window)
{
    tiku_surface_t *scratch;
    tiku_dl_t *dl;
    tiku_rect_t content;

    if (workspace == NULL || tiku_workspace_surface(workspace) == NULL ||
        window == NULL || !window->open || window->draw == NULL) {
        return NULL;
    }
    content = tiku_window_content(window);
    if (content.w <= 0 || content.h <= 0) {
        return NULL;
    }
    /* A scratch surface of the workspace's size: recording leaves the
     * shown pixels alone. */
    scratch = tiku_surface_new(tiku_workspace_surface(workspace)->w,
                                    tiku_workspace_surface(workspace)->h, TIKU_C_PANEL);
    if (scratch == NULL) {
        return NULL;
    }
    dl = tiku_dl_new();
    if (dl == NULL) {
        tiku_surface_free(scratch);
        return NULL;
    }
    scratch->record = dl;
    tiku_clip_set(scratch, content);
    window->draw(window, scratch, content, window->context);
    tiku_clip_reset(scratch);
    scratch->record = NULL;
    tiku_surface_free(scratch);
    return dl;
}

size_t
tiku_workspace_narrate(tiku_workspace_t *workspace,
                            tiku_window_t *window, char *out, size_t max)
{
    tiku_dl_t *dl;
    size_t used;

    if (out == NULL || max == 0u) {
        return 0u;
    }
    out[0] = '\0';
    dl = workspace_record(workspace, window);
    if (dl == NULL) {
        return 0u;
    }
    used = tiku_dl_say(dl, out, max);
    tiku_dl_free(dl);
    return used;
}

size_t
tiku_workspace_controls(tiku_workspace_t *workspace,
                             tiku_window_t *window, char *out, size_t max)
{
    tiku_dl_t *dl;
    size_t used;

    if (out == NULL || max == 0u) {
        return 0u;
    }
    out[0] = '\0';
    dl = workspace_record(workspace, window);
    if (dl == NULL) {
        return 0u;
    }
    used = tiku_dl_facts(dl, out, max);
    tiku_dl_free(dl);
    return used;
}
