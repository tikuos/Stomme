/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_match.c - starts / ends / contains / wildcard / regular expression.
 *
 * The wildcard matcher is TrackerString::StringMatchesPattern ported: a
 * pattern-level stack lets a failed tail back up to the last '*'.  Glyphs
 * are UTF-8: a '?' and a bracket class step one glyph, not one byte
 * (Q-049).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_trk_match.h"
#include <stdio.h>
#include <string.h>

#define LOWER(c) (((c) >= 'A' && (c) <= 'Z') ? (char)((c) + 32) : (c))

/**
 * @brief Bytes in the UTF-8 glyph beginning at @p c (Q-049).
 *
 * A '?' eats a glyph, not a byte, so one '?' matches an accented character.
 * A stray continuation byte counts as one.
 */
static int
glyph_len(const char *c)
{
    unsigned char b = (unsigned char)c[0];
    int n = (b >= 0xF0u) ? 4 : (b >= 0xE0u) ? 3 : (b >= 0xC0u) ? 2 : 1;
    int i;

    for (i = 1; i < n; i++) {
        if (((unsigned char)c[i] & 0xC0u) != 0x80u) {
            return 1;           /* a truncated sequence is one byte */
        }
    }
    return n;
}

/** @brief Compare two characters, folding case when asked. */
static int
chars_equal(char a, char b, int cs)
{
    return cs ? (a == b) : (LOWER(a) == LOWER(b));
}

/** @brief @p c lowered, unless matching is case-sensitive. */
static char
cond_lower(char c, int cs)
{
    return cs ? c : LOWER(c);
}

/*---------------------------------------------------------------------------*/
/* wildcard                                                                  */
/*---------------------------------------------------------------------------*/

/**
 * @brief Whether the string's first glyph matches a bracket expression,
 *        @p pattern pointing past the opening '['.
 *
 * Starting past it means a '[' inside is taken literally, which is what
 * makes "[[]" a way to match a bracket.
 */
static int
bracket_match(const char *string, const char *pattern, int cs)
{
    char testChar = cond_lower(string[0], cs);
    int match = 0;
    int inverse = (*pattern == '^' || *pattern == '!');

    if (inverse) {
        pattern++;
    }
    while (!match && *pattern != ']' && *pattern != '\0') {
        if (*pattern == '-') {
            char start = cond_lower(*(pattern - 1), cs);
            char stop  = cond_lower(*(pattern + 1), cs);
            int lo = (start >= 'a' && start <= 'z');
            int up = (start >= 'A' && start <= 'Z');
            int di = (start >= '0' && start <= '9');

            /* A range whose ends are not both lower case, both upper case
             * or both digits fails the match. */
            if ((lo && stop >= 'a' && stop <= 'z') ||
                (up && stop >= 'A' && stop <= 'Z') ||
                (di && stop >= '0' && stop <= '9')) {
                match = (start <= testChar && testChar <= stop);
            } else {
                return 0;
            }
        } else if (glyph_len(pattern) > 1) {
            /* A multi-byte entry matches the string's whole glyph. */
            int gp = glyph_len(pattern);

            match = (glyph_len(string) == gp &&
                     memcmp(string, pattern, (size_t)gp) == 0);
            if (!match) {
                pattern += gp - 1;      /* the ++ below takes the last byte */
            }
        } else {
            match = chars_equal(testChar, *pattern, cs);
        }
        if (!match) {
            pattern++;
        }
    }
    if (*pattern == '\0') {
        return 0;                       /* unmatched bracket is a failure */
    }
    return (match ^ inverse) != 0;
}

#define GLOB_LEVELS 100

/**
 * @brief Whether @p string matches the wildcard @p pattern: '?', '*' and
 * '[...]', case-sensitive when @p cs.  Each '*' keeps a place to back up
 * to, GLOB_LEVELS of them at most.
 */
