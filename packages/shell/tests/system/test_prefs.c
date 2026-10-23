/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_prefs.c - the settings file, and the rules the window's buttons obey.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "tiku_trk_prefs.h"

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
    tiku_trk_prefs_t p;
    const char *path = "/tmp/trk_prefs_test";

    printf("defaults (TS-045, TS-056)\n");
    tiku_trk_prefs_init(&p);
    check(tiku_trk_prefs_get(&p, "GenerateImageThumbnails") == 1, "TS-053",
          "thumbnails are on by default");
    check(tiku_trk_prefs_get(&p, "HideDotFiles") == 0, "TS-051",
          "dotfiles are shown by default");
    check(tiku_trk_prefs_get(&p, "GlobalMenuBar") == 1, "DEV-001",
          "the global menu bar is on by default: the delegated UI is "
          "the product, not an option");

    check(tiku_trk_prefs_get(&p, "SelectedLabelColor") ==
          (int32_t)0xc01a3350, "TS-045",
          "the Desktop's selected-label colour defaults to a deep navy");
    check(tiku_trk_prefs_colour_next((int32_t)0xc01a3350) ==
          (int32_t)0xc0000000, "TS-045",
          "clicking the swatch walks to the next colour, alpha intact");
    check(tiku_trk_prefs_colour_next((int32_t)0xc0cb0000) ==
          (int32_t)0xc01a3350, "TS-045",
          "and the walk wraps back around to the first");
    check((tiku_trk_prefs_colour_next(0x123456) & 0xffffff) == 0x1a3350,
          "TS-045", "a colour off the wheel lands on its first stop");
    check(tiku_trk_prefs_get(&p, "RecentApplications") == 10 &&
          tiku_trk_prefs_get(&p, "RecentFolders") == 10, "TS-056",
          "ten remembered of each kind");
    check(tiku_trk_prefs_find(&p, "NoSuchSetting") == NULL, "TS-045",
          "and a name nobody registered is not a setting");

    printf("\nthe file (TS-045, TS-062)\n");
    check(tiku_trk_prefs_find(&p, "ReduceMotion") != NULL &&
          !tiku_trk_prefs_get(&p, "ReduceMotion"), "MOTION",
          "Reduce motion is registered, defaulting to off");
    tiku_trk_prefs_set(&p, "ReduceMotion", 1);
    tiku_trk_prefs_set(&p, "HideDotFiles", 1);
    tiku_trk_prefs_set(&p, "ShowNavigator", 1);
    tiku_trk_prefs_set(&p, "SingleWindowBrowse", 0);
    check(tiku_trk_prefs_get(&p, "ShowNavigator") == 0, "TS-048",
          "turning single-window browsing off also hides its navigator");
    tiku_trk_prefs_set(&p, "UsedSpaceColor", (int32_t)0x11223344);
    check(tiku_trk_prefs_save(&p, path) == 0, "TS-045", "it saves");
    {
        FILE *f = fopen(path, "r");
        char buf[4096];
        size_t n = (f != NULL) ? fread(buf, 1, sizeof buf - 1u, f) : 0u;

        if (f != NULL) { (void)fclose(f); }
        buf[n] = '\0';
        check(strstr(buf, "HideDotFiles on") != NULL, "TS-045",
              "booleans are written on/off, not 1/0");
        check(strstr(buf, "GlobalMenuBar") == NULL, "TS-063",
              "a setting still at its default is NOT written: the file "
              "records decisions, so a later build's better default is "
              "not vetoed by every file saved before it");
        check(strstr(buf, "UsedSpaceColor 0x11223344") != NULL, "TS-045",
              "and colours as 0x%08x");
    }
    {   /* Replaced whole, never rewritten in place: a Tracker that opened
         * the old file reads all of it, and the name then holds the new. */
        FILE *old = fopen(path, "r");
        struct stat was, now;
        char buf[4096];
        size_t n;

        check(old != NULL && stat(path, &was) == 0, "TS-045",
              "the saved file is there to be read");
        tiku_trk_prefs_set(&p, "HideDotFiles", 0);
        check(tiku_trk_prefs_save(&p, path) == 0 &&
              stat(path, &now) == 0 && now.st_ino != was.st_ino,
              "TS-045", "a save puts a new file in place of the old one");
        n = (old != NULL) ? fread(buf, 1, sizeof buf - 1u, old) : 0u;
        buf[n] = '\0';
        check(strstr(buf, "HideDotFiles on") != NULL, "TS-045",
              "while a reader of the old one still has all of it");
        if (old != NULL) { (void)fclose(old); }
        tiku_trk_prefs_set(&p, "HideDotFiles", 1);
        (void)tiku_trk_prefs_save(&p, path);
    }
    {   /* A settings file kept as a link stays a link to it. */
        const char *target = "/tmp/trk_prefs_target";
        const char *link = "/tmp/trk_prefs_link";
        struct stat st;
        FILE *f;
        char buf[4096];
        size_t n = 0u;

        (void)unlink(link);
        (void)unlink(target);
        f = fopen(target, "w");
        if (f != NULL) { (void)fclose(f); }
        check(f != NULL && symlink(target, link) == 0 &&
              tiku_trk_prefs_save(&p, link) == 0 &&
              lstat(link, &st) == 0 && S_ISLNK(st.st_mode), "TS-045",
              "saving through a link leaves the link in place");
        f = fopen(target, "r");
        if (f != NULL) {
            n = fread(buf, 1, sizeof buf - 1u, f);
            (void)fclose(f);
        }
        buf[n] = '\0';
        check(strstr(buf, "HideDotFiles on") != NULL, "TS-045",
              "and writes the file it points to");
        (void)unlink(link);
        (void)unlink(target);
    }
    {   /* A setting this build does not know must survive a round trip:
         * unknown records from other versions remain in the file. */
        FILE *f = fopen(path, "a");

        if (f != NULL) {
            fputs("SomeFutureSetting 42\n", f);
            (void)fclose(f);
        }
        tiku_trk_prefs_free(&p);
        tiku_trk_prefs_init(&p);
        check(tiku_trk_prefs_load(&p, path) == 0, "TS-045", "it loads back");
        check(tiku_trk_prefs_get(&p, "HideDotFiles") == 1, "TS-051",
              "with the values it was given");
        check(tiku_trk_prefs_get(&p, "ReduceMotion") == 1, "MOTION",
              "Reduce motion survives saving and reloading");
        check(tiku_trk_prefs_get(&p, "UsedSpaceColor") ==
              (int32_t)0x11223344, "TS-054", "colours included");
        check(p.nunknown == 1, "TS-062",
              "a setting this build does not know is KEPT");
        (void)tiku_trk_prefs_save(&p, path);
        {
            FILE *g = fopen(path, "r");
            char buf[4096];
            size_t n = (g != NULL) ? fread(buf, 1, sizeof buf - 1u, g) : 0u;

            if (g != NULL) { (void)fclose(g); }
            buf[n] = '\0';
            check(strstr(buf, "SomeFutureSetting 42") != NULL, "TS-062",
                  "and written back, so the file round-trips through a "
                  "build that never heard of it");
        }
    }
    {   /* A missing file is not an error: the defaults stand. */
        tiku_trk_prefs_t q;

        tiku_trk_prefs_init(&q);
        check(tiku_trk_prefs_load(&q, "/tmp/trk_prefs_absent") == 0 &&
              tiku_trk_prefs_get(&q, "RecentFolders") == 10, "TS-045",
              "a settings file that is not there leaves the defaults");
        tiku_trk_prefs_free(&q);
    }

    printf("\nwhat the buttons obey (TS-059, TS-060)\n");
    tiku_trk_prefs_free(&p);
    tiku_trk_prefs_init(&p);
    tiku_trk_prefs_mark_opened(&p);
    check(!tiku_trk_prefs_any_defaultable(&p) &&
          !tiku_trk_prefs_any_revertable(&p), "TS-059",
          "a window just opened on untouched settings offers neither "
          "button");

    tiku_trk_prefs_set(&p, "HideDotFiles", 1);
    check(tiku_trk_prefs_defaultable(&p, TIKU_TRK_PAGE_WINDOWS), "TS-060",
          "changing a Windows setting marks THAT page");
    check(!tiku_trk_prefs_defaultable(&p, TIKU_TRK_PAGE_DESKTOP), "TS-060",
          "and not the others -- which is what shows one page in bold");
    check(tiku_trk_prefs_any_defaultable(&p) &&
          tiku_trk_prefs_any_revertable(&p), "TS-059",
          "so both buttons become available");

    tiku_trk_prefs_revert(&p, TIKU_TRK_PAGE_WINDOWS);
    check(tiku_trk_prefs_get(&p, "HideDotFiles") == 0, "TS-061",
          "reverting a page really does restore Hide dotfiles -- the "
          "original's Windows page misses it, which is a bug rather than a "
          "behaviour worth porting");
    check(!tiku_trk_prefs_any_revertable(&p), "TS-059",
          "and the Revert button goes away again");

    {   /* Defaults and Revert are different questions: a setting can be
         * back at what it was when the window opened and still differ
         * from the compiled default. */
        tiku_trk_prefs_set(&p, "TransparentSelection", 0);
        tiku_trk_prefs_mark_opened(&p);
        check(tiku_trk_prefs_any_defaultable(&p), "TS-059",
              "a setting saved away from its default keeps Defaults live");
        check(!tiku_trk_prefs_any_revertable(&p), "TS-059",
              "while Revert, which measures against the window opening, "
              "does not");
        tiku_trk_prefs_defaults(&p, TIKU_TRK_PAGE_WINDOWS);
        check(tiku_trk_prefs_get(&p, "TransparentSelection") == 1, "TS-059",
              "and Defaults puts the page back to the compiled values");
    }

    printf("\nthe pages (TS-058)\n");
    check(strcmp(tiku_trk_prefs_page_name(TIKU_TRK_PAGE_DESKTOP),
                 "Desktop") == 0 &&
          strcmp(tiku_trk_prefs_page_name(TIKU_TRK_PAGE_MOUNT),
                 "Disk mount") == 0, "TS-058",
          "four pages, named as the original names them");
    {
        int k, with = 0;

        for (k = 0; k < TIKU_TRK_PAGES; k++) {
            int i, any = 0;

            for (i = 0; i < p.n; i++) {
                if (p.pref[i].page == (tiku_trk_page_t)k) { any = 1; }
            }
            with += any;
        }
        check(with == TIKU_TRK_PAGES, "TS-058",
              "and every one of them has something on it");
    }


    printf("\nthe window's layout (TS-058)\n");
    {
        int idx[32], n, x, y, w, h, x2, which = -1;

        n = tiku_trk_prefs_page_controls(&p, TIKU_TRK_PAGE_MOUNT, idx, 32);
        check(n == 2, "TS-057",
              "the Disk mount page shows its two settings");
        n = tiku_trk_prefs_page_controls(&p, TIKU_TRK_PAGE_NONE, idx, 32);
        check(n > 0, "TS-062",
              "settings with no page exist but are not on one -- which is "
              "how an obsolete one keeps round-tripping without a control");

        tiku_trk_prefs_page_rect(0, &x, &y, &w, &h);
        check(x + w <= TIKU_TRK_PREF_LIST_W, "TS-058",
              "the page list stays in its column");
        tiku_trk_prefs_control_rect(0, &x2, NULL, &w, NULL);
        check(x2 >= TIKU_TRK_PREF_LIST_W, "TS-058",
              "and the controls start after it");

        check(tiku_trk_prefs_hit(&p, TIKU_TRK_PAGE_WINDOWS, x + 2, y + 2,
                                 &which) == TIKU_TRK_PREF_HIT_PAGE &&
              which == 0, "TS-058",
              "a click in the list picks a page, and says which");

        n = tiku_trk_prefs_page_controls(&p, TIKU_TRK_PAGE_WINDOWS, idx, 32);
        tiku_trk_prefs_control_rect(1, &x, &y, &w, &h);
        check(tiku_trk_prefs_hit(&p, TIKU_TRK_PAGE_WINDOWS, x + 2, y + 2,
                                 &which) == TIKU_TRK_PREF_HIT_SETTING &&
              which == idx[1], "TS-058",
              "a click on a control picks the setting the table has in "
              "that position -- one order, not two");
        check(tiku_trk_prefs_hit(&p, TIKU_TRK_PAGE_MOUNT, x + 2, y + 2,
                                 &which) == TIKU_TRK_PREF_HIT_SETTING &&
              which != idx[1], "TS-058",
              "and the same spot on another page picks that page's control");

        tiku_trk_prefs_button_rect(0, &x, &y, &w, &h);
        tiku_trk_prefs_button_rect(1, &x2, NULL, NULL, NULL);
        check(x2 > x, "TS-059",
              "Revert sits to the right of Defaults");
        check(tiku_trk_prefs_hit(&p, TIKU_TRK_PAGE_WINDOWS, x + 2, y + 2,
                                 NULL) == TIKU_TRK_PREF_HIT_DEFAULTS,
              "TS-059", "and both are clickable");
        check(tiku_trk_prefs_hit(&p, TIKU_TRK_PAGE_WINDOWS, 4, 250, NULL)
              == TIKU_TRK_PREF_HIT_NONE, "TS-058",
              "while empty space is nothing");
    }

    printf("\nthe Desktop page interlocks (TS-046)\n");
    {
        tiku_trk_prefs_t q;

        tiku_trk_prefs_init(&q);
        tiku_trk_prefs_set(&q, "MountVolumesOntoDesktop", 1);
        tiku_trk_prefs_set(&q, "MountSharedVolumesOntoDesktop", 1);
        tiku_trk_prefs_set(&q, "ShowDisksIcon", 1);
        check(tiku_trk_prefs_get(&q, "MountVolumesOntoDesktop") == 0,
              "TS-046", "the Disks icon turns volumes-on-Desktop off");
        check(tiku_trk_prefs_get(&q, "MountSharedVolumesOntoDesktop") == 0,
              "TS-046", "and shared volumes went with it");
        check(!tiku_trk_prefs_control_enabled(&q,
                  tiku_trk_prefs_find(&q, "MountSharedVolumesOntoDesktop")),
              "TS-046", "with the base setting off, the shared control is greyed");
        tiku_trk_prefs_set(&q, "MountVolumesOntoDesktop", 1);
        check(tiku_trk_prefs_get(&q, "ShowDisksIcon") == 0,
              "TS-046", "and volumes-on-Desktop turns the Disks icon off -- the two "
              "are one question");
        check(tiku_trk_prefs_control_enabled(&q,
                  tiku_trk_prefs_find(&q, "MountSharedVolumesOntoDesktop")),
              "TS-046", "which un-greys shared volumes again");
        {   /* The Mount page can never be defaulted (TS-065). */
            tiku_trk_prefs_t r;

            tiku_trk_prefs_init(&r);
            tiku_trk_prefs_set(&r, "EjectWhenUnmounting", 0);
            check(!tiku_trk_prefs_defaultable(&r, TIKU_TRK_PAGE_MOUNT),
                  "TS-065", "the Mount page never reports defaultable");
            tiku_trk_prefs_defaults(&r, TIKU_TRK_PAGE_MOUNT);
            check(tiku_trk_prefs_get(&r, "EjectWhenUnmounting") == 0,
                  "TS-065", "and Defaults cannot touch it -- the mount "
                  "policy stays what the user set");
            tiku_trk_prefs_free(&r);
        }
        tiku_trk_prefs_free(&q);
    }

    (void)remove(path);
    tiku_trk_prefs_free(&p);
    printf("\n%s\n", fails ? "FAILURES" : "the settings hold");
    return fails ? 1 : 0;
}
