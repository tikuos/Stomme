/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_desktop_infowin.c - the Info window as a responder: its tabs, its
 * name, its permission boxes, its icon (a drag source, a drop target and a
 * menu) and its fields.  The floating field pop-up belongs to the
 * info-hover responder.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_trk_desktop_private.h"

/** @brief The focused Info window's slot when it is one, else -1. */
static int
focused_slot(desktop_t *app)
{
    return tiku_trk_info_windows_slot(&app->info_windows,
                                      tiku_workspace_focused(app->desk));
}

/** @brief A point on a part of the focused Info window, while no alert is
 *         open. */
static int
info_hit(void *self, int x, int y)
{
    desktop_t *app = self;
    int islot = focused_slot(app), which = -1;

    if (islot < 0 || alert_model(app)->open) {
        return 0;
    }
    return tiku_trk_info_windows_hit(&app->info_windows, islot, x, y,
                                     &which) != TIKU_TRK_INFO_HIT_NONE;
}

/**
 * @brief Open the icon's menu: the context menu for the window's item, plus
 *        Recalculate folder size for a folder and Set new link target for a
 *        link.
 * @return 1 when the item could be read.
 */
static int
icon_menu(desktop_t *app, tiku_trk_info_t *info, const tiku_event_t *ev)
{
    tiku_model_t m;
    tiku_trk_sc_ctx_t c = menu_ctx(app, desktop_folder(app), 0u);

    if (app->backend->ops->stat(app->backend, info->path, &m) != 0) {
        return 0;
    }
    c.selection = 1;
    c.selection_is_volume = m.kind == TIKU_KIND_VOLUME;
    c.selection_ro = !tiku_model_is_writable(&m);
    c.can_trash_or_dup = !c.selection_ro;
    c.can_edit_name = m.kind != TIKU_KIND_TRASH &&
                      m.kind != TIKU_KIND_DESKTOP && !c.selection_ro;
    snprintf(app->popup_path, sizeof app->popup_path, "%s", info->path);
    app->popup = tiku_trk_cwmenu_context(&app->menus, &m, &c);
    app->menus.info_ctx = *app->popup;
    app->popup = &app->menus.info_ctx;
    if (tiku_model_is_container(&m) && m.kind != TIKU_KIND_VOLUME &&
        m.kind != TIKU_KIND_ROOT) {
        (void)tiku_menu_add(app->popup, "Recalculate folder size",
                            TIKU_TRK_CMD_RECALC_SIZE, 0, 0u, 1);
    }
    if (m.kind == TIKU_KIND_SYMLINK) {
        (void)tiku_menu_add(app->popup, "Set new link target",
                            TIKU_TRK_CMD_SET_LINK_TARGET, 0, 0u, 1);
    }
    app->popup_sub = NULL;
    app->popup_frame = tiku_menu_place(app->popup, ev->x, ev->y,
        (tiku_rect_t){ 0, 0, app->surf->w, app->surf->h });
    return 1;
}

/** @brief A press on the icon: its menu, a second press on the same icon
 *         within half a second opens, a press that moves carries the item
 *         (AW-075). */
static void
icon_press(desktop_t *app, tiku_window_t *iw, tiku_trk_info_t *info,
           const tiku_event_t *ev)
{
    if (ev->button == 3u) {
        (void)icon_menu(app, info, ev);
        return;
    }
    if (app->info_icon_win == iw &&
        tiku_trk_now() - app->info_icon_at < 500000) {
        (void)open_folder(app, info->path);
        app->info_icon_at = tiku_trk_now();
        return;
    }
    app->info_icon_at = tiku_trk_now();
    app->info_icon_win = iw;
    if (ev->button == 1u && info->model.kind != TIKU_KIND_TRASH) {
        /* The Trash is never carried, only opened. */
        char iparent[TIKU_PATH_MAX];
        tiku_model_t im;

        if (app->backend->ops->stat(app->backend, info->path, &im) == 0) {
            parent_path(info->path, iparent, sizeof iparent);
            tiku_trk_drag_arm(&app->drag, ev->x, ev->y, ev->button, 0u, 1,
                              iw, iparent);
            (void)tiku_trk_drag_add_at(&app->drag, info->path, im.type,
                                       ev->x, ev->y, 1);
            tiku_trk_drag_target_reset(&app->drag_target);
        }
    }
}

/**
 * @brief A press on a field: Opens with steps to the next application, a
 *        folder's size starts counting, and the location and the link
 *        target open the folder that holds them, with the entry selected;
 *        a link whose target is gone opens the file panel to re-point it.
 */
