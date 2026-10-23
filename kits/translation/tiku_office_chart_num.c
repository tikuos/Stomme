/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_office_chart_num.c - numbers written as a spreadsheet's number
 * format says: "General", digits and decimals ("0.00", "#,##0"),
 * percentages, scientific notation, currencies and quoted words, a
 * section for negative numbers and one for zero, and dates and times
 * from the serial numbers spreadsheets keep them as.
 *
 * Conditions and colours are read past; fractions are not written as
 * fractions.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_office_chart.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define NF_DIGITS 30                /* decimals, or leading zeros, at most */

/** @brief Output being written, never past its end. */
typedef struct {
    char   *s;                      /* where                             */
    size_t  n, max;                 /* written, and room with its end    */
} nb_t;

/** @brief What one section of a format asks of a number. */
typedef struct {
    int dec;                        /* digits after the point            */
    int lead;                       /* digits before it, at least        */
    int group;                      /* thousands separated               */
    int scale;                      /* thousands divided away            */
    int pct;                        /* percent signs: times 100 each     */
    int sci;                        /* scientific: 1 + exponent digits   */
    int sci_plus;                   /* its sign written when positive    */
    int date;                       /* a date or time, not a number      */
    int ampm;                       /* hours counted to twelve           */
} nf_t;

static const char *const month[] = {
    "January", "February", "March", "April", "May", "June", "July",
    "August", "September", "October", "November", "December"};
static const char *const weekday[] = {
    "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday",
    "Saturday"};

/** @brief @p len bytes of @p t added. */
static void
put(nb_t *b, const char *t, size_t len)
{
    while (len-- > 0 && b->n + 1 < b->max) {
        b->s[b->n++] = *t++;
    }
    b->s[b->n] = '\0';
}

/** @brief A number written as printf's @p fmt writes an int, added. */
static void
puti(nb_t *b, const char *fmt, long v)
{
    char t[32];

    snprintf(t, sizeof t, fmt, v);
    put(b, t, strlen(t));
}

/* ---------------------------------------------------------------- general  */

/** @brief @p v as "General" writes it: up to ten significant digits,
 *         very large and very small numbers in scientific notation. */
static void
general(nb_t *b, double v)
{
    char t[64], *e;
    size_t n;
    long exp;

    if (v == 0) {
        put(b, "0", 1);
        return;
    }
    if (fabs(v) < 1e11 && fabs(v) >= 1e-9) {
        snprintf(t, sizeof t, "%.10g", v);
        if (strchr(t, 'e') == NULL) {
            put(b, t, strlen(t));
            return;
        }
    }
    snprintf(t, sizeof t, "%.5e", v);
    if ((e = strchr(t, 'e')) == NULL) {
        return;
    }
    exp = strtol(e + 1, NULL, 10);
    for (n = (size_t)(e - t); n > 1 && t[n - 1] == '0'; n--) {
    }
    n -= n > 1 && t[n - 1] == '.';
    put(b, t, n);
    puti(b, exp < 0 ? "E-%02ld" : "E+%02ld", labs(exp));
}

/* --------------------------------------------------------------- sections  */

/** @brief Section @p which of format @p fmt (they are split at semicolons
 *         outside quotes and brackets) into @p out.
 *  @return how many sections the format has. */
static int
section(const char *fmt, int which, char *out, size_t max)
{
    int n = 0, quote = 0, bracket = 0;
    size_t o = 0;

    for (; *fmt; fmt++) {
        if (*fmt == '\\' && fmt[1] && !quote) {
            if (n == which && o + 2 < max) {
                out[o++] = fmt[0];
                out[o++] = fmt[1];
            }
            fmt++;
            continue;
        }
        quote = *fmt == '"' && !bracket ? !quote : quote;
        bracket = !quote && *fmt == '[' ? 1 : !quote && *fmt == ']'
                  ? 0 : bracket;
        if (*fmt == ';' && !quote && !bracket) {
            n++;
        } else if (n == which && o + 1 < max) {
            out[o++] = *fmt;
        }
    }
    out[o] = '\0';
    return n + 1;
}

