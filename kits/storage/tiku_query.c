/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_query.c - predicates, matching, and walking the namespace.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_query.h"
#include "tiku_state.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#define RESULTS_MAX 512
#define QUEUE_MAX   256

/** @brief Unit named by a relative date, in seconds. */
static int64_t
relative_unit(const char *unit)
{
    if (strcasecmp(unit, "second") == 0 ||
        strcasecmp(unit, "seconds") == 0) {
        return 1;
    }
    if (strcasecmp(unit, "minute") == 0 ||
        strcasecmp(unit, "minutes") == 0) {
        return 60;
    }
    if (strcasecmp(unit, "hour") == 0 ||
        strcasecmp(unit, "hours") == 0) {
        return 3600;
    }
    if (strcasecmp(unit, "day") == 0 ||
        strcasecmp(unit, "days") == 0) {
        return 86400;
    }
    return 0;
}

/** @brief Whether a date spelling moves when the wall clock moves. */
static int
relative_date(const char *text)
{
    long long amount;
    char unit[16], direction[16], tail[16];
    int n;

    if (text == NULL) {
        return 0;
    }
    if (strcasecmp(text, "now") == 0 || strcasecmp(text, "today") == 0 ||
        strcasecmp(text, "yesterday") == 0 ||
        strcasecmp(text, "tomorrow") == 0) {
        return 1;
    }
    n = sscanf(text, "%lld %15s %15s %15s", &amount, unit, direction,
               tail);
    if (n == 3 && relative_unit(unit) != 0 &&
        strcasecmp(direction, "ago") == 0) {
        return 1;
    }
    return n == 4 && relative_unit(unit) != 0 &&
           strcasecmp(direction, "from") == 0 &&
           strcasecmp(tail, "now") == 0;
}

void
tiku_query_init(tiku_query_t *q)
{
    if (q != NULL) {
        memset(q, 0, sizeof *q);
        q->max_results = RESULTS_MAX;
    }
}

void
tiku_query_set_name(tiku_query_t *q, const char *text)
{
    if (q == NULL) {
        return;
    }
    q->mode = TIKU_QMODE_NAME;
    q->term_count = 0;
    /* A star anywhere in the text makes it a whole-name pattern (IS):
     * "*.txt" and "a*b" are patterns, "txt" is a substring (CONTAINS). */
    (void)tiku_query_add_term(q, TIKU_QF_NAME,
                                  (text != NULL && strchr(text, '*') != NULL)
                                      ? TIKU_OP_IS
                                      : TIKU_OP_CONTAINS,
                                  text, TIKU_JOIN_AND);
}

/** @brief Map the formula's field vocabulary back to the model enum. */
static int
formula_field(const char *s, size_t n, tiku_qfield_t *out)
{
    static const struct {
        const char *name;
        tiku_qfield_t field;
    } fields[] = {
        { "name", TIKU_QF_NAME }, { "path", TIKU_QF_PATH },
        { "kind", TIKU_QF_KIND }, { "value", TIKU_QF_VALUE },
        { "size", TIKU_QF_SIZE }, { "cap", TIKU_QF_CAP },
        { "unit", TIKU_QF_UNIT },
        { "writable", TIKU_QF_WRITABLE },
        { "type", TIKU_QF_TYPE },
        { "modified", TIKU_QF_MODIFIED }
    };
    size_t i;

    for (i = 0; i < sizeof fields / sizeof fields[0]; i++) {
        if (strlen(fields[i].name) == n &&
            strncmp(fields[i].name, s, n) == 0) {
            *out = fields[i].field;
            return 1;
        }
    }
    return 0;
}

int
tiku_query_set_formula(tiku_query_t *q, const char *formula)
{
    const char *p = formula;
    tiku_query_t parsed;
    tiku_qjoin_t join = TIKU_JOIN_AND;

    if (q == NULL) {
        return -1;
    }
    /* On every failure path @p q is left reset and marked invalid, so
     * neither the previous formula nor an empty predicate, which matches
     * every node, stays live. */
    tiku_query_init(q);
    q->mode = TIKU_QMODE_FORMULA;
    q->invalid = 1;
    if (formula == NULL) {
        return -1;
    }
    tiku_query_init(&parsed);
    parsed.mode = TIKU_QMODE_FORMULA;
    while (*p != '\0') {
        const char *field, *value;
        size_t fn, vn;
        tiku_qfield_t f;
        tiku_qop_t op;
        int quoted = 0, at;

        while (*p == ' ' || *p == '\t') { p++; }
        if (*p++ != '(') { return -1; }
        field = p;
        while ((*p >= 'a' && *p <= 'z') || *p == '_') { p++; }
        fn = (size_t)(p - field);
        if (!formula_field(field, fn, &f)) { return -1; }
        if (strncmp(p, "==", 2) == 0) {
            op = TIKU_OP_IS; p += 2;
        } else if (strncmp(p, "!=", 2) == 0) {
            op = TIKU_OP_IS_NOT; p += 2;
        } else if (*p == '>') {
            op = TIKU_OP_GREATER; p++;
        } else if (*p == '<') {
            op = TIKU_OP_LESS; p++;
        } else {
            return -1;
        }
        if (*p == '"') {
            quoted = 1;
            value = ++p;
            while (*p != '\0' && *p != '"') { p++; }
            if (*p != '"') { return -1; }
            vn = (size_t)(p - value);
            p++;
        } else {
            value = p;
            while (*p != '\0' && *p != ')') { p++; }
            vn = (size_t)(p - value);
        }
        if (*p++ != ')' || vn >= TIKU_QUERY_VALUE_MAX) { return -1; }
        {
            char text[TIKU_QUERY_VALUE_MAX];

            memcpy(text, value, vn);
            text[vn] = '\0';
            at = tiku_query_add_term(&parsed, f, op, text, join);
        }
        if (at < 0) { return -1; }
        parsed.term[at].pattern_ready = quoted;
        while (*p == ' ' || *p == '\t') { p++; }
        if (*p == '\0') { break; }
        if (strncmp(p, "&&", 2) == 0) {
            join = TIKU_JOIN_AND;
        } else if (strncmp(p, "||", 2) == 0) {
            join = TIKU_JOIN_OR;
        } else {
            return -1;
        }
        p += 2;
    }
    if (parsed.term_count == 0) { return -1; }
    parsed.invalid = 0;
    *q = parsed;
    return 0;
}

int
tiku_query_add_term(tiku_query_t *q, tiku_qfield_t field,
                        tiku_qop_t op, const char *value,
                        tiku_qjoin_t join)
{
    tiku_qterm_t *t;

    if (q == NULL || q->term_count >= TIKU_QUERY_TERMS_MAX) {
        return -1;
    }
    t = &q->term[q->term_count];
    memset(t, 0, sizeof *t);
    t->field = field;
    t->op = op;
    t->join = join;
    snprintf(t->value, sizeof t->value, "%s", (value != NULL) ? value : "");
    if (field == TIKU_QF_MODIFIED && relative_date(t->value)) {
        q->dynamic_date = 1;
    }
    return q->term_count++;
}

