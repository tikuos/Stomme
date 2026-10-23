/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_addon.c - the add-on table: registering, discovering, the menu
 * and shortcuts, and running an external add-on.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_trk_addon.h"
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

void
tiku_trk_addons_init(tiku_trk_addons_t *a)
{ if (a != NULL) memset(a, 0, sizeof *a); }

int
tiku_trk_addons_register(tiku_trk_addons_t *a, const char *name,
                         const char *command, const char *type, char key)
{
    tiku_trk_addon_t *x;
    if (a == NULL || name == NULL || name[0] == '\0' ||
        a->count >= TIKU_TRK_ADDON_MAX) return -1;
    x = &a->item[a->count++];
    memset(x, 0, sizeof *x);
    snprintf(x->name, sizeof x->name, "%s", name);
    snprintf(x->command, sizeof x->command, "%s", command != NULL ? command : "");
    snprintf(x->type, sizeof x->type, "%s", type != NULL ? type : "*");
    x->generic = strcmp(x->type, "*") == 0;
    x->key = key;
    x->mods = 0x3u; /* Command and Option, in the key handler's bits */
    a->generation++;
    return a->count - 1;
}

int
tiku_trk_addons_rebind(tiku_trk_addons_t *a, const char *settings)
{
    const char *p;
    int changed = 0;
    if (a == NULL || settings == NULL) return 0;
    for (p = settings; *p != '\0'; ) {
        const char *eq = strchr(p, '=');
        const char *nl = strchr(p, '\n');
        size_t n = eq != NULL ? (size_t)(eq - p) : 0;
        if (nl == NULL) nl = p + strlen(p);
        if (eq != NULL && eq < nl && n > 0 && eq + 1 < nl) {
            int i;
            for (i = 0; i < a->count; i++) {
                if (strlen(a->item[i].name) == n &&
                    strncmp(a->item[i].name, p, n) == 0) {
                    a->item[i].key = eq[1];
                    changed++;
                    break;
                }
            }
        }
        p = (*nl != '\0') ? nl + 1 : nl;
    }
    if (changed) a->generation++;
    return changed;
}

/** @brief Whether @p type is one of the comma-separated types in @p list. */
static int
listed(const char *list, const char *type)
{
    size_t n = strlen(type);

    while (*list != '\0') {
        const char *comma = strchr(list, ',');
        size_t len = (comma != NULL) ? (size_t)(comma - list) : strlen(list);

        if (len == n && strncmp(list, type, n) == 0) {
            return 1;
        }
        list += len;
        if (*list == ',') {
            list++;
        }
    }
    return 0;
}

int
tiku_trk_addons_accepts(const tiku_trk_addon_t *a, const char *const *types,
                        int ntypes)
{
    int i;
    if (a == NULL) return 0;
    if (a->generic) return 1;
    for (i = 0; i < ntypes; i++) {
        if (types[i] != NULL && listed(a->type, types[i])) return 1;
    }
    return 0;
}

/**
 * @brief Whether @p a sorts before @p b for a menu: accepting the types
 *        first, specific before generic, then by name.
 */
static int
before(const tiku_trk_addon_t *a, const tiku_trk_addon_t *b,
       const char *const *types, int ntypes)
{
    int aa = tiku_trk_addons_accepts(a, types, ntypes);
    int ab = tiku_trk_addons_accepts(b, types, ntypes);
    if (aa && ab && a->generic != b->generic) return !a->generic;
    if (aa != ab) return aa > ab;
    return strcmp(a->name, b->name) < 0;
}

/** @brief @p mods, in the key handler's bits (0x1 Option, 0x2 Command), as
 *         TIKU_MENU_MOD_* bits. */
static unsigned
menu_mods(unsigned mods)
{
    unsigned m = 0u;

    if (mods & 0x1u) m |= TIKU_MENU_MOD_OPTION;
    if (mods & 0x2u) m |= TIKU_MENU_MOD_CMD;
    return m;
}

int
tiku_trk_addons_build_menu(const tiku_trk_addons_t *a, const char *const *types,
                           int ntypes, tiku_menu_t *menu, int base)
{
    int order[TIKU_TRK_ADDON_MAX], i, j;
    if (a == NULL || menu == NULL) return 0;
    tiku_menu_clear(menu);
    for (i = 0; i < a->count; i++) order[i] = i;
    for (i = 1; i < a->count; i++) {
        int hold = order[i];
        for (j = i; j > 0 && before(&a->item[hold], &a->item[order[j-1]], types, ntypes); j--)
            order[j] = order[j-1];
        order[j] = hold;
    }
    for (i = 0; i < a->count && menu->count < TIKU_MENU_ITEMS_MAX; i++) {
        const tiku_trk_addon_t *x = &a->item[order[i]];
        (void)tiku_menu_add(menu, x->name, base + order[i], x->key,
                                menu_mods(x->mods), 1);
    }
    return menu->count;
}

