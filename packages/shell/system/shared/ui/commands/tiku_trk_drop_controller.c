/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_drop_controller.c - carrying out a drop: the Deskbar, a
 * rearrangement in the source view, an application launch, a colour, a
 * volume dropped on the Trash, and copy, move or link into a folder.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_model.h"
#include "tiku_state.h"
#include "tiku_trk_drop_controller.h"
#include "tiku_trk_drop_protocol.h"
#include "tiku_trk_launch.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

void
tiku_trk_drop_controller_init(tiku_trk_drop_controller_t *controller,
    tiku_trk_deskbar_t *deskbar, tiku_backend_t *backend,
    tiku_fs_t *fs, tiku_trk_desktop_view_t *desktop_view,
    tiku_trk_cwmenu_t *menus, tiku_alert_t *alert,
    tiku_trk_drag_t *drag, tiku_trk_folder_window_t **drop_folder,
    int *drop_row, tiku_menu_t **popup, tiku_menu_t **popup_sub,
    tiku_rect_t *popup_frame, char *progress_destination,
    size_t progress_destination_size, const tiku_trk_services_t *services,
    int screen_w, int screen_h,
    tiku_trk_drop_color_fn set_color, void *color_context)
{
    *controller = (tiku_trk_drop_controller_t){
        .deskbar = deskbar,
        .backend = backend,
        .fs = fs,
        .desktop_view = desktop_view,
        .menus = menus,
        .alert = alert,
        .drag = drag,
        .drop_folder = drop_folder,
        .drop_row = drop_row,
        .popup = popup,
        .popup_sub = popup_sub,
        .popup_frame = popup_frame,
        .progress_destination = progress_destination,
        .progress_destination_size = progress_destination_size,
        .services = services,
        .set_color = set_color,
        .color_context = color_context,
        .screen_w = screen_w,
        .screen_h = screen_h
    };
}

/** @brief A line for the status strip, through the shell. */
static void
say(tiku_trk_drop_controller_t *controller, const char *fmt, ...)
{
    char text[256];
    va_list ap;

    va_start(ap, fmt);
    (void)vsnprintf(text, sizeof text, fmt, ap);
    va_end(ap);
    if (controller->services != NULL && controller->services->status != NULL) {
        controller->services->status(controller->services->ctx, text);
    }
}

/** @brief Ask the shell to redraw the whole screen. */
static void
changed(tiku_trk_drop_controller_t *controller)
{
    if (controller->services != NULL && controller->services->damage != NULL) {
        controller->services->damage(controller->services->ctx,
                                     (tiku_rect_t){ 0, 0, -1, -1 });
    }
}

/** @brief Item @p from of @p drag copied down to place @p to. */
static void
drag_move_item(tiku_trk_drag_t *drag, int from, int to)
{
    if (from != to) {
        memcpy(drag->path[to], drag->path[from], sizeof drag->path[to]);
        memcpy(drag->name[to], drag->name[from], sizeof drag->name[to]);
        drag->ox[to] = drag->ox[from];
        drag->oy[to] = drag->oy[from];
    }
}

/** @brief The drag's paths into @p paths.  @return how many there are. */
static int
drop_paths(tiku_trk_drop_controller_t *controller, const char **paths)
{
    int i;

    for (i = 0; i < controller->drag->count; i++) {
        paths[i] = controller->drag->path[i];
    }
    return controller->drag->count;
}

static int drop_on_application(tiku_trk_drop_controller_t *controller,
                               const char *destination, unsigned modifiers);
static const tiku_trk_app_t *app_behind(
    const tiku_trk_drop_controller_t *controller, const char *path);

/**
 * @brief The installed application behind Deskbar entry @p team: the one
 *        its process was started from, else the one of its name.  NULL
 *        when there is none.
 */
