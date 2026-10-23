/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_circuit_eagle.c - EAGLE XML schematics: the document's sections
 * checked, each sheet drawn from the libraries embedded in the file, and
 * the sheets published as a project.  The reader opens no other file.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_circuit_eagle_private.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define EAGLE_WORK_MAX   2000000    /* steps in one import               */
#define EAGLE_DIGITS_MAX 16         /* digits in one number              */
#define EAGLE_NUMBER_MAX 10000      /* a number's magnitude              */
/* The substitute font's bytes. */
#define EAGLE_FONT_MAX   (8u * 1024u * 1024u)

int
eagle_fail(circuit_eagle_t *e, tiku_circuit_err_t code, const char *message)
{
    if (!e->error) {
        e->error = code;
        if (e->diag) {
            e->diag->code = code;
            snprintf(e->diag->message, sizeof e->diag->message, "%s",
                     message);
        }
    }
    return 0;
}

int
eagle_step(circuit_eagle_t *e)
{
    if (e->error) {
        return 0;
    }
    if (++e->work > EAGLE_WORK_MAX) {
        return eagle_fail(e, TIKU_CIRCUIT_LIMIT,
                          "EAGLE interpretation budget exceeded");
    }
    if (e->options && e->options->cancel
        && e->options->cancel(e->options->ctx)) {
        return eagle_fail(e, TIKU_CIRCUIT_CANCELLED,
                          "EAGLE import cancelled");
    }
    return 1;
}

const char *
eagle_attr(eagle_node_t *n, const char *key, const char *fallback)
{
    const char *s = tiku_xml_attr(n, key);

    return s ? s : fallback;
}

double
eagle_number(circuit_eagle_t *e, eagle_node_t *n, const char *key,
             double fallback, int required)
{
    const char *s = tiku_xml_attr(n, key);

    if (!s) {
        if (required) {
            eagle_fail(e, TIKU_CIRCUIT_BAD,
                       "Missing EAGLE numeric attribute");
        }
        return fallback;
    }
    int sign = 1, digits = 0, point = 0;
    double v = 0, scale = 1;

    if (*s == '-' || *s == '+') {
        if (*s == '-') {
            sign = -1;
        }
        s++;
    }
    while (*s) {
        if (*s == '.' && !point) {
            point = 1;
            s++;
            continue;
        }
        if (*s < '0' || *s > '9' || ++digits > EAGLE_DIGITS_MAX) {
            eagle_fail(e, TIKU_CIRCUIT_BAD, "Invalid EAGLE number");
            return 0;
        }
        v = v * 10 + (*s++ - '0');
        if (point) {
            scale *= 10;
        }
    }
    if (!digits) {
        eagle_fail(e, TIKU_CIRCUIT_BAD, "Invalid EAGLE number");
        return 0;
    }
    v = sign * v / scale;
    if (!isfinite(v) || fabs(v) > EAGLE_NUMBER_MAX) {
        eagle_fail(e, TIKU_CIRCUIT_LIMIT, "EAGLE number exceeds range");
    }
    return v;
}

int
eagle_rotation(circuit_eagle_t *e, const char *s, double *angle, int *mirror,
               int *spin)
{
    *mirror = *spin = 0;
    *angle = 0;
    if (!s) {
        return 1;
    }
    if (*s == 'S') {
        *spin = 1;
        s++;
    }
    if (*s == 'M') {
        *mirror = 1;
        s++;
    }
    if (*s++ != 'R') {
        return eagle_fail(e, TIKU_CIRCUIT_BAD, "Invalid EAGLE rotation");
    }
    /* eagle_number() reads a node's attribute, so the angle's text is
     * wrapped in a node of its own. */
    const char *attrs[] = {"angle", s};
    tiku_xml_node_t n = {.attr = attrs, .nattr = 1};

    *angle = eagle_number(e, &n, "angle", 0, 1);
    if (*angle < 0 || *angle >= 360) {
        return eagle_fail(e, TIKU_CIRCUIT_BAD,
                          "EAGLE rotation must be in [0,360)");
    }
    return !e->error;
}

