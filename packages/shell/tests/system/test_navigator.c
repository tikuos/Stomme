/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_navigator.c - browser history and parent paths.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_trk_navigator.h"

#include <stdio.h>
#include <string.h>

static int fails;

static void
check(int ok, const char *what)
{
    printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) {
        fails++;
    }
}

int
main(void)
{
    tiku_trk_navigator_t n;
    tiku_model_t dropped;
    char path[TIKU_PATH_MAX];
    int i, steps = 0;

    printf("browser history\n");
    tiku_trk_navigator_init(&n, "/start");
    check(strcmp(tiku_trk_navigator_current(&n), "/start") == 0,
          "the first folder is the current location");
    check(!tiku_trk_navigator_can_back(&n) &&
          !tiku_trk_navigator_can_forward(&n),
          "a new window has nowhere back or forward");

    tiku_trk_navigator_set(&n, "/start/one");
    tiku_trk_navigator_set(&n, "/start/one/two");
    check(tiku_trk_navigator_can_back(&n) &&
          !tiku_trk_navigator_can_forward(&n),
          "new locations grow Back and clear Forward");
    check(tiku_trk_navigator_back(&n, path, sizeof path) &&
          strcmp(path, "/start/one") == 0 &&
          tiku_trk_navigator_can_forward(&n),
          "Back returns the previous folder and grows Forward");
    check(tiku_trk_navigator_forward(&n, path, sizeof path) &&
          strcmp(path, "/start/one/two") == 0,
          "Forward returns to the folder Back left");
    (void)tiku_trk_navigator_back(&n, path, sizeof path);
    tiku_trk_navigator_set(&n, "/elsewhere");
    check(!tiku_trk_navigator_can_forward(&n),
          "a new destination discards the abandoned forward branch");
    check(!tiku_trk_navigator_back(&n, NULL, sizeof path) &&
          !tiku_trk_navigator_forward(&n, path, 0),
          "history refuses destinations it cannot return to the caller");

    printf("\nthe bounded stack\n");
    tiku_trk_navigator_init(&n, "/p0");
    for (i = 1; i <= 40; i++) {
        snprintf(path, sizeof path, "/p%d", i);
        tiku_trk_navigator_set(&n, path);
    }
    while (tiku_trk_navigator_back(&n, path, sizeof path)) {
        steps++;
    }
    check(steps == TIKU_TRK_NAV_HISTORY_MAX - 1,
          "history keeps at most 32 folders, dropping the oldest");
    check(strcmp(path, "/p9") == 0,
          "the oldest retained folder is the one the cap leaves reachable");

    printf("\nparent paths\n");
    check(tiku_trk_navigator_parent("/a/b", path, sizeof path) &&
          strcmp(path, "/a") == 0,
          "Up removes one path component");
    check(tiku_trk_navigator_parent("/a", path, sizeof path) &&
          strcmp(path, "/") == 0,
          "Up from a top-level folder reaches the root");
    check(tiku_trk_navigator_parent("/a/b///", path, sizeof path) &&
          strcmp(path, "/a") == 0,
          "Up ignores trailing separators");
    check(!tiku_trk_navigator_parent("/", path, sizeof path),
          "the root has no Up destination");
    {
        char chain[8][TIKU_PATH_MAX];
        int nchain = tiku_trk_navigator_hierarchy("/a/b/c", chain, 8);

        check(nchain == 3 && strcmp(chain[0], "/a/b") == 0 &&
              strcmp(chain[1], "/a") == 0 && strcmp(chain[2], "/") == 0,
              "the count-area parent menu gets every ancestor, nearest first");
    }

    printf("\ndrop destinations\n");
    memset(&dropped, 0, sizeof dropped);
    dropped.kind = TIKU_KIND_DIRECTORY;
    snprintf(dropped.path, sizeof dropped.path, "/a/folder");
    check(tiku_trk_navigator_drop_path(&dropped, path, sizeof path) &&
          strcmp(path, "/a/folder") == 0,
          "dropping a folder navigates into that folder");
    dropped.kind = TIKU_KIND_FILE;
    snprintf(dropped.path, sizeof dropped.path, "/a/file");
    check(tiku_trk_navigator_drop_path(&dropped, path, sizeof path) &&
          strcmp(path, "/a") == 0,
          "dropping a file navigates to its enclosing folder");

    printf("\n%s\n", fails ? "FAILURES" : "the navigator holds its history");
    return fails ? 1 : 0;
}
