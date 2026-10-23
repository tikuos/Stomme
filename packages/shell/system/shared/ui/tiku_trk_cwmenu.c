/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_cwmenu.c - building a folder window's menus, the Templates
 * hierarchy and the Mount submenu among them.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_strings.h"
#include "tiku_trk_cwmenu.h"
#include "tiku_trk_view_defs.h"
#include "tiku_volume.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#define ADD(m, cmd)  (void)tiku_trk_sc_add((m), (cmd), c)
#define SEP(m)       (void)tiku_menu_add_separator(m)

void
tiku_trk_cwmenu_init(tiku_trk_cwmenu_t *cw)
{
    if (cw == NULL) {
        return;
    }
    memset(cw, 0, sizeof *cw);
    {
        const char *home = getenv("HOME");

        if (home != NULL && home[0] != '\0') {
            snprintf(cw->templates_root, sizeof cw->templates_root,
                     "%s/Templates", home);
        }
    }
    tiku_menubar_init(&cw->bar);
    snprintf(cw->file.title, sizeof cw->file.title, "%s",
             tiku_str("menu.file"));
    snprintf(cw->new_menu.title, sizeof cw->new_menu.title, "%s",
             tiku_str("menu.new"));
    snprintf(cw->templates.title, sizeof cw->templates.title, "%s",
             tiku_str("menu.templates"));
    snprintf(cw->window.title, sizeof cw->window.title, "%s",
             tiku_str("menu.window"));
    snprintf(cw->attributes.title, sizeof cw->attributes.title, "%s",
             tiku_str("menu.attributes"));
    snprintf(cw->addons.title, sizeof cw->addons.title, "%s",
             tiku_str("menu.add_ons"));
    (void)tiku_menu_add(&cw->addons, tiku_str("menu.no_add_ons"), TIKU_TRK_CMD_NONE,
                            0, 0u, 0);
    (void)tiku_menubar_add(&cw->bar, &cw->file);
    (void)tiku_menubar_add(&cw->bar, &cw->window);
}

/* One entry of a Templates folder. */
typedef struct {
    char name[TIKU_NAME_MAX];
    char path[TIKU_PATH_MAX];
    int is_dir;                 /* a subfolder: a submenu of its own */
} template_child_t;

/** @brief Name order, case first ignored then not. */
static int
template_child_cmp(const void *a, const void *b)
{
    const template_child_t *aa = a;
    const template_child_t *bb = b;
    int c = strcasecmp(aa->name, bb->name);

    return c != 0 ? c : strcmp(aa->name, bb->name);
}

/**
 * @brief A submenu node for the Templates folder at @p path.
 *        @return its index, -1 when the table is full.
 */
static int
template_node_new(tiku_trk_cwmenu_t *cw, const char *path)
{
    int n;

    if (cw == NULL || path == NULL || cw->template_node_count >= 64) {
        return -1;
    }
    n = cw->template_node_count++;
    memset(&cw->template_node[n], 0, sizeof cw->template_node[n]);
    snprintf(cw->template_node[n].path,
             sizeof cw->template_node[n].path, "%s", path);
    tiku_menu_clear(&cw->template_node[n].menu);
    return n;
}

/**
 * @brief A command number for the template at @p path, in the add-submenu
 *        range when @p add_submenu.  @return the command, -1 when full.
 */
static int
template_command(tiku_trk_cwmenu_t *cw, const char *path, int add_submenu)
{
    int n;

    if (cw == NULL || path == NULL || cw->template_entry_count >= 128) {
        return -1;
    }
    n = cw->template_entry_count++;
    cw->template_entry[n].command = (add_submenu
                                     ? TIKU_TRK_CMD_ADD_TEMPLATE_SUBMENU_BASE
                                     : TIKU_TRK_CMD_TEMPLATE_BASE) + n;
    cw->template_entry[n].add_submenu = add_submenu;
    snprintf(cw->template_entry[n].path,
             sizeof cw->template_entry[n].path, "%s", path);
    return cw->template_entry[n].command;
}

/**
 * @brief Fill node @p node's menu from its folder: a row per template,
 *        a submenu per subfolder, to the depth a menu can nest.
 */
