/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_circuit_kicad.c - the KiCad schematic interpreter's frame: the
 * root checked and each object handed to its reader, with the shared
 * field, flag, number and position readers.  Anything outside the subset
 * refuses the document; no partial scene is published.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_circuit_kicad_private.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/* Checked operations in one decode, operations between cancellation
 * checks, and distinct fields in one list. */
#define KC_WORK_MAX       2000000
#define KC_CANCEL_STRIDE  128
#define KC_FIELDS_MAX     40

/* Format revisions read: from the first S-expression schematic to the
 * largest date-shaped number. */
#define KC_VERSION_MIN    20211123
#define KC_VERSION_LAST   99999999
#define KC_VERSION_TESTED 20230121

int
kc_fail(circuit_kc_t *k, size_t n, tiku_circuit_err_t code, const char *text)
{
    if (!k->error) {
        size_t at = n < k->sx.count ? n : k->searched;

        k->error = code;
        if (k->diag) {
            k->diag->code = code;
            k->diag->line = at < k->sx.count ? k->sx.node[at].line : 1;
            k->diag->column = at < k->sx.count ? k->sx.node[at].column : 1;
            snprintf(k->diag->message, sizeof k->diag->message, "%s", text);
        }
    }
    return 0;
}

int
kc_step(circuit_kc_t *k, size_t n)
{
    if (k->error) {
        return 0;
    }
    if (++k->work > KC_WORK_MAX) {
        return kc_fail(k, n, TIKU_CIRCUIT_LIMIT,
                       "Schematic interpretation work limit reached");
    }
    if ((k->work & (KC_CANCEL_STRIDE - 1)) == 1 && k->options &&
        k->options->cancel && k->options->cancel(k->options->ctx)) {
        return kc_fail(k, n, TIKU_CIRCUIT_CANCELLED, "Import cancelled");
    }
    return 1;
}

const char *
kc_arg(circuit_kc_t *k, size_t n, size_t index)
{
    size_t a = circuit_sx_arg(&k->sx, n, index);

    return a == SX_NONE ? NULL : k->sx.node[a].text;
}

size_t
kc_child(circuit_kc_t *k, size_t n, const char *name)
{
    size_t child = circuit_sx_child(&k->sx, n, name);

    if (child == SX_NONE && n < k->sx.count) {
        k->searched = n;
    }
    return child;
}

/** @brief Whether @p text is one of the space-separated words of @p list.
 */
static int
word(const char *list, const char *text)
{
    if (!text) {
        return 0;
    }
    size_t len = strlen(text);

    while (*list) {
        size_t n = strcspn(list, " ");

        if (n == len && !memcmp(list, text, n)) {
            return 1;
        }
        list += n;
        if (*list) {
            list++;
        }
    }
    return 0;
}

int
kc_fields(circuit_kc_t *k, size_t n, const char *fields, const char *repeat,
          unsigned args, const char *flags)
{
    if (!circuit_sx_head(&k->sx, n)) {
        return kc_fail(k, n, TIKU_CIRCUIT_BAD, "Expected named expression");
    }
    const char *seen[KC_FIELDS_MAX];
    size_t count = 0;
    unsigned atoms = 0;

    for (size_t i = n + 2; i < k->sx.node[n].end; i = k->sx.node[i].end) {
        if (!kc_step(k, i)) {
            return 0;
        }
        const char *head = circuit_sx_head(&k->sx, i);

        if (!head) {
            if (atoms++ < args) {
                continue;
            }
            if (k->sx.node[i].quoted || !word(flags, k->sx.node[i].text)) {
                return kc_fail(k, i, TIKU_CIRCUIT_UNSUPPORTED,
                               "Unsupported positional value or flag");
            }
            continue;
        }
        if (atoms < args) {
            return kc_fail(k, i, TIKU_CIRCUIT_BAD,
                           "Missing leading positional value");
        }
        if (!word(fields, head)) {
            char message[sizeof k->diag->message];

            snprintf(message, sizeof message, "Unsupported '%s' inside '%s'",
                     head, circuit_sx_head(&k->sx, n));
            return kc_fail(k, i, TIKU_CIRCUIT_UNSUPPORTED, message);
        }
        if (word(repeat, head)) {
            continue;
        }
        for (size_t j = 0; j < count; j++) {
            if (!strcmp(seen[j], head)) {
                return kc_fail(k, i, TIKU_CIRCUIT_BAD,
                               "Duplicate schematic field");
            }
        }
        if (count == KC_FIELDS_MAX) {
            return kc_fail(k, i, TIKU_CIRCUIT_LIMIT, "Too many fields");
        }
        seen[count++] = head;
    }
    return atoms >= args ||
           kc_fail(k, n, TIKU_CIRCUIT_BAD, "Missing positional value");
}