static int
glob_match(const char *string, const char *pattern, int cs)
{
    const char *pStorage[GLOB_LEVELS];
    const char *sStorage[GLOB_LEVELS];
    int level = 0;

    if (string == NULL || pattern == NULL) {
        return 0;
    }
    while (*pattern != '\0') {
        switch (*pattern) {
        case '?':
            if (*string == '\0') {
                return 0;               /* '?' needs a character to eat   */
            }
            pattern++;
            string += glyph_len(string);
            break;

        case '*': {
            int match = 0;
            const char *before;

            /* Collapse ** and *?, so the level stack holds one entry per
             * run of wildcards. */
            while (*pattern == '*' || *pattern == '?') {
                pattern++;
                if (*pattern == '?' && *string != '\0') {
                    string += glyph_len(string);
                }
            }
            if (*pattern == '\0') {
                return 1;               /* a trailing * matches the rest  */
            }
            before = pattern - 1;
            if (*pattern == '[') {
                pattern++;
                while (!match && *string != '\0') {
                    match = bracket_match(string, pattern, cs);
                    string += glyph_len(string);
                }
                while (*pattern != ']' && *pattern != '\0') {
                    pattern++;
                }
                if (*pattern == '\0') {
                    return 0;
                }
            } else {
                while (!match && *string != '\0') {
                    match = chars_equal(*string++, *pattern, cs);
                }
            }
            if (!match) {
                return 0;
            }
            pStorage[level] = before;
            sStorage[level++] = string;
            if (level >= GLOB_LEVELS) {
                return 0;
            }
            pattern++;
            break;
        }

        case '[':
            pattern++;
            if (!bracket_match(string, pattern, cs)) {
                if (level > 0) {
                    pattern = pStorage[--level];
                    string = sStorage[level];
                } else {
                    return 0;
                }
            } else {
                while (*pattern != ']' && *pattern != '\0') {
                    pattern++;
                }
                if (*pattern == '\0') {
                    return 0;
                }
                string += glyph_len(string);
                pattern++;
            }
            break;

        default:
            if (chars_equal(*string, *pattern, cs)) {
                pattern++;
                string++;
            } else if (level > 0) {
                pattern = pStorage[--level];
                string = sStorage[level];
            } else {
                return 0;
            }
            break;
        }

        if (*pattern == '\0' && *string != '\0' && level > 0) {
            pattern = pStorage[--level];
            string = sStorage[level];
        }
    }
    return *string == '\0' && *pattern == '\0';
}

/*---------------------------------------------------------------------------*/
/* regular expression                                                        */
/*---------------------------------------------------------------------------*/

/* Limits of one compiled expression: nodes, groups, classes, and branches
 * per group. */
#define RX_NODES 192
#define RX_ALTS   32
#define RX_CLS    24
#define RX_BRANCH  8

/* What one node matches: a byte, any byte, a class, a group, '^' or '$'. */
enum { RX_CHAR = 0, RX_ANY, RX_CLASS, RX_GROUP, RX_BOL, RX_EOL };

/** @brief One atom of an expression, with its repeat count. */
typedef struct {
    int           type;
    unsigned char ch;
    int           cls;              /* index into cls[]                    */
    int           grp;              /* index into alt[]                    */
    int           min, max;         /* max < 0 is unbounded                */
    int           next;             /* next node in this sequence, or -1   */
} rx_node_t;

/** @brief One group's alternatives. */
typedef struct {
    int first[RX_BRANCH];           /* head node of each alternative       */
    int count;
} rx_alt_t;

/** @brief A compiled expression. */
typedef struct {
    rx_node_t     node[RX_NODES];
    int           nnode;
    rx_alt_t      alt[RX_ALTS];
    int           nalt;
    unsigned char cls[RX_CLS][32];  /* one bit per byte                    */
    int           ncls;
    int           root;
    int           cs;               /* 0 folds case while compiling        */
    const char   *err;
    const char   *start;            /* the whole subject, for '^'          */
} rx_t;

static int rx_parse_alt(rx_t *r, const char **p, int depth);

/**
 * @brief A fresh node, matching once and linked to nothing.  @return its
 * index, or -1 with the error set when the table is full.
 */
static int
rx_new_node(rx_t *r)
{
    if (r->nnode >= RX_NODES) {
        r->err = "expression too complex";
        return -1;
    }
    memset(&r->node[r->nnode], 0, sizeof r->node[0]);
    r->node[r->nnode].min = 1;
    r->node[r->nnode].max = 1;
    r->node[r->nnode].next = -1;
    r->node[r->nnode].cls = -1;
    r->node[r->nnode].grp = -1;
    return r->nnode++;
}

/** @brief Put @p c in a class's bitmap, and its other case unless @p cs. */
static void
cls_set(unsigned char *bits, unsigned char c, int cs)
{
    bits[c >> 3] |= (unsigned char)(1u << (c & 7u));
    if (!cs) {
        unsigned char o = (unsigned char)((c >= 'A' && c <= 'Z') ? c + 32
                          : (c >= 'a' && c <= 'z') ? c - 32 : c);
        bits[o >> 3] |= (unsigned char)(1u << (o & 7u));
    }
}

