/*
 * Desktop shell and file manager.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_desktop_globalbar.c - the global menu bar: the strip along the
 * top of the screen, the menus the focused window publishes into it, the
 * system and Devices menus and their commands; and the key helpers the
 * responders share.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_shell_product.h"
#include "tiku_trk_desktop_private.h"

/** @brief The strip's rectangle, empty when the bar is off. */
tiku_rect_t
global_bar_rect(desktop_t *app)
{
    if (app->gbar_on != 1) {
        return (tiku_rect_t){ 0, 0, 0, 0 };
    }
    return (tiku_rect_t){ 0, 0, app->surf->w,
                               app->menus.bar.height };
}

/**
 * @brief Draw the status area at the bar's right: a camera button that
 *        takes a screenshot, and left of it the device tray, which stops
 *        short of the menu titles.  The camera's rectangle is kept in
 *        app->shot_btn for presses.
 */
void
global_bar_status_draw(desktop_t *app, tiku_rect_t strip)
{
    int bw = 18, bh = 13;               /* the camera body */
    int cell = bw + 16;
    int x0 = strip.x + strip.w - cell;  /* the carved cell's left edge */
    int bx = x0 + (cell - bw) / 2;
    int by = strip.y + (strip.h - bh) / 2 + 1;
    int cx = bx + bw / 2, cy = by + bh / 2 + 1, r = 4, i;
#if TIKU_SHELL_BOARD_SERVICES

    int motion = app->host != NULL &&
                 !tiku_trk_prefs_get(prefs_model(app), "ReduceMotion");

    desktop_devtray_draw(app, strip,
                         strip.x + tiku_menubar_width(&app->gbar), x0,
                         motion ? tiku_trk_now() : 0);
#endif


    app->shot_btn = (tiku_rect_t){ x0, strip.y, cell, strip.h };
    /* The cell is a little darker than the bar, darker still while
     * pressed, with an engraved line down its left: a groove, then a
     * highlight. */
    tiku_fill(app->surf, app->shot_btn,
              tiku_tint(TIKU_C_PANEL, app->shot_pressed ? 0.82f : 0.95f));
    tiku_vline(app->surf, x0, strip.y, strip.h - 1,
               tiku_tint(TIKU_C_PANEL, 0.72f));
    tiku_vline(app->surf, x0 + 1, strip.y, strip.h - 1,
               tiku_tint(TIKU_C_PANEL, 1.18f));
    /* The camera in line art: a body outline, a viewfinder bump, a lens
     * ring and a flash dot. */
    tiku_hline(app->surf, bx, by, bw, TIKU_C_TEXT);
    tiku_hline(app->surf, bx, by + bh - 1, bw, TIKU_C_TEXT);
    tiku_vline(app->surf, bx, by, bh, TIKU_C_TEXT);
    tiku_vline(app->surf, bx + bw - 1, by, bh, TIKU_C_TEXT);
    tiku_hline(app->surf, bx + 3, by - 2, 5, TIKU_C_TEXT);   /* viewfinder */
    tiku_vline(app->surf, bx + 3, by - 2, 2, TIKU_C_TEXT);
    tiku_vline(app->surf, bx + 7, by - 2, 2, TIKU_C_TEXT);
    tiku_fill(app->surf, (tiku_rect_t){ bx + bw - 4, by + 2, 2, 2 },
              TIKU_C_TEXT);                                  /* flash */
    for (i = -r; i <= r; i++) {                              /* lens ring */
        int half = (int)(0.5 + __builtin_sqrt((double)(r * r - i * i)));

        tiku_fill(app->surf, (tiku_rect_t){ cx - half, cy + i, 1, 1 },
                  TIKU_C_TEXT);
        tiku_fill(app->surf, (tiku_rect_t){ cx + half, cy + i, 1, 1 },
                  TIKU_C_TEXT);
    }
    tiku_fill(app->surf, (tiku_rect_t){ cx - 1, cy - 1, 2, 2 }, TIKU_C_TEXT);
#if TIKU_SHELL_BOARD_SERVICES

    desktop_devtray_draw_tip(app);
#endif

}

/**
 * @brief A pick from a folder window's published menus: run the command on
 *        that folder through menu_invoke().
 */
static void
folder_menu_pick(tiku_window_t *window, int command, void *context)
{
    desktop_t *app = context;
    folder_t *fl = folder_of(app, window);

    if (fl != NULL) {
        menu_invoke(app, fl, command);
    }
}

