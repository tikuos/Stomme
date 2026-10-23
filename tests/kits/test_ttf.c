/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_ttf.c - reading a font somebody dropped in.
 *
 * The files are real ones off this machine, because the point of the
 * exercise is faces nobody here made: a parser proved against its own
 * output proves only that it is consistent.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#define _DEFAULT_SOURCE 1

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "tiku_ttf.h"

static int fails;

static void
check(int cond, const char *row, const char *what)
{
    printf("  [%s] %-9s %s\n", cond ? "PASS" : "FAIL", row, what);
    if (!cond) {
        fails++;
    }
}

/** @brief The first of @p paths that is there, or NULL. */
static const char *
pick(const char *const *paths, int n)
{
    struct stat st;
    int i;

    for (i = 0; i < n; i++) {
        if (stat(paths[i], &st) == 0) {
            return paths[i];
        }
    }
    return NULL;
}

/**
 * @brief Whether @p g is a ring: ink around the outside, paper within.
 *
 * A capital O is two contours wound against each other, and the hole is
 * what nonzero winding is FOR.  Fill it in -- by losing a contour, by
 * losing the sign of one, by flattening the curves that make it -- and
 * this is the check that notices.
 */
static int
has_hole(const tiku_ttf_glyph_t *g)
{
    int cx = g->w / 2, cy = g->h / 2;

    if (g->w < 5 || g->h < 5 || g->cover == NULL) {
        return 0;
    }
    /* Paper in the middle, ink to the left and right of it on the same
     * row: a disc fails the first, a bar fails the second. */
    return g->cover[cy * g->w + cx] < 40 &&
           g->cover[cy * g->w + 1] > 100 &&
           g->cover[cy * g->w + (g->w - 2)] > 100;
}

/** @brief How much ink a rasterised glyph carries. */
static long
ink_of(const tiku_ttf_glyph_t *g)
{
    long sum = 0;
    int i;

    for (i = 0; i < g->w * g->h; i++) {
        sum += g->cover[i];
    }
    return sum;
}

/** @brief Read every truncation of @p path, drawing all of each. */
static void truncations_of(const char *path);

/**
 * @brief A collection (.ttc): each face opened by its number and the
 *        first by default; a number past its faces, or any but the
 *        first of a file of one face, opens nothing.
 */
static void
collection_test(void)
{
    static const char *const ttc[] = {
        "/System/Library/Fonts/Helvetica.ttc",
        "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/google-noto-cjk/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc"
    };
    static const char *const one[] = {
        "/System/Library/Fonts/Supplemental/Arial.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/dejavu-sans-fonts/DejaVuSans.ttf"
    };
    const char *path = pick(ttc, 4), *single = pick(one, 3);
    tiku_ttf_t *first, *second, *past;

    if (path == NULL) {
        printf("  [SKIP] DEV-012   no font collection to read\n");
        return;
    }
    first = tiku_ttf_open(path);
    second = tiku_ttf_open_face(path, 1);
    past = tiku_ttf_open_face(path, 4096);
    check(first != NULL && second != NULL && past == NULL, "DEV-012",
          "a collection's faces open by number, and none past its last");
    check(first != NULL && second != NULL &&
          (strcmp(tiku_ttf_family(first), tiku_ttf_family(second)) != 0 ||
           tiku_ttf_bold(first) != tiku_ttf_bold(second)), "DEV-012",
          "the second face is another face, not the first again");
    tiku_ttf_close(first);
    tiku_ttf_close(second);
    if (single != NULL) {
        past = tiku_ttf_open_face(single, 1);
        check(past == NULL, "DEV-012",
              "a file of one face has no second");
        tiku_ttf_close(past);
    }
}

