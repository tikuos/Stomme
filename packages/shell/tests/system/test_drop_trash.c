/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_drop_trash.c - a volume dropped on the Trash with a file: the
 * volume is taken out of the drop and the file still goes to the Trash.
 * The volume is a folder named as a boot volume in an installed volume
 * table, so it is refused and nothing is unmounted.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "tiku_trk_drop_controller.h"
#include "tiku_volume.h"

static int fails;

static void
check(int cond, const char *row, const char *what)
{
    printf("  [%s] %-7s %s\n", cond ? "PASS" : "FAIL", row, what);
    if (!cond) {
        fails++;
    }
}

/** @brief Whether @p path exists. */
static int
there(const char *path)
{
    struct stat st;

    return stat(path, &st) == 0;
}

/** @brief A file holding one line at @p path. */
static void
put(const char *path)
{
    FILE *f = fopen(path, "w");

    if (f != NULL) {
        fputs("x\n", f);
        (void)fclose(f);
    }
}

/** @brief Step @p fs until its job ends. */
static void
run_to_end(tiku_fs_t *fs)
{
    int guard = 0;

    while (tiku_fs_busy(fs) && guard++ < 1000) {
        if (tiku_fs_step(fs, 4) == 0) {
            break;
        }
    }
}

/** @brief One drop on the Trash of @p first and @p second, in that order.
 *         @return how many items the job skipped. */
static int
drop_two(tiku_fs_t *fs, tiku_backend_t *b, tiku_alert_t *alert,
         const char *trash, const char *first, const char *second)
{
    static tiku_trk_drag_t drag;
    static tiku_trk_drop_controller_t c;
    tiku_trk_folder_window_t *folder = NULL;
    tiku_menu_t *popup = NULL, *sub = NULL;
    tiku_rect_t frame = { 0, 0, 0, 0 };
    char progress[TIKU_PATH_MAX];
    int row = -1;

    memset(&drag, 0, sizeof drag);
    memset(alert, 0, sizeof *alert);
    drag.count = 2;
    snprintf(drag.path[0], sizeof drag.path[0], "%s", first);
    snprintf(drag.path[1], sizeof drag.path[1], "%s", second);
    tiku_trk_drop_controller_init(&c, NULL, b, fs, NULL, NULL, alert,
                                  &drag, &folder, &row, &popup, &sub,
                                  &frame, progress, sizeof progress, NULL,
                                  800, 600, NULL, NULL);
    tiku_trk_drop_controller_execute_into(&c, trash, 0, 0, 0u, 1u, 0);
    run_to_end(fs);
    return tiku_fs_progress(fs)->skipped;
}

int
main(void)
{
    char home[] = "/tmp/tiku-drop-trash-XXXXXX";
    char vol[64], file[64], trash[TIKU_PATH_MAX], kept[TIKU_PATH_MAX + 16];
    char cmd[128];
    tiku_volumes_t vs;
    tiku_backend_t *b;
    tiku_alert_t alert;
    tiku_fs_t *fs;
    struct stat st;
    int skipped;

    if (mkdtemp(home) == NULL) {
        perror("mkdtemp");
        return 2;
    }
    (void)setenv("HOME", home, 1);
    snprintf(vol, sizeof vol, "%s/Vol", home);
    snprintf(file, sizeof file, "%s/note.txt", home);
    (void)mkdir(vol, 0755);
    (void)stat(vol, &st);
    memset(&vs, 0, sizeof vs);
    snprintf(vs.v[0].mount, sizeof vs.v[0].mount, "%s", vol);
    snprintf(vs.v[0].name, sizeof vs.v[0].name, "Vol");
    vs.v[0].dev = st.st_dev;
    vs.v[0].is_boot = 1;
    vs.n = 1;
    tiku_volumes_cache_set(&vs, 1);

    b = tiku_backend_posix_open();
    fs = tiku_fs_new(b);
    if (tiku_fs_trash_dir(fs, file, trash, sizeof trash) != 0) {
        printf("  (no Trash under %s)\n", home);
        return 2;
    }
    printf("Drops on the Trash: a volume with a file\n");

    put(file);
    skipped = drop_two(fs, b, &alert, trash, vol, file);
    snprintf(kept, sizeof kept, "%s/note.txt", trash);
    check(!there(file) && there(kept), "FS-041",
          "a volume dragged first: the file still goes to the Trash");
    check(there(vol) && skipped == 0 &&
          strstr(alert.text, "boot volume") != NULL, "FS-041",
          "and the volume stays, refused only as a boot volume");

    (void)unlink(kept);
    put(file);
    skipped = drop_two(fs, b, &alert, trash, file, vol);
    check(!there(file) && there(kept) && there(vol) && skipped == 0,
          "FS-041", "a volume dragged second is not part of the move");

    snprintf(cmd, sizeof cmd, "rm -rf '%s'", home);
    if (system(cmd) != 0) {
        fails++;
    }
    printf("%s\n", fails ? "FAIL" : "PASS: a volume dropped on the Trash "
           "is taken out of the move");
    return fails ? 1 : 0;
}
