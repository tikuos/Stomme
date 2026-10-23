/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_pdf_lex.c - the objects of a PDF, read from bytes.
 *
 * Numbers, strings, names, arrays, dictionaries and references, as
 * ISO 32000 section 7.3 spells them; a stream's dictionary is read
 * here and its bytes located, never copied.  The same reader takes a
 * content stream, where an unknown word is an operator.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_pdf_private.h"

#include <stdlib.h>
#include <string.h>

#define ARRAY_MAX 200000            /* items in one array or dictionary */

/** @brief Whether @p c is PDF white space, which includes NUL. */
static int
is_ws(int c)
{
    return c == ' ' || c == '\n' || c == '\r' || c == '\t' || c == '\f' ||
           c == '\0';
}

/** @brief Whether @p c ends a token. */
static int
is_delim(int c)
{
    return c == '(' || c == ')' || c == '<' || c == '>' || c == '[' ||
           c == ']' || c == '{' || c == '}' || c == '/' || c == '%';
}

/** @brief A hex digit's value, or -1. */
static int
hexval(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

pdf_obj_t *
pdf_new(pdf_kind_t kind)
{
    pdf_obj_t *o = calloc(1, sizeof *o);

    if (o != NULL) {
        o->kind = kind;
    }
    return o;
}

void
pdf_free(pdf_obj_t *o)
{
    int i;

    if (o == NULL) {
        return;
    }
    switch (o->kind) {
    case PDF_STRING: case PDF_NAME: case PDF_OP:
        free(o->u.s.p);
        break;
    case PDF_ARRAY:
        for (i = 0; i < o->u.a.n; i++) {
            pdf_free(o->u.a.item[i]);
        }
        free(o->u.a.item);
        break;
    case PDF_DICT:
        for (i = 0; i < o->u.d.n; i++) {
            free(o->u.d.key[i]);
            pdf_free(o->u.d.val[i]);
        }
        free(o->u.d.key);
        free(o->u.d.val);
        break;
    case PDF_STREAM:
        pdf_free(o->u.st.dict);
        break;
    default:
        break;
    }
    free(o);
}

int
pdf_skip_ws(pdf_cur_t *c)
{
    while (c->pos < c->n) {
        int ch = c->p[c->pos];

        if (is_ws(ch)) {
            c->pos++;
        } else if (ch == '%') {
            while (c->pos < c->n && c->p[c->pos] != '\n' &&
                   c->p[c->pos] != '\r') {
                c->pos++;
            }
        } else {
            return 1;
        }
    }
    return 0;
}

int
pdf_keyword_at(const pdf_cur_t *c, const char *word)
{
    long n = (long)strlen(word);

    if (c->pos + n > c->n || memcmp(c->p + c->pos, word, (size_t)n) != 0) {
        return 0;
    }
    return c->pos + n == c->n || is_ws(c->p[c->pos + n]) ||
           is_delim(c->p[c->pos + n]);
}

long
pdf_find(const unsigned char *p, long n, long from, const char *word)
{
    long w = (long)strlen(word), i;

    if (from < 0) {
        from = 0;
    }
    for (i = from; i + w <= n; i++) {
        if (p[i] == (unsigned char)word[0] &&
            memcmp(p + i, word, (size_t)w) == 0) {
            return i;
        }
    }
    return -1;
}

int
pdf_dict_set(pdf_obj_t *d, const char *key, pdf_obj_t *val)
{
    char **keys;
    pdf_obj_t **vals;
    int i;

    for (i = 0; i < d->u.d.n; i++) {
        if (strcmp(d->u.d.key[i], key) == 0) {
            pdf_free(d->u.d.val[i]);
            d->u.d.val[i] = val;
            return 0;
        }
    }
    if (d->u.d.n >= ARRAY_MAX) {
        return -1;
    }
    keys = realloc(d->u.d.key, (size_t)(d->u.d.n + 1) * sizeof *keys);
    if (keys == NULL) {
        return -1;
    }
    d->u.d.key = keys;
    vals = realloc(d->u.d.val, (size_t)(d->u.d.n + 1) * sizeof *vals);
    if (vals == NULL) {
        return -1;
    }
    d->u.d.val = vals;
    keys[d->u.d.n] = strdup(key);
    if (keys[d->u.d.n] == NULL) {
        return -1;
    }
    vals[d->u.d.n] = val;
    d->u.d.n++;
    return 0;
}

pdf_obj_t *
pdf_dict_raw(const pdf_obj_t *d, const char *key)
{
    int i;

    if (d == NULL) {
        return NULL;
    }
    if (d->kind == PDF_STREAM) {
        d = d->u.st.dict;
    }
    if (d == NULL || d->kind != PDF_DICT) {
        return NULL;
    }
    for (i = 0; i < d->u.d.n; i++) {
        if (strcmp(d->u.d.key[i], key) == 0) {
            return d->u.d.val[i];
        }
    }
    return NULL;
}

int
pdf_array_push(pdf_obj_t *a, pdf_obj_t *item)
{
    pdf_obj_t **items;

    if (a->u.a.n >= ARRAY_MAX) {
        return -1;
    }
    items = realloc(a->u.a.item, (size_t)(a->u.a.n + 1) * sizeof *items);
    if (items == NULL) {
        return -1;
    }
    a->u.a.item = items;
    items[a->u.a.n++] = item;
    return 0;
}

int
pdf_is_name(const pdf_obj_t *o, const char *name)
{
    return o != NULL && o->kind == PDF_NAME &&
           strlen(name) == o->u.s.n &&
           memcmp(o->u.s.p, name, o->u.s.n) == 0;
}

double
pdf_number(const pdf_obj_t *o, double dflt)
{
    if (o == NULL) {
        return dflt;
    }
    if (o->kind == PDF_INT) {
        return (double)o->u.i;
    }
    if (o->kind == PDF_REAL) {
        return o->u.r;
    }
    return dflt;
}

int
pdf_int(const pdf_obj_t *o, int dflt)
{
    double v = pdf_number(o, dflt);

    return v > -2147483648.0 && v < 2147483648.0 ? (int)v : dflt;
}

/* ------------------------------------------------------------- tokens  */

/** @brief Bytes of a string or name, grown as they come. */
typedef struct {
    unsigned char *p;
    size_t         n, cap;
    int            failed;
} buf_t;

/** @brief One byte more; a failure is remembered, not reported yet. */
static void
buf_add(buf_t *b, int ch)
{
    if (b->n + 1 >= b->cap) {
        size_t cap = b->cap ? b->cap * 2 : 32;
        unsigned char *p = realloc(b->p, cap);

        if (p == NULL) {
            b->failed = 1;
            return;
        }
        b->p = p;
        b->cap = cap;
    }
    b->p[b->n++] = (unsigned char)ch;
}

/** @brief Hand @p b's bytes to a new object of @p kind, NUL-terminated. */
static pdf_obj_t *
buf_object(buf_t *b, pdf_kind_t kind)
{
    pdf_obj_t *o;

    buf_add(b, 0);
    if (b->failed) {
        free(b->p);
        return NULL;
    }
    o = pdf_new(kind);
    if (o == NULL) {
        free(b->p);
        return NULL;
    }
    o->u.s.p = b->p;
    o->u.s.n = b->n - 1;
    return o;
}

/** @brief A literal string: nested parentheses and backslash escapes. */
static pdf_obj_t *
lex_string(pdf_cur_t *c)
{
    buf_t b = {0};
    int depth = 1;

    c->pos++;                       /* the opening parenthesis */
    while (c->pos < c->n && depth > 0) {
        int ch = c->p[c->pos++];

        if (ch == '\\') {
            int v;

            if (c->pos >= c->n) {
                break;
            }
            ch = c->p[c->pos++];
            switch (ch) {
            case 'n': buf_add(&b, '\n'); break;
            case 'r': buf_add(&b, '\r'); break;
            case 't': buf_add(&b, '\t'); break;
            case 'b': buf_add(&b, '\b'); break;
            case 'f': buf_add(&b, '\f'); break;
            case '\r':
                if (c->pos < c->n && c->p[c->pos] == '\n') {
                    c->pos++;
                }
                break;
            case '\n': break;
            default:
                if (ch >= '0' && ch <= '7') {
                    int k;

                    v = ch - '0';
                    for (k = 0; k < 2 && c->pos < c->n &&
                                c->p[c->pos] >= '0' && c->p[c->pos] <= '7';
                         k++) {
                        v = v * 8 + (c->p[c->pos++] - '0');
                    }
                    buf_add(&b, v & 0xff);
                } else {
                    buf_add(&b, ch);
                }
            }
        } else if (ch == '(') {
            depth++;
            buf_add(&b, ch);
        } else if (ch == ')') {
            depth--;
            if (depth > 0) {
                buf_add(&b, ch);
            }
        } else {
            buf_add(&b, ch);
        }
    }
    return buf_object(&b, PDF_STRING);
}

/** @brief A hex string: pairs of digits, white space ignored. */
static pdf_obj_t *
lex_hex(pdf_cur_t *c)
{
    buf_t b = {0};
    int hi = -1;

    c->pos++;                       /* the < */
    while (c->pos < c->n && c->p[c->pos] != '>') {
        int v = hexval(c->p[c->pos++]);

        if (v < 0) {
            continue;
        }
        if (hi < 0) {
            hi = v;
        } else {
            buf_add(&b, hi * 16 + v);
            hi = -1;
        }
    }
    if (hi >= 0) {
        buf_add(&b, hi * 16);       /* a lone last digit is followed by 0 */
    }
    if (c->pos < c->n) {
        c->pos++;
    }
    return buf_object(&b, PDF_STRING);
}

/** @brief A name: the slash dropped, #xx pairs resolved. */
static pdf_obj_t *
lex_name(pdf_cur_t *c)
{
    buf_t b = {0};

    c->pos++;
    while (c->pos < c->n && !is_ws(c->p[c->pos]) &&
           !is_delim(c->p[c->pos])) {
        int ch = c->p[c->pos++];

        if (ch == '#' && c->pos + 1 < c->n && hexval(c->p[c->pos]) >= 0 &&
            hexval(c->p[c->pos + 1]) >= 0) {
            ch = hexval(c->p[c->pos]) * 16 + hexval(c->p[c->pos + 1]);
            c->pos += 2;
        }
        buf_add(&b, ch);
    }
    return buf_object(&b, PDF_NAME);
}

/** @brief A number, or a bare word (keyword or operator). */
static pdf_obj_t *
lex_word(pdf_cur_t *c)
{
    long start = c->pos;
    int numeric = 1, digits = 0, dots = 0;
    buf_t b = {0};
    pdf_obj_t *o;

    while (c->pos < c->n && !is_ws(c->p[c->pos]) &&
           !is_delim(c->p[c->pos])) {
        int ch = c->p[c->pos++];

        if (ch >= '0' && ch <= '9') {
            digits++;
        } else if (ch == '.') {
            dots++;
        } else if (!((ch == '+' || ch == '-') && c->pos - 1 == start)) {
            numeric = 0;
        }
        buf_add(&b, ch);
        if (b.n > 4096) {
            break;                  /* a longer word is split here */
        }
    }
    if (c->pos == start) {
        c->pos++;                   /* a stray delimiter: step over it */
    }
    if (numeric && digits > 0 && dots <= 1) {
        char *end = NULL;

        buf_add(&b, 0);
        o = pdf_new(dots ? PDF_REAL : PDF_INT);
        if (o != NULL && !b.failed) {
            if (dots) {
                o->u.r = strtod((char *)b.p, &end);
            } else {
                o->u.i = strtol((char *)b.p, &end, 10);
            }
        }
        free(b.p);
        return o;
    }
    if (b.n == 4 && memcmp(b.p, "true", 4) == 0) {
        free(b.p);
        o = pdf_new(PDF_BOOL);
        if (o) o->u.b = 1;
        return o;
    }
    if (b.n == 5 && memcmp(b.p, "false", 5) == 0) {
        free(b.p);
        return pdf_new(PDF_BOOL);
    }
    if (b.n == 4 && memcmp(b.p, "null", 4) == 0) {
        free(b.p);
        return pdf_new(PDF_NULL);
    }
    return buf_object(&b, PDF_OP);
}

/* ------------------------------------------------------------ objects  */

static pdf_obj_t *parse_array(pdf_cur_t *c, int depth);
static pdf_obj_t *parse_dict(pdf_cur_t *c, int depth);

/**
 * @brief Turn the two integers before an R into a reference.
 *
 * "1 0 R" reads as three tokens; when the R arrives, the two numbers
 * already on @p a's end are taken back.  @return 1 when it was one.
 */
static int
fold_ref(pdf_obj_t *a)
{
    pdf_obj_t *num, *gen, *ref;
    int n = a->u.a.n;

    if (n < 2 || a->u.a.item[n - 1]->kind != PDF_INT ||
        a->u.a.item[n - 2]->kind != PDF_INT) {
        return 0;
    }
    num = a->u.a.item[n - 2];
    gen = a->u.a.item[n - 1];
    ref = pdf_new(PDF_REF);
    if (ref == NULL) {
        return 0;
    }
    ref->u.ref.num = num->u.i;
    ref->u.ref.gen = gen->u.i;
    pdf_free(num);
    pdf_free(gen);
    a->u.a.item[n - 2] = ref;
    a->u.a.n = n - 1;
    return 1;
}

pdf_obj_t *
pdf_parse(pdf_cur_t *c, int depth)
{
    int ch;

    if (depth > PDF_NEST_MAX || !pdf_skip_ws(c)) {
        return NULL;
    }
    ch = c->p[c->pos];
    switch (ch) {
    case '(':  return lex_string(c);
    case '/':  return lex_name(c);
    case '[':  return parse_array(c, depth + 1);
    case '<':
        if (c->pos + 1 < c->n && c->p[c->pos + 1] == '<') {
            return parse_dict(c, depth + 1);
        }
        return lex_hex(c);
    case ')': case '>': case ']': case '}': case '{':
        c->pos++;                   /* unbalanced: skipped, not fatal */
        return pdf_parse(c, depth);
    default:
        return lex_word(c);
    }
}

/** @brief [ ... ], with "n g R" folded as it is met. */
static pdf_obj_t *
parse_array(pdf_cur_t *c, int depth)
{
    pdf_obj_t *a = pdf_new(PDF_ARRAY);

    if (a == NULL) {
        return NULL;
    }
    c->pos++;
    while (pdf_skip_ws(c)) {
        pdf_obj_t *o;

        if (c->p[c->pos] == ']') {
            c->pos++;
            return a;
        }
        o = pdf_parse(c, depth);
        if (o == NULL) {
            break;
        }
        if (o->kind == PDF_OP) {
            int is_r = o->u.s.n == 1 && o->u.s.p[0] == 'R';

            pdf_free(o);
            if (is_r && fold_ref(a)) {
                continue;
            }
            if (!is_r) {
                /* A keyword inside an array is a broken file; take
                 * the array as read so far. */
                return a;
            }
            continue;
        }
        if (pdf_array_push(a, o) != 0) {
            pdf_free(o);
            break;
        }
    }
    return a;
}

/** @brief Give the values waiting in @p pending to @p d under @p key. */
static void
settle(pdf_obj_t *d, char **key, pdf_obj_t *pending)
{
    int i;

    for (i = 0; i < pending->u.a.n; i++) {
        if (pdf_dict_set(d, *key, pending->u.a.item[i]) != 0) {
            pdf_free(pending->u.a.item[i]);
        }
    }
    pending->u.a.n = 0;
    free(*key);
    *key = NULL;
}

/**
 * @brief << /Key value ... >>, and the stream that may follow.
 *
 * A value may still turn into a reference two tokens on ("3 0 R"), so
 * it waits in @c pending until the next key or the end settles it.
 */
static pdf_obj_t *
parse_dict(pdf_cur_t *c, int depth)
{
    pdf_obj_t *d = pdf_new(PDF_DICT), *pending = pdf_new(PDF_ARRAY);
    char *key = NULL;

    if (d == NULL || pending == NULL) {
        pdf_free(d);
        pdf_free(pending);
        return NULL;
    }
    c->pos += 2;
    while (pdf_skip_ws(c)) {
        pdf_obj_t *o;
        int at_end = c->p[c->pos] == '>' && c->pos + 1 < c->n &&
                     c->p[c->pos + 1] == '>';

        /* A name after a key with nothing waiting is the value; one
         * after a value is the next key. */
        if (key != NULL &&
            (at_end || (c->p[c->pos] == '/' && pending->u.a.n > 0))) {
            settle(d, &key, pending);
        }
        if (at_end) {
            c->pos += 2;
            break;
        }
        o = pdf_parse(c, depth);
        if (o == NULL) {
            break;
        }
        if (o->kind == PDF_OP) {
            int is_r = o->u.s.n == 1 && o->u.s.p[0] == 'R';

            pdf_free(o);
            if (!is_r) {
                break;              /* endobj or worse: stop here */
            }
            (void)fold_ref(pending);
            continue;
        }
        if (key == NULL) {
            if (o->kind == PDF_NAME) {
                key = strdup((char *)o->u.s.p);
            }
            pdf_free(o);
            continue;
        }
        if (pdf_array_push(pending, o) != 0) {
            pdf_free(o);
            break;
        }
    }
    if (key != NULL) {
        settle(d, &key, pending);
    }
    pdf_free(pending);
    /* A stream: its bytes start after the EOL that follows "stream". */
    if (pdf_skip_ws(c) && pdf_keyword_at(c, "stream")) {
        pdf_obj_t *st = pdf_new(PDF_STREAM);

        c->pos += 6;
        if (c->pos < c->n && c->p[c->pos] == '\r') {
            c->pos++;
        }
        if (c->pos < c->n && c->p[c->pos] == '\n') {
            c->pos++;
        }
        if (st == NULL) {
            pdf_free(d);
            return NULL;
        }
        st->u.st.dict = d;
        st->u.st.off = c->pos;
        st->u.st.len = -1;
        st->u.st.num = st->u.st.gen = -1;
        return st;
    }
    return d;
}
