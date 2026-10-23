/*
 * KintsugiOS v0.01 kits -- the foundation KintsugiDesktop and KintsugiTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@kinstugi-os.org>
 *
 * test_fontset.c - finding faces by the names documents use, in a folder
 * of this machine's own faces copied under names that say nothing, so
 * what is found is found by the files' tables: a family's weights and
 * slants, what must be made up, stand-ins, kinds, a character's face.
 * A machine with none of the usual faces skips what it cannot show; a
 * folder with no faces at all finds nothing, in a second process.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "tiku_fontset.h"
#include "tiku_ttf.h"

static int fails;

/** @brief One row of the report, PASS or FAIL; a FAIL is counted. */
static void
check(int cond, const char *row, const char *what)
{
    printf("  [%s] %-7s %s\n", cond ? "PASS" : "FAIL", row, what);
    if (!cond) {
        fails++;
    }
}

/** @brief Faces a Mac or a Linux machine has, by what they are for. */
static const struct {
    const char *role, *on_mac, *on_linux;
} faces[] = {
    {"sans", "/System/Library/Fonts/Supplemental/Arial.ttf",
     "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"},
    {"sans-bold", "/System/Library/Fonts/Supplemental/Arial Bold.ttf",
     "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"},
    {"serif", "/System/Library/Fonts/Supplemental/Times New Roman.ttf",
     "/usr/share/fonts/truetype/dejavu/DejaVuSerif.ttf"},
    {"mono", "/System/Library/Fonts/Supplemental/Courier New.ttf",
     "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf"},
};

static char scratch[PATH_MAX];
static char family[4][64]; /* each role's family, as found     */
static int have[4];

/** @brief Each role's face copied into the scratch folder as face<i>. */
static int
gather(void)
{
    char cmd[PATH_MAX * 3];
    size_t i;
    int n = 0;

    for (i = 0; i < sizeof faces / sizeof faces[0]; i++) {
        const char *from = access(faces[i].on_mac, R_OK) == 0 ? faces[i].on_mac
                           : access(faces[i].on_linux, R_OK) == 0
                               ? faces[i].on_linux
                               : NULL;
        if (from == NULL) {
            continue;
        }
        snprintf(cmd, sizeof cmd, "cp '%s' '%s/face%zu.ttf'", from, scratch, i);
        have[i] = system(cmd) == 0;
        n += have[i];
    }
    return n;
}

/** @brief The folder with nothing in it: no faces, nothing found. */
static int
empty_run(void)
{
    tiku_fontset_face_t f;

    return tiku_fontset_count() == 0 &&
                   tiku_fontset_find("Arial", 400, 0, &f) == -1 &&
                   tiku_fontset_cover('A', "Arial", 400, 0, &f) == -1
               ? 0
               : 1;
}

/** @brief Enumerate copied native metadata without substitution or aliasing. */
static void
metadata_tests(void)
{
    tiku_fontset_face_t face, again;
    check(tiku_fontset_at(-1, &face) == -1 && !*face.path, "FS-032",
          "invalid enumeration clears metadata");
    for (int i = 0; i < tiku_fontset_count(); i++) {
        check(!tiku_fontset_at(i, &face) && face.exact && *face.family &&
                  *face.path,
              "FS-033", "indexed face preserves native metadata");
        again = face;
        memset(&face, 0, sizeof face);
        check(!tiku_fontset_at(i, &face) && !memcmp(&face, &again, sizeof face),
              "FS-034", "enumerated values do not alias the font index");
    }
}

/** @brief A family's faces: the one asked, the weight and slant made up
 *         where the family has not got them. */