/** @brief Past the literal or bracket at @p p: the last character it
 *         takes.  @p p itself when it starts none. */
static const char *
past_literal(const char *p)
{
    const char *e;

    if (*p == '"') {
        e = strchr(p + 1, '"');
        return e != NULL ? e : p + strlen(p) - 1;
    }
    if (*p == '[') {
        e = strchr(p, ']');
        return e != NULL ? e : p + strlen(p) - 1;
    }
    return (*p == '\\' || *p == '_' || *p == '*') && p[1] ? p + 1 : p;
}

/** @brief A section's digits, point, grouping, percent and exponent. */
static void
shape(const char *s, nf_t *f)
{
    int point = 0, seen = 0, commas = 0;

    memset(f, 0, sizeof *f);
    for (; *s; s++) {
        const char *e = past_literal(s);

        if (e != s || *s == '"') {
            s = e;
        } else if (strncasecmp(s, "AM/PM", 5) == 0 ||
                   strncasecmp(s, "A/P", 3) == 0) {
            f->date = f->ampm = 1;
            s += s[1] == '/' ? 2 : 4;
        } else if (strchr("0#?", *s) != NULL) {
            f->sci += f->sci > 0;
            f->dec += point && !f->sci;
            f->lead += !point && !f->sci && *s == '0';
            f->group |= commas > 0 && !point && !f->sci;
            commas = 0;
            seen = 1;
        } else if (*s == ',' && seen) {
            commas++;
        } else if ((*s == 'E' || *s == 'e') && (s[1] == '+' || s[1] == '-') &&
                   seen) {
            f->sci = 1;
            f->sci_plus = *++s == '+';
        } else if (strchr("yYmMdDhHsS", *s) != NULL) {
            f->date = 1;
        } else {
            f->pct += *s == '%';
            point |= *s == '.';
            f->scale += commas;
            commas = 0;
        }
    }
    f->scale += commas;
    f->dec = f->dec > NF_DIGITS ? NF_DIGITS : f->dec;
    f->lead = f->lead > NF_DIGITS ? NF_DIGITS : f->lead;
}

/* ---------------------------------------------------------------- numbers  */

/** @brief Digits of @p v (not negative) as @p f asks, added: grouped,
 *         padded, with its decimals. */
static void
digits(nb_t *b, double v, const nf_t *f)
{
    char t[512];
    size_t whole, i, from;

    snprintf(t, sizeof t, "%.*f", f->dec, v);
    whole = strcspn(t, ".");
    from = whole == 1 && t[0] == '0' && f->lead == 0;   /* "#.00": .50 */
    for (i = whole; i < (size_t)f->lead; i++) {
        put(b, "0", 1);
    }
    for (i = from; i < whole; i++) {
        put(b, t + i, 1);
        if (f->group && (whole - i - 1) % 3 == 0 && i + 1 < whole) {
            put(b, ",", 1);
        }
    }
    put(b, t + whole, strlen(t + whole));
}

/** @brief @p v (not negative) in scientific notation as @p f asks. */
static void
science(nb_t *b, double v, const nf_t *f)
{
    int exp = v > 0 ? (int)floor(log10(v)) : 0;
    double m = v > 0 ? v / pow(10, exp) : 0;
    char t[64];

    snprintf(t, sizeof t, "%.*f", f->dec, m);
    if (atof(t) >= 10) {            /* 9.99 rounded up */
        exp++;
        snprintf(t, sizeof t, "%.*f", f->dec, m / 10);
    }
    put(b, t, strlen(t));
    put(b, "E", 1);
    if (exp < 0 || f->sci_plus) {
        put(b, exp < 0 ? "-" : "+", 1);
    }
    snprintf(t, sizeof t, "%0*d", f->sci > 9 ? 8 : f->sci > 1 ? f->sci - 1
                                                              : 1, abs(exp));
    put(b, t, strlen(t));
}

