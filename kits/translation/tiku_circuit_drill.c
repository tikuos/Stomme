/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_circuit_drill.c - XNC/Excellon drill files read a line at a time
 * through explicit header, body and end states, into holes, slots and
 * routes for display.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_circuit_drill_private.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DRILL_INPUT_MAX     (8u * 1024u * 1024u)    /* bytes of one file */
#define DRILL_LINES_MAX     262144  /* lines in one file                 */
#define DRILL_LINE_BYTES    1024    /* one line's text                   */
#define DRILL_DIGITS_MAX    16      /* digits in one number              */
#define DRILL_FRACTION_MAX  8       /* of them after the point           */
#define DRILL_TOOL_DIGITS   3       /* digits in a tool number           */
/* The largest tool diameter, mm. */
#define DRILL_DIAMETER_MAX  1000

int
drill_fail(circuit_drill_t *d, tiku_circuit_err_t e, const char *message)
{
    if (!d->error) {
        d->error = e;
        if (d->diag) {
            d->diag->code = e;
            d->diag->line = d->line;
            d->diag->column = 1;
            snprintf(d->diag->message, sizeof d->diag->message, "%s",
                     message);
        }
    }
    return 0;
}

int
drill_number(circuit_drill_t *d, const char **at, double *out,
             int coordinate)
{
    const char *p = *at;
    int sign = 1, digits = 0, point = 0, fraction = 0;
    double value = 0, scale = 1;

    if (*p == '-' || *p == '+') {
        if (*p == '-') {
            sign = -1;
        }
        p++;
    }
    while ((*p >= '0' && *p <= '9') || *p == '.') {
        if (*p == '.') {
            if (point++) {
                return drill_fail(d, TIKU_CIRCUIT_BAD,
                                  "Repeated decimal point");
            }
            p++;
            continue;
        }
        if (++digits > DRILL_DIGITS_MAX
            || (point && ++fraction > DRILL_FRACTION_MAX)) {
            return drill_fail(d, TIKU_CIRCUIT_LIMIT,
                              "Drill number exceeds precision limit");
        }
        value = value * 10 + (*p++ - '0');
        if (point) {
            scale *= 10;
        }
    }
    if (!digits) {
        return drill_fail(d, TIKU_CIRCUIT_BAD, "Expected a decimal number");
    }
    if (coordinate && !point && value && !d->decimal) {
        return drill_fail(d, TIKU_CIRCUIT_UNSUPPORTED,
                          "Ambiguous integer coordinate: export with "
                          "explicit decimal points; fixed-point format "
                          "selection is not implemented yet");
    }
    *out = sign * value / scale * d->unit;
    *at = p;
    if (!isfinite(*out) || fabs(*out) > DRILL_NUMBER_MAX) {
        return drill_fail(d, TIKU_CIRCUIT_LIMIT,
                          "Drill coordinate exceeds 10000 mm");
    }
    return 1;
}

int
drill_point(circuit_drill_t *d, const char **at, double *x, double *y)
{
    const char *p = *at;

    *x = d->x;
    *y = d->y;
    int got_x = 0, got_y = 0;

    if (*p == 'X') {
        p++;
        if (!drill_number(d, &p, x, 1)) {
            return 0;
        }
        got_x = 1;
    }
    if (*p == 'Y') {
        p++;
        if (!drill_number(d, &p, y, 1)) {
            return 0;
        }
        got_y = 1;
    }
    if ((!got_x && !got_y) || (!d->have_point && (!got_x || !got_y))) {
        return drill_fail(d, TIKU_CIRCUIT_BAD,
                          "Both coordinates are required before a current "
                          "point exists");
    }
    *at = p;
    return 1;
}

/** @brief A T command at @p p: a tool declaration (T1C1.0) in the header,
 *         a tool selection in the body.  @return 1, or 0 on failure. */
static int
tool(circuit_drill_t *d, const char *p)
{
    unsigned number = 0, digits = 0;

    p++;
    while (*p >= '0' && *p <= '9') {
        if (++digits > DRILL_TOOL_DIGITS) {
            return drill_fail(d, TIKU_CIRCUIT_LIMIT,
                              "Tool numbers must be 1 to 255");
        }
        number = number * 10 + (*p++ - '0');
    }
    if (!number || number >= DRILL_TOOLS) {
        return drill_fail(d, TIKU_CIRCUIT_BAD,
                          "Tool numbers must be 1 to 255");
    }
    if (d->state == DRILL_IN_HEADER) {
        if (!d->unit || *p++ != 'C' || d->tools[number].diameter) {
            return drill_fail(d, TIKU_CIRCUIT_BAD,
                              "Tool requires units, a unique number and a "
                              "diameter");
        }
        double diameter;

        if (!drill_number(d, &p, &diameter, 0)) {
            return 0;
        }
        if (*p) {
            return drill_fail(d, TIKU_CIRCUIT_UNSUPPORTED,
                              "Unsupported tool declaration suffix");
        }
        if (diameter <= 0 || diameter > DRILL_DIAMETER_MAX) {
            return drill_fail(d, TIKU_CIRCUIT_BAD,
                              "Tool diameter must be greater than zero and "
                              "at most 1000 mm");
        }
        d->tools[number].diameter = diameter;
        return drill_attributes(d, d->aperture, d->tools[number].attributes);
    }
    if (*p || d->down || !d->tools[number].diameter) {
        return drill_fail(d, TIKU_CIRCUIT_BAD,
                          "Invalid, undefined, or in-route tool change");
    }
    d->tool = (int)number;
    return 1;
}

