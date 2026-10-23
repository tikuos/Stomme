/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_circuit_job_json.c - a Gerber job's JSON: the whole grammar is
 * checked, within bounds on depth, string length and work, and the
 * FilesAttributes entries are kept.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_circuit_job_private.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define JSON_INPUT_MAX  (1024u * 1024u)     /* bytes of one job          */
#define JSON_DEPTH_MAX  24          /* nested values                     */
#define JSON_VALUES_MAX 32768       /* values in one job                 */
#define JSON_MEMBERS    32          /* members of one object             */
#define JSON_STRING     4096        /* a string or key and its NUL       */
#define JSON_NUMBER_MAX 128         /* a number's bytes                  */

/* What a value is in the job: anything, the job itself, its file list,
 * or one entry of the list. */
enum { SCOPE_ANY, SCOPE_JOB, SCOPE_FILES, SCOPE_ENTRY };

/** @brief The reader's place in one job's bytes. */
typedef struct {
    const unsigned char *s;         /* the job's bytes                   */
    size_t n;                       /* how many                          */
    size_t at;                      /* the next one to read              */
    size_t steps;                   /* values read, against the budget   */
    unsigned line;                  /* line of the next byte             */
    const tiku_circuit_import_t *options;   /* cancellation; may be NULL */
    circuit_job_list_t *list;       /* the entries kept                  */
    tiku_circuit_diagnostic_t *diag;        /* the caller's; may be NULL */
    tiku_circuit_err_t error;       /* the first failure; 0 while none   */
    int found;                      /* FilesAttributes has been read     */
    char *keys;                     /* the open objects' member names,
                                       each with its NUL, innermost last */
    size_t keys_used;               /* bytes of keys in use              */
    size_t keys_size;               /* bytes of keys allocated           */
} json_t;

/** @brief Record the first failure @p e, with message @p s, in @p j and
 *         the caller's diagnostic.  @return 0. */
static int
fail(json_t *j, tiku_circuit_err_t e, const char *s)
{
    if (!j->error) {
        j->error = e;
        if (j->diag) {
            j->diag->code = e;
            j->diag->line = j->line;
            j->diag->column = 1;
            snprintf(j->diag->message, sizeof j->diag->message, "%s", s);
        }
    }
    return 0;
}

/** @brief Pass white space, counting lines. */
static void
space(json_t *j)
{
    while (j->at < j->n && j->s[j->at] && strchr(" \t\r\n", j->s[j->at])) {
        if (j->s[j->at] == '\n') {
            j->line++;
        }
        j->at++;
    }
}

/** @brief Pass white space, then @p c if it is next.
 *  @return 1 when it was. */
static int
take(json_t *j, int c)
{
    space(j);
    if (j->at < j->n && j->s[j->at] == c) {
        j->at++;
        return 1;
    }
    return 0;
}

/** @brief Four hex digits at the cursor into @p out.
 *  @return 1, or 0 when they are not there. */
static int
hex4(json_t *j, unsigned *out)
{
    unsigned n = 0;

    for (int i = 0; i < 4; i++) {
        if (j->at == j->n) {
            return 0;
        }
        unsigned c = j->s[j->at++];
        unsigned d = c >= '0' && c <= '9' ? c - '0'
                   : c >= 'a' && c <= 'f' ? c - 'a' + 10
                   : c >= 'A' && c <= 'F' ? c - 'A' + 10 : 16;

        if (d == 16) {
            return 0;
        }
        n = n * 16 + d;
    }
    *out = n;
    return 1;
}

/** @brief The string at the cursor, its escapes decoded, as UTF-8 into
 *         @p out of @p capacity bytes with its NUL.  NUL itself, broken
 *         UTF-8 and lone surrogates fail.  @return 1, or 0 on failure. */
