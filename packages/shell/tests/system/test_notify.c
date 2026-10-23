/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_notify.c - change notices, and what a view does with one.
 *
 * Every check names the PVN row it enforces, so a failure says which promise
 * about live updating broke.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "tiku_notify.h"
#include "tiku_trk_poseview.h"

static int fails;

static void
check(int cond, const char *row, const char *what)
{
    printf("  [%s] %-9s %s\n", cond ? "PASS" : "FAIL", row, what);
    if (!cond) { fails++; }
}

static tiku_model_t
node(const char *path, const char *name, uint64_t id, int64_t size)
{
    tiku_model_t m;

    memset(&m, 0, sizeof m);
    snprintf(m.path, sizeof m.path, "%s", path);
    snprintf(m.name, sizeof m.name, "%s", name);
    snprintf(m.facts.meta, sizeof m.facts.meta, "-");
    m.kind = TIKU_KIND_FILE;
    m.node_id = id;
    m.facts.size = size;
    m.facts.perm = TIKU_P_READ | TIKU_P_WRITE;
    return m;
}

static void
touch(const char *dir, const char *name)
{
    char p[512];
    FILE *f;

    snprintf(p, sizeof p, "%s/%s", dir, name);
    f = fopen(p, "w");
    if (f != NULL) { (void)fputs("x", f); (void)fclose(f); }
}

static void
rm_tree(const char *dir)
{
    DIR *d = opendir(dir);
    struct dirent *e;
    char p[512];

    if (d == NULL) { return; }
    while ((e = readdir(d)) != NULL) {
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) {
            continue;
        }
        snprintf(p, sizeof p, "%s/%s", dir, e->d_name);
        (void)remove(p);
    }
    (void)closedir(d);
    (void)rmdir(dir);
}