static const tiku_trk_app_t *
team_application(const tiku_trk_drop_controller_t *controller, int team)
{
    const tiku_trk_services_t *services = controller->services;
    const char *command =
        tiku_trk_deskbar_team_command(controller->deskbar, team);
    const char *name = tiku_trk_deskbar_team_app(controller->deskbar, team);
    const tiku_trk_app_t *application = NULL;

    if (command != NULL && command[0] != '\0') {
        application = app_behind(controller, command);
    }
    if (application == NULL && name != NULL && services != NULL &&
        services->app_named != NULL) {
        application = services->app_named(services->ctx, name);
    }
    return application;
}

/**
 * @brief Launch each dragged item that is an installed application, with
 *        no documents; a folder is passed over, and the status line counts
 *        the other items, which are not launched.
 */
static void
launch_on_bar(tiku_trk_drop_controller_t *controller)
{
    int launched = 0;
    int refused = 0;
    int failed = 0;
    int i;

    for (i = 0; i < controller->drag->count; i++) {
        const tiku_trk_app_t *application;
        tiku_model_t model;

        if (controller->backend->ops->stat(controller->backend,
                controller->drag->path[i], &model) != 0 ||
            !tiku_trk_deskbar_launchable(&model)) {
            continue;
        }
        application = app_behind(controller, controller->drag->path[i]);
        if (application == NULL || controller->launch == NULL) {
            refused++;
        } else if (controller->launch(controller->launch_context,
                                      application, NULL, 0)) {
            launched++;
        } else {
            failed++;
        }
    }
    if (failed > 0) {
        return;     /* the launcher's status line says why */
    }
    if (launched == 0 && refused > 0) {
        say(controller, "nothing launched: %d item%s %s not an application",
            refused, refused == 1 ? "" : "s", refused == 1 ? "is" : "are");
    } else if (refused > 0) {
        say(controller, "launching %d application%s; %d item%s %s not one",
            launched, launched == 1 ? "" : "s", refused,
            refused == 1 ? "" : "s", refused == 1 ? "is" : "are");
    } else if (launched > 0) {
        say(controller, "launching %d application%s", launched,
            launched == 1 ? "" : "s");
    }
}

/**
 * @brief A drop that lands on the Deskbar: a menu folder links the items
 *        into it, a menu file launches with them, the logo links them into
 *        the Deskbar folder, the tray subscribes to one node, a team row
 *        launches its installed application with the items, and the bar
 *        itself launches the items that are applications.  @return 1 when
 *        the bar took the drop, 0 when it landed elsewhere.
 */