/** @brief Add the text a bracketed section writes: a currency bracket
 *         writes its symbol ("[$€-2]" writes "€"); a colour, condition or
 *         locale bracket writes nothing. */
static void
bracket(nb_t *b, const char *p, const char *end)
{
    size_t n;

    if (p[1] != '$') {
        return;
    }
    n = strcspn(p + 2, "-]");
    n = p + 2 + n <= end ? n : 0;
    put(b, p + 2, n);
}

/** @brief A literal's words added: a quoted run, an escaped character, a
 *         space for "_x", nothing for "*x"; its last character returned. */
static const char *
literal(nb_t *b, const char *p)
{
    const char *e = past_literal(p);

    if (*p == '"') {
        put(b, p + 1, (size_t)(e - p - (*e == '"' ? 1 : 0)));
    } else if (*p == '[') {
        bracket(b, p, e);
    } else if (*p == '\\' && e != p) {
        put(b, e, 1);
    } else if (*p == '_' && e != p) {
        put(b, " ", 1);
    } else if (*p != '*' || e == p) {
        put(b, p, 1);
    }
    return e;
}

/** @brief @p v written as number section @p s asks: its words around the
 *         number, a minus first when @p neg. */
static void
number(nb_t *b, double v, int neg, const char *s, const nf_t *f)
{
    int done = 0, i;

    if (strpbrk(s, "0#?") == NULL && strchr(s, '@') != NULL) {
        general(b, neg ? -v : v);   /* a section for text: as it is */
        return;
    }
    for (i = 0; i < f->pct; i++) {
        v *= 100;
    }
    for (i = 0; i < f->scale; i++) {
        v /= 1000;
    }
    if (neg) {
        put(b, "-", 1);
    }
    for (; *s; s++) {
        if (strncasecmp(s, "General", 7) == 0) {
            general(b, v);
            s += 6;
        } else if (strchr("0#?.,", *s) != NULL && (done || *s != ',')) {
            if (!done && f->sci) {
                science(b, v, f);
            } else if (!done) {
                digits(b, v, f);
            }
            done = 1;
        } else if ((*s == 'E' || *s == 'e') && done && f->sci &&
                   (s[1] == '+' || s[1] == '-')) {
            s++;
        } else {
            s = literal(b, s);
        }
    }
}

/* ------------------------------------------------------------------ dates  */

/** @brief Serial day @p v as its date, 1900's way (its day 60 the 29th
 *         of February that never was): year, month, day, weekday from
 *         Sunday into @p t, the seconds into the day into @p secs.
 *  @return 0, or -1 for a serial no calendar holds. */
static int
civil(double v, int *t, long *secs)
{
    long days, era, doe, yoe, doy, mp;

    if (!(v >= 0 && v < 2958466)) {
        return -1;
    }
    *secs = lround((v - floor(v)) * 86400);
    days = (long)floor(v) + (*secs >= 86400);
    *secs %= 86400;
    if (days == 60) {
        t[0] = 1900, t[1] = 2, t[2] = 29, t[3] = 3;
        return 0;
    }
    days -= days < 60 ? 25568 : 25569;     /* from 1 January 1970 */
    t[3] = (int)(((days % 7) + 11) % 7);
    days += 719468;
    era = (days >= 0 ? days : days - 146096) / 146097;
    doe = days - era * 146097;
    yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    mp = (5 * doy + 2) / 153;
    t[2] = (int)(doy - (153 * mp + 2) / 5 + 1);
    t[1] = (int)(mp < 10 ? mp + 3 : mp - 9);
    t[0] = (int)(yoe + era * 400 + (t[1] <= 2));
    return 0;
}

int
ch_date(double v, int *ymd)
{
    long secs;
    int t[4];

    if (civil(v, t, &secs) != 0) {
        return -1;
    }
    ymd[0] = t[0];
    ymd[1] = t[1];
    ymd[2] = t[2];
    return 0;
}

/** @brief Whether the next date letter after @p s is a second's: an "m"
 *         before it counts minutes. */