static int
string(json_t *j, char *out, size_t capacity)
{
    if (!take(j, '"')) {
        return fail(j, TIKU_CIRCUIT_BAD, "Expected JSON string");
    }
    size_t used = 0;

    while (j->at < j->n) {
        unsigned c = j->s[j->at++];

        if (c == '"') {
            out[used] = 0;
            return 1;
        }
        if (c < 32) {
            return fail(j, TIKU_CIRCUIT_BAD,
                        "Control character in JSON string");
        }
        if (c == '\\') {
            if (j->at == j->n) {
                break;
            }
            c = j->s[j->at++];
            if (c == 'u') {
                if (!hex4(j, &c)) {
                    break;
                }
                if (c >= 0xd800 && c <= 0xdbff) {
                    /* A high surrogate takes the low one after it. */
                    unsigned low;

                    if (j->n - j->at < 2 || j->s[j->at++] != '\\'
                        || j->s[j->at++] != 'u' || !hex4(j, &low)
                        || low < 0xdc00 || low > 0xdfff) {
                        break;
                    }
                    c = 0x10000 + ((c - 0xd800) << 10) + (low - 0xdc00);
                } else if (c >= 0xdc00 && c <= 0xdfff) {
                    break;
                }
            } else if (c == 'n') {
                c = '\n';
            } else if (c == 'r') {
                c = '\r';
            } else if (c == 't') {
                c = '\t';
            } else if (c == 'b') {
                c = '\b';
            } else if (c == 'f') {
                c = '\f';
            } else if (c != '"' && c != '\\' && c != '/') {
                break;
            }
        } else if (c >= 128) {
            int extra = c >= 0xc2 && c <= 0xdf ? 1
                      : c >= 0xe0 && c <= 0xef ? 2
                      : c >= 0xf0 && c <= 0xf4 ? 3 : -1;

            if (extra < 0) {
                break;
            }
            unsigned minimum = extra == 1 ? 0x80
                             : extra == 2 ? 0x800 : 0x10000;

            c &= (1u << (6 - extra)) - 1;
            for (int i = 0; i < extra; i++) {
                if (j->at == j->n || (j->s[j->at] & 0xc0) != 0x80) {
                    return fail(j, TIKU_CIRCUIT_BAD, "Invalid UTF-8 in job");
                }
                c = (c << 6) | (j->s[j->at++] & 63);
            }
            if (c < minimum || c > 0x10ffff || (c >= 0xd800 && c <= 0xdfff)) {
                break;
            }
        }
        if (!c) {
            return fail(j, TIKU_CIRCUIT_BAD,
                        "NUL is not accepted in job strings");
        }
        unsigned char encoded[4];
        size_t n;

        if (c < 128) {
            encoded[0] = (unsigned char)c;
            n = 1;
        } else if (c < 0x800) {
            encoded[0] = 0xc0 | (c >> 6);
            encoded[1] = 0x80 | (c & 63);
            n = 2;
        } else if (c < 0x10000) {
            encoded[0] = 0xe0 | (c >> 12);
            encoded[1] = 0x80 | ((c >> 6) & 63);
            encoded[2] = 0x80 | (c & 63);
            n = 3;
        } else {
            encoded[0] = 0xf0 | (c >> 18);
            encoded[1] = 0x80 | ((c >> 12) & 63);
            encoded[2] = 0x80 | ((c >> 6) & 63);
            encoded[3] = 0x80 | (c & 63);
            n = 4;
        }
        if (n >= capacity - used) {
            return fail(j, TIKU_CIRCUIT_LIMIT,
                        "JSON string exceeds field limit");
        }
        memcpy(out + used, encoded, n);
        used += n;
    }
    return fail(j, TIKU_CIRCUIT_BAD, "Unfinished or invalid JSON string");
}

static int value(json_t *, unsigned, int, circuit_job_entry_t *);

/** @brief The members of an object whose '{' has been read, at @p depth
 *         in @p scope: an entry's four fields are kept in @p entry, the
 *         job's FilesAttributes is read as the file list, and the rest
 *         is checked and passed.  A name given twice fails.
 *  @return 1, or 0 on failure. */
static int
object(json_t *j, unsigned depth, int scope, circuit_job_entry_t *entry)
{
    size_t base = j->keys_used, count = 0;
    int path = 0;