static void
template_fill(tiku_trk_cwmenu_t *cw, int node, int depth)
{
    template_child_t child[128];
    DIR *dir;
    struct dirent *ent;
    int n = 0, i;
    tiku_menu_t *menu;

    if (cw == NULL || node < 0 || node >= cw->template_node_count ||
        depth > TIKU_MENU_DEPTH_MAX) {
        return;
    }
    menu = &cw->template_node[node].menu;
    dir = opendir(cw->template_node[node].path);
    if (dir == NULL) {
        return;
    }
    while ((ent = readdir(dir)) != NULL && n < 128) {
        struct stat st;
        char path[TIKU_PATH_MAX];

        if (ent->d_name[0] == '.' || ent->d_name[0] == '\0') {
            continue;
        }
        if (snprintf(path, sizeof path, "%s/%s",
                     cw->template_node[node].path, ent->d_name) < 0 ||
            stat(path, &st) != 0 ||
            (!S_ISREG(st.st_mode) && !S_ISDIR(st.st_mode) &&
             !S_ISLNK(st.st_mode))) {
            continue;
        }
        snprintf(child[n].name, sizeof child[n].name, "%.127s", ent->d_name);
        snprintf(child[n].path, sizeof child[n].path, "%s", path);
        child[n].is_dir = S_ISDIR(st.st_mode);
        n++;
    }
    (void)closedir(dir);
    qsort(child, (size_t)n, sizeof child[0], template_child_cmp);
    for (i = 0; i < n; i++) {
        if (child[i].is_dir && depth < TIKU_MENU_DEPTH_MAX) {
            int sub = template_node_new(cw, child[i].path);

            if (sub >= 0) {
                snprintf(cw->template_node[sub].menu.title,
                         sizeof cw->template_node[sub].menu.title,
                         "%.47s", child[i].name);
                (void)tiku_menu_add_submenu(menu, child[i].name,
                                                &cw->template_node[sub].menu,
                                                1);
                template_fill(cw, sub, depth + 1);
                /* A subfolder is both a submenu and a template: its first
                 * row, "New <name>", uses the subfolder itself. */
                {
                    int use = template_command(cw, child[i].path, 0);

                    if (use >= 0) {
                        (void)tiku_menu_insert(
                            &cw->template_node[sub].menu, 0,
                            "New " /* followed by the directory name below */,
                            use, 0, 0u, 1);
                        snprintf(cw->template_node[sub].menu.item[0].label,
                                 sizeof cw->template_node[sub].menu.item[0].label,
                                 "New %.37s", child[i].name);
                    }
                }
            }
        } else if (!child[i].is_dir) {
            int command = template_command(cw, child[i].path, 0);

            if (command >= 0) {
                (void)tiku_menu_add(menu, child[i].name, command,
                                        0, 0u, 1);
            }
        }
    }
    /* Every template folder, at every level, ends with an "Add new
     * submenu" row.  An empty root gets none, so with no templates the
     * File menu shows a plain New folder. */
    if (n > 0 || node != 0) {
        int command = template_command(cw,
                                       cw->template_node[node].path, 1);

        if (command >= 0) {
            (void)tiku_menu_add_separator(menu);
            (void)tiku_menu_add(menu, tiku_str("menu.add_new_submenu"),
                                    command,
                                    0, 0u, 1);
        }
    }
}

/** @brief The Templates submenu, rebuilt from the folder if it exists. */
static void
build_templates(tiku_trk_cwmenu_t *cw)
{
    struct stat st;
    int root;

    if (cw == NULL) {
        return;
    }
    cw->template_node_count = 0;
    cw->template_entry_count = 0;
    tiku_menu_clear(&cw->templates);
    if (cw->templates_root[0] == '\0' ||
        stat(cw->templates_root, &st) != 0 || !S_ISDIR(st.st_mode)) {
        return;
    }
    root = template_node_new(cw, cw->templates_root);
    if (root < 0) {
        return;
    }
    cw->template_node[root].menu = cw->templates;
    template_fill(cw, root, 0);
    cw->templates = cw->template_node[root].menu;
}

const char *
tiku_trk_cwmenu_template_path(const tiku_trk_cwmenu_t *cw, int command)
{
    int i;

    if (cw == NULL) {
        return NULL;
    }
    for (i = 0; i < cw->template_entry_count; i++) {
        if (cw->template_entry[i].command == command &&
            !cw->template_entry[i].add_submenu) {
            return cw->template_entry[i].path;
        }
    }
    return NULL;
}

const char *
tiku_trk_cwmenu_template_add_path(const tiku_trk_cwmenu_t *cw, int command)
{
    int i;

    if (cw == NULL) {
        return NULL;
    }
    for (i = 0; i < cw->template_entry_count; i++) {
        if (cw->template_entry[i].command == command &&
            cw->template_entry[i].add_submenu) {
            return cw->template_entry[i].path;
        }
    }
    return NULL;
}

const char *
tiku_trk_cwmenu_templates_root(const tiku_trk_cwmenu_t *cw)
{
    return (cw != NULL && cw->templates_root[0] != '\0')
           ? cw->templates_root : NULL;
}

