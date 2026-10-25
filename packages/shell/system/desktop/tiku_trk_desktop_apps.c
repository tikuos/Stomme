/*
 * Desktop shell and file manager.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_desktop_apps.c - the installed applications: where they are
 * found, what they are called and drawn as, and how one is started; the help
 * folder and its pages; and the Move to, Copy to and Create link pickers.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_shell_product.h"
#include "tiku_icons.h"
#include "tiku_manifest.h"
#include "tiku_trk_desktop_private.h"
#include <limits.h>
#include "tiku_trk_desktop_appgroups.inl"

/**
 * @brief Add one destination row to picker @p k (CW-035); Move to (@p k 0)
 *        takes it only when @p move_dev_ok.
 */
static void
picker_add(desktop_t *app, int k, const char *label, const char *path,
           int move_dev_ok)
{
    tiku_trk_cwmenu_t *cw = &app->menus;
    int n = cw->picker_paths[k];
    int i;

    if (path == NULL || path[0] == '\0' || n >= TIKU_TRK_CMD_DEST_MAX ||
        (k == 0 && !move_dev_ok)) {
        return;
    }
    for (i = 0; i < n; i++) {
        if (strcmp(cw->picker_path[k][i], path) == 0) {
            return;             /* one row per destination */
        }
    }
    if (tiku_menu_add(&cw->picker[k], label,
            TIKU_TRK_CMD_DEST_BASE + k * TIKU_TRK_CMD_DEST_MAX + n,
            0, 0u, 1) >= 0) {
        snprintf(cw->picker_path[k][n], TIKU_PATH_MAX, "%s", path);
        cw->picker_paths[k] = n + 1;
    }
}

/**
 * @brief Build the Move to, Copy to and Create link pickers for folder
 *        @p fl (CW-035, CW-058).
 *
 * The rows: the current folder, recent folders, Desktop, Home, then the
 * volumes.  Move to offers only destinations on the folder's own device,
 * since a move across devices is a copy.
 */
void
build_pickers(desktop_t *app, folder_t *fl)
{
    static const char *title[3] = { "Move to", "Copy to", "Create link" };
    const char *home = getenv("HOME");
    char recent[8][TIKU_PATH_MAX];
    char desk[TIKU_PATH_MAX];
    struct stat here;
    tiku_volumes_t vs;
    int have_here;
    int nrecent;
    int nvol;
    int k, i;

    if (fl == NULL) {
        return;
    }
    have_here = stat(fl->path, &here) == 0;
    nrecent = tiku_trk_recent_list(app->store, TIKU_TRK_RECENT_FOLDERS, 6,
                                   recent, 8);
    nvol = (tiku_volumes_scan(&vs) > 0) ? vs.n : 0;
    (void)tiku_desktop_dir(desk, sizeof desk);
    for (k = 0; k < 3; k++) {
        tiku_menu_clear(&app->menus.picker[k]);
        snprintf(app->menus.picker[k].title,
                 sizeof app->menus.picker[k].title, "%s", title[k]);
        app->menus.picker_paths[k] = 0;
        picker_add(app, k, "Current folder", fl->path, 1);
        for (i = 0; i < nrecent; i++) {
            struct stat rst;
            const char *leaf = strrchr(recent[i], '/');
            const char *rows[8];
            char label[TIKU_PATH_MAX];
            int r;

            if (stat(recent[i], &rst) != 0) {
                continue;       /* a destination that went away */
            }
            /* A recent whose leaf name another recent shares is labelled
             * with its leaf and the shortest trailing path that tells it
             * apart (NeXT UIG ch8). */
            for (r = 0; r < nrecent; r++) {
                rows[r] = recent[r];
            }
            (void)tiku_attr_disambiguate(recent[i], rows, nrecent,
                                             label, sizeof label);
            if (strcmp(recent[i], desk) == 0 ||
                (home != NULL && strcmp(recent[i], home) == 0)) {
                /* Desktop and Home have their own named rows below. */
                continue;
            }
            (void)leaf;
            picker_add(app, k, label, recent[i],
                       have_here && rst.st_dev == here.st_dev);
        }
        {
            struct stat dst;

            if (stat(desk, &dst) == 0) {
                picker_add(app, k, "Desktop", desk,
                           have_here && dst.st_dev == here.st_dev);
            }
            if (home != NULL && stat(home, &dst) == 0) {
                picker_add(app, k, "Home", home,
                           have_here && dst.st_dev == here.st_dev);
            }
        }
        for (i = 0; i < nvol; i++) {
            struct stat vst;

            if (!tiku_volume_shows_icon(&vs, &vs.v[i]) ||
                stat(vs.v[i].mount, &vst) != 0) {
                continue;
            }
            picker_add(app, k, vs.v[i].name, vs.v[i].mount,
                       have_here && vst.st_dev == here.st_dev);
        }
        if (k < 2 && app->menus.picker[k].count > 0) {
            /* Choose folder... opens the file panel to pick any folder
             * (AW-050). */
            (void)tiku_menu_add_separator(&app->menus.picker[k]);
            (void)tiku_menu_add(&app->menus.picker[k],
                "Choose folder\xe2\x80\xa6",
                (k == 0) ? TIKU_TRK_CMD_MOVE_TO : TIKU_TRK_CMD_COPY_TO,
                0, 0u, 1);
        }
    }
}

