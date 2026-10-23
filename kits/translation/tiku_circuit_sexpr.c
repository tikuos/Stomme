/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_circuit_sexpr.c - KiCad's S-expressions parsed in one pass into a
 * flat array: strings unescaped and checked as strict UTF-8, numbers read
 * without the C locale, and nesting bounded so no reader recurses without
 * limit.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_circuit_sexpr.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CANCEL_BYTES 4096       /* bytes skipped between cancel checks   */
#define CANCEL_NODES 256        /* nodes made between cancel checks      */

/** @brief A parse under way. */
typedef struct {
    const unsigned char *data;      /* the document                      */
    size_t bytes;                   /* its length                        */
    size_t at;                      /* the next byte to read             */
    size_t used;                    /* bytes of tree.strings filled      */
    unsigned line, column;          /* where at is, 1-based              */
    circuit_sx_t tree;              /* the tree being built              */
    const tiku_circuit_import_t *options;   /* cancellation, or NULL     */
    tiku_circuit_diagnostic_t *diag;        /* the first failure, or NULL */
    tiku_circuit_err_t error;       /* the failure, OK until one         */
} parser_t;

/** @brief Note a failure; the diagnostic keeps the first, with the
 *         position the parse had reached.  @return 0 */
static int
fail(parser_t *p, tiku_circuit_err_t code, const char *message)
{
    if (!p->error && p->diag) {
        p->diag->code = code;
        p->diag->line = p->line;
        p->diag->column = p->column;
        snprintf(p->diag->message, sizeof p->diag->message, "%s", message);
    }
    p->error = code;
    return 0;
}

/** @brief Consume a source byte, keeping the line and column. */
static unsigned char
take(parser_t *p)
{
    unsigned char c = p->data[p->at++];

    if (c == '\n') {
        p->line++;
        p->column = 1;
    } else {
        p->column++;
    }
    return c;
}

/** @brief Skip ASCII whitespace and comments from ';' to the line's end.
 *  @return 1, or 0 when cancelled */
static int
space(parser_t *p)
{
    while (p->at < p->bytes) {
        if ((p->at & (CANCEL_BYTES - 1)) == 0 && p->options &&
            p->options->cancel && p->options->cancel(p->options->ctx)) {
            return fail(p, TIKU_CIRCUIT_CANCELLED, "Import cancelled");
        }
        unsigned char c = p->data[p->at];

        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            take(p);
        } else if (c == ';') {
            while (p->at < p->bytes && p->data[p->at] != '\n') {
                take(p);
                if ((p->at & (CANCEL_BYTES - 1)) == 0 && p->options &&
                    p->options->cancel &&
                    p->options->cancel(p->options->ctx)) {
                    return fail(p, TIKU_CIRCUIT_CANCELLED,
                                "Import cancelled");
                }
            }
        } else {
            break;
        }
    }
    return 1;
}

/** @brief Whether @p s is strict UTF-8: no overlong forms or surrogates,
 *         and no character below space but tab and newline. */
static int
utf8(const unsigned char *s)
{
    while (*s) {
        unsigned cp = *s++, n = 0, min = 0;

        if (cp < 128) {
            if (cp < 32 && cp != '\n' && cp != '\t') {
                return 0;
            }
            continue;
        }
        if (cp >= 0xc2 && cp <= 0xdf) {
            cp &= 31;
            n = 1;
            min = 128;
        } else if (cp >= 0xe0 && cp <= 0xef) {
            cp &= 15;
            n = 2;
            min = 2048;
        } else if (cp >= 0xf0 && cp <= 0xf4) {
            cp &= 7;
            n = 3;
            min = 65536;
        } else {
            return 0;
        }
        for (unsigned i = 0; i < n; i++) {
            if ((*s & 0xc0) != 0x80) {
                return 0;
            }
            cp = cp * 64 + (*s++ & 63);
        }
        if (cp < min || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) {
            return 0;
        }
    }
    return 1;
}

/** @brief A new node at the array's end, the array doubled as needed
 *         within the node budget.  @return its index, or SX_NONE */