/**
 * @brief Parse "[...]" with @p p on the character after '['.
 *
 * @return the class's index, or -1 with the error set.
 */
static int
rx_parse_class(rx_t *r, const char **p, int cs)
{
    unsigned char *bits;
    int idx, negate = 0;
    const char *s = *p;

    if (r->ncls >= RX_CLS) {
        r->err = "expression too complex";
        return -1;
    }
    idx = r->ncls++;
    bits = r->cls[idx];
    memset(bits, 0, 32);
    if (*s == '^') {
        negate = 1;
        s++;
    }
    /* A ']' first is a literal ']', as in other regular expression
     * syntaxes. */
    if (*s == ']') {
        cls_set(bits, (unsigned char)*s, cs);
        s++;
    }
    while (*s != '\0' && *s != ']') {
        unsigned char lo;

        if (*s == '\\' && s[1] != '\0') {
            s++;
        }
        lo = (unsigned char)*s++;
        if (*s == '-' && s[1] != '\0' && s[1] != ']') {
            unsigned char hi = (unsigned char)s[1];
            unsigned int c;

            s += 2;
            for (c = lo; c <= hi; c++) {
                cls_set(bits, (unsigned char)c, cs);
            }
        } else {
            cls_set(bits, lo, cs);
        }
    }
    if (*s != ']') {
        r->err = "unmatched [";
        return -1;
    }
    s++;
    if (negate) {
        int i;

        for (i = 0; i < 32; i++) {
            bits[i] = (unsigned char)~bits[i];
        }
        bits[0] &= (unsigned char)~1u;   /* never match the terminator */
    }
    *p = s;
    return idx;
}

/**
 * @brief Parse one sequence.
 *
 * @return its head node index, or -1 when it is empty or on an error, which
 *         sets r->err.
 */
static int
rx_parse_seq(rx_t *r, const char **p, int depth)
{
    int head = -1, tail = -1;
    const int cs = r->cs;

    while (**p != '\0' && **p != '|' && **p != ')') {
        int n = rx_new_node(r);
        char c;

        if (n < 0) {
            return -1;
        }
        c = **p;
        switch (c) {
        case '(':
            (*p)++;
            r->node[n].type = RX_GROUP;
            r->node[n].grp = rx_parse_alt(r, p, depth + 1);
            if (r->node[n].grp < 0) {
                return -1;
            }
            if (**p != ')') {
                r->err = "unmatched (";
                return -1;
            }
            (*p)++;
            break;
        case '[':
            (*p)++;
            r->node[n].type = RX_CLASS;
            r->node[n].cls = rx_parse_class(r, p, cs);
            if (r->node[n].cls < 0) {
                return -1;
            }
            break;
        case '.':
            (*p)++;
            r->node[n].type = RX_ANY;
            break;
        case '^':
            (*p)++;
            r->node[n].type = RX_BOL;
            break;
        case '$':
            (*p)++;
            r->node[n].type = RX_EOL;
            break;
        case '*':
        case '+':
        case '?':
            r->err = "?+* follows nothing";
            return -1;
        case '\\':
            (*p)++;
            if (**p == '\0') {
                r->err = "trailing \\";
                return -1;
            }
            r->node[n].type = RX_CHAR;
            r->node[n].ch = (unsigned char)cond_lower(**p, cs);
            (*p)++;
            break;
        default:
            r->node[n].type = RX_CHAR;
            r->node[n].ch = (unsigned char)cond_lower(**p, cs);
            (*p)++;
            break;
        }
        switch (**p) {
        case '*': r->node[n].min = 0; r->node[n].max = -1; (*p)++; break;
        case '+': r->node[n].min = 1; r->node[n].max = -1; (*p)++; break;
        case '?': r->node[n].min = 0; r->node[n].max =  1; (*p)++; break;
        default: break;
        }
        if (head < 0) {
            head = n;
        } else {
            r->node[tail].next = n;
        }
        tail = n;
    }
    return head;
}

/**
 * @brief Parse an alternation at @p p: branches split on '|', up to ')' or
 * the end.  @return its index, or -1 with the error set when the table or
 * the nesting is exhausted.
 */
static int
rx_parse_alt(rx_t *r, const char **p, int depth)
{
    int idx;

    if (r->nalt >= RX_ALTS || depth > 8) {
        r->err = "expression too complex";
        return -1;
    }
    idx = r->nalt++;
    r->alt[idx].count = 0;
    for (;;) {
        int seq;

        if (r->alt[idx].count >= RX_BRANCH) {
            r->err = "too many alternatives";
            return -1;
        }
        seq = rx_parse_seq(r, p, depth);
        if (r->err != NULL) {
            return -1;
        }
        r->alt[idx].first[r->alt[idx].count++] = seq;
        if (**p != '|') {
            break;
        }
        (*p)++;
    }
    return idx;
}