/**
 * @brief Find companion program @p name, shipped with the desktop.
 *
 * Looked for where the build and an install put it, relative to this
 * binary, so a tree that was moved or built elsewhere finds its programs.
 *
 * @return nonzero when @p out names an executable program.
 */
static int
find_companion(const char *argv0, const char *name, char *out, size_t max)
{
    static const char *const where[] = {
        "%s/%s",                        /* beside the desktop's binary   */
        "%s/build/runtime/system/%s",
        "%s/system/%s",
        "%s/build/%s",                  /* the tree's own build          */
        "%s/libexec/tiku/%s"
    };
    char dir[TIKU_PATH_MAX];
    char *slash;
    size_t i;
    int up;

    /* TIKU_APPS names where applications are installed; companion
     * programs are looked for from this binary, not there. */
    snprintf(dir, sizeof dir, "%s", (argv0 != NULL) ? argv0 : "");
    slash = strrchr(dir, '/');
    if (slash != NULL) {
        *slash = '\0';
    } else {
        snprintf(dir, sizeof dir, ".");
    }
    /* Climb up to four levels, since a development build sits deeper
     * than an installed one.  The climb appends "/..", which keeps a
     * relative argv[0] working. */
    for (up = 0; up < 5; up++) {
        char step[TIKU_PATH_MAX];
        int k;

        snprintf(step, sizeof step, "%s", dir);
        for (k = 0; k < up; k++) {
            size_t used = strlen(step);

            if (used + 4u >= sizeof step) {
                break;
            }
            snprintf(step + used, sizeof step - used, "/..");
        }
        for (i = 0; i < sizeof where / sizeof where[0]; i++) {
            char found[TIKU_PATH_MAX];
            char real[PATH_MAX];

            snprintf(found, sizeof found, where[i], step, name);
            if (access(found, X_OK) != 0) {
                continue;
            }
            /* Made absolute: a launched program starts in the user's
             * home, where a relative path names nothing.  realpath() gets
             * a PATH_MAX buffer, since a fortified libc aborts on a
             * smaller one. */
            if (realpath(found, real) != NULL && strlen(real) < max) {
                memcpy(out, real, strlen(real) + 1);
                return 1;
            }
        }
    }
    out[0] = '\0';
    return 0;
}

/**
 * @brief @p path made absolute into @p out, as a launched program changes
 *        its working directory.
 * @return nonzero when it resolved and fits.
 */
static int canonical_directory(const char *path, char *out, size_t max)
{
    char *resolved = realpath(path, NULL);
    int ok = resolved && strlen(resolved) < max;
    if (ok) snprintf(out, max, "%s", resolved);
    else if (max) out[0] = 0;
    free(resolved);
    return ok;
}

/**
 * @brief Find the installed applications' directory: TIKU_APPS when set,
 *        else apps/ or build/runtime/apps, climbing from this binary as
 *        find_companion() does.
 * @return nonzero when found.
 */