int
main(void)
{
    const char *dir = "/tmp/trk_notify_test";

    printf("deriving notices from two listings\n");
    {
        tiku_model_t was[3], now[3];
        tiku_evlist_t l;

        was[0] = node("/d/a", "a", 1, 10);
        was[1] = node("/d/b", "b", 2, 20);
        now[0] = was[0];
        now[1] = was[1];

        tiku_evlist_clear(&l);
        check(tiku_notify_diff(was, 2, now, 2, 0, &l) == 0, "PVN-001",
              "an unchanged listing produces no notices at all");

        now[2] = node("/d/c", "c", 3, 30);
        tiku_evlist_clear(&l);
        (void)tiku_notify_diff(was, 2, now, 3, 0, &l);
        check(l.count == 1 && l.ev[0].op == TIKU_EV_CREATED, "PVN-001",
              "a new entry is one creation");

        tiku_evlist_clear(&l);
        (void)tiku_notify_diff(now, 3, was, 2, 0, &l);
        check(l.count == 1 && l.ev[0].op == TIKU_EV_REMOVED, "PVN-001",
              "a vanished entry is one removal");

        /* Same node under a new name is a MOVE, not a remove plus a create:
         * the two-event form destroys and rebuilds the row, losing its
         * selection and its place. */
        now[0] = node("/d/renamed", "renamed", 1, 10);
        now[1] = was[1];
        tiku_evlist_clear(&l);
        (void)tiku_notify_diff(was, 2, now, 2, 0, &l);
        check(l.count == 1 && l.ev[0].op == TIKU_EV_MOVED &&
              strcmp(l.ev[0].from, "/d/a") == 0, "PVN-001",
              "the same node under a new name is ONE move, carrying its old path");

        now[0] = was[0];
        now[0].facts.size = 99;
        now[1] = was[1];
        tiku_evlist_clear(&l);
        (void)tiku_notify_diff(was, 2, now, 2, 0, &l);
        check(l.count == 1 && l.ev[0].op == TIKU_EV_CHANGED, "PVN-001",
              "a changed size is one change");

        /* The three facts that move on their own, with nothing else about
         * the entry changing.  A diff blind to them is a diff that reports
         * nothing, and a row that reports nothing never repaints. */
        now[0] = was[0];
        now[0].facts.trash_full = 1;
        now[1] = was[1];
        tiku_evlist_clear(&l);
        (void)tiku_notify_diff(was, 2, now, 2, 0, &l);
        check(l.count == 1 && l.ev[0].op == TIKU_EV_CHANGED, "AW-029",
              "a Trash filling is a change, though nothing else about the "
              "folder moved -- without this the icon shows the state it "
              "had when the window opened");

        now[0] = was[0];
        now[0].facts.avail = 4096;
        now[1] = was[1];
        tiku_evlist_clear(&l);
        (void)tiku_notify_diff(was, 2, now, 2, 0, &l);
        check(l.count == 1 && l.ev[0].op == TIKU_EV_CHANGED, "AW-097",
              "and so is free space moving, which is the whole of what a "
              "space bar shows");

        now[0] = was[0];
        now[0].facts.total = 8192;
        now[1] = was[1];
        tiku_evlist_clear(&l);
        (void)tiku_notify_diff(was, 2, now, 2, 0, &l);
        check(l.count == 1 && l.ev[0].op == TIKU_EV_CHANGED, "AW-097",
              "as is a capacity, which changes when a volume is replaced "
              "under a mount point that kept its name");

        {
            tiku_evlist_t burst, folded;
            tiku_model_t latest = was[0];

            tiku_evlist_clear(&burst);
            (void)tiku_evlist_add(&burst, TIKU_EV_CHANGED,
                                      &was[0], NULL, 1);
            latest.facts.size = 101;
            (void)tiku_evlist_add(&burst, TIKU_EV_ATTR,
                                      &latest, NULL, 2);
            (void)tiku_evlist_add(&burst, TIKU_EV_CHANGED,
                                      &was[1], NULL, 3);
            (void)tiku_evlist_coalesce(&burst, &folded);
            check(folded.count == 2 && folded.ev[0].model.facts.size == 101,
                  "PVL-094", "a notice burst visits each node once with its final facts");
            check(folded.ev[0].at == 2 && folded.ev[1].at == 3,
                  "PVN-054", "the accumulator keeps the newest arrival time");
        }
    }

    printf("\nparking a notice that arrives before its row\n");
    {
        tiku_park_t park;
        tiku_node_event_t e, out[4];
        tiku_model_t m = node("/d/late", "late", 9, 1);
        int n;

        tiku_park_clear(&park);
        memset(&e, 0, sizeof e);
        e.op = TIKU_EV_CHANGED;
        e.model = m;
        snprintf(e.path, sizeof e.path, "%s", m.path);
        e.at = 1000;
        (void)tiku_park_add(&park, &e);
        e.at = 2000;
        e.model.facts.size = 2;
        (void)tiku_park_add(&park, &e);
        check(tiku_park_count(&park) == 2, "PVN-002",
              "a notice for an unknown row is held, not dropped");

        n = tiku_park_take(&park, "/d/late", out, 4);
        check(n == 2 && out[0].at == 1000 && out[1].at == 2000, "PVN-004",
              "and comes back in arrival order, oldest first");
        check(tiku_park_count(&park) == 0, "PVN-004",
              "taking them empties the park");

        /* Taking fewer than are held must not destroy the rest: the caller
         * never saw them, and they are for the node it just asked about. */
        {
            int k;

            tiku_park_clear(&park);
            for (k = 0; k < 10; k++) {
                e.at = 100 + k;
                (void)tiku_park_add(&park, &e);
            }
            n = tiku_park_take(&park, "/d/late", out, 4);
            check(n == 4, "PVN-004", "a bounded take returns what fits");
            check(tiku_park_count(&park) == 6, "PVN-004",
                  "and leaves the rest held, not destroyed");
            n = tiku_park_take(&park, "/d/late", out, 4);
            check(n == 4 && out[0].at == 104, "PVN-004",
                  "the next take continues where it left off");
        }
        tiku_park_clear(&park);

        /* A row that never appears must not hold its notice for ever. */
        e.at = 0;
        (void)tiku_park_add(&park, &e);
        check(tiku_park_expire(&park, TIKU_PARK_LIFETIME_US - 1) == 0,
              "PVN-003", "a notice survives just under the lifetime");
        check(tiku_park_expire(&park, TIKU_PARK_LIFETIME_US) == 1,
              "PVN-003", "and is discarded at it");
    }

    printf("\nwhat a view does with a notice\n");
    {
        tiku_backend_t *b;
        tiku_trk_poseview_t *v;
        tiku_node_event_t e;
        unsigned f;
        int before;

        rm_tree(dir);
        (void)mkdir(dir, 0755);
        touch(dir, "alpha.txt");
        touch(dir, "beta.txt");
        touch(dir, "gamma.txt");
        b = tiku_backend_posix_open();
        v = tiku_trk_poseview_new(b, dir);
        (void)tiku_trk_poseview_reload(v);
        before = tiku_trk_poseview_count(v);
        check(before == 3, "-", "three rows to start");
        {
            uint64_t menu_before = tiku_trk_poseview_menu_generation(v);

            memset(&e, 0, sizeof e);
            e.op = TIKU_EV_ATTR;
            snprintf(e.path, sizeof e.path, "%s/alpha.txt", dir);
            e.model = tiku_trk_poseview_at(v, 0)->model;
            e.model.facts.size++;
            (void)tiku_trk_poseview_notify(v, &e);
            check(tiku_trk_poseview_menu_generation(v) > menu_before,
                  "PVN-035", "a type/attribute notice invalidates dependent menus");
        }

        /* A creation in some OTHER folder is not this folder's business. */
        memset(&e, 0, sizeof e);
        e.op = TIKU_EV_CREATED;
        e.model = node("/elsewhere/x", "x", 40, 1);
        snprintf(e.path, sizeof e.path, "/elsewhere/x");
        snprintf(e.dir, sizeof e.dir, "/elsewhere");
        (void)tiku_trk_poseview_notify(v, &e);
        check(tiku_trk_poseview_count(v) == before, "PVN-011",
              "a file created in another folder adds no row");

        /* One created here does, at its sorted place. */
        {
            char p[512];

            snprintf(p, sizeof p, "%s/delta.txt", dir);
            e.model = node(p, "delta.txt", 41, 1);
            snprintf(e.path, sizeof e.path, "%s", p);
            snprintf(e.dir, sizeof e.dir, "%s", dir);
            f = tiku_trk_poseview_notify(v, &e);
            check(tiku_trk_poseview_count(v) == before + 1 &&
                  (f & TIKU_TRK_NOTIFY_REDRAW) != 0, "PVN-011",
                  "one created here adds exactly one row");
            {
                /* Assert the PROPERTY, not a hand-computed index: the row
                 * belongs wherever the sort puts it, and a test that hard
                 * codes the slot tests my arithmetic instead. */
                int k, ordered = 1, found = -1;

                for (k = 1; k < tiku_trk_poseview_count(v); k++) {
                    if (strcmp(tiku_trk_poseview_at(v, k - 1)->model.name,
                               tiku_trk_poseview_at(v, k)->model.name) > 0) {
                        ordered = 0;
                    }
                }
                for (k = 0; k < tiku_trk_poseview_count(v); k++) {
                    if (strcmp(tiku_trk_poseview_at(v, k)->model.name,
                               "delta.txt") == 0) {
                        found = k;
                    }
                }
                check(ordered && found > 0 &&
                      found < tiku_trk_poseview_count(v) - 1, "PVN-036",
                      "and lands in sorted position, not at the end");
            }

            /* A second notice for the same file must not double it. */
            (void)tiku_trk_poseview_notify(v, &e);
            check(tiku_trk_poseview_count(v) == before + 1, "PVN-012",
                  "a duplicate creation notice makes no second row");
        }

        /* Removing a row clears everything that pointed at it. */
        {
            tiku_trk_pose_t *p = tiku_trk_poseview_at(v, 1);

            tiku_trk_poseview_click(v, 1, 0u);
            check(tiku_trk_poseview_selection_count(v) == 1, "-",
                  "the row is selected");
            snprintf(e.path, sizeof e.path, "%s", p->model.path);
            e.op = TIKU_EV_REMOVED;
            e.model = p->model;
            (void)tiku_trk_poseview_notify(v, &e);
            check(tiku_trk_poseview_count(v) == before, "PVN-030",
                  "the row goes");
            check(tiku_trk_poseview_selection_count(v) == 0, "PVN-030",
                  "and takes the selection with it");
            check(tiku_trk_poseview_active(v) == NULL, "PVN-030",
                  "and the active pose is cleared, not left pointing at "
                  "whatever slid up");
        }

        /* A change for a row that is not here yet is parked, then replayed
         * when the row arrives. */
        {
            char p[512];

            snprintf(p, sizeof p, "%s/later.txt", dir);
            memset(&e, 0, sizeof e);
            e.op = TIKU_EV_CHANGED;
            e.model = node(p, "later.txt", 55, 4242);
            snprintf(e.path, sizeof e.path, "%s", p);
            snprintf(e.dir, sizeof e.dir, "%s", dir);
            e.at = 100;
            (void)tiku_trk_poseview_notify(v, &e);
            check(tiku_trk_poseview_parked(v) == 1, "PVN-002",
                  "a change for an unknown row is parked");

            e.op = TIKU_EV_CREATED;
            e.model.facts.size = 1;
            (void)tiku_trk_poseview_notify(v, &e);
            check(tiku_trk_poseview_parked(v) == 0, "PVN-004",
                  "creating the row replays what was waiting for it");
            {
                int i, found = -1;

                for (i = 0; i < tiku_trk_poseview_count(v); i++) {
                    if (strcmp(tiku_trk_poseview_at(v, i)->model.name,
                               "later.txt") == 0) {
                        found = i;
                    }
                }
                check(found >= 0 &&
                      tiku_trk_poseview_at(v, found)->model.facts.size == 4242,
                      "PVN-004",
                      "so the row shows the CHANGED state, not the one it "
                      "was created with");
            }
        }

        /* A rename keeps the row's selection and re-places it. */
        {
            int i, at = -1;
            char np[512];

            for (i = 0; i < tiku_trk_poseview_count(v); i++) {
                if (strcmp(tiku_trk_poseview_at(v, i)->model.name,
                           "alpha.txt") == 0) {
                    at = i;
                }
            }
            tiku_trk_poseview_click(v, at, 0u);
            snprintf(np, sizeof np, "%s/zulu.txt", dir);
            memset(&e, 0, sizeof e);
            e.op = TIKU_EV_MOVED;
            e.model = node(np, "zulu.txt", 1, 1);
            snprintf(e.path, sizeof e.path, "%s", np);
            snprintf(e.from, sizeof e.from, "%s/alpha.txt", dir);
            snprintf(e.dir, sizeof e.dir, "%s", dir);
            f = tiku_trk_poseview_notify(v, &e);
            check((f & TIKU_TRK_NOTIFY_REDRAW) != 0, "PVN-033",
                  "a rename redraws");
            for (i = 0, at = -1; i < tiku_trk_poseview_count(v); i++) {
                if (strcmp(tiku_trk_poseview_at(v, i)->model.name,
                           "zulu.txt") == 0) {
                    at = i;
                }
            }
            check(at >= 0, "PVN-033", "the row is under its new name");
            check(at == tiku_trk_poseview_count(v) - 1, "PVN-034",
                  "moved to its sorted place, which for zulu is last");
            check(tiku_trk_poseview_at(v, at)->selected, "PVN-033",
                  "and it is still the selected row");
        }

        /* The folder itself. */
        {
            {
                /* As a real notice arrives: the NEW path in path, the old
                 * one in from.  Matching only path would miss precisely the
                 * case this rule is about. */
                char renamed[700];

                snprintf(renamed, sizeof renamed, "%.400s_renamed", dir);
                memset(&e, 0, sizeof e);
                e.op = TIKU_EV_MOVED;
                snprintf(e.path, sizeof e.path, "%.500s", renamed);
                snprintf(e.from, sizeof e.from, "%s", dir);
                e.model = node(renamed, "renamed", 77, 0);
                f = tiku_trk_poseview_notify(v, &e);
                check((f & TIKU_TRK_NOTIFY_RETITLE) != 0, "PVN-035",
                      "renaming the folder on screen retitles the window");
                check(strcmp(tiku_trk_poseview_path(v), renamed) == 0,
                      "PVN-035", "and the view now knows its new path");
                snprintf(e.path, sizeof e.path, "%.500s", renamed);
                e.from[0] = '\0';
            }

            e.op = TIKU_EV_REMOVED;
            f = tiku_trk_poseview_notify(v, &e);
            check((f & TIKU_TRK_NOTIFY_GONE) != 0, "PVN-024",
                  "deleting the folder on screen closes the window");
        }
        tiku_trk_poseview_free(v);

        /* The root is exempt from that: it has nowhere to go. */
        {
            tiku_trk_poseview_t *rv = tiku_trk_poseview_new(b, "/");

            memset(&e, 0, sizeof e);
            e.op = TIKU_EV_REMOVED;
            snprintf(e.path, sizeof e.path, "/");
            f = tiku_trk_poseview_notify(rv, &e);
            check((f & TIKU_TRK_NOTIFY_GONE) == 0, "PVN-025",
                  "the root window stays open when told its node is gone");
            tiku_trk_poseview_free(rv);
        }
        tiku_backend_close(b);
    }

    printf("\npolling derives the notices from the folder itself\n");
    {
        tiku_backend_t *b;
        tiku_trk_poseview_t *v;
        unsigned f;
        char p[512];

        rm_tree(dir);
        (void)mkdir(dir, 0755);
        touch(dir, "one.txt");
        touch(dir, "two.txt");
        b = tiku_backend_posix_open();
        v = tiku_trk_poseview_new(b, dir);
        (void)tiku_trk_poseview_reload(v);
        tiku_trk_poseview_click(v, 0, 0u);

        touch(dir, "three.txt");
        f = tiku_trk_poseview_poll(v, 1000, NULL, -1, 1);
        check(tiku_trk_poseview_count(v) == 3 &&
              (f & TIKU_TRK_NOTIFY_REDRAW) != 0, "PVN-001",
              "a file appearing on disk becomes a row");
        check(tiku_trk_poseview_at(v, 0)->selected, "PVN-030",
              "and the selection that was there survives it");

        snprintf(p, sizeof p, "%s/two.txt", dir);
        (void)remove(p);
        f = tiku_trk_poseview_poll(v, 2000, NULL, -1, 1);
        check(tiku_trk_poseview_count(v) == 2, "PVN-001",
              "a file removed on disk loses its row");
        check(tiku_trk_poseview_at(v, 0)->selected, "PVN-030",
              "without disturbing the rest");

        f = tiku_trk_poseview_poll(v, 3000, NULL, -1, 1);
        check(f == TIKU_TRK_NOTIFY_NOTHING, "PVN-001",
              "and a quiet folder reports nothing at all");

        tiku_trk_poseview_free(v);
        tiku_backend_close(b);
        rm_tree(dir);
    }

    printf("\na query window updates by the same path as a folder\n");
    {
        tiku_backend_t *b = tiku_backend_posix_open();
        tiku_trk_poseview_t *v = tiku_trk_poseview_new(b, "/");
        tiku_model_t set[3];
        unsigned f;
        int i;

        for (i = 0; i < 3; i++) {
            char p[64];

            snprintf(p, sizeof p, "/found/r%d", i);
            set[i] = node(p, "r", 100u + (unsigned)i, i);
        }
        (void)tiku_trk_poseview_set_results(v, set, 3);
        check(tiku_trk_poseview_count(v) == 3, "PVN-001",
              "a query view holds its results");
        tiku_trk_poseview_click(v, 1, 0u);
        check(tiku_trk_poseview_selection_count(v) == 1, "-",
              "one of them is selected");

        /* One result leaves.  The other two must keep their rows AND the
         * selection: a window rebuilt wholesale loses both. */
        f = tiku_trk_poseview_apply_results(v, set, 2, 1000);
        check(tiku_trk_poseview_count(v) == 2, "PVN-001",
              "re-running the query drops the result that went");
        check((f & TIKU_TRK_NOTIFY_REDRAW) != 0, "PVN-001",
              "and reports it through the handler");
        check(tiku_trk_poseview_selection_count(v) == 1, "PVN-001",
              "while the surviving selection survives");
        tiku_trk_poseview_free(v);
        tiku_backend_close(b);
    }

    printf("\na rename replays what was waiting under the new name\n");
    {
        tiku_backend_t *b;
        tiku_trk_poseview_t *v;
        tiku_node_event_t e;
        char np[512];
        int i, at = -1;

        rm_tree(dir);
        (void)mkdir(dir, 0755);
        touch(dir, "old.txt");
        b = tiku_backend_posix_open();
        v = tiku_trk_poseview_new(b, dir);
        (void)tiku_trk_poseview_reload(v);

        /* A change arrives for the name the row is ABOUT to take. */
        snprintf(np, sizeof np, "%s/new.txt", dir);
        memset(&e, 0, sizeof e);
        e.op = TIKU_EV_CHANGED;
        e.model = node(np, "new.txt", 7, 8888);
        snprintf(e.path, sizeof e.path, "%s", np);
        snprintf(e.dir, sizeof e.dir, "%s", dir);
        (void)tiku_trk_poseview_notify(v, &e);
        check(tiku_trk_poseview_parked(v) == 1, "PVN-004",
              "the notice is parked under a name no row has yet");

        e.op = TIKU_EV_MOVED;
        e.model.facts.size = 1;
        snprintf(e.from, sizeof e.from, "%s/old.txt", dir);
        (void)tiku_trk_poseview_notify(v, &e);
        check(tiku_trk_poseview_parked(v) == 0, "PVN-004",
              "and a rename into that name replays it, as a creation does");
        for (i = 0; i < tiku_trk_poseview_count(v); i++) {
            if (strcmp(tiku_trk_poseview_at(v, i)->model.name,
                       "new.txt") == 0) {
                at = i;
            }
        }
        check(at >= 0 &&
              tiku_trk_poseview_at(v, at)->model.facts.size == 8888,
              "PVN-004",
              "so the renamed row shows the change, not the rename's model");
        tiku_trk_poseview_free(v);
        tiku_backend_close(b);
        rm_tree(dir);
    }

    printf("\nswitching folder takes the old one's state with it\n");
    {
        tiku_backend_t *b;
        tiku_trk_poseview_t *v;
        tiku_node_event_t e;
        char other[300];

        rm_tree(dir);
        (void)mkdir(dir, 0755);
        touch(dir, "a.txt");
        snprintf(other, sizeof other, "%s_two", dir);
        rm_tree(other);
        (void)mkdir(other, 0755);
        touch(other, "b.txt");

        b = tiku_backend_posix_open();
        v = tiku_trk_poseview_new(b, dir);
        (void)tiku_trk_poseview_reload(v);

        memset(&e, 0, sizeof e);
        e.op = TIKU_EV_CHANGED;
        e.model = node("/nowhere/ghost", "ghost", 900, 1);
        snprintf(e.path, sizeof e.path, "/nowhere/ghost");
        (void)tiku_trk_poseview_notify(v, &e);
        check(tiku_trk_poseview_parked(v) == 1, "PVN-009",
              "a notice is parked for the folder being shown");

        (void)tiku_trk_poseview_switch_dir(v, other);
        check(tiku_trk_poseview_parked(v) == 0, "PVN-009",
              "and is dropped on the way out, not carried into the next");

        tiku_trk_poseview_free(v);
        tiku_backend_close(b);
        rm_tree(other);
        rm_tree(dir);
    }

    printf("\nthe op decides the work, and one drain serves every view\n");
    {
        tiku_backend_t *b;
        tiku_trk_poseview_t *v1, *v2;
        tiku_model_change_t ch[4];
        char p[512];
        unsigned f1, f2;

        rm_tree(dir);
        (void)mkdir(dir, 0755);
        touch(dir, "one.txt");
        touch(dir, "two.txt");
        b = tiku_backend_posix_open();
        v1 = tiku_trk_poseview_new(b, dir);
        v2 = tiku_trk_poseview_new(b, dir);
        (void)tiku_trk_poseview_reload(v1);
        (void)tiku_trk_poseview_reload(v2);

        /* A record naming a row this view HOLDS, with a removal op.  Scoring
         * it as handled because the id is known is what leaves a deleted row
         * on screen: only a re-read can say what the folder looks like now. */
        snprintf(p, sizeof p, "%s/two.txt", dir);
        (void)remove(p);
        ch[0].id = tiku_trk_poseview_at(v1, 1)->model.node_id;
        ch[0].op = TIKU_CH_REMOVED;

        f1 = tiku_trk_poseview_poll(v1, 1000, ch, 1, 1);
        check(tiku_trk_poseview_count(v1) == 1 &&
              (f1 & TIKU_TRK_NOTIFY_REDRAW) != 0, "PVN-001",
              "a removal record forces the re-read that finds the row gone");

        /* The same record set reaches the second view.  A view that drained
         * for itself would find the ring already empty and conclude nothing
         * had happened. */
        f2 = tiku_trk_poseview_poll(v2, 1000, ch, 1, 1);
        check(tiku_trk_poseview_count(v2) == 1 &&
              (f2 & TIKU_TRK_NOTIFY_REDRAW) != 0, "PVN-010",
              "and the second window sees it too, from the same drain");

        /* A value move is served in place -- no re-listing could see it. */
        ch[0].id = tiku_trk_poseview_at(v1, 0)->model.node_id;
        ch[0].op = TIKU_CH_CHANGED;
        tiku_trk_poseview_at(v1, 0)->value_valid = 1;
        f1 = tiku_trk_poseview_poll(v1, 2000, ch, 1, 1);
        check(!tiku_trk_poseview_at(v1, 0)->value_valid, "PVN-001",
              "a changed record marks that row's reading stale");

        /* An incomplete answer must re-read rather than be trusted. */
        f1 = tiku_trk_poseview_poll(v1, 3000, ch, 0, 0);
        check(f1 != 0xFFFFFFFFu, "PVN-010",
              "an incomplete answer falls through to a re-read");

        tiku_trk_poseview_free(v1);
        tiku_trk_poseview_free(v2);
        tiku_backend_close(b);
        rm_tree(dir);
    }

    printf("\nan entry that cannot be described is held, not shown\n");
    {
        tiku_backend_t *b;
        tiku_trk_poseview_t *v;
        tiku_node_event_t e;
        char gone[512];

        rm_tree(dir);
        (void)mkdir(dir, 0755);
        touch(dir, "real.txt");
        /* A dangling symlink: lstat succeeds, stat does not. */
        snprintf(gone, sizeof gone, "%s/dangling", dir);
        if (symlink("/nonexistent/target", gone) != 0) { /* fine */ }

        b = tiku_backend_posix_open();
        v = tiku_trk_poseview_new(b, dir);
        (void)tiku_trk_poseview_reload(v);
        {
            int i, broken = -1;

            for (i = 0; i < tiku_trk_poseview_count(v); i++) {
                if (tiku_trk_poseview_at(v, i)->model.facts.link_broken) {
                    broken = i;
                }
            }
            check(broken >= 0, "PVN-027",
                  "a link whose target is gone keeps its row");
            check(broken >= 0 &&
                  strcmp(tiku_model_kind_string(
                             &tiku_trk_poseview_at(v, broken)->model),
                         "Broken link") == 0, "PVN-027",
                  "and says so rather than looking like a live one");
        }

        /*
         * The live path applies the same hold as the first listing.  NOTE:
         * this check cannot currently FAIL, because the posix backend has no
         * way to produce an undescribable entry -- lstat succeeds even for a
         * dangling link.  It is kept as a regression guard for the device
         * backend, where a node the manifest does not describe can occur,
         * and it is not evidence that the hold works.
         */
        {
            int rows_before = tiku_trk_poseview_count(v);
            int z_before = tiku_trk_poseview_zombies(v);

            (void)tiku_trk_poseview_poll(v, 5000, NULL, -1, 1);
            check(tiku_trk_poseview_count(v) == rows_before, "PVN-014",
                  "polling does not turn a held entry into a row");
            check(tiku_trk_poseview_zombies(v) == z_before, "PVN-014",
                  "and it stays held");
        }

        /* An UNKNOWN entry is held back rather than shown. */
        memset(&e, 0, sizeof e);
        e.op = TIKU_EV_CREATED;
        e.model = node("/x/busy", "busy", 77, 0);
        e.model.kind = TIKU_KIND_UNKNOWN;
        snprintf(e.path, sizeof e.path, "%s/busy", dir);
        snprintf(e.model.path, sizeof e.model.path, "%s/busy", dir);
        snprintf(e.dir, sizeof e.dir, "%s", dir);
        {
            int before = tiku_trk_poseview_count(v);

            /* Through the listing path, which is where collect() runs. */
            touch(dir, "busy");
            (void)tiku_trk_poseview_reload(v);
            check(tiku_trk_poseview_count(v) == before + 1, "PVN-014",
                  "a describable entry becomes a row");
        }
        tiku_trk_poseview_free(v);
        tiku_backend_close(b);
        rm_tree(dir);
    }

    printf("\nan entry marked invisible is not shown\n");
    {
        tiku_backend_t *b;
        tiku_trk_poseview_t *v;
        int before, after;

        rm_tree(dir);
        (void)mkdir(dir, 0755);
        touch(dir, "seen.txt");
        touch(dir, "hidden.txt");
        b = tiku_backend_posix_open();
        v = tiku_trk_poseview_new(b, dir);
        (void)tiku_trk_poseview_reload(v);
        before = tiku_trk_poseview_count(v);
        check(before == 2, "-", "both entries list to start");

        {
            /* Mark one invisible in its saved record and reload. */
            int i;

            for (i = 0; i < before; i++) {
                if (strcmp(tiku_trk_poseview_at(v, i)->model.name,
                           "hidden.txt") == 0) {
                    check(tiku_trk_poseview_set_invisible(v, i, 1),
                          "PVN-013", "marking an entry invisible takes");
                }
            }
            (void)tiku_trk_poseview_save_state(v);
            (void)tiku_trk_poseview_reload(v);
            (void)tiku_trk_poseview_restore_state(v);
        }
        after = tiku_trk_poseview_count(v);
        check(after == before - 1, "PVN-013",
              "an entry marked invisible is not listed");
        {
            int i, still = 0;

            for (i = 0; i < after; i++) {
                if (strcmp(tiku_trk_poseview_at(v, i)->model.name,
                           "hidden.txt") == 0) {
                    still = 1;
                }
            }
            check(!still, "PVN-013", "and it really is the marked one");
        }
        tiku_trk_poseview_free(v);
        tiku_backend_close(b);
        rm_tree(dir);
    }

    printf("\nlosing the last visible row scrolls up by one\n");
    {
        tiku_backend_t *b;
        tiku_trk_poseview_t *v;
        tiku_node_event_t e;
        char nm[32], p[512];
        int i;

        rm_tree(dir);
        (void)mkdir(dir, 0755);
        for (i = 0; i < 10; i++) {
            snprintf(nm, sizeof nm, "f%02d.txt", i);
            touch(dir, nm);
        }
        b = tiku_backend_posix_open();
        v = tiku_trk_poseview_new(b, dir);
        (void)tiku_trk_poseview_reload(v);
        /* A window showing four rows, scrolled to the end. */
        tiku_trk_poseview_set_visible_rows(v, 4);
        tiku_trk_poseview_scroll_to(v, 6);
        check(tiku_trk_poseview_scroll(v) == 6, "-", "scrolled to the end");

        snprintf(p, sizeof p, "%s/f09.txt", dir);
        memset(&e, 0, sizeof e);
        e.op = TIKU_EV_REMOVED;
        e.model = node(p, "f09.txt", 9, 0);
        snprintf(e.path, sizeof e.path, "%s", p);
        snprintf(e.dir, sizeof e.dir, "%s", dir);
        (void)tiku_trk_poseview_notify(v, &e);
        check(tiku_trk_poseview_scroll(v) == 5, "PVN-031",
              "losing the last visible row scrolls up by one, not to the end");
        tiku_trk_poseview_free(v);
        tiku_backend_close(b);
        rm_tree(dir);
    }

    printf("\nthe anchors survive a filtered view losing a row\n");
    {
        tiku_backend_t *b;
        tiku_trk_poseview_t *v;
        tiku_node_event_t e;
        char p[512];
        int i, kept = -1;

        rm_tree(dir);
        (void)mkdir(dir, 0755);
        touch(dir, "aa1.txt");
        touch(dir, "aa2.txt");
        touch(dir, "bb1.txt");
        touch(dir, "aa3.txt");
        b = tiku_backend_posix_open();
        v = tiku_trk_poseview_new(b, dir);
        (void)tiku_trk_poseview_reload(v);

        /* Filter to the three aa rows, then anchor on the last of them. */
        tiku_trk_poseview_set_filtering(v, 1);
        tiku_trk_poseview_type_ahead(v, 'a');
        tiku_trk_poseview_type_ahead(v, 'a');
        check(tiku_trk_poseview_count(v) == 3, "-", "three rows shown");
        tiku_trk_poseview_click(v, 2, 0u);
        for (i = 0; i < tiku_trk_poseview_count(v); i++) {
            if (tiku_trk_poseview_at(v, i)->selected) { kept = i; }
        }
        check(kept == 2, "-", "the last shown row is the anchor");

        /*
         * Remove a row that is not shown.  A pivot stored as a display index
         * and corrected as a pose index ends up pointing elsewhere.
         *
         * NOTE: this check has not been shown to FAIL against the previous
         * display-index code -- the attempt to revert it did not apply
         * cleanly, so it is not yet evidence for the index unification.  It
         * stands as a regression guard only.
         */
        snprintf(p, sizeof p, "%s/bb1.txt", dir);
        memset(&e, 0, sizeof e);
        e.op = TIKU_EV_REMOVED;
        e.model = node(p, "bb1.txt", 99, 0);
        snprintf(e.path, sizeof e.path, "%s", p);
        snprintf(e.dir, sizeof e.dir, "%s", dir);
        (void)tiku_trk_poseview_notify(v, &e);

        check(tiku_trk_poseview_count(v) == 3, "PVN-030",
              "the filtered view still shows its three rows");
        /* Extending from the anchor must still reach the anchored row. */
        tiku_trk_poseview_click(v, 0, TIKU_TRK_MOD_SHIFT);
        {
            int sel = tiku_trk_poseview_selection_count(v);

            check(sel == 3, "PVN-030",
                  "and shift-extending from the anchor spans all three");
        }
        tiku_trk_poseview_type_clear(v);
        tiku_trk_poseview_set_filtering(v, 0);
        tiku_trk_poseview_free(v);
        tiku_backend_close(b);
        rm_tree(dir);
    }

    printf("\nmoving a row OUT leaves by the same door as removing it\n");
    {
        tiku_backend_t *b;
        tiku_trk_poseview_t *v;
        tiku_node_event_t e;
        char p[512];

        rm_tree(dir);
        (void)mkdir(dir, 0755);
        touch(dir, "aa1.txt");
        touch(dir, "aa2.txt");
        b = tiku_backend_posix_open();
        v = tiku_trk_poseview_new(b, dir);
        (void)tiku_trk_poseview_reload(v);
        tiku_trk_poseview_set_filtering(v, 1);
        tiku_trk_poseview_type_ahead(v, 'a');
        check(tiku_trk_poseview_count(v) == 2, "-", "two rows shown");

        snprintf(p, sizeof p, "%s/aa1.txt", dir);
        memset(&e, 0, sizeof e);
        e.op = TIKU_EV_MOVED;
        e.model = node("/elsewhere/aa1.txt", "aa1.txt", 1, 0);
        snprintf(e.path, sizeof e.path, "/elsewhere/aa1.txt");
        snprintf(e.from, sizeof e.from, "%s", p);
        snprintf(e.dir, sizeof e.dir, "/elsewhere");
        (void)tiku_trk_poseview_notify(v, &e);

        /* A move OUT is a removal by another name, so the filtered list must
         * be rebuilt exactly as a removal rebuilds it. */
        check(tiku_trk_poseview_count(v) == 1, "PVN-038",
              "a row moved out of a FILTERED view really leaves it");
        {
            tiku_trk_pose_t *row = tiku_trk_poseview_at(v, 0);

            check(row != NULL && strcmp(row->model.name, "aa2.txt") == 0,
                  "PVN-038", "and the row still shown is the one that stayed");
        }
        tiku_trk_poseview_type_clear(v);
        tiku_trk_poseview_set_filtering(v, 0);
        tiku_trk_poseview_free(v);
        tiku_backend_close(b);
        rm_tree(dir);
    }

    printf("\na hidden row stays hidden through a bare reload\n");
    {
        tiku_backend_t *b;
        tiku_trk_poseview_t *v;
        int i;

        rm_tree(dir);
        (void)mkdir(dir, 0755);
        touch(dir, "seen.txt");
        touch(dir, "hid.txt");
        b = tiku_backend_posix_open();
        v = tiku_trk_poseview_new(b, dir);
        (void)tiku_trk_poseview_reload(v);
        for (i = 0; i < tiku_trk_poseview_count(v); i++) {
            if (strcmp(tiku_trk_poseview_at(v, i)->model.name,
                       "hid.txt") == 0) {
                (void)tiku_trk_poseview_set_invisible(v, i, 1);
            }
        }
        (void)tiku_trk_poseview_save_state(v);
        /* A BARE reload -- the path poll() takes on overflow, with no
         * restore after it.  The hidden row must not come back. */
        (void)tiku_trk_poseview_reload(v);
        {
            int back = 0;

            for (i = 0; i < tiku_trk_poseview_count(v); i++) {
                if (strcmp(tiku_trk_poseview_at(v, i)->model.name,
                           "hid.txt") == 0) {
                    back = 1;
                }
            }
            check(!back, "PVN-013",
                  "a bare reload does not resurrect a hidden row");
        }
        tiku_trk_poseview_free(v);
        tiku_backend_close(b);
        rm_tree(dir);
    }

    printf("\na rename does not move the scroll\n");
    {
        tiku_backend_t *b;
        tiku_trk_poseview_t *v;
        tiku_node_event_t e;
        char nm[32], p[512], np[512];
        int i;

        rm_tree(dir);
        (void)mkdir(dir, 0755);
        for (i = 0; i < 10; i++) {
            snprintf(nm, sizeof nm, "g%02d.txt", i);
            touch(dir, nm);
        }
        b = tiku_backend_posix_open();
        v = tiku_trk_poseview_new(b, dir);
        (void)tiku_trk_poseview_reload(v);
        tiku_trk_poseview_set_visible_rows(v, 4);
        tiku_trk_poseview_scroll_to(v, 6);

        /* Rename a row IN PLACE: nothing about what is visible changes, so
         * the scroll must not move -- but the rename is remove+insert, and
         * the removal half carries a scroll correction. */
        snprintf(p, sizeof p, "%s/g02.txt", dir);
        snprintf(np, sizeof np, "%s/g02x.txt", dir);
        memset(&e, 0, sizeof e);
        e.op = TIKU_EV_MOVED;
        e.model = node(np, "g02x.txt", 2, 0);
        snprintf(e.path, sizeof e.path, "%s", np);
        snprintf(e.from, sizeof e.from, "%s", p);
        snprintf(e.dir, sizeof e.dir, "%s", dir);
        (void)tiku_trk_poseview_notify(v, &e);
        check(tiku_trk_poseview_scroll(v) == 6, "PVN-022",
              "an in-place rename leaves the scroll where it was");
        tiku_trk_poseview_free(v);
        tiku_backend_close(b);
        rm_tree(dir);
    }

    printf("\na second sighting of the same node is not a second row\n");
    {
        tiku_backend_t *b;
        tiku_trk_poseview_t *v;
        tiku_node_event_t e;
        char p[512], p2[512];
        int before;

        rm_tree(dir);
        (void)mkdir(dir, 0755);
        touch(dir, "one.txt");
        b = tiku_backend_posix_open();
        v = tiku_trk_poseview_new(b, dir);
        (void)tiku_trk_poseview_reload(v);
        before = tiku_trk_poseview_count(v);

        /* A creation notice under a DIFFERENT name but the same identity:
         * the path is a name, the node is the thing. */
        snprintf(p2, sizeof p2, "%s/alias.txt", dir);
        memset(&e, 0, sizeof e);
        e.op = TIKU_EV_CREATED;
        e.model = node(p2, "alias.txt",
                       tiku_trk_poseview_at(v, 0)->model.node_id, 1);
        snprintf(e.path, sizeof e.path, "%s", p2);
        snprintf(e.dir, sizeof e.dir, "%s", dir);
        (void)tiku_trk_poseview_notify(v, &e);
        check(tiku_trk_poseview_count(v) == before, "PVN-012",
              "the same identity under another name adds no second row");
        (void)p;
        tiku_trk_poseview_free(v);
        tiku_backend_close(b);
        rm_tree(dir);
    }

    printf("\na position asked for before the entry exists (PVL-025)\n");
    {
        tiku_backend_t *b;
        tiku_trk_poseview_t *v;
        tiku_node_event_t e;
        char np[512];
        int x = 0, y = 0;

        rm_tree(dir);
        (void)mkdir(dir, 0755);
        touch(dir, "one.txt");
        b = tiku_backend_posix_open();
        v = tiku_trk_poseview_new(b, dir);
        (void)tiku_trk_poseview_reload(v);
        tiku_trk_poseview_set_mode(v, TIKU_TRK_MODE_ICON);

        snprintf(np, sizeof np, "%s/made.txt", dir);
        tiku_trk_poseview_set_drop_point(v, 140, 90);
        memset(&e, 0, sizeof e);
        e.op = TIKU_EV_CREATED;
        e.model = node(np, "made.txt", 4242u, 1);
        snprintf(e.path, sizeof e.path, "%s", np);
        snprintf(e.dir, sizeof e.dir, "%s", dir);
        (void)tiku_trk_poseview_notify(v, &e);
        {
            int i, n = tiku_trk_poseview_count(v), found = 0;

            for (i = 0; i < n; i++) {
                tiku_trk_pose_t *q = tiku_trk_poseview_at(v, i);

                if (strcmp(q->model.name, "made.txt") == 0) {
                    found = q->placed;
                    tiku_trk_poseview_pose_location(v, q, &x, &y);
                }
            }
            check(found && x == 140 && y == 90, "PVL-025",
                  "the new entry appears where the click asked for it");
        }
        /* And the seed is spent: the NEXT entry is not put there too. */
        snprintf(np, sizeof np, "%s/second.txt", dir);
        memset(&e, 0, sizeof e);
        e.op = TIKU_EV_CREATED;
        e.model = node(np, "second.txt", 4243u, 1);
        snprintf(e.path, sizeof e.path, "%s", np);
        snprintf(e.dir, sizeof e.dir, "%s", dir);
        (void)tiku_trk_poseview_notify(v, &e);
        {
            int i, n = tiku_trk_poseview_count(v), placed = 1;

            for (i = 0; i < n; i++) {
                tiku_trk_pose_t *q = tiku_trk_poseview_at(v, i);

                if (strcmp(q->model.name, "second.txt") == 0) {
                    placed = q->placed;
                }
            }
            check(!placed, "PVL-098",
                  "and the seed is spent, so the next one is not put there "
                  "as well");
        }
        tiku_trk_poseview_free(v);
        tiku_backend_close(b);
        rm_tree(dir);
    }

    printf("\nan ancestor moving takes the window with it (PVN-057)\n");
    {
        tiku_backend_t *b;
        tiku_trk_poseview_t *v;
        tiku_node_event_t e;
        char sub[512];
        unsigned f;

        rm_tree(dir);
        (void)mkdir(dir, 0755);
        snprintf(sub, sizeof sub, "%s/inner", dir);
        (void)mkdir(sub, 0755);
        touch(sub, "one.txt");
        b = tiku_backend_posix_open();
        v = tiku_trk_poseview_new(b, sub);

        /* The PARENT is renamed.  Nothing in this folder moved. */
        memset(&e, 0, sizeof e);
        e.op = TIKU_EV_MOVED;
        e.model = node("/tmp/trk_notify_moved", "trk_notify_moved", 991u, 0);
        snprintf(e.path, sizeof e.path, "/tmp/trk_notify_moved");
        snprintf(e.from, sizeof e.from, "%s", dir);
        snprintf(e.dir, sizeof e.dir, "/tmp");
        f = tiku_trk_poseview_notify(v, &e);
        check((f & TIKU_TRK_NOTIFY_RETITLE) != 0u, "PVN-057",
              "renaming an ancestor retitles the window");
        check(strcmp(tiku_trk_poseview_path(v),
                     "/tmp/trk_notify_moved/inner") == 0, "PVN-057",
              "and the window's own path is rebuilt under the new name");

        /* A sibling whose name merely starts with the folder name is not an ancestor. */
        memset(&e, 0, sizeof e);
        e.op = TIKU_EV_MOVED;
        e.model = node("/tmp/elsewhere", "elsewhere", 992u, 0);
        snprintf(e.path, sizeof e.path, "/tmp/elsewhere");
        snprintf(e.from, sizeof e.from, "/tmp/trk_notify_moved/inn");
        snprintf(e.dir, sizeof e.dir, "/tmp");
        f = tiku_trk_poseview_notify(v, &e);
        check((f & TIKU_TRK_NOTIFY_RETITLE) == 0u &&
              strcmp(tiku_trk_poseview_path(v),
                     "/tmp/trk_notify_moved/inner") == 0, "PVN-057",
              "but a path that is only a text prefix is not an ancestor, "
              "so \"inn\" moving leaves \"inner\" alone");

        tiku_trk_poseview_free(v);
        tiku_backend_close(b);
        /* Nothing was renamed on DISK -- the notice was synthetic -- so the
         * cleanup is of the paths that really exist.  Left behind, the
         * extra entry breaks this suite's earlier cases on the next run. */
        {
            char f[600];

            snprintf(f, sizeof f, "%s/one.txt", sub);
            (void)unlink(f);
        }
        (void)rmdir(sub);
        rm_tree(dir);
    }

    printf("\n%s\n", fails ? "FAILURES" : "every notification rule holds");
    return fails ? 1 : 0;
}
