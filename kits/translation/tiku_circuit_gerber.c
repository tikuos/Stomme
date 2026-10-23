/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_circuit_gerber.c - one Gerber artwork layer read command by
 * command: the token stream, numbers, parameters and operations.  A
 * command or parameter outside the implemented subset fails the import.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_circuit_gerber_private.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GERBER_INPUT_MAX     (8u * 1024u * 1024u)  /* bytes of one file  */
#define GERBER_COMMANDS_MAX  262144     /* commands in one file          */
#define GERBER_COMMAND_BYTES 1024       /* one command's text            */
#define GERBER_DIGITS_MAX    16         /* digits in a decimal parameter */
#define GERBER_FRACTION_MAX  8          /* of them after the point       */
#define GERBER_NUMBER_MAX    10000      /* a coordinate in mm, or a
                                           parameter in file units       */
#define MM_PER_INCH          25.4

int
gerber_fail(circuit_gerber_t *g, tiku_circuit_err_t e, const char *message)
{
    if (!g->error) {
        g->error = e;
        if (g->diag) {
            g->diag->code = e;
            g->diag->line = g->line;
            g->diag->column = 1;
            snprintf(g->diag->message, sizeof g->diag->message, "%s",
                     message);
        }
    }
    return 0;
}

int
gerber_number(circuit_gerber_t *g, const char **at, double *out,
              int coordinate)
{
    const char *p = *at;
    int sign = 1, digits = 0, point = 0, fraction = 0;
    double n = 0, scale = 1;

    if (*p == '+' || *p == '-') {
        if (*p == '-') {
            sign = -1;
        }
        p++;
    }
    while ((*p >= '0' && *p <= '9') || (!coordinate && *p == '.')) {
        if (*p == '.') {
            if (point++) {
                return gerber_fail(g, TIKU_CIRCUIT_BAD,
                                   "Repeated decimal point");
            }
            p++;
            continue;
        }
        if (++digits > (coordinate ? g->integers + g->decimals
                                   : GERBER_DIGITS_MAX)
            || (point && ++fraction > GERBER_FRACTION_MAX)) {
            return gerber_fail(g, TIKU_CIRCUIT_LIMIT,
                               "Gerber number exceeds declared precision");
        }
        n = n * 10 + (*p++ - '0');
        if (point) {
            scale *= 10;
        }
    }
    if (!digits) {
        return gerber_fail(g, TIKU_CIRCUIT_BAD, "Expected a Gerber number");
    }
    if (coordinate) {
        scale = pow(10, g->decimals);
    }
    *out = sign * n / scale;
    if (coordinate) {
        *out *= g->unit;
    }
    if (!isfinite(*out) || fabs(*out) > GERBER_NUMBER_MAX) {
        return gerber_fail(g, TIKU_CIRCUIT_LIMIT,
                           "Gerber number exceeds supported range");
    }
    *at = p;
    return 1;
}

/** @brief A parameter command @p s, the text between '%' and its '*':
 *         format, units, apertures, polarity, repeats, attributes and
 *         the identity transforms.  @return 1, or 0 on failure. */