    if (take(j, '}')) {
        return scope == SCOPE_ENTRY
               ? fail(j, TIKU_CIRCUIT_BAD, "Job entry needs Path") : 1;
    }
    do {
        if (count == JSON_MEMBERS) {
            return fail(j, TIKU_CIRCUIT_LIMIT,
                        "Too many JSON object members");
        }
        char *key = j->keys + j->keys_used;
        size_t room = j->keys_size - j->keys_used;

        if (!string(j, key, room < JSON_STRING ? room : JSON_STRING)) {
            return 0;
        }
        for (const char *k = j->keys + base; k < key; k += strlen(k) + 1) {
            if (!strcmp(k, key)) {
                return fail(j, TIKU_CIRCUIT_BAD, "Duplicate JSON key");
            }
        }
        j->keys_used += strlen(key) + 1;
        count++;
        if (!take(j, ':')) {
            return fail(j, TIKU_CIRCUIT_BAD,
                        "Expected colon in JSON object");
        }
        if (scope == SCOPE_JOB && !strcmp(key, "FilesAttributes")) {
            j->found = 1;
            if (!value(j, depth + 1, SCOPE_FILES, NULL)) {
                return 0;
            }
        } else if (scope == SCOPE_ENTRY
                   && (!strcmp(key, "Path") || !strcmp(key, "FileFunction")
                       || !strcmp(key, "FilePolarity")
                       || !strcmp(key, "FileFormat"))) {
            char *out;
            size_t cap;

            if (!strcmp(key, "Path")) {
                out = entry->path;
                cap = sizeof entry->path;
                path = 1;
            } else if (!strcmp(key, "FileFunction")) {
                out = entry->function;
                cap = sizeof entry->function;
            } else if (!strcmp(key, "FilePolarity")) {
                out = entry->polarity;
                cap = sizeof entry->polarity;
            } else {
                out = entry->format;
                cap = sizeof entry->format;
            }
            if (!string(j, out, cap)) {
                return 0;
            }
            for (const unsigned char *p = (void *)out; *p; p++) {
                if (*p < 32 || *p == 127) {
                    return fail(j, TIKU_CIRCUIT_BAD,
                                "Control character in job file entry");
                }
            }
        } else if (!value(j, depth + 1, SCOPE_ANY, NULL)) {
            return 0;
        }
    } while (take(j, ','));
    if (!take(j, '}')) {
        return fail(j, TIKU_CIRCUIT_BAD, "Expected closing JSON object");
    }
    if (scope == SCOPE_ENTRY && (!path || !entry->path[0])) {
        return fail(j, TIKU_CIRCUIT_BAD, "Job entry needs a nonempty Path");
    }
    j->keys_used = base;
    return 1;
}

/** @brief One value at @p depth in @p scope; in the file list each
 *         element is a new entry of the list.  @return 1, or 0 on
 *         failure. */