/** @brief One nonempty line @p s: header, mode, tool, routing or
 *         coordinate command.  @return 1, or 0 with the failure recorded. */
static int
command(circuit_drill_t *d, const char *s)
{
    if (d->state == DRILL_AFTER_END) {
        return drill_fail(d, TIKU_CIRCUIT_BAD, "Data after M30 end of file");
    }
    if (*s == ';') {
        return drill_comment(d, s + 1);
    }
    if (d->state == DRILL_BEFORE_HEADER) {
        if (strcmp(s, "M48")) {
            return drill_fail(d, TIKU_CIRCUIT_FORMAT,
                              "Expected an XNC/Excellon M48 header");
        }
        d->state = DRILL_IN_HEADER;
        return 1;
    }
    if (d->state == DRILL_IN_HEADER) {
        if (!strcmp(s, "METRIC") || !strcmp(s, "INCH")) {
            if (d->unit) {
                return drill_fail(d, TIKU_CIRCUIT_BAD,
                                  "Repeated or conflicting drill units");
            }
            d->unit = !strcmp(s, "METRIC") ? 1 : DRILL_MM_PER_INCH;
            if (d->format_unit && d->format_unit != d->unit) {
                return drill_fail(d, TIKU_CIRCUIT_BAD,
                                  "FORMAT comment conflicts with declared "
                                  "units");
            }
            return 1;
        }
        if (!strcmp(s, "FMAT,2")) {
            return 1;               /* KiCad's format-2 envelope */
        }
        if (!strcmp(s, "%") || !strcmp(s, "M95")) {
            if (!d->unit) {
                return drill_fail(d, TIKU_CIRCUIT_BAD,
                                  "Drill units are missing");
            }
            d->state = DRILL_IN_BODY;
            return 1;
        }
        if (*s == 'T') {
            return tool(d, s);
        }
        return drill_fail(d, TIKU_CIRCUIT_UNSUPPORTED,
                          "Unsupported drill header; use absolute "
                          "coordinates with explicit decimal points");
    }
    if (*s == 'T') {
        return tool(d, s);
    }
    if (!strcmp(s, "G90")) {
        return 1;                   /* absolute only */
    }
    if (!strcmp(s, "G05")) {
        if (d->down) {
            return drill_fail(d, TIKU_CIRCUIT_BAD,
                              "Lift the tool before changing to drill mode");
        }
        d->route = 0;
        return 1;
    }
    if (!strcmp(s, "M15")) {
        if (!d->route || d->down || !d->tool || !d->have_point) {
            return drill_fail(d, TIKU_CIRCUIT_BAD,
                              "Tool down requires a selected tool at a route "
                              "start");
        }
        d->down = 1;
        d->route_drawn = 0;
        return 1;
    }
    if (!strcmp(s, "M16") || !strcmp(s, "M17")) {
        if (!d->route || !d->down) {
            return drill_fail(d, TIKU_CIRCUIT_BAD,
                              "Tool up without an active route");
        }
        /* A plunge that cut nothing still leaves a hole. */
        if (!d->route_drawn && !drill_emit(d, DRILL_HOLE, d->x, d->y, 0)) {
            return 0;
        }
        d->down = 0;
        return 1;
    }
    if (!strcmp(s, "M30")) {
        if (d->down) {
            return drill_fail(d, TIKU_CIRCUIT_BAD,
                              "Unfinished route at end of file");
        }
        d->state = DRILL_AFTER_END;
        return 1;
    }
    int kind = -1;
    const char *p = s;

    if (!strncmp(p, "G00", 3)) {
        kind = DRILL_RAPID;
        p += 3;
    } else if (!strncmp(p, "G01", 3)) {
        kind = DRILL_STRAIGHT;
        p += 3;
    } else if (!strncmp(p, "G02", 3)) {
        kind = DRILL_CLOCKWISE;
        p += 3;
    } else if (!strncmp(p, "G03", 3)) {
        kind = DRILL_COUNTERCLOCKWISE;
        p += 3;
    } else if (*p == 'X' || *p == 'Y') {
        kind = DRILL_HOLE;
    } else {
        return drill_fail(d, TIKU_CIRCUIT_UNSUPPORTED,
                          "Unsupported drill command (no incremental, "
                          "repeats, compensation or machine commands)");
    }
    double x, y, radius = 0;

    if (!drill_point(d, &p, &x, &y)) {
        return 0;
    }
    if (kind == DRILL_HOLE && !strncmp(p, "G85", 3)) {
        /* A slot: the first point starts it, the second ends it. */
        if (d->route || d->down) {
            return drill_fail(d, TIKU_CIRCUIT_BAD,
                              "G85 slot requires drill mode");
        }
        d->x = x;
        d->y = y;
        d->have_point = 1;
        p += 3;
        if (!drill_point(d, &p, &x, &y)) {
            return 0;
        }
        kind = DRILL_SLOT;
    }
    if (kind == DRILL_CLOCKWISE || kind == DRILL_COUNTERCLOCKWISE) {
        if (*p++ != 'A') {
            return drill_fail(d, TIKU_CIRCUIT_UNSUPPORTED,
                              "Circular routes require an explicit A radius");
        }
        if (!drill_number(d, &p, &radius, 1)) {
            return 0;
        }
    }
    if (*p) {
        return drill_fail(d, TIKU_CIRCUIT_UNSUPPORTED,
                          "Unsupported or repeated coordinate fields");
    }
    if (kind == DRILL_RAPID) {
        if (d->down) {
            return drill_fail(d, TIKU_CIRCUIT_BAD,
                              "Rapid move while tool is down");
        }
        d->route = 1;
    } else {
        if (!d->tool || (kind == DRILL_HOLE && d->route)
            || (kind <= DRILL_COUNTERCLOCKWISE
                && (!d->route || !d->down || !d->have_point))) {
            return drill_fail(d, TIKU_CIRCUIT_BAD,
                              "Hole or route conflicts with current "
                              "mode/tool state");
        }
        if (!drill_emit(d, kind, x, y, radius)) {
            return 0;
        }
        if (d->down) {
            d->route_drawn = 1;
        }
    }
    d->x = x;
    d->y = y;
    d->have_point = 1;
    return 1;
}