/**
 * @brief Describe folder @p fl's menu bar as a flat menu set, a submenu as
 *        level-1 entries under its holder, and publish it on the folder's
 *        window.
 */
void
publish_folder_menus(desktop_t *app, folder_t *fl)
{
    tiku_menuset_t set;
    int m;

    if (fl == NULL || fl->win == NULL) {
        return;
    }
    memset(&set, 0, sizeof set);
    for (m = 0; m < app->menus.bar.count &&
                set.nmenu < TIKU_MENUSET_MENUS; m++) {
        const tiku_menu_t *from = app->menus.bar.menu[m];
        tiku_menu_list_t *to = &set.menu[set.nmenu];
        int i;

        snprintf(to->title, sizeof to->title, "%s", from->title);
        for (i = 0; i < from->count; i++) {
            const tiku_menu_item_t *fi = &from->item[i];
            tiku_menu_entry_t *ti;

            if (to->nitem >= TIKU_MENUSET_ITEMS) {
                break;
            }
            ti = &to->item[to->nitem++];
            snprintf(ti->label, sizeof ti->label, "%s", fi->label);
            ti->command = fi->command;
            ti->sc = fi->shortcut;
            ti->mods = fi->mods;
            ti->enabled = (unsigned char)!!fi->enabled;
            ti->marked = (unsigned char)!!fi->marked;
            ti->separator = (unsigned char)!!fi->separator;
            ti->level = 0;
            if (fi->submenu != NULL) {
                /* The submenu follows its holder as level-1 entries. */
                const tiku_menu_t *sub = fi->submenu;
                int k;

                for (k = 0; k < sub->count &&
                            to->nitem < TIKU_MENUSET_ITEMS; k++) {
                    const tiku_menu_item_t *si = &sub->item[k];
                    tiku_menu_entry_t *st = &to->item[to->nitem++];

                    snprintf(st->label, sizeof st->label, "%s", si->label);
                    st->command = si->command;
                    st->sc = si->shortcut;
                    st->mods = si->mods;
                    st->enabled = (unsigned char)!!si->enabled;
                    st->marked = (unsigned char)!!si->marked;
                    st->separator = (unsigned char)!!si->separator;
                    st->level = 1;
                }
            }
        }
        set.nmenu++;
    }
    tiku_window_publish_menus(fl->win, &set, folder_menu_pick, app);
#if TIKU_SHELL_BOARD_SERVICES

}

/**
 * @brief The @p idx-th connected board's leaf, in the name order the
 *        Devices menu is built in, so a pick's index maps back.
 * @return 1 when there is one.
 */
static int
device_leaf_at(desktop_t *app, int idx, char *out, size_t max)
{
    char names[512];
    const char *p = names;
    int i = 0;

    out[0] = '\0';
    if (idx < 0 || desktop_board_names(app, names, sizeof names) <= 0) {
        return 0;
    }
    while (*p != '\0') {
        const char *nl = strchr(p, '\n');
        size_t len = (nl != NULL) ? (size_t)(nl - p) : strlen(p);

        if (i == idx) {
            if (len >= max) {
                len = max - 1u;
            }
            memcpy(out, p, len);
            out[len] = '\0';
            return 1;
        }
        i++;
        if (nl == NULL) {
            break;
        }
        p = nl + 1;
    }
    return 0;
}

/** @brief "<@p lead><@p name> (<@p port>)" into @p out, the name cut so
 *         the whole fits a menu label. */
static void
device_row(char *out, size_t max, const char *lead, const char *name,
           const char *port)
{
    int keep = (TIKU_MENU_LABEL_MAX - 1) - (int)strlen(lead) -
               (int)strlen(port) - 3;

    snprintf(out, max, "%s%.*s (%s)", lead, (keep > 4) ? keep : 4, name,
             port);
}

/**
 * @brief Build the Devices menu: the Default device submenu (this machine
 *        and each connected board, the current one checked), a Disconnect
 *        row per connected board, a Connect row per port a board answered
 *        on, and Scan for devices.  Ports are named so no two read alike.
 */