static int
drop_on_deskbar(tiku_trk_drop_controller_t *controller, int x, int y,
                unsigned modifiers)
{
    const char *paths[TIKU_TRK_DRAG_MAX];
    char destination[TIKU_PATH_MAX];
    tiku_trk_db_drop_t drop;
    tiku_trk_db_drop_t menu_drop = TIKU_TRK_DB_DROP_NONE;
    int team = -1;
    int count;
    int i;

    drop = tiku_trk_deskbar_drop_at(controller->deskbar, x, y, &team);
    if (drop == TIKU_TRK_DB_DROP_NONE) {
        menu_drop = tiku_trk_deskbar_menu_drop_at(controller->deskbar, x, y,
            destination, sizeof destination);
    }
    if (menu_drop == TIKU_TRK_DB_DROP_MENU_DIR) {
        count = drop_paths(controller, paths);
        if (count > 0) {
            (void)tiku_fs_link(controller->fs, paths, count,
                                   destination, 0);
            say(controller,
                     "linking %d item%s into %.40s", count,
                     count == 1 ? "" : "s", destination);
        }
        return 1;
    }
    if (menu_drop == TIKU_TRK_DB_DROP_MENU_FILE) {
        /* The menu takes the drop whether or not the file launches. */
        (void)drop_on_application(controller, destination, modifiers);
        return 1;
    }
    if (drop == TIKU_TRK_DB_DROP_TRAY) {
        /* The tray takes exactly one node, as a subscription with the id
         * tiku_trk_deskbar_add_replicant() returns (DB-038). */
        count = drop_paths(controller, paths);
        if (count == 1) {
            const char *base = strrchr(paths[0], '/');
            int id = tiku_trk_deskbar_add_replicant(controller->deskbar,
                base != NULL ? base + 1 : paths[0], paths[0]);

            say(controller,
                     id > 0 ? "subscribed to the tray (id %d)"
                            : "the tray refused it (name taken or full)",
                     id);
            changed(controller);
        }
        return 1;
    }
    if (drop == TIKU_TRK_DB_DROP_NONE) {
        return 0;
    }
    count = drop_paths(controller, paths);
    if (count == 0) {
        return 1;
    }
    if (drop == TIKU_TRK_DB_DROP_LEAF) {
        const char *home = getenv("HOME");

        if (home != NULL && snprintf(destination, sizeof destination,
                "%s/.config/deskbar", home) > 0) {
            /* Its parents too, since a fresh home may have no
             * .config. */
            (void)tiku_state_mkparents(destination);
            (void)mkdir(destination, 0755);
            (void)tiku_fs_link(controller->fs, paths, count,
                                   destination, 0);
            say(controller,
                     "linking %d item%s into the Deskbar", count,
                     count == 1 ? "" : "s");
        }
        return 1;
    }
    if (drop == TIKU_TRK_DB_DROP_TEAM && team >= 0) {
        const char *types[TIKU_TRK_DRAG_TYPES];
        const char *name = tiku_trk_deskbar_team_app(controller->deskbar,
                                                     team);
        const tiku_trk_app_t *application;
        int type_count = tiku_trk_drag_types(controller->drag);

        for (i = 0; i < type_count; i++) {
            types[i] = controller->drag->type[i];
        }
        if (!tiku_trk_deskbar_team_accepts(controller->deskbar, team,
                types, type_count,
                (modifiers & TIKU_MOD_CMD) != 0u)) {
            say(controller,
                     "%.40s cannot open %s", name,
                     count == 1 ? "that" : "those");
            return 1;
        }
        application = team_application(controller, team);
        if (application == NULL || controller->launch == NULL) {
            say(controller, "%.40s is not an installed application", name);
            return 1;
        }
        /* The launch starts a new instance; the launcher's status line
         * reports it. */
        (void)controller->launch(controller->launch_context, application,
                                 paths, count);
        return 1;
    }
    launch_on_bar(controller);
    return 1;
}

/**
 * @brief A drop back into the icon view the drag began in: the selected
 *        icons move by the drag's offset, snapped to the grid with Cmd,
 *        and the layout is saved.  @return 1 when the drop was a
 *        rearrangement, 0 otherwise.
 */
static int
arrange_in_source(tiku_trk_drop_controller_t *controller, int x, int y,
                  unsigned modifiers)
{
    tiku_trk_folder_window_t *folder = *controller->drop_folder;
    int dx;
    int dy;
    int i;
    int count;
    int snap;

    if (*controller->drop_row >= 0 || folder->view == NULL ||
        tiku_trk_poseview_mode(folder->view) == TIKU_TRK_MODE_LIST ||
        strcmp(controller->drag->src_dir, folder->path) != 0) {
        return 0;
    }
    dx = x - controller->drag->x0;
    dy = y - controller->drag->y0;
    snap = (modifiers & TIKU_MOD_CMD) != 0u;
    count = tiku_trk_poseview_count(folder->view);
    for (i = 0; i < count; i++) {
        tiku_trk_pose_t *pose = tiku_trk_poseview_at(folder->view, i);
        int px;
        int py;

        if (pose == NULL || !pose->selected) {
            continue;
        }
        tiku_trk_poseview_pose_location(folder->view, pose, &px, &py);
        tiku_trk_poseview_move_pose(folder->view, pose, px + dx, py + dy,
                                    snap);
    }
    if (folder == tiku_trk_desktop_view_folder(controller->desktop_view)) {
        (void)tiku_trk_desktop_view_ensure_visible(controller->desktop_view);
    }
    (void)tiku_trk_poseview_save_state(folder->view);
    changed(controller);
    return 1;
}