eagle_node_t *
eagle_named(circuit_eagle_t *e, eagle_node_t *parent, const char *kind,
            const char *name)
{
    eagle_node_t *found = NULL;

    if (!name) {
        eagle_fail(e, TIKU_CIRCUIT_BAD, "Missing EAGLE reference");
        return NULL;
    }
    for (eagle_node_t *n = tiku_xml_child(parent, kind); n;
         n = tiku_xml_next(n, kind)) {
        if (!eagle_step(e)) {
            return NULL;
        }
        const char *s = tiku_xml_attr(n, "name");

        if (!s && !strcmp(kind, "device")) {
            s = "";                 /* the DTD's default */
        }
        if (s && !strcmp(s, name)) {
            if (found) {
                eagle_fail(e, TIKU_CIRCUIT_BAD,
                           "Duplicate EAGLE definition");
                return NULL;
            }
            found = n;
        }
    }
    if (!found) {
        eagle_fail(e, TIKU_CIRCUIT_MISSING,
                   "Missing embedded EAGLE library, symbol, gate, device or "
                   "part");
    }
    return found;
}

circuit_point_t
eagle_point(circuit_eagle_t *e, double x, double y)
{
    return (circuit_point_t){e->m[0] * x + e->m[2] * y + e->m[4],
                             -(e->m[1] * x + e->m[3] * y + e->m[5])};
}

int
eagle_metadata(circuit_eagle_t *e, size_t start)
{
    if (e->error) {
        return 0;
    }
    char info[2048];
    int n = snprintf(info, sizeof info,
                     "EAGLE XML schematic\n"
                     "Component: %s\n"
                     "Value: %s\n"
                     "Gate: %s\n"
                     "Library: %s\n"
                     "Deviceset: %s\n"
                     "Device: %s\n"
                     "Net/bus: %s",
                     e->name, e->value,
                     e->gate ? e->gate : "Not applicable",
                     eagle_attr(e->part, "library", "Not applicable"),
                     eagle_attr(e->part, "deviceset", "Not applicable"),
                     eagle_attr(e->part, "device", "Not applicable"),
                     e->net ? e->net : "Not applicable");

    if (n < 0 || (size_t)n >= sizeof info) {
        return eagle_fail(e, TIKU_CIRCUIT_LIMIT,
                          "EAGLE metadata exceeds limit");
    }
    for (size_t i = start; i < e->doc->count; i++) {
        tiku_circuit_err_t error = circuit_details(e->doc, i, info);

        if (error) {
            return eagle_fail(e, error, "Cannot retain EAGLE metadata");
        }
    }
    return 1;
}

/** @brief Check that every child of @p parent is named in @p allowed,
 *         each name there with a space either side, and appears once.
 *  @return 1, or 0 with the failure recorded. */
static int
schema(circuit_eagle_t *e, eagle_node_t *parent, const char *allowed)
{
    for (eagle_node_t *n = tiku_xml_child(parent, NULL); n;
         n = tiku_xml_next(n, NULL)) {
        if (!eagle_step(e)) {
            return 0;
        }
        char word[128];
        int len = snprintf(word, sizeof word, " %s ", n->name);

        if (len < 0 || (size_t)len >= sizeof word
            || !strstr(allowed, word)) {
            return eagle_fail(e, TIKU_CIRCUIT_UNSUPPORTED,
                              "Unsupported EAGLE schematic section");
        }
        if (tiku_xml_next(n, n->name)) {
            return eagle_fail(e, TIKU_CIRCUIT_BAD,
                              "Duplicate EAGLE schematic section");
        }
    }
    return 1;
}

