/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_folder_menu.c - the folder menus' controller: the enablement
 * context, and the Open with and per-type submenus.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_trk_folder_menu.h"
#include "tiku_trk_shortcuts.h"
#include "tiku_volume.h"
#include <stdio.h>
#include <string.h>

void
tiku_trk_folder_menu_init(tiku_trk_folder_menu_t *controller,
    tiku_trk_cwmenu_t *menus, tiku_trk_clip_t *clip,
    tiku_backend_t *backend, tiku_fs_t *fs,
    tiku_trk_types_t *types, tiku_trk_launch_controller_t *launcher,
    tiku_trk_folder_window_t **bar_folder, int *bar_open)
{
    if (controller == NULL) {
        return;
    }
    *controller = (tiku_trk_folder_menu_t){
        .menus = menus,
        .clip = clip,
        .backend = backend,
        .fs = fs,
        .types = types,
        .launcher = launcher,
        .bar_folder = bar_folder,
        .bar_open = bar_open
    };
}

void
tiku_trk_folder_menu_set_editor(tiku_trk_folder_menu_t *controller,
                                tiku_trk_folder_edit_t *editor)
{
    if (controller != NULL) {
        controller->editor = editor;
    }
}

tiku_trk_sc_ctx_t
tiku_trk_folder_menu_context(tiku_trk_folder_menu_t *controller,
    tiku_trk_folder_window_t *folder, unsigned modifiers)
{
    tiku_trk_sc_ctx_t context;

    memset(&context, 0, sizeof context);
    context.mods = modifiers;
    context.view_mode = TIKU_TRK_MODE_LIST;
    if (controller == NULL || folder == NULL) {
        return context;
    }
    context.selection = tiku_trk_poseview_selection_count(folder->view);
    context.text_focus = controller->editor != NULL &&
                         (controller->editor->editing ||
                          controller->editor->renaming);
    context.is_desktop = folder->win == NULL;
    context.view_mode = tiku_trk_poseview_mode(folder->view);
    context.icon_size = tiku_trk_poseview_icon_size(folder->view);
    context.sort_field = tiku_trk_poseview_sort_field(folder->view);
    context.reverse_sort = tiku_trk_poseview_sort_reverse(folder->view);
    {
        static const int fields[] = {
            TIKU_TRK_COL_NAME, TIKU_TRK_COL_KIND, TIKU_TRK_COL_VALUE,
            TIKU_TRK_COL_SIZE, TIKU_TRK_COL_CAP, TIKU_TRK_COL_DATE,
            TIKU_TRK_COL_PATH, TIKU_TRK_COL_PERM, TIKU_TRK_COL_ORIGINAL
        };
        size_t i;

        for (i = 0; i < sizeof fields / sizeof fields[0]; i++) {
            if (tiku_trk_poseview_has_column(folder->view, fields[i])) {
                context.columns |= 1u << fields[i];
            }
        }
    }
    context.can_edit_name = context.selection == 1;
    context.clipboard_has_refs = tiku_trk_clip_count(controller->clip) > 0;
    context.clipboard_has_layout = tiku_trk_poseview_has_layout_clipboard();
    context.can_open_parent = !tiku_trk_poseview_is_query(folder->view) &&
                              strcmp(folder->path, "/") != 0;
    {
        char trash[TIKU_PATH_MAX];

        if (controller->fs != NULL &&
            tiku_fs_trash_dir(controller->fs, folder->path, trash,
                                  sizeof trash) == 0) {
            tiku_model_t model;

            context.is_trash = strcmp(folder->path, trash) == 0;
            context.in_trash = !context.is_trash &&
                tiku_fs_in_trash(controller->fs, folder->path);
            if (controller->backend->ops->stat(controller->backend, trash,
                                               &model) == 0) {
                context.trash_full = model.facts.trash_full;
            }
        }
    }
    {
        tiku_trk_pose_t *pose = tiku_trk_poseview_active(folder->view);

        if (pose != NULL) {
            context.is_virtual =
                pose->model.kind == TIKU_KIND_VIRTUAL_DIR;
            context.selection_ro = !tiku_model_is_writable(&pose->model);
            context.can_trash_or_dup = context.selection > 0 &&
                                       !context.selection_ro;
            context.selection_is_volume = pose->model.kind ==
                TIKU_KIND_VOLUME || pose->model.kind == TIKU_KIND_ROOT;
            if (context.selection_is_volume) {
                /* Scanned only when a volume is the selection: menus are
                 * rebuilt per draw, and the mount table is a file. */
                tiku_volumes_t vols;
                const tiku_volume_t *vol;

                if (tiku_volumes_scan(&vols) > 0 &&
                    (vol = tiku_volume_of(&vols,
                                              pose->model.path)) != NULL) {
                    context.selection_is_boot = vol->is_boot;
                }
            }
        }
        context.target_ro = strncmp(folder->path, "/sys", 4) == 0 ||
                            strncmp(folder->path, "/dev", 4) == 0 ||
                            strncmp(folder->path, "/proc", 5) == 0;
        context.is_root = strcmp(folder->path, "/") == 0;
        context.is_query = tiku_trk_poseview_is_query(folder->view);
    }
    return context;
}