/**
 * @brief Where the drop goes: the row under the pointer, else the folder
 *        shown.  @return 0 when that row has no pose.
 */
static int
drop_destination(tiku_trk_drop_controller_t *controller, char *destination,
                 size_t destination_size)
{
    tiku_trk_folder_window_t *folder = *controller->drop_folder;

    if (*controller->drop_row >= 0) {
        tiku_trk_pose_t *pose = tiku_trk_poseview_at(folder->view,
                                                     *controller->drop_row);

        if (pose == NULL) {
            return 0;
        }
        snprintf(destination, destination_size, "%s", pose->model.path);
    } else {
        snprintf(destination, destination_size, "%s", folder->path);
    }
    return 1;
}

/**
 * @brief Whether @p destination takes the drag: items from another
 *        application must pass their offer, then every drag needs a
 *        writable folder, no copy or link into the Trash, and a first item
 *        that may move there.  A refusal opens an alert.
 * @return 1 when the drop may go on.
 */
static int
drop_allowed(tiku_trk_drop_controller_t *controller,
             const char *destination, unsigned drag_modifiers)
{
    tiku_model_t model;
    const char *reason = NULL;
    char refused[200];

    if (controller->drag->foreign_refs) {
        tiku_trk_foreign_offer_t offer;
        tiku_trk_drop_result_t result;
        const char *types = controller->drag->ntype > 0
                                ? controller->drag->type[0] : "text/uri-list";
        (void)tiku_trk_foreign_offer_parse(&offer, "copy move link", types, 0);
        result = tiku_trk_foreign_drop(&offer, destination[0] != '\0', 0,
                                       strcmp(destination, "/") == 0, 0);
        if (!result.accepted) {
            tiku_alert_open(controller->alert, TIKU_ALERT_WARN,
                                TIKU_TRK_ALERT_TAG_REFUSAL,
                                "This drop target cannot accept those items.",
                                "Cancel");
            return 0;
        }
    }

    if (controller->backend->ops->stat(controller->backend, destination,
                                       &model) != 0 ||
        !tiku_model_is_mutable_container(&model)) {
        reason = "You can't move or copy items to read-only volumes.";
    } else if (tiku_fs_in_trash(controller->fs, destination)) {
        if ((drag_modifiers & TIKU_TRK_DRAG_MOD_LINK) != 0u) {
            reason = "Sorry, you can't create links in the Trash.";
        } else if ((drag_modifiers & TIKU_TRK_DRAG_MOD_COPY) != 0u) {
            reason = "Sorry, you can't copy items to the Trash.";
        }
    }
    if (reason == NULL && tiku_fs_check_move(controller->fs,
            controller->drag->path[0], destination, refused,
            sizeof refused) != TIKU_REFUSE_NONE) {
        reason = refused;
    }
    if (reason != NULL) {
        tiku_alert_open(controller->alert, TIKU_ALERT_WARN,
                            TIKU_TRK_ALERT_TAG_REFUSAL, reason, "Cancel");
        return 0;
    }
    return 1;
}

static const char *link_name_of(const tiku_trk_drop_controller_t *controller,
                                int i);

/**
 * @brief In an icon view, when the drop is not on a pose, reserve for each
 *        dropped item the spot it was carried to, so it appears where it
 *        was let go.
 */
