/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_circuit_gerber_attributes.c - X2 attributes in their file,
 * aperture and object scopes, kept as bounded text for inspection, and
 * the details each object carries.  Only LPD and LPC set an object's
 * polarity; a polarity attribute is kept as text.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_circuit_gerber_private.h"

#include <stdio.h>
#include <string.h>

int
gerber_attributes(circuit_gerber_t *g, const gerber_attribute_t *attrs,
                  char out[GERBER_ATTRIBUTE_TEXT])
{
    size_t used = 0;

    out[0] = 0;
    for (int i = 0; i < GERBER_ATTRIBUTES; i++) {
        if (*attrs[i].name) {
            int n = snprintf(out + used, GERBER_ATTRIBUTE_TEXT - used,
                             "%s: %s\n", attrs[i].name, attrs[i].value);

            if (n < 0 || (size_t)n >= GERBER_ATTRIBUTE_TEXT - used) {
                return gerber_fail(g, TIKU_CIRCUIT_LIMIT,
                                   "Too much Gerber attribute text");
            }
            used += (size_t)n;
        }
    }
    return 1;
}

int
gerber_attribute(circuit_gerber_t *g, const char *s)
{
    if (g->region || strlen(s) < 2 || *s != 'T') {
        return gerber_fail(g, TIKU_CIRCUIT_BAD, "Invalid attribute command");
    }
    int kind = s[1];

    s += 2;
    if (kind == 'D') {
        /* TD with no name clears the aperture and object scopes; the
         * file scope stays. */
        if (strchr(s, ',')) {
            return gerber_fail(g, TIKU_CIRCUIT_BAD,
                               "Invalid attribute deletion");
        }
        for (int i = 0; i < GERBER_ATTRIBUTES; i++) {
            if (!*s || !strcmp(s, g->tool[i].name)) {
                memset(&g->tool[i], 0, sizeof g->tool[i]);
            }
            if (!*s || !strcmp(s, g->object[i].name)) {
                memset(&g->object[i], 0, sizeof g->object[i]);
            }
        }
        return 1;
    }
    gerber_attribute_t *attrs = kind == 'F' ? g->file
                              : kind == 'A' ? g->tool
                              : kind == 'O' ? g->object : NULL;

    if (!attrs) {
        return gerber_fail(g, TIKU_CIRCUIT_UNSUPPORTED,
                           "Unknown Gerber attribute command");
    }
    if (kind == 'F' && g->doc->count) {
        return gerber_fail(g, TIKU_CIRCUIT_BAD,
                           "File attributes must precede geometry");
    }
    const char *comma = strchr(s, ',');
    size_t n = comma ? (size_t)(comma - s) : strlen(s);
    const char *value = comma ? comma + 1 : "";

    if (!n) {
        return gerber_fail(g, TIKU_CIRCUIT_BAD, "Empty attribute name");
    }
    if (n >= sizeof attrs[0].name || strlen(value) >= sizeof attrs[0].value) {
        return gerber_fail(g, TIKU_CIRCUIT_LIMIT,
                           "Gerber attribute text exceeds limit");
    }
    int slot = -1;

    /* The same name replaces its value; otherwise the first free slot. */
    for (int i = 0; i < GERBER_ATTRIBUTES; i++) {
        if (strlen(attrs[i].name) == n && !strncmp(attrs[i].name, s, n)) {
            slot = i;
            break;
        }
        if (!*attrs[i].name && slot < 0) {
            slot = i;
        }
    }
    if (slot < 0) {
        return gerber_fail(g, TIKU_CIRCUIT_LIMIT,
                           "Too many Gerber attributes");
    }
    memcpy(attrs[slot].name, s, n);
    attrs[slot].name[n] = 0;
    snprintf(attrs[slot].value, sizeof attrs[slot].value, "%s", value);
    return 1;
}

int
gerber_details(circuit_gerber_t *g, const char *description,
               const char *attrs)
{
    char file[GERBER_ATTRIBUTE_TEXT], object[GERBER_ATTRIBUTE_TEXT];
    char text[CIRCUIT_DETAILS_MAX + 1];

    if (!gerber_attributes(g, g->file, file)
        || !gerber_attributes(g, g->object, object)) {
        return 0;
    }
    int n = snprintf(text, sizeof text,
                     "Operation polarity: %s\n"
                     "%s\n"
                     "Source units: %s\n"
                     "Source line: %u\n"
                     "File attributes:\n"
                     "%sAperture attributes:\n"
                     "%sObject attributes:\n"
                     "%s",
                     g->clear ? "clear" : "dark", description,
                     g->unit == 1 ? "mm" : "inch", g->line, file, attrs,
                     object);

    if (n < 0 || (size_t)n >= sizeof text) {
        return gerber_fail(g, TIKU_CIRCUIT_LIMIT,
                           "Combined Gerber details exceed 4096 bytes");
    }
    g->doc->shapes[g->doc->count - 1].clear = g->clear;
    tiku_circuit_err_t e = circuit_details(g->doc, g->doc->count - 1, text);

    return e ? gerber_fail(g, e, "Cannot store Gerber details") : 1;
}