static void
field_press(desktop_t *app, int islot, tiku_trk_info_t *info, int which)
{
    char value[TIKU_TRK_INFO_VAL_MAX];
    const char *label = "";
    tiku_trk_info_field_t fk = tiku_trk_info_field(info, app->store, which,
                                                   &label, value,
                                                   sizeof value);
    char dir[TIKU_PATH_MAX];
    char sel[TIKU_NAME_MAX];

    if (fk == TIKU_TRK_INFO_F_OPENS_WITH) {
        info_opens_with_next(app, info);
        return;
    }
    if (fk == TIKU_TRK_INFO_F_SIZE && tiku_model_is_container(&info->model)) {
        tiku_trk_info_calc_start(info);
    } else if (fk == TIKU_TRK_INFO_F_LOCATION &&
               tiku_trk_info_location(info, dir, sizeof dir, sel,
                                      sizeof sel)) {
        folder_t *nf = open_folder(app, dir);

        if (nf != NULL) {
            select_named(app, nf, sel);
        }
    } else if (fk == TIKU_TRK_INFO_F_LINK) {
        if (tiku_trk_info_link_target(info, app->backend, dir, sizeof dir,
                                      sel, sizeof sel)) {
            folder_t *nf = open_folder(app, dir);

            if (nf != NULL) {
                select_named(app, nf, sel);
            }
        } else {
            /* A link whose target is gone: the picker re-points it
             * (AW-074). */
            folder_t *ffl = folder_of(app,
                                      tiku_workspace_focused(app->desk));
            folder_t *start = (ffl != NULL) ? ffl : &app->folders[0];

            snprintf(app->info_relink_path, sizeof app->info_relink_path,
                     "%s", app->info_windows.model[islot].path);
            tiku_trk_panel_window_open_any(&app->file_panel, start,
                                           TIKU_TRK_CMD_GET_INFO,
                                           start->path);
        }
    }
}

/**
 * @brief A press on the focused Info window's parts.
 * @return 1 when a part took the press.
 */
static int
info_press(void *self, const tiku_event_t *ev, tiku_rect_t *damage)
{
    desktop_t *app = self;
    tiku_window_t *iw = tiku_workspace_focused(app->desk);
    int islot = focused_slot(app), which = -1;
    tiku_trk_info_t *info;
    tiku_trk_info_hit_t ih;

    *damage = tiku_trk_damage_none();
    if (islot < 0 || alert_model(app)->open) {
        return 0;
    }
    info = tiku_trk_info_windows_model(&app->info_windows, islot);
    ih = tiku_trk_info_windows_hit(&app->info_windows, islot, ev->x, ev->y,
                                   &which);
    switch (ih) {
    case TIKU_TRK_INFO_HIT_TAB:
        if (which < 0) {
            return 0;
        }
        /* The tab strip owns which is current; the window re-reads the
         * Attributes tab as it becomes current. */
        tiku_trk_info_windows_select_tab(&app->info_windows, islot, which);
        *damage = tiku_trk_damage_all();
        return 1;
    case TIKU_TRK_INFO_HIT_NAME:
        tiku_trk_info_windows_begin_rename(&app->info_windows, islot);
        return 1;
    case TIKU_TRK_INFO_HIT_PERMISSION: {
        char perr[160];

        if (which < 0) {
            return 0;
        }
        if (tiku_trk_info_perm_toggle(info, app->backend, which, perr,
                                      sizeof perr) != 0) {
            tiku_sound_play(TIKU_SOUND_BEEP);
            tiku_alert_open(alert_model(app), TIKU_ALERT_WARN,
                            TIKU_TRK_ALERT_TAG_ERROR, perr, "Cancel");
        }
        *damage = tiku_trk_damage_all();
        return 1;
    }
    case TIKU_TRK_INFO_HIT_ICON:
        icon_press(app, iw, info, ev);
        *damage = tiku_trk_damage_all();
        return 1;
    case TIKU_TRK_INFO_HIT_FIELD:
        if (which < 0) {
            return 0;
        }
        field_press(app, islot, info, which);
        *damage = tiku_trk_damage_all();
        return 1;
    default:
        return 0;
    }
}

/** @brief An Info window's frame as a damage; none for no window. */
static tiku_rect_t
info_slot_frame(desktop_t *app, int slot)
{
    tiku_window_t *w = (slot >= 0)
        ? tiku_trk_info_windows_handle(&app->info_windows, slot) : NULL;

    return (w != NULL) ? w->frame : tiku_trk_damage_none();
}

/**
 * @brief A drag over an Info window's icon highlights it as a target, and
 *        the highlight goes when the drag leaves (IV-058).
 */
static int
info_move(void *self, const tiku_event_t *ev, tiku_rect_t *damage)
{
    desktop_t *app = self;
    int hot = tiku_trk_drag_active(&app->drag)
                  ? info_icon_drop_slot(app, ev->x, ev->y) : -1;

    *damage = tiku_trk_damage_none();
    if (hot != app->info_windows.drop_hot) {
        *damage = info_slot_frame(app, app->info_windows.drop_hot);
        app->info_windows.drop_hot = hot;
        *damage = tiku_trk_damage_union(*damage, info_slot_frame(app, hot));
    }
    return 0;
}

/** @brief The Info windows' keys: their rename editor and shortcuts. */
static int
info_key(void *self, const tiku_event_t *ev, tiku_rect_t *damage)
{
    desktop_t *app = self;

    *damage = tiku_trk_damage_none();
    if (ev->type != TIKU_EVENT_KEY_DOWN) {
        return 0;
    }
    if (tiku_trk_info_windows_key(&app->info_windows, ev->key,
                                  ev->modifiers,
                                  key_char(ev))) {
        return 1;
    }
    return 0;
}

/** @brief Put the Info windows' parts on the responder list. */
void
desktop_infowin_attach(desktop_t *app)
{
    app->infowin_responder = (tiku_trk_responder_t){
        .name = "info", .self = app, .hit = info_hit, .press = info_press,
        .move = info_move, .key = info_key
    };
    (void)tiku_trk_responders_push(&app->responders, &app->infowin_responder);
}