int
kc_flag(circuit_kc_t *k, size_t n, unsigned args, const char *flag)
{
    if (n >= k->sx.count) {
        return 0;
    }
    unsigned atoms = 0;

    for (size_t i = n + 2; i < k->sx.node[n].end; i = k->sx.node[i].end) {
        const char *s = k->sx.node[i].text;

        if (!s || atoms++ < args || k->sx.node[i].quoted) {
            continue;
        }
        if (!strcmp(s, flag)) {
            return 1;
        }
    }
    size_t child = kc_child(k, n, flag);
    const char *v = kc_arg(k, child, 0);

    if (child != SX_NONE &&
        (!kc_fields(k, child, "", "", v ? 1 : 0, "") ||
         (v && strcmp(v, "yes") && strcmp(v, "no")))) {
        kc_fail(k, child, TIKU_CIRCUIT_BAD, "Expected a yes/no flag");
        return 0;
    }
    return child != SX_NONE && (!v || !strcmp(v, "yes"));
}

int
kc_number(circuit_kc_t *k, size_t n, size_t index, double *out)
{
    return circuit_sx_number(&k->sx, circuit_sx_arg(&k->sx, n, index), out) ||
           kc_fail(k, n, TIKU_CIRCUIT_BAD, "Expected a finite decimal number");
}

int
kc_at(circuit_kc_t *k, size_t n, double *x, double *y, double *angle)
{
    size_t at = kc_child(k, n, "at");

    *angle = 0;
    if (!kc_number(k, at, 0, x) || !kc_number(k, at, 1, y)) {
        return 0;
    }
    if (kc_arg(k, at, 2) && !kc_number(k, at, 2, angle)) {
        return 0;
    }
    if (fabs(*x) > CIRCUIT_COORD_MAX || fabs(*y) > CIRCUIT_COORD_MAX ||
        fabs(*angle) > KC_ANGLE_MAX) {
        return kc_fail(k, at, TIKU_CIRCUIT_BAD,
                       "Position or angle outside supported range");
    }
    return kc_fields(k, at, "", "", kc_arg(k, at, 2) ? 3 : 2, "");
}

int
kc_result(circuit_kc_t *k, size_t n, tiku_circuit_err_t code)
{
    return !code || kc_fail(k, n, code, tiku_circuit_error(code));
}

circuit_point_t
kc_transform(const double m[6], double x, double y)
{
    return (circuit_point_t){m[0] * x + m[2] * y + m[4],
                             m[1] * x + m[3] * y + m[5]};
}

/** @brief Check the root's fields, then read each drawing object in file
 *         order; metadata draws nothing.  @return 1 or 0 */
