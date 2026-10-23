/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_circuit_kicad_instances.c - the reference, unit or page an object
 * takes in this instance of its sheet, chosen by the sheet's UUID path.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_circuit_kicad_private.h"

#include <string.h>

int
kc_uuid(const char *s)
{
    if (!s || strlen(s) != KC_UUID_BYTES - 1) {
        return 0;
    }
    for (size_t i = 0; i < KC_UUID_BYTES - 1; i++) {
        /* the dashes of the 8-4-4-4-12 form */
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (s[i] != '-') {
                return 0;
            }
        } else if (!((s[i] >= '0' && s[i] <= '9') ||
                     (s[i] >= 'a' && s[i] <= 'f') ||
                     (s[i] >= 'A' && s[i] <= 'F'))) {
            return 0;
        }
    }
    return 1;
}

size_t
kc_instance(circuit_kc_t *k, size_t n, const char *fields)
{
    size_t instances = kc_child(k, n, "instances");

    if (!k->instance || instances == SX_NONE) {
        return SX_NONE;
    }
    if (!kc_fields(k, instances, "project", "project", 0, "")) {
        return SX_NONE;
    }
    size_t found = SX_NONE, preferred = SX_NONE, matches = 0;

    for (size_t p = instances + 2; p < k->sx.node[instances].end;
         p = k->sx.node[p].end) {
        if (!kc_fields(k, p, "path", "path", 1, "")) {
            return SX_NONE;
        }
        const char *project = kc_arg(k, p, 0);

        for (size_t i = p + 3; i < k->sx.node[p].end;
             i = k->sx.node[i].end) {
            if (!kc_fields(k, i, fields, "", 1, "")) {
                return SX_NONE;
            }
            const char *path = kc_arg(k, i, 0);

            if (strcmp(path, k->instance)) {
                continue;
            }
            found = i;
            matches++;
            if (k->project && !strcmp(project, k->project)) {
                if (preferred != SX_NONE) {
                    kc_fail(k, i, TIKU_CIRCUIT_BAD,
                            "Duplicate reference for this sheet instance");
                    return SX_NONE;
                }
                preferred = i;
            }
        }
    }
    /* An entry under the preferred project wins.  Without one, a single
     * matching entry is taken, as for a renamed root; several fail as
     * MISSING. */
    if (preferred != SX_NONE) {
        return preferred;
    }
    if (matches == 1) {
        return found;
    }
    kc_fail(k, n, TIKU_CIRCUIT_MISSING,
            matches ? "Ambiguous sheet instance; open its original project "
                      "root"
                    : "No matching sheet instance; open the project root "
                      "schematic");
    return SX_NONE;
}