static int
seconds_next(const char *s)
{
    while (*s && strchr("yYmMdDhHsS", *s) == NULL) {
        s++;
    }
    return *s == 's' || *s == 'S';
}

/** @brief A name, whole or cut to @p n letters, added. */
static void
name(nb_t *b, const char *nm, size_t n)
{
    put(b, nm, n < strlen(nm) ? n : strlen(nm));
}

/** @brief One run of @p n date letter @p c added, from date @p t and
 *         seconds @p secs; @p minute says an "m" counts minutes. */
static void
date_part(nb_t *b, char c, int n, const int *t, long secs, int minute,
          const nf_t *f)
{
    long h = secs / 3600;

    if (c == 'y') {
        puti(b, n > 2 ? "%04ld" : "%02ld", n > 2 ? t[0] : t[0] % 100);
    } else if (c == 'm' && minute) {
        puti(b, n > 1 ? "%02ld" : "%ld", secs / 60 % 60);
    } else if (c == 'm') {
        n < 3 ? puti(b, n > 1 ? "%02ld" : "%ld", t[1])
              : name(b, month[t[1] - 1], n == 3 ? 3 : n == 4 ? 99 : 1);
    } else if (c == 'd') {
        n < 3 ? puti(b, n > 1 ? "%02ld" : "%ld", t[2])
              : name(b, weekday[t[3]], n == 3 ? 3 : 99);
    } else if (c == 'h') {
        h = f->ampm ? (h % 12 == 0 ? 12 : h % 12) : h;
        puti(b, n > 1 ? "%02ld" : "%ld", h);
    } else if (c == 's') {
        puti(b, n > 1 ? "%02ld" : "%ld", secs % 60);
    }
}

/** @brief Serial @p v written as date section @p s asks. */
static void
date(nb_t *b, double v, const char *s, const nf_t *f)
{
    int t[4], n, hour = 0;
    long secs;
    char c;

    if (civil(v, t, &secs) != 0) {
        general(b, v);
        return;
    }
    for (; *s; s++) {
        c = (char)tolower((unsigned char)*s);
        if (strncasecmp(s, "AM/PM", 5) == 0 || strncasecmp(s, "A/P", 3) == 0) {
            name(b, secs < 43200 ? "AM" : "PM", s[1] == '/' ? 1 : 2);
            s += s[1] == '/' ? 2 : 4;
        } else if (strchr("ymdhs", c) != NULL) {
            for (n = 1; tolower((unsigned char)s[n]) == c; n++) {
            }
            date_part(b, c, n, t, secs, n < 3 && (hour ||
                      seconds_next(s + n)), f);
            hour = c == 'h';
            s += n - 1;
        } else {
            s = literal(b, s);
        }
    }
}

/** @brief Spaces a format pads with taken off both ends of @p s. */
static void
trim(char *s)
{
    size_t n = strlen(s), from = strspn(s, " ");

    while (n > from && s[n - 1] == ' ') {
        n--;
    }
    memmove(s, s + from, n - from);
    s[n - from] = '\0';
}

void
ch_number(double v, const char *fmt, char *out, size_t max)
{
    nb_t b;
    char sec[128];
    int n, which;
    nf_t f;

    if (out == NULL || max == 0) {
        return;
    }
    b.s = out;
    b.n = 0;
    b.max = max;
    out[0] = '\0';
    if (!isfinite(v)) {
        return;
    }
    if (fmt == NULL || *fmt == '\0' || strcasecmp(fmt, "General") == 0) {
        general(&b, v);
        return;
    }
    n = section(fmt, 0, sec, sizeof sec);
    which = v < 0 && n >= 2 ? 1 : v == 0 && n >= 3 ? 2 : 0;
    (void)section(fmt, which, sec, sizeof sec);
    shape(sec, &f);
    if (f.date && v >= 0) {
        date(&b, v, sec, &f);
    } else {
        number(&b, fabs(v), v < 0 && which == 0, sec, &f);
    }
    trim(out);
}