/** @brief The icon-size submenu: fixed sizes, radio-marked to the current. */
static void
build_icon_size(tiku_trk_cwmenu_t *cw, const tiku_trk_sc_ctx_t *c)
{
    /* Every rung of the ladder, so the menu reaches each size the keyboard
     * does. */
    static const int sizes[] = { TIKU_TRK_CMD_ICON_SIZE_32,
                                 TIKU_TRK_CMD_ICON_SIZE_40,
                                 TIKU_TRK_CMD_ICON_SIZE_48,
                                 TIKU_TRK_CMD_ICON_SIZE_64,
                                 TIKU_TRK_CMD_ICON_SIZE_96,
                                 TIKU_TRK_CMD_ICON_SIZE_128 };
    static const int px[] = { 32, 40, 48, 64, 96, 128 };
    int chosen = TIKU_TRK_CMD_NONE;
    int i, nsz = (int)(sizeof sizes / sizeof sizes[0]);

    tiku_menu_clear(&cw->icon_size);
    for (i = 0; i < nsz; i++) {
        ADD(&cw->icon_size, sizes[i]);
    }
    /* A size is marked only in icon mode. */
    if (c->view_mode == TIKU_TRK_MODE_ICON) {
        for (i = 0; i < nsz; i++) {
            if (c->icon_size == px[i]) {
                chosen = sizes[i];
                break;
            }
        }
    }
    tiku_menu_mark_radio(&cw->icon_size, sizes, nsz, chosen);
}

/** @brief Arrange by: the sort keys, then Reverse order and Clean up. */
static void
build_arrange(tiku_trk_cwmenu_t *cw, const tiku_trk_sc_ctx_t *c)
{
    static const int keys[] = { TIKU_TRK_CMD_SORT_BY_NAME,
                                TIKU_TRK_CMD_SORT_BY_KIND,
                                TIKU_TRK_CMD_SORT_BY_SIZE,
                                TIKU_TRK_CMD_SORT_BY_VALUE,
                                TIKU_TRK_CMD_SORT_BY_CAP };
    int chosen = TIKU_TRK_CMD_SORT_BY_NAME;

    tiku_menu_clear(&cw->arrange);
    ADD(&cw->arrange, TIKU_TRK_CMD_SORT_BY_NAME);
    ADD(&cw->arrange, TIKU_TRK_CMD_SORT_BY_KIND);
    ADD(&cw->arrange, TIKU_TRK_CMD_SORT_BY_SIZE);
    ADD(&cw->arrange, TIKU_TRK_CMD_SORT_BY_VALUE);
    ADD(&cw->arrange, TIKU_TRK_CMD_SORT_BY_CAP);
    SEP(&cw->arrange);
    ADD(&cw->arrange, TIKU_TRK_CMD_REVERSE_ORDER);
    SEP(&cw->arrange);
    ADD(&cw->arrange, TIKU_TRK_CMD_CLEANUP);

    switch (c->sort_field) {
    case TIKU_TRK_COL_KIND:  chosen = TIKU_TRK_CMD_SORT_BY_KIND;  break;
    case TIKU_TRK_COL_SIZE:  chosen = TIKU_TRK_CMD_SORT_BY_SIZE;  break;
    case TIKU_TRK_COL_VALUE: chosen = TIKU_TRK_CMD_SORT_BY_VALUE; break;
    case TIKU_TRK_COL_CAP:   chosen = TIKU_TRK_CMD_SORT_BY_CAP;   break;
    default: break;
    }
    /* The sort field is marked; a field the menu does not offer marks
     * Name. */
    tiku_menu_mark_radio(&cw->arrange, keys,
                             (int)(sizeof keys / sizeof keys[0]), chosen);
    tiku_menu_mark(&cw->arrange, TIKU_TRK_CMD_REVERSE_ORDER,
                       c->reverse_sort);
}

/** @brief Whether the clipboard block is offered: not in the root or the
 *         Trash. */
static int
wants_clipboard(const tiku_trk_sc_ctx_t *c)
{
    return !(c->is_root || c->is_trash || c->in_trash);
}

/**
 * @brief One picker item: a submenu when the shell built one, the plain
 *        panel command when it did not (CW-035).
 */
static void
add_picker(tiku_trk_cwmenu_t *cw, tiku_menu_t *m, int k,
           const tiku_trk_sc_ctx_t *c)
{
    static const int cmd[3] = { TIKU_TRK_CMD_MOVE_TO, TIKU_TRK_CMD_COPY_TO,
                                TIKU_TRK_CMD_CREATE_LINK };

    if (cw->picker[k].count > 0) {
        (void)tiku_menu_add_submenu(m, cw->picker[k].title,
                                        &cw->picker[k], 1);
    } else {
        ADD(m, cmd[k]);
    }
}

/**
 * @brief The Open with row: a submenu of applications when there are any
 *        (AW-048), else the plain item, disabled.
 */
