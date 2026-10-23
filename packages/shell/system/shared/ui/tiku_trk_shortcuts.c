/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_shortcuts.c - the action table: each action's label, command,
 * shortcut key, shortcut modifiers and enable rule.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_strings.h"
#include "tiku_trk_input.h"
#include "tiku_trk_shortcuts.h"
#include "tiku_trk_view_defs.h"
#include <string.h>

/**
 * @brief The unshifted command a shifted one belongs to, which the table is
 *        keyed on; through it an item showing its shifted face is updated
 *        back when Shift is released.
 */
static int
canonical(int command)
{
    switch (command) {
    case TIKU_TRK_CMD_CLEANUP_ALL:      return TIKU_TRK_CMD_CLEANUP;
    case TIKU_TRK_CMD_CLOSE_ALL:        return TIKU_TRK_CMD_CLOSE;
    case TIKU_TRK_CMD_COPY_MORE:        return TIKU_TRK_CMD_COPY;
    case TIKU_TRK_CMD_CUT_MORE:         return TIKU_TRK_CMD_CUT;
    case TIKU_TRK_CMD_PASTE_LINKS:      return TIKU_TRK_CMD_PASTE;
    case TIKU_TRK_CMD_CREATE_REL_LINK:  return TIKU_TRK_CMD_CREATE_LINK;
    case TIKU_TRK_CMD_REL_LINK_HERE:    return TIKU_TRK_CMD_LINK_HERE;
    case TIKU_TRK_CMD_DELETE:           return TIKU_TRK_CMD_MOVE_TO_TRASH;
    default:                            return command;
    }
}

/** @brief Whether Shift is among @p mods. */
static int
shifted(unsigned mods)
{
    return (mods & TIKU_TRK_MOD_SHIFT) != 0u;
}