static size_t
node(parser_t *p)
{
    if (p->tree.count == CIRCUIT_TOKEN_MAX) {
        fail(p, TIKU_CIRCUIT_LIMIT, "Too many schematic tokens");
        return SX_NONE;
    }
    if (p->tree.count == p->tree.capacity) {
        size_t cap = p->tree.capacity ? p->tree.capacity * 2
                                      : CIRCUIT_NODES_FIRST;
        void *next = realloc(p->tree.node, cap * sizeof *p->tree.node);

        if (!next) {
            fail(p, TIKU_CIRCUIT_NOMEM, "Token allocation failed");
            return SX_NONE;
        }
        p->tree.node = next;
        p->tree.capacity = cap;
    }
    size_t at = p->tree.count++;

    p->tree.node[at] = (circuit_sx_node_t){.line = p->line,
                                           .column = p->column};
    return at;
}

/** @brief Parse one expression at nesting @p depth.  Nodes are kept by
 *         index, which stays valid as the array grows.  @return 1 or 0 */
static int
expression(parser_t *p, unsigned depth)
{
    if ((p->tree.count & (CANCEL_NODES - 1)) == 0 && p->options &&
        p->options->cancel && p->options->cancel(p->options->ctx)) {
        return fail(p, TIKU_CIRCUIT_CANCELLED, "Import cancelled");
    }
    if (!space(p)) {
        return 0;
    }
    if (depth > CIRCUIT_NEST_MAX) {
        return fail(p, TIKU_CIRCUIT_LIMIT,
                    "Schematic nesting exceeds 128 levels");
    }
    if (p->at == p->bytes || p->data[p->at] == ')') {
        return fail(p, TIKU_CIRCUIT_BAD, "Expected an expression");
    }
    size_t id = node(p);

    if (id == SX_NONE) {
        return 0;
    }
    if (take(p) == '(') {
        if (!space(p)) {
            return 0;
        }
        while (p->at < p->bytes && p->data[p->at] != ')') {
            if (!expression(p, depth + 1) || !space(p)) {
                return 0;
            }
        }
        if (p->at == p->bytes) {
            return fail(p, TIKU_CIRCUIT_BAD, "Unclosed expression");
        }
        take(p);
        if (p->tree.count == id + 1 || !p->tree.node[id + 1].text ||
            p->tree.node[id + 1].quoted) {
            return fail(p, TIKU_CIRCUIT_BAD,
                        "A list must start with a name");
        }
    } else {
        unsigned char first = p->data[p->at - 1];
        size_t start = p->used;
        int quoted = first == '"', closed = !quoted;

        if (!quoted) {
            p->tree.strings[p->used++] = (char)first;
        }
        while (p->at < p->bytes) {
            unsigned char c = p->data[p->at];

            if (!quoted && (c == '(' || c == ')' || c == ';' || c == ' ' ||
                            c == '\t' || c == '\r' || c == '\n')) {
                break;
            }
            take(p);
            if (quoted && c == '"') {
                closed = 1;
                break;
            }
            if (!quoted && c == '"') {
                return fail(p, TIKU_CIRCUIT_BAD, "Quote inside an atom");
            }
            if (c == '\\' && quoted) {
                if (p->at == p->bytes) {
                    break;
                }
                c = take(p);
                if (c == 'n') {
                    c = '\n';
                } else if (c == 't') {
                    c = '\t';
                } else if (c == 'r') {
                    c = '\n';
                    if (p->bytes - p->at >= 2 &&
                        p->data[p->at] == '\\' && p->data[p->at + 1] == 'n') {
                        take(p);
                        take(p);
                    }
                } else if (c != '\\' && c != '"') {
                    return fail(p, TIKU_CIRCUIT_BAD,
                                "Unknown string escape");
                }
            }
            if (!c) {
                return fail(p, TIKU_CIRCUIT_BAD, "NUL byte in schematic");
            }
            if (p->used - start >= CIRCUIT_TOKEN_BYTES) {
                return fail(p, TIKU_CIRCUIT_LIMIT,
                            "Schematic token exceeds 4096 bytes");
            }
            p->tree.strings[p->used++] = (char)c;
        }
        if (!closed) {
            return fail(p, TIKU_CIRCUIT_BAD, "Unclosed string");
        }
        p->tree.strings[p->used++] = 0;
        if (!first || !utf8((unsigned char *)p->tree.strings + start)) {
            return fail(p, TIKU_CIRCUIT_BAD,
                        "Invalid UTF-8 or control character");
        }
        p->tree.node[id].text = p->tree.strings + start;
        p->tree.node[id].quoted = quoted;
    }
    p->tree.node[id].end = p->tree.count;
    return 1;
}