int
tiku_trk_addons_invoke(const tiku_trk_addons_t *a, int command, int base,
                       tiku_trk_addon_invoke_fn fn, void *context)
{
    int index = command - base;

    if (a == NULL || fn == NULL || index < 0 || index >= a->count) {
        return 0;
    }
    return fn(&a->item[index], context) != 0;
}

int
tiku_trk_addons_shortcut(const tiku_trk_addons_t *a, char key, unsigned mods,
                         int base)
{
    int i;

    if (a == NULL || key == 0) return -1;
    for (i = 0; i < a->count; i++) {
        if (a->item[i].key == key && a->item[i].mods == mods) {
            return base + i;
        }
    }
    return -1;
}


int
tiku_trk_addons_discover(tiku_trk_addons_t *addons, const char *dir)
{
    DIR *d;
    struct dirent *e;
    int i, found = 0;

    if (addons == NULL || dir == NULL) {
        return 0;
    }
    /* What an earlier discovery found is replaced, so an add-on removed
     * from the directory leaves the menu (AW-023). */
    for (i = 0; i < addons->count; i++) {
        if (addons->item[i].external) {
            addons->item[i] = addons->item[--addons->count];
            i--;
        }
    }
    addons->generation++;
    d = opendir(dir);
    if (d == NULL) {
        return 0;
    }
    while ((e = readdir(d)) != NULL &&
           addons->count < TIKU_TRK_ADDON_MAX) {
        char path[256], types_path[280], type[TIKU_TYPE_MAX] = "";
        char keyline[8] = "";
        struct stat st;
        FILE *tf;

        if (e->d_name[0] == '.' || strstr(e->d_name, ".types") != NULL) {
            continue;
        }
        if (snprintf(path, sizeof path, "%s/%s", dir, e->d_name) < 0) {
            continue;
        }
        if (stat(path, &st) != 0 || !S_ISREG(st.st_mode) ||
            access(path, X_OK) != 0) {
            continue;
        }
        snprintf(types_path, sizeof types_path, "%s.types", path);
        tf = fopen(types_path, "r");
        if (tf != NULL) {
            if (fgets(type, sizeof type, tf) != NULL) {
                type[strcspn(type, "\n")] = '\0';
            }
            /* The second line is the add-on's own default key, the
             * declaration Haiku kept in a resource (AW-022).  Exactly one
             * character, as the source requires. */
            if (fgets(keyline, sizeof keyline, tf) != NULL) {
                keyline[strcspn(keyline, "\n")] = '\0';
                if (strlen(keyline) != 1u) {
                    keyline[0] = '\0';
                }
            }
            (void)fclose(tf);
        }
        {
            tiku_trk_addon_t *a = &addons->item[addons->count++];

            memset(a, 0, sizeof *a);
            snprintf(a->name, sizeof a->name, "%s", e->d_name);
            snprintf(a->command, sizeof a->command, "%s", path);
            snprintf(a->type, sizeof a->type, "%s", type);
            a->generic = (type[0] == '\0');
            a->key = keyline[0];
            a->mods = 0x3u; /* Command and Option, as registered ones */
            a->external = 1;
            found++;
        }
    }
    (void)closedir(d);
    return found;
}

int
tiku_trk_addons_run_external(const tiku_trk_addon_t *addon,
                             const char *const *paths, int n)
{
    const char *argv[66];
    int argc = 0, i;
    pid_t pid;
    int status;

    if (addon == NULL || !addon->external || addon->command[0] == '\0') {
        return -1;
    }
    argv[argc++] = addon->command;
    for (i = 0; i < n && argc < (int)(sizeof argv / sizeof argv[0]) - 1;
         i++) {
        if (paths[i] != NULL && paths[i][0] != '\0') {
            argv[argc++] = paths[i];
        }
    }
    argv[argc] = NULL;
    pid = fork();
    if (pid < 0) {
        return -1;
    }
    if (pid == 0) {
        pid_t worker = fork();
        if (worker < 0) _exit(127);
        if (worker > 0) _exit(0);
        execvp(argv[0], (char *const *)(void *)argv);
        _exit(127);
    }
    /* Only the launch helper is waited for; the add-on runs independently. */
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) return -1;
    }
    return WIFEXITED(status) && WEXITSTATUS(status) == 0 ? 0 : -1;
}