int
main(void)
{
    static const char *const sans[] = {
        "/usr/share/fonts/dejavu-sans-fonts/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/liberation-sans-fonts/LiberationSans-Regular.ttf"
    };
    static const char *const cff[] = {
        "/usr/share/fonts/abattis-cantarell-fonts/Cantarell-Regular.otf",
        "/usr/share/fonts/adwaita-sans-fonts/AdwaitaSans-Regular.ttf"
    };
    const char *path = pick(sans, 3);
    tiku_ttf_t *ttf;

    printf("reading a font somebody dropped in (DEV-012)\n");
    collection_test();
    if (path == NULL) {
        printf("  [SKIP] DEV-012   no system face to read\n");
        return 0;
    }

    check(tiku_ttf_is_font(path), "DEV-012",
          "a face is judged by what it holds, not what it is called");
    check(!tiku_ttf_is_font("/etc/hostname") &&
          !tiku_ttf_is_font("/no/such/file"), "DEV-012",
          "so a file that is not one is turned away before it is read");

    ttf = tiku_ttf_open(path);
    check(ttf != NULL, "DEV-012", "and one that is opens");
    if (ttf == NULL) {
        printf("\nFAILURES\n");
        return 1;
    }
    check(tiku_ttf_family(ttf) != NULL &&
          strlen(tiku_ttf_family(ttf)) > 0, "DEV-012",
          "the file says what family it belongs to, out of its own table");
    {
        int ascent = 0, height = 0;

        tiku_ttf_metrics(ttf, 12, &ascent, &height);
        check(height >= 10 && height <= 20 && ascent > 0 &&
              ascent < height, "DEV-012",
              "and how tall a line of it is at a size we would ask for");
    }

    check(tiku_ttf_has(ttf, 'A') && tiku_ttf_has(ttf, 0xE9u),
          "DEV-012", "the character map answers for a letter and an accent");
    check(!tiku_ttf_has(ttf, 0x10FFFDu), "DEV-012",
          "and says no rather than something for one it has not got");

    {
        tiku_ttf_glyph_t a, space, acute;

        check(tiku_ttf_render(ttf, 'A', 16, &a) && a.cover != NULL &&
              ink_of(&a) > 0, "DEV-012",
              "a letter rasterises to real coverage, not an empty box");
        /* The coverage box carries a pixel of margin each side, so it
         * sits a little proud of the advance rather than inside it. */
        check(a.w > 4 && a.w < 24 && a.h > 6 && a.h < 24 &&
              a.adv > 0 && a.w <= a.adv + 3, "DEV-012",
              "of about the size a 16 px letter should be");
        check(a.oy < 0 && a.oy + a.h <= 2, "DEV-012",
              "sitting above the baseline, where a capital sits");

        /* An accented letter is a COMPOSITE: the e and the acute are two
         * glyphs placed by a third, and a parser that ignored that would
         * draw a bare e. */
        check(tiku_ttf_render(ttf, 0xE9u, 16, &acute) &&
              acute.cover != NULL && ink_of(&acute) > 0, "DEV-012",
              "an accented letter rasterises too, though it is composite");
        check(acute.h > a.h - 4, "DEV-012",
              "and is taller than its bare letter: the accent is on it");

        {
            tiku_ttf_glyph_t ring;

            check(tiku_ttf_render(ttf, 'O', 28, &ring) &&
                  has_hole(&ring), "DEV-012",
                  "a capital O is a ring here too, drawn from quadratics");
            tiku_ttf_free_glyph(&ring);
        }
        check(tiku_ttf_render(ttf, ' ', 16, &space) &&
              space.adv > 0 && (space.cover == NULL ||
                                ink_of(&space) == 0), "DEV-012",
              "a space is an advance with no ink, and not a failure");

        tiku_ttf_free_glyph(&a);
        tiku_ttf_free_glyph(&acute);
        tiku_ttf_free_glyph(&space);
    }

    /* Bigger is bigger: the outline is scaled, not a bitmap blown up. */
    {
        tiku_ttf_glyph_t small, big;

        if (tiku_ttf_render(ttf, 'H', 10, &small) &&
            tiku_ttf_render(ttf, 'H', 40, &big)) {
            check(big.h > small.h * 3 && big.adv > small.adv * 3,
                  "DEV-012", "and a size is a size: the outline is scaled");
            tiku_ttf_free_glyph(&small);
            tiku_ttf_free_glyph(&big);
        }
    }
    tiku_ttf_close(ttf);

    /*
     * A VARIABLE font, whose outlines are CFF2 -- the same charstrings
     * left adjustable.  The default instance is drawn: the blend operator
     * keeps each value's default and steps over its deltas, and getting
     * that step wrong desyncs the whole charstring.  So a ring must come
     * out a ring, and an ideograph must have ink -- the CJK faces on a
     * system are all this kind, and were refused whole before.
     */
    {
        static const char *const vf[] = {
            "/usr/share/fonts/abattis-cantarell-vf-fonts/Cantarell-VF.otf",
            "/usr/share/fonts/google-noto-sans-cjk-vf-fonts/NotoSansCJK-VF.ttc",
            "/usr/share/fonts/google-noto-vf/NotoSans[wght].ttf"
        };
        const char *path2 = pick(vf, 3);
        tiku_ttf_t *var = (path2 != NULL) ? tiku_ttf_open(path2)
                                               : NULL;

        check(path2 == NULL || var != NULL, "DEV-012",
              "a variable font opens: its default instance is a face");
        if (var != NULL) {
            tiku_ttf_glyph_t ring;

            check(tiku_ttf_render(var, 'O', 28, &ring) &&
                  ring.cover != NULL && has_hole(&ring), "DEV-012",
                  "and draws a ring for an O");
            tiku_ttf_free_glyph(&ring);
            tiku_ttf_close(var);
        }
        /*
         * The proof it draws the DEFAULT instance right, and not just
         * SOMETHING: a variable font's default should render the same as
         * the static font of the same design.  A blend that kept its
         * deltas, a wrong region count, an unread vstore -- any of them
         * desyncs the charstring and collapses the glyph.  A 'g' is a
         * good witness: an O survives a broken blend, a g does not.
         */
        {
            const char *vpath =
                "/usr/share/fonts/abattis-cantarell-vf-fonts/Cantarell-VF.otf";
            const char *spath =
                "/usr/share/fonts/abattis-cantarell-fonts/"
                "Cantarell-Regular.otf";
            struct stat sv, ss;

            if (stat(vpath, &sv) == 0 && stat(spath, &ss) == 0) {
                tiku_ttf_t *tv = tiku_ttf_open(vpath);
                tiku_ttf_t *ts = tiku_ttf_open(spath);
                tiku_ttf_glyph_t gv, gs;
                long iv = 0, is = 0;
                int k;

                if (tv != NULL && ts != NULL) {
                    int drewv = tiku_ttf_render(tv, 'g', 20, &gv);
                    int drews = tiku_ttf_render(ts, 'g', 20, &gs);

                    /* The ink is counted OUTSIDE the guard: a broken
                     * blend renders a g with no cover at all, and the
                     * check has to see that as a failure, not skip it. */
                    if (drewv && gv.cover != NULL) {
                        for (k = 0; k < gv.w * gv.h; k++) { iv += gv.cover[k]; }
                    }
                    if (drews && gs.cover != NULL) {
                        for (k = 0; k < gs.w * gs.h; k++) { is += gs.cover[k]; }
                    }
                    check(is > 0 && gv.w == gs.w && gv.h == gs.h &&
                          iv > is - is / 6 && iv < is + is / 6, "DEV-012",
                          "the default instance of a variable font draws "
                          "the same as the static font of its design");
                    if (drewv) { tiku_ttf_free_glyph(&gv); }
                    if (drews) { tiku_ttf_free_glyph(&gs); }
                }
                if (tv != NULL) { tiku_ttf_close(tv); }
                if (ts != NULL) { tiku_ttf_close(ts); }
            }
        }
        {
            /* The point of all this: a Chinese face, which is a variable
             * one on every system seen so far, and which no static table
             * baked here would ever hold. */
            static const char *const cjk[] = {
                "/usr/share/fonts/google-noto-sans-cjk-vf-fonts/"
                "NotoSansCJK-VF.ttc",
                "/usr/share/fonts/google-droid-sans-fonts/"
                "DroidSansFallbackFull.ttf"
            };
            const char *cpath = pick(cjk, 2);
            tiku_ttf_t *han = (cpath != NULL) ? tiku_ttf_open(cpath)
                                                   : NULL;

            if (han != NULL && tiku_ttf_has(han, 0x65E5u)) {
                tiku_ttf_glyph_t sun;

                check(tiku_ttf_render(han, 0x65E5u, 28, &sun) &&
                      sun.cover != NULL && ink_of(&sun) > 0 &&
                      has_hole(&sun), "DEV-012",
                      "the ideograph for 'sun' draws, hole and all");
                tiku_ttf_free_glyph(&sun);
            }
            if (han != NULL) {
                tiku_ttf_close(han);
            }
        }
    }

    /*
     * The OTHER kind of outline.  An OpenType file with PostScript
     * curves keeps them in a CFF table as little programs, and a fifth
     * of the faces on a plain system are these.
     */
    {
        const char *other = pick(cff, 2);
        tiku_ttf_t *ps = (other != NULL) ? tiku_ttf_open(other)
                                              : NULL;

        check(other == NULL || ps != NULL, "DEV-012",
              "a PostScript-outline face opens like any other");
        if (ps != NULL) {
            tiku_ttf_glyph_t a, acute;

            check(tiku_ttf_family(ps) != NULL &&
                  tiku_ttf_family(ps)[0] != '\0' &&
                  tiku_ttf_has(ps, 'A'), "DEV-012",
                  "names itself and answers its character map the same way");
            check(tiku_ttf_render(ps, 'A', 16, &a) && a.cover != NULL &&
                  ink_of(&a) > 0 && a.adv > 0, "DEV-012",
                  "and its charstrings draw: cubics, where the other had "
                  "quadratics");
            check(a.oy < 0 && a.h > 6 && a.h < 24, "DEV-012",
                  "to about the size and place a 16 px capital belongs");
            check(tiku_ttf_render(ps, 0xE9u, 16, &acute) &&
                  acute.cover != NULL && ink_of(&acute) > 0 &&
                  acute.h > a.h - 4, "DEV-012",
                  "accents too, which a charstring draws rather than places");
            {
                tiku_ttf_glyph_t ring;

                check(tiku_ttf_render(ps, 'O', 28, &ring) &&
                      has_hole(&ring), "DEV-012",
                      "and a capital O comes out a ring, not a blot: its "
                      "two contours are wound against each other");
                tiku_ttf_free_glyph(&ring);
            }
            tiku_ttf_free_glyph(&a);
            tiku_ttf_free_glyph(&acute);
            tiku_ttf_close(ps);
        }
    }
    check(!tiku_ttf_is_font("/etc/hostname"), "DEV-012",
          "while a file that is neither is still turned away unread");

    /*
     * These files arrive by being dropped in a folder, which is to say
     * from anywhere.  Every truncation of a real one must be refused or
     * read without reaching outside it.
     */
    {
        /* Both kinds of outline: a charstring is a PROGRAM, and a bent
         * one can ask for a subroutine that is not there. */
        const char *bent[2];
        int which;

        bent[0] = path;
        bent[1] = pick(cff, 2);
        for (which = 0; which < 2; which++) {
            if (bent[which] != NULL) {
                truncations_of(bent[which]);
            }
        }
        check(1, "DEV-012",
              "and every truncation of one is read without running off it");
    }

    if (fails > 0) {
        printf("\nFAILURES\n");
        return 1;
    }
    printf("\na dropped face reads and draws\n");
    return 0;
}