static void
build_devices_menu(desktop_t *app)
{
    char leaf[64], wheres[TIKU_MOUNT_MAX][32];
    const char *all[2 * TIKU_MOUNT_MAX];
    int i, any, nall = 0;

    /* Every port the menu names, connected or on offer: a port's label
     * keeps enough of its leaf to tell it from these. */
    for (i = 0; device_leaf_at(app, i, leaf, sizeof leaf) &&
                i < TIKU_MOUNT_MAX; i++) {
        char port[256];

        wheres[i][0] = '\0';
        if (desktop_board_port(app, leaf, port, sizeof port) > 0) {
            device_port_where(port, wheres[i], sizeof wheres[i]);
            all[nall++] = wheres[i];
        }
    }
    for (i = 0; i < app->ncandidates && i < TIKU_MOUNT_MAX; i++) {
        all[nall++] = app->candidates[i].where;
    }

    tiku_menu_clear(&app->devmenu);
    tiku_menu_clear(&app->devsub);
    snprintf(app->devmenu.title, sizeof app->devmenu.title, "Devices");
    snprintf(app->devsub.title, sizeof app->devsub.title, "Default device");
    (void)tiku_menu_add(&app->devsub, "This machine",
                        TIKU_TRK_CMD_DEVICE_HOST, 0, 0u, 1);
    tiku_menu_set_icon(&app->devsub, TIKU_TRK_CMD_DEVICE_HOST, "computer");
    tiku_menu_mark(&app->devsub, TIKU_TRK_CMD_DEVICE_HOST,
                   app->current_device[0] == '\0');
    snprintf(leaf, sizeof leaf, "Default device: %.31s",
             app->current_device[0] ? app->current_device : "This machine");
    (void)tiku_menu_add_submenu(&app->devmenu, leaf, &app->devsub, 1);
    (void)tiku_menu_add_separator(&app->devmenu);
    (void)tiku_menu_add(&app->devmenu, "Connected devices", 0, 0, 0u, 0);
    any = 0;
    for (i = 0; device_leaf_at(app, i, leaf, sizeof leaf) &&
                i < TIKU_MOUNT_MAX; i++) {
        int cmd = TIKU_TRK_CMD_DEVICE_PICK_BASE + i;
        char row[TIKU_MENU_LABEL_MAX], action[TIKU_MENU_LABEL_MAX];
        char label[32];

        if (!any) {
            (void)tiku_menu_add_separator(&app->devsub);
            any = 1;
        }
        /* The name, then the port, which tells identical boards apart;
         * the name is cut to fit the menu's label, the port is not. */
        if (wheres[i][0] != '\0') {
            device_port_label(wheres[i], all, nall, label, sizeof label);
            device_row(row, sizeof row, "", leaf, label);
            device_row(action, sizeof action, "Disconnect ", leaf, label);
        } else {
            snprintf(row, sizeof row, "%.35s", leaf);
            snprintf(action, sizeof action, "Disconnect %.35s", leaf);
        }
        (void)tiku_menu_add(&app->devsub, row, cmd, 0, 0u, 1);
        tiku_menu_set_icon(&app->devsub, cmd, "microcontroller");
        if (strcmp(leaf, app->current_device) == 0) {
            tiku_menu_mark(&app->devsub, cmd, 1);
        }
        (void)tiku_menu_add(&app->devmenu, action,
                            TIKU_TRK_CMD_DEVICE_DROP_BASE + i, 0, 0u, 1);
        tiku_menu_set_icon(&app->devmenu,
                           TIKU_TRK_CMD_DEVICE_DROP_BASE + i,
                           "microcontroller");
    }
    if (!any) {
        (void)tiku_menu_add(&app->devmenu, "No devices connected",
                            0, 0, 0u, 0);
    }
    any = 0;
    for (i = 0; i < app->ncandidates && i < TIKU_MOUNT_MAX; i++) {
        char row[TIKU_MENU_LABEL_MAX], label[32];

        if (port_is_mounted(app, app->candidates[i].port)) {
            continue;           /* already connected, shown above */
        }
        /* Only a port a board answered on is offered: a candidate gets
         * its name from a look's probe.  A debug probe's extra ports
         * answer nothing and are left out. */
        if (app->candidates[i].name[0] == '\0') {
            continue;
        }
        if (!any) {
            (void)tiku_menu_add_separator(&app->devmenu);
            (void)tiku_menu_add(&app->devmenu, "Available devices",
                                0, 0, 0u, 0);
            any = 1;
        }
        device_port_label(app->candidates[i].where, all, nall, label,
                          sizeof label);
        device_row(row, sizeof row, "Connect to ", app->candidates[i].name,
                   label);
        (void)tiku_menu_add(&app->devmenu, row,
                            TIKU_TRK_CMD_DEVICE_CONNECT_BASE + i, 0, 0u, 1);
        tiku_menu_set_icon(&app->devmenu,
                           TIKU_TRK_CMD_DEVICE_CONNECT_BASE + i,
                           "microcontroller");
    }
    (void)tiku_menu_add_separator(&app->devmenu);
    (void)tiku_menu_add(&app->devmenu, "Scan for devices",
                        TIKU_TRK_CMD_DEVICE_SCAN,
                        0, 0u, 1);
    tiku_menu_set_icon(&app->devmenu, TIKU_TRK_CMD_DEVICE_SCAN, "magnifier");
#endif

}

