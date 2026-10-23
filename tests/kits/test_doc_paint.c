/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_doc_paint.c - a help page drawn, proven from the wire.
 *
 * The claim the painter makes is that it draws where the layout said
 * and measures nothing itself, so what is pinned here is the display
 * list: every word recorded at the piece's own rectangle, every link
 * with a line under it, and nothing recorded for a page scrolled out of
 * sight.  A drawing that agreed with itself but not with the table
 * would pass a pixel test and fail a reader's click.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TEST_SOURCE_ROOT
#define TIKU_TEST_SOURCE_ROOT "."
#endif

#include <stdio.h>
#include <string.h>

#include "tiku_doc.h"
#include "tiku_doc_paint.h"
#include "tiku_dl.h"
#include "tiku_font.h"

static int fails;
static tiku_doc_t doc;

static void
check(int cond, const char *what)
{
    printf("  [%s] paint  %s\n", cond ? "PASS" : "FAIL", what);
    if (!cond) {
        fails++;
    }
}

static const char *const page =
    "<h1>Help</h1>"
    "<p>A page with <b>bold</b>, <code>code</code> and "
    "<a href=\"other.html\">a link</a> in it.</p>"
    "<ul><li>an item</li><li>another item</li></ul>"
    "<pre>kept  spaces</pre>"
    "<blockquote>said aside</blockquote><hr>"
    "<p>and a closing paragraph that is long enough to wrap at least "
    "once at the width these checks use</p>";

/** @brief Draw the page and hand back what the wire carried. */
static tiku_dl_t *
drawn(const tiku_doc_paint_t *how, tiku_rect_t where)
{
    tiku_surface_t *s = tiku_surface_new(where.x + where.w + 20,
                                         where.y + where.h + 20, TIKU_C_PANEL);
    tiku_dl_t *dl = tiku_dl_new();

    s->record = dl;
    tiku_doc_draw(s, &doc, where, how);
    s->record = NULL;
    tiku_surface_free(s);
    return dl;
}

/** @brief Every text command on the wire, in order. */
static int
texts(const tiku_dl_t *dl, tiku_dl_fact_t *out, int max)
{
    char buf[65536];
    size_t len = 0, at = 0;
    tiku_dl_fact_t f;
    int n = 0;

    (void)tiku_dl_flatten(dl, buf, sizeof buf, &len);

    while (n < max && tiku_dl_read(buf, len, &at, &f)) {
        if (f.kind == TIKU_DL_FACT_TEXT) out[n++] = f;
    }
    return n;
}