static int
find_apps_dir(const char *argv0, char *out, size_t max)
{
    char dir[TIKU_PATH_MAX];
    char *slash;
    int up;

    if (getenv("TIKU_APPS") != NULL) {
        return canonical_directory(getenv("TIKU_APPS"), out, max) &&
               access(out, X_OK) == 0;
    }
    snprintf(dir, sizeof dir, "%s", (argv0 != NULL) ? argv0 : "");
    slash = strrchr(dir, '/');
    if (slash != NULL) {
        *slash = '\0';
    } else {
        snprintf(dir, sizeof dir, ".");
    }
    for (up = 0; up < 5; up++) {
        char step[TIKU_PATH_MAX];
        char found[TIKU_PATH_MAX];
        int k;

        snprintf(step, sizeof step, "%s", dir);
        for (k = 0; k < up; k++) {
            size_t used = strlen(step);

            if (used + 4u >= sizeof step) {
                break;
            }
            snprintf(step + used, sizeof step - used, "/..");
        }
        const char *layouts[] = {"%s/apps", "%s/build/runtime/apps"};
        for (size_t i = 0; i < sizeof layouts / sizeof layouts[0]; i++) {
            if (snprintf(found, sizeof found, layouts[i], step) >=
                (int)sizeof found) continue;
            if (access(found, X_OK) == 0 &&
                canonical_directory(found, out, max)) return 1;
        }
    }
    out[0] = '\0';
    return 0;
}

/** @brief The label an installed file is offered under. */
static void
app_label(const char *file, char *out, size_t max)
{
    size_t i;

    /* Made from the file name, without loading the file: "tiku_" and
     * ".so" dropped, underscores as spaces, the first letter capital. */
    if (strncmp(file, "tiku_", 5) == 0) {
        file += 5;
    }
    snprintf(out, max, "%s", file);
    for (i = 0; out[i] != '\0'; i++) {
        if (out[i] == '_') {
            out[i] = ' ';
        }
    }
    i = strlen(out);
    if (i > 3u && strcmp(out + i - 3, ".so") == 0) {
        out[i - 3] = '\0';
    }
    if (out[0] >= 'a' && out[0] <= 'z') {
        out[0] = (char)(out[0] - 'a' + 'A');
    }
}

/**
 * @brief The icon drawn on a program's row, guessed from its file name
 *        without loading it: the text file icon for a name with "edit",
 *        else the application icon.
 */
static const char *
app_icon(const char *file)
{
    if (strstr(file, "edit") != NULL) {
        return "text_file";
    }
    return "application";
}

/** @brief Whether this desktop found a help folder at all. */
int
desktop_help_ready(desktop_t *app)
{
    return app != NULL && app->help_root[0] != 0;
}

/**
 * @brief The help page for @p window: the page its application named (an
 *        embedded slot's or a session's), or the fixed page of one of the
 *        desktop's own windows.  Nothing is read from disk.
 * @return the page, or NULL when there is none.
 */
const char *
desktop_help_page_of(desktop_t *app, tiku_window_t *window)
{
    struct embed_win *ew;
    tiku_remote_session_t *session;

    if (app == NULL || window == NULL) return NULL;
    ew = embed_win_of_window(app, window);
    if (ew != NULL && ew->slot != NULL && ew->slot->help_page[0] != '\0') {
        return ew->slot->help_page;
    }
    session = tiku_remote_owner(&app->remote, window);
    if (session != NULL && session->help_page[0] != '\0') {
        return session->help_page;
    }
    if (window == app->fonts_window.window) return "settings.html#fonts";
    if (window == app->prefs_window.window) return "settings.html#tracker";
    if (window == app->deskbar_prefs_window.window)
        return "settings.html#deskbar";
    if (window == app->desktop_prefs_window.window)
        return "settings.html#desktop";
    if (window == app->sounds_window.window) return "settings.html#sounds";
    if (window == app->find_window.window ||
        window == app->select_window.window) return "finding.html";
#if TIKU_SHELL_BOARD_SERVICES

    if (!strcmp(window->title, "Devices Preferences"))
        return "settings.html#devices";
#endif

    if (!strcmp(window->title, "Startup Preferences"))
        return "settings.html#startup";
    if (!strcmp(window->title, "File Associations"))
        return "settings.html#associations";
    if (window == desktop_forcequit_handle(app))
        return "deskbar.html#forcequit";
    return NULL;
}

