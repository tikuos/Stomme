/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_poseview_spatial.c - the state that rides on the objects: each
 * entry's position, and the folder's view state, saved and restored.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_trk_poseview_private.h"

/**
 * @brief The node this view's own records live on (MA-064's three slots).
 *
 * The folder itself, except the root listing -- the Disks window -- which
 * keeps its state on the Desktop directory: the store cannot write on the
 * root.  The slots are told apart by attribute name: see state_attr().
 */
const char *
pv_state_node(const tiku_trk_poseview_t *v, char *buf, size_t max)
{
    if (v->state_key[0] == '\0' && strcmp(v->path, "/") == 0) {
        return tiku_desktop_dir(buf, max);
    }
    return v->path;
}

/**
 * @brief The attribute the view's state is kept under: the Desktop's, the
 *        Disks root's, or the ordinary folder one.
 */
static const char *
state_attr(const tiku_trk_poseview_t *v)
{
    if (v->state_key[0] != '\0') {
        return TIKU_ATTR_DESK_VIEW_STATE;
    }
    if (strcmp(v->path, "/") == 0) {
        return TIKU_ATTR_DISKS_VIEW_STATE;
    }
    return TIKU_ATTR_VIEW_STATE;
}

void
tiku_trk_poseview_set_no_autoscroll(tiku_trk_poseview_t *v, int on)
{
    if (v != NULL) {
        v->no_autoscroll = on;
    }
}

void
tiku_trk_poseview_set_state_slot(tiku_trk_poseview_t *v, const char *key)
{
    if (v != NULL) {
        snprintf(v->state_key, sizeof v->state_key, "%s",
                 key != NULL ? key : "");
    }
}

int
tiku_trk_poseview_last_icon_mode(const tiku_trk_poseview_t *v)
{
    return (v != NULL) ? v->last_icon : TIKU_TRK_MODE_ICON;
}

int
tiku_trk_poseview_restore_state(tiku_trk_poseview_t *v)
{
    tiku_store_t *store;
    int i, n = 0;

    if (v == NULL || v->backend->ops->state_store == NULL) {
        return 0;
    }
    store = v->backend->ops->state_store(v->backend);
    if (store == NULL) {
        return 0;
    }
    /* View state belongs to the folder; pose info to each entry. */
    {
        char rec[96];
        char node[TIKU_PATH_MAX];
        int len = tiku_state_read(store,
                                      pv_state_node(v, node, sizeof node),
                                      state_attr(v), rec, sizeof rec - 1);
        if (len > 0) {
            rec[len] = '\0';
            /* An unrecognised mode word discards the whole view-state
             * record, size and sort keys included (MA-063); the entries'
             * own records are still read below. */
            if (strncmp(rec, "icon", 4) == 0) { v->mode = TIKU_TRK_MODE_ICON; }
            else if (strncmp(rec, "mini", 4) == 0) { v->mode = TIKU_TRK_MODE_MINI; }
            else if (strncmp(rec, "list", 4) == 0) { v->mode = TIKU_TRK_MODE_LIST; }
            else { len = 0; }
        }
        if (len > 0) {
            int px = 0;

            /* IV-014: the size comes back with the mode.  A record without
             * a number leaves the constructor's size standing. */
            {
                int sc = 0, ix = 0, iy = 0, mx = 0, my = 0;
                int s1 = -1, s2 = -1, rev = 0, li = -1;
                int got = sscanf(rec, "%*s %d %d %d %d %d %d %d %d %d %d",
                                 &px, &sc, &ix, &iy, &mx, &my, &s1, &s2,
                                 &rev, &li);

                if (got >= 1) {
                    /* Clamped to 16..128, so the folder reopens at the
                     * nearest size it can show (MA-063). */
                    v->icon_size = px < 16 ? 16 : (px > 128 ? 128 : px);
                }
                if (got >= 6) {
                    /* Clamped to the content when the view is next given
                     * a viewport (PVL-053).  A value out of range reads as
                     * 0: the list scroll must be a non-negative row offset,
                     * and an icon origin, which may be negative, must lie
                     * within TIKU_TRK_SANE_LOCATION. */
                    v->scroll = pv_sane_scroll_origin(sc, 0);
                    v->icon_x = pv_sane_scroll_origin(ix, 1);
                    v->icon_y = pv_sane_scroll_origin(iy, 1);
                    v->mini_x = pv_sane_scroll_origin(mx, 1);
                    v->mini_y = pv_sane_scroll_origin(my, 1);
                }
                if (got >= 10) {
                    /* Which icon grid the stored positions belong to; a
                     * record without the word leaves the default standing
                     * (MA-061). */
                    v->last_icon = (li == 1) ? TIKU_TRK_MODE_MINI
                                             : TIKU_TRK_MODE_ICON;
                }
                if (got >= 9 && s1 >= 0 && s1 < TIKU_TRK_COL_KINDS) {
                    /* A record without the keys leaves the constructor's
                     * own key standing. */
                    v->sort_field = s1;
                    v->sort2_field = (s2 >= 0 && s2 < TIKU_TRK_COL_KINDS)
                                         ? s2 : -1;
                    v->sort_reverse = rev ? 1 : 0;
                    pv_resort(v);
                }
            }
        }
    }
    for (i = 0; i < v->count; i++) {
        pose_info_t info;
        char raw[sizeof info + 4];
        int got;

        memset(&info, 0, sizeof info);
        got = tiku_state_read(store, v->poses[i].model.path,
                                  TIKU_ATTR_POSE_INFO, raw, sizeof raw);
        if (got == (int)sizeof info) {
            memcpy(&info, raw, sizeof info);
        } else if (got == (int)sizeof(pose_info_v1_t)) {
            /* The older record without flags: the position is good and
             * nothing is hidden. */
            pose_info_v1_t old_info;

            memcpy(&old_info, raw, sizeof old_info);
            info.x = old_info.x;
            info.y = old_info.y;
        } else {
            /* A record of any other length is ignored. */
            continue;
        }
        /* A position beyond TIKU_TRK_SANE_LOCATION is ignored and the icon
         * auto-placed instead. */
        if (info.x > TIKU_TRK_SANE_LOCATION || info.x < -TIKU_TRK_SANE_LOCATION ||
            info.y > TIKU_TRK_SANE_LOCATION || info.y < -TIKU_TRK_SANE_LOCATION) {
            continue;
        }
        v->poses[i].invisible = (info.flags & POSE_INFO_INVISIBLE) ? 1 : 0;
        v->poses[i].loc_x = info.x;
        v->poses[i].loc_y = info.y;
        v->poses[i].placed = 1;
        v->poses[i].auto_placed = 0;
        n++;
    }
    if (n > 0) {
        pv_extent_clear(v);     /* the box is rebuilt from the positions */
    }
    /* The invisible bit is known only once the saved records are read, so
     * the listing is filtered here. */
    pv_apply_visibility(v);
    v->state_dirty = 0;
    return n;
}