tiku_circuit_err_t
tiku_circuit_drill(const void *data, size_t bytes,
                   const tiku_circuit_import_t *options, tiku_circuit_t **out,
                   tiku_circuit_diagnostic_t *diag)
{
    if (diag) {
        memset(diag, 0, sizeof *diag);
    }
    circuit_drill_t *d = calloc(1, sizeof *d);

    if (!d) {
        if (diag) {
            diag->code = TIKU_CIRCUIT_NOMEM;
            snprintf(diag->message, sizeof diag->message,
                     "Cannot allocate drill parser");
        }
        return TIKU_CIRCUIT_NOMEM;
    }
    d->options = options;
    d->diag = diag;
    d->line = 1;
    if (!data || !bytes || !out) {
        drill_fail(d, TIKU_CIRCUIT_BAD, "Missing drill input or output");
    } else if (bytes > DRILL_INPUT_MAX) {
        drill_fail(d, TIKU_CIRCUIT_LIMIT, "Drill input exceeds 8 MiB");
    }
    if (!d->error && !(d->doc = circuit_new())) {
        drill_fail(d, TIKU_CIRCUIT_NOMEM, "Cannot allocate drill scene");
    }
    const unsigned char *src = data;
    size_t at = 0, lines = 0;

    while (!d->error && at < bytes) {
        if (options && options->cancel && options->cancel(options->ctx)) {
            drill_fail(d, TIKU_CIRCUIT_CANCELLED, "Drill import cancelled");
            break;
        }
        if (++lines > DRILL_LINES_MAX) {
            drill_fail(d, TIKU_CIRCUIT_LIMIT, "Too many drill lines");
            break;
        }
        char line[DRILL_LINE_BYTES + 1];
        size_t used = 0;

        while (at < bytes && src[at] != '\r' && src[at] != '\n') {
            unsigned c = src[at++];

            if (c < 32 || c > 126) {
                drill_fail(d, TIKU_CIRCUIT_BAD,
                           "Drill input must be printable ASCII");
                break;
            }
            if (used == sizeof line - 1) {
                drill_fail(d, TIKU_CIRCUIT_LIMIT,
                           "Drill line exceeds 1024 bytes");
                break;
            }
            line[used++] = (char)c;
        }
        line[used] = 0;
        if (at < bytes && src[at] == '\r') {
            at++;
            if (at < bytes && src[at] == '\n') {
                at++;
            }
        } else if (at < bytes && src[at] == '\n') {
            at++;
        }
        if (!d->error && used) {
            command(d, line);
        }
        d->line++;
    }
    if (!d->error && d->state != DRILL_AFTER_END) {
        drill_fail(d, TIKU_CIRCUIT_BAD,
                   "Missing M30 end of file or unfinished header");
    }
    if (!d->error && options && options->cancel
        && options->cancel(options->ctx)) {
        drill_fail(d, TIKU_CIRCUIT_CANCELLED, "Drill import cancelled");
    }
    if (!d->error) {
        snprintf(d->doc->warning, sizeof d->doc->warning,
                 "Drill preview: holes and routes only; no copper or board "
                 "outline. Source units: %s.", d->unit == 1 ? "mm" : "inch");
        *out = d->doc;
        d->doc = NULL;
    }
    tiku_circuit_err_t error = d->error;

    tiku_circuit_release(d->doc);
    free(d);
    return error;
}