/**
 * @brief Find the help folder, a folder holding an index.html: TIKU_HELP
 *        when set, else climbing from this binary as find_companion() does.
 * @return nonzero when @p out names it.
 */
int
find_help_dir(const char *argv0, char *out, size_t max)
{
    static const char *const where[] = {
#if TIKU_SHELL_BOARD_SERVICES

        "%s/assets/help", "%s/help", "%s/TikuTracker/help",
        TIKU_SHELL_INSTALLED_HELP_PATH
    }
#else

        "%s/assets/help", "%s/help",
        TIKU_SHELL_INSTALLED_HELP_PATH
    }
#endif
;
    char dir[TIKU_PATH_MAX];
    char *slash;
    int up;

    if (getenv("TIKU_HELP") != NULL) {
        char page[TIKU_PATH_MAX];

        if (!canonical_directory(getenv("TIKU_HELP"), out, max)) return 0;
        /* TIKU_HELP too must hold an index.html. */
        if (snprintf(page, sizeof page, "%s/index.html", out) <
            (int)sizeof page && access(page, R_OK) == 0) {
            return 1;
        }
        out[0] = '\0';
        return 0;
    }
    snprintf(dir, sizeof dir, "%s", (argv0 != NULL) ? argv0 : "");
    slash = strrchr(dir, '/');
    if (slash != NULL) {
        *slash = '\0';
    } else {
        snprintf(dir, sizeof dir, ".");
    }
    for (up = 0; up < 5; up++) {
        char step[TIKU_PATH_MAX];
        size_t i;
        int k;

        snprintf(step, sizeof step, "%s", dir);
        for (k = 0; k < up; k++) {
            size_t used = strlen(step);

            if (used + 4u >= sizeof step) {
                break;
            }
            snprintf(step + used, sizeof step - used, "/..");
        }
        for (i = 0; i < sizeof where / sizeof where[0]; i++) {
            char found[TIKU_PATH_MAX];
            char page[TIKU_PATH_MAX];

            if (snprintf(found, sizeof found, where[i], step) >=
                (int)sizeof found) {
                continue;
            }
            if (snprintf(page, sizeof page, "%s/index.html", found) >=
                (int)sizeof page) {
                continue;
            }
            if (access(page, R_OK) == 0) {
                return canonical_directory(found, out, max);
            }
        }
    }
    out[0] = '\0';
    return 0;
}

/* The leaf menu's table holds at least as many programs as the launch
 * registry. */
_Static_assert(TIKU_TRK_DB_APPS_MAX >= TIKU_TRK_APP_MAX,
               "the Deskbar's program table is smaller than the registry");

/**
 * @brief Offer the application bundle manifest @p m describes, without
 *        loading its module: to the launch registry, the startup list and
 *        its category's Deskbar group.  A known id or module is skipped.
 */
static void offer_bundle(desktop_t *app, const char *dir,
                         const char *runner, const tiku_manifest_t *m)
{
    char module[TIKU_PATH_MAX], id[96];
    for (int i = 0; i < app->apps.count; i++)
        if (!strcmp(app->apps.app[i].id, m->id)) return;
    if (snprintf(module, sizeof module, "%s/%s", dir, m->module) >=
        (int)sizeof module || access(module, R_OK) != 0) return;
    for (int i = 0; i < app->apps.count; i++)
        if (!strcmp(app->apps.app[i].module, module)) return;
    int at = tiku_trk_launch_add_document(&app->apps, m->name, runner,
                                          module, m->types);
    if (at < 0) return;
    snprintf(id, sizeof id, "app/%s", m->id);
    desktop_startup_offer_id(app, module, m->name, id, m->startup);
    (void)tiku_trk_deskbar_add_app_in(app->deskbar,
        installed_group(m->category), m->name, module, m->icon);
    if (at >= 0) {
        snprintf(app->apps.app[at].id, sizeof app->apps.app[at].id,
                 "%s", m->id);
        snprintf(app->apps.app[at].category, sizeof app->apps.app[at].category,
                 "%s", m->category);
        snprintf(app->apps.app[at].startup, sizeof app->apps.app[at].startup,
                 "%s", m->startup);
        snprintf(app->apps.app[at].help, sizeof app->apps.app[at].help, "%s",
                 m->help);
        snprintf(app->apps.app[at].icon, sizeof app->apps.app[at].icon,
                 "%s", m->icon);
    }
}