void
circuit_sx_free(circuit_sx_t *tree)
{
    free(tree->node);
    free(tree->strings);
    memset(tree, 0, sizeof *tree);
}

tiku_circuit_err_t
circuit_sx_parse(const void *data, size_t bytes,
                 const tiku_circuit_import_t *options, circuit_sx_t *out,
                 tiku_circuit_diagnostic_t *diag)
{
    if (diag) {
        memset(diag, 0, sizeof *diag);
    }
    parser_t p = {.data = data, .bytes = bytes, .line = 1, .column = 1,
                  .options = options, .diag = diag};

    if (!out || !data || !bytes) {
        fail(&p, TIKU_CIRCUIT_BAD, "Empty schematic");
    } else if (bytes > CIRCUIT_INPUT_MAX) {
        fail(&p, TIKU_CIRCUIT_LIMIT, "Schematic exceeds 8 MiB");
    } else {
        p.tree.strings = malloc(bytes + 1);
        if (!p.tree.strings) {
            fail(&p, TIKU_CIRCUIT_NOMEM, "Token allocation failed");
        } else if (expression(&p, 0) && space(&p) && p.at != bytes) {
            fail(&p, TIKU_CIRCUIT_BAD, "Data follows the root expression");
        }
    }
    if (p.error) {
        circuit_sx_free(&p.tree);
        return p.error;
    }
    *out = p.tree;
    return TIKU_CIRCUIT_OK;
}

const char *
circuit_sx_head(const circuit_sx_t *t, size_t list)
{
    return list < t->count && !t->node[list].text &&
           list + 1 < t->node[list].end ? t->node[list + 1].text : NULL;
}

size_t
circuit_sx_child(const circuit_sx_t *t, size_t list, const char *name)
{
    if (list >= t->count || t->node[list].text) {
        return SX_NONE;
    }
    for (size_t n = list + 1; n < t->node[list].end; n = t->node[n].end) {
        const char *head = circuit_sx_head(t, n);

        if (head && !strcmp(head, name)) {
            return n;
        }
    }
    return SX_NONE;
}

size_t
circuit_sx_arg(const circuit_sx_t *t, size_t list, size_t index)
{
    if (!circuit_sx_head(t, list)) {
        return SX_NONE;
    }
    size_t n = list + 2;

    while (index-- && n < t->node[list].end) {
        n = t->node[n].end;
    }
    return n < t->node[list].end && t->node[n].text ? n : SX_NONE;
}

int
circuit_sx_number(const circuit_sx_t *t, size_t n, double *out)
{
    if (n >= t->count || !t->node[n].text || t->node[n].quoted) {
        return 0;
    }
    const char *s = t->node[n].text;
    int negative = *s == '-', digits = 0;

    if (*s == '-' || *s == '+') {
        s++;
    }
    double value = 0, step = .1;

    while (*s >= '0' && *s <= '9') {
        value = value * 10 + (*s++ - '0');
        digits++;
    }
    if (*s == '.') {
        s++;
        while (*s >= '0' && *s <= '9') {
            value += (*s++ - '0') * step;
            step *= .1;
            digits++;
        }
    }
    if (!digits || *s || !isfinite(value)) {
        return 0;
    }
    *out = negative ? -value : value;
    return 1;
}