const char *
tiku_trk_sc_label(int command, unsigned mods)
{
    int sh = shifted(mods);

    /* Delete asked for directly, as a Trash menu does, is labelled Delete
     * without Shift. */
    if (command == TIKU_TRK_CMD_DELETE) {
        return tiku_str("cmd.delete");
    }
    switch (canonical(command)) {
    /* The actions Shift renames, with Copy and Paste layout among them. */
    case TIKU_TRK_CMD_CLEANUP:
        return tiku_str(sh ? "cmd.cleanup_shift" : "cmd.cleanup");
    case TIKU_TRK_CMD_CLOSE:
        return tiku_str(sh ? "cmd.close_shift" : "cmd.close");
    case TIKU_TRK_CMD_COPY:
        return tiku_str(sh ? "cmd.copy_shift" : "cmd.copy");
    case TIKU_TRK_CMD_CUT:
        return tiku_str(sh ? "cmd.cut_shift" : "cmd.cut");
    case TIKU_TRK_CMD_PASTE:
        return tiku_str(sh ? "cmd.paste_shift" : "cmd.paste");
    case TIKU_TRK_CMD_COPY_LAYOUT:
        return tiku_str("cmd.copy_layout");
    case TIKU_TRK_CMD_PASTE_LAYOUT:
        return tiku_str("cmd.paste_layout");
    case TIKU_TRK_CMD_CREATE_LINK:
        return tiku_str(sh ? "cmd.create_link_shift" : "cmd.create_link");
    case TIKU_TRK_CMD_LINK_HERE:
        return tiku_str(sh ? "cmd.link_here_shift" : "cmd.link_here");
    case TIKU_TRK_CMD_MOVE_TO_TRASH:
        return tiku_str(sh ? "cmd.move_to_trash_shift" : "cmd.move_to_trash");

    case TIKU_TRK_CMD_FIND:
        return tiku_str("cmd.find");
    case TIKU_TRK_CMD_NEW_FOLDER:
        return tiku_str("cmd.new_folder");
    case TIKU_TRK_CMD_OPEN:
        return tiku_str("cmd.open");
    case TIKU_TRK_CMD_EDIT_QUERY:
        return tiku_str("cmd.edit_query");
    case TIKU_TRK_CMD_IDENTIFY:
        return tiku_str("cmd.identify");
    case TIKU_TRK_CMD_OPEN_WITH:
        return tiku_str("cmd.open_with");
    case TIKU_TRK_CMD_GET_INFO:
        return tiku_str("cmd.get_info");
    case TIKU_TRK_CMD_EDIT_NAME:
        return tiku_str("cmd.edit_name");
    case TIKU_TRK_CMD_DUPLICATE:
        return tiku_str("cmd.duplicate");
    case TIKU_TRK_CMD_RESTORE:
        return tiku_str("cmd.restore");
    case TIKU_TRK_CMD_EMPTY_TRASH:
        return tiku_str("cmd.empty_trash");
    case TIKU_TRK_CMD_MOVE_TO:
        return tiku_str("cmd.move_to");
    case TIKU_TRK_CMD_COPY_TO:
        return tiku_str("cmd.copy_to");
    case TIKU_TRK_CMD_ICON_VIEW:
        return tiku_str("cmd.icon_view");
    case TIKU_TRK_CMD_MINI_ICON_VIEW:
        return tiku_str("cmd.mini_icon_view");
    case TIKU_TRK_CMD_LIST_VIEW:
        return tiku_str("cmd.list_view");
    case TIKU_TRK_CMD_RESIZE_TO_FIT:
        return tiku_str("cmd.resize_to_fit");
    case TIKU_TRK_CMD_ARRANGE_BY:
        return tiku_str("cmd.arrange_by");
    case TIKU_TRK_CMD_REVERSE_ORDER:
        return tiku_str("cmd.reverse_order");
    case TIKU_TRK_CMD_SELECT_MATCHING:
        return tiku_str("cmd.select_matching");
    case TIKU_TRK_CMD_SELECT_ALL:
        return tiku_str("cmd.select_all");
    case TIKU_TRK_CMD_INVERT_SELECTION:
        return tiku_str("cmd.invert_selection");
    case TIKU_TRK_CMD_OPEN_PARENT:
        return tiku_str("cmd.open_parent");
    case TIKU_TRK_CMD_CLOSE_ALL_WORKSPACE:
        return tiku_str("cmd.close_all_workspace");
    case TIKU_TRK_CMD_PREFERENCES:
        return tiku_str("cmd.preferences");
    case TIKU_TRK_CMD_EDIT_TEMPLATES:
        return tiku_str("cmd.edit_templates");
    case TIKU_TRK_CMD_SORT_BY_NAME:
        return tiku_str("label.name");
    case TIKU_TRK_CMD_SORT_BY_KIND:
        return tiku_str("label.kind");
    case TIKU_TRK_CMD_SORT_BY_SIZE:
        return tiku_str("label.size");
    case TIKU_TRK_CMD_SORT_BY_VALUE:
        return tiku_str("label.value");
    case TIKU_TRK_CMD_ICON_SIZE_32:
        return tiku_str("cmd.icon_size_32");
    case TIKU_TRK_CMD_ICON_SIZE_40:
        return tiku_str("cmd.icon_size_40");
    case TIKU_TRK_CMD_ICON_SIZE_48:
        return tiku_str("cmd.icon_size_48");
    case TIKU_TRK_CMD_ICON_SIZE_64:
        return tiku_str("cmd.icon_size_64");
    case TIKU_TRK_CMD_ICON_SIZE_96:
        return tiku_str("cmd.icon_size_96");
    case TIKU_TRK_CMD_ICON_SIZE_128:
        return tiku_str("cmd.icon_size_128");
    case TIKU_TRK_CMD_SORT_BY_CAP:
        return tiku_str("label.capability");
    case TIKU_TRK_CMD_COL_NAME:
        return tiku_str("label.name");
    case TIKU_TRK_CMD_COL_KIND:
        return tiku_str("label.kind");
    case TIKU_TRK_CMD_COL_SIZE:
        return tiku_str("label.size");
    case TIKU_TRK_CMD_COL_VALUE:
        return tiku_str("label.value");
    case TIKU_TRK_CMD_COL_DATE:
        return tiku_str("cmd.col_date");
    case TIKU_TRK_CMD_COL_PATH:
        return tiku_str("cmd.col_path");
    case TIKU_TRK_CMD_COL_PERM:
        return tiku_str("cmd.col_perm");
    case TIKU_TRK_CMD_COL_ORIGINAL:
        return tiku_str("cmd.col_original");
    case TIKU_TRK_CMD_COL_CAP:
        return tiku_str("label.capability");
    case TIKU_TRK_CMD_ADD_ONS:
        return tiku_str("cmd.add_ons");
    case TIKU_TRK_CMD_MOUNT:
        return tiku_str("cmd.mount");
    case TIKU_TRK_CMD_UNMOUNT:
        return tiku_str("cmd.unmount");
    case TIKU_TRK_CMD_MOUNT_ALL:
        return tiku_str("cmd.mount_all");
    case TIKU_TRK_CMD_MOUNT_SETTINGS:
        return tiku_str("cmd.mount_settings");
    case TIKU_TRK_CMD_OPEN_WITH_WINDOW:
        return tiku_str("cmd.open_with_window");
    case TIKU_TRK_CMD_MOVE_HERE:
        return tiku_str("cmd.move_here");
    case TIKU_TRK_CMD_COPY_HERE:
        return tiku_str("cmd.copy_here");
    case TIKU_TRK_CMD_ABOUT:
        return tiku_str("cmd.about");
    case TIKU_TRK_CMD_SHUTDOWN:
        return tiku_str("cmd.shutdown");
    case TIKU_TRK_CMD_CREATE_QUERY:
        return tiku_str("cmd.create_query");
    case TIKU_TRK_CMD_CREATE_QUERY_TPL:
        return tiku_str("cmd.create_query_tpl");
    case TIKU_TRK_CMD_CANCEL:
        return tiku_str("button.cancel");
    default:                             return "";
    }
}

