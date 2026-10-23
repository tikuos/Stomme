#include "tiku_shell_product.h"
/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_launch.c - which application opens a document, and running it.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#include "tiku_trk_launch.h"
#include "tiku_state.h"

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
    tiku_trk_apps_t r;
    tiku_store_t *st = tiku_store_memory_open();
    tiku_trk_cand_t cand[TIKU_TRK_APP_MAX];
    const char *one[1];
    const char *two[2];
    int n;

    printf("how an application matches a type (MA-017)\n");
    tiku_trk_launch_clear(&r);
    (void)tiku_trk_launch_add(&r, "Writer", "/bin/true", "text/plain");
    (void)tiku_trk_launch_add(&r, "Reader", "/bin/true", "text/");
    (void)tiku_trk_launch_add(&r, "Anything", "/bin/true", "*");
    (void)tiku_trk_launch_add(&r, "Painter", "/bin/true", "image/png");

    check(tiku_trk_launch_relation(&r.app[0], "text/plain") ==
          TIKU_TRK_REL_EXACT, "MA-017",
          "naming the type is the strongest match there is");
    check(tiku_trk_launch_relation(&r.app[1], "text/plain") ==
          TIKU_TRK_REL_SUPERTYPE, "MA-017",
          "claiming the supertype matches, more weakly");
    check(tiku_trk_launch_relation(&r.app[2], "text/plain") ==
          TIKU_TRK_REL_SUPERHANDLER, "MA-017",
          "and an application that takes anything matches weakest of all");
    check(tiku_trk_launch_relation(&r.app[3], "text/plain") ==
          TIKU_TRK_REL_NONE, "MA-017",
          "one that claims another type does not match at all");
    check(tiku_trk_launch_relation(&r.app[1], "text/") ==
          TIKU_TRK_REL_EXACT, "MA-017",
          "a supertype claim is exact against the supertype itself");

    printf("\nthe list a selection offers (AW-040, AW-042)\n");
    one[0] = "text/plain";
    n = tiku_trk_launch_candidates(&r, one, 1, st, cand, TIKU_TRK_APP_MAX);
    check(n == 3, "AW-040",
          "every application that can open it is offered, and the one that "
          "cannot is not");
    check(strcmp(cand[0].app->name, "Writer") == 0 &&
          strcmp(cand[1].app->name, "Reader") == 0 &&
          strcmp(cand[2].app->name, "Anything") == 0, "AW-042",
          "best match first, so the order is an explanation rather than an "
          "accident");
    check(strcmp(tiku_trk_launch_relation_label(cand[1].rel),
                 "opens this kind of file") == 0, "MA-081",
          "and each row can say in words WHY it is there");

    two[0] = "text/plain";
    two[1] = "image/png";
    n = tiku_trk_launch_candidates(&r, two, 2, st, cand, TIKU_TRK_APP_MAX);
    check(n == 4, "MA-080",
          "over a mixed selection the list is the UNION: an application "
          "that opens either one is offered, which is what makes a mixed "
          "selection openable at all");

    printf("\nthe handler the user chose (AW-043, AW-044)\n");
    (void)tiku_trk_launch_set_preferred(st, "text/plain", "Anything");
    n = tiku_trk_launch_candidates(&r, one, 1, st, cand, TIKU_TRK_APP_MAX);
    check(strcmp(cand[0].app->name, "Anything") == 0 && cand[0].preferred,
          "AW-044",
          "the chosen handler comes first even when something else matches "
          "the type better -- it is a choice, not a match");
    check(tiku_trk_launch_agreed(&r, one, 1, st) != NULL, "AW-043",
          "one type agrees with itself, so there is a handler to "
          "preselect");
    check(tiku_trk_launch_agreed(&r, two, 2, st) == NULL, "AW-043",
          "two types wanting different applications agree on nothing, and "
          "a preselection would be a guess");
    (void)tiku_trk_launch_set_preferred(st, "image/png", "Anything");
    check(tiku_trk_launch_agreed(&r, two, 2, st) != NULL, "AW-043",
          "made to agree, they have an answer again");
    {
        /* A real type of the length the office formats reach: the store
         * keeps the whole name or the choice is written and never
         * found again. */
        const char *lengthy = "application/vnd.oasis.opendocument.text";
        char back[TIKU_TRK_APP_NAME_MAX];

        check(strlen(lengthy) > 31u, "AW-044",
              "the type is longer than a short name would hold");
        (void)tiku_trk_launch_set_preferred(st, lengthy, "Anything");
        (void)tiku_trk_launch_preferred(st, lengthy, back, sizeof back);
        check(strcmp(back, "Anything") == 0, "AW-044",
              "and a choice saved for it comes back whole");
    }

    printf("\nrunning it (AW-002, AW-035, AW-038, AW-039)\n");
    {
        const char *paths[2];
        char script[128];
        char stamp[128];
        FILE *f;
        int i;

        snprintf(script, sizeof script, "/tmp/trk_launch_%d.sh",
                 (int)getpid());
        snprintf(stamp, sizeof stamp, "/tmp/trk_launch_%d.stamp",
                 (int)getpid());
        f = fopen(script, "w");
        if (f != NULL) {
            /* It writes down what it was handed, so the test can check the
             * arguments an application really receives. */
            fprintf(f, "#!/bin/sh\nprintf '%%s\\n' \"$@\" > %s\n", stamp);
            (void)fclose(f);
            (void)chmod(script, 0755);
        }
        tiku_trk_launch_clear(&r);
        (void)tiku_trk_launch_add(&r, "Stamper", script, "text/plain");
        snprintf(r.app[0].path, sizeof r.app[0].path, "%s", script);
        paths[0] = "/tmp/trk_launch_doc.txt";
        paths[1] = script;      /* the application's own file */
        check(tiku_trk_launch_run(&r.app[0], paths, 2) ==
              TIKU_TRK_LAUNCH_OK, "AW-036",
              "a launch reports at once rather than waiting for what it "
              "started");
        for (i = 0; i < 50; i++) {
            f = fopen(stamp, "r");
            if (f != NULL) { break; }
            usleep(20000);
        }
        if (f != NULL) {
            char got[256] = "";
            size_t rd = fread(got, 1u, sizeof got - 1u, f);

            got[rd] = '\0';
            (void)fclose(f);
            check(strstr(got, "/tmp/trk_launch_doc.txt") != NULL, "AW-036",
                  "the application really ran, and was handed the document");
            check(strstr(got, script) == NULL, "AW-038",
                  "but not its own file: an application never opens itself");
        } else {
            check(0, "AW-036", "the application really ran");
            check(0, "AW-038", "and was not handed its own file");
        }
        (void)unlink(script);
        (void)unlink(stamp);
    }
    {
        tiku_trk_app_t self;

        memset(&self, 0, sizeof self);
        snprintf(self.name, sizeof self.name, TIKU_SHELL_TRACKER_NAME);
        snprintf(self.cmd, sizeof self.cmd, "/bin/true");
        self.is_tracker = 1;
        check(tiku_trk_launch_run(&self, NULL, 0) ==
              TIKU_TRK_LAUNCH_IS_TRACKER, "AW-035",
              "choosing this Tracker is not a launch: the running one "
              "takes it rather than a second copy appearing");
        check(tiku_trk_launch_reason(TIKU_TRK_LAUNCH_NO_APP)[0] != '\0' &&
              tiku_trk_launch_reason(TIKU_TRK_LAUNCH_NOTHING)[0] != '\0' &&
              tiku_trk_launch_reason(TIKU_TRK_LAUNCH_FAILED)[0] != '\0' &&
              strcmp(tiku_trk_launch_reason(TIKU_TRK_LAUNCH_NO_APP),
                     tiku_trk_launch_reason(TIKU_TRK_LAUNCH_NOTHING)) != 0 &&
              strcmp(tiku_trk_launch_reason(TIKU_TRK_LAUNCH_NOTHING),
                     tiku_trk_launch_reason(TIKU_TRK_LAUNCH_FAILED)) != 0,
              "AW-039",
              "each failure says WHICH thing went wrong in its OWN words: "
              "nothing claims the type, nothing to open, could not start "
              "-- three distinct messages, not one");
    }
    {
        /* The desktop registers itself with no command. */
        tiku_trk_apps_t own;
        int at;

        tiku_trk_launch_clear(&own);
        at = tiku_trk_launch_add(&own, TIKU_SHELL_TRACKER_NAME, "", "inode/directory");
        if (at >= 0) {
            own.app[at].is_tracker = 1;
        }
        check(at >= 0 && tiku_trk_launch_run(&own.app[at], NULL, 0) ==
              TIKU_TRK_LAUNCH_IS_TRACKER, "AW-035",
              "this Tracker registered with no command is still this "
              "Tracker, not an application that is missing");
    }

    printf("\nthe registry as a file (AW-013)\n");
    {
        char path[128];
        FILE *f;

        snprintf(path, sizeof path, "/tmp/trk_apps_%d", (int)getpid());
        f = fopen(path, "w");
        if (f != NULL) {
            fprintf(f, "# comment\n\nEditor\t/bin/ed\ttext/, text/plain\n"
                       "Viewer\t/bin/cat\t*\nbroken line with no command\n");
            (void)fclose(f);
        }
        tiku_trk_launch_clear(&r);
        check(tiku_trk_launch_load(&r, path) == 2, "AW-013",
              "a registry file is read, comments and blank lines are not "
              "applications, and a line naming no command is not one "
              "either");
        check(r.app[0].ntype == 2 && r.app[1].takes_anything, "AW-013",
              "an application's claims come across, and '*' is the claim "
              "on everything");
        (void)unlink(path);
        check(tiku_trk_launch_load(&r, "/tmp/there-is-no-such-registry")
              == -1, "AW-013",
              "and a registry that is not there is said so rather than "
              "read as empty");
    }

    printf("\na thrown-away application leaves the list (AW-041)\n");
    {
        char path[128];
        FILE *f;

        snprintf(path, sizeof path, "/tmp/trk_apps2_%d", (int)getpid());
        f = fopen(path, "w");
        if (f != NULL) {
            fprintf(f, "Ghost\t/tmp/no-such-program-anywhere\ttext/plain\n"
                       "Real\t/bin/cat\ttext/plain\n"
                       "ByPath\tsome-tool-on-path\ttext/plain\n");
            (void)fclose(f);
        }
        tiku_trk_launch_clear(&r);
        check(tiku_trk_launch_load(&r, path) == 2, "AW-041",
              "an absolute command whose file is gone never reaches the "
              "list; a PATH-resolved name is not second-guessed");
        check(strcmp(r.app[0].name, "Real") == 0, "AW-041",
              "and what survives is the application that still exists");
        (void)unlink(path);
    }

    tiku_store_free(st);
    printf("\nan executable is its own application\n");
    {
        /*
         * The registry answers which application opens a DOCUMENT.  Asked
         * about a program it has nothing to say, and a shell that stops
         * there makes the one file in a folder guaranteed to be runnable
         * the one that will not run.
         */
        tiku_trk_app_t self;
        tiku_model_t m;

        memset(&m, 0, sizeof m);
        snprintf(m.path, sizeof m.path, "%s", "/tmp/trk_launch_self");
        snprintf(m.name, sizeof m.name, "%s", "trk_launch_self");
        m.kind = TIKU_KIND_FILE;
        m.facts.perm = TIKU_P_READ;
        check(!tiku_trk_launch_self(&m, &self), "AW-030",
              "a file that cannot be executed is not an application");
        m.facts.perm |= TIKU_P_EXEC;
        check(tiku_trk_launch_self(&m, &self), "AW-030",
              "one that can, is: it needs nothing else to open it");
        check(strcmp(self.cmd, m.path) == 0, "AW-030",
              "and it is run as ITSELF, by its own path");
        check(strcmp(self.name, "trk_launch_self") == 0, "AW-030",
              "named by its leaf, which is what a status line shows");
        m.kind = TIKU_KIND_DIRECTORY;
        check(!tiku_trk_launch_self(&m, &self), "AW-030",
              "a directory is never run, whatever its bits say");
    }

    printf("\na program this system built is not just any binary\n");
    {
        /*
         * The stamp, end to end: the application kit puts a ".tikuos"
         * section into anything that links it, and the backend reads the
         * section table to tell tiku-term from /bin/ls.  Without it every
         * TikuOS application wore the same art as any executable on the
         * machine, which is the thing a person is trying to pick out.
         */
        tiku_backend_t *b = tiku_backend_posix_open();
        tiku_model_t m;
        const char *self = "build/runtime/apps/terminal/tiku-term";   /* the tree's own build */

        if (b != NULL && b->ops->stat(b, self, &m) == 0) {
            check(strcmp(tiku_model_icon_name(&m), "application") == 0,
                  "AW-030", TIKU_SHELL_TEST_APPLICATION_DESCRIPTION
                  "art, not the generic executable one");
            check(strcmp(m.type, "application/x-vnd.be-app") == 0,
                  "AW-030", "and says so in its type");
        }
        /* /bin/ls, not /bin/sh: on many systems sh is a SYMLINK, and a
         * link is neither an executable nor an application -- which the
         * first draft of this check discovered the hard way. */
        if (b != NULL && b->ops->stat(b, "/bin/ls", &m) == 0) {
            check(strcmp(tiku_model_icon_name(&m), "executable") == 0,
                  "AW-030", "while a program it did not build stays a "
                  "plain executable");
        }
        if (b != NULL) { b->ops->close(b); }
    }

    printf("\n%s\n", fails ? "FAILURES" : "the launch rules hold");
    return fails ? 1 : 0;
}