/**
 * @brief Offer every application in @p root, in name order: a folder with
 *        an app.manifest, or, when @p legacy is given, a flat .so module
 *        (by its .manifest, or named from its file).  Each runs in a
 *        process of its own.
 */
static void offer_directory(desktop_t *app, const char *root,
                            const char *runner, const char *legacy)
{
    struct dirent **entries = NULL;
    int count = scandir(root, &entries, NULL, alphasort);
    for (int i = 0; i < count; i++) {
        char path[TIKU_PATH_MAX], manifest[TIKU_PATH_MAX];
        char label[TIKU_TRK_DB_LABEL_MAX];
        tiku_manifest_t m;
        const char *name = entries[i]->d_name;
        size_t n = strlen(name);
        if (name[0] != '.' &&
            snprintf(path, sizeof path, "%s/%s", root, name) <
            (int)sizeof path) {
            if (snprintf(manifest, sizeof manifest, "%s/app.manifest",
                         path) < (int)sizeof manifest &&
                tiku_manifest_read(manifest, &m, NULL, 0) == 0) {
                offer_bundle(app, path, runner, &m);
            } else if (legacy && n > 3 && !strcmp(name + n - 3, ".so")) {
                if (snprintf(manifest, sizeof manifest, "%s.manifest", path) <
                    (int)sizeof manifest &&
                    tiku_manifest_read(manifest, &m, NULL, 0) == 0 &&
                    !strcmp(m.module, name)) {
                    offer_bundle(app, root, runner, &m);
                    free(entries[i]);
                    continue;
                }
                app_label(name, label, sizeof label);
                desktop_startup_offer(app, path, label, legacy);
                (void)tiku_trk_deskbar_add_app_in(app->deskbar,
                    installed_group(legacy), label, path, app_icon(name));
            }
        }
        free(entries[i]);
    }
    free(entries);
}

/**
 * @brief Offer every installed application: the apps directory and its
 *        programming/ and trusted/ subdirectories.
 *
 * Each runs in a process of its own through the runner, tiku-run; with no
 * runner found, nothing is offered.
 */
void
offer_installed(desktop_t *app, const char *argv0)
{
    char root[TIKU_PATH_MAX];
    char runner[TIKU_PATH_MAX];
    char subdir[TIKU_PATH_MAX];
    int have_runner;

    if (!find_apps_dir(argv0, root, sizeof root)) {
        return;
    }
    have_runner = find_companion(argv0, "tiku-run", runner, sizeof runner);
    if (have_runner) {
        snprintf(app->runner, sizeof app->runner, "%s", runner);
    }

    if (!have_runner) return;
    offer_directory(app, root, runner, "apps");
    if (snprintf(subdir, sizeof subdir, "%s/programming", root) <
        (int)sizeof subdir)
        offer_directory(app, subdir, runner, "programming");
    if (snprintf(subdir, sizeof subdir, "%s/trusted", root) <
        (int)sizeof subdir)
        offer_directory(app, subdir, runner, "trusted");
}

/**
 * @brief The installed application whose module, command or path @p path
 *        is, through a link if it is one; NULL for anything else and for
 *        TikuTracker itself.
 */
const tiku_trk_app_t *
desktop_app_at(desktop_t *app, const char *path)
{
    char real[PATH_MAX];
    const char *look = path;

    if (path == NULL || path[0] == '\0') {
        return NULL;
    }
    if (realpath(path, real) != NULL) {
        look = real;
    }
    for (int i = 0; i < app->apps.count; i++) {
        const tiku_trk_app_t *a = &app->apps.app[i];

        if (a->is_tracker) {
            continue;
        }
        if ((a->module[0] != '\0' && (strcmp(a->module, look) == 0 ||
                                      strcmp(a->module, path) == 0)) ||
            (a->cmd[0] != '\0' && strcmp(a->cmd, look) == 0) ||
            (a->path[0] != '\0' && strcmp(a->path, look) == 0)) {
            return a;
        }
    }
    return NULL;
}