int
tiku_trk_sc_command(int command, unsigned mods)
{
    int sh = shifted(mods);

    if (command == TIKU_TRK_CMD_DELETE) {
        return TIKU_TRK_CMD_DELETE;
    }
    switch (canonical(command)) {
    case TIKU_TRK_CMD_CLEANUP:
        return sh ? TIKU_TRK_CMD_CLEANUP_ALL : TIKU_TRK_CMD_CLEANUP;
    case TIKU_TRK_CMD_CLOSE:
        return sh ? TIKU_TRK_CMD_CLOSE_ALL : TIKU_TRK_CMD_CLOSE;
    case TIKU_TRK_CMD_COPY:
        return sh ? TIKU_TRK_CMD_COPY_MORE : TIKU_TRK_CMD_COPY;
    case TIKU_TRK_CMD_CUT:
        return sh ? TIKU_TRK_CMD_CUT_MORE : TIKU_TRK_CMD_CUT;
    case TIKU_TRK_CMD_PASTE:
        return sh ? TIKU_TRK_CMD_PASTE_LINKS : TIKU_TRK_CMD_PASTE;
    case TIKU_TRK_CMD_CREATE_LINK:
        return sh ? TIKU_TRK_CMD_CREATE_REL_LINK : TIKU_TRK_CMD_CREATE_LINK;
    case TIKU_TRK_CMD_LINK_HERE:
        return sh ? TIKU_TRK_CMD_REL_LINK_HERE : TIKU_TRK_CMD_LINK_HERE;
    case TIKU_TRK_CMD_MOVE_TO_TRASH:
        return sh ? TIKU_TRK_CMD_DELETE : TIKU_TRK_CMD_MOVE_TO_TRASH;
    default:
        return canonical(command);
    }
}

