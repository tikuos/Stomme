/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_circuit_gerber_macro.c - aperture macros.  An AM definition is
 * kept as text; an AD naming it evaluates the statements' variables and
 * arithmetic here and builds the primitives into a scene of its own.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_circuit_gerber_macro.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define MACRO_VARIABLES  1024       /* $1 to $1024                       */
#define MACRO_VAR_DIGITS 4          /* digits in a variable's number     */
#define MACRO_DEPTH_MAX  24         /* atoms open in one expression      */
#define MACRO_WORK_MAX   32768      /* atoms read in one instantiation   */
#define MACRO_VALUE_MAX  1e9        /* an intermediate result's magnitude */
#define MACRO_STATEMENTS 256        /* statements in one instantiation   */
#define MACRO_PARAMS     2055       /* values of one primitive, room for
                                       an outline of 1,024 vertices      */

/** @brief One instantiation's evaluator: where it reads, the variables,
 *         and the work spent so far. */
typedef struct {
    circuit_gerber_t *g;            /* the decoder, for failures         */
    const char *at;                 /* the next statement character      */
    double vars[MACRO_VARIABLES + 1];           /* $n by number; 0 unused */
    unsigned char defined[MACRO_VARIABLES + 1]; /* $n set by AD or $n=   */
    unsigned depth;                 /* atoms open in this expression     */
    unsigned work;                  /* atoms read so far                 */
} expression_t;

/** @brief Pass spaces and line breaks at the cursor. */
static void
spaces(expression_t *e)
{
    while (*e->at == ' ' || *e->at == '\n' || *e->at == '\r') {
        e->at++;
    }
}

/** @brief The number of the variable whose '$' is at the cursor, which
 *         it passes.  @return 1 to 1024, or 0 with the failure recorded. */
static unsigned
variable(expression_t *e)
{
    unsigned n = 0, digits = 0;

    e->at++;
    while (*e->at >= '0' && *e->at <= '9') {
        if (++digits > MACRO_VAR_DIGITS) {
            gerber_fail(e->g, TIKU_CIRCUIT_LIMIT,
                        "Macro variable exceeds $1024");
            return 0;
        }
        n = n * 10 + (*e->at++ - '0');
    }
    if (!n || n > MACRO_VARIABLES) {
        gerber_fail(e->g, TIKU_CIRCUIT_BAD, "Invalid macro variable");
        return 0;
    }
    return n;
}

static double expression(expression_t *e, int precedence);

/** @brief One operand at the cursor: a signed operand, a bracketed
 *         expression, a variable or a number.  @return its value. */
static double
atom(expression_t *e)
{
    spaces(e);
    if (++e->depth > MACRO_DEPTH_MAX || ++e->work > MACRO_WORK_MAX) {
        gerber_fail(e->g, TIKU_CIRCUIT_LIMIT,
                    "Macro expression budget exceeded");
        e->depth--;
        return 0;
    }
    double value = 0;

    if (*e->at == '+' || *e->at == '-') {
        int minus = *e->at++ == '-';

        value = atom(e);
        if (minus) {
            value = -value;
        }
    } else if (*e->at == '(') {
        e->at++;
        value = expression(e, 0);
        spaces(e);
        if (*e->at == ')') {
            e->at++;
        } else {
            gerber_fail(e->g, TIKU_CIRCUIT_BAD, "Unclosed macro expression");
        }
    } else if (*e->at == '$') {
        value = e->vars[variable(e)];
    } else {
        gerber_number(e->g, &e->at, &value, 0);
    }
    e->depth--;
    return value;
}

/** @brief The expression at the cursor, folding in the operators that
 *         bind tighter than @p precedence: + and - are 1, x and / are 2,
 *         so 0 takes them all.  @return its value. */
