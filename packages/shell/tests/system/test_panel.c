#include "tiku_shell_product.h"
/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_panel.c - the Open and Save panel's rules.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "tiku_trk_panel.h"
#include "tiku_trk_poseview.h"

static int fails;

static void
check(int cond, const char *row, const char *what)
{
    printf("  [%s] %-9s %s\n", cond ? "PASS" : "FAIL", row, what);
    if (!cond) { fails++; }
}

static char work[256];

static void
sh(const char *fmt, ...)
{
    char cmd[1024];
    va_list ap;

    va_start(ap, fmt);
    (void)vsnprintf(cmd, sizeof cmd, fmt, ap);
    va_end(ap);
    if (system(cmd) != 0) { /* the caller checks the effect, not the shell */ }
}

static tiku_model_t
of(tiku_backend_t *b, const char *path)
{
    tiku_model_t m;

    memset(&m, 0, sizeof m);
    (void)b->ops->stat(b, path, &m);
    return m;
}

int
main(void)
{
    tiku_backend_t *b = tiku_backend_posix_open();
    tiku_store_t *st = tiku_store_memory_open();
    tiku_trk_panel_t p;
    char buf[TIKU_PATH_MAX], sel[TIKU_NAME_MAX];
    tiku_model_t m;

    snprintf(work, sizeof work, "/tmp/trk-panel-%d", (int)getpid());
    sh("rm -rf %s && mkdir -p %s/sub/deep && touch %s/a.txt %s/sub/b.txt",
       work, work, work, work);

    printf("where a panel opens (AW-050)\n");
    tiku_trk_panel_open(&p, TIKU_TRK_PANEL_OPEN, TIKU_TRK_PANEL_FILE, work,
                        b, TIKU_SHELL_TRACKER_NAME);
    check(strcmp(p.dir, work) == 0, "AW-050",
          "the folder that was asked for");
    tiku_trk_panel_open(&p, TIKU_TRK_PANEL_OPEN, TIKU_TRK_PANEL_FILE,
                        "/nowhere/at/all", b, TIKU_SHELL_TRACKER_NAME);
    check(strcmp(p.dir, getenv("HOME")) == 0, "AW-050",
          "one that does not exist falls back to home");
    {
        char file[512];

        snprintf(file, sizeof file, "%s/a.txt", work);
        tiku_trk_panel_open(&p, TIKU_TRK_PANEL_OPEN, TIKU_TRK_PANEL_FILE,
                            file, b, TIKU_SHELL_TRACKER_NAME);
        check(strcmp(p.dir, getenv("HOME")) == 0, "AW-050",
              "and so does a FILE -- existing is not enough, it has to be "
              "somewhere the panel can browse");
    }

    printf("\nthe title says what is asked (AW-064)\n");
    tiku_trk_panel_open(&p, TIKU_TRK_PANEL_SAVE, TIKU_TRK_PANEL_FILE, work,
                        b, "StyledEdit");
    tiku_trk_panel_title(&p, buf, sizeof buf);
    check(strcmp(buf, "Save") == 0, "AW-064",
          "the question plainly, and only the question: the asker's name "
          "made the title too long to read");

    printf("\nthe default button (AW-053, AW-054)\n");
    tiku_trk_panel_open(&p, TIKU_TRK_PANEL_OPEN, TIKU_TRK_PANEL_FILE, work,
                        b, TIKU_SHELL_TRACKER_NAME);
    check(!tiku_trk_panel_button_enabled(&p, NULL, 0), "AW-053",
          "an open panel wanting a FILE has no answer with nothing picked");
    m = of(b, work);
    check(strcmp(tiku_trk_panel_button_label(&p, &m, 1), "Open") == 0 &&
          tiku_trk_panel_button_enabled(&p, &m, 1), "AW-053",
          "a lone folder always enables it and labels it Open, since going "
          "in is the only sense a button can make of a folder");
    {
        char file[512];
        tiku_model_t fm;

        snprintf(file, sizeof file, "%s/a.txt", work);
        fm = of(b, file);
        check(tiku_trk_panel_button_enabled(&p, &fm, 1), "AW-053",
              "a file enables it in a panel that asked for files");
        tiku_trk_panel_open(&p, TIKU_TRK_PANEL_OPEN, TIKU_TRK_PANEL_DIR,
                            work, b, TIKU_SHELL_TRACKER_NAME);
        check(!tiku_trk_panel_button_enabled(&p, &fm, 1), "AW-053",
              "and does not in one that asked for a folder");
        check(tiku_trk_panel_button_enabled(&p, NULL, 0), "AW-053",
              "which with nothing picked answers with the folder being "
              "shown rather than refusing");

        tiku_trk_panel_open(&p, TIKU_TRK_PANEL_SAVE, TIKU_TRK_PANEL_FILE,
                            work, b, TIKU_SHELL_TRACKER_NAME);
        {
            tiku_model_t dm = of(b, work);

            check(strcmp(tiku_trk_panel_button_label(&p, &dm, 1),
                         "Open") == 0, "AW-053",
                  "a lone folder relabels the button even in a SAVE panel, "
                  "where it would otherwise say Save -- the thing in front "
                  "of the user is a folder");
            check(strcmp(tiku_trk_panel_button_label(&p, &fm, 1),
                         "Save") == 0, "AW-053",
                  "while a file leaves it saying what the panel is for");
        }
        check(!tiku_trk_panel_button_enabled(&p, &fm, 1), "AW-053",
              "a save panel follows the NAME FIELD, not the selection: a "
              "highlighted row is only a suggestion for a name");
        check(tiku_trk_panel_pick_name(&p, &fm, 1) &&
              strcmp(p.name, "a.txt") == 0 &&
              p.focus == TIKU_TRK_PANEL_FOCUS_NAME, "AW-054",
              "picking one file copies its name into the field");
        check(tiku_trk_panel_button_enabled(&p, &fm, 1), "AW-053",
              "which is what enables the button");
        {
            tiku_model_t dm = of(b, work);
            char was[TIKU_TRK_PANEL_NAME_MAX];

            snprintf(was, sizeof was, "%s", p.name);
            check(!tiku_trk_panel_pick_name(&p, &dm, 1) &&
                  strcmp(p.name, was) == 0, "AW-054",
                  "picking a FOLDER does not: it relabels the button "
                  "instead, and a folder's name is not a file name");
        }
    }

    printf("\nsingle selection degrades rather than half-works "
           "(PVS-009, PVK-006)\n");
    {
        tiku_trk_poseview_t *v = tiku_trk_poseview_new(b, work);

        (void)tiku_trk_poseview_reload(v);
        tiku_trk_poseview_click(v, 0, 0u);
        tiku_trk_poseview_click(v, 1, TIKU_TRK_MOD_SHIFT);
        check(tiku_trk_poseview_selection_count(v) == 2, "PVS-009",
              "an ordinary view extends with Shift");
        tiku_trk_poseview_set_multiple(v, 0);
        check(tiku_trk_poseview_selection_count(v) == 1, "PVS-009",
              "turning it off cuts what was already gathered down to one, "
              "rather than leaving a choice it will not honour");
        tiku_trk_poseview_click(v, 0, 0u);
        tiku_trk_poseview_click(v, 1, TIKU_TRK_MOD_SHIFT);
        check(tiku_trk_poseview_selection_count(v) == 1, "PVS-009",
              "a shift-click is then a plain click");
        tiku_trk_poseview_click(v, 0, TIKU_TRK_MOD_CMD);
        check(tiku_trk_poseview_selection_count(v) == 1, "PVS-009",
              "and so is a toggle-click");
        tiku_trk_poseview_select_all(v);
        check(tiku_trk_poseview_selection_count(v) == 1, "PVS-009",
              "Select All selects the one thing it can");
        tiku_trk_poseview_set_mode(v, TIKU_TRK_MODE_ICON);
        (void)tiku_trk_poseview_place_unplaced(v, 400);
        tiku_trk_poseview_set_multiple(v, 1);
        (void)tiku_trk_poseview_select_rect(v, 0, 0, 4000, 4000, 0u);
        check(tiku_trk_poseview_selection_count(v) > 1, "PVS-009",
              "a swept band gathers everything under it in an ordinary "
              "view");
        tiku_trk_poseview_set_multiple(v, 0);
        (void)tiku_trk_poseview_select_rect(v, 0, 0, 4000, 4000, 0u);
        check(tiku_trk_poseview_selection_count(v) == 1, "PVS-009",
              "and cannot in a single-selection one -- every way of "
              "extending has to degrade, or the rule holds at three call "
              "sites out of four");
        tiku_trk_poseview_click(v, 0, 0u);
        (void)tiku_trk_poseview_key_nav(v, TIKU_TRK_KEY_DOWN,
                                    TIKU_TRK_MOD_SHIFT);
        check(tiku_trk_poseview_selection_count(v) == 1, "PVK-006",
              "Shift+arrow adds nothing: it is a plain arrow");
        tiku_trk_poseview_free(v);
    }

    printf("\nentering versus choosing (AW-055)\n");
    tiku_trk_panel_open(&p, TIKU_TRK_PANEL_OPEN,
                        TIKU_TRK_PANEL_FILE | TIKU_TRK_PANEL_DIR, work, b,
                        TIKU_SHELL_TRACKER_NAME);
    {
        char sub[512];
        tiku_model_t dm, fm;

        snprintf(sub, sizeof sub, "%s/sub", work);
        dm = of(b, sub);
        snprintf(sub, sizeof sub, "%s/a.txt", work);
        fm = of(b, sub);
        check(tiku_trk_panel_activate(&p, &dm) == TIKU_TRK_PANEL_ACT_ENTER,
              "AW-055",
              "a folder is entered even by a panel that would accept one, "
              "or the tree could not be walked past its first level");
        check(tiku_trk_panel_activate(&p, &fm) == TIKU_TRK_PANEL_ACT_CHOOSE,
              "AW-055", "a file is the answer");
        tiku_trk_panel_open(&p, TIKU_TRK_PANEL_OPEN, TIKU_TRK_PANEL_DIR,
                            work, b, TIKU_SHELL_TRACKER_NAME);
        check(tiku_trk_panel_activate(&p, &fm) ==
              TIKU_TRK_PANEL_ACT_NOTHING, "AW-055",
              "and a file is nothing at all to a panel asking for folders");
    }

    printf("\nwhat a save refuses (AW-052)\n");
    tiku_trk_panel_open(&p, TIKU_TRK_PANEL_SAVE, TIKU_TRK_PANEL_FILE, work,
                        b, TIKU_SHELL_TRACKER_NAME);
    check(tiku_trk_panel_save_check(&p, b, buf, sizeof buf) ==
          TIKU_TRK_SAVE_EMPTY, "AW-052", "nothing typed is nothing to save");
    snprintf(p.name, sizeof p.name, "..");
    p.name_len = 2;
    check(tiku_trk_panel_save_check(&p, b, buf, sizeof buf) ==
          TIKU_TRK_SAVE_DOTS, "AW-052",
          "\"..\" is not a name -- it would resolve to a directory that is "
          "not the one being written");
    snprintf(p.name, sizeof p.name, "notes.txt");
    p.name_len = 9;
    check(tiku_trk_panel_save_check(&p, b, buf, sizeof buf) ==
          TIKU_TRK_SAVE_OK && strcmp(buf, buf) == 0, "AW-052",
          "a fresh name in a writable folder goes through");
    snprintf(p.name, sizeof p.name, "a.txt");
    p.name_len = 5;
    check(tiku_trk_panel_save_check(&p, b, buf, sizeof buf) ==
          TIKU_TRK_SAVE_REPLACE, "AW-052",
          "an existing FILE is a question, since replacing one is what "
          "saving often means");
    snprintf(p.name, sizeof p.name, "sub");
    p.name_len = 3;
    check(tiku_trk_panel_save_check(&p, b, buf, sizeof buf) ==
          TIKU_TRK_SAVE_IS_FOLDER, "AW-052",
          "an existing FOLDER is final, not a question");
    {
        tiku_trk_panel_t root = p;

        snprintf(root.dir, sizeof root.dir, "/");
        snprintf(root.name, sizeof root.name, "x");
        root.name_len = 1;
        check(tiku_trk_panel_save_check(&root, b, buf, sizeof buf) ==
              TIKU_TRK_SAVE_ROOT, "AW-052", "the system root is refused");
    }
    {
        tiku_trk_panel_t ro = p;
        char locked[512];

        snprintf(locked, sizeof locked, "%s/locked", work);
        sh("mkdir -p %s && chmod 500 %s", locked, locked);
        snprintf(ro.dir, sizeof ro.dir, "%s", locked);
        snprintf(ro.name, sizeof ro.name, "x");
        ro.name_len = 1;
        check(tiku_trk_panel_save_check(&ro, b, buf, sizeof buf) ==
              TIKU_TRK_SAVE_UNWRITABLE, "AW-052",
              "and so is a folder that will not be written in");
        sh("chmod 700 %s", locked);
    }

    printf("\na drop points the panel (AW-059)\n");
    tiku_trk_panel_open(&p, TIKU_TRK_PANEL_SAVE, TIKU_TRK_PANEL_FILE,
                        getenv("HOME"), b, TIKU_SHELL_TRACKER_NAME);
    {
        char sub[512];

        snprintf(sub, sizeof sub, "%s/sub", work);
        check(tiku_trk_panel_drop(&p, b, sub, sel, sizeof sel) &&
              strcmp(p.dir, sub) == 0 && sel[0] == '\0', "AW-059",
              "a folder is navigated into");
        snprintf(sub, sizeof sub, "%s/a.txt", work);
        check(tiku_trk_panel_drop(&p, b, sub, sel, sizeof sel) &&
              strcmp(p.dir, work) == 0 && strcmp(sel, "a.txt") == 0,
              "AW-059",
              "a file lands on its FOLDER with the file pointed at, not "
              "left for the user to find again");
        check(strcmp(p.name, "a.txt") == 0, "AW-059",
              "and in a save panel it fills in the name too");
    }

    printf("\nthe folder being shown moves or goes (AW-060, PVN-029)\n");
    tiku_trk_panel_open(&p, TIKU_TRK_PANEL_OPEN, TIKU_TRK_PANEL_FILE, work,
                        b, TIKU_SHELL_TRACKER_NAME);
    {
        char sub[512], moved[512];

        snprintf(sub, sizeof sub, "%s/sub", work);
        snprintf(moved, sizeof moved, "%s/renamed", work);
        snprintf(p.dir, sizeof p.dir, "%s/sub/deep", work);
        check(tiku_trk_panel_dir_moved(&p, sub, moved) &&
              strcmp(p.dir + strlen(work), "/renamed/deep") == 0, "AW-060",
              "an ANCESTOR moving takes the panel with it, so a rename "
              "does not throw away a half-typed name");
        check(!tiku_trk_panel_dir_moved(&p, "/somewhere/else", "/x"),
              "AW-060", "an unrelated move does not");
        tiku_trk_panel_dir_gone(&p, b);
        check(strcmp(p.dir, getenv("HOME")) == 0, "PVN-029",
              "and a folder that is DELETED falls back rather than closing "
              "the panel -- closing it would answer the question as cancel");
    }

    printf("\ngoing up, with and without Desktop as the root (AW-061)\n");
    tiku_trk_panel_open(&p, TIKU_TRK_PANEL_OPEN, TIKU_TRK_PANEL_FILE, work,
                        b, TIKU_SHELL_TRACKER_NAME);
    check(tiku_trk_panel_parent(&p, buf, sizeof buf) &&
          strcmp(buf, "/tmp") == 0, "AW-061", "up is the parent folder");
    snprintf(p.dir, sizeof p.dir, "/");
    check(!tiku_trk_panel_parent(&p, buf, sizeof buf), "AW-061",
          "and there is nothing above the root");
    p.desktop_is_root = 1;
    check(tiku_trk_panel_parent(&p, buf, sizeof buf) &&
          strcmp(buf, p.desktop) == 0, "AW-061",
          "unless the Desktop is the root, when up from / lands there");
    snprintf(p.dir, sizeof p.dir, "%s", p.desktop);
    check(!tiku_trk_panel_parent(&p, buf, sizeof buf), "AW-061",
          "and the Desktop itself is then the top");

    printf("\nthe panel's chords (AW-057, AW-058, PVK-022)\n");
    tiku_trk_panel_open(&p, TIKU_TRK_PANEL_OPEN, TIKU_TRK_PANEL_FILE, work,
                        b, TIKU_SHELL_TRACKER_NAME);
    check(tiku_trk_panel_key(&p, 27, 0u, 1, 0) ==
          TIKU_TRK_PANEL_KEY_CANCEL, "AW-057", "Escape cancels");
    check(tiku_trk_panel_key(&p, 'w', TIKU_TRK_MOD_CMD, 1, 0) ==
          TIKU_TRK_PANEL_KEY_CANCEL, "AW-057", "and so does Cmd+W");
    check(tiku_trk_panel_key(&p, TIKU_TRK_KEY_UP, TIKU_TRK_MOD_CMD, 1, 0) ==
          TIKU_TRK_PANEL_KEY_PARENT, "AW-057", "Cmd+Up is the parent");
    check(tiku_trk_panel_key(&p, TIKU_TRK_KEY_DOWN, TIKU_TRK_MOD_CMD, 1,
                             0) == TIKU_TRK_PANEL_KEY_ENTER, "AW-057",
          "Cmd+Down opens what is picked");
    check(tiku_trk_panel_key(&p, 'h', TIKU_TRK_MOD_CMD, 1, 0) ==
          TIKU_TRK_PANEL_KEY_HOME, "AW-057", "Cmd+H is home");
    check(tiku_trk_panel_key(&p, '\t', 0u, 1, 0) ==
          TIKU_TRK_PANEL_KEY_FOCUS, "PVK-022",
          "Tab moves between the controls instead of walking the rows: a "
          "panel has a field and buttons to reach");
    check(tiku_trk_panel_key(&p, 'd', TIKU_TRK_MOD_CMD, 0, 1) ==
          TIKU_TRK_PANEL_KEY_DESKTOP, "AW-058", "Cmd+D is the Desktop");
    check(tiku_trk_panel_key(&p, 'd', TIKU_TRK_MOD_CMD, 1, 1) ==
          TIKU_TRK_PANEL_KEY_DUPLICATE, "AW-058",
          "unless the LIST has the keyboard and the selection could be "
          "duplicated, when it means what it means in a folder window");
    check(tiku_trk_panel_key(&p, 'd', TIKU_TRK_MOD_CMD, 1, 0) ==
          TIKU_TRK_PANEL_KEY_DESKTOP, "AW-058",
          "with nothing duplicable it is the Desktop again");
    check(tiku_trk_panel_key(&p, 'h', 0u, 1, 0) ==
          TIKU_TRK_PANEL_KEY_NONE, "AW-057",
          "and a bare letter is typing, not a chord");

    printf("\nrecent lists and favourites (AW-056, AW-062, CW-058)\n");
    tiku_trk_recent_add(st, TIKU_TRK_RECENT_DOCS, "/one");
    tiku_trk_recent_add(st, TIKU_TRK_RECENT_DOCS, "/two");
    tiku_trk_recent_add(st, TIKU_TRK_RECENT_DOCS, "/one");
    {
        char got[TIKU_TRK_RECENT_MAX][TIKU_PATH_MAX];
        int n = tiku_trk_recent_list(st, TIKU_TRK_RECENT_DOCS, 10, got,
                                     TIKU_TRK_RECENT_MAX);

        check(n == 2 && strcmp(got[0], "/one") == 0 &&
              strcmp(got[1], "/two") == 0, "AW-056",
              "most recent first, and using one again moves it rather than "
              "listing it twice");
        n = tiku_trk_recent_list(st, TIKU_TRK_RECENT_FOLDERS, 10, got,
                                 TIKU_TRK_RECENT_MAX);
        check(n == 0, "AW-056",
              "documents and folders are separate lists");
        n = tiku_trk_recent_list(st, TIKU_TRK_RECENT_DOCS, 2, got,
                                 TIKU_TRK_RECENT_MAX);
        check(n == 1, "CW-058",
              "a configured count of 2 shows ONE: matching the number the "
              "user sees beats matching the number they typed");
        n = tiku_trk_recent_list(st, TIKU_TRK_RECENT_DOCS, 0, got,
                                 TIKU_TRK_RECENT_MAX);
        check(n == 0, "CW-058", "and a count of 0 shows none");
    }
    tiku_trk_panel_open(&p, TIKU_TRK_PANEL_OPEN, TIKU_TRK_PANEL_FILE, work,
                        b, TIKU_SHELL_TRACKER_NAME);
    check(tiku_trk_panel_add_favorite(&p, st), "AW-062",
          "the folder being shown can be made a favourite");
    check(!tiku_trk_panel_add_favorite(&p, st), "AW-062",
          "twice over is still one favourite");
    {
        char got[TIKU_TRK_RECENT_MAX][TIKU_PATH_MAX];

        check(tiku_trk_panel_favorites(st, got, TIKU_TRK_RECENT_MAX) == 1 &&
              strcmp(got[0], work) == 0, "AW-062", "and it is the one added");
    }

    printf("\none shared frame for every panel (AW-063)\n");
    {
        int x = 1, y = 2, w = 3, h = 4;

        check(!tiku_trk_panel_restore_frame(st, &x, &y, &w, &h) && x == 1,
              "AW-063",
              "with nothing saved the caller's own frame is left alone");
        tiku_trk_panel_save_frame(st, 40, 50, 600, 400);
        check(tiku_trk_panel_restore_frame(st, &x, &y, &w, &h) && x == 40 &&
              w == 600 && h == 400, "AW-063", "what was saved comes back");
        {   /* Written straight into the store, as a template from another
             * screen or an older build would be -- save_frame clamps on the
             * way in, so going through it could not test this. */
            int32_t wild[4] = { 40, 50, 20, 8000 };

            (void)tiku_state_write(st, "\x01panel", "_trk/panel_frame",
                                       wild, sizeof wild);
        }
        (void)tiku_trk_panel_restore_frame(st, &x, &y, &w, &h);
        check(w == TIKU_TRK_PANEL_MIN_W && h == TIKU_TRK_PANEL_MAX_H,
              "AW-063",
              "and a frame outside the limits is clamped on the way back "
              "out, not only on the way in -- a template written by a "
              "bigger screen must not open a panel with its buttons off "
              "the bottom");
    }

    tiku_store_free(st);
    tiku_backend_close(b);
    sh("rm -rf %s", work);
    printf("\n%s\n", fails ? "FAILURES" : "the panel holds its terms");
    return fails ? 1 : 0;
}