/** @brief What is left to match after a node: a chain to continue, or a
 *         repetition to try again. */
typedef struct rx_cont {
    int                    kind;    /* 0 = continue a chain, 1 = repeat  */
    int                    node;
    int                    done;
    const char            *at;      /* where this repetition started     */
    const struct rx_cont  *next;
} rx_cont_t;

static int rx_node_match(rx_t *r, int n, const char *s, const rx_cont_t *k);
static int rx_repeat(rx_t *r, const rx_cont_t *k, const char *s);

/**
 * @brief Carry on with continuation @p k at @p s: none left is a match, a
 * node continues through its own chain, a repeat repeats.
 */
static int
rx_cont_match(rx_t *r, const rx_cont_t *k, const char *s)
{
    if (k == NULL) {
        return 1;                       /* the whole pattern is satisfied */
    }
    if (k->kind == 0) {
        return rx_node_match(r, k->node, s, k->next);
    }
    return rx_repeat(r, k, s);
}

/** @brief Match the atom itself, once, then continue. */
static int
rx_atom(rx_t *r, int n, const char *s, const rx_cont_t *k)
{
    const rx_node_t *nd = &r->node[n];

    switch (nd->type) {
    case RX_CHAR:
        if (*s != '\0' && (unsigned char)*s == nd->ch) {
            return rx_cont_match(r, k, s + 1);
        }
        return 0;
    case RX_ANY:
        if (*s != '\0') {
            return rx_cont_match(r, k, s + 1);
        }
        return 0;
    case RX_CLASS:
        if (*s != '\0' &&
            (r->cls[nd->cls][(unsigned char)*s >> 3] &
             (1u << ((unsigned char)*s & 7u))) != 0) {
            return rx_cont_match(r, k, s + 1);
        }
        return 0;
    case RX_BOL:
        return (s == r->start) ? rx_cont_match(r, k, s) : 0;
    case RX_EOL:
        return (*s == '\0') ? rx_cont_match(r, k, s) : 0;
    case RX_GROUP: {
        const rx_alt_t *a = &r->alt[nd->grp];
        int i;

        for (i = 0; i < a->count; i++) {
            if (a->first[i] < 0) {
                if (rx_cont_match(r, k, s)) {
                    return 1;
                }
            } else if (rx_node_match(r, a->first[i], s, k)) {
                return 1;
            }
        }
        return 0;
    }
    default:
        return 0;
    }
}

/**
 * @brief Match node @p n at @p s, then everything after it through @p k;
 * a node of -1 means only the continuation is left.
 */
static int
rx_node_match(rx_t *r, int n, const char *s, const rx_cont_t *k)
{
    const rx_node_t *nd;
    rx_cont_t after, again;
    const rx_cont_t *rest;

    if (n < 0) {
        return rx_cont_match(r, k, s);
    }
    nd = &r->node[n];
    after.kind = 0;
    after.node = nd->next;
    after.done = 0;
    after.at = NULL;
    after.next = k;
    rest = (nd->next >= 0) ? &after : k;

    if (nd->min == 1 && nd->max == 1) {
        return rx_atom(r, n, s, rest);
    }
    /* Greedy: take another repetition while one is allowed, and fall back to
     * the rest of the pattern only when it will not go. */
    if (nd->max < 0 || nd->max > 0) {
        again.kind = 1;
        again.node = n;
        again.done = 1;
        again.at = s;
        again.next = k;
        if (rx_atom(r, n, s, &again)) {
            return 1;
        }
    }
    if (nd->min == 0) {
        return rx_cont_match(r, rest, s);
    }
    return 0;
}

/**
 * @brief Entered after one repetition matched; decides whether to take more.
 *
 * A repetition that consumed nothing would repeat for ever, so the position
 * the last one started at is carried in the continuation and no further
 * repetition is attempted from the same place.
 */
static int
rx_repeat(rx_t *r, const rx_cont_t *k, const char *s)
{
    const rx_node_t *nd = &r->node[k->node];
    rx_cont_t after, again;
    const rx_cont_t *rest;

    after.kind = 0;
    after.node = nd->next;
    after.done = 0;
    after.at = NULL;
    after.next = k->next;
    rest = (nd->next >= 0) ? &after : k->next;

    if (s != k->at && (nd->max < 0 || k->done < nd->max)) {
        again.kind = 1;
        again.node = k->node;
        again.done = k->done + 1;
        again.at = s;
        again.next = k->next;
        if (rx_atom(r, k->node, s, &again)) {
            return 1;
        }
    }
    if (k->done >= nd->min) {
        return rx_cont_match(r, rest, s);
    }
    return 0;
}

