/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_circuit_kicad_text.c - KiCad text in document coordinates: free
 * text and labels, a library symbol's own text and placed properties,
 * each sized, aligned and slanted as its effects say and drawn in the
 * substitute font the application supplies.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_circuit_kicad_private.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* How far an italic glyph leans: run per unit of rise. */
#define ITALIC_SLANT 0.2

int
kc_font(circuit_kc_t *k, size_t n)
{
    if (k->doc->font) {
        return 1;
    }
    if (!k->options || !k->options->font_data || !k->options->font_bytes) {
        return kc_fail(k, n, TIKU_CIRCUIT_MISSING,
                       "A substitute TrueType font is required for visible "
                       "text");
    }
    if (k->options->font_bytes > CIRCUIT_FONT_MAX) {
        return kc_fail(k, n, TIKU_CIRCUIT_LIMIT,
                       "Substitute font exceeds 8 MiB");
    }
    k->doc->font = tiku_ttf_open_mem(k->options->font_data,
                                     k->options->font_bytes);
    if (k->doc->font) {
        return 1;
    }
    /* The font reader fails alike for bytes it cannot read and for want
     * of memory to copy them; an allocation of the copy's size tells the
     * two apart. */
    void *copy = malloc(k->options->font_bytes);

    if (!copy) {
        return kc_fail(k, n, TIKU_CIRCUIT_NOMEM,
                       "Not enough memory to copy the substitute font");
    }
    free(copy);
    return kc_fail(k, n, TIKU_CIRCUIT_BAD,
                   "Cannot read the supplied TrueType font");
}

/** @brief Note circuit_text()'s result @p code at @p n; a glyph the font
 *         lacks gets a message of its own.  @return 1 for OK, else 0 */
static int
text_result(circuit_kc_t *k, size_t n, tiku_circuit_err_t code)
{
    if (code == TIKU_CIRCUIT_UNSUPPORTED) {
        return kc_fail(k, n, code,
                       "Text contains a character the substitute font "
                       "lacks");
    }
    return kc_result(k, n, code);
}

int
kc_text_at(circuit_kc_t *k, size_t n, const char *name, const char *value,
           const char *text, double x, double y, double angle, double height,
           int justify, int vertical, int bold, int italic)
{
    if (!text || !*text) {
        return 1;
    }
    if (!kc_font(k, n)) {
        return 0;
    }
    if (strstr(text, "${") || strstr(text, "~{") || strstr(text, "^{") ||
        strstr(text, "_{")) {
        return kc_fail(k, n, TIKU_CIRCUIT_UNSUPPORTED,
                       "Text variables, overbars and scripted text are not "
                       "supported yet");
    }
    double c = cos(angle * M_PI / 180), s = sin(angle * M_PI / 180);
    double m[] = {height * c, -height * s, height * s, height * c, x, y};

    if (italic) {
        m[2] -= ITALIC_SLANT * m[0];
        m[3] -= ITALIC_SLANT * m[1];
    }
    return text_result(k, n, circuit_text(k->doc, name, value, text, m,
                                          justify, vertical, bold, KC_INK));
}

/**
 * @brief Append text object @p n showing @p text: free text and labels
 *        through @p parent, a library's own text when @p symbol_text, or
 *        a placed symbol's property when @p placed.
 *
 * Hidden or empty text appends nothing.
 *
 * @return 1, or 0 with the failure noted
 */