static int
objects(circuit_kc_t *k)
{
    const char *metadata = "version generator generator_version uuid paper "
                           "title_block lib_symbols sheet_instances "
                           "symbol_instances bus_alias embedded_fonts";

    if (!kc_fields(k, 0,
                   "version generator generator_version uuid paper "
                   "title_block lib_symbols sheet_instances symbol_instances "
                   "bus_alias embedded_fonts wire bus bus_entry junction "
                   "no_connect polyline rectangle circle arc text label "
                   "symbol sheet hierarchical_label global_label",
                   "wire bus bus_entry junction no_connect polyline "
                   "rectangle circle arc text label symbol sheet "
                   "hierarchical_label global_label bus_alias", 0, "")) {
        return 0;
    }
    for (size_t n = 2; n < k->sx.node[0].end; n = k->sx.node[n].end) {
        if (!kc_step(k, n)) {
            return 0;
        }
        const char *head = circuit_sx_head(&k->sx, n);

        if (word(metadata, head)) {
            continue;
        }
        if (!strcmp(head, "sheet")) {
            if (!kc_sheet(k, n)) {
                return 0;
            }
        } else if (!strcmp(head, "hierarchical_label")) {
            if (!kc_hlabel(k, n, 0)) {
                return 0;
            }
        } else if (!strcmp(head, "global_label")) {
            if (!kc_glabel(k, n)) {
                return 0;
            }
        } else if (!strcmp(head, "symbol")) {
            if (!kc_symbol(k, n)) {
                return 0;
            }
        } else if (!strcmp(head, "junction") ||
                   !strcmp(head, "no_connect") ||
                   !strcmp(head, "bus_entry")) {
            if (!kc_mark(k, n)) {
                return 0;
            }
        } else if (!strcmp(head, "text") || !strcmp(head, "label")) {
            const char *text = kc_arg(k, n, 0);

            if (!text) {
                return kc_fail(k, n, TIKU_CIRCUIT_BAD, "Missing text value");
            }
            if (!kc_text(k, n, KC_IDENTITY, head,
                         !strcmp(head, "label") ? text : "", text, 0)) {
                return 0;
            }
        } else if (!kc_graphic(k, n, KC_IDENTITY, head, "",
                               !strcmp(head, "wire") ? KC_WIRE
                               : !strcmp(head, "bus") ? KC_BLUE : KC_INK)) {
            return 0;
        }
    }
    return 1;
}

tiku_circuit_err_t
circuit_kc_decode(const circuit_sx_t *tree,
                  const tiku_circuit_import_t *options, const char *instance,
                  const char *project, int child, circuit_kc_links_t *links,
                  tiku_circuit_t **out, tiku_circuit_diagnostic_t *diag)
{
    circuit_kc_t k = {.options = options, .diag = diag,
                      .libraries = SX_NONE, .sx = *tree,
                      .instance = instance, .project = project,
                      .child = child, .links = links};

    if (diag) {
        memset(diag, 0, sizeof *diag);
    }
    if (!out) {
        kc_fail(&k, 0, TIKU_CIRCUIT_BAD, "Missing document output");
        return k.error;
    }
    const char *head = circuit_sx_head(&k.sx, 0);

    if (!head || strcmp(head, "kicad_sch")) {
        kc_fail(&k, 0, TIKU_CIRCUIT_FORMAT,
                "Expected a modern KiCad .kicad_sch document");
    }
    double version = 0;

    if (!k.error &&
        kc_number(&k, kc_child(&k, 0, "version"), 0, &version) &&
        (version < KC_VERSION_MIN || version > KC_VERSION_LAST ||
         floor(version) != version)) {
        kc_fail(&k, 0, TIKU_CIRCUIT_UNSUPPORTED,
                "Unsupported schematic format revision");
    }
    if (!k.error) {
        if (instance && kc_child(&k, 0, "symbol_instances") != SX_NONE) {
            kc_fail(&k, 0, TIKU_CIRCUIT_UNSUPPORTED,
                    "Legacy symbol instance tables are not supported; "
                    "resave with KiCad 7 or later");
        }
    }
    if (!k.error) {
        k.doc = circuit_new();
        if (!k.doc) {
            kc_fail(&k, 0, TIKU_CIRCUIT_NOMEM, "Cannot allocate schematic");
        } else {
            snprintf(k.doc->warning, sizeof k.doc->warning,
                     "Preview: substitute font and theme; page frame not "
                     "shown.%s",
                     version == KC_VERSION_TESTED
                     ? "" : " Untested format revision.");
            k.libraries = kc_child(&k, 0, "lib_symbols");
            if (!objects(&k) && !k.error) {
                kc_fail(&k, 0, TIKU_CIRCUIT_BAD, "Invalid schematic object");
            }
        }
    }
    if (k.error) {
        tiku_circuit_release(k.doc);
        return k.error;
    }
    *out = k.doc;
    return TIKU_CIRCUIT_OK;
}

tiku_circuit_err_t
tiku_circuit_kicad(const void *data, size_t bytes,
                   const tiku_circuit_import_t *options, tiku_circuit_t **out,
                   tiku_circuit_diagnostic_t *diag)
{
    circuit_sx_t tree = {0};
    tiku_circuit_err_t error = circuit_sx_parse(data, bytes, options, &tree,
                                                diag);

    if (!error) {
        error = circuit_kc_decode(&tree, options, NULL, NULL, 0, NULL, out,
                                  diag);
    }
    circuit_sx_free(&tree);
    return error;
}