tiku_circuit_err_t
tiku_circuit_eagle(const void *data, size_t bytes, const char *path,
                   const tiku_circuit_import_t *options,
                   tiku_circuit_project_t **out,
                   tiku_circuit_diagnostic_t *diag)
{
    if (diag) {
        memset(diag, 0, sizeof *diag);
    }
    circuit_eagle_t e = {.options = options, .diag = diag, .name = "Drawing",
                         .value = "", .path = path,
                         .m = {1, 0, 0, 1, 0, 0}};

    if (!out || !path) {
        eagle_fail(&e, TIKU_CIRCUIT_BAD, "Missing EAGLE output/path");
        return e.error;
    }
    tiku_xml_t *xml = NULL;

    e.error = circuit_eagle_xml(data, bytes, options, &xml, diag);
    if (e.error) {
        return e.error;
    }
    eagle_node_t *root = tiku_xml_root(xml);
    eagle_node_t *drawing = tiku_xml_child(root, "drawing");
    const char *version = eagle_attr(root, "version", "");

    if (!tiku_xml_is(root, "eagle") || !drawing) {
        eagle_fail(&e, TIKU_CIRCUIT_FORMAT,
                   "Expected an EAGLE XML drawing, not legacy binary/other "
                   ".sch data");
    }
    if (!e.error
        && (version[0] < '6' || version[0] > '9' || version[1] != '.')) {
        eagle_fail(&e, TIKU_CIRCUIT_UNSUPPORTED,
                   "EAGLE XML versions 6 through 9 are supported");
    }
    if (!e.error) {
        schema(&e, root, " drawing  compatibility ");
    }
    if (!e.error) {
        schema(&e, drawing, " settings  grid  layers  schematic ");
    }
    e.schematic = tiku_xml_child(drawing, "schematic");
    if (!e.error && !e.schematic) {
        eagle_fail(&e, TIKU_CIRCUIT_FORMAT,
                   "This EAGLE drawing is not a schematic");
    }
    if (!e.error) {
        schema(&e, e.schematic,
               " description  libraries  attributes  variantdefs "
               " classes  parts  sheets  errors ");
    }
    for (eagle_node_t *v = tiku_xml_child(tiku_xml_child(e.schematic,
                                                         "variantdefs"),
                                          "variantdef");
         v && !e.error; v = tiku_xml_next(v, "variantdef")) {
        if (!strcmp(eagle_attr(v, "current", "no"), "yes")) {
            eagle_fail(&e, TIKU_CIRCUIT_UNSUPPORTED,
                       "Active EAGLE assembly variants require explicit "
                       "variant selection; open the default assembly");
        }
    }
    e.libraries = tiku_xml_child(e.schematic, "libraries");
    e.parts = tiku_xml_child(e.schematic, "parts");
    tiku_circuit_t *scenes[TIKU_CIRCUIT_SHEETS_MAX] = {0};
    const char *names[TIKU_CIRCUIT_SHEETS_MAX];
    char labels[TIKU_CIRCUIT_SHEETS_MAX][64];
    size_t count = 0, budget = 0;

    for (eagle_node_t *s = tiku_xml_child(tiku_xml_child(e.schematic,
                                                         "sheets"), NULL);
         s; s = tiku_xml_next(s, NULL)) {
        e.sheet_total++;
    }
    for (eagle_node_t *sheet = tiku_xml_child(tiku_xml_child(e.schematic,
                                                             "sheets"),
                                              NULL);
         sheet && !e.error; sheet = tiku_xml_next(sheet, NULL)) {
        if (!tiku_xml_is(sheet, "sheet")) {
            eagle_fail(&e, TIKU_CIRCUIT_UNSUPPORTED,
                       "Unexpected sheet element");
            break;
        }
        if (count == TIKU_CIRCUIT_SHEETS_MAX) {
            eagle_fail(&e, TIKU_CIRCUIT_LIMIT, "EAGLE exceeds 64 sheets");
            break;
        }
        if (!eagle_step(&e)) {
            break;
        }
        e.doc = circuit_new();
        if (!e.doc) {
            eagle_fail(&e, TIKU_CIRCUIT_NOMEM, "Cannot allocate EAGLE scene");
            break;
        }
        scenes[count] = e.doc;
        snprintf(labels[count], sizeof labels[count], "Sheet %zu",
                 count + 1);
        names[count] = labels[count];
        count++;
        e.sheet_number = (unsigned)count;
        if (options && options->font_data) {
            if (options->font_bytes > EAGLE_FONT_MAX) {
                eagle_fail(&e, TIKU_CIRCUIT_LIMIT, "Font exceeds 8 MiB");
                break;
            }
            e.doc->font = tiku_ttf_open_mem(options->font_data,
                                            options->font_bytes);
            if (!e.doc->font) {
                eagle_fail(&e, TIKU_CIRCUIT_BAD,
                           "Cannot open substitute font");
                break;
            }
        }
        if (!eagle_sheet(&e, sheet)) {
            break;
        }
        size_t used = e.doc->bytes + (options ? options->font_bytes : 0);

        if (used > TIKU_CIRCUIT_PROJECT_BYTES - budget) {
            eagle_fail(&e, TIKU_CIRCUIT_LIMIT, "EAGLE scenes exceed 64 MiB");
            break;
        }
        budget += used;
        snprintf(e.doc->warning, sizeof e.doc->warning,
                 "EAGLE XML preview: substitute outline font and theme "
                 "colours; text metrics approximate. Inspection only, not "
                 "electrical verification.");
    }
    if (!e.error && !count) {
        eagle_fail(&e, TIKU_CIRCUIT_BAD, "EAGLE schematic has no sheets");
    }
    if (!e.error && eagle_step(&e)) {
        tiku_circuit_err_t error = circuit_project_sheets(scenes, names, path,
                                                          count, out);

        if (error) {
            eagle_fail(&e, error, "Cannot publish EAGLE sheets");
        }
    }
    for (size_t i = 0; i < count; i++) {
        tiku_circuit_release(scenes[i]);
    }
    tiku_xml_free(xml);
    return e.error;
}
