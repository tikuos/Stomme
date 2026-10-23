/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_frame.c - where a window was, and which windows were open.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <string.h>

#include "tiku_trk_frame.h"

static int fails;

static void
check(int cond, const char *row, const char *what)
{
    printf("  [%s] %-9s %s\n", cond ? "PASS" : "FAIL", row, what);
    if (!cond) { fails++; }
}

int
main(void)
{
    tiku_store_t *st = tiku_store_memory_open();
    tiku_trk_frame_t f, got;

    printf("a folder remembers its own window (CW-001)\n");
    f.x = 100; f.y = 60; f.w = 400; f.h = 300;
    check(!tiku_trk_frame_load(st, "/a", &got), "CW-001",
          "a folder never opened has no frame, so the caller cascades");
    check(tiku_trk_frame_save(st, "/a", &f, NULL) == 1, "CW-001",
          "one that has been moved keeps it");
    check(tiku_trk_frame_load(st, "/a", &got) && got.x == 100 &&
          got.h == 300, "CW-001", "and it comes back whole");
    check(!tiku_trk_frame_load(st, "/b", &got), "CW-001",
          "kept per FOLDER, not in one shared slot -- one frame for every "
          "folder is the same as none, since the last window closed would "
          "decide where the next one opens");

    printf("\nwritten only when it moved (CW-002)\n");
    check(tiku_trk_frame_save(st, "/a", &f, &f) == 0, "CW-002",
          "a window that opened where it was told and was never touched "
          "has nothing to say");
    {
        tiku_trk_frame_t moved = f;

        moved.x += 1;
        check(tiku_trk_frame_save(st, "/a", &moved, &f) == 1, "CW-002",
              "one pixel is a move");
        moved = f;
        moved.h += 1;
        check(tiku_trk_frame_save(st, "/a", &moved, &f) == 1, "CW-002",
              "and so is a resize -- the test is the whole rectangle, not "
              "its corner");
    }

    printf("\nwhat a first opening inherits (CW-005, CW-069)\n");
    check(tiku_trk_frame_inherit(st, "/a/child", &got) && got.x == 100 + 22,
          "CW-005",
          "a folder opened for the first time takes its parent's frame, "
          "offset -- landing exactly on top of the window it came from "
          "looks like nothing happened");
    check(!tiku_trk_frame_inherit(st, "/", &got), "CW-069",
          "the ROOT inherits nothing: it has no parent, and a default "
          "applied to it would move the one window everything else is "
          "measured against");
    check(!tiku_trk_frame_inherit(st, "/nowhere/deep", &got), "CW-006",
          "with no parent frame and no default there is nothing to "
          "inherit, and the caller cascades");
    {
        tiku_trk_frame_t def;

        def.x = 10; def.y = 12; def.w = 640; def.h = 480;
        tiku_trk_frame_set_default(st, &def);
        check(tiku_trk_frame_inherit(st, "/nowhere/deep", &got) &&
              got.w == 640, "CW-006",
              "once a default exists, that is what an unvisited folder "
              "starts from");
        check(!tiku_trk_frame_inherit(st, "/", &got), "CW-069",
              "and the root still takes none of it");
    }

    printf("\nclamped onto the screen it is opening on (CW-001)\n");
    {
        tiku_trk_frame_t wild;

        wild.x = 5000; wild.y = 5000; wild.w = 400; wild.h = 300;
        tiku_trk_frame_clamp(&wild, 1000, 680);
        check(wild.x < 1000 && wild.y <= 680 - TIKU_TRK_FRAME_TAB &&
              wild.y >= 0, "CW-001",
              "a frame from a bigger screen is pulled back until its TAB "
              "is reachable -- a window nobody can grab is the same as no "
              "window");
        wild.x = -900; wild.y = 100; wild.w = 400; wild.h = 300;
        tiku_trk_frame_clamp(&wild, 1000, 680);
        check(wild.x + wild.w >= TIKU_TRK_FRAME_GRAB, "CW-001",
              "and one dragged off the left is pulled back the same way");
        wild.x = 40; wild.y = 500; wild.w = 400; wild.h = 300;
        tiku_trk_frame_clamp(&wild, 1000, 680);
        check(wild.x == 40 && wild.y == 500, "CW-001",
              "but a window that merely HANGS off the bottom is left where "
              "it is: fitting entirely on screen is a stricter rule than "
              "being reachable, and it would move windows the user put "
              "there deliberately");
        wild.x = 0; wild.y = 0; wild.w = 9000; wild.h = 9000;
        tiku_trk_frame_clamp(&wild, 1000, 680);
        check(wild.w == 1000 && wild.h == 680, "CW-001",
              "and one bigger than the screen is cut down to it");
        wild.w = 2; wild.h = 2;
        tiku_trk_frame_clamp(&wild, 1000, 680);
        check(wild.w >= TIKU_TRK_FRAME_MIN_W &&
              wild.h >= TIKU_TRK_FRAME_MIN_H, "CW-001",
              "a sliver is grown to something usable");
    }
    {
        tiku_trk_frame_t tiny;

        tiny.x = 0; tiny.y = 0; tiny.w = 4; tiny.h = 4;
        (void)tiku_state_write(st, "/tiny", TIKU_ATTR_FRAME, &tiny,
                                   sizeof tiny);
        check(!tiku_trk_frame_load(st, "/tiny", &got), "CW-001",
              "a stored record too small to be a window is not treated as "
              "one: the caller cascades rather than opening a sliver");
    }

    printf("\nthe set of windows that were open (AW-004)\n");
    {
        char paths[3][TIKU_PATH_MAX];
        char back[TIKU_TRK_SESSION_MAX][TIKU_PATH_MAX];
        int n;

        snprintf(paths[0], TIKU_PATH_MAX, "/one");
        snprintf(paths[1], TIKU_PATH_MAX, "/two");
        snprintf(paths[2], TIKU_PATH_MAX, "/three");
        check(tiku_trk_session_save(st, paths, 3) == 3, "AW-004",
              "the open set is written");
        n = tiku_trk_session_load(st, back, TIKU_TRK_SESSION_MAX);
        check(n == 3 && strcmp(back[0], "/one") == 0 &&
              strcmp(back[2], "/three") == 0, "AW-004",
              "and comes back IN ORDER -- that is the order they reopen "
              "in, which decides which ends up in front");
        tiku_trk_session_clear(st);
        check(tiku_trk_session_load(st, back, TIKU_TRK_SESSION_MAX) == 0,
              "AW-004", "clearing it means the next start opens the "
              "default instead");
        (void)tiku_trk_session_save(st, paths, 3);
        check(tiku_trk_session_load(st, back, TIKU_TRK_SESSION_MAX) == 3,
              "AW-004", "a session is written again");
        check(tiku_trk_session_save(st, NULL, 0) == 0 &&
              tiku_trk_session_load(st, back, TIKU_TRK_SESSION_MAX) == 0,
              "AW-004",
              "and quitting with nothing open CLEARS it, rather than "
              "leaving the last session to be restored for ever by a shell "
              "the user deliberately emptied");
    }

    printf("\nminimized windows survive a session (AW-006)\n");
    {
        tiku_trk_session_entry_t saved[2];
        tiku_trk_session_entry_t back[TIKU_TRK_SESSION_MAX];
        int n;

        snprintf(saved[0].path, sizeof saved[0].path, "/shown");
        saved[0].minimized = 0;
        snprintf(saved[1].path, sizeof saved[1].path, "/hidden");
        saved[1].minimized = 1;
        check(tiku_trk_session_save_entries(st, saved, 2) == 2, "AW-006",
              "each restored window carries its shown or minimized state");
        n = tiku_trk_session_load_entries(st, back, TIKU_TRK_SESSION_MAX);
        check(n == 2 && strcmp(back[0].path, "/shown") == 0 &&
              !back[0].minimized && strcmp(back[1].path, "/hidden") == 0 &&
              back[1].minimized, "AW-006",
              "the state comes back with the same window, in order");
        (void)tiku_state_write(st, TIKU_KEY_SESSION,
                                   TIKU_ATTR_SESSION,
                                   "/old-format\n", 12);
        check(tiku_trk_session_load_entries(st, back,
                                            TIKU_TRK_SESSION_MAX) == 1 &&
              strcmp(back[0].path, "/old-format") == 0 &&
              !back[0].minimized, "AW-006",
              "path-only sessions from older builds remain shown windows");
    }

    tiku_store_free(st);
    printf("\n%s\n", fails ? "FAILURES" : "windows come back where they were");
    return fails ? 1 : 0;
}