static int
text_object(circuit_kc_t *k, size_t n, const double parent[6],
            const char *name, const char *value, const char *text,
            int symbol_text, int placed)
{
    const char *head = circuit_sx_head(&k->sx, n);
    unsigned args = head && !strcmp(head, "property") ? 2 : 1;

    if (!kc_fields(k, n, "at effects uuid id hide show_name do_not_autoplace "
                   "fields_autoplaced exclude_from_sim", "", args, "hide")) {
        return 0;
    }
    size_t effects = kc_child(k, n, "effects");

    if (effects != SX_NONE &&
        !kc_fields(k, effects, "font justify hide", "", 0, "hide")) {
        return 0;
    }
    if (kc_flag(k, n, args, "hide") || kc_flag(k, effects, 0, "hide") ||
        !text || !*text) {
        return 1;
    }
    double x, y, angle, height = KC_TEXT_SIZE, width = KC_TEXT_SIZE;

    if (!kc_at(k, n, &x, &y, &angle)) {
        return 0;
    }
    size_t face = kc_child(k, effects, "font");
    size_t size = kc_child(k, face, "size");

    if (face != SX_NONE &&
        !kc_fields(k, face, "face size thickness bold italic line_spacing",
                   "", 0, "bold italic")) {
        return 0;
    }
    if (size != SX_NONE && (!kc_number(k, size, 0, &height) ||
                            !kc_number(k, size, 1, &width))) {
        return 0;
    }
    if (height <= 0 || width <= 0 || height > KC_TEXT_MAX ||
        width > KC_TEXT_MAX) {
        return kc_fail(k, size, TIKU_CIRCUIT_BAD,
                       "Text size outside supported range");
    }
    if (kc_child(k, face, "line_spacing") != SX_NONE) {
        return kc_fail(k, face, TIKU_CIRCUIT_UNSUPPORTED,
                       "Custom text line spacing is not supported yet");
    }
    if (kc_flag(k, n, args, "show_name")) {
        return kc_fail(k, n, TIKU_CIRCUIT_UNSUPPORTED,
                       "Combined property name/value text is not supported "
                       "yet");
    }
    int justify = 0, vertical = 0;
    size_t j = kc_child(k, effects, "justify");

    if (j != SX_NONE) {
        if (!kc_fields(k, j, "", "", 0, "left right top bottom")) {
            return 0;
        }
        if (kc_flag(k, j, 0, "left")) {
            justify = -1;
        }
        if (kc_flag(k, j, 0, "right")) {
            justify = 1;
        }
        if (kc_flag(k, j, 0, "top")) {
            vertical = -1;
        }
        if (kc_flag(k, j, 0, "bottom")) {
            vertical = 1;
        }
    }
    if (strstr(text, "${") || strstr(text, "~{") || strstr(text, "^{") ||
        strstr(text, "_{")) {
        return kc_fail(k, n, TIKU_CIRCUIT_UNSUPPORTED,
                       "Text variables, overbars and scripted text are not "
                       "supported yet");
    }
    if (!kc_font(k, n)) {
        return 0;
    }
    if (placed && fmod(angle, 90)) {
        return kc_fail(k, n, TIKU_CIRCUIT_UNSUPPORTED,
                       "Symbol properties require right-angle text rotation");
    }
    if (!placed && !symbol_text) {
        /* KiCad 7 reads free text's angle by its whole degrees: 90 and
         * 270 read upward, any other angle left to right, each with the
         * alignment as stored. */
        int whole = (int)angle;

        angle = whole == 90 || whole == 270 ? 90 : 0;
    }
    /* KiCad 7 draws a library's own text across the sheet when the symbol
     * stands upright or upside down and the text's angle is zero, or when
     * the symbol is turned a quarter and the angle is anything else; up
     * the sheet otherwise. */
    int across = (fabs(parent[0]) > .5) == (angle == 0);

    /* A library's own text keeps its angle in tenths of a degree. */
    angle *= M_PI / (symbol_text ? 1800 : 180);
    double c = cos(angle), s = sin(angle), ysign = symbol_text ? -1 : 1;
    circuit_point_t origin = placed ? (circuit_point_t){x, y}
                                    : kc_transform(parent, x, y);
    double rx = c, ry = -ysign * s, dx = s, dy = ysign * c;
    double m[] = {width * (parent[0] * rx + parent[2] * ry),
                  width * (parent[1] * rx + parent[3] * ry),
                  height * (parent[0] * dx + parent[2] * dy),
                  height * (parent[1] * dx + parent[3] * dy),
                  origin.x, origin.y};

    if (symbol_text) {
        /* Centred on the box the text would fill turned by its angle,
         * which is where KiCad puts it. */
        double em, lines;

        if (!circuit_text_extent(k->doc, text, &em, &lines)) {
            return text_result(k, n, TIKU_CIRCUIT_UNSUPPORTED);
        }
        double cx = -justify * em / 2, cy = -vertical * lines / 2;

        m[4] += m[0] * cx + m[2] * cy;
        m[5] += m[1] * cx + m[3] * cy;
        justify = 0;
        vertical = 0;
        m[0] = across ? width : 0;
        m[1] = across ? 0 : -width;
        m[2] = across ? 0 : height;
        m[3] = across ? height : 0;
    }
    if (placed) {
        /* A property's position is on the sheet, but its direction is
         * relative to the symbol: undo the library's y-up conversion
         * before rotating it.  The glyphs stay upright; the alignment, not
         * the letters, is reflected, so the text keeps the box KiCad gives
         * it. */
        double vx = parent[0] * c + parent[2] * s;
        double vy = parent[1] * c + parent[3] * s;
        double downx = parent[0] * s - parent[2] * c;
        double downy = parent[1] * s - parent[3] * c;
        int v = fabs(vx) < .5;

        if (vx < -.5 || vy > .5) {
            justify = -justify;
        }
        if ((v ? downx : downy) < 0) {
            vertical = -vertical;
        }
        m[0] = v ? 0 : width;
        m[1] = v ? -width : 0;
        m[2] = v ? height : 0;
        m[3] = v ? 0 : height;
    }
    if (kc_flag(k, face, 0, "italic")) {
        m[2] -= ITALIC_SLANT * m[0];
        m[3] -= ITALIC_SLANT * m[1];
    }
    return text_result(k, n, circuit_text(k->doc, name, value, text, m,
                                          justify, vertical,
                                          kc_flag(k, face, 0, "bold"), KC_INK));
}

int
kc_text(circuit_kc_t *k, size_t n, const double m[6], const char *name,
        const char *value, const char *text, int symbol_text)
{
    return text_object(k, n, m, name, value, text, symbol_text, 0);
}

int
kc_property_text(circuit_kc_t *k, size_t n, const double m[6],
                 const char *name, const char *value, const char *text)
{
    return text_object(k, n, m, name, value, text, 0, 1);
}
