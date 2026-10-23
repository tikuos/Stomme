/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_match.c - the five name-match rules, against TrackerString's source.
 *
 * The cases that matter are the ones a naive implementation gets wrong: an
 * ends-with that is really "the last occurrence is at the end", a wildcard
 * that cannot back up out of a failed tail, and a bad regular expression
 * that silently matches nothing instead of being reported.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <string.h>

#include "tiku_trk_match.h"

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
    char err[80];

    printf("name matching (TrackerString)\n");

    /* --- starts / ends / contains --------------------------------------- */
    check(tiku_trk_match("gpio_bank1", "gpio", 0, TIKU_TRK_MATCH_STARTS),
          "Q-047", "starts with");
    check(!tiku_trk_match("a_gpio", "gpio", 0, TIKU_TRK_MATCH_STARTS),
          "Q-047", "starts with is anchored at the front");
    check(tiku_trk_match("GPIO_bank1", "gpio", 0, TIKU_TRK_MATCH_STARTS),
          "AW-080", "ignore case is the default");
    check(!tiku_trk_match("GPIO_bank1", "gpio", 1, TIKU_TRK_MATCH_STARTS),
          "AW-080", "and case sensitivity is honoured when asked");
    check(tiku_trk_match("led0.state", "state", 0, TIKU_TRK_MATCH_ENDS),
          "Q-047", "ends with");
    check(!tiku_trk_match("state.led0", "state", 0, TIKU_TRK_MATCH_ENDS),
          "Q-047", "an occurrence that is not at the end does not count");
    check(!tiku_trk_match("ate", "state", 0, TIKU_TRK_MATCH_ENDS),
          "Q-047", "a name shorter than the expression cannot end with it");
    check(tiku_trk_match("uart2_baud", "t2_b", 0, TIKU_TRK_MATCH_CONTAINS),
          "Q-047", "contains");

    /* --- wildcard ------------------------------------------------------- */
    check(tiku_trk_match("uart2_baud", "uart*", 0, TIKU_TRK_MATCH_GLOB),
          "AW-080", "trailing star matches the rest");
    check(tiku_trk_match("uart2_baud", "u?rt*", 0, TIKU_TRK_MATCH_GLOB),
          "AW-080", "? matches exactly one");
    check(!tiku_trk_match("uart", "uart?", 0, TIKU_TRK_MATCH_GLOB),
          "AW-080", "? needs a character to consume");
    check(tiku_trk_match("xaybz", "*a*b*", 0, TIKU_TRK_MATCH_GLOB),
          "AW-080", "a failed tail backs up to the last star");
    check(!tiku_trk_match("hello", "*a*b", 0, TIKU_TRK_MATCH_GLOB),
          "AW-080", "and still fails when it genuinely cannot match");
    check(tiku_trk_match("gpio3", "gpio[0-9]", 0, TIKU_TRK_MATCH_GLOB),
          "AW-080", "bracket range");
    check(!tiku_trk_match("gpioX", "gpio[0-9]", 0, TIKU_TRK_MATCH_GLOB),
          "AW-080", "outside the range");
    check(tiku_trk_match("gpioX", "gpio[^0-9]", 0, TIKU_TRK_MATCH_GLOB),
          "AW-080", "inverted bracket");
    check(!tiku_trk_match("gpio3", "gpio[0-9", 0, TIKU_TRK_MATCH_GLOB),
          "AW-080", "an unclosed bracket is a failure, not a literal");

    /* --- regular expression --------------------------------------------- */
    check(tiku_trk_match("led0", "^led0$", 0, TIKU_TRK_MATCH_REGEX),
          "PVK-047", "anchored exact match -- how Tracker reveals a ref");
    check(!tiku_trk_match("led01", "^led0$", 0, TIKU_TRK_MATCH_REGEX),
          "PVK-047", "and the anchor really anchors");
    check(tiku_trk_match("uart2_baud", "art2", 0, TIKU_TRK_MATCH_REGEX),
          "Q-050", "an unanchored expression searches anywhere");
    check(tiku_trk_match("gpio_bank_12", "bank_[0-9]+$", 0,
                         TIKU_TRK_MATCH_REGEX),
          "Q-050", "class with +");
    check(tiku_trk_match("aaab", "a*b", 0, TIKU_TRK_MATCH_REGEX),
          "Q-050", "greedy star gives back enough to match");
    check(tiku_trk_match("b", "a*b", 0, TIKU_TRK_MATCH_REGEX),
          "Q-050", "star matches zero");
    check(tiku_trk_match("xyllo", "(he|xy)llo", 0, TIKU_TRK_MATCH_REGEX),
          "Q-050", "alternation inside a group");
    check(!tiku_trk_match("zyllo", "(he|xy)llo", 0, TIKU_TRK_MATCH_REGEX),
          "Q-050", "and a branch that does not apply does not match");
    check(tiku_trk_match("abcabc", "^(abc)+$", 0, TIKU_TRK_MATCH_REGEX),
          "Q-050", "a repeated group");
    check(tiku_trk_match("a.b", "a\\.b", 1, TIKU_TRK_MATCH_REGEX),
          "Q-050", "escape makes a metacharacter literal");
    check(!tiku_trk_match("axb", "a\\.b", 1, TIKU_TRK_MATCH_REGEX),
          "Q-050", "and the escaped dot is no longer any character");
    check(tiku_trk_match("LED0", "led0", 0, TIKU_TRK_MATCH_REGEX),
          "Q-050", "case insensitivity by lowercasing both sides");

    /* --- a bad expression is REPORTED ----------------------------------- */
    check(!tiku_trk_regex_check("a(b", err, sizeof err) &&
          strstr(err, "(") != NULL,
          "Q-048", "an unmatched ( is reported by name");
    check(!tiku_trk_regex_check("a[b", err, sizeof err) &&
          strstr(err, "[") != NULL,
          "Q-048", "an unmatched [ is reported by name");
    check(!tiku_trk_regex_check("*a", err, sizeof err),
          "Q-048", "a quantifier with nothing to repeat is reported");
    check(tiku_trk_regex_check("^gpio_[0-9]+$", err, sizeof err),
          "Q-048", "a good expression passes the check");

    /* --- kNone ---------------------------------------------------------- */
    check(!tiku_trk_match("anything", "anything", 0, TIKU_TRK_MATCH_NONE),
          "Q-047", "the unset rule matches nothing, not everything");

    printf("\na glyph is a glyph, not a byte (Q-049)\n");
    check(tiku_trk_match("caf\xc3\xa9", "caf?", 1, TIKU_TRK_MATCH_GLOB),
          "Q-049", "one '?' eats one accented glyph, not half of one");
    check(!tiku_trk_match("caf\xc3\xa9", "caf??", 1, TIKU_TRK_MATCH_GLOB),
          "Q-049", "and two question marks find only one glyph there");
    check(tiku_trk_match("caf\xc3\xa9", "caf[\xc3\xa9x]", 1,
                         TIKU_TRK_MATCH_GLOB),
          "Q-049", "a bracket class lists whole glyphs");
    check(!tiku_trk_match("caf\xc3\xa8", "caf[\xc3\xa9x]", 1,
                          TIKU_TRK_MATCH_GLOB),
          "Q-049", "and a different accent is a different glyph");
    check(tiku_trk_match("na\xc3\xafve.txt", "*\xc3\xaf*", 1,
                         TIKU_TRK_MATCH_GLOB),
          "Q-049", "a star scans by glyph and lands on the right one");

    printf("\n%s\n", fails ? "FAILURES" : "all match rules hold");
    return fails ? 1 : 0;
}
