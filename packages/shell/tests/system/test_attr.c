/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_attr.c - typed descriptors: parsing, formatting, refusing.
 *
 * The descriptors used here are the ones a real board actually reports, read
 * off its own manifest, not invented shapes.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "tiku_attr.h"
#include "tiku_trk_view_defs.h"

static int fails;

/** @brief One "pixel" per character, so a width is a character count. */
static int
char_measure(const char *text, void *ctx)
{
    (void)ctx;
    return (int)strlen(text);
}

static void
check(int cond, const char *what)
{
    printf("  [%s] %s\n", cond ? "PASS" : "FAIL", what);
    if (!cond) { fails++; }
}

static void
fmt_is(const char *meta, const char *raw, const char *want)
{
    tiku_desc_t d;
    char got[64], msg[160];

    (void)tiku_desc_parse(meta, &d);
    (void)tiku_attr_format(&d, raw, got, sizeof got);
    snprintf(msg, sizeof msg, "%-24s %-10s -> \"%s\"", meta, raw, want);
    check(strcmp(got, want) == 0, msg);
    if (strcmp(got, want) != 0) {
        printf("         got \"%s\"\n", got);
    }
}

int
main(void)
{
    printf("descriptors the board really reports\n");
    {
        tiku_desc_t d;

        check(tiku_desc_parse("u32,bytes,static,free", &d) == 1 &&
              d.vtype == TIKU_VT_U32 && d.unit == TIKU_UNIT_BYTES &&
              d.fresh == TIKU_FRESH_STATIC,
              "u32,bytes,static,free parses to all four fields");
        check(tiku_desc_parse("bool,bool,cached,free", &d) == 1 &&
              d.vtype == TIKU_VT_BOOL && d.unit == TIKU_UNIT_BOOL,
              "bool,bool,cached,free parses");
        check(tiku_desc_parse("u32,Hz,static,free", &d) == 1 &&
              d.unit == TIKU_UNIT_HZ, "u32,Hz,static,free parses");
        check(tiku_desc_parse("u32,s,cached,free", &d) == 1 &&
              d.unit == TIKU_UNIT_SECONDS, "u32,s,cached,free parses");
        check(tiku_desc_parse("u32,count,cached,free", &d) == 1 &&
              d.unit == TIKU_UNIT_COUNT, "u32,count,cached,free parses");

        /* An untyped node is the common case and must not be an error. */
        check(tiku_desc_parse("-", &d) == 0 &&
              d.vtype == TIKU_VT_NONE, "\"-\" is untyped, not a failure");
        check(tiku_desc_parse("", &d) == 0, "so is an empty descriptor");
        /* A device that grows a type nobody here knows must still be listed. */
        check(tiku_desc_parse("f64,degC,live,slow", &d) == 0,
              "an unrecognised vtype degrades to untyped rather than lying");
        check(tiku_desc_parse("u32,degC,live,slow", &d) == 1 &&
              d.unit == TIKU_UNIT_OTHER,
              "a known vtype with an unknown unit keeps the unit name");

        check(tiku_desc_parse("u32,count,cached,free,0..100", &d) == 1 &&
              d.has_range && d.lo == 0 && d.hi == 100,
              "an optional lo..hi range is picked up");
        check(tiku_desc_parse("u32,count,cached,free,0..100;read=poll",
                              &d) == 1 &&
              d.has_range && d.hi == 100 && !strcmp(d.cost, "free"),
              "revision-4 read policy preserves the range and cost");
        check(tiku_desc_parse("str,,cached,free;read=consume", &d) == 1 &&
              !strcmp(d.cost, "free") && !d.has_range,
              "read policy is not part of the cost field");
        check(tiku_attr_read_policy("u32,count,cached,free;read=poll") == 1,
              "passive counters can be refreshed automatically");
        check(tiku_attr_read_policy("str,,cached,free;read=consume") == 0 &&
              tiku_attr_read_policy("str,,live,bus;read=effect") == 0 &&
              tiku_attr_read_policy("u32,adc,live,periph;read=sample") == 0,
              "streams, commands and live samples need explicit reads");
        check(tiku_attr_read_policy("str,,cached,free;read=poll;secret") == 0 &&
              tiku_attr_read_policy("str,,cached,free;secret;read=poll") == 0,
              "secret wins regardless of metadata order");
        check(tiku_attr_read_policy("str,,cached,free;read=polluted") == 0 &&
              tiku_attr_read_policy("str,,cached,free;read=") == 0 &&
              tiku_attr_read_policy(
                  "str,,cached,free;read=poll;read=consume") == 0 &&
              tiku_attr_read_policy("str,,cached,free;read=poll;secr") == 0,
              "unknown, truncated and conflicting policies fail closed");
        check(tiku_attr_read_policy(NULL) == -1 &&
              tiku_attr_read_policy("-") == -1 &&
              tiku_attr_read_policy("u32,count,cached,free") == -1,
              "legacy metadata remains identifiable");
        check(!tiku_attr_autoread("-", "/sys/cold_boots") &&
              !tiku_attr_autoread(NULL, "/dev/console") &&
              !tiku_attr_autoread("-", "/dev/i2c/scan") &&
              !tiku_attr_autoread("-", "/sys/vfs/events") &&
              tiku_attr_autoread("-", "/sys/uptime"),
              "without a policy, only nodes that act when read are refused");
        check(tiku_attr_autoread("u32,s,cached,free;read=poll",
                                 "/sys/cold_boots") &&
              !tiku_attr_autoread("u32,s,cached,free;read=effect",
                                  "/sys/uptime"),
              "a stated policy decides whatever the node's name");
        check(!tiku_attr_autoread("-", NULL),
              "a node with no path is not read unasked");
    }

    printf("\nformatting: the descriptor is what makes a reading readable\n");
    fmt_is("u32,bytes,static,free", "1024", "1.0 KiB");
    fmt_is("u32,bytes,static,free", "512", "512 bytes");
    fmt_is("u32,bytes,static,free", "1", "1 byte");
    fmt_is("u32,bytes,cached,free", "32768", "32.0 KiB");
    fmt_is("u32,bytes,cached,free", "1572864", "1.5 MiB");
    fmt_is("bool,bool,cached,free", "0", "off");
    fmt_is("bool,bool,cached,free", "1", "on");
    fmt_is("u32,Hz,static,free", "200000000", "200 MHz");
    fmt_is("u32,Hz,static,free", "32768", "32.768 kHz");
    fmt_is("u32,count,cached,free", "1234567", "1 234 567");
    fmt_is("u32,count,cached,free", "30", "30");
    /* Uptime, the value that made this worth doing: 57471 raw says nothing.
     * A duration reads H:MM:SS, and M:SS under the hour (MA-035). */
    fmt_is("u32,s,cached,free", "57471", "15:57:51");
    fmt_is("u32,s,cached,free", "90061", "25:01:01");
    fmt_is("u32,s,cached,free", "45", "0:45");
    fmt_is("u32,s,cached,free", "605", "10:05");
    fmt_is("-", "hello", "hello");

    printf("\nan unread value is not an empty value\n");
    {
        tiku_desc_t d;
        char got[32];

        (void)tiku_desc_parse("u32,bytes,static,free", &d);
        (void)tiku_attr_format(&d, NULL, got, sizeof got);
        check(strcmp(got, "-") == 0, "an unread node shows \"-\"");
        (void)tiku_attr_format(&d, "0", got, sizeof got);
        check(strcmp(got, "0 bytes") == 0, "a node that really reads 0 does not");
    }

    printf("\nrefusing a bad write before it reaches the device\n");
    {
        tiku_desc_t b, u, r;
        char err[128];

        (void)tiku_desc_parse("bool,bool,cached,free", &b);
        (void)tiku_desc_parse("u32,count,cached,free", &u);
        (void)tiku_desc_parse("u32,count,cached,free,0..100", &r);

        check(tiku_attr_validate(&b, "on", err, sizeof err) == 1,
              "a boolean node takes \"on\"");
        check(tiku_attr_validate(&b, "true", err, sizeof err) == 1 &&
              tiku_attr_validate(&b, "1", err, sizeof err) == 1,
              "and \"true\" and \"1\"");
        check(tiku_attr_validate(&b, "banana", err, sizeof err) == 0,
              "but not \"banana\"");
        check(strstr(err, "on or off") != NULL,
              "and the refusal says what it wanted, not \"invalid input\"");

        check(tiku_attr_validate(&u, "12", err, sizeof err) == 1,
              "a u32 node takes a number");
        check(tiku_attr_validate(&u, "12x", err, sizeof err) == 0 &&
              strstr(err, "not a number") != NULL,
              "and refuses trailing rubbish by name");
        check(tiku_attr_validate(&u, "-5", err, sizeof err) == 0 &&
              strstr(err, "negative") != NULL,
              "and refuses a negative value for an unsigned node");

        check(tiku_attr_validate(&r, "50", err, sizeof err) == 1,
              "a ranged node takes a value inside its range");
        check(tiku_attr_validate(&r, "500", err, sizeof err) == 0 &&
              strstr(err, "0..100") != NULL,
              "and names the range when refusing one outside it");

        /* MA-078: a BOOLEAN reads an empty field as false -- clearing the
         * box is the input.  Every other type has nothing to send. */
        check(tiku_attr_validate(&b, "", err, sizeof err) == 1,
              "an empty edit on a boolean is false, not a refusal");
        {
            char c[8];

            (void)tiku_attr_canonical(&b, "", c, sizeof c);
            check(strcmp(c, "0") == 0, "and it is sent as 0");
        }
        {
            tiku_desc_t u;

            (void)tiku_desc_parse("u32,count,live,free", &u);
            check(tiku_attr_validate(&u, "", err, sizeof err) == 0 &&
                  strstr(err, "nothing") != NULL,
                  "while an empty edit on a number is still nothing to write");
        }
    }

    printf("\ncanonical form\n");
    {
        tiku_desc_t b;
        char out[16];

        (void)tiku_desc_parse("bool,bool,cached,free", &b);
        (void)tiku_attr_canonical(&b, "on", out, sizeof out);
        check(strcmp(out, "1") == 0, "\"on\" is sent as 1");
        (void)tiku_attr_canonical(&b, "false", out, sizeof out);
        check(strcmp(out, "0") == 0, "\"false\" is sent as 0");
    }

    printf("\nsorting uses the value, not the text shown\n");
    {
        tiku_desc_t d;

        (void)tiku_desc_parse("u32,bytes,static,free", &d);
        /* Shown, these are "9 bytes" and "10.0 KiB"; compared as text the
         * larger one sorts first. */
        check(tiku_attr_compare(&d, "9", "10240") < 0,
              "9 bytes sorts before 10 KiB");
        check(tiku_attr_compare(&d, "10240", "9") > 0, "and the reverse");
        check(tiku_attr_compare(&d, "100", "100") == 0, "equal is equal");
        check(strcmp("9", "10240") > 0,
              "(comparing the raw text would have got this wrong)");
    }

    printf("\na negative interval keeps its sign and its magnitude\n");
    /* Dividing a negative total truncates every field toward zero, so an
     * hour ago comes out as "0s" -- now -- unless the sign is carried. */
    fmt_is("i32,s,cached,free", "-3600", "-1:00:00");
    fmt_is("i32,s,cached,free", "-90", "-1:30");
    fmt_is("i32,s,cached,free", "-1", "-0:01");
    fmt_is("i32,s,cached,free", "0", "0:00");

    printf("\na size may be typed with its prefix\n");
    {
        tiku_desc_t d;
        char err[128], out[32];

        (void)tiku_desc_parse("u32,bytes,cached,free", &d);
        check(tiku_attr_validate(&d, "64k", err, sizeof err),
              "\"64k\" is a number");
        (void)tiku_attr_canonical(&d, "64k", out, sizeof out);
        check(strcmp(out, "65536") == 0,
              "and the device is sent 65536, not the suffix");
        (void)tiku_attr_canonical(&d, "2M", out, sizeof out);
        check(strcmp(out, "2097152") == 0, "2M is two mebibytes");
        (void)tiku_attr_canonical(&d, "1kb", out, sizeof out);
        check(strcmp(out, "1024") == 0, "the trailing b is allowed");
        check(!tiku_attr_validate(&d, "64q", err, sizeof err),
              "but an unknown suffix is still refused");
        check(!tiku_attr_validate(&d, "64k9", err, sizeof err),
              "and so is trailing rubbish after one");
    }

    printf("\ndates take the longest form that fits (MA-025)\n");
    {
        /* A fixed instant, so the check is about the LADDER and not about
         * what today happens to be: 2026-08-07 15:04:05 local. */
        struct tm tmv;
        time_t when;
        char out[96];
        int wide, narrow;

        memset(&tmv, 0, sizeof tmv);
        tmv.tm_year = 126;              /* 2026 */
        tmv.tm_mon = 7;                 /* August */
        tmv.tm_mday = 7;
        tmv.tm_hour = 15;
        tmv.tm_min = 4;
        tmv.tm_sec = 5;
        tmv.tm_isdst = -1;
        when = mktime(&tmv);

        (void)tiku_attr_time_fit((int64_t)when, 100000, char_measure,
                                     NULL, out, sizeof out);
        check(strstr(out, "August") != NULL && strstr(out, "2026") != NULL &&
              strstr(out, ":04:05") != NULL,
              "a wide column gets the long date and the seconds");
        wide = (int)strlen(out);

        (void)tiku_attr_time_fit((int64_t)when, wide - 1, char_measure,
                                     NULL, out, sizeof out);
        check(strstr(out, "August") != NULL && strstr(out, ":04:05") == NULL,
              "one character less and the seconds are the first thing to go");
        wide = (int)strlen(out);

        (void)tiku_attr_time_fit((int64_t)when, wide - 1, char_measure,
                                     NULL, out, sizeof out);
        check(strstr(out, "August") == NULL && strstr(out, "Aug") != NULL &&
              strstr(out, "PM") != NULL,
              "narrower still and the month shortens, keeping the time");
        wide = (int)strlen(out);

        (void)tiku_attr_time_fit((int64_t)when, wide - 1, char_measure,
                                     NULL, out, sizeof out);
        check(strstr(out, "Aug") == NULL && strstr(out, "08/07/26") != NULL &&
              strstr(out, "PM") != NULL,
              "then the date itself goes numeric, still with the time");
        wide = (int)strlen(out);

        (void)tiku_attr_time_fit((int64_t)when, wide - 1, char_measure,
                                     NULL, out, sizeof out);
        check(strcmp(out, "08/07/26") == 0,
              "a column too narrow for any pair gets the date alone");
        narrow = (int)strlen(out);

        (void)tiku_attr_time_fit((int64_t)when, narrow - 2, char_measure,
                                     NULL, out, sizeof out);
        check(strstr(out, "\xe2\x80\xa6") != NULL,
              "and only then is it elided");

        (void)tiku_attr_time_fit(0, 100000, char_measure, NULL, out,
                                     sizeof out);
        check(strcmp(out, "-") == 0,
              "a store with no wall clock says so instead of claiming 1970");
    }

    printf("\nfive stars out of ten, with halves (MA-037)\n");
    {
        char out[16];

        (void)tiku_attr_rating(0, out, sizeof out);
        check(strcmp(out, ".....") == 0, "0 of 10 is five empty cells");
        (void)tiku_attr_rating(10, out, sizeof out);
        check(strcmp(out, "*****") == 0, "10 of 10 is five full ones");
        (void)tiku_attr_rating(5, out, sizeof out);
        check(strcmp(out, "**+..") == 0,
              "5 is two full, a half, and two empty");
        (void)tiku_attr_rating(1, out, sizeof out);
        check(strcmp(out, "+....") == 0,
              "a value that has only just entered a cell half-fills it");
        (void)tiku_attr_rating(99, out, sizeof out);
        check(strcmp(out, "*****") == 0, "above the maximum is clamped");
        (void)tiku_attr_rating(-4, out, sizeof out);
        check(strcmp(out, ".....") == 0, "and below zero is too");
    }

    printf("\nthe same cells over a declared range\n");
    {
        tiku_desc_t d;
        char out[16];

        (void)tiku_desc_parse("u32,count,live,free,0..100", &d);
        check(tiku_attr_gauge(&d, "100", 4, out, sizeof out) == 4 &&
              strcmp(out, "****") == 0, "a full reading fills the gauge");
        check(tiku_attr_gauge(&d, "0", 4, out, sizeof out) == 4 &&
              strcmp(out, "....") == 0, "and an empty one empties it");
        (void)tiku_desc_parse("u32,count,live,free", &d);
        check(tiku_attr_gauge(&d, "50", 4, out, sizeof out) == 0,
              "a node that declares no range gets no gauge at all");
    }

    printf("\nsizes are human-readable and degrade (MA-023)\n");
    {
        char out[40];

        (void)tiku_attr_size_fit(-1, 100000, char_measure, NULL, out,
                                     sizeof out);
        check(strcmp(out, "-") == 0,
              "an unknown size is \"-\", not \"0 B\" -- they are different "
              "facts");
        (void)tiku_attr_size_fit(0, 100000, char_measure, NULL, out,
                                     sizeof out);
        check(strcmp(out, "0 B") == 0, "and an empty file really is 0 B");
        (void)tiku_attr_size_fit(999, 100000, char_measure, NULL, out,
                                     sizeof out);
        check(strcmp(out, "999 B") == 0, "under a kibibyte it is bytes");
        (void)tiku_attr_size_fit(1024, 100000, char_measure, NULL, out,
                                     sizeof out);
        check(strcmp(out, "1.0 KiB") == 0,
              "at a kibibyte it scales, and the insignificant trailing zero "
              "goes");
        (void)tiku_attr_size_fit(1536, 100000, char_measure, NULL, out,
                                     sizeof out);
        check(strcmp(out, "1.5 KiB") == 0,
              "1.50 loses its trailing zero too -- the rule is about the "
              "second decimal being insignificant, not about the first");
        (void)tiku_attr_size_fit(1500, 100000, char_measure, NULL, out,
                                     sizeof out);
        check(strcmp(out, "1.46 KiB") == 0,
              "but a significant second decimal stays");
        (void)tiku_attr_size_fit(5ll * 1024 * 1024 * 1024, 100000,
                                     char_measure, NULL, out, sizeof out);
        check(strstr(out, "GiB") != NULL, "and it climbs the units");
        (void)tiku_attr_size_fit(1536, 6, char_measure, NULL, out,
                                     sizeof out);
        check(strcmp(out, "1536") == 0,
              "too narrow for the formatted form and it falls back to the "
              "plain count rather than eliding something readable");
    }

    printf("\ntruncation from the beginning (TS-041)\n");
    {
        char out[64];

        (void)tiku_text_fit("abcdefghij", 6, TIKU_TRUNC_BEGIN,
                                char_measure, NULL, out, sizeof out);
        check(strcmp(out + strlen(out) - 3, "hij") == 0,
              "what survives is the TAIL -- the newest characters typed");
        check(out[0] != 'a',
              "and the head is what was given up");
        (void)tiku_text_fit("abc", 10, TIKU_TRUNC_BEGIN,
                                char_measure, NULL, out, sizeof out);
        check(strcmp(out, "abc") == 0,
              "a string that fits is left alone");
    }

    printf("\nwhich end a column gives up (MA-026)\n");
    {
        check(!tiku_trk_col_trunc_end(TIKU_TRK_COL_NAME) &&
              !tiku_trk_col_trunc_end(TIKU_TRK_COL_VALUE) &&
              !tiku_trk_col_trunc_end(TIKU_TRK_COL_PATH) &&
              !tiku_trk_col_trunc_end(TIKU_TRK_COL_ATTR_BASE),
              "generic strings keep both ends -- the middle goes");
        check(tiku_trk_col_trunc_end(TIKU_TRK_COL_SIZE) &&
              tiku_trk_col_trunc_end(TIKU_TRK_COL_DATE),
              "formatted values keep their head -- the end goes");
    }

    printf("\ndisambiguating path suffixes (NeXT UIG ch8)\n");
    {
        const char *rows[4] = { "/net/work/notes.txt",
                                "/net/home/notes.txt",
                                "/net/home/todo.txt",
                                "/deep/a/x/notes.txt" };
        char out[128];

        (void)tiku_attr_disambiguate(rows[2], rows, 4, out, sizeof out);
        check(strcmp(out, "todo.txt") == 0,
              "NEXT-91: a name with no same-named neighbour stays plain");
        (void)tiku_attr_disambiguate(rows[0], rows, 4, out, sizeof out);
        check(strcmp(out, "notes.txt -- .../work") == 0,
              "NEXT-91: the MINIMUM suffix, one component when enough");
        (void)tiku_attr_disambiguate(rows[3], rows, 4, out, sizeof out);
        check(strcmp(out, "notes.txt -- .../x") == 0,
              "NEXT-91: computed against the rows shown, not path depth");
        {
            const char *two[2] = { "/a/b/n.txt", "/c/b/n.txt" };

            (void)tiku_attr_disambiguate(two[0], two, 2, out,
                                             sizeof out);
            check(strcmp(out, "n.txt -- /a/b") == 0,
                  "NEXT-91: a whole parent is shown whole, no dots");
        }
    }

    printf("\n%s\n", fails ? "FAILURES" : "every attribute rule holds");
    return fails ? 1 : 0;
}