/**
 * @brief Rebuild the strip's menus from what the focused window published,
 *        with the system menu first and the Devices menu last.
 */
void
rebuild_global_bar(desktop_t *app)
{
    tiku_window_t *fw = tiku_workspace_focused(app->desk);
    const tiku_menuset_t *set =
        (fw != NULL) ? tiku_window_menus(fw) : NULL;
    int was_open = app->gbar_open;
    int m;

    tiku_menubar_init(&app->gbar);
    /*
     * The first top-level row whose label starts with "About " moves from
     * the focused window's menus into the system menu.  The label is all
     * that marks it.
     */
    app->about_hoist = 0;
    if (set != NULL) {
        for (m = 0; m < set->nmenu && app->about_hoist == 0; m++) {
            int i;

            for (i = 0; i < set->menu[m].nitem; i++) {
                if (strncmp(set->menu[m].item[i].label, "About ", 6)
                        == 0 &&
                    set->menu[m].item[i].level == 0) {
                    app->about_hoist = set->menu[m].item[i].command;
                    snprintf(app->about_label, sizeof app->about_label,
                             "%s", set->menu[m].item[i].label);
                    break;
                }
            }
        }
    }
    /*
     * The system menu: About (the focused window's, when it published
     * one) and Help (when desktop_help_page_of() has a page for the
     * focused window); with no application window focused, also Find,
     * Preferences, Take Screenshot and Shut down.  The Deskbar's leaf menu
     * always has Shut down.
     */
    tiku_menu_clear(&app->sysmenu);
    snprintf(app->sysmenu.title, sizeof app->sysmenu.title,
             TIKU_SHELL_MENU_NAME);
    app->sysmenu.mark = 1;      /* drawn with the system mark */
    {
        int program = (fw != NULL &&
                       (embed_win_of_window(app, fw) != NULL ||
                        tiku_remote_owner(&app->remote, fw) != NULL));

        const char *page = desktop_help_page_of(app, fw);

        if (app->about_hoist != 0) {
            (void)tiku_menu_add(&app->sysmenu, app->about_label,
                                    TIKU_TRK_CMD_APP_ABOUT, 0, 0u, 1);
            tiku_menu_set_icon(&app->sysmenu, TIKU_TRK_CMD_APP_ABOUT,
                                    MARK_ICON);
        } else {
            (void)tiku_menu_add(&app->sysmenu, tiku_str("cmd.about"),
                                    TIKU_TRK_CMD_ABOUT, 0, 0u, 1);
            tiku_menu_set_icon(&app->sysmenu, TIKU_TRK_CMD_ABOUT,
                                    MARK_ICON);
        }
        /* The Help row shows only when the focused window has a help page
         * (its application's own, or a fixed page for one of the desktop's
         * windows) and a help folder was found. */
        if (page != NULL && desktop_help_ready(app)) {
            snprintf(app->help_row, sizeof app->help_row, "%s", page);
            (void)tiku_menu_add(&app->sysmenu, tiku_str("menu.help"),
                                    TIKU_TRK_CMD_APP_HELP, 0, 0u, 1);
        } else {
            app->help_row[0] = '\0';
        }
        if (!program) {
            (void)tiku_menu_add_separator(&app->sysmenu);
            (void)tiku_menu_add(&app->sysmenu, tiku_str("cmd.find"),
                                    TIKU_TRK_CMD_FIND,
                                    'f', TIKU_TRK_MOD_CMD, 1);
                        (void)tiku_menu_add(&app->sysmenu,
                                    tiku_str("cmd.preferences"),
                                    TIKU_TRK_CMD_PREFERENCES, 0, 0u, 1);
            (void)tiku_menu_add_separator(&app->sysmenu);
            (void)tiku_menu_add(&app->sysmenu,
                                    tiku_str("cmd.screenshot"),
                                    TIKU_TRK_CMD_SCREENSHOT, '3',
                                    TIKU_TRK_MOD_CMD | TIKU_TRK_MOD_SHIFT,
                                    1);
            (void)tiku_menu_add_separator(&app->sysmenu);
            (void)tiku_menu_add(&app->sysmenu, tiku_str("cmd.shutdown"),
                                    TIKU_TRK_CMD_SHUTDOWN, 0, 0u, 1);
        }
    }
    (void)tiku_menubar_add(&app->gbar, &app->sysmenu);
    app->gbar_from = (set != NULL) ? fw : NULL;
    for (m = 0; set != NULL && m < set->nmenu; m++) {
        const tiku_menu_list_t *from = &set->menu[m];
        tiku_menu_t *to = &app->gmenu[m];
        int i, nsub = 0;

        tiku_menu_clear(to);
        snprintf(to->title, sizeof to->title, "%s", from->title);
        for (i = 0; i < from->nitem; i++) {
            const tiku_menu_entry_t *fi = &from->item[i];

            if (fi->level == 1) {
                continue;           /* consumed with its holder below */
            }
            if (app->about_hoist != 0 &&
                fi->command == app->about_hoist &&
                strncmp(fi->label, "About ", 6) == 0) {
                continue;           /* hoisted into the system menu */
            }
            if (fi->separator) {
                /* Not as the first row: the row above it may have been
                 * hoisted away. */
                if (to->count > 0) {
                    (void)tiku_menu_add_separator(to);
                }
            } else if (i + 1 < from->nitem &&
                       from->item[i + 1].level == 1 && nsub < 4) {
                /*
                 * At most four submenus per published menu, the size of
                 * gsub[m].  A fifth holder becomes a plain row, and its
                 * level-1 entries are skipped at the top of this loop.
                 */
                tiku_menu_t *sub = &app->gsub[m][nsub++];
                int k = i + 1;

                tiku_menu_clear(sub);
                snprintf(sub->title, sizeof sub->title, "%s", fi->label);
                for (; k < from->nitem && from->item[k].level == 1; k++) {
                    const tiku_menu_entry_t *si = &from->item[k];

                    if (si->separator) {
                        (void)tiku_menu_add_separator(sub);
                    } else {
                        (void)tiku_menu_add(sub, si->label,
                            si->command, si->sc, si->mods, si->enabled);
                        if (si->marked) {
                            tiku_menu_mark(sub, si->command, 1);
                        }
                    }
                }
                (void)tiku_menu_add_submenu(to, fi->label, sub,
                                                fi->enabled);
                if (fi->marked && to->count > 0) {
                    to->item[to->count - 1].marked = 1;
                }
            } else {
                (void)tiku_menu_add(to, fi->label, fi->command,
                                        fi->sc, fi->mods, fi->enabled);
                if (fi->marked) {
                    tiku_menu_mark(to, fi->command, 1);
                }
            }
        }
        (void)tiku_menubar_add(&app->gbar, to);
    }
#if TIKU_SHELL_BOARD_SERVICES

    /* The Devices menu is always on the bar, last, so the focused
     * window's menus keep their places. */
    build_devices_menu(app);
    (void)tiku_menubar_add(&app->gbar, &app->devmenu);
#endif

    app->gbar.open = was_open;
}

