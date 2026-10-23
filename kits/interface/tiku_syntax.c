/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_syntax.c - telling one line of TikuOS BASIC or C apart.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <string.h>

#include "tiku_syntax.h"

/*
 * Every spelling the interpreter recognises, sorted for a binary search.
 *
 * The list is the union of three things the interpreter holds: the crunch
 * table BASIC_TOK_LIST (the words that fold to a byte when a numbered
 * line is stored), the spellings the dispatchers match by hand and never
 * crunch, and the six the bundled extension kit registers at boot (GCD,
 * ISQRT, BITCNT, HEXPR, REV$, ROMAN$), which are refused as variable
 * names while they are registered.
 *
 * The table over-promises on a small build: most of these words are
 * compiled out of one (PEEK and POKE, the GPIO, BLE and HTTP families,
 * the whole `$` sub-language), and there a compiled-out word is an
 * ordinary variable name.  The editor cannot learn which build the board
 * on the other end of the cable runs, so the table holds the whole
 * language.
 */
static const char *const basic_word[] = {
    "ABS", "ADC", "AND", "APPEND", "ASC", "ATAN", "AUTO", "BASE64$",
    "BETWEEN$", "BIN$", "BITCNT", "BLEADV", "BLEAVAIL", "BLEBEACON",
    "BLEGET$", "BLEOBSERVE", "BLEOFF", "BLESCAN$", "BLESEEN", "BLESEEN$",
    "BLESEND", "BLEUP", "BROWSE", "BYE", "CALL", "CASE", "CHANGE", "CHR$",
    "CLS", "CONST", "CONTINUE", "COS", "COUNT", "DATA", "DATE$", "DEF",
    "DELAY", "DIGREAD", "DIGWRITE", "DIM", "DIR", "ELSE", "ELSEIF", "END",
    "ENDIF", "ENDSUB", "ERL", "ERR", "ERROR", "EVERY", "EXIT", "EXP",
    "FALSE", "FDIV", "FETCH", "FMUL", "FN", "FOR", "FPOW", "FREAD$",
    "FSTR$", "FWRITE", "GCD", "GOSUB", "GOTO", "HELP", "HEX$", "HEXPR",
    "HMAC$", "HTTPGET$", "HTTPHEADER", "HTTPPOST$", "HTTPSTATUS", "I2CREAD",
    "I2CWRITE", "IF", "IMPORT", "INKEY$", "INPUT", "INSTR", "INT",
    "IPADDR$", "ISQRT", "JSON$", "LCASE$", "LED", "LEFT$", "LEN", "LET",
    "LINE$", "LIST", "LOAD", "LOCAL", "LOG", "LOWER$", "LTRIM$", "MAX",
    "MID$", "MILLIS", "MIN", "MOD", "MODACT", "MODLOAD", "MQTTPUB",
    "MQTTWAIT$", "NETUP", "NEW", "NEXT", "NOT", "NOW", "OFF", "ON", "OR",
    "PEEK", "PERSIST", "PI", "PIN", "POKE", "POW", "PRINT", "QUIT", "READ",
    "REBOOT", "REM", "RENUM", "REPEAT", "REPLACE$", "RESTORE", "RESULT",
    "RESUME", "RETURN", "REV$", "RIGHT$", "RND", "ROMAN$", "RTRIM$", "RUN",
    "SAVE", "SECS", "SELECT", "SETTIME", "SGN", "SHA256$", "SHL", "SHR",
    "SIN", "SLEEP", "SPACE$", "SPC", "SQR", "STEP", "STOP", "STR$",
    "STRING$", "STRIP$", "SUB", "SWAP", "TAB", "TAN", "THEN", "TIME$",
    "TIMER", "TO", "TRACE", "TRIM$", "TRUE", "UCASE$", "UDPSEND", "UNTIL",
    "UPPER$", "USING", "VAL", "VFSREAD", "VFSREAD$", "VFSWRITE",
    "VFSWRITE$", "WEND", "WHILE", "WORD$", "XOR",
};

