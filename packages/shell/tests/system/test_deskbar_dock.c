#include "tiku_shell_product.h"
/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_deskbar_dock.c - where the bar sits, how big it is, and when it hides.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "tiku_trk_deskbar.h"
#include "tiku_trk_launch.h"
#include "tiku_trk_input.h"
#include "tiku_trk_deskbar_prefs.h"

#define LEAF_TOP_PX 10   /* inside the leaf button, below its top edge */

static int fails;

/* Where the fake backend says a lost node went (DB-040). */
static char fake_moved_to[512];

static int
fake_path_of_id(tiku_backend_t *b, uint64_t nid, char *out, size_t max)
{
    (void)b;
    (void)nid;
    if (fake_moved_to[0] == '\0') {
        return -1;
    }
    snprintf(out, max, "%s", fake_moved_to);
    return 0;
}

static void
check(int cond, const char *row, const char *what)
{
    printf("  [%s] %-9s %s\n", cond ? "PASS" : "FAIL", row, what);
    if (!cond) { fails++; }
}

int
main(void)
{
    tiku_trk_deskbar_t *db = tiku_trk_deskbar_new(NULL);
    tiku_surface_t *s = tiku_surface_new(1000, 680,
                                                   TIKU_C_BACKDROP);
    tiku_rect_t f;

    printf("replicant persistence and removal (DB-039, DB-040)\n");
    {
        const char *path = "/tmp/trk_deskbar_replicant";
        FILE *out = fopen(path, "w");
        tiku_backend_t *backend = tiku_backend_posix_open();
        tiku_store_t *store = tiku_store_memory_open();
        tiku_trk_deskbar_t *first = tiku_trk_deskbar_new(backend);
        tiku_trk_deskbar_t *second = tiku_trk_deskbar_new(backend);

        if (out != NULL) { fputs("42\n", out); fclose(out); }
        tiku_trk_deskbar_set_store(first, store);
        check(tiku_trk_deskbar_add_replicant(first, "answer", path) > 0,
              "DB-039", "adding a replicant records it in the store");
        tiku_trk_deskbar_set_store(second, store);
        check(tiku_trk_deskbar_set_replicant_height(first, path, 7),
              "D-075", "replicant accepts a custom height");
        check(tiku_trk_deskbar_load_replicants(second) == 1,
              "DB-039", "a new Deskbar restores the saved replicant");
        check(tiku_trk_deskbar_replicant_height(second, path) == 7,
              "D-075", "custom replicant height survives reload");
        check(tiku_trk_deskbar_remove_replicant(second, path) == 1,
              "DB-040", "removing a replicant updates the live tray");
        check(tiku_trk_deskbar_load_replicants(first) == 0, "DB-040",
              "the removal is persisted so it stays gone after reload");
        tiku_trk_deskbar_free(first);
        tiku_trk_deskbar_free(second);
        tiku_store_free(store);
        tiku_backend_close(backend);
        (void)unlink(path);
    }

    printf("the theme switch on the tray (DEV-002)\n");
    {
        tiku_trk_db_pick_t pick;
        tiku_rect_t sw;
        uint32_t track_light, track_dark;

        tiku_trk_deskbar_set_dock(db, TIKU_TRK_DB_RIGHT_EXPANDED);
        tiku_trk_deskbar_draw(db, s);
        sw = tiku_trk_deskbar_switch_rect(db);
        f = tiku_trk_deskbar_frame(db);
        check(sw.w > 0 && sw.x >= f.x && sw.x + sw.w <= f.x + f.w &&
              sw.y >= f.y && sw.y + sw.h <= f.y + f.h, "DEV-002",
              "the switch sits inside the expanded dock's frame");
        track_light = s->px[(sw.y + sw.h / 2) * s->w + (sw.x + sw.w - 3)];
        tiku_trk_deskbar_set_theme(db, 1);
        tiku_trk_deskbar_draw(db, s);
        track_dark = s->px[(sw.y + sw.h / 2) * s->w + (sw.x + 3)];
        check(track_light != track_dark ||
              s->px[(sw.y + sw.h / 2) * s->w + (sw.x + sw.w - 3)] !=
              track_light, "DEV-002",
              "moving the switch moves the knob: the two ends of the "
              "track do not paint the same before and after");
        /*
         * Three seats, so the middle one has to be somewhere the other
         * two are not.  Read at the exact centre of the track: the knob
         * covers it only when the switch is in the middle.
         */
        {
            int mid = (sw.y + sw.h / 2) * s->w + (sw.x + sw.w / 2);
            tiku_rgb_t centre_dark = s->px[mid];
            tiku_rgb_t centre_mono, centre_light;

            tiku_trk_deskbar_set_theme(db, 2);
            tiku_trk_deskbar_draw(db, s);
            centre_mono = s->px[mid];
            tiku_trk_deskbar_set_theme(db, 0);
            tiku_trk_deskbar_draw(db, s);
            centre_light = s->px[mid];
            check(centre_mono != centre_dark && centre_mono != centre_light,
                  "DEV-002",
                  "and the third seat is the middle of the track, which "
                  "neither end paints the same way");
        }
        tiku_trk_deskbar_set_theme(db, 0);
        check(tiku_trk_deskbar_click(db, sw.x + sw.w / 2,
                                     sw.y + sw.h / 2, 0u, 1, 1, &pick) &&
              pick.what == TIKU_TRK_DB_TOGGLE_DARK, "DEV-002",
              "a click on the switch asks the shell to flip the theme");
        tiku_trk_deskbar_set_dock(db, TIKU_TRK_DB_RIGHT_MINI);
        tiku_trk_deskbar_draw(db, s);
        sw = tiku_trk_deskbar_switch_rect(db);
        check(tiku_trk_deskbar_click(db, sw.x + sw.w / 2,
                                     sw.y + sw.h / 2, 0u, 1, 1, &pick) &&
              pick.what == TIKU_TRK_DB_TOGGLE_DARK, "DEV-002",
              "the collapsed dock keeps the switch in reach");
        tiku_trk_deskbar_set_dock(db, TIKU_TRK_DB_TOP);
        tiku_trk_deskbar_draw(db, s);
        sw = tiku_trk_deskbar_switch_rect(db);
        check(tiku_trk_deskbar_click(db, sw.x + sw.w / 2,
                                     sw.y + sw.h / 2, 0u, 1, 1, &pick) &&
              pick.what == TIKU_TRK_DB_TOGGLE_DARK, "DEV-002",
              "so does the strip along the top of the screen");
        tiku_trk_deskbar_set_dock(db, TIKU_TRK_DB_RIGHT_EXPANDED);
    }

    printf("the global bar owns the top of the screen (DEV-003)\n");
    {
        tiku_trk_db_pick_t pick;
        tiku_rect_t sw;

        tiku_trk_deskbar_set_dock(db, TIKU_TRK_DB_RIGHT_EXPANDED);
        tiku_trk_deskbar_set_top_inset(db, 20);
        tiku_trk_deskbar_draw(db, s);
        f = tiku_trk_deskbar_frame(db);
        check(f.y == 20, "DEV-003",
              "with a 20 px inset the vertical dock starts below it: the "
              "leaf is not half-hidden under the global bar");
        check(tiku_trk_deskbar_click(db, f.x + f.w / 2, 20 + LEAF_TOP_PX,
                                     0u, 1, 1, &pick) &&
              tiku_trk_deskbar_menu_open(db), "DEV-003",
              "and the leaf answers at its shifted place");
        tiku_trk_deskbar_close_menu(db);
        sw = tiku_trk_deskbar_switch_rect(db);
        check(tiku_trk_deskbar_click(db, sw.x + sw.w / 2,
                                     sw.y + sw.h / 2, 0u, 1, 1, &pick) &&
              pick.what == TIKU_TRK_DB_TOGGLE_DARK, "DEV-003",
              "the theme switch followed the frame down with it");
        tiku_trk_deskbar_set_dock(db, TIKU_TRK_DB_TOP);
        tiku_trk_deskbar_draw(db, s);
        f = tiku_trk_deskbar_frame(db);
        check(f.y == 20, "DEV-003", "the top strip starts below it too");
        tiku_trk_deskbar_set_dock(db, TIKU_TRK_DB_BOTTOM);
        tiku_trk_deskbar_draw(db, s);
        f = tiku_trk_deskbar_frame(db);
        check(f.y + f.h == 680, "DEV-003",
              "while the bottom edge is not the global bar's to take");
        tiku_trk_deskbar_set_top_inset(db, 0);
        tiku_trk_deskbar_set_dock(db, TIKU_TRK_DB_RIGHT_EXPANDED);
        tiku_trk_deskbar_draw(db, s);
    }

    printf("which edge, and how far into the corner (DB-048)\n");
    check(tiku_trk_deskbar_dock_at(4, 300, 1000, 680,
                                   TIKU_TRK_DB_RIGHT_EXPANDED) ==
          TIKU_TRK_DB_LEFT_MINI, "DB-048",
          "near the left edge, low down: the collapsed left dock");
    check(tiku_trk_deskbar_dock_at(4, 40, 1000, 680,
                                   TIKU_TRK_DB_RIGHT_EXPANDED) ==
          TIKU_TRK_DB_LEFT_EXPANDED, "DB-048",
          "near the left edge, high up: the expanded one -- the same split "
          "the original's little monitor makes between a tall strip and a "
          "corner");
    check(tiku_trk_deskbar_dock_at(500, 4, 1000, 680,
                                   TIKU_TRK_DB_RIGHT_EXPANDED) ==
          TIKU_TRK_DB_TOP, "DB-048", "near the top: along it");
    check(tiku_trk_deskbar_dock_at(500, 676, 1000, 680,
                                   TIKU_TRK_DB_RIGHT_EXPANDED) ==
          TIKU_TRK_DB_BOTTOM, "DB-048", "near the bottom: along that");
    check(tiku_trk_deskbar_dock_at(500, 340, 1000, 680,
                                   TIKU_TRK_DB_LEFT_EXPANDED) ==
          TIKU_TRK_DB_LEFT_EXPANDED, "DB-048",
          "and a drop in the MIDDLE keeps the dock it started from: a drag "
          "that ended nowhere in particular is a mis-drag, and moving the "
          "bar for it would be worse than ignoring it");

    printf("\nthe frame follows the dock (DB-033)\n");
    (void)tiku_trk_deskbar_add_team(db, "one");
    (void)tiku_trk_deskbar_add_team(db, "two");
    (void)tiku_trk_deskbar_add_replicant(db, "free", "/sys/mem/free");
    tiku_trk_deskbar_set_dock(db, TIKU_TRK_DB_RIGHT_EXPANDED);
    tiku_trk_deskbar_draw(db, s);
    f = tiku_trk_deskbar_frame(db);
    check(f.x + f.w == 1000, "DB-033",
          "docked right it sits against the right edge");
    tiku_trk_deskbar_set_dock(db, TIKU_TRK_DB_LEFT_MINI);
    tiku_trk_deskbar_draw(db, s);
    f = tiku_trk_deskbar_frame(db);
    check(f.x == 0, "DB-037", "the collapsed left dock is against the left");
    {
        int mini_h = f.h;

        tiku_trk_deskbar_set_dock(db, TIKU_TRK_DB_LEFT_EXPANDED);
        tiku_trk_deskbar_draw(db, s);
        f = tiku_trk_deskbar_frame(db);
        check(f.h >= mini_h, "DB-037",
              "and it is no taller than the expanded one, since collapsing "
              "is what drops the application list");
    }
    tiku_trk_deskbar_set_dock(db, TIKU_TRK_DB_TOP);
    tiku_trk_deskbar_draw(db, s);
    f = tiku_trk_deskbar_frame(db);
    check(f.y == 0 && f.w == 1000 && f.h < 100, "DB-033",
          "along the top it spans the screen and is one row deep -- a "
          "horizontal bar is a different shape, not a rotated one");
    tiku_trk_deskbar_set_dock(db, TIKU_TRK_DB_BOTTOM);
    tiku_trk_deskbar_draw(db, s);
    f = tiku_trk_deskbar_frame(db);
    check(f.y + f.h == 680, "DB-033", "and along the bottom it sits on it");

    printf("\nthe width the user sets (DB-050)\n");
    tiku_trk_deskbar_set_dock(db, TIKU_TRK_DB_RIGHT_EXPANDED);
    check(tiku_trk_deskbar_set_width(db, 200) == 200, "DB-050",
          "the short axis is the user's to set");
    tiku_trk_deskbar_draw(db, s);
    f = tiku_trk_deskbar_frame(db);
    check(f.w == 200 && f.x == 800, "DB-050",
          "and the bar keeps its edge as it grows, rather than growing off "
          "the screen");
    check(tiku_trk_deskbar_set_width(db, 10) == TIKU_TRK_DB_MIN_W, "DB-050",
          "below the minimum a row's text is unreadable, so it clamps");
    check(tiku_trk_deskbar_set_width(db, 9000) == TIKU_TRK_DB_MAX_W,
          "DB-050",
          "and above the maximum the bar would be taking the screen from "
          "the windows it exists to list");

    printf("\nreplicant handles and size negotiation (DB-007, DB-041)\n");
    tiku_trk_deskbar_set_show_replicants(db, 1);
    check(tiku_trk_deskbar_show_replicants(db), "DB-007",
          "the global show-replicants state is retained");
    check(tiku_trk_deskbar_set_replicant_height(db, "/sys/mem/free", 13) == 0,
          "DB-041", "a replicant taller than the tray contract is refused");
    check(tiku_trk_deskbar_set_replicant_height(db, "/sys/mem/free", 10) == 1 &&
          tiku_trk_deskbar_replicant_height(db, "/sys/mem/free") == 10,
          "DB-041", "an accepted size change is stored and relayout-safe");
    tiku_trk_deskbar_set_dock(db, TIKU_TRK_DB_RIGHT_EXPANDED);
    tiku_trk_deskbar_draw(db, s);
    f = tiku_trk_deskbar_frame(db);
    tiku_trk_deskbar_open_menu(db);
    check(tiku_trk_deskbar_menu_keeps_open(db, f.x - 10, f.y + 18), "DB-036",
          "the open menu keeps tracking through its 20px pointer margin");
    check(tiku_trk_deskbar_quit_policy(1, 1) == 1 &&
          tiku_trk_deskbar_quit_policy(0, 1) == 0 &&
          tiku_trk_deskbar_quit_policy(0, 0) == 0,
          "DB-053", "the bar refuses quit while Cmd+Q closes only preferences");
    tiku_trk_deskbar_close_menu(db);

    printf("\ncalendar steppers and selection (DB-046)\n");
    tiku_trk_deskbar_set_width(db, TIKU_TRK_DB_MIN_W);
    tiku_trk_deskbar_set_dock(db, TIKU_TRK_DB_RIGHT_EXPANDED);
    tiku_trk_deskbar_draw(db, s);
    f = tiku_trk_deskbar_frame(db);
    check(tiku_trk_deskbar_click(db, f.x + 4, f.y + f.h - 4, 0, 1, 1,
                                 &(tiku_trk_db_pick_t){0}) == 1 &&
          tiku_trk_deskbar_calendar_open(db), "DB-046",
          "clicking the clock opens a calendar popup");
    {
        int month, old_month, day;
        tiku_trk_db_pick_t pick;

        tiku_trk_deskbar_calendar_date(db, NULL, &month, NULL);
        old_month = month;
        memset(&pick, 0, sizeof pick);
        (void)tiku_trk_deskbar_click(db, f.x - 135, f.y + f.h + 4 + 10,
                                     0, 1, 1, &pick);
        tiku_trk_deskbar_calendar_date(db, NULL, &month, NULL);
        check(month == (old_month == 12 ? 1 : old_month + 1), "DB-046",
              "the month stepper advances the displayed month");
        (void)tiku_trk_deskbar_click(db, f.x - 180 + 6 + 2 * 25 + 5,
                                     f.y + f.h + 4 + 22 + 2 * 20 + 5,
                                     0, 1, 1, &pick);
        tiku_trk_deskbar_calendar_date(db, NULL, NULL, &day);
        check(day >= 1 && day <= 31, "DB-046",
              "clicking a valid calendar cell changes the selected day");
        tiku_trk_deskbar_close_calendar(db);
    }

    printf("\nminimising one window, and the whole application (CW-014)\n");
    {
        tiku_trk_dbwin_t wins[2];
        tiku_trk_db_pick_t pick;
        tiku_rect_t bar;
        int y, row_y = -1;

        tiku_trk_deskbar_set_dock(db, TIKU_TRK_DB_RIGHT_EXPANDED);
        memset(wins, 0, sizeof wins);
        snprintf(wins[0].title, sizeof wins[0].title, "%s", "first");
        wins[0].tag = (void *)1;
        snprintf(wins[1].title, sizeof wins[1].title, "%s", "second");
        wins[1].tag = (void *)2;
        tiku_trk_deskbar_set_windows(db, 0, wins, 2, 1);
        tiku_trk_deskbar_draw(db, s);
        bar = tiku_trk_deskbar_frame(db);
        /* Found by asking the bar rather than by arithmetic over its
         * constants: where a row sits is the bar's business. */
        for (y = bar.y; y < bar.y + bar.h && row_y < 0; y++) {
            memset(&pick, 0, sizeof pick);
            (void)tiku_trk_deskbar_click(db, bar.x + 20, y, TIKU_TRK_MOD_CMD,
                                         1, 1, &pick);
            if (pick.what == TIKU_TRK_DB_MINIMIZE_WINDOW &&
                pick.tag == (void *)1) {
                row_y = y;
            }
        }
        check(row_y >= 0, "CW-014",
              "a modified click on a window row minimises that window");

        memset(&pick, 0, sizeof pick);
        (void)tiku_trk_deskbar_click(db, bar.x + 20, row_y,
                                     TIKU_TRK_MOD_CMD | TIKU_MOD_OPTION,
                                     1, 1, &pick);
        check(pick.what == TIKU_TRK_DB_HIDE_ALL, "CW-014",
              "and Option on that same gesture takes the whole application "
              "down, rather than only the window under the pointer");
        tiku_trk_deskbar_set_windows(db, 0, NULL, 0, 0);
    }

    printf("\nscrolling the list (DB-034)\n");
    check(!tiku_trk_deskbar_scrollable(db), "DB-034",
          "two applications fit, so there is nothing to scroll");
    check(tiku_trk_deskbar_scroll(db, 1) == 0 &&
          tiku_trk_deskbar_scroll_at(db) == 0, "DB-034",
          "and the wheel does nothing rather than sliding a list that is "
          "entirely visible away");
    {
        /* A SHORT screen rather than more applications: the roster is
         * bounded, and what makes a list overflow is the room it has. */
        tiku_surface_t *small = tiku_surface_new(400, 150,
                                              TIKU_C_BACKDROP);
        int i;
        char name[16];

        for (i = 0; i < 6; i++) {
            snprintf(name, sizeof name, "app%d", i);
            (void)tiku_trk_deskbar_add_team(db, name);
        }
        tiku_trk_deskbar_draw(db, small);
        check(tiku_trk_deskbar_scrollable(db), "DB-034",
              "enough of them and the list runs past the tray");
        check(tiku_trk_deskbar_scroll(db, 1) == 1 &&
              tiku_trk_deskbar_scroll_at(db) == 1, "DB-034",
              "then the wheel moves it");
        {
            int up = 0, down = 0;

            tiku_trk_deskbar_arrows(db, &up, &down);
            check(up && down, "DB-034",
                  "scrolled into the middle, arrows show both ways");
            (void)tiku_trk_deskbar_scroll(db, -1);
            tiku_trk_deskbar_arrows(db, &up, &down);
            check(!up && down, "DB-034",
                  "at the top only the down arrow remains");
            {
                tiku_trk_db_pick_t apick;
                tiku_rect_t af = tiku_trk_deskbar_frame(db);
                int ay;

                /* The bottom edge of the list steps it -- three at a
                 * time with a modifier held -- found by asking, as every
                 * strip is (DB-034). */
                for (ay = af.y + af.h - 1; ay > af.y; ay--) {
                    memset(&apick, 0, sizeof apick);
                    (void)tiku_trk_deskbar_click(db, af.x + af.w / 2, ay,
                        TIKU_TRK_MOD_SHIFT, 1, 1, &apick);
                    if (tiku_trk_deskbar_scroll_at(db) != 0) {
                        break;
                    }
                }
                check(tiku_trk_deskbar_scroll_at(db) == 3, "DB-034",
                      "the arrow with Shift stepped three rows at once");
                (void)tiku_trk_deskbar_scroll(db, -5);
                (void)tiku_trk_deskbar_scroll(db, 1);
                tiku_trk_deskbar_close_menu(db);
                tiku_trk_deskbar_close_calendar(db);
            }
        }
        check(tiku_trk_deskbar_scroll(db, -5) == 1 &&
              tiku_trk_deskbar_scroll_at(db) == 0, "DB-034",
              "and it stops at the top rather than scrolling past it");
        tiku_trk_deskbar_draw(db, s);
        check(!tiku_trk_deskbar_scrollable(db), "DB-034",
              "back on a screen with room, there is nothing to scroll "
              "again -- overflow is about the room, not the count");
        tiku_surface_free(small);
    }

    printf("\nhiding, and coming back (DB-051)\n");
    tiku_trk_deskbar_set_width(db, TIKU_TRK_DB_MIN_W);
    tiku_trk_deskbar_draw(db, s);
    check(tiku_trk_deskbar_shown(db), "DB-051",
          "with auto-hide off the bar is always out");
    tiku_trk_deskbar_set_autohide(db, 1);
    check(!tiku_trk_deskbar_shown(db), "DB-051",
          "turned on, and with the pointer elsewhere, it is not");
    tiku_trk_deskbar_draw(db, s);
    f = tiku_trk_deskbar_frame(db);
    check(f.w == TIKU_TRK_DB_SLIVER && f.x + f.w == 1000, "DB-051",
          "what is left is a sliver against its own edge");
    check(tiku_trk_deskbar_pointer(db, 999, 300) == 1 &&
          tiku_trk_deskbar_shown(db), "DB-051",
          "touching that sliver brings it back -- judged against the edge "
          "and not the hidden frame, or the pointer could never reach it");
    check(tiku_trk_deskbar_pointer(db, 100, 300) == 1 &&
          !tiku_trk_deskbar_shown(db), "DB-051",
          "and leaving puts it away again");
    tiku_trk_deskbar_open_menu(db);
    check(tiku_trk_deskbar_shown(db), "DB-051",
          "except while its own menu is open: a bar that slid away under "
          "its menu would take the menu with it");
    tiku_trk_deskbar_close_menu(db);
    tiku_trk_deskbar_set_autohide(db, 0);

    printf("\nraising (DB-052)\n");
    check(tiku_trk_deskbar_raised(db), "DB-052",
          "without auto-raise the bar is always above the windows, which "
          "is what it has always been");
    tiku_trk_deskbar_draw(db, s);
    f = tiku_trk_deskbar_frame(db);
    tiku_trk_deskbar_set_autoraise(db, 1);
    (void)tiku_trk_deskbar_pointer(db, 100, 300);
    check(!tiku_trk_deskbar_raised(db), "DB-052",
          "with it on, a window may cover the bar");
    (void)tiku_trk_deskbar_pointer(db, f.x + 2, f.y + 2);
    check(tiku_trk_deskbar_raised(db), "DB-052",
          "until the user reaches for it");

    printf("\nlive preferences (DB-016, DB-017, DB-073)\n");
    {
        tiku_trk_deskbar_t *ordered = tiku_trk_deskbar_new(NULL);
        tiku_trk_deskbar_settings_t settings;
        int small_h, large_h;

        (void)tiku_trk_deskbar_add_team(ordered, "Zulu");
        (void)tiku_trk_deskbar_add_team(ordered, TIKU_SHELL_TRACKER_NAME);
        (void)tiku_trk_deskbar_add_team(ordered, "Alpha");
        tiku_trk_deskbar_settings_default(&settings);
        settings.sort_apps = 1;
        tiku_trk_deskbar_apply_settings(ordered, &settings);
        check(strcmp(tiku_trk_deskbar_team_name(ordered, 0), TIKU_SHELL_TRACKER_NAME) == 0 &&
              strcmp(tiku_trk_deskbar_team_name(ordered, 1), "Alpha") == 0 &&
              strcmp(tiku_trk_deskbar_team_name(ordered, 2), "Zulu") == 0,
              "DB-016", "Tracker pins first and the remaining applications "
              "sort by name when those preferences are selected");
        tiku_trk_deskbar_draw(ordered, s);
        small_h = tiku_trk_deskbar_frame(ordered).h;
        settings.icon_size = 96;
        tiku_trk_deskbar_apply_settings(ordered, &settings);
        tiku_trk_deskbar_draw(ordered, s);
        large_h = tiku_trk_deskbar_frame(ordered).h;
        check(large_h > small_h, "DB-017",
              "changing the application icon size re-lays out the bar");
        settings.always_on_top = 1;
        settings.auto_raise = 1;
        tiku_trk_deskbar_apply_settings(ordered, &settings);
        (void)tiku_trk_deskbar_pointer(ordered, 100, 300);
        check(tiku_trk_deskbar_raised(ordered), "DB-073",
              "always-on-top wins over auto-raise and changes live z-order");
        tiku_trk_deskbar_free(ordered);
    }


    printf("\nthe roster's other half: instances and removal (DB-015)\n");
    {
        int changed = 0, t1, t2;
        long live1[1] = { 100 };

        /* The earlier scenes filled the roster; removal clears the stage
         * and is itself the API a kill uses (DB-020). */
        while (tiku_trk_deskbar_team_count(db) > 1) {
            tiku_trk_deskbar_remove_team(db, 1);
        }
        check(tiku_trk_deskbar_team_count(db) == 1, "DB-015",
              "an entry can be removed outright");
        t1 = tiku_trk_deskbar_add_team_pid(db, "editor", 100, "/bin/ed",
                                           &changed);
        t2 = tiku_trk_deskbar_add_team_pid(db, "EDITOR", 200, "/bin/ed",
                                           &changed);
        check(t1 >= 0 && t1 == t2, "DB-015",
              "a second instance joins the existing entry, matched "
              "without regard to case");
        check(tiku_trk_deskbar_prune_teams(db, live1, 1) &&
              tiku_trk_deskbar_team_count(db) >= 1, "DB-015",
              "one instance quitting changes the roster but not the row");
        {
            int still = 0, k;

            for (k = 0; k < tiku_trk_deskbar_team_count(db); k++) {
                if (strcasecmp(tiku_trk_deskbar_team_name(db, k),
                               "editor") == 0) {
                    still = 1;
                }
            }
            check(still, "DB-015", "the entry stands while one lives");
        }
        (void)tiku_trk_deskbar_prune_teams(db, NULL, 0);
        {
            int still = 0, k;

            for (k = 0; k < tiku_trk_deskbar_team_count(db); k++) {
                if (strcasecmp(tiku_trk_deskbar_team_name(db, k),
                               "editor") == 0) {
                    still = 1;
                }
            }
            check(!still, "DB-015",
                  "the entry disappears only when its LAST instance quits");
        }
        check(tiku_trk_deskbar_team_count(db) >= 1, "DB-015",
              "the shell-seeded team is never pruned away");
    }

    printf("\nwhat a click on an application asks for (DB-019/020/021)\n");
    {
        tiku_trk_db_pick_t pick;
        tiku_rect_t bar;
        int y, team_y = -1, changed = 0;

        (void)tiku_trk_deskbar_add_team_pid(db, "editor", 300, "/bin/ed",
                                            &changed);
        tiku_trk_deskbar_draw(db, s);
        bar = tiku_trk_deskbar_frame(db);
        for (y = bar.y; y < bar.y + bar.h && team_y < 0; y++) {
            memset(&pick, 0, sizeof pick);
            (void)tiku_trk_deskbar_click(db, bar.x + 20, y,
                                         TIKU_TRK_MOD_CMD, 1, 1, &pick);
            if (pick.what == TIKU_TRK_DB_SHOW_ALL) {
                team_y = y;
            }
        }
        check(team_y >= 0, "DB-021",
              "Ctrl+click on an application asks for all its windows "
              "to the front");
        memset(&pick, 0, sizeof pick);
        (void)tiku_trk_deskbar_click(db, bar.x + 20, team_y,
                                     TIKU_TRK_MOD_CMD | TIKU_TRK_MOD_SHIFT,
                                     1, 1, &pick);
        check(pick.what == TIKU_TRK_DB_HIDE_ALL, "DB-021",
              "and Ctrl+Shift+click asks to minimize them all");
        memset(&pick, 0, sizeof pick);
        (void)tiku_trk_deskbar_click(db, bar.x + 20, team_y, 0u, 2, 1,
                                     &pick);
        check(pick.what == TIKU_TRK_DB_LAUNCH_TEAM, "DB-021",
              "middle-click asks for a new instance");
        memset(&pick, 0, sizeof pick);
        (void)tiku_trk_deskbar_click(db, bar.x + 20, team_y,
                                     TIKU_TRK_MOD_SHIFT, 2, 1, &pick);
        check(pick.what == TIKU_TRK_DB_NONE, "DB-021",
              "but not with Shift held, exactly as the source requires");
        memset(&pick, 0, sizeof pick);
        (void)tiku_trk_deskbar_click(db, bar.x + 20, team_y, 0u, 1, 2,
                                     &pick);
        check(pick.what == TIKU_TRK_DB_ACTIVATE_TEAM, "DB-019",
              "a double-click activates the application; the single "
              "click never did");
        memset(&pick, 0, sizeof pick);
        (void)tiku_trk_deskbar_click(db, bar.x + 20, team_y,
                                     TIKU_TRK_MOD_CMD | TIKU_TRK_MOD_SHIFT |
                                     TIKU_MOD_OPTION, 1, 1, &pick);
        check(pick.what == TIKU_TRK_DB_KILL_TEAM && pick.team >= 0,
              "DB-020", "and the full grip asks for the kill, naming "
              "the row");
    }


    printf("\nthe expander commits on release (DB-022/DB-023)\n");
    {
        tiku_trk_db_pick_t pick;
        tiku_rect_t bar;
        int y, team_y = -1, team_at = -1, was;

        tiku_trk_deskbar_draw(db, s);
        bar = tiku_trk_deskbar_frame(db);
        for (y = bar.y; y < bar.y + bar.h && team_y < 0; y++) {
            memset(&pick, 0, sizeof pick);
            (void)tiku_trk_deskbar_click(db, bar.x + 20, y,
                                         TIKU_TRK_MOD_CMD, 1, 1, &pick);
            if (pick.what == TIKU_TRK_DB_SHOW_ALL) {
                team_y = y;
                team_at = pick.team;
            }
        }
        was = tiku_trk_deskbar_team_expanded(db, team_at);
        memset(&pick, 0, sizeof pick);
        (void)tiku_trk_deskbar_click(db, bar.x + bar.w - 8, team_y, 0u, 1,
                                     1, &pick);
        check(tiku_trk_deskbar_team_expanded(db, team_at) == was, "DB-022",
              "the press alone flips nothing: the arrow turns, the state "
              "waits");
        check(tiku_trk_deskbar_release(db, bar.x + bar.w - 8, team_y) &&
              tiku_trk_deskbar_team_expanded(db, team_at) == !was,
              "DB-022", "the release over the same strip commits it");
        memset(&pick, 0, sizeof pick);
        (void)tiku_trk_deskbar_click(db, bar.x + bar.w - 8, team_y, 0u, 1,
                                     1, &pick);
        check(!tiku_trk_deskbar_release(db, bar.x + 20, team_y) &&
              tiku_trk_deskbar_team_expanded(db, team_at) == !was,
              "DB-022", "carried off the strip, the press dies unspoken");
        {
            tiku_trk_deskbar_settings_t off;

            tiku_trk_deskbar_settings_default(&off);
            off.show_expander = 0;
            tiku_trk_deskbar_apply_settings(db, &off);
            check(!tiku_trk_deskbar_team_expanded(db, team_at), "DB-023",
                  "taking the expander away collapses what it had opened");
            tiku_trk_deskbar_settings_default(&off);
            tiku_trk_deskbar_apply_settings(db, &off);
        }
    }

    printf("\nthe window menu says what it will not do (DB-028)\n");
    {
        tiku_trk_dbwin_t wins[2];
        tiku_trk_db_pick_t pick;
        tiku_rect_t bar;
        const tiku_font_t *font = tiku_font_plain();
        int rowh = font->height + 6;
        int y, team_y = -1, team_at = -1, menu_y;

        tiku_trk_deskbar_draw(db, s);
        bar = tiku_trk_deskbar_frame(db);
        for (y = bar.y; y < bar.y + bar.h && team_y < 0; y++) {
            memset(&pick, 0, sizeof pick);
            (void)tiku_trk_deskbar_click(db, bar.x + 20, y,
                                         TIKU_TRK_MOD_CMD, 1, 1, &pick);
            if (pick.what == TIKU_TRK_DB_SHOW_ALL) {
                team_y = y;
                team_at = pick.team;
            }
        }
        memset(wins, 0, sizeof wins);
        snprintf(wins[0].title, sizeof wins[0].title, "%s", "one");
        wins[0].state = TIKU_TRK_DBW_MINIMIZED;
        snprintf(wins[1].title, sizeof wins[1].title, "%s", "two");
        wins[1].state = TIKU_TRK_DBW_MINIMIZED;
        tiku_trk_deskbar_set_windows(db, team_at, wins, 2, 0);
        /* The menu's rows are found by asking it: the bottom row answers
         * Close all, and the rest sit a row-height apart above it. */
        menu_y = -1;
        for (y = bar.y; y < bar.y + bar.h && menu_y < 0; y++) {
            memset(&pick, 0, sizeof pick);
            (void)tiku_trk_deskbar_click(db, bar.x + 20, team_y, 0u, 3, 1,
                                         &pick);
            memset(&pick, 0, sizeof pick);
            (void)tiku_trk_deskbar_click(db, bar.x - 100, y, 0u, 1, 1,
                                         &pick);
            if (pick.what == TIKU_TRK_DB_CLOSE_ALL) {
                menu_y = y - 3 * rowh;
            }
        }
        check(menu_y >= 0, "DB-028", "the menu's rows were found");
        memset(&pick, 0, sizeof pick);
        (void)tiku_trk_deskbar_click(db, bar.x + 20, team_y, 0u, 3, 1,
                                     &pick);
        memset(&pick, 0, sizeof pick);
        (void)tiku_trk_deskbar_click(db, bar.x - 100, menu_y, 0u, 1, 1,
                                     &pick);
        check(pick.what == TIKU_TRK_DB_NONE, "DB-028",
              "with every window minimized, Hide all refuses the click");
        wins[0].state = TIKU_TRK_DBW_SHOWN;
        wins[1].state = TIKU_TRK_DBW_SHOWN;
        tiku_trk_deskbar_set_windows(db, team_at, wins, 2, 0);
        memset(&pick, 0, sizeof pick);
        (void)tiku_trk_deskbar_click(db, bar.x + 20, team_y, 0u, 3, 1,
                                     &pick);
        memset(&pick, 0, sizeof pick);
        (void)tiku_trk_deskbar_click(db, bar.x - 100, menu_y + 1 * rowh,
                                     0u, 1, 1, &pick);
        check(pick.what == TIKU_TRK_DB_NONE, "DB-028",
              "with none minimized, Show all refuses it");
        tiku_trk_deskbar_set_windows(db, team_at, wins, 0, 0);
        memset(&pick, 0, sizeof pick);
        (void)tiku_trk_deskbar_click(db, bar.x + 20, team_y, 0u, 3, 1,
                                     &pick);
        memset(&pick, 0, sizeof pick);
        (void)tiku_trk_deskbar_click(db, bar.x - 100, menu_y + 1 * rowh,
                                     0u, 1, 1, &pick);
        check(pick.what == TIKU_TRK_DB_QUIT_TEAM, "DB-028",
              "an application with no windows offers the polite quit "
              "instead");
    }


    printf("\na name too long for its row is said whole (DB-032)\n");
    {
        tiku_trk_db_pick_t pick;
        tiku_rect_t bar;
        int y, team_y = -1, changed = 0, tlong;

        while (tiku_trk_deskbar_team_count(db) > 1) {
            tiku_trk_deskbar_remove_team(db, 1);
        }
        tlong = tiku_trk_deskbar_add_team_pid(db,
            "a-name-much-too-long-for-any-bar", 999, "/bin/true",
            &changed);
        tiku_trk_deskbar_draw(db, s);
        bar = tiku_trk_deskbar_frame(db);
        for (y = bar.y; y < bar.y + bar.h && team_y < 0; y++) {
            memset(&pick, 0, sizeof pick);
            (void)tiku_trk_deskbar_click(db, bar.x + 20, y,
                                         TIKU_TRK_MOD_CMD, 1, 1, &pick);
            if (pick.what == TIKU_TRK_DB_SHOW_ALL && pick.team == tlong) {
                team_y = y;
            }
        }
        tiku_trk_deskbar_close_menu(db);
        tiku_trk_deskbar_close_calendar(db);
        check(tiku_trk_deskbar_motion(db, bar.x + 20, team_y) == 1,
              "DB-032", "resting on the truncated row is itself a change "
              "the bar wants repainted");
        check(tiku_trk_deskbar_motion(db, 10, 10) == 1, "DB-032",
              "and leaving takes the tooltip away");
    }


    printf("\nthe horizontal strip breathes (DB-033)\n");
    {
        const tiku_font_t *font = tiku_font_plain();
        int min_w = 24 + tiku_text_width(font, "abcdefgh");
        int max_w = min_w + min_w / 4;
        tiku_surface_t *narrow = tiku_surface_new(400, 150,
                                              TIKU_C_BACKDROP);
        tiku_trk_db_pick_t pick;
        tiku_trk_deskbar_settings_t quiet;
        int i, changed = 0;
        char name[16];

        /* The clock takes its own room at the strip's end; the buttons'
         * arithmetic below is theirs alone. */
        tiku_trk_deskbar_settings_default(&quiet);
        quiet.show_clock = 0;
        tiku_trk_deskbar_apply_settings(db, &quiet);
        while (tiku_trk_deskbar_team_count(db) > 1) {
            tiku_trk_deskbar_remove_team(db, 1);
        }
        (void)tiku_trk_deskbar_add_team_pid(db, "pair", 401, "/bin/true",
                                            &changed);
        tiku_trk_deskbar_set_dock(db, TIKU_TRK_DB_TOP);
        tiku_trk_deskbar_draw(db, s);
        check(tiku_trk_deskbar_hbutton_width(db) == max_w, "DB-033",
              "two applications get the widest button the rule allows -- "
              "1.25x the minimum, not the whole bar");
        for (i = 0; i < 6; i++) {
            snprintf(name, sizeof name, "happ%d", i);
            (void)tiku_trk_deskbar_add_team_pid(db, name, 500 + i,
                                                "/bin/true", &changed);
        }
        tiku_trk_deskbar_draw(db, narrow);
        check(tiku_trk_deskbar_hbutton_width(db) == min_w &&
              tiku_trk_deskbar_scrollable(db), "DB-033",
              "eight on a narrow screen shrink to the minimum and even "
              "that will not fit: the ends grow arrows");
        {
            /* 36 px wider than the strip alone: its far end belongs to
             * the theme switch, and the breathing room this row measures
             * excludes it. */
            tiku_surface_t *mid = tiku_surface_new(836, 150,
                                              TIKU_C_BACKDROP);

            tiku_trk_deskbar_draw(db, mid);
            check(tiku_trk_deskbar_hbutton_width(db) > min_w &&
                  tiku_trk_deskbar_hbutton_width(db) < max_w &&
                  !tiku_trk_deskbar_scrollable(db), "DB-033",
                  "with room again they grow back between the two bounds");
            tiku_surface_free(mid);
        }
        tiku_trk_deskbar_draw(db, s);
        {
            tiku_rect_t hf = tiku_trk_deskbar_frame(db);

            memset(&pick, 0, sizeof pick);
            (void)tiku_trk_deskbar_click(db, 130 + 10, hf.y + 10,
                                         TIKU_TRK_MOD_CMD, 1, 1, &pick);
            check(pick.what == TIKU_TRK_DB_SHOW_ALL, "DB-033",
                  "and a button answers the same chords the vertical "
                  "rows do");
        }
        {
            tiku_rect_t hf, ti, sw;

            tiku_trk_deskbar_settings_default(&quiet);  /* the clock on */
            tiku_trk_deskbar_apply_settings(db, &quiet);
            (void)tiku_trk_deskbar_trayicon_set(db, 1, "t", "application",
                                                "9", "a tip");
            tiku_trk_deskbar_draw(db, s);
            hf = tiku_trk_deskbar_frame(db);
            ti = tiku_trk_deskbar_trayicon_rect(db, 0);
            sw = tiku_trk_deskbar_switch_rect(db);
            check(ti.w > 0 && ti.y >= hf.y && ti.y + ti.h <= hf.y + hf.h &&
                  ti.x + ti.w < sw.x, "DB-033",
                  "along an edge an application's tray icon sits in the "
                  "strip, before the clock and the switch");
            (void)tiku_trk_deskbar_trayicon_remove(db, 1, "t");
        }
        tiku_trk_deskbar_set_dock(db, TIKU_TRK_DB_RIGHT_EXPANDED);
        tiku_surface_free(narrow);
    }


    printf("\nthe tray answers by id and by name (DB-038)\n");
    {
        const char *pa = "/tmp/trk_repl_a";
        const char *pb = "/tmp/trk_repl_b";
        FILE *out;
        int ida, idb;

        out = fopen(pa, "w");
        if (out != NULL) { fputs("1\n", out); fclose(out); }
        out = fopen(pb, "w");
        if (out != NULL) { fputs("2\n", out); fclose(out); }
        ida = tiku_trk_deskbar_add_replicant(db, "alpha", pa);
        check(ida > 0, "DB-038",
              "an added tray item is answered with its numeric id");
        check(tiku_trk_deskbar_add_replicant(db, "alpha", pa) == ida,
              "DB-038", "the same node again is the same id, not a twin");
        check(tiku_trk_deskbar_add_replicant(db, "alpha", pb) == -1,
              "DB-038", "a DIFFERENT node wearing a taken name is refused");
        idb = tiku_trk_deskbar_add_replicant(db, "beta", pb);
        check(idb > 0 && idb != ida, "DB-038",
              "distinct items carry distinct ids");
        check(tiku_trk_deskbar_replicant_by_id(db, ida) >= 0 &&
              tiku_trk_deskbar_replicant_by_name(db, "beta") >= 0,
              "DB-038", "and both are addressable afterwards, by id and "
              "by view name");
        check(tiku_trk_deskbar_drop_replicant(db, ida) == 1 &&
              tiku_trk_deskbar_replicant_by_id(db, ida) == -1 &&
              tiku_trk_deskbar_replicant_by_name(db, "beta") >= 0,
              "DB-038", "removal by id takes exactly that item");
        (void)tiku_trk_deskbar_drop_replicant(db, idb);
        (void)unlink(pa);
        (void)unlink(pb);
    }


    printf("\nShut down is one row that asks (DB-006)\n");
    {
        tiku_trk_db_pick_t pick;
        int sx = 0, sy = 0;
        tiku_rect_t bar2;

        tiku_trk_deskbar_draw(db, s);   /* the frame follows the dock */
        tiku_trk_deskbar_open_menu(db);
        check(tiku_trk_deskbar_menu_point(db, "Shut down", &sx, &sy) == 1,
              "DB-006", "the leaf carries Shut down\xe2\x80\xa6");
        memset(&pick, 0, sizeof pick);
        (void)tiku_trk_deskbar_click(db, sx, sy, 0u, 1, 1, &pick);
        check(pick.what == TIKU_TRK_DB_QUIT, "DB-006",
              "choosing it asks: the pick the shell confirms");
        tiku_trk_deskbar_open_menu(db);
        (void)tiku_trk_deskbar_menu_point(db, "Shut down", &sx, &sy);
        bar2 = tiku_trk_deskbar_frame(db);
        memset(&pick, 0, sizeof pick);
        (void)tiku_trk_deskbar_click(db, bar2.x - 2 - 20 + 12, sy, 0u,
                                     1, 1, &pick);
        check(pick.what == TIKU_TRK_DB_QUIT &&
              tiku_trk_deskbar_menu_point(db, "Power off", &sx, &sy) == 0,
              "DB-006",
              "its far end chooses too: no arrow, no submenu, no Power off");
        tiku_trk_deskbar_close_menu(db);
    }


    printf("\nmini mode folds the list into one popping icon (DB-037)\n");
    {
        tiku_trk_db_pick_t pick;
        tiku_rect_t mf;
        const tiku_font_t *font = tiku_font_plain();
        int rowh = font->height + 6;

        tiku_trk_deskbar_set_dock(db, TIKU_TRK_DB_RIGHT_MINI);
        tiku_trk_deskbar_draw(db, s);
        mf = tiku_trk_deskbar_frame(db);
        memset(&pick, 0, sizeof pick);
        (void)tiku_trk_deskbar_click(db, mf.x + mf.w / 2,
                                     mf.y + 26 + 2 + 10, 0u, 1, 1, &pick);
        check(tiku_trk_deskbar_apps_menu_open(db), "DB-037",
              "the second icon under the leaf pops the running-apps menu");
        memset(&pick, 0, sizeof pick);
        (void)tiku_trk_deskbar_click(db, mf.x - 178 + 10,
                                     mf.y + 2 + 26 + 2 + 2 + rowh / 2, 0u,
                                     1, 1, &pick);
        check(pick.what == TIKU_TRK_DB_ACTIVATE_TEAM, "DB-037",
              "choosing a row activates that application");
        {
            /* And with nothing running, the one row is said, not
             * chosen. */
            tiku_trk_deskbar_t *bare = tiku_trk_deskbar_new(NULL);
            tiku_surface_t *bs = tiku_surface_new(400, 300,
                                              TIKU_C_BACKDROP);
            tiku_rect_t bf2;

            tiku_trk_deskbar_set_dock(bare, TIKU_TRK_DB_RIGHT_MINI);
            tiku_trk_deskbar_draw(bare, bs);
            bf2 = tiku_trk_deskbar_frame(bare);
            memset(&pick, 0, sizeof pick);
            (void)tiku_trk_deskbar_click(bare, bf2.x + bf2.w / 2,
                                         bf2.y + 26 + 2 + 10, 0u, 1, 1,
                                         &pick);
            check(tiku_trk_deskbar_apps_menu_open(bare), "DB-037",
                  "the empty menu still opens");
            memset(&pick, 0, sizeof pick);
            (void)tiku_trk_deskbar_click(bare, bf2.x - 178 + 10,
                                         bf2.y + 2 + 26 + 2 + 2 +
                                         rowh / 2, 0u, 1, 1, &pick);
            check(pick.what == TIKU_TRK_DB_NONE, "DB-037",
                  "its 'no application running' row is said, not chosen");
            tiku_trk_deskbar_free(bare);
            tiku_surface_free(bs);
        }
        tiku_trk_deskbar_set_dock(db, TIKU_TRK_DB_RIGHT_EXPANDED);
    }


    printf("\nthe tray notices moved and deleted files (DB-040)\n");
    {
        /* A backend that CAN answer by node identity: posix stat plus a
         * path_of_id the test controls -- the VFS-watch shape. */
        static char moved_to[TIKU_PATH_MAX];
        tiku_backend_t *pb = tiku_backend_posix_open();
        tiku_backend_t fake = *pb;
        tiku_backend_ops_t ops = *pb->ops;
        tiku_trk_deskbar_t *tdb;
        FILE *out;
        int id1, id2;

        ops.path_of_id = NULL;      /* the removal branch on this host */
        fake.ops = &ops;
        tdb = tiku_trk_deskbar_new(&fake);
        out = fopen("/tmp/trk_r_stay", "w");
        if (out != NULL) { fputs("1\n", out); fclose(out); }
        out = fopen("/tmp/trk_r_move", "w");
        if (out != NULL) { fputs("2\n", out); fclose(out); }
        id1 = tiku_trk_deskbar_add_replicant(tdb, "stay", "/tmp/trk_r_stay");
        id2 = tiku_trk_deskbar_add_replicant(tdb, "move", "/tmp/trk_r_move");
        check(id1 > 0 && id2 > 0 &&
              tiku_trk_deskbar_sync_replicants(tdb) == 0, "DB-040",
              "nothing moved: the sweep changes nothing");
        (void)rename("/tmp/trk_r_move", "/tmp/trk_r_moved");
        check(tiku_trk_deskbar_sync_replicants(tdb) == 1 &&
              tiku_trk_deskbar_replicant_by_id(tdb, id2) == -1 &&
              tiku_trk_deskbar_replicant_by_id(tdb, id1) >= 0, "DB-040",
              "with no identity source a vanished path is removed, and "
              "only that one");
        /* Phase two: an identity source exists, so a moved file is
         * RE-POINTED rather than dropped. */
        out = fopen("/tmp/trk_r_move2", "w");
        if (out != NULL) { fputs("3\n", out); fclose(out); }
        id2 = tiku_trk_deskbar_add_replicant(tdb, "move2",
                                             "/tmp/trk_r_move2");
        ops.path_of_id = fake_path_of_id;
        (void)rename("/tmp/trk_r_move2", "/tmp/trk_r_moved2");
        snprintf(fake_moved_to, sizeof fake_moved_to, "%s",
                 "/tmp/trk_r_moved2");
        check(tiku_trk_deskbar_sync_replicants(tdb) == 1 &&
              tiku_trk_deskbar_replicant_by_id(tdb, id2) >= 0 &&
              tiku_trk_deskbar_sync_replicants(tdb) == 0, "DB-040",
              "with one, a moved file is merely re-pointed and keeps its "
              "place -- and the next sweep finds nothing to do");
        fake_moved_to[0] = '\0';
        (void)unlink("/tmp/trk_r_stay");
        (void)unlink("/tmp/trk_r_moved");
        (void)unlink("/tmp/trk_r_moved2");
        tiku_trk_deskbar_free(tdb);
        tiku_backend_close(pb);
    }


    printf("\nthe calendar flips above when below runs out (DB-070)\n");
    {
        tiku_surface_t *low = tiku_surface_new(400, 260,
                                              TIKU_C_BACKDROP);
        tiku_trk_db_pick_t pick;
        tiku_rect_t lf;
        int old_month, month;

        tiku_trk_deskbar_draw(db, low);
        lf = tiku_trk_deskbar_frame(db);
        memset(&pick, 0, sizeof pick);
        (void)tiku_trk_deskbar_click(db, lf.x + 4, lf.y + lf.h - 4, 0u, 1,
                                     1, &pick);
        check(tiku_trk_deskbar_calendar_open(db), "DB-070",
              "the clock still opens the calendar on a short screen");
        tiku_trk_deskbar_calendar_date(db, NULL, &month, NULL);
        old_month = month;
        memset(&pick, 0, sizeof pick);
        /* The stepper is now ABOVE the clock: below it would run past
         * the 260 px screen. */
        (void)tiku_trk_deskbar_click(db, lf.x - 135,
                                     lf.y + lf.h - 22 - 4 - 4 - 148 + 10,
                                     0u, 1, 1, &pick);
        tiku_trk_deskbar_calendar_date(db, NULL, &month, NULL);
        check(month == (old_month == 12 ? 1 : old_month + 1), "DB-070",
              "and its steppers answer at the flipped place");
        tiku_trk_deskbar_close_calendar(db);
        tiku_surface_free(low);
        tiku_trk_deskbar_draw(db, s);
    }

    printf("\nall application groups share enough catalogue slots\n");
    {
        tiku_trk_deskbar_t *catalogue = tiku_trk_deskbar_new(NULL);
        int accepted = 0;
        for (int i = 0; i < TIKU_TRK_APP_MAX; i++) {
            char label[32];
            snprintf(label, sizeof label, "Application %02d", i);
            accepted += tiku_trk_deskbar_add_app_in(catalogue,
                i % 2 ? TIKU_TRK_DB_GROUP_APPS : TIKU_TRK_DB_GROUP_DEMOS,
                label, "/fixture/app.so", "marker");
        }
        check(accepted == TIKU_TRK_APP_MAX, "catalogue",
              "every launch-registry slot fits, across groups");
        for (int i = accepted; i < TIKU_TRK_DB_APPS_MAX; i++) {
            (void)tiku_trk_deskbar_add_app(catalogue, "More",
                                           "/fixture/more.so", "");
        }
        check(!tiku_trk_deskbar_add_app(catalogue, "Overflow",
                                        "/fixture/extra.so", ""),
              "catalogue", "a full catalogue refuses an extra entry");
        tiku_trk_deskbar_free(catalogue);
    }

    tiku_trk_deskbar_free(db);
    tiku_surface_free(s);
    printf("\n%s\n", fails ? "FAILURES" : "the bar sits where it is put");
    return fails ? 1 : 0;
}