void
tiku_trk_folder_menu_build_open_with(tiku_trk_folder_menu_t *controller,
    tiku_trk_folder_window_t *folder)
{
    if (controller != NULL && controller->launcher != NULL) {
        tiku_trk_launch_controller_build_menu(controller->launcher,
                                              &controller->menus->open_with,
                                              folder);
    }
}

/**
 * @brief The command that shows @p column, with its title.  @return the
 *        command, or 0 for a column no menu offers.
 */
static int
column_command(int column, const char **title)
{
    switch (column) {
    case TIKU_TRK_COL_KIND:
        *title = "Kind";
        return TIKU_TRK_CMD_COL_KIND;
    case TIKU_TRK_COL_VALUE:
        *title = "Value";
        return TIKU_TRK_CMD_COL_VALUE;
    case TIKU_TRK_COL_SIZE:
        *title = "Size";
        return TIKU_TRK_CMD_COL_SIZE;
    case TIKU_TRK_COL_CAP:
        *title = "Capability";
        return TIKU_TRK_CMD_COL_CAP;
    case TIKU_TRK_COL_DATE:
        *title = "Modified";
        return TIKU_TRK_CMD_COL_DATE;
    case TIKU_TRK_COL_PATH:
        *title = "Location";
        return TIKU_TRK_CMD_COL_PATH;
    case TIKU_TRK_COL_PERM:
        *title = "Permissions";
        return TIKU_TRK_CMD_COL_PERM;
    case TIKU_TRK_COL_ORIGINAL:
        *title = "Original path";
        return TIKU_TRK_CMD_COL_ORIGINAL;
    default:
        *title = "";
        return 0;
    }
}

void
tiku_trk_folder_menu_build_per_type(tiku_trk_folder_menu_t *controller,
    tiku_trk_folder_window_t *folder)
{
    const char *types[4];
    int count, i;

    if (controller == NULL) {
        return;
    }
    controller->menus->per_type_count = 0;
    if (folder == NULL || folder->view == NULL) {
        return;
    }
    count = tiku_trk_poseview_types_present(folder->view, types, 4);
    for (i = 0; i < count; i++) {
        /* The submenu is built in the slot its name and icon go to. */
        tiku_menu_t *menu =
            &controller->menus->per_type[controller->menus->per_type_count];
        const tiku_trk_typerec_t *record =
            tiku_trk_types_find(controller->types, types[i]);
        int k;

        if (record == NULL) {
            continue;
        }
        /* The submenu's item wears the type's icon; tiku_trk_types_find()
         * has already fallen back from the exact type to its supertype
         * (IV-056). */
        snprintf(controller->menus->per_type_icon[controller->menus->per_type_count],
                 sizeof controller->menus->per_type_icon[0], "%s",
                 record->icon);
        tiku_menu_clear(menu);
        for (k = 0; k < record->ncol; k++) {
            const char *title = NULL;
            int command = column_command(record->col[k], &title);

            if (command != 0) {
                (void)tiku_menu_add(menu, title, command, 0, 0u, 1);
            }
        }
        for (k = 0; k < record->nattr; k++) {
            int field = tiku_trk_poseview_attr_column(folder->view,
                record->attr[k], record->attr[k], 120);

            if (field >= 0) {
                (void)tiku_menu_add(menu, record->attr[k],
                    TIKU_TRK_CMD_COL_ATTR_BASE + field, 0, 0u, 1);
            }
        }
        if (menu->count == 0) {
            continue;
        }
        snprintf(controller->menus->per_type_name[
                     controller->menus->per_type_count],
                 sizeof controller->menus->per_type_name[0], "%s",
                 record->desc[0] != '\0' ? record->desc : types[i]);
        snprintf(menu->title, sizeof menu->title, "%s",
            controller->menus->per_type_name[
                controller->menus->per_type_count]);
        controller->menus->per_type_count++;
    }
}

void
tiku_trk_folder_menu_prepare(void *context,
                             tiku_trk_folder_window_t *folder)
{
    tiku_trk_folder_menu_t *controller = context;
    tiku_trk_sc_ctx_t menu;

    if (controller == NULL) {
        return;
    }
    menu = tiku_trk_folder_menu_context(controller, folder, 0u);
    tiku_trk_folder_menu_build_per_type(controller, folder);
    tiku_trk_cwmenu_rebuild(controller->menus, &menu);
    controller->menus->bar.open =
        *controller->bar_folder == folder ? *controller->bar_open : -1;
}