static void
add_open_with(tiku_trk_cwmenu_t *cw, tiku_menu_t *m,
              const tiku_trk_sc_ctx_t *c)
{
    if (cw->open_with.count > 0) {
        (void)tiku_menu_add_submenu(m, tiku_str("menu.open_with"),
                                        &cw->open_with,
                                        c->selection > 0);
    } else {
        ADD(m, TIKU_TRK_CMD_OPEN_WITH);
        tiku_menu_enable(m, TIKU_TRK_CMD_OPEN_WITH, 0);
    }
}

/** @brief The File menu for context @p c. */
static void
build_file(tiku_trk_cwmenu_t *cw, const tiku_trk_sc_ctx_t *c)
{
    tiku_menu_t *m = &cw->file;

    tiku_menu_clear(m);
    /* The Trash window leads with Empty Trash and its own separator. */
    if (c->is_trash) {
        ADD(m, TIKU_TRK_CMD_EMPTY_TRASH);
        SEP(m);
    }
    ADD(m, TIKU_TRK_CMD_FIND);
    if (!c->is_query) {
        if (cw->templates.count > 0) {
            (void)tiku_menu_add_submenu(m, tiku_str("menu.new"),
                                            &cw->new_menu,
                                            tiku_trk_sc_enabled(
                                                TIKU_TRK_CMD_NEW_FOLDER, c));
        } else {
            ADD(m, TIKU_TRK_CMD_NEW_FOLDER);
        }
    }
    SEP(m);
    ADD(m, TIKU_TRK_CMD_OPEN);
    /* Edit query, between Open and Open with, is enabled only for a query
     * (CW-033, CW-042). */
    ADD(m, TIKU_TRK_CMD_EDIT_QUERY);
    add_open_with(cw, m, c);
    ADD(m, TIKU_TRK_CMD_GET_INFO);
    ADD(m, TIKU_TRK_CMD_EDIT_NAME);
    if (c->selection_is_volume) {
        (void)tiku_menu_add_submenu(m, tiku_str("cmd.mount"),
                                        &cw->mount_menu, 1);
        ADD(m, TIKU_TRK_CMD_UNMOUNT);
    }
    /* In the Trash, Delete and Restore stand where Duplicate and Move to
     * Trash would be. */
    if (c->is_trash || c->in_trash) {
        ADD(m, TIKU_TRK_CMD_DELETE);
        ADD(m, TIKU_TRK_CMD_RESTORE);
    } else if (!c->is_root) {
        ADD(m, TIKU_TRK_CMD_DUPLICATE);
        ADD(m, TIKU_TRK_CMD_MOVE_TO_TRASH);
    }
    if (wants_clipboard(c)) {
        SEP(m);
        add_picker(cw, m, 0, c);
        add_picker(cw, m, 1, c);
        add_picker(cw, m, 2, c);
        ADD(m, TIKU_TRK_CMD_CUT);
        ADD(m, TIKU_TRK_CMD_COPY);
        if (!c->is_query) {
            ADD(m, TIKU_TRK_CMD_PASTE);
        }
        SEP(m);
        ADD(m, TIKU_TRK_CMD_IDENTIFY);
    }
    (void)tiku_menu_add_submenu(m, tiku_str("menu.add_ons"), &cw->addons, cw->addons.count > 0);
}

/** @brief The New submenu: New folder, the user's templates, and Edit
 *         templates. */
static void
build_new(tiku_trk_cwmenu_t *cw, const tiku_trk_sc_ctx_t *c)
{
    tiku_menu_t *m = &cw->new_menu;
    int writable = tiku_trk_sc_enabled(TIKU_TRK_CMD_NEW_FOLDER, c);

    tiku_menu_clear(m);
    ADD(m, TIKU_TRK_CMD_NEW_FOLDER);
    if (cw->templates.count > 0) {
        SEP(m);
        (void)tiku_menu_add_submenu(m, tiku_str("menu.templates"),
                                        &cw->templates,
                                        writable);
    }
    SEP(m);
    (void)tiku_menu_add(m, tiku_str("cmd.edit_templates"),
                            TIKU_TRK_CMD_EDIT_TEMPLATES, 0, 0u,
                            cw->templates_root[0] != '\0');
}