static double
expression(expression_t *e, int precedence)
{
    double value = atom(e);

    while (!e->g->error) {
        spaces(e);
        char op = *e->at;
        int level = op == '+' || op == '-' ? 1
                  : op == 'x' || op == '/' ? 2 : 0;

        if (!level || level <= precedence) {
            break;
        }
        e->at++;
        double right = expression(e, level);

        if (op == '/' && !right) {
            gerber_fail(e->g, TIKU_CIRCUIT_BAD, "Division by zero in macro");
            break;
        }
        value = op == '+' ? value + right
              : op == '-' ? value - right
              : op == 'x' ? value * right : value / right;
        if (!isfinite(value) || fabs(value) > MACRO_VALUE_MAX) {
            gerber_fail(e->g, TIKU_CIRCUIT_LIMIT,
                        "Macro arithmetic exceeds range");
        }
    }
    return value;
}

/** @brief Whether the @p n bytes at @p s make a macro name: letters,
 *         digits, '_', '.' and '$', and fewer than GERBER_MACRO_NAME. */
static int
name_ok(const char *s, size_t n)
{
    if (!n || n >= GERBER_MACRO_NAME) {
        return 0;
    }
    for (size_t i = 0; i < n; i++) {
        if (!((s[i] >= 'a' && s[i] <= 'z') || (s[i] >= 'A' && s[i] <= 'Z')
              || (s[i] >= '0' && s[i] <= '9') || s[i] == '_' || s[i] == '.'
              || s[i] == '$')) {
            return 0;
        }
    }
    return 1;
}

int
gerber_macro_define(circuit_gerber_t *g, const char *name, const char *body)
{
    if (!name_ok(name, strlen(name))) {
        return gerber_fail(g, TIKU_CIRCUIT_BAD, "Invalid macro name");
    }
    if (strlen(name) == 1 && strchr("CROP", *name)) {
        return gerber_fail(g, TIKU_CIRCUIT_BAD,
                           "Macro name is reserved for a standard aperture");
    }
    for (size_t i = 0; i < g->macro_count; i++) {
        if (!strcmp(name, g->macros[i].name)) {
            return gerber_fail(g, TIKU_CIRCUIT_BAD, "Duplicate macro name");
        }
    }
    size_t n = strlen(body) + 1;

    if (g->macro_count == GERBER_MACROS
        || n > GERBER_MACRO_BYTES - g->macro_bytes) {
        return gerber_fail(g, TIKU_CIRCUIT_LIMIT,
                           "Macro definition budget exceeded");
    }
    gerber_macro_t *m = &g->macros[g->macro_count];

    m->body = strdup(body);
    if (!m->body) {
        return gerber_fail(g, TIKU_CIRCUIT_NOMEM,
                           "Cannot retain macro definition");
    }
    strcpy(m->name, name);
    g->macro_count++;
    g->macro_bytes += n;
    return 1;
}