int
tiku_query_add_scope(tiku_query_t *q, const char *path)
{
    char norm[TIKU_PATH_MAX];
    int i;
    size_t n;

    if (q == NULL || path == NULL || path[0] == '\0' ||
        q->scope_count >= TIKU_QUERY_SCOPES_MAX) {
        return 0;
    }
    /* The path gets a trailing separator before the duplicate test: the
     * stored scopes carry one, and the prefix test needs it so /dev does
     * not admit /devices. */
    snprintf(norm, sizeof norm, "%s", path);
    n = strlen(norm);
    if (n > 0u && norm[n - 1u] != '/' && n + 1u < sizeof norm) {
        norm[n] = '/';
        norm[n + 1u] = '\0';
    }
    for (i = 0; i < q->scope_count; i++) {
        if (strcmp(q->scope[i], norm) == 0) {
            return 0;
        }
    }
    memcpy(q->scope[q->scope_count], norm, strlen(norm) + 1u);
    q->scope_count++;
    return 1;
}

void
tiku_query_clear_scope(tiku_query_t *q)
{
    if (q != NULL) {
        q->scope_count = 0;
        memset(q->scope_was, 0, sizeof q->scope_was);
    }
}

/** @brief Turn the normalised stored path back into a stat-able path. */
static void
scope_path(const char *stored, char *out, size_t max)
{
    size_t n;

    snprintf(out, max, "%s", stored);
    n = strlen(out);
    if (n > 1u && out[n - 1u] == '/') {
        out[n - 1u] = '\0';
    }
}

void
tiku_query_bind_scopes(tiku_query_t *q,
                           tiku_backend_t *backend)
{
    int i;

    if (q == NULL || backend == NULL || backend->ops->stat == NULL) {
        return;
    }
    for (i = 0; i < q->scope_count; i++) {
        tiku_model_t m;
        char path[TIKU_PATH_MAX];

        if (q->scope_was[i].dev != 0u) {
            continue;           /* keep the identity the record named */
        }
        scope_path(q->scope[i], path, sizeof path);
        if (backend->ops->stat(backend, path, &m) != 0 ||
            !tiku_model_is_container(&m)) {
            continue;
        }
        snprintf(q->scope_was[i].path, sizeof q->scope_was[i].path,
                 "%s", q->scope[i]);
        q->scope_was[i].dev = m.facts.dev;
        snprintf(q->scope_was[i].volume, sizeof q->scope_was[i].volume,
                 "%s", backend->devid[0] != '\0' ? backend->devid
                                                   : m.name);
    }
}

/** @brief Whether a saved scope still names the container it was bound to. */
static int
scope_available(const tiku_query_t *q, int at,
                tiku_backend_t *backend)
{
    tiku_model_t m;
    char path[TIKU_PATH_MAX];

    if (backend->ops->stat == NULL) {
        return 1;               /* this backend cannot revalidate */
    }
    scope_path(q->scope[at], path, sizeof path);
    if (backend->ops->stat(backend, path, &m) != 0) {
        return 0;
    }
    return q->scope_was[at].dev == 0u || m.facts.dev == 0u ||
           q->scope_was[at].dev == m.facts.dev;
}

int
tiku_query_in_scope(const tiku_query_t *q, const char *path)
{
    int i;

    if (q == NULL || q->scope_count == 0) {
        return 1;                     /* no filter: the whole namespace   */
    }
    for (i = 0; i < q->scope_count; i++) {
        size_t n = strlen(q->scope[i]);
        size_t prefix_n = n;

        /* Scopes are stored with a trailing separator so child prefixes are
         * unambiguous; a file scope, which has no children, admits the node
         * itself (PVN-070). */
        if (prefix_n > 1u && q->scope[i][prefix_n - 1u] == '/') {
            prefix_n--;
        }

        if ((strncmp(q->scope[i], path, n) == 0) ||
            (strncmp(q->scope[i], path, prefix_n) == 0 &&
             path[prefix_n] == '\0')) {
            return 1;
        }
    }
    return 0;
}

int
tiku_query_allows_path(const tiku_query_t *q,
                           const tiku_model_t *m)
{
    size_t n;

    if (q == NULL || m == NULL || q->include_trash) {
        return 1;
    }
    if (m->kind == TIKU_KIND_TRASH) {
        return 0;
    }
    if (q->trash_path[0] == '\0') {
        return 1;
    }
    n = strlen(q->trash_path);
    return !(strncmp(m->path, q->trash_path, n) == 0 &&
             (m->path[n] == '\0' || m->path[n] == '/'));
}

/*---------------------------------------------------------------------------*/
/* matching                                                                  */
/*---------------------------------------------------------------------------*/

/** @brief Scalar syntax used by typed attributes, including K/M/G. */
static int
parse_scalar(const char *text, long double *out)
{
    char *end;
    long double value, scale = 1.0L;

    if (text == NULL || text[0] == '\0' || out == NULL) {
        return 0;
    }
    value = strtold(text, &end);
    if (end == text) {
        return 0;
    }
    if (*end == 'K' || *end == 'k') {
        scale = 1024.0L; end++;
    } else if (*end == 'M' || *end == 'm') {
        scale = 1024.0L * 1024.0L; end++;
    } else if (*end == 'G' || *end == 'g') {
        scale = 1024.0L * 1024.0L * 1024.0L; end++;
    }
    while (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n') {
        end++;
    }
    if (*end != '\0') {
        return 0;
    }
    *out = value * scale;
    return 1;
}

/** @brief A permissive boolean: "true", "on", "yes" or an odd number is 1,
 *         anything else 0. */
static int
parse_bool(const char *text)
{
    char *end;
    long value;

    if (text == NULL || text[0] == '\0' || strcasecmp(text, "false") == 0 ||
        strcasecmp(text, "off") == 0 || strcasecmp(text, "no") == 0) {
        return 0;
    }
    if (strcasecmp(text, "true") == 0 || strcasecmp(text, "on") == 0 ||
        strcasecmp(text, "yes") == 0) {
        return 1;
    }
    value = strtol(text, &end, 0);
    return (end != text && (value % 2) != 0) ? 1 : 0;
}

/** @brief Local midnight @p days calendar days from the day of @p now. */
static int64_t
day_start(int64_t now, int days)
{
    time_t source = (time_t)now;
    struct tm tmv;
    time_t answer;

    if (localtime_r(&source, &tmv) == NULL) {
        return now;
    }
    tmv.tm_hour = tmv.tm_min = tmv.tm_sec = 0;
    tmv.tm_mday += days;
    tmv.tm_isdst = -1;
    answer = mktime(&tmv);
    return (answer == (time_t)-1) ? now : (int64_t)answer;
}