static int
extended(circuit_gerber_t *g, const char *s)
{
    if (g->region) {
        return gerber_fail(g, TIKU_CIRCUIT_BAD,
                           "Parameter change inside a region");
    }
    if (*s == 'T') {
        return gerber_attribute(g, s);
    }
    if (!strncmp(s, "FS", 2)) {
        if (g->integers || g->have_point) {
            return gerber_fail(g, TIKU_CIRCUIT_BAD,
                               "Repeated or late coordinate format");
        }
        if (strlen(s) != 10 || strncmp(s, "FSLAX", 5) || s[7] != 'Y'
            || s[5] != s[8] || s[6] != s[9]
            || s[5] < '1' || s[5] > '6' || s[6] < '1' || s[6] > '6') {
            return gerber_fail(g, TIKU_CIRCUIT_UNSUPPORTED,
                               "Use absolute, leading-zero-omitted, "
                               "matching X/Y formats (1 to 6 digits each)");
        }
        g->integers = s[5] - '0';
        g->decimals = s[6] - '0';
        return 1;
    }
    if (!strcmp(s, "MOMM") || !strcmp(s, "MOIN")) {
        if (g->unit || g->have_point) {
            return gerber_fail(g, TIKU_CIRCUIT_BAD,
                               "Repeated or late Gerber units");
        }
        g->unit = !strcmp(s, "MOMM") ? 1 : MM_PER_INCH;
        return 1;
    }
    if (!strncmp(s, "ADD", 3)) {
        return gerber_aperture(g, s + 3);
    }
    if (!strncmp(s, "SR", 2)) {
        return gerber_repeat(g, s + 2);
    }
    if (!strcmp(s, "LPD") || !strcmp(s, "LPC")) {
        g->clear = s[2] == 'C';
        return 1;
    }
    if (!strcmp(s, "LMN")) {
        return 1;
    }
    if (!strncmp(s, "LR", 2) || !strncmp(s, "LS", 2)) {
        const char *p = s + 2;
        double n;

        if (!gerber_number(g, &p, &n, 0)) {
            return 0;
        }
        if (!*p && n == (s[1] == 'R' ? 0 : 1)) {
            return 1;               /* LR0 and LS1 change nothing */
        }
    }
    return gerber_fail(g, TIKU_CIRCUIT_UNSUPPORTED,
                       "Unsupported Gerber parameter (nonidentity aperture "
                       "transforms and aperture blocks are not implemented)");
}

/** @brief A word command @p s: a comment, M02, a region bracket, a mode,
 *         an aperture selection, or coordinates with an operation.
 *  @return 1, or 0 with the failure recorded. */
static int
command(circuit_gerber_t *g, const char *s)
{
    if (!strncmp(s, "G04", 3)) {
        if (g->region) {
            return gerber_fail(g, TIKU_CIRCUIT_BAD,
                               "Comment inside a region");
        }
        s += 3;
        while (*s == ' ') {
            s++;
        }
        if (!strncmp(s, "#@!", 3)) {
            s += 3;
            while (*s == ' ') {
                s++;
            }
            return gerber_attribute(g, s);
        }
        return 1;
    }
    if (!strcmp(s, "M02")) {
        if (g->region || g->repeat_depth || !g->unit || !g->integers) {
            return gerber_fail(g, TIKU_CIRCUIT_BAD,
                               "Unfinished region/repeat or missing "
                               "units/format");
        }
        g->ended = 1;
        return 1;
    }
    if (!strcmp(s, "G36")) {
        if (g->region || !g->unit || !g->integers) {
            return gerber_fail(g, TIKU_CIRCUIT_BAD, "Invalid region start");
        }
        g->region = 1;
        g->count = 0;
        return 1;
    }
    if (!strcmp(s, "G37")) {
        return gerber_region(g, 0, 0, 0);
    }
    if (!strcmp(s, "G75")) {
        g->quadrants = 1;
        return 1;
    }
    if (!strcmp(s, "G90") && !g->region) {
        return 1;
    }
    if (!strncmp(s, "G01", 3) || !strncmp(s, "G02", 3)
        || !strncmp(s, "G03", 3)) {
        g->mode = s[2] - '0';
        s += 3;
        if (!*s) {
            return 1;
        }
    }
    if (*s == 'D' && s[1] >= '0' && s[1] <= '9') {
        char *end;
        long a = strtol(s + 1, &end, 10);

        if (a >= GERBER_APERTURE_FIRST && !*end) {
            if (g->region || a >= GERBER_APERTURES
                || !g->apertures[a].kind) {
                return gerber_fail(g, TIKU_CIRCUIT_BAD,
                                   "Undefined aperture or aperture "
                                   "selection inside a region");
            }
            g->aperture = (int)a;
            return 1;
        }
    }
    if (!g->unit || !g->integers) {
        return gerber_fail(g, TIKU_CIRCUIT_BAD,
                           "Coordinates require declared units and format");
    }
    double x = g->x, y = g->y, i = 0, j = 0;
    int axes = 0, offsets = 0, op = g->operation;   /* axes: X 1, Y 2 */

    if (*s == 'X') {
        s++;
        if (!gerber_number(g, &s, &x, 1)) {
            return 0;
        }
        axes |= 1;
    }
    if (*s == 'Y') {
        s++;
        if (!gerber_number(g, &s, &y, 1)) {
            return 0;
        }
        axes |= 2;
    }
    if (*s == 'I') {
        s++;
        if (!gerber_number(g, &s, &i, 1)) {
            return 0;
        }
        offsets = 1;
    }
    if (*s == 'J') {
        s++;
        if (!gerber_number(g, &s, &j, 1)) {
            return 0;
        }
        offsets = 1;
    }
    if (*s == 'D' && s[1] == '0' && s[2] >= '1' && s[2] <= '3') {
        op = s[2] - '0';
        s += 3;
    }
    if (*s || !op) {
        return gerber_fail(g, TIKU_CIRCUIT_UNSUPPORTED,
                           "Unknown command, coordinate order or operation");
    }
    if ((!g->have_point && axes != 3) || (op == 1 && !g->have_point)) {
        return gerber_fail(g, TIKU_CIRCUIT_BAD,
                           "Initial point needs both X and Y and a move or "
                           "flash");
    }
    if (offsets && (op != 1 || g->mode == 1)) {
        return gerber_fail(g, TIKU_CIRCUIT_BAD,
                           "Arc offsets outside circular interpolation");
    }
    if (g->region) {
        if (op == 1 && g->mode != 1) {
            if (!gerber_region_arc(g, x, y, i, j)) {
                return 0;
            }
        } else if (!gerber_region(g, op, x, y)) {
            return 0;
        }
    } else if (op != 2 && !gerber_draw(g, op, x, y, i, j)) {
        return 0;
    }
    g->x = x;
    g->y = y;
    g->have_point = 1;
    g->operation = op;
    return 1;
}