/**
 * @brief Carry out the commands of the system and Devices menus.  They act
 *        with no folder window focused, where menu_invoke() does nothing.
 * @return nonzero when @p command was one of these.
 */
static int
system_invoke(desktop_t *app, int command)
{
#if TIKU_SHELL_BOARD_SERVICES

    if (desktop_devtray_invoke(app, command)) {
        return 1;
    }
    if (command >= TIKU_TRK_CMD_DEVICE_PICK_BASE &&
        command < TIKU_TRK_CMD_DEVICE_PICK_BASE + TIKU_MOUNT_MAX) {
        char leaf[64];

        if (device_leaf_at(app, command - TIKU_TRK_CMD_DEVICE_PICK_BASE,
                           leaf, sizeof leaf)) {
            device_set_current(app, leaf);
        }
        return 1;
    }
    if (command >= TIKU_TRK_CMD_DEVICE_DROP_BASE &&
        command < TIKU_TRK_CMD_DEVICE_DROP_BASE + TIKU_MOUNT_MAX) {
        char leaf[64];

        if (device_leaf_at(app, command - TIKU_TRK_CMD_DEVICE_DROP_BASE,
                           leaf, sizeof leaf)) {
            (void)device_disconnect(app, leaf);
        }
        return 1;
    }
    if (command >= TIKU_TRK_CMD_DEVICE_CONNECT_BASE &&
        command < TIKU_TRK_CMD_DEVICE_CONNECT_BASE + TIKU_MOUNT_MAX) {
        int i = command - TIKU_TRK_CMD_DEVICE_CONNECT_BASE;

        if (i >= 0 && i < app->ncandidates) {
            (void)device_connect(app, app->candidates[i].port);
        }
        return 1;
    }
#endif

    switch (command) {
#if TIKU_SHELL_BOARD_SERVICES

    case TIKU_TRK_CMD_DEVICE_HOST:
        device_set_current(app, "");
        return 1;
    case TIKU_TRK_CMD_DEVICE_SCAN:
        (void)device_scan(app);
        desktop_board_look_begin(app, -1, NULL);   /* probe them for names */
        desktop_damage_all(app);
        return 1;
#endif

    case TIKU_TRK_CMD_ABOUT:
        (void)embed_open_or_raise(app, &tiku_trk_about_app);
        return 1;

    case TIKU_TRK_CMD_FIND:
        open_panel(app);
        return 1;
        case TIKU_TRK_CMD_PREFERENCES:
        tiku_trk_prefs_window_open(&app->prefs_window);
        return 1;
    case TIKU_TRK_CMD_SCREENSHOT:
        /* Taken from the next frame drawn, after the menu has closed. */
        app->shot_pending = 1;
        return 1;
    case TIKU_TRK_CMD_APP_HELP:
        /* The focused window's help page, which rebuild_global_bar() put
         * in help_row, sent to the installed TikuReader application. */
        if (app->help_row[0] != '\0') {
            (void)desktop_help_open(app, app->help_row);
        }
        return 1;
    case TIKU_TRK_CMD_APP_ABOUT:
        /* The hoisted About row goes back to the window that published
         * it. */
        if (app->about_hoist != 0 && app->gbar_from != NULL &&
            app->gbar_from->menu_pick != NULL) {
            app->gbar_from->menu_pick(app->gbar_from, app->about_hoist,
                                      app->gbar_from->menu_pick_context);
        }
        return 1;
    case TIKU_TRK_CMD_SHUTDOWN:
        /* Shut down asks first (DB-006), with the question the
         * Deskbar's leaf menu asks. */
        tiku_sound_play(TIKU_SOUND_BEEP);
        tiku_alert_open(alert_model(app), TIKU_ALERT_WARN,
                            TIKU_TRK_ALERT_TAG_SHUTDOWN,
                            tiku_str("alert.msg.shut_down"), tiku_str("alert.btn.shut_down"));
        return 1;
    default:
        break;
    }
    return 0;
}

