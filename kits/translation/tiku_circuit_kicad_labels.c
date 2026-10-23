/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_circuit_kicad_labels.c - hierarchy connection markers: a
 * hierarchical label inside a sheet and the matching pin on the sheet's
 * outline, each a text-height shape at the connection point with its
 * text upright beside it.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_circuit_kicad_private.h"

#include <math.h>
#include <string.h>

/* Points of the largest label shape, and text heights from a label's
 * anchor to its text. */
#define LABEL_POINTS   5
#define LABEL_TEXT_GAP 1.15

int
kc_hlabel(circuit_kc_t *k, size_t n, int sheet_pin)
{
    if (!kc_fields(k, n, "shape at effects uuid fields_autoplaced", "",
                   sheet_pin ? 2 : 1, "")) {
        return 0;
    }
    const char *text = kc_arg(k, n, 0);
    const char *shape = sheet_pin ? kc_arg(k, n, 1)
                                  : kc_arg(k, kc_child(k, n, "shape"), 0);

    if (!text || !*text || !shape) {
        return kc_fail(k, n, TIKU_CIRCUIT_BAD,
                       "Missing hierarchy label or shape");
    }
    int input = !strcmp(shape, "input"), output = !strcmp(shape, "output");
    int diamond = !strcmp(shape, "bidirectional") ||
                  !strcmp(shape, "tri_state");
    int passive = !strcmp(shape, "passive");

    if (!input && !output && !diamond && !passive) {
        return kc_fail(k, n, TIKU_CIRCUIT_UNSUPPORTED,
                       "Unknown hierarchy label shape");
    }
    size_t effects = kc_child(k, n, "effects");
    size_t font = kc_child(k, effects, "font");
    size_t size = kc_child(k, font, "size");
    size_t justify = kc_child(k, effects, "justify");

    if (!kc_fields(k, effects, "font justify", "", 0, "") ||
        !kc_fields(k, font, "size bold italic", "", 0, "bold italic")) {
        return 0;
    }
    double h, w, x, y, angle;

    if (!kc_number(k, size, 0, &h) || !kc_number(k, size, 1, &w) ||
        !kc_at(k, n, &x, &y, &angle)) {
        return 0;
    }
    if (h <= 0 || h > KC_TEXT_MAX || h != w || fmod(angle, 90)) {
        return kc_fail(k, n, TIKU_CIRCUIT_UNSUPPORTED,
                       "Hierarchy labels require square text size and "
                       "right-angle rotation");
    }
    int just = 0;

    if (justify != SX_NONE) {
        if (!kc_fields(k, justify, "", "", 0, "left right")) {
            return 0;
        }
        if (kc_flag(k, justify, 0, "left")) {
            just = -1;
        }
        if (kc_flag(k, justify, 0, "right")) {
            just = 1;
        }
    }
    /* A label's body is a text-height square beside the connection point.
     * A sheet pin points the other way and swaps input for output. */
    circuit_point_t p[LABEL_POINTS];
    size_t count = 0;

    if (diamond) {
        p[0] = (circuit_point_t){0, 0};
        p[1] = (circuit_point_t){h / 2, h / 2};
        p[2] = (circuit_point_t){h, 0};
        p[3] = (circuit_point_t){h / 2, -h / 2};
        count = 4;
    } else if (passive) {
        p[0] = (circuit_point_t){0, -h / 2};
        p[1] = (circuit_point_t){h, -h / 2};
        p[2] = (circuit_point_t){h, h / 2};
        p[3] = (circuit_point_t){0, h / 2};
        count = 4;
    } else {
        p[0] = (circuit_point_t){0, 0};
        p[1] = (circuit_point_t){h / 2, h / 2};
        p[2] = (circuit_point_t){h, h / 2};
        p[3] = (circuit_point_t){h, -h / 2};
        p[4] = (circuit_point_t){h / 2, -h / 2};
        count = 5;
        if (output != sheet_pin) {
            for (size_t i = 0; i < count; i++) {
                p[i].x = h - p[i].x;
            }
        }
    }
    double dx = cos(angle * M_PI / 180), dy = -sin(angle * M_PI / 180);

    if (sheet_pin) {
        dx = -dx;
        dy = -dy;
    }
    for (size_t i = 0; i < count; i++) {
        double a = p[i].x, b = p[i].y;

        p[i] = (circuit_point_t){x + dx * a - dy * b, y + dy * a + dx * b};
    }
    if (!kc_result(k, n, circuit_add(k->doc, text, shape, p, count,
                                     KC_STROKE, KC_INK, 1, 0))) {
        return 0;
    }
    return kc_text_at(k, n, text, shape, text, x + dx * h * LABEL_TEXT_GAP,
                      y + dy * h * LABEL_TEXT_GAP, fabs(dx) > .5 ? 0 : 90,
                      h, just, 0, kc_flag(k, font, 0, "bold"),
                      kc_flag(k, font, 0, "italic"));
}