int
gerber_macro_aperture(circuit_gerber_t *g, gerber_aperture_t *a,
                      const char *s)
{
    size_t name = strcspn(s, ",");
    gerber_macro_t *m = NULL;

    if (!name_ok(s, name)) {
        return gerber_fail(g, TIKU_CIRCUIT_BAD,
                           "Invalid macro aperture name");
    }
    for (size_t i = 0; i < g->macro_count; i++) {
        if (strlen(g->macros[i].name) == name
            && !memcmp(s, g->macros[i].name, name)) {
            m = &g->macros[i];
        }
    }
    if (!m) {
        return gerber_fail(g, TIKU_CIRCUIT_BAD, "Undefined aperture macro");
    }
    expression_t *e = calloc(1, sizeof *e);

    if (!e) {
        return gerber_fail(g, TIKU_CIRCUIT_NOMEM,
                           "Cannot allocate macro variables");
    }
    e->g = g;
    s += name;
    unsigned variable_id = 1;

    /* The AD's values become $1, $2 and so on. */
    if (*s == ',') {
        s++;
        do {
            if (variable_id > MACRO_VARIABLES) {
                gerber_fail(g, TIKU_CIRCUIT_LIMIT,
                            "Too many macro parameters");
                break;
            }
            if (!gerber_number(g, &s, &e->vars[variable_id], 0)) {
                break;
            }
            e->defined[variable_id++] = 1;
            if (*s != 'X') {
                break;
            }
            s++;
        } while (1);
    }
    if (*s && !g->error) {
        gerber_fail(g, TIKU_CIRCUIT_BAD, "Invalid macro aperture parameters");
    }
    tiku_circuit_t *doc = circuit_new();

    if (!doc) {
        gerber_fail(g, TIKU_CIRCUIT_NOMEM, "Cannot allocate macro aperture");
    }
    e->at = m->body;
    unsigned primitives = 0;

    while (!g->error) {
        spaces(e);
        if (!*e->at) {
            break;
        }
        if (g->options && g->options->cancel
            && g->options->cancel(g->options->ctx)) {
            gerber_fail(g, TIKU_CIRCUIT_CANCELLED, "Macro cancelled");
            break;
        }
        if (++primitives > MACRO_STATEMENTS) {
            gerber_fail(g, TIKU_CIRCUIT_LIMIT,
                        "Macro exceeds 256 statements");
            break;
        }
        if (e->at[0] == '0' && e->at[1] == ' ') {
            while (*e->at && *e->at != '*') {
                e->at++;            /* primitive 0 is a comment */
            }
        } else if (*e->at == '$') {
            unsigned id = variable(e);

            spaces(e);
            if (*e->at != '=' || e->defined[id]) {
                gerber_fail(g, TIKU_CIRCUIT_BAD,
                            "Invalid or redefined macro variable");
                break;
            }
            e->at++;
            double value = expression(e, 0);

            e->vars[id] = value;
            e->defined[id] = 1;
        } else {
            double code;

            if (!gerber_number(g, &e->at, &code, 0)) {
                break;
            }
            double params[MACRO_PARAMS];
            size_t n = 0;

            while (*e->at == ',' && !g->error) {
                if (n == sizeof params / sizeof *params) {
                    gerber_fail(g, TIKU_CIRCUIT_LIMIT,
                                "Too many primitive parameters");
                    break;
                }
                e->at++;
                params[n++] = expression(e, 0);
                spaces(e);
            }
            if (code != floor(code)) {
                gerber_fail(g, TIKU_CIRCUIT_BAD,
                            "Primitive code must be an integer");
            }
            if (!g->error
                && !gerber_macro_primitive(g, doc, (int)code, params, n)) {
                break;
            }
        }
        spaces(e);
        if (*e->at != '*') {
            gerber_fail(g, TIKU_CIRCUIT_BAD, "Unfinished macro statement");
            break;
        }
        e->at++;
    }
    if (!g->error && doc->bytes > CIRCUIT_BYTES_MAX - g->aperture_bytes) {
        gerber_fail(g, TIKU_CIRCUIT_LIMIT, "Aperture library exceeds 16 MiB");
    }
    if (!g->error
        && (doc->bounds.x1 - doc->bounds.x0 > GERBER_APERTURE_MAX
            || doc->bounds.y1 - doc->bounds.y0 > GERBER_APERTURE_MAX)) {
        gerber_fail(g, TIKU_CIRCUIT_UNSUPPORTED,
                    "Macro aperture exceeds 1000 mm");
    }
    if (!g->error) {
        a->macro = doc;
        doc->coverage = 1;
        a->kind = 'M';
        strcpy(a->macro_name, m->name);
        a->w = doc->bounds.x1 - doc->bounds.x0;
        a->h = doc->bounds.y1 - doc->bounds.y0;
        g->aperture_bytes += doc->bytes;
        doc = NULL;
    }
    tiku_circuit_release(doc);
    free(e);
    return !g->error && gerber_attributes(g, g->tool, a->attributes);
}