static void
family_tests(void)
{
    tiku_fontset_face_t f, b;

    check(tiku_fontset_find(family[0], 400, 0, &f) == 0 && f.exact &&
              strcmp(f.family, family[0]) == 0 && f.weight == 400 &&
              !f.bold_synth && !f.italic_synth && strstr(f.path, "face0"),
          "FS-001", "a family by its own name: its regular face");
    if (have[1]) {
        check(tiku_fontset_find(family[0], 700, 0, &b) == 0 &&
                  b.weight >= 600 && !b.bold_synth && strstr(b.path, "face1"),
              "FS-002", "bold: the family's bold face, nothing made up");
    }
    check(tiku_fontset_find(family[0], 400, 1, &f) == 0 && f.italic_synth &&
              !f.italic && strstr(f.path, "face0"),
          "FS-003",
          "italic the family has not got: the upright, to be slanted");
    if (!have[1]) {
        check(tiku_fontset_find(family[0], 700, 0, &f) == 0 && f.bold_synth,
              "FS-002", "bold it has not got: to be made");
    }
    check(tiku_fontset_find("  ", 400, 0, &f) == 0 && !f.exact &&
              f.path[0] != '\0',
          "FS-004", "no name at all: still a face");
}

/** @brief Families the machine lacks: stand-ins, then their kind. */
static void
stand_in_tests(void)
{
    tiku_fontset_face_t f;

    check(tiku_fontset_find("Liberation Sans", 400, 0, &f) == 0 && !f.exact &&
              strstr(f.path, "face0") != NULL,
          "FS-010", "a family not here: its stand-in, the sans face");
    if (have[2]) {
        check(tiku_fontset_find("Some Book Serif", 400, 0, &f) == 0 &&
                  !f.exact && strstr(f.path, "face2") != NULL &&
                  tiku_fontset_kind("Georgia Pro") == 2,
              "FS-011", "an unknown serif: a serif face");
    }
    if (have[3]) {
        check(tiku_fontset_find("Fira Code Mono", 400, 0, &f) == 0 &&
                  strstr(f.path, "face3") != NULL &&
                  tiku_fontset_kind(family[3]) == 1,
              "FS-012", "an unknown monospaced face: a monospaced one");
    }
    check(tiku_fontset_kind("Totally Unknown") == 0, "FS-013",
          "a name that says nothing is sans");
}

/** @brief Characters: a face that draws them, or none. */
static void
cover_tests(void)
{
    tiku_fontset_face_t f;

    check(tiku_fontset_cover('A', family[0], 400, 0, &f) == 0 &&
              f.path[0] != '\0',
          "FS-020", "a letter any face draws: found");
    check(tiku_fontset_cover(0x10fffd, family[0], 400, 0, &f) == -1, "FS-021",
          "a private-use character nothing draws: none");
}

/** @brief Every group, over this machine's faces copied aside. */
int
main(int argc, char **argv)
{
    const char *tmp = getenv("TMPDIR");
    char cmd[PATH_MAX * 2 + 32];
    int i, n;

    if (argc > 1 && strcmp(argv[1], "--empty") == 0) {
        return empty_run();
    }
    printf("Font set: faces found by the names documents use\n");
    snprintf(scratch, sizeof scratch, "%s/tiku-fontset-XXXXXX",
             tmp != NULL ? tmp : "/tmp");
    if (mkdtemp(scratch) == NULL) {
        perror("mkdtemp");
        return 2;
    }
    snprintf(cmd, sizeof cmd, "TIKU_FONTSET_ONLY='%s' '%s' --empty", scratch,
             argv[0]);
    check(system(cmd) == 0, "FS-030",
          "a folder with no faces: nothing found, nothing given");
    n = gather();
    setenv("TIKU_FONTSET_ONLY", scratch, 1);
    if (n == 0 || !have[0]) {
        printf("  [SKIP] no usual faces on this machine\n");
    } else {
        check(tiku_fontset_count() == n, "FS-031",
              "every face copied is found by its tables");
        for (i = 0; i < 4; i++) {
            char path[PATH_MAX];
            tiku_ttf_t *t;

            snprintf(path, sizeof path, "%s/face%d.ttf", scratch, i);
            if (have[i] && (t = tiku_ttf_open(path)) != NULL) {
                snprintf(family[i], sizeof family[i], "%s", tiku_ttf_family(t));
                tiku_ttf_close(t);
            }
        }
        metadata_tests();
        family_tests();
        stand_in_tests();
        cover_tests();
    }
    snprintf(cmd, sizeof cmd, "rm -rf '%s'", scratch);
    (void)system(cmd);
    printf("%s\n", fails ? "FAIL"
                         : "PASS: faces found by family, weight "
                           "and slant, stand-ins and scripts");
    return fails ? 1 : 0;
}
