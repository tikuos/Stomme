/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_scripting.c - the scripting suite: what each property takes, and
 * what it does when asked for something that is not there.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "tiku_trk_script_api.h"

static int fails;

static void
check(int cond, const char *row, const char *what)
{
    printf("  [%s] %-9s %s\n", cond ? "PASS" : "FAIL", row, what);
    if (!cond) { fails++; }
}

static void
put(const char *dir, const char *name)
{
    char p[600];
    FILE *f;

    snprintf(p, sizeof p, "%s/%s", dir, name);
    f = fopen(p, "w");
    if (f != NULL) { fputs("x", f); (void)fclose(f); }
}

/** @brief Run one line of script.  @return the reply. */
static tiku_trk_rep_t
run(tiku_trk_poseview_t *v, const char *line)
{
    char store[8][TIKU_PATH_MAX];
    tiku_trk_req_t req;
    tiku_trk_rep_t rep;

    memset(&rep, 0, sizeof rep);
    if (tiku_trk_script_parse(line, &req, store) != 0) {
        rep.error = TIKU_TRK_ERR_UNHANDLED;
        return rep;
    }
    tiku_trk_script_run(v, &req, &rep);
    return rep;
}

int
main(void)
{
    tiku_backend_t *b = tiku_backend_posix_open();
    tiku_trk_poseview_t *v;
    char dir[] = "/tmp/trk_scr";
    tiku_trk_rep_t r;
    char p0[600], p1[600], p2[600];

    (void)system("rm -rf /tmp/trk_scr");
    (void)mkdir(dir, 0755);
    put(dir, "alpha.txt");
    put(dir, "beta.txt");
    put(dir, "gamma.txt");
    v = tiku_trk_poseview_new(b, dir);

    printf("the suite describes itself (PVK-048)\n");
    check(tiku_trk_script_suite(0) != NULL &&
          strstr(tiku_trk_script_suite(0), "Path") != NULL, "PVK-048",
          "a client can ask what it may ask for");
    {
        int i = 0, seen = 0;

        while (tiku_trk_script_suite(i) != NULL) { i++; seen++; }
        check(seen == 3, "PVK-048",
              "three properties: Path, Entry and Selection");
    }

    printf("\nthe window's folder (PVK-049)\n");
    r = run(v, "get Path");
    check(r.error == TIKU_TRK_ERR_OK && r.nresult == 1 &&
          strcmp(r.result[0], dir) == 0, "PVK-049",
          "'get Path' returns the folder the window is showing");
    r = run(v, "set Path /tmp");
    check(r.error == TIKU_TRK_ERR_UNHANDLED, "PVK-054",
          "and a verb the property does not take is left UNHANDLED rather "
          "than failed -- the caller may have something else to try");

    printf("\nthe listing (PVK-050)\n");
    r = run(v, "count Entry");
    check(r.error == TIKU_TRK_ERR_OK && r.count == 3, "PVK-050",
          "'count Entry' counts the poses");
    r = run(v, "get Entry");
    check(r.nresult == 3, "PVK-050", "'get Entry' returns them all");
    snprintf(p0, sizeof p0, "%s", r.result[0]);
    snprintf(p1, sizeof p1, "%s", r.result[1]);
    snprintf(p2, sizeof p2, "%s", r.result[2]);
    r = run(v, "get Entry 1");
    check(r.error == TIKU_TRK_ERR_OK && strcmp(r.result[0], p1) == 0,
          "PVK-050", "an index picks one out, in the CURRENT order");
    r = run(v, "get Entry 9");
    check(r.error == TIKU_TRK_ERR_BAD_INDEX, "PVK-055",
          "and an index past the end is a bad index rather than a crash -- "
          "indices are unstable and clients must be able to be wrong");
    {
        char line[700];

        snprintf(line, sizeof line, "get Entry next %s", p0);
        r = run(v, line);
        check(r.error == TIKU_TRK_ERR_OK && strcmp(r.result[0], p1) == 0,
              "PVK-050", "'next' walks from a path to the one after it");
        snprintf(line, sizeof line, "get Entry previous %s", p0);
        r = run(v, line);
        check(r.error == TIKU_TRK_ERR_BAD_INDEX, "PVK-050",
              "and asking before the first is a bad index");
        r = run(v, "get Entry next /tmp/nosuch");
        check(r.error == TIKU_TRK_ERR_NOT_FOUND, "PVK-054",
              "while a path that is not in the listing is not found");
    }

    printf("\nthe selection (PVK-051)\n");
    r = run(v, "count Selection");
    check(r.count == 0, "PVK-051", "nothing is selected to begin with");
    r = run(v, "set Selection 0 2");
    check(r.error == TIKU_TRK_ERR_OK, "PVK-051", "a range selects");
    r = run(v, "count Selection");
    check(r.count == 2, "PVK-051",
          "and its END IS EXCLUSIVE: 0 to 2 is two poses, not three -- the "
          "original's loop is `index < end` and a client that assumed "
          "otherwise would select one too few");
    r = run(v, "set Selection 0 9");
    check(r.error == TIKU_TRK_ERR_BAD_INDEX, "PVK-051",
          "both bounds are checked as valid indices");
    r = run(v, "get Selection");
    check(r.nresult == 2, "PVK-051", "'get Selection' returns what is on");
    r = run(v, "set Selection 1 2");
    r = run(v, "count Selection");
    check(r.count == 1, "PVK-051",
          "a second 'set' REPLACES the first rather than adding to it");
    {
        char line[700];

        /* p0 is not in the 1..2 range just selected, so adding it must
         * make two rather than replacing the one. */
        snprintf(line, sizeof line, "create Selection %s", p0);
        r = run(v, line);
        r = run(v, "count Selection");
        check(r.count == 2, "PVK-051",
              "'create' ADDS to the selection rather than replacing it");
        snprintf(line, sizeof line, "set Selection %s", p1);
        r = run(v, line);
        r = run(v, "count Selection");
        check(r.count == 1, "PVK-051", "while 'set' replaces it");
    }
    {
        char line[1300];

        snprintf(line, sizeof line, "set Selection %s %s", p0, p2);
        r = run(v, line);
        check(r.error == TIKU_TRK_ERR_OK && r.count == 2, "PVK-051",
              "'set' with two paths answers two");
        r = run(v, "get Selection");
        check(r.nresult == 2 && strcmp(r.result[0], p0) == 0 &&
              strcmp(r.result[1], p2) == 0, "PVK-051",
              "and selects both paths it names");
    }
    r = run(v, "delete Selection");
    r = run(v, "count Selection");
    check(r.count == 0, "PVK-051", "and 'delete Selection' clears it");

    printf("\nopening and trashing (PVK-052)\n");
    r = run(v, "execute Entry 0");
    check(r.error == TIKU_TRK_ERR_OK && r.nresult == 1 &&
          strcmp(r.result[0], p0) == 0, "PVK-052",
          "'execute Entry' resolves what to open, by index");
    {
        char line[1300];

        snprintf(line, sizeof line, "delete Entry %s", p1);
        r = run(v, line);
        check(r.error == TIKU_TRK_ERR_OK && r.nresult == 1, "PVK-052",
              "'delete Entry' resolves what to TRASH -- naming it, so the "
              "caller trashes rather than destroys");
        snprintf(line, sizeof line, "delete Entry %s %s", p0, p2);
        r = run(v, line);
        check(r.error == TIKU_TRK_ERR_OK && r.nresult == 2 &&
              strcmp(r.result[0], p0) == 0 && strcmp(r.result[1], p2) == 0,
              "PVK-052", "'delete Entry' with two paths names each once");
    }
    r = run(v, "execute Entry 9");
    check(r.error == TIKU_TRK_ERR_BAD_INDEX, "PVK-054",
          "and every failure comes back in the reply rather than thrown");

    tiku_trk_poseview_free(v);
    tiku_backend_close(b);
    (void)system("rm -rf /tmp/trk_scr");
    printf("\n%s\n", fails ? "FAILURES" : "the scripting suite holds");
    return fails ? 1 : 0;
}