int
main(void)
{
    const tiku_rect_t where = {20, 30, 300, 400};
    tiku_dl_fact_t seen[512];

    tiku_font_set_size(12);
    tiku_doc_read(&doc, page);
    tiku_doc_layout(&doc, where.w);

    printf("\nevery word is drawn where the layout put it\n");
    {
        tiku_doc_paint_t how = {0, TIKU_DOC_NONE, NULL, NULL, NULL};
        tiku_dl_t *dl = drawn(&how, where);
        int n = texts(dl, seen, 512);
        int matched = 0, visible = 0;

        for (int i = 0; i < tiku_doc_piece_count(&doc); i++) {
            tiku_rect_t r = tiku_doc_piece_rect(&doc, i);

            if (r.y >= where.h) continue;   /* below the frame */
            visible++;
            for (int k = 0; k < n; k++) {
                if (seen[k].rect.x == where.x + r.x) matched++, k = n;
            }
        }
        check(n > 0, "the page reaches the wire as words");
        check(visible > 0 && matched == visible,
              "and every visible piece is drawn at its own x");
        tiku_dl_free(dl);
    }

    printf("\nthe page moves with the scroll and nothing else\n");
    {
        tiku_doc_paint_t a = {0, TIKU_DOC_NONE, NULL, NULL, NULL};
        tiku_doc_paint_t b = {40, TIKU_DOC_NONE, NULL, NULL, NULL};
        tiku_dl_t *da = drawn(&a, where);
        tiku_dl_t *db = drawn(&b, where);
        tiku_dl_fact_t first[512], second[512];
        int na = texts(da, first, 512), nb = texts(db, second, 512);
        int moved = 1;

        /* The first word of the scrolled drawing is somewhere in the
         * unscrolled one, exactly forty rows lower. */
        if (na && nb) {
            int found = 0;

            for (int i = 0; i < na; i++) {
                if (first[i].rect.y == second[0].rect.y + 40 &&
                    first[i].rect.x == second[0].rect.x) found = 1;
            }
            moved = found;
        }
        check(na > 0 && nb > 0 && moved,
              "a scroll of forty moves the words forty, and not sideways");
        tiku_dl_free(da);
        tiku_dl_free(db);
    }

    printf("\na link is told apart without a colour\n");
    {
        tiku_doc_paint_t plain = {0, TIKU_DOC_NONE, NULL, NULL, NULL};
        tiku_doc_paint_t rung = {0, 0, NULL, NULL, NULL};
        char said[8192];
        tiku_dl_t *dl = drawn(&plain, where);
        size_t len = 0, at = 0;
        tiku_dl_fact_t f;

        (void)tiku_dl_flatten(dl, said, sizeof said, &len);
        int lines = 0, first = 0, n = 0;

        (void)tiku_doc_link_pieces(&doc, 0, &first, &n);
        while (tiku_dl_read(said, len, &at, &f)) {
            tiku_rect_t r = tiku_doc_piece_rect(&doc, first);

            if (f.kind != TIKU_DL_FACT_PAINT || f.rect.h != 1) continue;
            if (f.rect.x == where.x + r.x && f.rect.w == r.w) lines++;
        }
        check(n >= 1 && lines >= 1,
              "a link has a line under it, the width of its own words");
        tiku_dl_free(dl);

        /* The ring is the only difference a focused link makes. */
        tiku_dl_t *a = drawn(&plain, where);
        tiku_dl_t *b = drawn(&rung, where);

        check(tiku_dl_count(b) > tiku_dl_count(a),
              "and a focused one is drawn a ring besides");
        tiku_dl_free(a);
        tiku_dl_free(b);
    }

    printf("\nnothing is drawn for what the frame does not show\n");
    {
        tiku_doc_paint_t past = {tiku_doc_height(&doc) + 500, TIKU_DOC_NONE,
                                 NULL, NULL, NULL};
        tiku_dl_t *dl = drawn(&past, where);

        check(texts(dl, seen, 512) == 0,
              "a page scrolled past its end is words nobody is shown");
        tiku_dl_free(dl);

        /* A short frame onto a tall page carries the rows it shows, not
         * the page: the wire pays for every word put on it. */
        tiku_rect_t slot = {20, 30, 300, 60};
        tiku_doc_paint_t top = {0, TIKU_DOC_NONE, NULL, NULL, NULL};
        tiku_dl_t *few = drawn(&top, slot);
        int shown = texts(few, seen, 512);

        check(shown > 0 && shown < tiku_doc_piece_count(&doc),
              "and a frame shows the rows it has room for, not the rest");
        tiku_dl_free(few);
    }

    printf("\na table is drawn whole, however tall its first column is\n");
    {
        /* The cells of a row are drawn one after another, so the last
         * line of a tall first column sits BELOW the first line of a
         * short second one.  A painter that stopped at the first line
         * past the frame would draw the first column and lose the
         * rest. */
        static const char *const table =
            "<table><tr>"
            "<td>a first column with enough words in it to wrap several "
            "times over at this width, which is what pushes the lines "
            "of the cells beside it up above its own last line</td>"
            "<td>rhubarb</td></tr></table>";
        tiku_rect_t slot = {10, 10, 240, 70};
        tiku_doc_paint_t how = {0, TIKU_DOC_NONE, NULL, NULL, NULL};
        tiku_dl_t *dl;
        char said[16384];
        size_t len = 0;
        int found = 0;

        tiku_doc_read(&doc, table);
        tiku_doc_layout(&doc, slot.w);
        dl = drawn(&how, slot);
        (void)tiku_dl_flatten(dl, said, sizeof said, &len);
        for (size_t i = 0; i + 7 < len; i++) {
            if (!memcmp(said + i, "rhubarb", 7)) found = 1;
        }
        check(found, "the second column is drawn even though the first "
                     "runs past the frame");
        tiku_dl_free(dl);

        /* And the rule under a row is drawn once, the width of the
         * page, by the cell that begins the row. */
        tiku_rect_t room = {10, 10, 240, 400};
        tiku_dl_t *all = drawn(&how, room);
        int rules = 0;
        size_t at = 0;
        tiku_dl_fact_t f;

        (void)tiku_dl_flatten(all, said, sizeof said, &len);
        while (tiku_dl_read(said, len, &at, &f)) {
            if (f.kind == TIKU_DL_FACT_PAINT && f.rect.h == 1 &&
                f.rect.w == room.w) rules++;
        }
        check(rules == 1, "and one rule, the width of the page, under it");
        tiku_dl_free(all);
        tiku_doc_read(&doc, page);
        tiku_doc_layout(&doc, where.w);
    }

    printf("\nthe painter measures nothing of its own\n");
    {
        FILE *f = fopen(TIKU_TEST_SOURCE_ROOT "/kits/interface/tiku_doc_paint.c", "r");
        char buf[65536];
        size_t n = f ? fread(buf, 1, sizeof buf - 1, f) : 0;

        if (f) fclose(f);
        buf[n] = 0;
        check(n > 0, "the painter is where it is said to be");
        check(n > 0 && !strstr(buf, "tiku_measure_") &&
              !strstr(buf, "tiku_text_wrap"),
              "and it measures nothing: a second layout is how the drawn "
              "page and the hit page come apart");
    }

    printf("\nit draws at every size, and on every table\n");
    {
        int sizes[] = {10, 12, 14, 16};
        int ok = 1;

        const tiku_theme_t *tables[3] = {NULL, tiku_theme_dark(),
                                         tiku_theme_mono()};

        for (int t = 0; t < 3; t++) {
            tiku_theme_set(tables[t]);
            for (size_t i = 0; i < sizeof sizes / sizeof sizes[0]; i++) {
                tiku_doc_paint_t how = {0, 0, "the page was cut", NULL, NULL};
                tiku_dl_t *dl;

                tiku_font_set_size(sizes[i]);
                tiku_doc_layout(&doc, where.w);
                dl = drawn(&how, where);
                if (texts(dl, seen, 512) == 0) ok = 0;
                if (!tiku_doc_check(&doc)) ok = 0;
                tiku_dl_free(dl);
            }
        }
        tiku_theme_set(NULL);
        tiku_font_set_size(12);
        check(ok, "four sizes and three tables, and words on all of them");
    }

    printf("\n%s\n", fails ? "FAILURES" : "the page is drawn where it was laid");
    return fails ? 1 : 0;
}
