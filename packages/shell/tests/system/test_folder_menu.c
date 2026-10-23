/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_folder_menu.c - the folder menus' controller: the per-type
 * submenus of the Attributes menu.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "tiku_trk_folder_menu.h"

static int fails;

static void
check(int cond, const char *row, const char *what)
{
    printf("  [%s] %-9s %s\n", cond ? "PASS" : "FAIL", row, what);
    if (!cond) { fails++; }
}

/** @brief Create the empty file @p name in @p dir.  @return 0, or -1. */
static int
touch(const char *dir, const char *name, char *path, size_t max)
{
    FILE *f;

    snprintf(path, max, "%s/%s", dir, name);
    if ((f = fopen(path, "w")) == NULL) {
        return -1;
    }
    return fclose(f);
}

int
main(void)
{
    static tiku_trk_cwmenu_t menus;
    static tiku_trk_folder_window_t folder;
    static tiku_trk_types_t types;
    tiku_trk_folder_menu_t controller;
    tiku_backend_t *b = tiku_backend_posix_open();
    char dir[] = "/tmp/trk_foldermenu_XXXXXX";
    char unknown[TIKU_PATH_MAX], picture[TIKU_PATH_MAX];
    const char *present[4];
    int n;

    if (b == NULL || mkdtemp(dir) == NULL ||
        touch(dir, "a.zzz", unknown, sizeof unknown) != 0 ||
        touch(dir, "b.png", picture, sizeof picture) != 0) {
        return 1;
    }
    tiku_trk_types_init(&types);
    (void)tiku_trk_types_add(&types, "image/png", "PNG image", "image",
                             "size");
    folder.view = tiku_trk_poseview_new(b, dir);
    snprintf(folder.path, sizeof folder.path, "%s", dir);
    tiku_trk_folder_menu_init(&controller, &menus, NULL, b, NULL, &types,
                              NULL, NULL, NULL);

    printf("per-type submenus after an unregistered type (D-022)\n");
    n = tiku_trk_poseview_types_present(folder.view, present, 4);
    check(folder.view != NULL && n == 2 &&
          strcmp(present[0], "image/png") != 0 &&
          strcmp(present[1], "image/png") == 0, "D-022",
          "the folder lists a type with no record before image/png");

    tiku_trk_folder_menu_build_per_type(&controller, &folder);
    check(menus.per_type_count == 1 &&
          strcmp(menus.per_type_name[0], "PNG image") == 0, "D-022",
          "one submenu is offered, named for the registered type");
    check(menus.per_type[0].count == 1 &&
          strcmp(menus.per_type[0].item[0].label, "Size") == 0, "D-022",
          "the submenu beside that name holds the type's columns");
    check(strcmp(menus.per_type[0].title, menus.per_type_name[0]) == 0 &&
          strcmp(menus.per_type_icon[0], "image") == 0, "D-022",
          "and its title and icon are the type's");

    tiku_trk_poseview_free(folder.view);
    tiku_backend_close(b);
    (void)unlink(unknown);
    (void)unlink(picture);
    (void)rmdir(dir);
    printf("\n%s\n", fails ? "FAILURES" : "folder menus hold");
    return fails ? 1 : 0;
}