/**
 * @brief Compile @p expr into @p r, case-sensitive when @p cs.
 * @return 1, or 0 with r->err naming what was wrong.
 */
static int
rx_compile(rx_t *r, const char *expr, int cs)
{
    const char *p = expr;

    int alt;

    memset(r, 0, sizeof *r);
    r->err = NULL;
    r->cs = cs;
    r->root = -1;
    alt = rx_parse_alt(r, &p, 0);
    if (alt < 0 || r->err != NULL) {
        return 0;
    }
    if (*p == ')') {
        r->err = "unmatched )";
        return 0;
    }
    r->root = alt;
    return 1;
}

/**
 * @brief Whether the compiled expression matches anywhere in @p text,
 * tried from each position in turn; an empty branch matches at once.
 */
static int
rx_search(rx_t *r, const char *text)
{
    const rx_alt_t *a = &r->alt[r->root];
    const char *p;

    r->start = text;
    for (p = text; ; p++) {
        int i;

        for (i = 0; i < a->count; i++) {
            if (a->first[i] < 0) {
                return 1;               /* an empty branch matches here   */
            }
            if (rx_node_match(r, a->first[i], p, NULL)) {
                return 1;
            }
        }
        if (*p == '\0') {
            break;
        }
    }
    return 0;
}

int
tiku_trk_regex_check(const char *expr, char *err, size_t errmax)
{
    rx_t r;

    if (expr == NULL) {
        if (err != NULL && errmax > 0u) {
            snprintf(err, errmax, "no expression");
        }
        return 0;
    }
    if (!rx_compile(&r, expr, 1)) {
        if (err != NULL && errmax > 0u) {
            snprintf(err, errmax, "%s",
                     (r.err != NULL) ? r.err : "bad expression");
        }
        return 0;
    }
    if (err != NULL && errmax > 0u) {
        err[0] = '\0';
    }
    return 1;
}

/*---------------------------------------------------------------------------*/

/** @brief A lowercased copy, for the case-folding paths. */
static void
fold_copy(char *out, size_t max, const char *in)
{
    size_t i;

    for (i = 0; i + 1u < max && in[i] != '\0'; i++) {
        out[i] = LOWER(in[i]);
    }
    out[(i < max) ? i : max - 1u] = '\0';
}

/* Room for a case-folded copy of the text and of the expression. */
#define MATCH_MAX 512

int
tiku_trk_match(const char *text, const char *expr, int case_sensitive,
               tiku_trk_match_t how)
{
    size_t tl, el;

    if (text == NULL || expr == NULL) {
        return 0;
    }
    tl = strlen(text);
    el = strlen(expr);

    switch (how) {
    case TIKU_TRK_MATCH_STARTS:
        if (el > tl) {
            return 0;
        }
        return case_sensitive ? (strncmp(text, expr, el) == 0)
                              : (strncasecmp(text, expr, el) == 0);
    case TIKU_TRK_MATCH_ENDS:
        /* Anchored at the end: a name shorter than the expression cannot
         * end with it. */
        if (el > tl) {
            return 0;
        }
        return case_sensitive ? (strcmp(text + tl - el, expr) == 0)
                              : (strcasecmp(text + tl - el, expr) == 0);
    case TIKU_TRK_MATCH_CONTAINS:
        if (el == 0u) {
            return 1;
        }
        return case_sensitive ? (strstr(text, expr) != NULL)
                              : (strcasestr(text, expr) != NULL);
    case TIKU_TRK_MATCH_GLOB:
        return glob_match(text, expr, case_sensitive);
    case TIKU_TRK_MATCH_REGEX: {
        rx_t r;
        char ftext[MATCH_MAX], fexpr[MATCH_MAX];

        if (!case_sensitive) {
            fold_copy(ftext, sizeof ftext, text);
            fold_copy(fexpr, sizeof fexpr, expr);
            text = ftext;
            expr = fexpr;
        }
        if (!rx_compile(&r, expr, 1)) {
            return 0;                   /* a bad expression matches nothing */
        }
        return rx_search(&r, text);
    }
    case TIKU_TRK_MATCH_NONE:
    default:
        /* kNone matches nothing: an unset rule does not behave like
         * "contains". */
        return 0;
    }
}