/** @brief The Window menu for context @p c, the mode in force ticked. */
static void
build_window(tiku_trk_cwmenu_t *cw, const tiku_trk_sc_ctx_t *c)
{
    tiku_menu_t *m = &cw->window;
    static const int modes[] = { TIKU_TRK_CMD_ICON_VIEW,
                                 TIKU_TRK_CMD_MINI_ICON_VIEW,
                                 TIKU_TRK_CMD_LIST_VIEW };
    int chosen = TIKU_TRK_CMD_LIST_VIEW;

    tiku_menu_clear(m);
    if (c->is_query) {
        /* A results window has no view modes, no arrangement and no
         * parent to open (CW-023). */
        ADD(m, TIKU_TRK_CMD_RESIZE_TO_FIT);
        ADD(m, TIKU_TRK_CMD_SELECT_MATCHING);
        ADD(m, TIKU_TRK_CMD_SELECT_ALL);
        ADD(m, TIKU_TRK_CMD_INVERT_SELECTION);
        ADD(m, TIKU_TRK_CMD_CLOSE);
        ADD(m, TIKU_TRK_CMD_CLOSE_ALL_WORKSPACE);
        return;
    }
    (void)tiku_menu_add_submenu(m, tiku_str("cmd.icon_view"),
                                    &cw->icon_size, 1);
    ADD(m, TIKU_TRK_CMD_MINI_ICON_VIEW);
    ADD(m, TIKU_TRK_CMD_LIST_VIEW);
    SEP(m);
    ADD(m, TIKU_TRK_CMD_RESIZE_TO_FIT);
    (void)tiku_menu_add_submenu(m, tiku_str("cmd.arrange_by"),
                                    &cw->arrange,
                                    tiku_trk_sc_enabled(
                                        TIKU_TRK_CMD_ARRANGE_BY, c));
    ADD(m, TIKU_TRK_CMD_SELECT_MATCHING);
    ADD(m, TIKU_TRK_CMD_SELECT_ALL);
    ADD(m, TIKU_TRK_CMD_INVERT_SELECTION);
    ADD(m, TIKU_TRK_CMD_OPEN_PARENT);
    ADD(m, TIKU_TRK_CMD_CLOSE);
    ADD(m, TIKU_TRK_CMD_CLOSE_ALL_WORKSPACE);
    SEP(m);
    ADD(m, TIKU_TRK_CMD_PREFERENCES);

    /* The Icon view row, item 0, is the icon-size submenu's holder, so its
     * mark is set on it directly. */
    switch (c->view_mode) {
    case TIKU_TRK_MODE_ICON: chosen = TIKU_TRK_CMD_ICON_VIEW;      break;
    case TIKU_TRK_MODE_MINI: chosen = TIKU_TRK_CMD_MINI_ICON_VIEW; break;
    default:                 chosen = TIKU_TRK_CMD_LIST_VIEW;      break;
    }
    tiku_menu_mark_radio(m, modes, 3, chosen);
    m->item[0].marked = (c->view_mode == TIKU_TRK_MODE_ICON);
}

/** @brief The Attributes menu: a row per column. */
static void
build_attributes(tiku_trk_cwmenu_t *cw, const tiku_trk_sc_ctx_t *c)
{
    tiku_menu_t *m = &cw->attributes;

    tiku_menu_clear(m);
    ADD(m, TIKU_TRK_CMD_COL_NAME);
    ADD(m, TIKU_TRK_CMD_COL_KIND);
    ADD(m, TIKU_TRK_CMD_COL_VALUE);
    ADD(m, TIKU_TRK_CMD_COL_SIZE);
    ADD(m, TIKU_TRK_CMD_COL_CAP);
    ADD(m, TIKU_TRK_CMD_COL_DATE);
    ADD(m, TIKU_TRK_CMD_COL_PATH);
    ADD(m, TIKU_TRK_CMD_COL_PERM);
    ADD(m, TIKU_TRK_CMD_COL_ORIGINAL);
    /* Name is always a column: marked and disabled. */
    tiku_menu_mark(m, TIKU_TRK_CMD_COL_NAME, 1);
    tiku_menu_enable(m, TIKU_TRK_CMD_COL_NAME, 0);
    /* Every other row is marked from the columns the view has. */
    tiku_menu_mark(m, TIKU_TRK_CMD_COL_KIND,
                       (c->columns & (1u << TIKU_TRK_COL_KIND)) != 0u);
    tiku_menu_mark(m, TIKU_TRK_CMD_COL_VALUE,
                       (c->columns & (1u << TIKU_TRK_COL_VALUE)) != 0u);
    tiku_menu_mark(m, TIKU_TRK_CMD_COL_SIZE,
                       (c->columns & (1u << TIKU_TRK_COL_SIZE)) != 0u);
    tiku_menu_mark(m, TIKU_TRK_CMD_COL_CAP,
                       (c->columns & (1u << TIKU_TRK_COL_CAP)) != 0u);
    tiku_menu_mark(m, TIKU_TRK_CMD_COL_DATE,
                       (c->columns & (1u << TIKU_TRK_COL_DATE)) != 0u);
    tiku_menu_mark(m, TIKU_TRK_CMD_COL_PATH,
                       (c->columns & (1u << TIKU_TRK_COL_PATH)) != 0u);
    tiku_menu_mark(m, TIKU_TRK_CMD_COL_PERM,
                       (c->columns & (1u << TIKU_TRK_COL_PERM)) != 0u);
    tiku_menu_mark(m, TIKU_TRK_CMD_COL_ORIGINAL,
                       (c->columns & (1u << TIKU_TRK_COL_ORIGINAL)) != 0u);
    SEP(m);
    ADD(m, TIKU_TRK_CMD_COPY_LAYOUT);
    ADD(m, TIKU_TRK_CMD_PASTE_LAYOUT);
    if (cw->per_type_count > 0) {
        int k;

        /* A submenu per type the folder holds, with that type's columns
         * (CW-026). */
        SEP(m);
        for (k = 0; k < cw->per_type_count; k++) {
            int at = tiku_menu_add_submenu(m, cw->per_type_name[k],
                                               &cw->per_type[k], 1);

            if (at >= 0) {
                /* The type's own art, resolved with the supertype
                 * fallback where it was built (IV-056). */
                snprintf(m->item[at].icon, sizeof m->item[at].icon, "%s",
                         cw->per_type_icon[k]);
            }
        }
    }
}

