/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_alert.c - the question the shell puts, and the progress it shows.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <string.h>

#include "tiku_trk_alert.h"
#include "tiku_event.h"
#include "tiku_strings.h"

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
    tiku_alert_t a;
    tiku_progress_t pr;
    tiku_trk_progress_win_t w;

    printf("the shape of the question (FS-018, FS-019)\n");
    memset(&pr, 0, sizeof pr);
    snprintf(pr.conflict_path, sizeof pr.conflict_path, "/dst/report.txt");
    pr.conflict = 1;
    pr.conflict_many = 0;
    tiku_trk_alert_for_conflict(&a, &pr);
    check(a.open && a.nbutton == 2, "FS-018",
          "a lone file-versus-file collision offers two buttons");
    check(strstr(a.text, "report.txt") != NULL, "FS-018",
          "and names the item, by its leaf rather than its path");
    check(strcmp(a.button[0], "Cancel") == 0, "FS-018",
          "with Cancel leftmost");
    check(a.cancel == 0, "FS-018",
          "which is what Escape presses -- the safe answer is the one a "
          "reflex reaches");

    pr.conflict_many = 1;
    tiku_trk_alert_for_conflict(&a, &pr);
    check(a.nbutton == 4, "FS-019",
          "a folder collision, or several pending, offers the four-way form");

    printf("\nwhat the answer means to the engine\n");
    a.chosen = 3;
    check(tiku_trk_alert_conflict_answer(&a) == TIKU_CONFLICT_REPLACE_ALL,
          "FS-017", "the rightmost of four is Replace all");
    a.chosen = 2;
    check(tiku_trk_alert_conflict_answer(&a) == TIKU_CONFLICT_SKIP_ALL,
          "FS-017", "and the one before it Skip all");
    a.chosen = 0;
    check(tiku_trk_alert_conflict_answer(&a) == TIKU_CONFLICT_SKIP,
          "FS-019", "Cancel skips rather than replacing: the answer that "
          "destroys nothing");
    pr.conflict_many = 0;
    tiku_trk_alert_for_conflict(&a, &pr);
    a.chosen = 1;
    check(tiku_trk_alert_conflict_answer(&a) == TIKU_CONFLICT_REPLACE,
          "FS-018", "and in the short form the second button replaces");

    check(tiku_strings_load_file("assets/strings/fr.xml") > 0,
          "D-109", "load the French alert labels");
    pr.conflict_many = 0;
    tiku_trk_alert_for_conflict(&a, &pr);
    check(tiku_alert_key(&a, TIKU_KEY_RETURN) == 0,
          "D-109", "Return cancels a translated replace question");
    pr.conflict_many = 1;
    tiku_trk_alert_for_conflict(&a, &pr);
    check(tiku_alert_key(&a, TIKU_KEY_RETURN) == 2,
          "D-109", "Return skips all in a translated multi-file conflict");
    check(tiku_alert_key(&a, TIKU_KEY_ESCAPE) == 0,
          "D-109", "Escape still chooses Cancel");
    tiku_strings_reset();

    printf("\nan error that cannot be continued (FS-035)\n");
    snprintf(pr.error, sizeof pr.error, "Error deleting \"x\": busy.");
    pr.error_can_continue = 0;
    tiku_trk_alert_for_error(&a, &pr);
    check(a.nbutton == 1 && a.kind == TIKU_ALERT_STOP, "FS-035",
          "a delete failure offers no way on -- there is nothing to "
          "continue to");
    pr.error_can_continue = 1;
    tiku_trk_alert_for_error(&a, &pr);
    check(a.nbutton == 2, "FS-035",
          "while a copy failure offers to carry on");

    printf("\nwhere the buttons are\n");
    {
        int bx, by, bw, bh, last;

        tiku_alert_button_rect(&a, 1, 400, 160, &bx, &by, &bw, &bh);
        check(bx + bw <= 400 && by + bh <= 160, "FS-035",
              "the default button sits inside the alert");
        tiku_alert_button_rect(&a, 0, 400, 160, &last, NULL, NULL, NULL);
        check(last < bx, "FS-035",
              "and Cancel is to its LEFT: the default is rightmost, where "
              "the pointer already is");
        check(tiku_alert_button_at(&a, 400, 160, bx + 4, by + 4) == 1,
              "FS-035", "a click inside a button finds it");
        check(tiku_alert_button_at(&a, 400, 160, 10, 10) == -1,
              "FS-035", "and one on the text finds nothing");
    }

    printf("\nthe progress window (FS-069, FS-072, FS-075)\n");
    tiku_trk_progress_reset(&w);
    check(!w.open, "FS-072", "with no job running there is no window");
    memset(&pr, 0, sizeof pr);
    pr.op = TIKU_OP_COPY;
    pr.total = 9;
    pr.done = 3;
    snprintf(pr.current, sizeof pr.current, "/src/big.bin");
    tiku_trk_progress_update(&w, 0, &pr, 1, "/dst");
    check(w.open, "FS-072", "the first job opens it");
    check(strcmp(w.row[0].caption, "Copying: ") == 0, "FS-069",
          "the row is captioned by what the job is doing");
    check(w.row[0].done == 3 && w.row[0].total == 9, "FS-069",
          "and carries the n-of-N counter");
    check(strcmp(w.row[0].dest, "/dst") == 0, "FS-069",
          "with the destination the original shows as \"To:\"");

    pr.current[0] = '\0';
    pr.done = 4;
    tiku_trk_progress_update(&w, 0, &pr, 1, "/dst");
    check(strcmp(w.row[0].current, "/src/big.bin") == 0, "FS-075",
          "a repaint that brings no name KEEPS the last one: the name is "
          "the one field a throttled update must not lose");

    {   /* A second concurrent job is a second row in the same window. */
        tiku_progress_t pr2;

        memset(&pr2, 0, sizeof pr2);
        pr2.op = TIKU_OP_TRASH;
        pr2.total = 2;
        tiku_trk_progress_update(&w, 1, &pr2, 1, NULL);
        check(w.open && w.row[0].used && w.row[1].used, "FS-072",
              "two jobs share one window rather than opening two");
        tiku_trk_progress_update(&w, 1, NULL, 0, NULL);
        check(w.open && !w.row[1].used, "FS-072",
              "one finishing takes its row and leaves the window");
        tiku_trk_progress_update(&w, 0, NULL, 0, NULL);
        check(!w.open, "FS-072",
              "and the last one closes it -- it exists only while a job "
              "does");
    }

    printf("\npause and stop (FS-074)\n");
    {
        tiku_progress_t p2;
        int bx, by, bw, bh, sx, r = -1;

        memset(&p2, 0, sizeof p2);
        p2.op = TIKU_OP_COPY;
        p2.total = 5;
        snprintf(p2.current, sizeof p2.current, "/src/x");
        tiku_trk_progress_reset(&w);
        tiku_trk_progress_update(&w, 0, &p2, 1, NULL);

        tiku_trk_progress_button_rect(0, TIKU_TRK_PROGRESS_HIT_PAUSE, 380,
                                      &bx, &by, &bw, &bh);
        tiku_trk_progress_button_rect(0, TIKU_TRK_PROGRESS_HIT_STOP, 380,
                                      &sx, NULL, NULL, NULL);
        check(sx > bx, "FS-074",
              "stop sits outside pause: the destructive button is furthest "
              "from the one being aimed for");
        check(tiku_trk_progress_hit(&w, 380, bx + 2, by + 2, &r) ==
              TIKU_TRK_PROGRESS_HIT_PAUSE && r == 0,
              "FS-074", "a click on pause finds it, and says which row");
        check(tiku_trk_progress_hit(&w, 380, 12, by + 2, NULL) ==
              TIKU_TRK_PROGRESS_HIT_NONE,
              "FS-074", "and a click on the file name hits nothing");
        check(strcmp(tiku_trk_progress_row_text(&w, 0), "/src/x") == 0,
              "FS-074", "a running row shows the file it is working on");
        w.row[0].paused = 1;
        check(strstr(tiku_trk_progress_row_text(&w, 0), "resume") != NULL,
              "FS-074",
              "and a paused one says how to get going again -- a frozen "
              "counter with no explanation reads as a hang");
        check(tiku_trk_progress_hit(&w, 380, bx + 2, by + 2 + 34, NULL) ==
              TIKU_TRK_PROGRESS_HIT_NONE,
              "FS-074", "a row that is not in use has no buttons");
    }


    printf("\nwhen a window is worth showing (FS-070)\n");
    check(tiku_trk_progress_wanted(TIKU_OP_COPY, 1), "FS-070",
          "a copy of one file still shows progress");
    check(!tiku_trk_progress_wanted(TIKU_OP_LINK, 3), "FS-070",
          "three links do not -- a window that appeared and vanished would "
          "be a flash nobody can read");
    check(!tiku_trk_progress_wanted(TIKU_OP_LINK, 10), "FS-070",
          "nor do exactly ten: the rule is MORE than ten");
    check(tiku_trk_progress_wanted(TIKU_OP_LINK, 11), "FS-070",
          "but eleven do, which is where the original draws it");

    printf("\nspeed and the estimate (FS-071)\n");
    {
        tiku_trk_progress_win_t q;
        tiku_progress_t pr2;
        int64_t t0 = 1000000;

        tiku_trk_progress_reset(&q);
        memset(&pr2, 0, sizeof pr2);
        pr2.op = TIKU_OP_COPY;
        pr2.total = 10;
        pr2.done = 2;
        tiku_trk_progress_update(&q, 0, &pr2, 1, NULL);

        tiku_trk_progress_sample(&q, 0, 500, t0);
        check(tiku_trk_progress_speed(&q, 0) == 0, "FS-071",
              "one sample is not a rate -- a speed needs two moments to "
              "be a speed at all");
        check(tiku_trk_progress_eta(&q, 0) == -1, "FS-071",
              "and with no speed there is no estimate, rather than a "
              "division by nothing");
        tiku_trk_progress_reset(&q);
        tiku_trk_progress_update(&q, 0, &pr2, 1, NULL);
        check(tiku_trk_progress_eta(&q, 0) == -1, "FS-071",
              "and a job that has just started has NO estimate rather than "
              "a wild one");

        tiku_trk_progress_sample(&q, 0, 0, t0);
        tiku_trk_progress_sample(&q, 0, 1000000, t0 + 1000000);
        check(tiku_trk_progress_speed(&q, 0) == 1000000, "FS-071",
              "a megabyte in a second is a megabyte a second");
        tiku_trk_progress_sample(&q, 0, 3000000, t0 + 2000000);
        check(tiku_trk_progress_speed(&q, 0) == 1500000, "FS-071",
              "and the speed is a ROLLING MEAN, so one large file does not "
              "set it for the rest of the job");
        {
            int64_t eta = tiku_trk_progress_eta(&q, 0);

            check(eta > 0, "FS-071",
                  "once there is a speed there is an estimate");
        }

        check(tiku_trk_progress_show_speed(0), "FS-071",
              "the line starts on speed");
        check(tiku_trk_progress_show_speed(7000000), "FS-071",
              "and stays there for eight seconds");
        check(!tiku_trk_progress_show_speed(9000000), "FS-071",
              "then shows the estimate");
        check(!tiku_trk_progress_show_speed(11000000), "FS-071",
              "for four");
        check(tiku_trk_progress_show_speed(13000000), "FS-071",
              "and comes back round -- the cycle is shared, since two rows "
              "alternating out of step would be harder to read than one");
    }

    printf("\n%s\n", fails ? "FAILURES" : "the alert and the progress hold");
    return fails ? 1 : 0;
}