static void
reserve_icon_landings(tiku_trk_drop_controller_t *controller,
                      const char **paths, int count, int x, int y,
                      int linking)
{
    tiku_trk_folder_window_t *folder = *controller->drop_folder;
    tiku_rect_t content;
    int origin_x = 0;
    int origin_y = 0;
    int i;

    if (*controller->drop_row >= 0 ||
        tiku_trk_poseview_mode(folder->view) == TIKU_TRK_MODE_LIST) {
        return;
    }
    content = tiku_trk_folder_window_content(folder);
    tiku_trk_poseview_origin(folder->view, &origin_x, &origin_y);
    for (i = 0; i < count; i++) {
        const char *name = strrchr(paths[i], '/');
        int landing_x;
        int landing_y;

        tiku_trk_drag_landing(controller->drag, i,
            x - (content.x + 2) + origin_x,
            y - (content.y + 2) + origin_y, &landing_x, &landing_y);
        tiku_trk_poseview_drop_land(folder->view,
            linking ? link_name_of(controller, i)
                    : (name != NULL ? name + 1 : paths[i]),
            landing_x, landing_y);
    }
}

void
tiku_trk_drop_controller_set_launcher(
    tiku_trk_drop_controller_t *controller, tiku_trk_drop_launch_fn launch,
    void *launch_context)
{
    controller->launch = launch;
    controller->launch_context = launch_context;
}

/**
 * @brief The registered application @p path is, or links to, from the
 *        shell's registry; NULL when there is none.
 */
static const tiku_trk_app_t *
app_behind(const tiku_trk_drop_controller_t *controller, const char *path)
{
    if (controller->services == NULL || controller->services->app_of == NULL) {
        return NULL;
    }
    return controller->services->app_of(controller->services->ctx, path);
}

/**
 * @brief What a link made of item @p i is called: the name the drag gave
 *        it, else a program's registered name, else the target's leaf.
 */
static const char *
link_name_of(const tiku_trk_drop_controller_t *controller, int i)
{
    const char *path = controller->drag->path[i];
    const tiku_trk_app_t *program;
    const char *leaf;

    if (controller->drag->name[i][0] != '\0') {
        return controller->drag->name[i];
    }
    program = app_behind(controller, path);
    if (program != NULL && program->name[0] != '\0') {
        return program->name;
    }
    leaf = strrchr(path, '/');
    return (leaf != NULL) ? leaf + 1 : path;
}

/** @brief Whether every item the drag carries is a registered application. */
static int
carries_applications(const tiku_trk_drop_controller_t *controller)
{
    int i;

    if (controller->drag->count == 0) {
        return 0;
    }
    for (i = 0; i < controller->drag->count; i++) {
        if (app_behind(controller, controller->drag->path[i]) == NULL) {
            return 0;
        }
    }
    return 1;
}

/**
 * @brief A drop on a file: an application launches with the dropped
 *        documents (PVS-044), and any other file refuses the drop.
 *
 * Only the documents the application claims to handle are passed; Cmd
 * held at the release passes every one.  With none left, nothing is
 * launched.
 *
 * @return 1 when @p destination is a file, 0 when it is a container, cannot
 *         be read, or no launcher is installed.
 */
static int
drop_on_application(tiku_trk_drop_controller_t *controller,
                    const char *destination, unsigned modifiers)
{
    const char *paths[TIKU_TRK_DRAG_MAX];
    const char *kept[TIKU_TRK_DRAG_MAX];
    const tiku_trk_app_t *application;
    int force = (modifiers & TIKU_MOD_CMD) != 0u;
    int count;
    int n = 0;
    int i;

    tiku_model_t target;

    if (controller->launch == NULL) {
        return 0;
    }
    if (controller->backend->ops->stat(controller->backend, destination,
                                       &target) != 0 ||
        tiku_model_is_container(&target)) {
        return 0;
    }
    application = app_behind(controller, destination);
    if (application == NULL) {
        /* Not a registered application: the drop is refused with a
         * status line. */
        say(controller,
                 "%s is not a registered application", target.name);
        changed(controller);
        return 1;
    }
    count = drop_paths(controller, paths);
    for (i = 0; i < count; i++) {
        tiku_model_t model;

        if (force) {
            kept[n++] = paths[i];
            continue;
        }
        if (controller->backend->ops->stat(controller->backend, paths[i],
                                           &model) == 0 &&
            tiku_trk_launch_relation(application, model.type) !=
                TIKU_TRK_REL_NONE) {
            kept[n++] = paths[i];
        }
    }
    if (n == 0) {
        say(controller,
                 "%s claims none of those types", application->name);
        changed(controller);
        return 1;
    }
    (void)controller->launch(controller->launch_context, application,
                             kept, n);
    return 1;
}