/** @brief The context menus: on a pose, on the window background, on a
 *         volume, and on the Trash. */
static void
build_contexts(tiku_trk_cwmenu_t *cw, const tiku_trk_sc_ctx_t *c)
{
    tiku_menu_t *m;

    /* On a pose. */
    m = &cw->pose_ctx;
    tiku_menu_clear(m);
    ADD(m, TIKU_TRK_CMD_OPEN);
    /* Edit query, between Open and Open with, is enabled only for a query
     * (CW-033, CW-042). */
    ADD(m, TIKU_TRK_CMD_EDIT_QUERY);
    add_open_with(cw, m, c);
    ADD(m, TIKU_TRK_CMD_GET_INFO);
    ADD(m, TIKU_TRK_CMD_EDIT_NAME);
    if (c->is_trash || c->in_trash) {
        ADD(m, TIKU_TRK_CMD_DELETE);
        ADD(m, TIKU_TRK_CMD_RESTORE);
    } else {
        ADD(m, TIKU_TRK_CMD_DUPLICATE);
        ADD(m, TIKU_TRK_CMD_MOVE_TO_TRASH);
    }
    if (wants_clipboard(c)) {
        SEP(m);
        add_picker(cw, m, 0, c);
        add_picker(cw, m, 1, c);
        add_picker(cw, m, 2, c);
        ADD(m, TIKU_TRK_CMD_CUT);
        ADD(m, TIKU_TRK_CMD_COPY);
        ADD(m, TIKU_TRK_CMD_PASTE);
        SEP(m);
        ADD(m, TIKU_TRK_CMD_IDENTIFY);
    }

    (void)tiku_menu_add_submenu(m, tiku_str("menu.add_ons"), &cw->addons, cw->addons.count > 0);

    /* On empty window background. */
    m = &cw->window_ctx;
    tiku_menu_clear(m);
    if (c->is_query) {
        ADD(m, TIKU_TRK_CMD_RESIZE_TO_FIT);
        ADD(m, TIKU_TRK_CMD_SELECT_MATCHING);
        ADD(m, TIKU_TRK_CMD_SELECT_ALL);
        ADD(m, TIKU_TRK_CMD_CLOSE);
    } else {
        if (c->is_trash) {
            ADD(m, TIKU_TRK_CMD_EMPTY_TRASH);
            SEP(m);
        }
        if (!(c->is_root || c->is_trash || c->in_trash)) {
            if (cw->templates.count > 0) {
                (void)tiku_menu_add_submenu(
                    m, "New", &cw->new_menu,
                    tiku_trk_sc_enabled(TIKU_TRK_CMD_NEW_FOLDER, c));
            } else {
                ADD(m, TIKU_TRK_CMD_NEW_FOLDER);
            }
        }
        if (c->view_mode != TIKU_TRK_MODE_LIST) {
            (void)tiku_menu_add_submenu(m, tiku_str("cmd.arrange_by"),
                                    &cw->arrange,
                                            1);
        }
        if (c->is_desktop) {
            (void)tiku_menu_add_submenu(m, "Icon size", &cw->icon_size,
                                            1);
        }
        if (wants_clipboard(c)) {
            ADD(m, TIKU_TRK_CMD_PASTE);
        }
        ADD(m, TIKU_TRK_CMD_SELECT_MATCHING);
        ADD(m, TIKU_TRK_CMD_SELECT_ALL);
        ADD(m, TIKU_TRK_CMD_OPEN_PARENT);
        if (c->is_desktop || c->is_root) {
            /* Mount, on the Desktop and the root (CW-034). */
            SEP(m);
            (void)tiku_menu_add_submenu(m, tiku_str("cmd.mount"),
                                        &cw->mount_menu, 1);
        }
        (void)tiku_menu_add_submenu(m, tiku_str("menu.add_ons"), &cw->addons, cw->addons.count > 0);
    }

    /* On a volume or the root: no clipboard, no trashing. */
    m = &cw->volume_ctx;
    tiku_menu_clear(m);
    ADD(m, TIKU_TRK_CMD_OPEN);
    /* Edit query is enabled only for a query (CW-033, CW-042). */
    ADD(m, TIKU_TRK_CMD_EDIT_QUERY);
    ADD(m, TIKU_TRK_CMD_GET_INFO);
    ADD(m, TIKU_TRK_CMD_EDIT_NAME);
    if (c->selection_is_volume) {
        ADD(m, TIKU_TRK_CMD_UNMOUNT);
    }
    (void)tiku_menu_add_submenu(m, tiku_str("cmd.mount"),
                                        &cw->mount_menu, 1);
    (void)tiku_menu_add_submenu(m, tiku_str("menu.add_ons"), &cw->addons, cw->addons.count > 0);

    /* On the Trash itself: Empty Trash first. */
    m = &cw->trash_ctx;
    tiku_menu_clear(m);
    ADD(m, TIKU_TRK_CMD_EMPTY_TRASH);
    ADD(m, TIKU_TRK_CMD_OPEN);
    /* Edit query is enabled only for a query (CW-033, CW-042). */
    ADD(m, TIKU_TRK_CMD_EDIT_QUERY);
    ADD(m, TIKU_TRK_CMD_GET_INFO);
    (void)tiku_menu_add_submenu(m, tiku_str("menu.add_ons"), &cw->addons, cw->addons.count > 0);
}

