/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_newfolder_backend.c - New folder in the save panel and in a folder
 * window, on a folder a fake backend serves.
 *
 * The local files are mounted at "/".  One fake backend is mounted at
 * /devices/x, a second over a host directory, and a third, which cannot
 * make folders, at /devices/y.  A new folder must be made through the
 * backend that serves the path, no host directory may appear, and a
 * refusal must be shown.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
/* new_folder_button() and event() are static: the file is compiled in. */
#include "../../system/shared/ui/windows/tiku_trk_panel_window.c"
#include "tiku_mount.h"
#include "tiku_fs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int fails;

/** @brief One result line; a failure counts. */
static void
check(int cond, const char *what)
{
    printf("  [%s] %s\n", cond ? "PASS" : "FAIL", what);
    if (!cond) {
        fails++;
    }
}

/*---------------------------------------------------------------------------*/
/* THE FAKE BACKEND                                                          */
/*---------------------------------------------------------------------------*/

/** @brief A store holding /data and the folders made in it. */
typedef struct {
    tiku_backend_t base;
    char           made[8][TIKU_PATH_MAX];
    int            nmade;
} fake_t;

/** @brief Fill @p out as a writable folder called @p path. */
static void
folder_model(const char *path, tiku_model_t *out)
{
    const char *slash = strrchr(path, '/');

    memset(out, 0, sizeof *out);
    snprintf(out->name, sizeof out->name, "%s",
             (slash != NULL && slash[1] != '\0') ? slash + 1 : "/");
    snprintf(out->path, sizeof out->path, "%s", path);
    out->kind = TIKU_KIND_DIRECTORY;
    out->facts.perm = TIKU_P_READ | TIKU_P_WRITE | TIKU_P_EXEC;
}

static int
fake_stat(tiku_backend_t *b, const char *path, tiku_model_t *out)
{
    fake_t *f = b->impl;
    int i;

    if (strcmp(path, "/") == 0 || strcmp(path, "/data") == 0) {
        folder_model(path, out);
        return 0;
    }
    for (i = 0; i < f->nmade; i++) {
        if (strcmp(f->made[i], path) == 0) {
            folder_model(path, out);
            return 0;
        }
    }
    return -1;
}

static int
fake_list(tiku_backend_t *b, const char *path, tiku_entry_fn fn, void *ctx)
{
    fake_t *f = b->impl;
    tiku_model_t m;
    size_t n = strlen(path);
    int i, count = 0;

    if (strcmp(path, "/") == 0) {
        folder_model("/data", &m);
        m.backend = b;
        (void)fn(&m, ctx);
        return 1;
    }
    for (i = 0; i < f->nmade; i++) {
        if (strncmp(f->made[i], path, n) == 0 && f->made[i][n] == '/' &&
            strchr(f->made[i] + n + 1, '/') == NULL) {
            folder_model(f->made[i], &m);
            m.backend = b;
            (void)fn(&m, ctx);
            count++;
        }
    }
    return count;
}

static int
fake_mkdir(tiku_backend_t *b, const char *path, char *err, size_t errmax)
{
    fake_t *f = b->impl;

    (void)err;
    (void)errmax;
    if (f->nmade < 8) {
        snprintf(f->made[f->nmade++], sizeof f->made[0], "%s", path);
    }
    return 0;
}

static const tiku_backend_ops_t fake_ops = {
    .name = "fake", .stat = fake_stat, .list = fake_list,
    .mkdir = fake_mkdir
};

/* The same store with no way to make a folder. */
static const tiku_backend_ops_t fixed_ops = {
    .name = "fixed", .stat = fake_stat, .list = fake_list
};

static fake_t *
fake_new(const tiku_backend_ops_t *ops)
{
    fake_t *f = calloc(1, sizeof *f);

    if (f != NULL) {
        f->base.ops = ops;
        f->base.impl = f;
        snprintf(f->base.devid, sizeof f->base.devid, "fake");
    }
    return f;
}

/** @brief Whether @p f made the folder @p path. */
static int
made(const fake_t *f, const char *path)
{
    int i;

    for (i = 0; i < f->nmade; i++) {
        if (strcmp(f->made[i], path) == 0) {
            return 1;
        }
    }
    return 0;
}

/** @brief Whether a host directory @p path exists. */
static int
on_host(const char *path)
{
    struct stat st;

    return stat(path, &st) == 0;
}

/*---------------------------------------------------------------------------*/
/* THE CHECKS                                                                */
/*---------------------------------------------------------------------------*/

/** @brief A press and a release on the save panel's New folder button. */
static void
press_new_folder(tiku_trk_panel_window_t *panel)
{
    tiku_rect_t nf = new_folder_button(panel);
    tiku_event_t ev;

    memset(&ev, 0, sizeof ev);
    ev.x = nf.x + nf.w / 2;
    ev.y = nf.y + nf.h / 2;
    ev.button = 1u;
    ev.type = TIKU_EVENT_POINTER_DOWN;
    (void)event(panel->window, &ev, panel);
    ev.type = TIKU_EVENT_POINTER_UP;
    (void)event(panel->window, &ev, panel);
}