void
tiku_trk_drop_controller_execute(tiku_trk_drop_controller_t *controller,
                                 int x, int y, unsigned modifiers,
                                 unsigned button)
{
    char destination[TIKU_PATH_MAX];

    if (controller == NULL || drop_on_deskbar(controller, x, y, modifiers) ||
        *controller->drop_folder == NULL ||
        arrange_in_source(controller, x, y, modifiers) ||
        !drop_destination(controller, destination, sizeof destination)) {
        return;
    }
    tiku_trk_drop_controller_execute_into(controller, destination, x, y,
                                          modifiers, button, 1);
}

void
tiku_trk_drop_controller_execute_into(tiku_trk_drop_controller_t *controller,
                                      const char *destination_in, int x,
                                      int y, unsigned modifiers,
                                      unsigned button, int in_view)
{
    const char *paths[TIKU_TRK_DRAG_MAX];
    char destination[TIKU_PATH_MAX];
    unsigned drag_modifiers = 0;
    tiku_trk_drop_t action;
    int same_store;
    int count;

    if (controller == NULL || destination_in == NULL ||
        destination_in[0] == '\0') {
        return;
    }
    snprintf(destination, sizeof destination, "%s", destination_in);
    if (drop_on_application(controller, destination, modifiers)) {
        return;
    }
    if (in_view && controller->drag->payload_size > 0u &&
        (strcmp(controller->drag->payload_type, "application/x-rgb-color") ==
             0 ||
         strcmp(controller->drag->payload_type, "text/x-color") == 0)) {
        tiku_trk_color_t color;

        if (tiku_trk_color_parse(controller->drag->payload,
                (unsigned)controller->drag->payload_size, &color) == 0 &&
            controller->set_color != NULL &&
            *controller->drop_folder ==
                tiku_trk_desktop_view_folder(controller->desktop_view)) {
            controller->set_color(controller->color_context, &color);
            say(controller,
                     "desktop color set to #%02x%02x%02x",
                     color.r, color.g, color.b);
            changed(controller);
        }
        return;
    }
    if ((modifiers & TIKU_MOD_OPTION) != 0u) {
        drag_modifiers |= TIKU_TRK_DRAG_MOD_COPY;
    }
    if ((modifiers & TIKU_MOD_CMD) != 0u) {
        drag_modifiers |= TIKU_TRK_DRAG_MOD_LINK;
    }
    /* Dragging a program makes a link to it, unless Option asks for a
     * copy. */
    if (carries_applications(controller) &&
        (drag_modifiers & TIKU_TRK_DRAG_MOD_COPY) == 0u) {
        drag_modifiers |= TIKU_TRK_DRAG_MOD_LINK;
    }
    if (tiku_fs_in_trash(controller->fs, destination)) {
        /* FS-041: a volume dragged to the Trash is unmounted, with an
         * alert for a refusal such as the boot volume's.  Volumes are
         * taken out of the drop here; anything dragged with them still
         * goes to the Trash below. */
        tiku_volumes_t vols;
        int scanned = 0, vols_dropped = 0, others = 0;
        int di;

        for (di = 0; di < controller->drag->count; di++) {
            const char *path = controller->drag->path[di];
            char why[200];

            if (!scanned) {
                scanned = (tiku_volumes_scan(&vols) > 0) ? 1 : -1;
            }
            if (scanned < 0 || !tiku_volume_is_root(&vols, path)) {
                drag_move_item(controller->drag, di, others++);
                continue;
            }
            vols_dropped++;
            why[0] = '\0';
            if (!tiku_volume_may_unmount(&vols, path, why,
                                             sizeof why)) {
                if (why[0] != '\0') {
                    tiku_alert_open(controller->alert,
                        TIKU_ALERT_STOP, TIKU_TRK_ALERT_TAG_REFUSAL,
                        why, "Cancel");
                }
            } else if (tiku_volume_unmount(&vols, path, NULL, NULL,
                           why, sizeof why) == TIKU_UNMOUNT_OK) {
                const tiku_volume_t *vol =
                    tiku_volume_of(&vols, path);

                say(controller,
                         "unmounted \"%.60s\"",
                         (vol != NULL) ? vol->name : path);
            } else if (why[0] != '\0') {
                tiku_alert_open(controller->alert,
                    TIKU_ALERT_STOP, TIKU_TRK_ALERT_TAG_REFUSAL,
                    why, "Cancel");
            }
        }
        controller->drag->count = others;
        if (vols_dropped > 0 && others == 0) {
            changed(controller);
            return;
        }
    }
    if (!drop_allowed(controller, destination, drag_modifiers)) {
        return;
    }
    same_store = tiku_fs_drop_action(controller->fs,
        controller->drag->path[0], destination, 0) == TIKU_OP_MOVE;
    action = tiku_trk_drag_action(controller->drag, destination,
                                  drag_modifiers, same_store);
    count = drop_paths(controller, paths);
    if (action == TIKU_TRK_DROP_NOTHING || count == 0) {
        return;
    }
    if (in_view && button == 3u) {
        tiku_model_t model;
        int destination_read_only = 0;
        int source_read_only = 0;

        if (controller->backend->ops->stat(controller->backend,
                destination, &model) == 0) {
            destination_read_only =
                !tiku_model_is_mutable_container(&model);
        }
        if (controller->backend->ops->stat(controller->backend,
                controller->drag->src_dir, &model) == 0) {
            source_read_only = !tiku_model_is_mutable_container(&model);
        }
        snprintf(controller->destination, sizeof controller->destination,
                 "%s", destination);
        controller->pending = *controller->drag;
        *controller->popup = tiku_trk_cwmenu_drop(controller->menus,
            destination_read_only, source_read_only,
            (modifiers & TIKU_MOD_SHIFT) ? TIKU_TRK_MOD_SHIFT : 0u);
        *controller->popup_sub = NULL;
        *controller->popup_frame = tiku_menu_place(*controller->popup,
            x, y, (tiku_rect_t){ 0, 0, controller->screen_w,
                                      controller->screen_h });
        return;
    }
    if (in_view) {
        reserve_icon_landings(controller, paths, count, x, y,
                              action == TIKU_TRK_DROP_LINK);
    }
    snprintf(controller->progress_destination,
             controller->progress_destination_size, "%s", destination);
    switch (action) {
    case TIKU_TRK_DROP_COPY:
        (void)tiku_fs_copy(controller->fs, paths, count, destination,
                               TIKU_CONFLICT_ASK);
        say(controller,
                 "copying %d item%s", count, count == 1 ? "" : "s");
        break;
    case TIKU_TRK_DROP_LINK: {
        int relative = (modifiers & TIKU_MOD_SHIFT) != 0u;

        if (count == 1) {
            (void)tiku_fs_link_as(controller->fs, paths[0], destination,
                                  link_name_of(controller, 0), relative);
        } else {
            (void)tiku_fs_link(controller->fs, paths, count, destination,
                               relative);
        }
        say(controller,
                 "%s %d item%s", relative ? "linking (relative)" : "linking",
                 count, count == 1 ? "" : "s");
        break;
    }
    default:
        (void)tiku_fs_move(controller->fs, paths, count, destination,
                               TIKU_CONFLICT_ASK);
        say(controller,
                 "moving %d item%s", count, count == 1 ? "" : "s");
        break;
    }
}