static int
value(json_t *j, unsigned depth, int scope, circuit_job_entry_t *entry)
{
    if (depth > JSON_DEPTH_MAX || ++j->steps > JSON_VALUES_MAX) {
        return fail(j, TIKU_CIRCUIT_LIMIT,
                    "Job JSON depth/work limit exceeded");
    }
    if (j->options && j->options->cancel
        && j->options->cancel(j->options->ctx)) {
        return fail(j, TIKU_CIRCUIT_CANCELLED, "Job import cancelled");
    }
    space(j);
    if (j->at == j->n) {
        return fail(j, TIKU_CIRCUIT_BAD, "Missing JSON value");
    }
    if ((scope == SCOPE_JOB || scope == SCOPE_ENTRY) && j->s[j->at] != '{') {
        return fail(j, TIKU_CIRCUIT_BAD, "Expected job object");
    }
    if (scope == SCOPE_FILES && j->s[j->at] != '[') {
        return fail(j, TIKU_CIRCUIT_BAD, "FilesAttributes must be an array");
    }
    if (take(j, '{')) {
        return object(j, depth, scope, entry);
    }
    if (take(j, '[')) {
        if (take(j, ']')) {
            return 1;
        }
        do {
            circuit_job_entry_t *next = NULL;

            if (scope == SCOPE_FILES) {
                if (j->list->count == TIKU_CIRCUIT_LAYERS_MAX) {
                    return fail(j, TIKU_CIRCUIT_LIMIT,
                                "Job exceeds 32 files");
                }
                next = &j->list->entries[j->list->count++];
            }
            if (!value(j, depth + 1,
                       scope == SCOPE_FILES ? SCOPE_ENTRY : SCOPE_ANY,
                       next)) {
                return 0;
            }
        } while (take(j, ','));
        return take(j, ']') ? 1 : fail(j, TIKU_CIRCUIT_BAD,
                                       "Expected closing JSON array");
    }
    if (j->s[j->at] == '"') {
        char text[JSON_STRING];

        return string(j, text, sizeof text);
    }
    const char *literal = j->s[j->at] == 't' ? "true"
                        : j->s[j->at] == 'f' ? "false"
                        : j->s[j->at] == 'n' ? "null" : NULL;

    if (literal) {
        size_t n = strlen(literal);

        if (n > j->n - j->at || memcmp(j->s + j->at, literal, n)) {
            return fail(j, TIKU_CIRCUIT_BAD, "Invalid JSON literal");
        }
        j->at += n;
        return 1;
    }
    /* A number: an optional minus, an integer without leading zeros, an
     * optional fraction and an optional exponent. */
    size_t start = j->at;

    if (j->s[j->at] == '-') {
        j->at++;
    }
    if (j->at == j->n || j->s[j->at] < '0' || j->s[j->at] > '9') {
        return fail(j, TIKU_CIRCUIT_BAD, "Invalid JSON number");
    }
    if (j->s[j->at++] != '0') {
        while (j->at < j->n && j->s[j->at] >= '0' && j->s[j->at] <= '9') {
            j->at++;
        }
    }
    if (j->at < j->n && j->s[j->at] == '.') {
        size_t begin = ++j->at;

        while (j->at < j->n && j->s[j->at] >= '0' && j->s[j->at] <= '9') {
            j->at++;
        }
        if (j->at == begin) {
            return fail(j, TIKU_CIRCUIT_BAD, "Empty JSON fraction");
        }
    }
    if (j->at < j->n && (j->s[j->at] == 'e' || j->s[j->at] == 'E')) {
        j->at++;
        if (j->at < j->n && (j->s[j->at] == '+' || j->s[j->at] == '-')) {
            j->at++;
        }
        size_t begin = j->at;

        while (j->at < j->n && j->s[j->at] >= '0' && j->s[j->at] <= '9') {
            j->at++;
        }
        if (j->at == begin) {
            return fail(j, TIKU_CIRCUIT_BAD, "Empty JSON exponent");
        }
    }
    return j->at - start <= JSON_NUMBER_MAX
           ? 1 : fail(j, TIKU_CIRCUIT_LIMIT, "JSON number exceeds 128 bytes");
}

tiku_circuit_err_t
circuit_job_parse(const void *data, size_t bytes,
                  const tiku_circuit_import_t *options,
                  circuit_job_list_t *list, tiku_circuit_diagnostic_t *diag)
{
    json_t j = {.s = data, .n = bytes, .line = 1, .options = options,
                .list = list, .diag = diag};

    if (diag) {
        memset(diag, 0, sizeof *diag);
    }
    memset(list, 0, sizeof *list);
    if (!data || !bytes) {
        fail(&j, TIKU_CIRCUIT_BAD, "Empty job file");
    } else if (bytes > JSON_INPUT_MAX) {
        fail(&j, TIKU_CIRCUIT_LIMIT, "Job metadata exceeds 1 MiB");
    } else if (!(j.keys = malloc(bytes + 1))) {
        fail(&j, TIKU_CIRCUIT_NOMEM, "Cannot allocate job keys");
    } else {
        /* A key and its NUL take no more bytes than its quoted form, so
         * every key of the job fits in as many bytes as the job. */
        j.keys_size = bytes + 1;
        value(&j, 0, SCOPE_JOB, NULL);
        space(&j);
        if (!j.error && (j.at != j.n || !j.found || !list->count)) {
            fail(&j, TIKU_CIRCUIT_BAD,
                 "Job needs a nonempty FilesAttributes array and no "
                 "trailing data");
        }
    }
    free(j.keys);
    return j.error;
}