#define BASIC_WORD_N ((int)(sizeof basic_word / sizeof basic_word[0]))

const char *
tiku_syntax_basic_keyword(int index)
{
    return index >= 0 && index < BASIC_WORD_N ? basic_word[index] : NULL;
}

/** @brief Room for the longest spelling above and its terminator. */
#define BASIC_WORD_MAX 16

/** @brief Whether @p c is an ASCII letter. */
static int
is_alpha(int c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

/** @brief Whether @p c is a decimal digit. */
static int
is_digit(int c)
{
    return c >= '0' && c <= '9';
}

/** @brief What may continue a word once it has started (the lexer's rule). */
static int
is_word_cont(int c)
{
    return is_alpha(c) || is_digit(c) || c == '_';
}

/** @brief Whether @p c is a hexadecimal digit. */
static int
is_hex(int c)
{
    return is_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

/** @brief @p c upper-cased, for ASCII letters. */
static int
up(int c)
{
    return (c >= 'a' && c <= 'z') ? c - 'a' + 'A' : c;
}

int
tiku_syntax_basic_word(const char *name)
{
    char folded[BASIC_WORD_MAX];
    int lo = 0, hi = BASIC_WORD_N - 1;
    size_t i, n;

    if (name == NULL) {
        return 0;
    }
    n = strlen(name);
    if (n == 0u || n >= sizeof folded) {
        /* Longer than any spelling, so not one of them.  Folding it into
         * the buffer cuts it short: VFSWRITELONG reads as VFSWRITE. */
        return 0;
    }
    for (i = 0u; i < n; i++) {
        folded[i] = (char)up((unsigned char)name[i]);
    }
    folded[n] = '\0';
    while (lo <= hi) {
        int mid = lo + (hi - lo) / 2;
        int cmp = strcmp(folded, basic_word[mid]);

        if (cmp == 0) {
            return 1;
        }
        if (cmp < 0) {
            hi = mid - 1;
        } else {
            lo = mid + 1;
        }
    }
    return 0;
}

tiku_syntax_lang_t
tiku_syntax_of_path(const char *path)
{
    size_t n;

    if (path == NULL) {
        return TIKU_SYNTAX_NONE;
    }
    n = strlen(path);
    if (n >= 4u) {
        const char *tail = path + n - 4;

        if (tail[0] == '.' && up((unsigned char)tail[1]) == 'B' &&
            up((unsigned char)tail[2]) == 'A' &&
            up((unsigned char)tail[3]) == 'S') {
            return TIKU_SYNTAX_BASIC;
        }
    }
    if (n >= 2u && path[n - 2] == '.' &&
        (up((unsigned char)path[n - 1]) == 'C' ||
         up((unsigned char)path[n - 1]) == 'H')) {
        return TIKU_SYNTAX_C;
    }
    return TIKU_SYNTAX_NONE;
}

typedef struct spans spans_t;
static int basic_spans(spans_t *s, tiku_syntax_lang_t lang,
                       const char *line);

/*---------------------------------------------------------------------------*/
/* TELLING A LINE APART                                                      */
/*---------------------------------------------------------------------------*/

/** @brief Where the runs go: a table with the room left in it, or, when
 *         streaming, a function handed each run once it is whole. */
struct spans {
    tiku_span_t *out;
    int          max;
    int          n;
    int          full;          /* a run was dropped: record no more */
    int          stream;        /* runs go to fn, and none is dropped */
    tiku_syntax_run_fn fn;      /* may be NULL: the runs are not wanted */
    void        *ctx;
    tiku_span_t  run;           /* the run being gathered for fn      */
};

/** @brief A sink filling @p out, at most @p max runs. */
static void
spans_table(spans_t *s, tiku_span_t *out, int max)
{
    memset(s, 0, sizeof *s);
    s->out = out;
    s->max = max;
}

/** @brief Hand the last run gathered to the function, when streaming. */
static void
spans_finish(spans_t *s)
{
    if (s->stream && s->fn != NULL && s->run.len > 0) {
        s->fn(s->ctx, s->run.ink, s->run.len);
    }
    s->run.len = 0;
}

/**
 * @brief Add @p len bytes meaning @p ink, merging with the run before it
 *        when they mean the same thing.
 *
 * Merging keeps an ordinary line to one run.  Once a run has been dropped
 * for want of room nothing more is recorded, not even a merge, so the
 * runs held cover only the bytes they were given.
 *
 * @return nonzero while there is still room to say more.
 */
static int
push(spans_t *s, tiku_ink_t ink, int len)
{
    if (len <= 0) {
        return 1;
    }
    if (s->stream) {
        if (s->run.len > 0 && s->run.ink == ink) {
            s->run.len += len;
            return 1;
        }
        spans_finish(s);
        s->run.ink = ink;
        s->run.len = len;
        return 1;
    }
    if (s->full) {
        return 0;
    }
    if (s->n > 0 && s->out[s->n - 1].ink == ink) {
        s->out[s->n - 1].len += len;
        return 1;
    }
    if (s->n >= s->max) {
        s->full = 1;            /* full: the rest of the line is plain */
        return 0;
    }
    s->out[s->n].ink = ink;
    s->out[s->n].len = len;
    s->n++;
    return 1;
}

/**
 * @brief How many bytes of a number literal start at @p p, or 0.
 *
 * Every form the interpreter's parse_unum() accepts and no others: 0x/0X
 * and 0b/0B, &H/&h and &B/&b, and decimal with an optional fractional
 * part.  There is no exponent form, no leading-dot form and no sign: a
 * minus is an operator, so in `A-1` only the 1 is a number.
 */
static int
number_at(const char *p)
{
    int i = 0;

    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X') && is_hex(p[2])) {
        i = 2;
        while (is_hex(p[i])) {
            i++;
        }
        return i;
    }
    if (p[0] == '0' && (p[1] == 'b' || p[1] == 'B') &&
        (p[2] == '0' || p[2] == '1')) {
        i = 2;
        while (p[i] == '0' || p[i] == '1') {
            i++;
        }
        return i;
    }
    if (p[0] == '&' && (p[1] == 'H' || p[1] == 'h') && is_hex(p[2])) {
        i = 2;
        while (is_hex(p[i])) {
            i++;
        }
        return i;
    }
    if (p[0] == '&' && (p[1] == 'B' || p[1] == 'b') &&
        (p[2] == '0' || p[2] == '1')) {
        i = 2;
        while (p[i] == '0' || p[i] == '1') {
            i++;
        }
        return i;
    }
    if (!is_digit((unsigned char)p[0])) {
        return 0;
    }
    while (is_digit((unsigned char)p[i])) {
        i++;
    }
    if (p[i] == '.') {
        int j = i + 1;

        while (is_digit((unsigned char)p[j])) {
            j++;
        }
        i = j;                  /* `1.` is a number too: the scale is Q.3 */
    }
    return i;
}

/**
 * @brief How many bytes the quoted string starting at @p p occupies.
 *
 * A backslash takes the next byte with it, so `"a\"b"` is one string and
 * not two.  An unterminated string runs to the end of the line, as the
 * interpreter reads it.
 */
static int
string_at(const char *p)
{
    int i = 1;                  /* past the opening quote */

    while (p[i] != '\0') {
        if (p[i] == '\\' && p[i + 1] != '\0') {
            i += 2;
            continue;
        }
        if (p[i] == '"') {
            return i + 1;
        }
        i++;
    }
    return i;
}

/** @brief The word at @p p: letters, then letters/digits/_, then a `$`. */
static int
word_at(const char *p)
{
    int i = 0;

    if (!is_alpha((unsigned char)p[0])) {
        return 0;
    }
    while (is_word_cont((unsigned char)p[i])) {
        i++;
    }
    if (p[i] == '$') {
        /* The sigil must touch the name -- `A $` is not `A$` -- which is
         * the same rule that makes STR$ one word and not two. */
        i++;
    }
    return i;
}

/**
 * @brief Whether the line opens with a label definition, and how long it
 *        is including the colon.
 *
 * A name of two or more characters followed by `:` at the head of a line
 * is a jump target.  One character is not: `A:` is the variable A and
 * then a statement separator, which is what the interpreter reads.
 */
static int
label_at(const char *p)
{
    int i = 0;

    if (!is_alpha((unsigned char)p[0])) {
        return 0;
    }
    while (is_word_cont((unsigned char)p[i])) {
        i++;
    }
    return (i >= 2 && p[i] == ':') ? i + 1 : 0;
}

/** @brief How many decimal digits start @p p. */
static size_t
digits_at(const char *p)
{
    size_t n = 0;

    while (is_digit((unsigned char)p[n])) {
        n++;
    }
    return n;
}

int
tiku_syntax_basic_names(const char *line, const char *target, size_t len)
{
    const char *p = line;
    size_t n, want;
    int label;

    if (line == NULL || target == NULL || len == 0u) {
        return 0;
    }
    while (*p == ' ' || *p == '\t') {
        p++;
    }
    n = digits_at(p);
    if (is_digit((unsigned char)target[0])) {
        /* A number names a line by its value: 0100 is line 100. */
        want = digits_at(target);
        if (want != len) {
            return 0;
        }
        while (want > 1u && *target == '0') {
            target++;
            want--;
        }
        while (n > 1u && *p == '0') {
            p++;
            n--;
        }
        return n == want && memcmp(p, target, n) == 0;
    }
    p += n;
    while (*p == ' ' || *p == '\t') {
        p++;
    }
    label = label_at(p);
    return label > 0 && (size_t)label - 1u == len &&
           memcmp(p, target, len) == 0;
}

/*---------------------------------------------------------------------------*/
/* C                                                                         */
/*---------------------------------------------------------------------------*/

/* The words C reserves and no more, so a variable named like a library
 * function stays plain. */
static const char *const c_word[] = {
    "auto", "break", "case", "char", "const", "continue", "default",
    "do", "double", "else", "enum", "extern", "float", "for", "goto",
    "if", "inline", "int", "long", "register", "restrict", "return",
    "short", "signed", "sizeof", "static", "struct", "switch",
    "typedef", "union", "unsigned", "void", "volatile", "while",
    "_Alignas", "_Alignof", "_Atomic", "_Bool", "_Complex", "_Generic",
    "_Noreturn", "_Static_assert", "_Thread_local"
};

/** @brief Whether the @p len bytes at @p p are a word C reserves. */
static int
c_word_at(const char *p, int len)
{
    size_t i;

    for (i = 0; i < sizeof c_word / sizeof c_word[0]; i++) {
        if (c_word[i][0] == p[0] &&
            strncmp(p, c_word[i], (size_t)len) == 0 &&
            c_word[i][len] == '\0') {
            return 1;
        }
    }
    return 0;
}

/** @brief How many bytes of a C name start at @p p, or 0. */
static int
c_name_at(const char *p)
{
    int i = 0;

    if (!is_alpha((unsigned char)p[0]) && p[0] != '_') {
        return 0;
    }
    while (is_word_cont((unsigned char)p[i]) || p[i] == '_') {
        i++;
    }
    return i;
}

/**
 * @brief Tell one line of C apart, carrying a block remark across.
 *
 * @return the number of runs written; @p left takes what this line
 *         leaves open.
 */
static int
c_spans(spans_t *sink, const char *line, tiku_syntax_state_t in,
        tiku_syntax_state_t *left)
{
    spans_t s = *sink;
    tiku_syntax_state_t state = in;
    int i = 0;
    int at_head = 1;            /* nothing but blanks seen yet */

    while (line[i] != '\0') {
        char c = line[i];

        if (state == TIKU_SYNTAX_BLOCK) {
            /* Inside a block remark: everything is the remark until it
             * closes, and if it does not close the next line begins
             * inside it too. */
            int run = 0;

            while (line[i + run] != '\0') {
                if (line[i + run] == '*' && line[i + run + 1] == '/') {
                    run += 2;
                    state = TIKU_SYNTAX_OPEN;
                    break;
                }
                run++;
            }
            (void)push(&s, TIKU_INK_REMARK, run);
            i += run;
            continue;
        }
        if (c == ' ' || c == '\t') {
            (void)push(&s, TIKU_INK_PLAIN, 1);
            i++;
            continue;
        }
        if (c == '/' && line[i + 1] == '/') {
            /* To the end of the line and no further: C does not carry
             * this remark. */
            (void)push(&s, TIKU_INK_REMARK, (int)strlen(line + i));
            i += (int)strlen(line + i);
            at_head = 0;
            continue;
        }
        if (c == '/' && line[i + 1] == '*') {
            state = TIKU_SYNTAX_BLOCK;
            at_head = 0;
            i += 2;
            (void)push(&s, TIKU_INK_REMARK, 2);
            continue;
        }
        if (c == '"' || c == '\'') {
            /* A character constant is quoted text too; an unclosed one
             * ends with the line, as in BASIC. */
            int len = 1;

            while (line[i + len] != '\0') {
                if (line[i + len] == '\\' && line[i + len + 1] != '\0') {
                    len += 2;
                    continue;
                }
                if (line[i + len] == c) {
                    len++;
                    break;
                }
                len++;
            }
            (void)push(&s, TIKU_INK_STR, len);
            i += len;
            at_head = 0;
            continue;
        }
        if (at_head && c == '#') {
            /* A preprocessor directive at the head of the line is
             * coloured as a reserved word. */
            int len = 1 + c_name_at(line + i + 1);

            (void)push(&s, TIKU_INK_WORD, len);
            i += len;
            at_head = 0;
            continue;
        }
        if (is_digit((unsigned char)c)) {
            int len = number_at(line + i);

            if (len <= 0) {
                len = 1;
            }
            (void)push(&s, TIKU_INK_NUM, len);
            i += len;
            at_head = 0;
            continue;
        }
        {
            int len = c_name_at(line + i);

            if (len > 0) {
                (void)push(&s, c_word_at(line + i, len) ? TIKU_INK_WORD
                                                       : TIKU_INK_PLAIN,
                          len);
                i += len;
                at_head = 0;
                continue;
            }
        }
        (void)push(&s, TIKU_INK_PLAIN, 1);
        i++;
        at_head = 0;
    }
    if (left != NULL) {
        *left = state;
    }
    spans_finish(&s);
    *sink = s;
    return s.n;
}

/**
 * @brief Fold the @p len bytes at @p p into @p buf, upper-cased.
 *
 * One fold answers both questions asked of a word, whether it is reserved
 * and whether it changes how the rest of the line reads, so the answers
 * agree.  A word too long for @p max folds to nothing, so VFSWRITELONG
 * does not read as VFSWRITE.
 *
 * @return 1, or 0 when the word does not fit
 */
static int
fold(const char *p, int len, char *buf, size_t max)
{
    int i;

    if (len <= 0 || (size_t)len >= max) {
        buf[0] = '\0';
        return 0;
    }
    for (i = 0; i < len; i++) {
        buf[i] = (char)up((unsigned char)p[i]);
    }
    buf[len] = '\0';
    return 1;
}

int
tiku_syntax_spans(tiku_syntax_lang_t lang, const char *line,
                  tiku_span_t *out, int max)
{
    return tiku_syntax_spans_on(lang, line, TIKU_SYNTAX_OPEN, NULL, out,
                                max);
}

int
tiku_syntax_spans_on(tiku_syntax_lang_t lang, const char *line,
                     tiku_syntax_state_t in,
                     tiku_syntax_state_t *out_state, tiku_span_t *out,
                     int max)
{
    spans_t s;

    if (out_state != NULL) {
        *out_state = TIKU_SYNTAX_OPEN;
    }
    if (line == NULL || out == NULL || max <= 0) {
        return 0;
    }
    spans_table(&s, out, max);
    if (lang == TIKU_SYNTAX_C) {
        return c_spans(&s, line, in, out_state);
    }
    return basic_spans(&s, lang, line);
}

void
tiku_syntax_runs(tiku_syntax_lang_t lang, const char *line,
                 tiku_syntax_state_t in, tiku_syntax_state_t *out_state,
                 tiku_syntax_run_fn fn, void *ctx)
{
    spans_t s;

    if (out_state != NULL) {
        *out_state = TIKU_SYNTAX_OPEN;
    }
    if (line == NULL) {
        return;
    }
    memset(&s, 0, sizeof s);
    s.stream = 1;
    s.fn = fn;
    s.ctx = ctx;
    if (lang == TIKU_SYNTAX_C) {
        (void)c_spans(&s, line, in, out_state);
    } else {
        (void)basic_spans(&s, lang, line);
    }
}

/**
 * @brief Tell one line of BASIC apart into @p sink.
 *
 * @return the number of runs written; 0 for a language other than BASIC
 */
static int
basic_spans(spans_t *sink, tiku_syntax_lang_t lang, const char *line)
{
    spans_t s = *sink;
    int i = 0;
    int data_tail = 0;          /* past DATA: items, not statements     */
    int label_next = 0;         /* past GOTO/GOSUB: a target, not a word */
    int comma_arms = 0;         /* a comma here brings another target    */
    int at_head = 1;            /* nothing but blanks seen yet          */
    int at_num = 1;             /* the line's own number may still come  */
    int at_stmt = 1;            /* a statement may begin here            */

    if (line == NULL || lang != TIKU_SYNTAX_BASIC) {
        return 0;
    }

    while (line[i] != '\0') {
        char c = line[i];
        int len;

        if (c == ' ' || c == '\t') {
            if (!push(&s, TIKU_INK_PLAIN, 1)) {
                break;
            }
            i++;
            continue;
        }
        if (at_num) {
            /* A number opening the line is the line's name, not a value,
             * so it takes the faint ink. */
            at_num = 0;
            len = is_digit((unsigned char)c) ? number_at(line + i) : 0;
            if (len > 0) {
                if (!push(&s, TIKU_INK_FAINT, len)) {
                    break;
                }
                i += len;
                continue;       /* a label may still open the body */
            }
        }
        if (at_head) {
            /* A label names the line as a number does, in plain ink.  It
             * may follow a line number, as in `10 loop: PRINT 1`, so the
             * number above does not close the head. */
            at_head = 0;
            len = label_at(line + i);
            if (len > 0) {
                if (!push(&s, TIKU_INK_PLAIN, len)) {
                    break;
                }
                i += len;
                continue;
            }
        }
        if (c == '"') {
            len = string_at(line + i);
            /* A quoted name that begins with a slash is a path in the
             * namespace: its body takes the path ink and the quotes stay
             * string ink. */
            if (len >= 2 && line[i + 1] == '/') {
                int closed = (line[i + len - 1] == '"');
                int body = closed ? len - 2 : len - 1;

                if (!push(&s, TIKU_INK_STR, 1)) {
                    break;
                }
                if (body > 0 && !push(&s, TIKU_INK_PATH, body)) {
                    break;
                }
                if (closed && !push(&s, TIKU_INK_STR, 1)) {
                    break;
                }
            } else if (!push(&s, TIKU_INK_STR, len)) {
                break;
            }
            at_stmt = 0;
            comma_arms = 0;
            i += len;
            continue;
        }
        if (data_tail) {
            /* After DATA the rest of the line is items the interpreter
             * never crunches, so a word there is plain.  Quoted items are
             * still strings, so the string test comes first. */
            if (!push(&s, TIKU_INK_PLAIN, 1)) {
                break;
            }
            i++;
            continue;
        }
        if (c == '\'' && at_stmt) {
            /* A remark to the end of the line, colons and all, but only
             * where a statement may begin: mid-statement the interpreter
             * refuses an apostrophe as trailing junk, so the tail is not
             * coloured as a remark there. */
            (void)push(&s, TIKU_INK_REMARK, (int)strlen(line + i));
            break;
        }
        if (c == '?') {
            /* The one reserved mark: `?` is PRINT spelled short. */
            if (!push(&s, TIKU_INK_WORD, 1)) {
                break;
            }
            at_stmt = 0;
            comma_arms = 0;
            i++;
            continue;
        }
        len = number_at(line + i);
        if (len > 0) {
            /* A number after GOTO or GOSUB is a line to go to: a link. */
            if (!push(&s, label_next ? TIKU_INK_LINK : TIKU_INK_NUM, len)) {
                break;
            }
            if (label_next) {
                /* `GOTO 100`: the target is spent; only a comma brings
                 * another. */
                label_next = 0;
                comma_arms = 1;
            } else {
                comma_arms = 0;
            }
            at_stmt = 0;
            i += len;
            continue;
        }
        len = word_at(line + i);
        if (len > 0) {
            char word[BASIC_WORD_MAX];
            int known = fold(line + i, len, word, sizeof word);
            int is_word;

            if (known && len > 1 && word[len - 1] == '$' &&
                !tiku_syntax_basic_word(word)) {
                /* The interpreter matches a keyword and then asks only
                 * whether the next byte could continue a word, which a `$`
                 * cannot: REM$ is REM and a `$`, and starts a remark.
                 * Only words the table spells with the sigil keep it. */
                word[len - 1] = '\0';
                len--;
            }
            is_word = !label_next && known && tiku_syntax_basic_word(word);

            if (is_word && at_stmt && strcmp(word, "REM") == 0) {
                /* Like the apostrophe: everything after it, colons and
                 * all, is a remark the interpreter steps over. */
                (void)push(&s, TIKU_INK_REMARK, (int)strlen(line + i));
                break;
            }
            if (!push(&s, label_next ? TIKU_INK_LINK :
                          is_word ? TIKU_INK_WORD : TIKU_INK_PLAIN, len)) {
                break;
            }
            if (label_next) {
                /* This word was the target.  Only a comma brings another,
                 * as in `GOTO l1, l2`, so the ELSE in
                 * `IF A THEN GOTO L1 ELSE ...` still reads as a keyword. */
                label_next = 0;
                comma_arms = 1;
            } else {
                comma_arms = 0;
                if (is_word) {
                    data_tail = (strcmp(word, "DATA") == 0);
                    /* A target may be a label, a name the interpreter
                     * reads raw and never folds, so it is not reserved. */
                    label_next = (strcmp(word, "GOTO") == 0 ||
                                  strcmp(word, "GOSUB") == 0);
                }
            }
            at_stmt = 0;
            i += len;
            continue;
        }
        if (c == ',') {
            if (comma_arms) {
                label_next = 1;
            }
            if (!push(&s, TIKU_INK_PLAIN, 1)) {
                break;
            }
            i++;
            continue;
        }
        comma_arms = 0;
        if (c == ':') {
            label_next = 0;     /* a new statement begins */
            at_stmt = 1;
        } else {
            at_stmt = 0;
        }
        if (!push(&s, TIKU_INK_PLAIN, 1)) {
            break;
        }
        i++;
    }
    spans_finish(&s);
    *sink = s;
    return s.n;
}
