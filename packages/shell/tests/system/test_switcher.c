#include "tiku_shell_product.h"
/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_switcher.c - the application switcher: its order, its hair trigger,
 * and what each key does while it is up.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <string.h>

#include "tiku_trk_switcher.h"

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
    tiku_trk_switcher_t s;
    char title[128];
    int a, bq, c;
    int64_t at = 20000000;          /* when the keys below arrive */

    tiku_trk_switcher_init(&s);
    a  = tiku_trk_switcher_app(&s, TIKU_SHELL_TRACKER_NAME, 1);
    bq = tiku_trk_switcher_app(&s, "Editor", 0);
    c  = tiku_trk_switcher_app(&s, "Terminal", 0);
    tiku_trk_switcher_window(&s, bq, "notes.txt");
    tiku_trk_switcher_window(&s, bq, "draft.txt");
    check(a == 0 && bq == 1 && c == 2, "DB-054",
          "applications appear once each");
    check(tiku_trk_switcher_app(&s, "Editor", 0) == bq, "DB-054",
          "and asking again finds the same one");

    printf("\nthe hair trigger (DB-054)\n");
    tiku_trk_switcher_open(&s, 1000000, 0);
    check(s.shown && s.sel == 1, "DB-054",
          "Ctrl+Tab shows it and moves on one");
    check(tiku_trk_switcher_release(&s, 1000000 + 100000), "DB-054",
          "released inside 180ms is a QUICK switch -- the user meant the "
          "previous application, not the list");
    check(!s.shown, "DB-054", "and it goes away");
    tiku_trk_switcher_open(&s, 5000000, 0);
    check(!tiku_trk_switcher_release(&s, 5000000 + 500000), "DB-054",
          "held longer is not: the same gesture means two things and only "
          "the timing tells them apart");

    printf("\nthe order (DB-055)\n");
    {
        tiku_trk_switcher_t t;
        int i;

        tiku_trk_switcher_init(&t);
        (void)tiku_trk_switcher_app(&t, "One", 0);
        (void)tiku_trk_switcher_app(&t, "Two", 0);
        (void)tiku_trk_switcher_app(&t, "Three", 0);
        t.app[2].used = 900;        /* Three used most recently */
        t.app[0].used = 100;
        t.app[1].used = 50;
        tiku_trk_switcher_open(&t, 10000000, 0);
        check(strcmp(t.app[0].name, "Three") == 0, "DB-055",
              "opening after a pause sorts most-recently-used first");
        (void)tiku_trk_switcher_release(&t, 10000000 + 10);
        /* Straight away again: the order must not move under the user. */
        for (i = 0; i < 3; i++) {
            tiku_trk_switcher_open(&t, 10000000 + 20 + i, 0);
            (void)tiku_trk_switcher_release(&t, 10000000 + 25 + i);
        }
        check(strcmp(t.app[0].name, "Three") == 0, "DB-055",
              "and fast repeated switching keeps that order -- a re-sort "
              "between presses would move the target out from under the "
              "second press");
    }

    printf("\nthe keys (DB-056)\n");
    tiku_trk_switcher_open(&s, 20000000, 0);
    s.sel = 1;
    check(tiku_trk_switcher_key(&s, "Tab", 0, at) == TIKU_TRK_SW_MOVED &&
          s.sel == 2, "DB-056", "Tab moves on");
    check(tiku_trk_switcher_key(&s, "Tab", 1, at) == TIKU_TRK_SW_MOVED &&
          s.sel == 1, "DB-056", "and with Shift, back");
    check(tiku_trk_switcher_key(&s, "Down", 0, at) == TIKU_TRK_SW_MOVED &&
          s.selwin == 1, "DB-056", "Down moves between that app's windows");
    check(tiku_trk_switcher_key(&s, "Down", 0, at) == TIKU_TRK_SW_NOTHING &&
          s.selwin == 1, "DB-056",
          "and they do NOT wrap: past the last window is nothing, which "
          "reads as the key not having been missed");
    s.sel = 0;                      /* Tracker itself */
    check(tiku_trk_switcher_key(&s, "q", 0, at) == TIKU_TRK_SW_NOTHING,
          "DB-056", "Q does not quit Tracker from its own switcher");
    s.sel = 1;
    check(tiku_trk_switcher_key(&s, "q", 0, at) == TIKU_TRK_SW_QUIT_APP,
          "DB-056", "but does quit anything else");
    check(tiku_trk_switcher_key(&s, "h", 0, at) == TIKU_TRK_SW_HIDE_APP,
          "DB-056", "and H hides");
    check(tiku_trk_switcher_key(&s, "Escape", 0, at) == TIKU_TRK_SW_CANCEL &&
          !s.shown, "DB-056", "Escape cancels");
    tiku_trk_switcher_open(&s, 30000000, 0);
    check(tiku_trk_switcher_key(&s, "space", 0, at) == TIKU_TRK_SW_COMMIT,
          "DB-056", "Space commits");

    printf("\nthe mouse (DB-059)\n");
    tiku_trk_switcher_open(&s, 40000000, 0);
    check(tiku_trk_switcher_click(&s, 2) == TIKU_TRK_SW_MOVED &&
          s.sel == 2, "DB-059", "a click on a slot selects it");
    check(tiku_trk_switcher_click(&s, 9) == TIKU_TRK_SW_NOTHING, "DB-059",
          "a click on an empty slot does nothing");
    check(tiku_trk_switcher_click(&s, -1) == TIKU_TRK_SW_CANCEL &&
          !s.shown, "DB-059",
          "and one outside dismisses WITHOUT switching -- a click that "
          "missed must not choose whatever it was over");

    printf("\nwhere it sits, and what it says (DB-060)\n");
    {
        int x = -1, y = -1;

        tiku_trk_switcher_place(1000, 800, 400, 100, &x, &y);
        check(x == 300, "DB-060", "centred across");
        check(y == 300, "DB-060",
              "and slightly ABOVE centre -- a panel centred exactly sits "
              "under where the eye already is");
    }
    tiku_trk_switcher_title(&s, bq, 0, title, sizeof title);
    check(strcmp(title, "Editor: notes.txt") == 0, "DB-060",
          "a row reads \"App: Window\"");
    /* A window that EXISTS but is untitled, which is the case the rule is
     * about -- an app with no windows would take the same branch for a
     * different reason. */
    tiku_trk_switcher_window(&s, c, "");
    tiku_trk_switcher_title(&s, c, 0, title, sizeof title);
    check(strcmp(title, "Terminal") == 0, "DB-060",
          "and just the application when the window has no title of its "
          "own, since repeating the name twice says nothing");


    printf("\nthe animation (DB-058)\n");
    {
        tiku_trk_switcher_t t;
        int64_t t0 = 100000000;

        tiku_trk_switcher_init(&t);
        (void)tiku_trk_switcher_app(&t, "One", 0);
        (void)tiku_trk_switcher_app(&t, "Two", 0);
        tiku_trk_switcher_open(&t, t0, 0);   /* 0 -> 1 */

        check(tiku_trk_switcher_animating(&t, t0), "DB-058",
              "a move starts the strip animating");
        check(tiku_trk_switcher_icon_size(&t, 1, t0) < TIKU_TRK_SW_LARGE &&
              tiku_trk_switcher_icon_size(&t, 1, t0) > TIKU_TRK_SW_SMALL,
              "DB-058", "the incoming icon is part way grown");
        check(tiku_trk_switcher_icon_size(&t, 0, t0) < TIKU_TRK_SW_LARGE &&
              tiku_trk_switcher_icon_size(&t, 0, t0) > TIKU_TRK_SW_SMALL,
              "DB-058", "and the outgoing one part way shrunk -- the pair "
              "says which way the selection went");
        check(tiku_trk_switcher_icon_size(&t, 0, t0) +
              tiku_trk_switcher_icon_size(&t, 1, t0) ==
              TIKU_TRK_SW_SMALL + TIKU_TRK_SW_LARGE, "DB-058",
              "they trade size between them rather than both growing");

        check(!tiku_trk_switcher_animating(&t, t0 + 40000), "DB-058",
              "it is over after two twenty-millisecond steps");
        check(tiku_trk_switcher_icon_size(&t, 1, t0 + 40000) ==
              TIKU_TRK_SW_LARGE &&
              tiku_trk_switcher_icon_size(&t, 0, t0 + 40000) ==
              TIKU_TRK_SW_SMALL, "DB-058",
              "and settles with only the selection large");
        check(tiku_trk_switcher_icon_size(&t, 1, t0 + 5) ==
              tiku_trk_switcher_icon_size(&t, 1, t0 + 19000), "DB-058",
              "the steps are QUANTISED, not a smooth slide: a continuous "
              "one would be a different animation wearing these numbers");
    }

    printf("\nthe end scrollers (DB-058)\n");
    {
        tiku_trk_switcher_t t;
        char nm[16];
        int i;

        tiku_trk_switcher_init(&t);
        for (i = 0; i < 3; i++) {
            snprintf(nm, sizeof nm, "app%d", i);
            (void)tiku_trk_switcher_app(&t, nm, 0);
        }
        check(!tiku_trk_switcher_scroller(&t, 0) &&
              !tiku_trk_switcher_scroller(&t, 1), "DB-058",
              "with fewer apps than slots there is nothing to reach, so no "
              "arrows");
        for (i = 3; i < 12; i++) {
            snprintf(nm, sizeof nm, "app%d", i);
            (void)tiku_trk_switcher_app(&t, nm, 0);
        }
        t.scroll = 0;
        check(!tiku_trk_switcher_scroller(&t, 0) &&
              tiku_trk_switcher_scroller(&t, 1), "DB-058",
              "at the left end only the right arrow shows");
        t.scroll = 12 - TIKU_TRK_SW_SLOTS;
        check(tiku_trk_switcher_scroller(&t, 0) &&
              !tiku_trk_switcher_scroller(&t, 1), "DB-058",
              "and at the right end only the left one");
    }

    printf("\nthe strip slides to the new slot (DB-058)\n");
    {
        tiku_trk_switcher_t t;
        int i2;

        tiku_trk_switcher_init(&t);
        for (i2 = 0; i2 < TIKU_TRK_SW_SLOTS + 3; i2++) {
            char nm[16];

            snprintf(nm, sizeof nm, "app%d", i2);
            (void)tiku_trk_switcher_app(&t, nm, 0);
        }
        tiku_trk_switcher_open(&t, 90000000, 0);
        for (i2 = 0; i2 < TIKU_TRK_SW_SLOTS; i2++) {
            tiku_trk_switcher_open(&t, 90000000 + i2, 0);
        }
        /* The selection just crossed the right edge: scroll went 0 -> 1
         * and the strip is mid-slide. */
        check(t.scroll > 0 && t.scroll_from == t.scroll - 1, "DB-058",
              "crossing the edge remembers where the strip slid from");
        check(tiku_trk_switcher_scroll_px(&t, 100, t.moved + 1) ==
                  t.scroll_from * 100 + 50, "DB-058",
              "one step in, the strip is half way to the new slot");
        check(tiku_trk_switcher_scroll_px(&t, 100,
                  t.moved + 2 * TIKU_TRK_SW_STEP_US) == t.scroll * 100,
              "DB-058", "and settled, it sits exactly on it");
    }

    printf("\nthe keys move the strip as Ctrl+Tab does (D-008)\n");
    {
        tiku_trk_switcher_t t;
        int64_t t1 = 110000000;
        int i3;

        tiku_trk_switcher_init(&t);
        for (i3 = 0; i3 < TIKU_TRK_SW_SLOTS + 2; i3++) {
            char nm[16];

            snprintf(nm, sizeof nm, "app%d", i3);
            (void)tiku_trk_switcher_app(&t, nm, 0);
        }
        tiku_trk_switcher_open(&t, t1, 0);              /* 0 -> 1 */
        for (i3 = 0; i3 < TIKU_TRK_SW_SLOTS; i3++) {
            (void)tiku_trk_switcher_key(&t, "Right", 0,
                                        t1 + 100000 * (i3 + 1));
        }
        check(t.sel == TIKU_TRK_SW_SLOTS + 1 && t.sel >= t.scroll &&
              t.sel < t.scroll + TIKU_TRK_SW_SLOTS, "D-008",
              "Right past the last visible slot scrolls the strip to it");
        check(tiku_trk_switcher_animating(&t,
                  t1 + 100000 * TIKU_TRK_SW_SLOTS + 1) &&
              t.scroll_from == t.scroll - 1, "D-008",
              "and the strip slides there from where it was");
        (void)tiku_trk_switcher_key(&t, "Right", 0, t1 + 2000000);
        check(t.sel == 0 && t.scroll == 0, "D-008",
              "Right on the last application wraps to the first, and the "
              "strip goes back to the start");
        (void)tiku_trk_switcher_key(&t, "Left", 0, t1 + 3000000);
        check(t.sel == TIKU_TRK_SW_SLOTS + 1 &&
              t.scroll == 2, "D-008",
              "Left on the first wraps to the last, which is visible");
    }

    printf("\n%s\n", fails ? "FAILURES" : "the switcher holds");
    return fails ? 1 : 0;
}
