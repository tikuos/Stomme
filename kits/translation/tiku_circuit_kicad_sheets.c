/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_circuit_kicad_sheets.c - hierarchical sheet objects: the outline
 * drawn with its name and file properties and its pins, and the link to
 * the child file collected for the project loader, which alone asks for
 * files.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_circuit_kicad_private.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define SHEET_LABEL 1024        /* bytes of a property's shown text      */

/** @brief Copy sheet identity @p s into @p out, @p bytes long, refusing
 *         an empty or overlong one.  @return 1, or 0 with it noted */
static int
copy(circuit_kc_t *k, size_t n, char *out, size_t bytes, const char *s)
{
    if (!s || !*s) {
        return kc_fail(k, n, TIKU_CIRCUIT_BAD, "Missing sheet identity");
    }
    if (strlen(s) >= bytes) {
        return kc_fail(k, n, TIKU_CIRCUIT_LIMIT,
                       "Sheet identity is too long");
    }
    memcpy(out, s, strlen(s) + 1);
    return 1;
}

/** @brief Append sheet @p n's outline, named after @p link: an unrotated
 *         rectangle with a solid stroke in its own colour, if it gives
 *         one, and no background colour.
 *  @return 1, or 0 with the failure noted */
static int
outline(circuit_kc_t *k, size_t n, const circuit_kc_link_t *link)
{
    double x, y, a, w, h, width = KC_STROKE;
    size_t size = kc_child(k, n, "size"), stroke = kc_child(k, n, "stroke");
    size_t fill = kc_child(k, n, "fill"), color = kc_child(k, fill, "color");

    if (!kc_at(k, n, &x, &y, &a) || !kc_number(k, size, 0, &w) ||
        !kc_number(k, size, 1, &h) || !kc_fields(k, size, "", "", 2, "")) {
        return 0;
    }
    if (a || w <= 0 || h <= 0) {
        return kc_fail(k, n, TIKU_CIRCUIT_BAD, "Invalid sheet rectangle");
    }
    uint32_t colour = KC_INK;

    if (stroke != SX_NONE) {
        if (!kc_fields(k, stroke, "width type color", "", 0, "")) {
            return 0;
        }
        size_t sw = kc_child(k, stroke, "width");

        if (sw != SX_NONE && !kc_number(k, sw, 0, &width)) {
            return 0;
        }
        const char *type = kc_arg(k, kc_child(k, stroke, "type"), 0);

        if (type && strcmp(type, "solid") && strcmp(type, "default")) {
            return kc_fail(k, stroke, TIKU_CIRCUIT_UNSUPPORTED,
                           "Only solid sheet outlines are supported");
        }
        size_t sc = kc_child(k, stroke, "color");

        if (sc != SX_NONE && !kc_colour(k, sc, &colour)) {
            return 0;
        }
        if (!width) {
            width = KC_STROKE;
        }
    }
    if (fill != SX_NONE) {
        if (!kc_fields(k, fill, "color", "", 0, "") ||
            !kc_fields(k, color, "", "", 4, "")) {
            return 0;
        }
        double rgba[4];

        for (int i = 0; i < 4; i++) {
            if (!kc_number(k, color, (size_t)i, &rgba[i])) {
                return 0;
            }
        }
        if (rgba[3] != 0) {
            return kc_fail(k, fill, TIKU_CIRCUIT_UNSUPPORTED,
                           "Coloured sheet backgrounds are not supported "
                           "yet");
        }
    }
    circuit_point_t p[] = {{x, y}, {x + w, y}, {x + w, y + h}, {x, y + h}};

    return kc_result(k, n, circuit_add(k->doc, link->name, "Sheet", p, 4,
                                       width, colour, 1, 0));
}

int
kc_sheet(circuit_kc_t *k, size_t n)
{
    if (!k->links) {
        return kc_fail(k, n, TIKU_CIRCUIT_UNSUPPORTED,
                       "Linked sheets require the project loader");
    }
    if (!kc_fields(k, n, "at size fields_autoplaced stroke fill uuid "
                   "property pin instances", "property pin", 0, "")) {
        return 0;
    }
    if (k->links->count == TIKU_CIRCUIT_SHEETS_MAX) {
        return kc_fail(k, n, TIKU_CIRCUIT_LIMIT, "Too many linked sheets");
    }
    circuit_kc_link_t link = {.line = k->sx.node[n].line,
                              .column = k->sx.node[n].column};
    const char *uuid = kc_arg(k, kc_child(k, n, "uuid"), 0);

    if (!kc_uuid(uuid)) {
        return kc_fail(k, n, TIKU_CIRCUIT_BAD,
                       "Sheet requires a valid UUID");
    }
    memcpy(link.uuid, uuid, KC_UUID_BYTES);
    for (size_t i = 0; i < k->links->count; i++) {
        if (!strcmp(k->links->link[i].uuid, uuid)) {
            return kc_fail(k, n, TIKU_CIRCUIT_BAD, "Duplicate sheet UUID");
        }
    }
    for (size_t i = n + 2; i < k->sx.node[n].end; i = k->sx.node[i].end) {
        const char *head = circuit_sx_head(&k->sx, i);

        if (!head || strcmp(head, "property")) {
            continue;
        }
        const char *key = kc_arg(k, i, 0), *value = kc_arg(k, i, 1);
        char *dst = key && !strcmp(key, "Sheetname") ? link.name
                    : key && !strcmp(key, "Sheetfile") ? link.file : NULL;

        if (dst) {
            if (*dst) {
                return kc_fail(k, i, TIKU_CIRCUIT_BAD,
                               "Duplicate sheet property");
            }
            if (!copy(k, i, dst, dst == link.name ? sizeof link.name
                                                  : sizeof link.file,
                      value)) {
                return 0;
            }
        }
    }
    if (!*link.name || !*link.file) {
        return kc_fail(k, n, TIKU_CIRCUIT_BAD,
                       "Sheet requires Sheetname and Sheetfile");
    }
    size_t instance = kc_instance(k, n, "page");

    if (k->error) {
        return 0;
    }
    if (instance != SX_NONE &&
        !copy(k, instance, link.page, sizeof link.page,
              kc_arg(k, kc_child(k, instance, "page"), 0))) {
        return 0;
    }
    if (!outline(k, n, &link)) {
        return 0;
    }
    for (size_t i = n + 2; i < k->sx.node[n].end; i = k->sx.node[i].end) {
        const char *head = circuit_sx_head(&k->sx, i);

        if (!head) {
            continue;
        }
        if (!strcmp(head, "property")) {
            const char *key = kc_arg(k, i, 0), *value = kc_arg(k, i, 1);

            if (!key || !value) {
                return kc_fail(k, i, TIKU_CIRCUIT_BAD,
                               "Invalid sheet property");
            }
            char label[SHEET_LABEL];
            const char *prefix = !strcmp(key, "Sheetfile") ? "File: " : "";

            if (snprintf(label, sizeof label, "%s%s", prefix, value) >=
                (int)sizeof label) {
                return kc_fail(k, i, TIKU_CIRCUIT_LIMIT,
                               "Sheet property is too long");
            }
            if (!kc_text(k, i, KC_IDENTITY, link.name, "Sheet", label, 0)) {
                return 0;
            }
        } else if (!strcmp(head, "pin") && !kc_hlabel(k, i, 1)) {
            return 0;
        }
    }
    k->links->link[k->links->count++] = link;
    return 1;
}