/**
 * @brief The art drawn for a pose that is an application: its own icon,
 *        or the application icon when its own is unknown; NULL for anything
 *        else.
 */
const char *
desktop_app_art(desktop_t *app, const char *path)
{
    const tiku_trk_app_t *a = desktop_app_at(app, path);

    if (a == NULL) {
        return NULL;
    }
    return (a->icon[0] != '\0' && tiku_icons_find(a->icon) != NULL)
               ? a->icon : "application";
}

/**
 * @brief Start the installed application @p id, on @p document if any.
 * @return 1 when it started; 0, with a note on the status line, when it is
 *         not installed.
 */
int desktop_launch_app(desktop_t *app, const char *id, const char *document)
{
    for (int i = 0; i < app->apps.count; i++) {
        const tiku_trk_app_t *a = &app->apps.app[i];
        if (strcmp(a->id, id)) continue;
        const char *paths[] = {document};
        return tiku_trk_launch_run(a, document ? paths : NULL,
            document ? 1 : 0) == TIKU_TRK_LAUNCH_OK;
    }
    snprintf(app->status, sizeof app->status,
             "Application '%s' is not installed.", id);
    desktop_damage_all(app);
    return 0;
}
#if TIKU_SHELL_BOARD_SERVICES


/**
 * @brief Open TikuBench on its Firmware tab for board @p board, by a
 *        launch message.
 * @return 1 when TikuBench was launched with it.
 */
int desktop_bench_firmware(desktop_t *app, const char *board)
{
    tiku_msg_t *m = tiku_msg_new(1);
    int result = 0;
    if (!m) return 0;
    int ok = tiku_msg_add_string(m, "tab", "firmware") &&
             tiku_msg_add_string(m, "board", board ? board : "");
    for (int i = 0; ok && i < app->apps.count; i++) {
        const tiku_trk_app_t *a = &app->apps.app[i];
        if (!strcmp(a->id, "bench")) {
            result = tiku_trk_launch_message(a, m) == TIKU_TRK_LAUNCH_OK;
            break;
        }
    }
    tiku_msg_free(m);
    desktop_damage_all(app);
    return result;
}
#endif


/**
 * @brief Send TikuReader the help root and a page, or a title and generated
 *        HTML, as a launch message.
 * @return 1 when the reader was launched; else 0, with a note on the
 *         status line.
 */
static int desktop_help_request(desktop_t *app, const char *page,
                                const char *title, const char *html)
{
    tiku_msg_t *m = tiku_msg_new(1);
    int result = 0;
    if (!m) return 0;
    int ok = tiku_msg_add_string(m, "root", app->help_root);
    if (page) ok &= tiku_msg_add_string(m, "page", page);
    if (title) ok &= tiku_msg_add_string(m, "title", title);
    if (html) ok &= tiku_msg_add_string(m, "html", html);
    for (int i = 0; ok && i < app->apps.count; i++) {
        const tiku_trk_app_t *a = &app->apps.app[i];
        if (!strcmp(a->id, "reader")) {
            result = tiku_trk_launch_message(a, m) == TIKU_TRK_LAUNCH_OK;
            break;
        }
    }
    tiku_msg_free(m);
    if (!result)
        snprintf(app->status, sizeof app->status,
                 "Help needs the installed TikuReader application.");
    desktop_damage_all(app);
    return result;
}

/**
 * @brief Open help page @p page, the index when NULL, in TikuReader.
 * @return 1 when the reader was launched; 0 otherwise, as for a page name
 *         of 256 bytes or more.
 */
int desktop_help_open(desktop_t *app, const char *page)
{
    if (page && strlen(page) >= 256) return 0;
    return desktop_help_request(app, page ? page : "index.html", NULL, NULL);
}

/**
 * @brief Show generated HTML under @p title in TikuReader, as help pages
 *        are shown.
 * @return 1 when the reader was launched; 0 otherwise, as for HTML over
 *         15000 bytes or a title over 127.
 */
int desktop_help_html(desktop_t *app, const char *title, const char *html)
{
    if (!html || strlen(html) > 15000 || (title && strlen(title) > 127))
        return 0;
    return desktop_help_request(app, NULL, title, html);
}