/** @brief The save panel on @p dir, offering the name @p name. */
static void
save_panel_on(tiku_trk_panel_window_t *panel, tiku_alert_t *alert,
              const char *dir, const char *name)
{
    memset(alert, 0, sizeof *alert);
    tiku_trk_panel_window_open_save(panel, panel, 0, dir, name);
}

int
main(void)
{
    char home[] = "/tmp/tiku-newfolder-XXXXXX";
    char shadow[64], shadow_data[80], host_new[128], cmd[96];
    char out[TIKU_PATH_MAX], err[200];
    static tiku_trk_panel_window_t panel;
    tiku_alert_t alert;
    tiku_backend_t *ns;
    tiku_surface_t *surf;
    tiku_workspace_t *ws;
    tiku_store_t *st;
    tiku_fs_t *fs;
    fake_t *dev, *over, *fixed;
    int dirty = 0, rc;

    if (mkdtemp(home) == NULL) {
        return 2;
    }
    (void)setenv("HOME", home, 1);
    snprintf(shadow, sizeof shadow, "%s/dev", home);
    snprintf(shadow_data, sizeof shadow_data, "%s/data", shadow);
    (void)mkdir(shadow, 0755);
    (void)mkdir(shadow_data, 0755);

    dev = fake_new(&fake_ops);
    over = fake_new(&fake_ops);
    fixed = fake_new(&fixed_ops);
    ns = tiku_mount_open();
    if (dev == NULL || over == NULL || fixed == NULL || ns == NULL ||
        tiku_mount_add(ns, "/", tiku_backend_posix_open(), 1) != 0 ||
        tiku_mount_add(ns, "/devices/x", &dev->base, 0) != 0 ||
        tiku_mount_add(ns, shadow, &over->base, 0) != 0 ||
        tiku_mount_add(ns, "/devices/y", &fixed->base, 0) != 0) {
        printf("  [FAIL] the namespace\n");
        return 1;
    }
    surf = tiku_surface_new(1000, 700, TIKU_C_BACKDROP);
    ws = tiku_workspace_new(surf);
    st = tiku_store_memory_open();
    fs = tiku_fs_new(ns);
    tiku_trk_panel_window_init(&panel, ws, ns, st, NULL, 18, 1000, 700,
                               &dirty, NULL, NULL);
    tiku_trk_panel_window_set_alert(&panel, &alert);

    printf("the save panel's New folder (D-029)\n");
    save_panel_on(&panel, &alert, "/devices/x/data", "Logs");
    press_new_folder(&panel);
    check(made(dev, "/data/Logs"), "on a board: made through its backend");
    check(!on_host("/devices/x/data/Logs") && !alert.open,
          "with no host directory and no alert");
    check(strcmp(panel.model.dir, "/devices/x/data/Logs") == 0,
          "and the panel goes into it");

    save_panel_on(&panel, &alert, shadow_data, "Logs");
    press_new_folder(&panel);
    snprintf(host_new, sizeof host_new, "%s/Logs", shadow_data);
    check(made(over, "/data/Logs") && !on_host(host_new),
          "over a host directory: made through the backend, not the host");

    save_panel_on(&panel, &alert, "/devices/y/data", "Logs");
    press_new_folder(&panel);
    printf("  (the alert says \"%s\")\n", alert.open ? alert.text : "");
    check(alert.open && strstr(alert.text, "could not be made") != NULL,
          "a backend that cannot make folders: the refusal is shown");
    tiku_trk_panel_window_close(&panel);

    printf("a folder window's New folder (D-029)\n");
    err[0] = '\0';
    rc = tiku_fs_new_folder(fs, "/devices/x/data", out, sizeof out, err,
                            sizeof err);
    check(rc == 0 && made(dev, "/data/New folder") &&
          strcmp(out, "/devices/x/data/New folder") == 0,
          "on a board: made through its backend");
    check(!on_host("/devices/x/data/New folder"), "with no host directory");

    err[0] = '\0';
    rc = tiku_fs_new_folder(fs, shadow_data, out, sizeof out, err,
                            sizeof err);
    snprintf(host_new, sizeof host_new, "%s/New folder", shadow_data);
    check(rc == 0 && made(over, "/data/New folder") && !on_host(host_new),
          "over a host directory: made through the backend, not the host");

    err[0] = '\0';
    rc = tiku_fs_new_folder(fs, "/devices/y/data", out, sizeof out, err,
                            sizeof err);
    printf("  (the error says \"%s\")\n", err);
    check(rc < 0 && err[0] != '\0',
          "a backend that cannot make folders: refused with a reason");

    err[0] = '\0';
    rc = tiku_fs_new_folder(fs, home, out, sizeof out, err, sizeof err);
    snprintf(host_new, sizeof host_new, "%s/New folder", home);
    check(rc == 0 && on_host(host_new),
          "on local files: the folder is made on the host");

    tiku_fs_free(fs);
    tiku_backend_close(ns);
    free(dev);
    free(over);
    free(fixed);
    snprintf(cmd, sizeof cmd, "rm -rf '%s'", home);
    if (system(cmd) != 0) {
        printf("  (could not remove %s)\n", home);
    }
    printf("%s\n", fails ? "FAILURES" : "all passed");
    return fails ? 1 : 0;
}
