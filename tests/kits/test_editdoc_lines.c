/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_editdoc_lines.c - the line index kept by text edits: the upstream
 * cases, then every deletion and insertion over short strings of a, CR
 * and LF, and long random runs, each checked against lines counted
 * afresh from the bytes.
 *
 * Cases adapted from Scintilla 5.6.7 test/unit/testCellBuffer.cxx
 * (CellBuffer, CharacterIndex, CellBufferLong), Copyright Neil Hodgson;
 * see kits/editor/LICENSE.Scintilla and kits/editor/UPSTREAM.md.
 *
 * SPDX-License-Identifier: Apache-2.0 AND LicenseRef-Scintilla
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tiku_editdoc_private.h"

static int fails;
static unsigned long long seed = 0x2545f4914f6cdd1dull;

/** @brief Count a failure and say which row it belongs to. */
static void
check(int cond, const char *row, const char *what)
{
    if (!cond) {
        printf("  [FAIL] %-28s %s\n", row, what);
        fails++;
    }
}

/** @brief The next of a fixed stream of numbers, below @p n. */
static unsigned
next(unsigned n)
{
    seed ^= seed << 13;
    seed ^= seed >> 7;
    seed ^= seed << 17;
    return (unsigned)(seed % n);
}

/** @brief The lines of @p s counted from scratch: a start after every LF,
 *         after every CR not followed by an LF.  Returns the count. */
static int64_t
model_starts(const char *s, int64_t n, int64_t *starts)
{
    int64_t i, count = 1;

    starts[0] = 0;
    for (i = 0; i < n; i++)
        if (s[i] == '\n' || (s[i] == '\r' && (i + 1 >= n || s[i + 1] != '\n')))
            starts[count++] = i + 1;
    starts[count] = n;
    return count;
}

/** @brief Whether @p t's bytes are @p s and its lines are the model's,
 *         each start, each line's end and each position's line. */
static int
agrees(const tiku_ed_text_t *t, const char *s, int64_t n)
{
    static int64_t starts[4096 + 2];
    static char got[4096];
    int64_t count, i, line;

    if (n > 4096 || t->bytes.length != n)
        return 0;
    tiku_ed_bytes_copy(&t->bytes, got, 0, (ptrdiff_t)n);
    if (memcmp(got, s, (size_t)n))
        return 0;
    count = model_starts(s, n, starts);
    if (tiku_ed_lines_count(&t->lines) != count)
        return 0;
    for (i = 0; i <= count; i++)
        if (tiku_ed_lines_start(&t->lines, i) != starts[i])
            return 0;
    for (i = 0; i < count; i++) {
        int64_t end = starts[i + 1];

        if (i + 1 < count)
            end -= (end >= 2 && s[end - 2] == '\r' && s[end - 1] == '\n')
                   ? 2 : 1;
        if (tiku_ed_text_line_end(t, i) != end)
            return 0;
    }
    for (i = 0, line = 0; i <= n; i++) {
        while (line + 1 < count && starts[line + 1] <= i)
            line++;
        if (tiku_ed_lines_find(&t->lines, i) != line)
            return 0;
    }
    return 1;
}

/** @brief A text holding @p s, built by one insertion. */
static void
text_of(tiku_ed_text_t *t, const char *s, int64_t n)
{
    check(tiku_ed_text_init(t), "room", "init");
    check(tiku_ed_text_reserve(t, n, tiku_ed_text_line_cost(s, n)), "room",
          "reserve");
    tiku_ed_text_insert(t, 0, s, n);
}

/** @brief Replace [pos, pos+del) of @p t by @p ins, as a transaction does:
 *         room first, then the deletion and the insertion. */
static void
replace(tiku_ed_text_t *t, int64_t pos, int64_t del, const char *ins,
        int64_t n)
{
    check(tiku_ed_text_reserve(t, n, tiku_ed_text_line_cost(ins, n)), "room",
          "reserve");
    tiku_ed_text_delete(t, pos, del);
    tiku_ed_text_insert(t, pos, ins, n);
}

