/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_circuit_drill_attributes.c - drill comments: KiCad's FORMAT
 * declaration and the X2 attributes in their file, tool and object
 * scopes, which are kept as text for inspection.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_circuit_drill_private.h"

#include <stdio.h>
#include <string.h>

int
drill_attributes(circuit_drill_t *d, const drill_attribute_t *attrs,
                 char out[DRILL_ATTRIBUTE_TEXT])
{
    size_t used = 0;

    out[0] = 0;
    for (int i = 0; i < DRILL_ATTRS; i++) {
        if (*attrs[i].name) {
            int n = snprintf(out + used, DRILL_ATTRIBUTE_TEXT - used,
                             "%s: %s\n", attrs[i].name, attrs[i].value);

            if (n < 0 || (size_t)n >= DRILL_ATTRIBUTE_TEXT - used) {
                return drill_fail(d, TIKU_CIRCUIT_LIMIT,
                                  "Too much drill attribute text");
            }
            used += (size_t)n;
        }
    }
    return 1;
}

int
drill_comment(circuit_drill_t *d, const char *s)
{
    while (*s == ' ') {
        s++;
    }
    if (!strncmp(s, "FILE_FORMAT=", 12)) {
        return drill_fail(d, TIKU_CIRCUIT_UNSUPPORTED,
                          "Fixed-point drill coordinates are not supported; "
                          "export explicit decimals");
    }
    if (!strncmp(s, "FORMAT=", 7)) {
        if (d->decimal || d->state > DRILL_IN_HEADER) {
            return drill_fail(d, TIKU_CIRCUIT_BAD,
                              "Repeated or late FORMAT declaration");
        }
        if (!strcmp(s, "FORMAT={-:-/ absolute / metric / decimal}")) {
            d->format_unit = 1;
        } else if (!strcmp(s, "FORMAT={-:-/ absolute / inch / decimal}")) {
            d->format_unit = DRILL_MM_PER_INCH;
        } else {
            return drill_fail(d, TIKU_CIRCUIT_UNSUPPORTED,
                              "Unsupported FORMAT declaration; export "
                              "absolute decimal coordinates");
        }
        if (d->unit && d->unit != d->format_unit) {
            return drill_fail(d, TIKU_CIRCUIT_BAD,
                              "FORMAT comment conflicts with declared units");
        }
        d->decimal = 1;
        return 1;
    }
    if (strncmp(s, "#@!", 3)) {
        return 1;                   /* an ordinary comment */
    }
    s += 3;
    while (*s == ' ') {
        s++;
    }
    if (strlen(s) < 2 || s[0] != 'T') {
        return drill_fail(d, TIKU_CIRCUIT_UNSUPPORTED,
                          "Unknown drill attribute command");
    }
    char kind = s[1];

    s += 2;
    if (kind == 'D') {
        /* TD with no name clears the tool and object scopes; the file
         * scope stays. */
        if (strchr(s, ',')) {
            return drill_fail(d, TIKU_CIRCUIT_BAD,
                              "Invalid attribute deletion");
        }
        for (int i = 0; i < DRILL_ATTRS; i++) {
            if (!*s || !strcmp(s, d->aperture[i].name)) {
                memset(&d->aperture[i], 0, sizeof d->aperture[i]);
            }
            if (!*s || !strcmp(s, d->object[i].name)) {
                memset(&d->object[i], 0, sizeof d->object[i]);
            }
        }
        return 1;
    }
    drill_attribute_t *attrs = kind == 'F' ? d->file
                             : kind == 'A' ? d->aperture
                             : kind == 'O' ? d->object : NULL;

    if (!attrs) {
        return drill_fail(d, TIKU_CIRCUIT_UNSUPPORTED,
                          "Unknown drill attribute command");
    }
    if (kind == 'F' && d->doc->count) {
        return drill_fail(d, TIKU_CIRCUIT_BAD,
                          "File attributes must precede geometry");
    }
    const char *comma = strchr(s, ',');
    size_t name = comma ? (size_t)(comma - s) : strlen(s);
    const char *value = comma ? comma + 1 : "";

    if (!name) {
        return drill_fail(d, TIKU_CIRCUIT_BAD, "Empty attribute name");
    }
    if (name >= sizeof attrs[0].name
        || strlen(value) >= sizeof attrs[0].value) {
        return drill_fail(d, TIKU_CIRCUIT_LIMIT,
                          "Drill attribute exceeds text limit");
    }
    int slot = -1;

    /* The same name replaces its value; otherwise the first free slot. */
    for (int i = 0; i < DRILL_ATTRS; i++) {
        if (strlen(attrs[i].name) == name
            && !strncmp(attrs[i].name, s, name)) {
            slot = i;
            break;
        }
        if (!*attrs[i].name && slot < 0) {
            slot = i;
        }
    }
    if (slot < 0) {
        return drill_fail(d, TIKU_CIRCUIT_LIMIT,
                          "Too many drill attributes");
    }
    memcpy(attrs[slot].name, s, name);
    attrs[slot].name[name] = 0;
    snprintf(attrs[slot].value, sizeof attrs[slot].value, "%s", value);
    return 1;
}