/** @brief Mount >: every partition, mounted ones marked (CW-034). */
static void
build_mount_menu(tiku_trk_cwmenu_t *cw, const tiku_trk_sc_ctx_t *c)
{
    tiku_partition_t part[TIKU_TRK_MOUNT_PART_MAX];
    (void)c;
    int n = tiku_partitions_scan(part, TIKU_TRK_MOUNT_PART_MAX);
    int i;

    tiku_menu_clear(&cw->mount_menu);
    cw->mount_parts = n;
    for (i = 0; i < n; i++) {
        char label[64];

        snprintf(cw->mount_part[i], sizeof cw->mount_part[i], "%s",
                 part[i].name);
        snprintf(label, sizeof label, "%s (%llu MB)", part[i].name,
                 (unsigned long long)(part[i].kb / 1024u));
        /* A mounted partition is marked and disabled. */
        (void)tiku_menu_add(&cw->mount_menu, label,
                                TIKU_TRK_CMD_MOUNT_PART_BASE + i, 0, 0u,
                                !part[i].mounted);
        tiku_menu_set_icon(&cw->mount_menu,
                               TIKU_TRK_CMD_MOUNT_PART_BASE + i, "volume");
        tiku_menu_mark(&cw->mount_menu,
                           TIKU_TRK_CMD_MOUNT_PART_BASE + i,
                           part[i].mounted);
    }
    if (n > 0) {
        SEP(&cw->mount_menu);
    }
    ADD(&cw->mount_menu, TIKU_TRK_CMD_MOUNT_ALL);
    ADD(&cw->mount_menu, TIKU_TRK_CMD_MOUNT_SETTINGS);
}

void
tiku_trk_cwmenu_rebuild(tiku_trk_cwmenu_t *cw, const tiku_trk_sc_ctx_t *c)
{
    if (cw == NULL || c == NULL) {
        return;
    }
    build_icon_size(cw, c);
    build_arrange(cw, c);
    build_mount_menu(cw, c);
    build_templates(cw);
    build_new(cw, c);
    build_file(cw, c);
    build_window(cw, c);
    build_attributes(cw, c);
    build_contexts(cw, c);

    /* Attributes is in the bar only in list view, after Window; in an icon
     * mode it is removed, not disabled. */
    if (c->view_mode == TIKU_TRK_MODE_LIST) {
        if (!cw->attributes_shown) {
            (void)tiku_menubar_insert(&cw->bar, 2, &cw->attributes);
            cw->attributes_shown = 1;
        }
    } else if (cw->attributes_shown) {
        tiku_menubar_remove(&cw->bar, &cw->attributes);
        cw->attributes_shown = 0;
    }
}