/** @brief testCellBuffer.cxx, CellBuffer: the line cases. */
static void
cell_buffer(void)
{
    tiku_ed_text_t t;

    text_of(&t, "Scintilla", 9);
    check(t.bytes.length == 9 && tiku_ed_lines_count(&t.lines) == 1 &&
          tiku_ed_lines_start(&t.lines, 0) == 0 &&
          tiku_ed_lines_find(&t.lines, 0) == 0 &&
          tiku_ed_lines_start(&t.lines, 1) == 9 &&
          tiku_ed_lines_find(&t.lines, 9) == 0, "CellBuffer/InsertOneLine",
          "one line of 9");
    tiku_ed_text_free(&t);

    text_of(&t, "Two\nLines", 9);
    check(tiku_ed_lines_count(&t.lines) == 2 &&
          tiku_ed_lines_start(&t.lines, 1) == 4 &&
          tiku_ed_lines_find(&t.lines, 5) == 1 &&
          tiku_ed_lines_start(&t.lines, 2) == 9 &&
          tiku_ed_lines_find(&t.lines, 9) == 1, "CellBuffer/InsertTwoLines",
          "two lines");
    tiku_ed_text_free(&t);

    {
        static const struct { const char *s; int n, lines; } ends[] = {
            { "Two\nLines", 9, 2 },             /* Unix LF             */
            { "Two\r\nLines", 10, 2 },          /* Windows CRLF        */
            { "Two\rLines", 9, 2 },             /* old macOS CR        */
            /* Adapted: NEL and LS are ordinary characters here, not
             * line ends. */
            { "Two\xc2\x85Lines", 10, 1 },
            { "Two\xe2\x80\xa8Lines", 11, 1 }
        };
        size_t i;

        for (i = 0; i < sizeof ends / sizeof *ends; i++) {
            text_of(&t, ends[i].s, ends[i].n);
            if (ends[i].lines == 2)
                check(tiku_ed_text_line_end(&t, 0) == 3 &&
                      tiku_ed_text_line_end(&t, 1) == ends[i].n,
                      "CellBuffer/LineEnds", "the break excluded");
            else
                check(tiku_ed_lines_count(&t.lines) == 1 &&
                      tiku_ed_text_line_end(&t, 0) == ends[i].n,
                      "CellBuffer/LineEnds", "NEL and LS stay in the line");
            check(tiku_ed_text_line_end(&t, -1) < 0, "CellBuffer/LineEnds",
                  "-1 before the start");
            replace(&t, 0, ends[i].n, "", 0);
            check(t.bytes.length == 0 && tiku_ed_lines_count(&t.lines) == 1,
                  "CellBuffer/LineEnds", "deleted back to empty");
            tiku_ed_text_free(&t);
        }
    }

    /* "Insert separates new line bytes", adapted from its UTF-16 index
     * to the line starts it implies. */
    text_of(&t, "a\r\nb", 4);
    replace(&t, 2, 0, "!", 1);
    check(tiku_ed_lines_count(&t.lines) == 3 &&
          tiku_ed_lines_start(&t.lines, 1) == 2 &&
          tiku_ed_lines_start(&t.lines, 2) == 4 &&
          tiku_ed_lines_start(&t.lines, 3) == 5 && agrees(&t, "a\r!\nb", 5),
          "CharacterIndex/Insert separates new line bytes",
          "a CR | ! | LF b");
    tiku_ed_text_free(&t);
}

/** @brief Every deletion and insertion over strings of a, CR and LF up to
 *         four bytes long, inserting up to three: each CRLF split and join
 *         the index has to see. */
static void
exhaustive(void)
{
    static const char alphabet[3] = { 'a', '\r', '\n' };
    char base[8], ins[4], want[16];
    int blen, b, pos, del, ilen, k;
    long cases = 0;

    for (blen = 0; blen <= 4; blen++) {
        int bases = 1;

        for (k = 0; k < blen; k++)
            bases *= 3;
        for (b = 0; b < bases; b++) {
            int code = b;

            for (k = 0; k < blen; k++, code /= 3)
                base[k] = alphabet[code % 3];
            for (pos = 0; pos <= blen; pos++)
            for (del = 0; del <= blen - pos; del++)
            for (ilen = 0; ilen <= 3; ilen++) {
                int inserts = 1, i;

                for (k = 0; k < ilen; k++)
                    inserts *= 3;
                for (i = 0; i < inserts; i++) {
                    tiku_ed_text_t t;
                    int c = i, n;

                    for (k = 0; k < ilen; k++, c /= 3)
                        ins[k] = alphabet[c % 3];
                    memcpy(want, base, (size_t)pos);
                    memcpy(want + pos, ins, (size_t)ilen);
                    memcpy(want + pos + ilen, base + pos + del,
                           (size_t)(blen - pos - del));
                    n = blen - del + ilen;
                    text_of(&t, base, blen);
                    replace(&t, pos, del, ins, ilen);
                    check(agrees(&t, want, n), "LINES-CRLF",
                          "split and join of every short text");
                    tiku_ed_text_free(&t);
                    cases++;
                }
            }
        }
    }
    check(cases > 50000, "LINES-CRLF", "the sweep ran");
}

/** @brief CellBufferLong/Random, adapted: upstream's insertions and
 *         deletions, then the same with line breaks in the alphabet, the
 *         lines checked against the model after every step. */
static void
random_runs(void)
{
    static const char alphabet[] = "ab\r\n\xc3\xa9";
    static char model[4096];
    tiku_ed_text_t t;
    int64_t n = 0;
    int round, i;

    for (round = 0; round < 2; round++) {
        text_of(&t, "", 0);
        n = 0;
        for (i = 0; i < 20000; i++) {
            char ins[16];
            int64_t pos = next((unsigned)n + 1), len = next(10) + 1, j;

            if (next(10) <= 4 && n + len < (int64_t)sizeof model) {
                for (j = 0; j < len; j++)
                    ins[j] = round ? alphabet[next(sizeof alphabet - 1)]
                                   : (char)('a' + j);
                replace(&t, pos, 0, ins, len);
                memmove(model + pos + len, model + pos, (size_t)(n - pos));
                memcpy(model + pos, ins, (size_t)len);
                n += len;
            } else if (pos + len <= n) {
                replace(&t, pos, len, "", 0);
                memmove(model + pos, model + pos + len,
                        (size_t)(n - pos - len));
                n -= len;
            }
            if (round || i % 97 == 0)
                check(agrees(&t, model, n), "CellBufferLong/Random",
                      "lines after each step");
        }
        tiku_ed_text_free(&t);
    }
}

int
main(void)
{
    cell_buffer();
    exhaustive();
    random_runs();
    printf("%s\n", fails ? "FAIL: editor lines" :
           "PASS: editor lines -- upstream CellBuffer cases, every CR/LF "
           "split and join of short texts, random runs");
    return fails != 0;
}