int
tiku_trk_sc_command_for_key(unsigned key, unsigned mods)
{
    int shift = (mods & 0x01u) != 0;    /* TIKU_MOD_SHIFT  */
    int option = (mods & 0x04u) != 0;   /* TIKU_MOD_OPTION */

    switch (key) {
    case 'o': return option ? TIKU_TRK_CMD_OPEN_WITH : TIKU_TRK_CMD_OPEN;
    case 'i': return TIKU_TRK_CMD_GET_INFO;
    case 'e': return TIKU_TRK_CMD_EDIT_NAME;
    case 'g': return TIKU_TRK_CMD_EDIT_QUERY;
    case 'u': return TIKU_TRK_CMD_UNMOUNT;
    case 'n': return TIKU_TRK_CMD_NEW_FOLDER;
    case 'x': return shift ? TIKU_TRK_CMD_CUT_MORE : TIKU_TRK_CMD_CUT;
    case 'c': return shift ? TIKU_TRK_CMD_COPY_MORE : TIKU_TRK_CMD_COPY;
    case 'v': return shift ? TIKU_TRK_CMD_PASTE_LINKS : TIKU_TRK_CMD_PASTE;
    case 'k': return shift ? TIKU_TRK_CMD_CLEANUP_ALL
                           : TIKU_TRK_CMD_CLEANUP;
    default:  return 0;
    }
}

char
tiku_trk_sc_key(int command)
{
    switch (canonical(command)) {
    case TIKU_TRK_CMD_FIND:              return 'F';
    case TIKU_TRK_CMD_NEW_FOLDER:        return 'N';
    case TIKU_TRK_CMD_OPEN:              return 'O';
    case TIKU_TRK_CMD_EDIT_QUERY:        return 'G';
    case TIKU_TRK_CMD_OPEN_WITH:         return 'O';   /* with Ctrl added   */
    case TIKU_TRK_CMD_GET_INFO:          return 'I';
    case TIKU_TRK_CMD_EDIT_NAME:         return 'E';
    case TIKU_TRK_CMD_DUPLICATE:         return 'D';
    case TIKU_TRK_CMD_CUT:               return 'X';
    case TIKU_TRK_CMD_COPY:              return 'C';
    case TIKU_TRK_CMD_PASTE:             return 'V';
    case TIKU_TRK_CMD_CLEANUP:           return 'K';
    case TIKU_TRK_CMD_RESIZE_TO_FIT:     return 'Y';
    case TIKU_TRK_CMD_SELECT_MATCHING:   return 'A';   /* with Shift        */
    case TIKU_TRK_CMD_SELECT_ALL:        return 'A';
    case TIKU_TRK_CMD_INVERT_SELECTION:  return 'S';
    case TIKU_TRK_CMD_CLOSE:             return 'W';
    case TIKU_TRK_CMD_CLOSE_ALL_WORKSPACE: return 'Q';
    case TIKU_TRK_CMD_PREFERENCES:       return ',';
    case TIKU_TRK_CMD_ICON_VIEW:         return '1';
    /* Open parent is Cmd+Up and shows no accelerator: the font has no
     * arrow glyph. */
    case TIKU_TRK_CMD_MINI_ICON_VIEW:    return '2';
    case TIKU_TRK_CMD_LIST_VIEW:         return '3';
    /* Move to Trash is bound to plain Del, without the command key. */
    case TIKU_TRK_CMD_MOVE_TO_TRASH:     return 0x7F;
    default:                             return 0;
    }
}