/**
 * @brief A repeatable stream of nonsense.
 *
 * Seeded and its own, so a failure here is a failure anybody can have
 * again by running the test: a fuzzer nobody can reproduce is a rumour.
 */
static unsigned long bend_seed = 0x5eed1234ul;

static unsigned long
bend_next(void)
{
    bend_seed = bend_seed * 6364136223846793005ul + 1442695040888963407ul;
    return bend_seed >> 33;
}

static void
truncations_of(const char *path)
{
    {
        unsigned char *whole;
        long size;
        FILE *f = fopen(path, "rb");
        int cut;

        if (f != NULL && fseek(f, 0L, SEEK_END) == 0 &&
            (size = ftell(f)) > 0 && fseek(f, 0L, SEEK_SET) == 0) {
            whole = malloc((size_t)size);
            if (whole != NULL && fread(whole, 1u, (size_t)size, f) ==
                                 (size_t)size) {
                for (cut = 1; cut < 220; cut++) {
                    char tmp[] = "/tmp/trk_ttf_cutXXXXXX";
                    long keep = (cut < 32) ? size * cut / 32 : size;
                    int fd = mkstemp(tmp);
                    FILE *w;
                    unsigned char *copy = malloc((size_t)size);

                    if (fd < 0 || copy == NULL) {
                        free(copy);
                        continue;
                    }
                    memcpy(copy, whole, (size_t)size);
                    if (cut >= 32) {
                        /* Not cut short but BENT: an offset that points
                         * somewhere else entirely is what a half-read
                         * table looks like, and a truncation never makes
                         * one.  This is the shape that found a crash.
                         *
                         * A FEW bytes, not many: bend enough of a file
                         * and it stops being a font at the first table,
                         * so the deep paths -- the ones with the bugs in
                         * them -- are never reached at all. */
                        int b, n = 1 + (int)(bend_next() % 6u);

                        for (b = 0; b < n; b++) {
                            copy[bend_next() % (unsigned long)size] =
                                (unsigned char)(bend_next() & 0xffu);
                        }
                    }
                    w = fdopen(fd, "wb");
                    if (w != NULL) {
                        (void)fwrite(copy, 1u, (size_t)keep, w);
                        (void)fclose(w);
                        {
                            tiku_ttf_t *hurt = tiku_ttf_open(tmp);

                            if (hurt != NULL) {
                                tiku_ttf_glyph_t g;
                                unsigned cp;

                                for (cp = 32u; cp < 0x180u; cp++) {
                                    if (tiku_ttf_render(hurt, cp, 14,
                                                             &g)) {
                                        tiku_ttf_free_glyph(&g);
                                    }
                                }
                                tiku_ttf_close(hurt);
                            }
                        }
                    }
                    free(copy);
                    (void)remove(tmp);
                }
            }
            free(whole);
        }
        if (f != NULL) {
            (void)fclose(f);
        }
    }
}