/**
 * @brief Route a menu pick: a pick from a published menu on the strip goes
 *        back to the window that published it; the system and Devices
 *        menus' commands, and every other pick, run here.
 */
void
global_or_invoke(desktop_t *app, folder_t *ffl, int command)
{
#if TIKU_SHELL_BOARD_SERVICES

    if (app->gbar_open >= 0 && app->gbar_from != NULL &&
        app->popup != &app->sysmenu && app->popup != &app->devmenu &&
        app->popup != &app->devtraymenu &&
        app->gbar_from->menu_pick != NULL) {
#else

    if (app->gbar_open >= 0 && app->gbar_from != NULL &&
        app->popup != &app->sysmenu &&
        app->gbar_from->menu_pick != NULL) {
#endif

        app->gbar_from->menu_pick(app->gbar_from, command,
                                  app->gbar_from->menu_pick_context);
        return;
    }
    if (system_invoke(app, command)) {
        return;
    }
    menu_invoke(app, ffl, command);
}

/** @brief The character a key event types, the first byte of its text, or
 *         0. */
char
key_char(const tiku_event_t *event)
{
    return (event != NULL) ? event->text[0] : (char)0;
}

/** @brief Name a portable key for controllers whose API uses key labels. */
const char *
key_name(const tiku_event_t *event, char printable[2])
{
    if (event == NULL) { return ""; }
    switch (event->key) {
    case TIKU_KEY_UP:     return "Up";
    case TIKU_KEY_DOWN:   return "Down";
    case TIKU_KEY_LEFT:   return "Left";
    case TIKU_KEY_RIGHT:  return "Right";
    case TIKU_KEY_ESCAPE: return "Escape";
    case TIKU_KEY_RETURN: return "Return";
    case TIKU_KEY_TAB:    return "Tab";
    default:
        if (event->key == ' ') { return "space"; }
        if (event->key >= 0x20u && event->key < 0x7fu) {
            printable[0] = (char)event->key;
            printable[1] = '\0';
            return printable;
        }
        return "";
    }
}