/** @brief Parse the absolute and relative spellings accepted by Modified. */
static int
parse_date(const char *text, int64_t now, int64_t *out)
{
    long long amount;
    char unit[16], direction[16], tail[16];
    int year, month, day, used = 0, n;
    int64_t step;

    if (text == NULL || out == NULL) {
        return 0;
    }
    if (strcasecmp(text, "now") == 0) {
        *out = now; return 1;
    }
    if (strcasecmp(text, "today") == 0) {
        *out = day_start(now, 0); return 1;
    }
    if (strcasecmp(text, "yesterday") == 0) {
        *out = day_start(now, -1); return 1;
    }
    if (strcasecmp(text, "tomorrow") == 0) {
        *out = day_start(now, 1); return 1;
    }
    n = sscanf(text, "%lld %15s %15s %15s", &amount, unit, direction,
               tail);
    step = (n >= 3) ? relative_unit(unit) : 0;
    if (n == 3 && step != 0 && strcasecmp(direction, "ago") == 0) {
        *out = now - (int64_t)amount * step; return 1;
    }
    if (n == 4 && step != 0 && strcasecmp(direction, "from") == 0 &&
        strcasecmp(tail, "now") == 0) {
        *out = now + (int64_t)amount * step; return 1;
    }
    if (sscanf(text, "%d-%d-%d%n", &year, &month, &day, &used) == 3 &&
        text[used] == '\0') {
        struct tm tmv;
        time_t answer;

        memset(&tmv, 0, sizeof tmv);
        tmv.tm_year = year - 1900;
        tmv.tm_mon = month - 1;
        tmv.tm_mday = day;
        tmv.tm_isdst = -1;
        answer = mktime(&tmv);
        if (answer != (time_t)-1) {
            *out = (int64_t)answer; return 1;
        }
    }
    return 0;
}