tiku_circuit_err_t
tiku_circuit_gerber(const void *data, size_t bytes,
                    const tiku_circuit_import_t *options,
                    tiku_circuit_t **out, tiku_circuit_diagnostic_t *diag)
{
    if (diag) {
        memset(diag, 0, sizeof *diag);
    }
    circuit_gerber_t *g = calloc(1, sizeof *g);

    if (!g) {
        if (diag) {
            diag->code = TIKU_CIRCUIT_NOMEM;
        }
        return TIKU_CIRCUIT_NOMEM;
    }
    g->line = 1;
    g->diag = diag;
    g->options = options;
    if (!data || !bytes || !out) {
        gerber_fail(g, TIKU_CIRCUIT_BAD, "Missing Gerber input/output");
    } else if (bytes > GERBER_INPUT_MAX) {
        gerber_fail(g, TIKU_CIRCUIT_LIMIT, "Gerber file exceeds 8 MiB");
    }
    if (!g->error && !(g->doc = circuit_new())) {
        gerber_fail(g, TIKU_CIRCUIT_NOMEM, "Cannot allocate artwork scene");
    }
    const unsigned char *src = data;
    size_t at = 0, commands = 0;
    unsigned line = 1;

    while (!g->error && at < bytes) {
        if (src[at] == '\r' || src[at] == '\n' || src[at] == ' ') {
            if (src[at] == '\n') {
                line++;
            }
            at++;
            continue;
        }
        g->line = line;
        if (options && options->cancel && options->cancel(options->ctx)) {
            gerber_fail(g, TIKU_CIRCUIT_CANCELLED, "Gerber import cancelled");
            break;
        }
        if (g->ended) {
            gerber_fail(g, TIKU_CIRCUIT_BAD, "Data after M02");
            break;
        }
        if (++commands > GERBER_COMMANDS_MAX) {
            gerber_fail(g, TIKU_CIRCUIT_LIMIT, "Too many Gerber commands");
            break;
        }
        int ext = src[at] == '%';

        if (ext) {
            at++;
        }
        char token[GERBER_COMMAND_BYTES + 1];
        size_t used = 0;

        while (at < bytes && src[at] != '*') {
            unsigned c = src[at++];

            if (c == '\n') {
                line++;
                continue;
            }
            if (c == '\r') {
                continue;
            }
            if (c < 32 || c > 126 || c == '%') {
                gerber_fail(g, TIKU_CIRCUIT_BAD,
                            "Invalid character in Gerber command");
                break;
            }
            if (used == sizeof token - 1) {
                gerber_fail(g, TIKU_CIRCUIT_LIMIT,
                            "Gerber command exceeds 1024 bytes");
                break;
            }
            token[used++] = (char)c;
        }
        token[used] = 0;
        if (g->error) {
            break;
        }
        if (at == bytes || !used) {
            gerber_fail(g, TIKU_CIRCUIT_BAD,
                        "Empty or unfinished Gerber command");
            break;
        }
        at++;
        if (ext) {
            if (!strncmp(token, "AM", 2)) {
                /* A macro's statements run to the closing '%'. */
                char body[GERBER_MACRO_BYTES + 1];
                size_t length = 0;

                if (g->region) {
                    gerber_fail(g, TIKU_CIRCUIT_BAD, "Macro inside region");
                    break;
                }
                while (at < bytes && src[at] != '%') {
                    unsigned c = src[at++];

                    if (c == '\n') {
                        line++;
                    }
                    if ((c < 32 && c != '\n' && c != '\r') || c > 126) {
                        gerber_fail(g, TIKU_CIRCUIT_BAD,
                                    "Invalid macro byte");
                        break;
                    }
                    if (length == GERBER_MACRO_BYTES) {
                        gerber_fail(g, TIKU_CIRCUIT_LIMIT,
                                    "Macro body exceeds budget");
                        break;
                    }
                    body[length++] = (char)c;
                }
                body[length] = 0;
                if (g->error) {
                    break;
                }
                if (at == bytes) {
                    gerber_fail(g, TIKU_CIRCUIT_BAD,
                                "Unclosed macro definition");
                    break;
                }
                at++;
                gerber_macro_define(g, token + 2, body);
                continue;
            }
            while (at < bytes && (src[at] == '\r' || src[at] == '\n')) {
                if (src[at] == '\n') {
                    line++;
                }
                at++;
            }
            if (at == bytes || src[at++] != '%') {
                gerber_fail(g, TIKU_CIRCUIT_UNSUPPORTED,
                            "Expected one parameter per percent-delimited "
                            "command");
                break;
            }
            extended(g, token);
        } else {
            command(g, token);
        }
    }
    if (!g->error && !g->ended) {
        gerber_fail(g, TIKU_CIRCUIT_BAD, "Missing M02 end of Gerber file");
    }
    if (!g->error && options && options->cancel
        && options->cancel(options->ctx)) {
        gerber_fail(g, TIKU_CIRCUIT_CANCELLED, "Gerber import cancelled");
    }
    if (!g->error) {
        g->doc->coverage = 1;
        snprintf(g->doc->warning, sizeof g->doc->warning,
                 "Gerber preview: one artwork layer; no drill overlay or "
                 "manufacturing verification. Source units: %s.",
                 g->unit == 1 ? "mm" : "inch");
        *out = g->doc;
        g->doc = NULL;
    }
    tiku_circuit_err_t error = g->error;

    for (size_t i = 0; i < g->macro_count; i++) {
        free(g->macros[i].body);
    }
    for (size_t i = 0; i < GERBER_APERTURES; i++) {
        tiku_circuit_release(g->apertures[i].macro);
    }
    tiku_circuit_release(g->doc);
    free(g);
    return error;
}