unsigned
tiku_trk_sc_mods(int command, unsigned mods)
{
    unsigned live = mods & TIKU_TRK_MOD_SHIFT;

    switch (canonical(command)) {
    case TIKU_TRK_CMD_MOVE_TO_TRASH:
        /* No command key; Shift joins it to mean Delete. */
        return live;
    case TIKU_TRK_CMD_SELECT_MATCHING:
        return TIKU_MENU_MOD_CMD | TIKU_MENU_MOD_SHIFT;
    case TIKU_TRK_CMD_OPEN_WITH:
        return TIKU_MENU_MOD_CMD | TIKU_MENU_MOD_CTRL;
    /* The five that carry the live shift into their own shortcut, so the
     * key shown is the key that fires the shifted command. */
    case TIKU_TRK_CMD_CLEANUP:
    case TIKU_TRK_CMD_CLOSE:
    case TIKU_TRK_CMD_COPY:
    case TIKU_TRK_CMD_CUT:
    case TIKU_TRK_CMD_PASTE:
        return TIKU_MENU_MOD_CMD | live;
    default:
        return (tiku_trk_sc_key(command) != 0) ? TIKU_MENU_MOD_CMD : 0u;
    }
}

int
tiku_trk_sc_enabled(int command, const tiku_trk_sc_ctx_t *c)
{
    if (c == NULL) {
        return 0;
    }
    switch (canonical(command)) {
    /* Always available. */
    case TIKU_TRK_CMD_FIND:
    case TIKU_TRK_CMD_GET_INFO:            /* enabled even with no selection */
    case TIKU_TRK_CMD_RESIZE_TO_FIT:
    case TIKU_TRK_CMD_SELECT_MATCHING:
    case TIKU_TRK_CMD_SELECT_ALL:
    case TIKU_TRK_CMD_INVERT_SELECTION:
    case TIKU_TRK_CMD_CLOSE:
    case TIKU_TRK_CMD_CLOSE_ALL_WORKSPACE:
    case TIKU_TRK_CMD_PREFERENCES:
    case TIKU_TRK_CMD_REVERSE_ORDER:
    case TIKU_TRK_CMD_ICON_VIEW:
    case TIKU_TRK_CMD_MINI_ICON_VIEW:
    case TIKU_TRK_CMD_LIST_VIEW:
    case TIKU_TRK_CMD_ABOUT:
    case TIKU_TRK_CMD_SHUTDOWN:
    case TIKU_TRK_CMD_CREATE_QUERY:
    case TIKU_TRK_CMD_CREATE_QUERY_TPL:
    case TIKU_TRK_CMD_CANCEL:
        return 1;

    /* Something has to be selected. */
    case TIKU_TRK_CMD_OPEN:
    case TIKU_TRK_CMD_OPEN_WITH:
    case TIKU_TRK_CMD_IDENTIFY:
    case TIKU_TRK_CMD_RESTORE:
    case TIKU_TRK_CMD_COPY_TO:
        return c->selection > 0;

    /* Selected, and where it lives can be written. */
    case TIKU_TRK_CMD_MOVE_TO:
    case TIKU_TRK_CMD_MOVE_TO_TRASH:
        return c->selection > 0 && !c->selection_ro;

    /* Only in a query's results window (CW-033). */
    case TIKU_TRK_CMD_EDIT_QUERY:
        return c->is_query;

    case TIKU_TRK_CMD_DUPLICATE:
        return c->can_trash_or_dup;
    case TIKU_TRK_CMD_EDIT_NAME:
        return c->can_edit_name;
    case TIKU_TRK_CMD_OPEN_PARENT:
        return c->can_open_parent;
    case TIKU_TRK_CMD_EMPTY_TRASH:
        return c->trash_full;

    /* With a text field focused, Cut, Copy and Paste are its text verbs and
     * always enabled.  Cut is refused in the root, the Trash and virtual
     * folders. */
    case TIKU_TRK_CMD_CUT:
        if (c->text_focus) {
            return 1;
        }
        if (c->is_root || c->is_trash || c->is_virtual) {
            return 0;
        }
        return c->selection > 0 && !c->target_ro;
    case TIKU_TRK_CMD_COPY:
        return c->text_focus || c->selection > 0;
    case TIKU_TRK_CMD_PASTE:
        if (c->text_focus) {
            return 1;
        }
        return c->clipboard_has_refs && !c->target_ro && !c->is_trash &&
               !c->is_query && !c->is_virtual;
    case TIKU_TRK_CMD_COPY_LAYOUT:
        return c->view_mode == TIKU_TRK_MODE_LIST;
    case TIKU_TRK_CMD_PASTE_LAYOUT:
        return c->view_mode == TIKU_TRK_MODE_LIST &&
               c->clipboard_has_layout;

    case TIKU_TRK_CMD_NEW_FOLDER:
        return !(c->is_root || c->is_trash || c->in_trash || c->is_query ||
                 c->is_virtual) && !c->target_ro;

    case TIKU_TRK_CMD_CREATE_LINK:
    case TIKU_TRK_CMD_LINK_HERE:
        return c->selection > 0;

    case TIKU_TRK_CMD_ADD_ONS:
    case TIKU_TRK_CMD_MOUNT:
        return 0;

    /* Unmount needs a selected volume other than the boot volume
     * (CW-034). */
    case TIKU_TRK_CMD_UNMOUNT:
        return c->selection_is_volume && !c->selection_is_boot;
    case TIKU_TRK_CMD_MOUNT_ALL:
    case TIKU_TRK_CMD_MOUNT_SETTINGS:
        return 1;
    case TIKU_TRK_CMD_OPEN_WITH_WINDOW:
        return c->selection > 0;

    /* Arrange by and Clean up apply outside list view, where the column
     * titles sort. */
    case TIKU_TRK_CMD_ARRANGE_BY:
        return c->view_mode != TIKU_TRK_MODE_LIST;
    case TIKU_TRK_CMD_CLEANUP:
        return c->view_mode != TIKU_TRK_MODE_LIST;

    default:
        return 1;
    }
}

