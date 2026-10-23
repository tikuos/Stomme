/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_app_layout.c - what follows a change: the title, the placement
 * of unplaced icons, the shared settings, the selection's paths and the
 * folder's writability.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_trk_app_private.h"

/** @brief The host window's title: the path, after "device:" and the
 *         device's id when the backend has one. */
void
browser_retitle(app_t *a)
{
    snprintf(a->title, sizeof a->title, "%s%s%s",
             (a->backend->devid[0] != '\0') ? "device:" : "",
             (a->backend->devid[0] != '\0') ? a->backend->devid : "",
             tiku_trk_poseview_path(a->view));
    if (a->backend->devid[0] == '\0') {
        snprintf(a->title, sizeof a->title, "%s",
                 tiku_trk_poseview_path(a->view));
    }
    tiku_host_set_title(a->host, a->title);
}

/**
 * @brief In the icon modes, place every pose the restore did not position.
 *
 * Saved positions cover only what the user arranged; new entries and icons
 * auto-placed last time are laid out here.  A pose with no position is not
 * drawn (PVL-020).
 */
void
browser_settle_layout(app_t *a)
{
    tiku_rect_t lr;

    if (tiku_trk_poseview_mode(a->view) == TIKU_TRK_MODE_LIST) {
        return;
    }
    lr = browser_list_rect(a);
    (void)tiku_trk_poseview_place_unplaced(a->view, lr.w - 4);
}

/**
 * @brief Re-read the shared settings file when it changes, at most every
 *        100 ms (at once with @p force), and apply ShowNavigator,
 *        HideDotFiles, SortFolderNamesFirst, TypeAheadFiltering,
 *        ReduceMotion and DarkMode.
 */
void
browser_sync_navigator_preference(app_t *a, int force)
{
    struct stat st;
    int present, visible = 1, hide_dotfiles = 0;
    int folders_first = 1, filter = 0, reduce_motion = 0;
    int64_t now = tiku_trk_now();
    time_t mtime = 0;
    off_t size = 0;

    if (!force && now - a->prefs_checked < 100000) {
        return;
    }
    a->prefs_checked = now;
    present = a->prefs_path[0] != '\0' && stat(a->prefs_path, &st) == 0;
    if (present) {
        mtime = st.st_mtime;
        size = st.st_size;
    }
    if (!force && present == a->prefs_present &&
        (!present || (mtime == a->prefs_mtime && size == a->prefs_size))) {
        return;
    }
    if (present) {
        tiku_trk_prefs_t prefs;
        int dark;

        tiku_trk_prefs_init(&prefs);
        (void)tiku_trk_prefs_load(&prefs, a->prefs_path);
        visible = tiku_trk_prefs_get(&prefs, "ShowNavigator") != 0;
        hide_dotfiles = tiku_trk_prefs_get(&prefs, "HideDotFiles") != 0;
        folders_first = tiku_trk_prefs_get(&prefs, "SortFolderNamesFirst") != 0;
        filter = tiku_trk_prefs_get(&prefs, "TypeAheadFiltering") != 0;
        reduce_motion = tiku_trk_prefs_get(&prefs, "ReduceMotion") != 0;
        dark = tiku_trk_prefs_get(&prefs, "DarkMode");
        if (dark < 0 || dark > 2) {
            dark = 0;
        }
        if (dark != a->theme_mode) {
            /* The theme's colour table is swapped at once; the browser
             * repaints whole frames, so a change costs one redraw. */
            a->theme_mode = dark;
            tiku_theme_set(dark == 1 ? tiku_theme_dark()
                         : dark == 2 ? tiku_theme_mono() : NULL);
            a->dirty = 1;
        }
        tiku_trk_prefs_free(&prefs);
    }
    a->prefs_present = present;
    a->prefs_mtime = mtime;
    a->prefs_size = size;
    if (a->reduce_motion != reduce_motion) {
        a->reduce_motion = reduce_motion;
        a->dirty = 1;
    }
    if (visible != a->show_nav) {
        a->show_nav = visible;
        tiku_trk_poseview_set_visible_rows(a->view, browser_visible_rows(a));
        browser_settle_layout(a);
        a->dirty = 1;
    }
    tiku_trk_poseview_set_hide_dotfiles(a->view, hide_dotfiles);
    tiku_trk_poseview_set_folders_first(a->view, folders_first);
    a->filter_pref = filter;
    if (tiku_trk_poseview_mode(a->view) == TIKU_TRK_MODE_LIST &&
        filter != tiku_trk_poseview_is_filtering(a->view)) {
        /* Live both ways: on engages filtering for the next keystroke,
         * off drops the terms and restores the listing (TS-052).  Only a
         * change of the setting resets it, so another setting changing
         * leaves a filter in use alone. */
        tiku_trk_poseview_set_filtering(a->view, filter);
        a->dirty = 1;
    }
}

/**
 * @brief Copy the selected poses' paths into @p paths, pointing @p out at
 *        each, at most @p max of them.  @return how many were copied.
 */
int
browser_selection_paths(app_t *a, char paths[][TIKU_PATH_MAX],
                const char *out[], int max)
{
    int i, n = tiku_trk_poseview_count(a->view), k = 0;

    for (i = 0; i < n && k < max; i++) {
        tiku_trk_pose_t *p = tiku_trk_poseview_at(a->view, i);
        if (p == NULL || !p->selected) {
            continue;
        }
        snprintf(paths[k], TIKU_PATH_MAX, "%s", p->model.path);
        out[k] = paths[k];
        k++;
    }
    return k;
}

/**
 * @brief Ask the store whether this folder can be written to (PVL-076).
 *
 * The folder is asked, not the backend: a device namespace has writable
 * and read-only regions side by side, and the tint follows the folder
 * shown.
 */
void
browser_settle_writability(app_t *a)
{
    tiku_model_t m;

    a->read_only = 0;
    if (a->backend == NULL || a->backend->ops->stat == NULL) {
        return;
    }
    if (a->backend->ops->stat(a->backend, tiku_trk_poseview_path(a->view),
                              &m) == 0) {
        a->read_only = ((m.facts.perm & TIKU_P_WRITE) == 0u);
    }
}
