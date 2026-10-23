/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_circuit_kicad_global.c - global net labels: a tag whose shape
 * shows the signal's direction, connected at its anchor, drawn round the
 * substitute font's upright text, with its visible literal properties.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_circuit_kicad_private.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/* A tag's outline in text heights: how far its point runs, the space
 * either side of the text, and how far the text box is lifted.  Then
 * the outline's points, and the bytes of a label's description. */
#define TAG_POINT     0.875
#define TAG_MARGIN    0.5
#define TAG_RAISE     0.15
#define TAG_CORNERS   6
#define LABEL_DETAILS 768

int
kc_glabel(circuit_kc_t *k, size_t n)
{
    if (!kc_fields(k, n, "shape at effects uuid fields_autoplaced property",
                   "property", 1, "")) {
        return 0;
    }
    const char *text = kc_arg(k, n, 0);
    const char *shape = kc_arg(k, kc_child(k, n, "shape"), 0);

    if (!text || !*text || !shape) {
        return kc_fail(k, n, TIKU_CIRCUIT_BAD,
                       "Missing global label or shape");
    }
    int input = !strcmp(shape, "input"), output = !strcmp(shape, "output");
    int both = !strcmp(shape, "bidirectional") ||
               !strcmp(shape, "tri_state");

    if (!input && !output && !both && strcmp(shape, "passive")) {
        return kc_fail(k, n, TIKU_CIRCUIT_UNSUPPORTED,
                       "Unknown global label shape");
    }
    if (strchr(text, '\n') || strchr(text, '\r')) {
        return kc_fail(k, n, TIKU_CIRCUIT_UNSUPPORTED,
                       "Multiline global labels are not supported");
    }
    size_t effects = kc_child(k, n, "effects");
    size_t font = kc_child(k, effects, "font");
    size_t size = kc_child(k, font, "size");
    size_t justify = kc_child(k, effects, "justify");

    if (!kc_fields(k, effects, "font justify", "", 0, "") ||
        !kc_fields(k, font, "size bold italic", "", 0, "bold italic") ||
        !kc_fields(k, size, "", "", 2, "") ||
        !kc_fields(k, kc_child(k, n, "shape"), "", "", 1, "") ||
        (justify != SX_NONE &&
         !kc_fields(k, justify, "", "", 0, "left right"))) {
        return 0;
    }
    double h, w, x, y, angle;

    if (!kc_number(k, size, 0, &h) || !kc_number(k, size, 1, &w) ||
        !kc_at(k, n, &x, &y, &angle)) {
        return 0;
    }
    if (h <= 0 || h > KC_TEXT_MAX || h != w || fmod(angle, 90)) {
        return kc_fail(k, n, TIKU_CIRCUIT_UNSUPPORTED,
                       "Global labels require square text size and "
                       "right-angle rotation");
    }
    if (!kc_font(k, n)) {
        return 0;
    }
    double width = circuit_text_line_width(k->doc, text) * h;

    if (width < 0) {
        return kc_fail(k, n, TIKU_CIRCUIT_UNSUPPORTED,
                       "Global label contains an unavailable glyph");
    }
    /* An input tag's point sits on the anchor and an output's at the far
     * end; a bidirectional or tri-state tag has both.  The body's length
     * follows the substitute font's text, so it can differ from the
     * length KiCad draws. */
    double near = (input || both) ? TAG_POINT * h : 0;
    double far = (output || both) ? TAG_POINT * h : 0;
    double margin = TAG_MARGIN * h, length = near + far + width + 2 * margin;
    circuit_point_t p[TAG_CORNERS] = {{0, 0}, {near, h}, {length - far, h},
                                      {length, 0}, {length - far, -h},
                                      {near, -h}};
    double dx = cos(angle * M_PI / 180), dy = -sin(angle * M_PI / 180);

    for (size_t i = 0; i < TAG_CORNERS; i++) {
        double a = p[i].x, b = p[i].y;

        p[i] = (circuit_point_t){x + dx * a - dy * b, y + dy * a + dx * b};
    }
    size_t first = k->doc->count;

    if (!kc_result(k, n, circuit_add(k->doc, text, "Global label", p,
                                     TAG_CORNERS, KC_STROKE, KC_INK, 1,
                                     0))) {
        return 0;
    }
    double middle = near + margin + width / 2;
    double tx = x + dx * middle, ty = y + dy * middle;
    int vertical = fabs(dx) < .5;

    /* The text box reaches lower than the glyphs: lift it to centre the
     * glyphs in the tag, kept upright. */
    if (vertical) {
        tx -= TAG_RAISE * h;
    } else {
        ty -= TAG_RAISE * h;
    }
    if (!kc_text_at(k, n, text, "Global label", text, tx, ty,
                    vertical ? 90 : 0, h, 0, 0, kc_flag(k, font, 0, "bold"),
                    kc_flag(k, font, 0, "italic"))) {
        return 0;
    }
    for (size_t i = n + 3; i < k->sx.node[n].end; i = k->sx.node[i].end) {
        const char *head = circuit_sx_head(&k->sx, i);

        if (head && !strcmp(head, "property") &&
            !kc_text(k, i, KC_IDENTITY, text, "Global label",
                     kc_arg(k, i, 1), 0)) {
            return 0;
        }
    }
    char details[LABEL_DETAILS];
    int bytes = snprintf(details, sizeof details,
                         "Global label\nName: %s\nShape: %s\n"
                         "Position: %.4g, %.4g mm\nRotation: %.4g degrees",
                         text, shape, x, y, angle);

    if (bytes < 0 || bytes >= (int)sizeof details) {
        return kc_fail(k, n, TIKU_CIRCUIT_LIMIT,
                       "Global label metadata is too long");
    }
    for (size_t i = first; i < k->doc->count; i++) {
        if (!kc_result(k, n, circuit_details(k->doc, i, details))) {
            return 0;
        }
    }
    return !k->error;
}