int
tiku_trk_sc_add(tiku_menu_t *m, int command, const tiku_trk_sc_ctx_t *c)
{
    unsigned mods = (c != NULL) ? c->mods : 0u;

    return tiku_menu_add(m, tiku_trk_sc_label(command, mods),
                             tiku_trk_sc_command(command, mods),
                             tiku_trk_sc_key(command),
                             tiku_trk_sc_mods(command, mods),
                             tiku_trk_sc_enabled(command, c));
}

void
tiku_trk_sc_update(tiku_menu_t *m, const tiku_trk_sc_ctx_t *c)
{
    unsigned mods = (c != NULL) ? c->mods : 0u;
    int i;

    if (m == NULL) {
        return;
    }
    for (i = 0; i < m->count; i++) {
        tiku_menu_item_t *it = &m->item[i];
        int base;

        if (it->separator) {
            continue;
        }
        if (it->submenu != NULL) {
            /* A submenu holder keeps its own label and state; its contents
             * are updated. */
            tiku_trk_sc_update(it->submenu, c);
            continue;
        }
        /* In the Trash, Delete is the base action; elsewhere the same
         * command is Move to Trash's shifted face, which turns back when
         * Shift is released. */
        base = (it->command == TIKU_TRK_CMD_DELETE && c != NULL &&
                (c->is_trash || c->in_trash))
                   ? TIKU_TRK_CMD_DELETE : canonical(it->command);
        if (tiku_trk_sc_label(base, mods)[0] == '\0') {
            /*
             * A command the table does not know -- a row its builder
             * labelled, such as a partition under Mount or an application
             * under Open with -- keeps its own label and enabled state.
             */
            continue;
        }
        tiku_menu_relabel(m, i, tiku_trk_sc_label(base, mods),
                              tiku_trk_sc_command(base, mods),
                              tiku_trk_sc_key(base),
                              tiku_trk_sc_mods(base, mods));
        it->enabled = tiku_trk_sc_enabled(base, c);
    }
    /* Marks are left to the caller, which sets them from view state. */
}