tiku_menu_t *
tiku_trk_cwmenu_context(tiku_trk_cwmenu_t *cw, const tiku_model_t *m,
                        const tiku_trk_sc_ctx_t *c)
{
    if (cw == NULL) {
        return NULL;
    }
    tiku_trk_cwmenu_rebuild(cw, c);
    if (m == NULL) {
        return &cw->window_ctx;      /* empty background                    */
    }
    if (m->kind == TIKU_KIND_TRASH) {
        return &cw->trash_ctx;
    }
    if (m->kind == TIKU_KIND_VOLUME ||
        m->kind == TIKU_KIND_ROOT) {
        return &cw->volume_ctx;
    }
    return &cw->pose_ctx;
}

tiku_menu_t *
tiku_trk_cwmenu_drop(tiku_trk_cwmenu_t *cw, int dst_read_only,
                     int src_read_only, unsigned mods)
{
    tiku_trk_sc_ctx_t ctx;
    const tiku_trk_sc_ctx_t *c = &ctx;
    tiku_menu_t *m;

    if (cw == NULL) {
        return NULL;
    }
    memset(&ctx, 0, sizeof ctx);
    ctx.mods = mods;
    /* The enablement table requires a selection for Create link, so the
     * drag's payload counts as one (PVS-039). */
    ctx.selection = 1;
    m = &cw->drop_ctx;
    tiku_menu_clear(m);
    ADD(m, TIKU_TRK_CMD_MOVE_HERE);
    ADD(m, TIKU_TRK_CMD_COPY_HERE);
    ADD(m, TIKU_TRK_CMD_LINK_HERE);
    SEP(m);
    ADD(m, TIKU_TRK_CMD_CANCEL);

    /* An unwritable destination disables every verb, an unwritable source
     * only Move; Cancel stays enabled. */
    if (dst_read_only) {
        tiku_menu_enable(m, TIKU_TRK_CMD_MOVE_HERE, 0);
        tiku_menu_enable(m, TIKU_TRK_CMD_COPY_HERE, 0);
        tiku_menu_enable(m, tiku_trk_sc_command(TIKU_TRK_CMD_LINK_HERE,
                                                    mods), 0);
    } else if (src_read_only) {
        tiku_menu_enable(m, TIKU_TRK_CMD_MOVE_HERE, 0);
    }
    return m;
}

tiku_menu_t *
tiku_trk_cwmenu_view_popup(tiku_trk_cwmenu_t *cw, const tiku_trk_sc_ctx_t *c)
{
    static const int modes[] = { TIKU_TRK_CMD_ICON_VIEW,
                                 TIKU_TRK_CMD_MINI_ICON_VIEW,
                                 TIKU_TRK_CMD_LIST_VIEW };
    tiku_menu_t *m;
    int chosen;

    if (cw == NULL || c == NULL) {
        return NULL;
    }
    m = &cw->view_ctx;
    tiku_menu_clear(m);
    ADD(m, TIKU_TRK_CMD_ICON_VIEW);
    ADD(m, TIKU_TRK_CMD_MINI_ICON_VIEW);
    ADD(m, TIKU_TRK_CMD_LIST_VIEW);
    switch (c->view_mode) {
    case TIKU_TRK_MODE_ICON: chosen = TIKU_TRK_CMD_ICON_VIEW;      break;
    case TIKU_TRK_MODE_MINI: chosen = TIKU_TRK_CMD_MINI_ICON_VIEW; break;
    default:                 chosen = TIKU_TRK_CMD_LIST_VIEW;      break;
    }
    tiku_menu_mark_radio(m, modes, 3, chosen);
    return m;
}

tiku_menu_t *
tiku_trk_cwmenu_query_drop(tiku_trk_cwmenu_t *cw, int dst_read_only)
{
    tiku_trk_sc_ctx_t ctx;
    const tiku_trk_sc_ctx_t *c = &ctx;
    tiku_menu_t *m;

    if (cw == NULL) {
        return NULL;
    }
    /* The Find icon's drop menu (Q-034): save the search as a query or as a
     * template, or cancel.  It reuses the drop menu's storage; only one
     * drop menu is open at a time. */
    memset(&ctx, 0, sizeof ctx);
    ctx.selection = 1;
    m = &cw->drop_ctx;
    tiku_menu_clear(m);
    ADD(m, TIKU_TRK_CMD_CREATE_QUERY);
    ADD(m, TIKU_TRK_CMD_CREATE_QUERY_TPL);
    SEP(m);
    ADD(m, TIKU_TRK_CMD_CANCEL);
    if (dst_read_only) {
        tiku_menu_enable(m, TIKU_TRK_CMD_CREATE_QUERY, 0);
        tiku_menu_enable(m, TIKU_TRK_CMD_CREATE_QUERY_TPL, 0);
    }
    return m;
}