/** @brief Whether @p c is an ASCII letter. */
static int
is_alpha(int c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

/** @brief @p c in ASCII lower case. */
static int
lower(int c)
{
    return (c >= 'A' && c <= 'Z') ? c + 32 : c;
}

int
tiku_query_pattern(const char *value, tiku_qop_t op, char *out,
                       size_t max)
{
    size_t o = 0;
    const char *p = value;

    if (out == NULL || max == 0u) {
        return 0;
    }
    /* Substring and suffix searches take a leading star, substring and
     * prefix searches a trailing one. */
    if ((op == TIKU_OP_CONTAINS || op == TIKU_OP_ENDS_WITH) &&
        o + 1u < max) {
        out[o++] = '*';
    }
    for (; p != NULL && *p != '\0'; p++) {
        if (*p == ' ') {
            /* A space matches any run: "foo bar" also finds "foo-bar" and
             * "foo.bar". */
            if (o + 1u < max) { out[o++] = '*'; }
            continue;
        }
        if (is_alpha((unsigned char)*p)) {
            /* Each letter becomes a class of its two cases. */
            if (o + 4u < max) {
                out[o++] = '[';
                out[o++] = (char)lower((unsigned char)*p);
                out[o++] = (char)(lower((unsigned char)*p) - 32);
                out[o++] = ']';
            }
            continue;
        }
        if (o + 1u < max) { out[o++] = *p; }
    }
    if ((op == TIKU_OP_CONTAINS || op == TIKU_OP_BEGINS_WITH) &&
        o + 1u < max) {
        out[o++] = '*';
    }
    out[o] = '\0';
    return (int)o;
}

int
tiku_query_glob(const char *pattern, const char *name)
{
    const char *star = NULL, *retry = NULL;

    if (pattern == NULL || name == NULL) {
        return 0;
    }
    while (*name != '\0') {
        if (*pattern == '[' && strchr(pattern, ']') != NULL) {
            /* Only a '[' with a ']' after it opens a class; any other is a
             * literal bracket in a name, so "[abc" does not match "a". */
            const char *cl = pattern + 1;
            int neg = 0, hit = 0;

            if (*cl == '!' || *cl == '^') { neg = 1; cl++; }
            for (; *cl != '\0' && *cl != ']'; cl++) {
                if (cl[1] == '-' && cl[2] != '\0' && cl[2] != ']') {
                    if ((unsigned char)*name >= (unsigned char)cl[0] &&
                        (unsigned char)*name <= (unsigned char)cl[2]) {
                        hit = 1;
                    }
                    cl += 2;
                } else if (*cl == *name) {
                    hit = 1;
                }
            }
            if (hit != neg) {
                pattern = (*cl == ']') ? cl + 1 : cl;
                name++;
                continue;
            }
        } else if (*pattern == '?' || *pattern == *name) {
            pattern++;
            name++;
            continue;
        } else if (*pattern == '*') {
            star = pattern++;
            retry = name;
            continue;
        }
        if (star != NULL) {
            /* Backtrack: the star swallows one more character. */
            pattern = star + 1;
            name = ++retry;
            continue;
        }
        return 0;
    }
    while (*pattern == '*') {
        pattern++;
    }
    return (*pattern == '\0');
}

/** @brief The text a term tests against. */
static const char *
field_text(tiku_qfield_t f, const tiku_model_t *m,
           const tiku_desc_t *d, const char *value, char *buf,
           size_t max)
{
    switch (f) {
    case TIKU_QF_PATH:  return m->path;
    case TIKU_QF_KIND:  return tiku_model_kind_string(m);
    case TIKU_QF_VALUE: return (value != NULL) ? value : "";
    case TIKU_QF_CAP:   return m->facts.req_cap;
    case TIKU_QF_UNIT:  return (d != NULL) ? d->unit_name : "";
    case TIKU_QF_SIZE:
        snprintf(buf, max, "%lld", (long long)m->facts.size);
        return buf;
    case TIKU_QF_MODIFIED:
        snprintf(buf, max, "%lld", (long long)m->facts.mtime);
        return buf;
    case TIKU_QF_WRITABLE:
        return tiku_model_is_writable(m) ? "yes" : "no";
    case TIKU_QF_TYPE:  return m->type;
    default:
        return m->name;
    }
}

/** @brief Whether a field is compared as a number: Size, and a Value whose
 *         descriptor gives a numeric type. */
static int
numeric_field(tiku_qfield_t f, const tiku_desc_t *d)
{
    if (f == TIKU_QF_SIZE) {
        return 1;
    }
    return (f == TIKU_QF_VALUE && d != NULL &&
            d->vtype != TIKU_VT_NONE && d->vtype != TIKU_VT_STR);
}

/** @brief Whether one term holds for @p m, its descriptor @p d and its
 *         value; @p now anchors relative dates. */
static int
term_matches(const tiku_qterm_t *t, const tiku_model_t *m,
             const tiku_desc_t *d, const char *value, int64_t now)
{
    char buf[64], pat[TIKU_QUERY_VALUE_MAX * 4 + 8];
    const char *text = field_text(t->field, m, d, value, buf, sizeof buf);

    if (!t->pattern_ready && t->field == TIKU_QF_TYPE &&
        (t->op == TIKU_OP_IS || t->op == TIKU_OP_IS_NOT)) {
        /* IS and IS_NOT match a type with tiku_model_type_is(), so "text/"
         * admits "text/x-source-code"; the other operators match the type
         * as text, so "contains vnd" works. */
        int yes = tiku_model_type_is(text, t->value);

        return (t->op == TIKU_OP_IS) ? yes : !yes;
    }
    if (!t->pattern_ready &&
        (t->field == TIKU_QF_WRITABLE ||
         (t->field == TIKU_QF_VALUE && d != NULL &&
          d->vtype == TIKU_VT_BOOL)) &&
        (t->op == TIKU_OP_IS || t->op == TIKU_OP_IS_NOT)) {
        int yes = parse_bool(text) == parse_bool(t->value);

        return (t->op == TIKU_OP_IS) ? yes : !yes;
    }
    if (t->field == TIKU_QF_MODIFIED &&
        (t->op == TIKU_OP_GREATER || t->op == TIKU_OP_LESS)) {
        int64_t boundary;

        if (!parse_date(t->value, now, &boundary)) {
            return 0;
        }
        return (t->op == TIKU_OP_GREATER)
                   ? m->facts.mtime > boundary : m->facts.mtime < boundary;
    }
    if (!t->pattern_ready && numeric_field(t->field, d) &&
        (t->op == TIKU_OP_IS || t->op == TIKU_OP_IS_NOT)) {
        long double a, b;
        int yes = parse_scalar(text, &a) && parse_scalar(t->value, &b) &&
                  a == b;

        return (t->op == TIKU_OP_IS) ? yes : !yes;
    }
    if (t->op == TIKU_OP_GREATER || t->op == TIKU_OP_LESS) {
        long double a, b;
        int text_num = parse_scalar(text, &a);
        int want_num = parse_scalar(t->value, &b);

        /* Two numbers compare as numbers, so "30" is less than "1000". */
        if (want_num && text_num) {
            return (t->op == TIKU_OP_GREATER) ? (a > b) : (a < b);
        }
        /* A number and a name do not compare: neither is greater nor
         * less. */
        if (want_num != text_num) {
            return 0;
        }
        if (numeric_field(t->field, d)) {
            return 0;
        }
        return (t->op == TIKU_OP_GREATER)
                   ? (strcmp(text, t->value) > 0)
                   : (strcmp(text, t->value) < 0);
    }
    if (t->pattern_ready) {
        snprintf(pat, sizeof pat, "%s", t->value);
    } else {
        (void)tiku_query_pattern(t->value, t->op, pat, sizeof pat);
    }
    {
        int hit = tiku_query_glob(pat, (text != NULL) ? text : "");

        return (t->op == TIKU_OP_IS_NOT) ? !hit : hit;
    }
}

int
tiku_query_matches_at(const tiku_query_t *q,
                          const tiku_model_t *m,
                          const tiku_desc_t *d, const char *value,
                          int64_t now)
{
    int i, acc = 0;

    if (q == NULL || m == NULL || q->term_count == 0) {
        return 1;
    }
    /* Terms fold left: each joins the result so far by its own join, as
     * the panel's and/or control sits on the row above the one it
     * governs. */
    acc = term_matches(&q->term[0], m, d, value, now);
    for (i = 1; i < q->term_count; i++) {
        int r = term_matches(&q->term[i], m, d, value, now);

        acc = (q->term[i].join == TIKU_JOIN_OR) ? (acc || r) : (acc && r);
    }
    return acc;
}

int
tiku_query_matches(const tiku_query_t *q, const tiku_model_t *m,
                       const tiku_desc_t *d, const char *value)
{
    return tiku_query_matches_at(q, m, d, value,
                                     (int64_t)time(NULL));
}

/** @brief Whether @p text contains @p needle, case ignored. */
static int
contains_ci(const char *text, const char *needle)
{
    size_t n = strlen(needle);

    for (; text != NULL && *text != '\0'; text++) {
        if (strncasecmp(text, needle, n) == 0) {
            return 1;
        }
    }
    return 0;
}

int64_t
tiku_query_next_refresh(const tiku_query_t *q, int64_t now)
{
    time_t source = (time_t)now;
    struct tm tmv;
    int i, grain = 86400;
    time_t answer;

    if (q == NULL || !q->dynamic_date || localtime_r(&source, &tmv) == NULL) {
        return 0;
    }
    for (i = 0; i < q->term_count; i++) {
        const char *value = q->term[i].value;

        if (q->term[i].field != TIKU_QF_MODIFIED) {
            continue;
        }
        if (contains_ci(value, "second") || contains_ci(value, "minute") ||
            strcasecmp(value, "now") == 0) {
            grain = 60;
            break;
        }
        if (contains_ci(value, "hour")) {
            grain = 3600;
        }
    }
    tmv.tm_isdst = -1;
    tmv.tm_sec = 0;
    if (grain == 60) {
        tmv.tm_min++;
    } else if (grain == 3600) {
        tmv.tm_min = 0;
        tmv.tm_hour++;
    } else {
        tmv.tm_min = 0;
        tmv.tm_hour = 0;
        tmv.tm_mday++;
    }
    answer = mktime(&tmv);
    return (answer == (time_t)-1) ? now + grain : (int64_t)answer;
}

/*---------------------------------------------------------------------------*/
/* rendering                                                                 */
/*---------------------------------------------------------------------------*/

/** @brief The formula's name for field @p f. */
static const char *
field_name(tiku_qfield_t f)
{
    switch (f) {
    case TIKU_QF_PATH:     return "path";
    case TIKU_QF_KIND:     return "kind";
    case TIKU_QF_VALUE:    return "value";
    case TIKU_QF_SIZE:     return "size";
    case TIKU_QF_CAP:      return "cap";
    case TIKU_QF_UNIT:     return "unit";
    case TIKU_QF_WRITABLE: return "writable";
    case TIKU_QF_TYPE:     return "type";
    case TIKU_QF_MODIFIED: return "modified";
    default:                   return "name";
    }
}

int
tiku_query_predicate(const tiku_query_t *q, char *out, size_t max)
{
    int i;
    size_t o = 0;

    if (out == NULL || max == 0u) {
        return 0;
    }
    out[0] = '\0';
    for (i = 0; q != NULL && i < q->term_count; i++) {
        const tiku_qterm_t *t = &q->term[i];
        char pat[TIKU_QUERY_VALUE_MAX * 4 + 8];
        const char *op = "==";
        char frag[TIKU_QUERY_VALUE_MAX * 4 + 64];

        if (i > 0) {
            o += (size_t)snprintf(out + o, (o < max) ? max - o : 0u, "%s",
                                  (t->join == TIKU_JOIN_OR) ? "||" : "&&");
        }
        switch (t->op) {
        case TIKU_OP_IS_NOT:  op = "!="; break;
        case TIKU_OP_GREATER: op = ">";  break;
        case TIKU_OP_LESS:    op = "<";  break;
        default:                  op = "=="; break;
        }
        if (t->op == TIKU_OP_GREATER || t->op == TIKU_OP_LESS) {
            snprintf(frag, sizeof frag, "(%s%s%s)", field_name(t->field), op,
                     t->value);
        } else {
            /* The pattern is what is compared, so it is what the formula
             * shows. */
            if (t->pattern_ready) {
                snprintf(pat, sizeof pat, "%s", t->value);
            } else {
                (void)tiku_query_pattern(t->value, t->op, pat,
                                              sizeof pat);
            }
            snprintf(frag, sizeof frag, "(%s%s\"%s\")", field_name(t->field),
                     op, pat);
        }
        o += (size_t)snprintf(out + o, (o < max) ? max - o : 0u, "%s", frag);
        if (o >= max) {
            break;
        }
    }
    return (int)strlen(out);
}

/*---------------------------------------------------------------------------*/
/* running                                                                   */
/*---------------------------------------------------------------------------*/

/** @brief A search in progress: the walk's stack of folders, the results
 *         and any refresh under way. */
struct tiku_qrun {
    tiku_backend_t *backend;
    tiku_query_t    q;
    tiku_query_t    requested;      /* identities before validation   */
    char                queue[QUEUE_MAX][TIKU_PATH_MAX];
    int                 head, tail;
    tiku_model_t    single[TIKU_QUERY_SCOPES_MAX];
    int                 single_count, single_at;
    tiku_model_t   *results;
    int                 count;
    int                 visited;
    int                 done;
    /* Live: the snapshot taken at the start of a refresh, and how many of
     * the current results are new since it. */
    unsigned char      *fresh;
    int                 refreshes;
    /* A refresh in progress: the snapshot lives here while the re-walk is
     * stepped. */
    tiku_model_t       *old;
    unsigned char      *seen;
    int                 old_count;
    int                 refreshing;
};

/** @brief Push @p path for the walk; when the stack is full the subtree
 *         is left out. */
static void
enqueue(tiku_qrun_t *r, const char *path)
{
    int next = (r->tail + 1) % QUEUE_MAX;

    if (next == r->head) {
        return;                /* full: this subtree is left out          */
    }
    snprintf(r->queue[r->tail], TIKU_PATH_MAX, "%s", path);
    r->tail = next;
}

/** @brief Receives one entry during the walk: push it when it is a folder,
 *         keep it when it is in scope and matches. */
static int
visit(const tiku_model_t *m, void *ctx)
{
    tiku_qrun_t *r = ctx;
    tiku_desc_t d;
    char value[128];

    /* The Trash test comes before the push, so the Trash's children are
     * never walked. */
    if (!tiku_query_allows_path(&r->q, m)) {
        return 0;
    }
    if (tiku_model_is_container(m) &&
        m->kind != TIKU_KIND_SYMLINK) {
        /* Every directory is descended, whether or not it matches.  A
         * symlink is not: following links would let /proc's cycles keep
         * the walk from ending. */
        enqueue(r, m->path);
    }
    if (r->count >= r->q.max_results) {
        return 1;
    }
    (void)tiku_desc_parse(m->facts.meta, &d);
    value[0] = '\0';
    /* The value is read, a round trip on a device, only when a term tests
     * it. */
    {
        int i, wants = 0;

        for (i = 0; i < r->q.term_count; i++) {
            if (r->q.term[i].field == TIKU_QF_VALUE) {
                wants = 1;
            }
        }
        if (wants && !tiku_model_is_container(m) &&
            r->backend->ops->read != NULL) {
            int n = r->backend->ops->read(r->backend, m->path, value,
                                          sizeof value - 1u);
            if (n > 0) {
                value[n] = '\0';
                while (n > 0 && (value[n - 1] == '\n' ||
                                 value[n - 1] == '\r')) {
                    value[--n] = '\0';
                }
            }
        }
    }
    if (tiku_query_in_scope(&r->q, m->path) &&
        tiku_query_matches(&r->q, m, &d, value)) {
        r->results[r->count++] = *m;
    }
    return 0;
}

tiku_qrun_t *
tiku_query_start(tiku_backend_t *b, const tiku_query_t *q)
{
    tiku_qrun_t *r;
    int i, requested_scopes;

    if (b == NULL || q == NULL || q->invalid) {
        return NULL;
    }
    r = calloc(1, sizeof *r);
    if (r == NULL) {
        return NULL;
    }
    r->results = calloc(RESULTS_MAX, sizeof *r->results);
    if (r->results == NULL) {
        free(r);
        return NULL;
    }
    r->backend = b;
    r->q = *q;
    r->requested = *q;
    requested_scopes = r->q.scope_count;
    if (requested_scopes > 0) {
        int kept = 0;

        for (i = 0; i < requested_scopes; i++) {
            if (!scope_available(&r->q, i, b)) {
                continue;
            }
            if (kept != i) {
                memcpy(r->q.scope[kept], r->q.scope[i],
                       sizeof r->q.scope[kept]);
                r->q.scope_was[kept] = r->q.scope_was[i];
            }
            kept++;
        }
        r->q.scope_count = kept;
    }
    if (r->q.max_results <= 0 || r->q.max_results > RESULTS_MAX) {
        r->q.max_results = RESULTS_MAX;
    }
    /* A scoped search starts its walk at the scopes. */
    if (r->q.scope_count > 0) {
        for (i = 0; i < r->q.scope_count; i++) {
            char start[TIKU_PATH_MAX];
            size_t n;
            tiku_model_t m;

            snprintf(start, sizeof start, "%s", r->q.scope[i]);
            n = strlen(start);
            if (n > 1u && start[n - 1u] == '/') {
                start[n - 1u] = '\0';
            }
            /* A scope may name a file: it is kept as a single visit,
             * because list() on a file fails (PVN-070). */
            if (b->ops->stat != NULL && b->ops->stat(b, start, &m) == 0 &&
                !tiku_model_is_container(&m)) {
                if (r->single_count < TIKU_QUERY_SCOPES_MAX) {
                    r->single[r->single_count++] = m;
                }
            } else {
                enqueue(r, start);
            }
        }
    } else if (requested_scopes == 0) {
        enqueue(r, "/");
    } else {
        /* No saved scope is available, so the run ends with no
         * results. */
        r->done = 1;
    }
    return r;
}

void
tiku_query_stop(tiku_qrun_t *r)
{
    if (r != NULL) {
        free(r->old);
        free(r->seen);
        free(r->fresh);
        free(r->results);
        free(r);
    }
}

int
tiku_query_step(tiku_qrun_t *r, int budget)
{
    int before;

    if (r == NULL || r->done) {
        return -1;
    }
    before = r->count;
    while (budget-- > 0 && r->single_at < r->single_count) {
        (void)visit(&r->single[r->single_at++], r);
        if (r->count >= r->q.max_results) {
            break;
        }
    }
    while (budget-- > 0 && r->head != r->tail) {
        char dir[TIKU_PATH_MAX];

        /* The most recently found directory first: a stack, so the walk
         * descends before it moves on, depth first (PVN-069).  The walk
         * takes no snapshot: a folder already passed misses later
         * creations. */
        r->tail = (r->tail + QUEUE_MAX - 1) % QUEUE_MAX;
        snprintf(dir, sizeof dir, "%s", r->queue[r->tail]);
        r->visited++;
        (void)r->backend->ops->list(r->backend, dir, visit, r);
        if (r->count >= r->q.max_results) {
            break;
        }
    }
    if ((r->head == r->tail && r->single_at >= r->single_count) ||
        r->count >= r->q.max_results) {
        r->done = 1;
    }
    return r->count - before;
}

int
tiku_query_count(const tiku_qrun_t *r)
{
    return (r != NULL) ? r->count : 0;
}

const tiku_model_t *
tiku_query_at(const tiku_qrun_t *r, int i)
{
    if (r == NULL || i < 0 || i >= r->count) {
        return NULL;
    }
    return &r->results[i];
}

int
tiku_query_is_new(const tiku_qrun_t *r, int i)
{
    if (r == NULL || r->fresh == NULL || i < 0 || i >= r->count) {
        return 0;
    }
    return r->fresh[i];
}

int
tiku_query_refresh_begin(tiku_qrun_t *r)
{
    int i;

    if (r == NULL || r->refreshing) {
        return -1;
    }
    /* The current results are copied to the snapshot and the walk starts
     * again from the scopes; tiku_query_refresh_end() compares the two. */
    r->old = calloc((size_t)(r->count > 0 ? r->count : 1), sizeof *r->old);
    r->seen = calloc((size_t)(r->count > 0 ? r->count : 1), 1);
    if (r->old == NULL || r->seen == NULL) {
        free(r->old);
        free(r->seen);
        r->old = NULL;
        r->seen = NULL;
        return -1;
    }
    r->old_count = r->count;
    for (i = 0; i < r->old_count; i++) {
        r->old[i] = r->results[i];
    }
    r->count = 0;
    r->visited = 0;
    r->head = r->tail = 0;
    r->single_count = r->single_at = 0;
    r->done = 0;
    if (r->q.scope_count > 0) {
        for (i = 0; i < r->q.scope_count; i++) {
            char start[TIKU_PATH_MAX];
            size_t n;
            tiku_model_t m;

            snprintf(start, sizeof start, "%s", r->q.scope[i]);
            n = strlen(start);
            if (n > 1u && start[n - 1u] == '/') {
                start[n - 1u] = '\0';
            }
            if (r->backend->ops->stat != NULL &&
                r->backend->ops->stat(r->backend, start, &m) == 0 &&
                !tiku_model_is_container(&m)) {
                if (r->single_count < TIKU_QUERY_SCOPES_MAX) {
                    r->single[r->single_count++] = m;
                }
            } else {
                enqueue(r, start);
            }
        }
    } else {
        enqueue(r, "/");
    }
    r->refreshing = 1;
    return 0;
}

int
tiku_query_refreshing(const tiku_qrun_t *r)
{
    return (r != NULL) ? r->refreshing : 0;
}

int
tiku_query_refresh_end(tiku_qrun_t *r, tiku_qdiff_t *out)
{
    int i, j;

    if (r == NULL || !r->refreshing) {
        return -1;
    }
    if (out != NULL) {
        memset(out, 0, sizeof *out);
    }
    if (r->fresh != NULL) {
        free(r->fresh);
    }
    r->fresh = calloc((size_t)(r->count > 0 ? r->count : 1), 1);

    /* Each result found again marks its snapshot entry seen; whatever is
     * still unseen at the end has left the set. */
    for (i = 0; i < r->count; i++) {
        int survived = 0;

        for (j = 0; j < r->old_count; j++) {
            if (!r->seen[j] &&
                strcmp(r->old[j].path, r->results[i].path) == 0) {
                r->seen[j] = 1;
                survived = 1;
                break;
            }
        }
        if (!survived) {
            if (r->fresh != NULL) {
                r->fresh[i] = 1;
            }
            if (out != NULL) {
                out->added++;
            }
        }
    }
    for (j = 0; j < r->old_count; j++) {
        tiku_model_t probe;

        if (r->seen[j]) {
            continue;
        }
        if (out != NULL) {
            out->removed++;
            /* A result that left has vanished when its node no longer
             * stats. */
            if (r->backend->ops->stat == NULL ||
                r->backend->ops->stat(r->backend, r->old[j].path,
                                      &probe) != 0) {
                out->vanished++;
            }
        }
    }
    r->refreshes++;
    free(r->old);
    free(r->seen);
    r->old = NULL;
    r->seen = NULL;
    r->old_count = 0;
    r->refreshing = 0;
    return 0;
}

int
tiku_query_refresh(tiku_qrun_t *r, tiku_qdiff_t *out)
{
    if (tiku_query_refresh_begin(r) != 0) {
        return -1;
    }
    while (!r->done) {
        if (tiku_query_step(r, 16) < 0) {
            break;
        }
    }
    return tiku_query_refresh_end(r, out);
}

int
tiku_query_visited(const tiku_qrun_t *r)
{
    return (r != NULL) ? r->visited : 0;
}

int
tiku_query_done(const tiku_qrun_t *r)
{
    return (r != NULL) ? r->done : 1;
}

int
tiku_query_targets_device(const tiku_qrun_t *r, uint64_t dev)
{
    int i;

    if (r == NULL || dev == 0u) {
        return 0;
    }
    if (r->requested.scope_count == 0) {
        return 1;               /* the whole namespace includes every disk */
    }
    for (i = 0; i < r->requested.scope_count; i++) {
        if (r->requested.scope_was[i].dev == dev) {
            return 1;
        }
    }
    return 0;
}

const char *
tiku_query_search_type(const tiku_qrun_t *r)
{
    return (r != NULL) ? r->requested.type : "";
}

/*---------------------------------------------------------------------------*/
/* Saved queries                                                             */
/*---------------------------------------------------------------------------*/

/**
 * @brief Serialise a query to text, one line per field, so a record reads
 *        the same across builds.  Length, or -1 when @p max is too small.
 */
static int
serialise(const tiku_query_t *q, char *out, size_t max)
{
    size_t o = 0;
    int i;

    /* The record's versions: v2 carries what a record needs to be
     * re-opened and swept -- its kind, whether it is temporary, when it
     * was written and what its scopes were; v3 adds formula patterns and
     * Include Trash, v4 dynamic dates.  A v1 record still reads, the
     * fields it does not carry keeping their defaults. */
    o += (size_t)snprintf(out + o, (o < max) ? max - o : 0u,
                          "v4\nmode %d\nkind %d %d %lld\n", (int)q->mode,
                          (int)q->kind, q->temporary,
                          (long long)q->saved_at);
    /* Only the Include Trash choice is written, not this machine's Trash
     * path. */
    o += (size_t)snprintf(out + o, (o < max) ? max - o : 0u,
                          "o %d\n", q->include_trash);
    o += (size_t)snprintf(out + o, (o < max) ? max - o : 0u,
                          "r %d\n", q->dynamic_date);
    if (q->type[0] != '\0') {
        o += (size_t)snprintf(out + o, (o < max) ? max - o : 0u,
                              "y %s\n", q->type);
    }
    for (i = 0; i < q->term_count; i++) {
        o += (size_t)snprintf(out + o, (o < max) ? max - o : 0u,
                              "t %d %d %d %d %s\n", (int)q->term[i].field,
                              (int)q->term[i].op, (int)q->term[i].join,
                              q->term[i].pattern_ready,
                              q->term[i].value);
    }
    for (i = 0; i < q->scope_count; i++) {
        /* The identity goes on a "d" line of its own after the path, so a
         * reader that only understands "s <path>" still gets the path. */
        o += (size_t)snprintf(out + o, (o < max) ? max - o : 0u,
                              "s %s\n", q->scope[i]);
        if (q->scope_was[i].dev != 0u || q->scope_was[i].volume[0] != '\0') {
            o += (size_t)snprintf(out + o, (o < max) ? max - o : 0u,
                                  "d %llu %s\n",
                                  (unsigned long long)q->scope_was[i].dev,
                                  q->scope_was[i].volume);
        }
    }
    return (o < max) ? (int)o : -1;
}

/** @brief Read a saved record, v1 to v4, into @p q; unknown lines are
 *         skipped.  @return 0, or -1 for an unknown version. */
static int
deserialise(tiku_query_t *q, const char *in)
{
    const char *p = in;

    tiku_query_init(q);
    if (in == NULL ||
        (strncmp(in, "v1\n", 3) != 0 && strncmp(in, "v2\n", 3) != 0 &&
         strncmp(in, "v3\n", 3) != 0 && strncmp(in, "v4\n", 3) != 0)) {
        return -1;              /* an unknown version is not guessed at   */
    }
    p += 3;
    while (*p != '\0') {
        const char *nl = strchr(p, '\n');
        char line[TIKU_PATH_MAX + 64];
        size_t n = (nl != NULL) ? (size_t)(nl - p) : strlen(p);

        if (n >= sizeof line) {
            n = sizeof line - 1u;
        }
        memcpy(line, p, n);
        line[n] = '\0';
        if (strncmp(line, "mode ", 5) == 0) {
            q->mode = (tiku_qmode_t)atoi(line + 5);
        } else if (line[0] == 't' && line[1] == ' ') {
            int f = 0, op = 0, j = 0, ready = 0, used = 0;

            if (in[1] >= '3' && sscanf(line + 2, "%d %d %d %d %n", &f,
                                       &op, &j, &ready, &used) >= 4) {
                int at = tiku_query_add_term(q, (tiku_qfield_t)f,
                           (tiku_qop_t)op, line + 2 + used,
                           (tiku_qjoin_t)j);
                if (at >= 0) { q->term[at].pattern_ready = ready; }
            } else if (sscanf(line + 2, "%d %d %d %n", &f, &op, &j,
                              &used) >= 3) {
                (void)tiku_query_add_term(q, (tiku_qfield_t)f,
                    (tiku_qop_t)op, line + 2 + used,
                    (tiku_qjoin_t)j);
            }
        } else if (strncmp(line, "kind ", 5) == 0) {
            int k = 0, t = 0;
            long long at = 0;

            if (sscanf(line + 5, "%d %d %lld", &k, &t, &at) >= 2) {
                q->kind = (tiku_qrec_t)k;
                q->temporary = t;
                q->saved_at = (int64_t)at;
            }
        } else if (line[0] == 'y' && line[1] == ' ') {
            snprintf(q->type, sizeof q->type, "%.*s",
                     (int)(sizeof q->type - 1u), line + 2);
        } else if (line[0] == 'o' && line[1] == ' ') {
            int include = 0, used = 0;

            if (sscanf(line + 2, "%d %n", &include, &used) >= 1) {
                q->include_trash = include ? 1 : 0;
                snprintf(q->trash_path, sizeof q->trash_path, "%.*s",
                         (int)(sizeof q->trash_path - 1u), line + 2 + used);
            }
        } else if (line[0] == 'r' && line[1] == ' ') {
            q->dynamic_date = atoi(line + 2) ? 1 : 0;
        } else if (line[0] == 's' && line[1] == ' ') {
            (void)tiku_query_add_scope(q, line + 2);
        } else if (line[0] == 'd' && line[1] == ' ') {
            /* A "d" line belongs to the scope just read, which the writer
             * puts directly before it; one with no scope before it is
             * counted in dropped. */
            unsigned long long dev = 0ull;
            int used = 0;

            if (q->scope_count <= 0) {
                q->dropped++;   /* nothing for it to describe */
            } else if (sscanf(line + 2, "%llu %n", &dev, &used) >= 1) {
                tiku_qscope_t *sc = &q->scope_was[q->scope_count - 1];

                sc->dev = (uint64_t)dev;
                memcpy(sc->path, q->scope[q->scope_count - 1],
                       sizeof sc->path);
                snprintf(sc->volume, sizeof sc->volume, "%.*s",
                         (int)(sizeof sc->volume - 1u), line + 2 + used);
            }
        }
        if (nl == NULL) {
            break;
        }
        p = nl + 1;
    }
    return 0;
}

int
tiku_query_serialise(const tiku_query_t *q, char *buf, size_t max)
{
    if (q == NULL || buf == NULL) {
        return -1;
    }
    return serialise(q, buf, max);
}

/* 1 when one line of the newline-separated index is exactly name. */
static int
index_has(const char *index, const char *name)
{
    size_t want = strlen(name);
    const char *p;

    for (p = index; *p != '\0'; ) {
        const char *nl = strchr(p, '\n');
        size_t len = (nl != NULL) ? (size_t)(nl - p) : strlen(p);

        if (len == want && strncmp(p, name, len) == 0) {
            return 1;
        }
        if (nl == NULL) {
            break;
        }
        p = nl + 1;
    }
    return 0;
}

int
tiku_query_save(const tiku_query_t *q, const char *name,
                    struct tiku_store *store)
{
    char buf[TIKU_QUERY_PRED_MAX * 2];
    char index[2048];
    int n;

    if (q == NULL || name == NULL || store == NULL) {
        return -1;
    }
    if (serialise(q, buf, sizeof buf) < 0) {
        return -1;
    }
    if (tiku_state_write(store, name, TIKU_QUERY_ATTR, buf,
                             strlen(buf)) != 0) {
        return -1;
    }
    /* The index lists the saved queries for the menus. */
    n = tiku_state_read(store, TIKU_QUERY_STORE_NODE,
                            TIKU_QUERY_INDEX_ATTR, index,
                            sizeof index - 1u);
    if (n < 0) {
        n = 0;
    }
    index[n] = '\0';
    if (!index_has(index, name)) {
        /* Newest first. */
        char merged[2048];

        /* The name is cut to 127 bytes and the old index to 1900, so the
         * oldest entries fall off the end. */
        snprintf(merged, sizeof merged, "%.127s\n%.1900s", name, index);
        (void)tiku_state_write(store, TIKU_QUERY_STORE_NODE,
                                   TIKU_QUERY_INDEX_ATTR, merged,
                                   strlen(merged));
    }
    return 0;
}

int
tiku_query_load(tiku_query_t *q, const char *name,
                    struct tiku_store *store)
{
    char buf[TIKU_QUERY_PRED_MAX * 2];
    int n;

    if (q == NULL || name == NULL || store == NULL) {
        return -1;
    }
    n = tiku_state_read(store, name, TIKU_QUERY_ATTR, buf,
                            sizeof buf - 1u);
    if (n < 0) {
        return -1;
    }
    if (n == 0) {
        return -1;              /* emptied: the record has been reclaimed */
    }
    buf[n] = '\0';
    return deserialise(q, buf);
}

int
tiku_query_list(struct tiku_store *store, char names[][128], int max)
{
    char index[2048];
    const char *p;
    int n, count = 0;

    if (store == NULL || names == NULL) {
        return 0;
    }
    n = tiku_state_read(store, TIKU_QUERY_STORE_NODE,
                            TIKU_QUERY_INDEX_ATTR, index,
                            sizeof index - 1u);
    if (n < 0) {
        return 0;
    }
    index[n] = '\0';
    for (p = index; *p != '\0' && count < max; ) {
        const char *nl = strchr(p, '\n');
        size_t len = (nl != NULL) ? (size_t)(nl - p) : strlen(p);

        if (len > 0u) {
            if (len > 127u) {
                len = 127u;
            }
            memcpy(names[count], p, len);
            names[count][len] = '\0';
            count++;
        }
        if (nl == NULL) {
            break;
        }
        p = nl + 1;
    }
    return count;
}

int
tiku_query_list_kind(struct tiku_store *store, tiku_qrec_t kind,
                         char names[][128], int max)
{
    char all[TIKU_QUERY_LIST_MAX][128];
    int n, i, count = 0;

    if (store == NULL || names == NULL || max <= 0) {
        return 0;
    }
    n = tiku_query_list(store, all, TIKU_QUERY_LIST_MAX);
    for (i = 0; i < n && count < max; i++) {
        tiku_query_t q;

        if (tiku_query_load(&q, all[i], store) != 0) {
            /* Indexed but not readable: its record has gone, and it is of
             * no kind. */
            continue;
        }
        if (q.kind != kind) {
            continue;
        }
        snprintf(names[count], 128, "%s", all[i]);
        count++;
    }
    return count;
}

int
tiku_query_sweep(struct tiku_store *store, int64_t now,
                     int64_t up_for, const char *const *open, int nopen)
{
    char all[TIKU_QUERY_LIST_MAX][128];
    int n, i, k, went = 0;

    if (store == NULL) {
        return -1;
    }
    if (up_for < TIKU_QUERY_SWEEP_WAIT) {
        /* Not before TIKU_QUERY_SWEEP_WAIT after start. */
        return -1;
    }
    n = tiku_query_list(store, all, TIKU_QUERY_LIST_MAX);
    for (i = 0; i < n; i++) {
        tiku_query_t q;
        int shown = 0;

        if (tiku_query_load(&q, all[i], store) != 0) {
            continue;
        }
        if (!q.temporary || q.kind == TIKU_QREC_TEMPLATE) {
            continue;           /* named, or a template: kept */
        }
        if (q.saved_at == 0 || now - q.saved_at < TIKU_QUERY_STALE_SEC) {
            continue;           /* young enough to still be wanted */
        }
        for (k = 0; k < nopen; k++) {
            if (open != NULL && open[k] != NULL &&
                strcmp(open[k], all[i]) == 0) {
                shown = 1;
                break;
            }
        }
        if (shown) {
            /* A window shows it, and its record stays. */
            continue;
        }
        if (tiku_query_forget(all[i], store) == 0) {
            went++;
        }
    }
    return went;
}

int
tiku_query_forget(const char *name, struct tiku_store *store)
{
    char index[2048], merged[2048];
    const char *p;
    size_t o = 0;
    int n;

    if (name == NULL || store == NULL) {
        return -1;
    }
    n = tiku_state_read(store, TIKU_QUERY_STORE_NODE,
                            TIKU_QUERY_INDEX_ATTR, index,
                            sizeof index - 1u);
    if (n < 0) {
        return -1;
    }
    index[n] = '\0';
    merged[0] = '\0';
    for (p = index; *p != '\0'; ) {
        const char *nl = strchr(p, '\n');
        size_t len = (nl != NULL) ? (size_t)(nl - p) : strlen(p);

        if (len > 0u && (len != strlen(name) ||
                         strncmp(p, name, len) != 0)) {
            o += (size_t)snprintf(merged + o,
                                  (o < sizeof merged) ? sizeof merged - o : 0u,
                                  "%.*s\n", (int)len, p);
        }
        if (nl == NULL) {
            break;
        }
        p = nl + 1;
    }
    /* The record is emptied as well as its index line, so nothing is left
     * that the index cannot reach (Q-037). */
    (void)tiku_state_write(store, name, TIKU_QUERY_ATTR, "", 0);
    return tiku_state_write(store, TIKU_QUERY_STORE_NODE,
                                TIKU_QUERY_INDEX_ATTR, merged,
                                strlen(merged));
}