int
tiku_trk_poseview_save_state(tiku_trk_poseview_t *v)
{
    tiku_store_t *store;
    int i, n = 0;

    if (v == NULL || v->is_query || !v->state_dirty ||
        v->backend->ops->state_store == NULL) {
        return 0;               /* nothing changed: nothing written */
    }
    store = v->backend->ops->state_store(v->backend);
    if (store == NULL || !tiku_store_writable(store)) {
        return 0;               /* a read-only store: nothing written */
    }
    {
        /* IV-014: the icon size is saved with the mode, so a folder
         * reopens at the size it was arranged at. */
        const char *mode = (v->mode == TIKU_TRK_MODE_ICON) ? "icon"
                         : (v->mode == TIKU_TRK_MODE_MINI) ? "mini" : "list";
        char rec[96];

        /* The list scroll and both icon origins are saved, each in its
         * own field, so a folder reopens where it was scrolled (MA-061). */
        /* The sort keys go with the rest, so a folder arranged by size and
         * closed comes back by size (MA-061, PVL-041). */
        snprintf(rec, sizeof rec, "%s %d %d %d %d %d %d %d %d %d %d", mode,
                 v->icon_size, v->scroll, v->icon_x, v->icon_y, v->mini_x,
                 v->mini_y, v->sort_field, v->sort2_field,
                 v->sort_reverse,
                 (v->last_icon == TIKU_TRK_MODE_MINI) ? 1 : 0);
        char node[TIKU_PATH_MAX];

        (void)tiku_state_write(store,
                                   pv_state_node(v, node, sizeof node),
                                   state_attr(v), rec, strlen(rec));
    }
    for (i = 0; i < v->count; i++) {
        pose_info_t info;

        /* An auto-placed icon's position is written too, so the folder
         * reopens with its icons where they were. */
        if (!v->poses[i].placed) {
            continue;
        }
        memset(&info, 0, sizeof info);
        info.x = (int32_t)v->poses[i].loc_x;
        info.y = (int32_t)v->poses[i].loc_y;
        if (v->poses[i].invisible) {
            info.flags |= POSE_INFO_INVISIBLE;
        }
        if (tiku_state_write(store, v->poses[i].model.path,
                                 TIKU_ATTR_POSE_INFO, &info,
                                 sizeof info) == 0) {
            n++;
        }
    }
    (void)tiku_store_flush(store);
    v->state_dirty = 0;
    return n;
}
