/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_circuit_kicad_symbols.c - placed symbols: the embedded library
 * definition found, its selected unit and body style drawn through the
 * placement's rotation and mirroring, its visible pins and properties,
 * the do-not-populate cross, and each object's description.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_circuit_kicad_private.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define KC_PIN_OFFSET  0.508    /* KiCad's default pin-name offset, mm   */
#define KC_PIN_GAP     0.4      /* mm between a pin and its labels       */
#define KC_BUBBLE      0.635    /* an inverted pin's circle radius, mm   */
#define KC_CLOCK_HALF  0.635    /* half a clock wedge's width, mm        */
#define KC_CLOCK_DEPTH 1.27     /* a clock wedge's depth, mm             */
#define UNIT_MAX       100      /* units one symbol may have             */
#define STYLE_MAX      2        /* body styles: normal and De Morgan     */
#define UNIT_LETTERS   26       /* unit suffixes run A to Z, then AA     */
#define SUFFIX_BYTES   8        /* letters of a unit suffix              */

/** @brief The library definition named @p id, which must be there once.
 *  @return its node, or SX_NONE with the failure noted */
static size_t
definition(circuit_kc_t *k, size_t placed, const char *id)
{
    size_t lib = k->libraries, found = SX_NONE;

    if (lib == SX_NONE || !id) {
        kc_fail(k, placed, TIKU_CIRCUIT_MISSING,
                "Missing embedded symbol library or identifier");
        return SX_NONE;
    }
    for (size_t n = lib + 2; n < k->sx.node[lib].end;
         n = k->sx.node[n].end) {
        if (!kc_step(k, n)) {
            return SX_NONE;
        }
        const char *name = kc_arg(k, n, 0);

        if (!name) {
            kc_fail(k, n, TIKU_CIRCUIT_BAD,
                    "Invalid embedded symbol definition");
            return SX_NONE;
        }
        if (!strcmp(name, id)) {
            if (found != SX_NONE) {
                kc_fail(k, n, TIKU_CIRCUIT_BAD,
                        "Duplicate embedded symbol identifier");
                return SX_NONE;
            }
            found = n;
        }
    }
    if (found == SX_NONE) {
        kc_fail(k, placed, TIKU_CIRCUIT_MISSING,
                "Placed symbol has no embedded definition");
    }
    return found;
}

/** @brief The value of @p n's property @p key, or NULL when it has none;
 *         a key given twice is noted as BAD. */
static const char *
property(circuit_kc_t *k, size_t n, const char *key)
{
    const char *found = NULL;

    for (size_t i = n + 2; i < k->sx.node[n].end; i = k->sx.node[i].end) {
        const char *head = circuit_sx_head(&k->sx, i);
        const char *name = kc_arg(k, i, 0);

        if (head && !strcmp(head, "property") && name &&
            !strcmp(name, key)) {
            if (found) {
                kc_fail(k, i, TIKU_CIRCUIT_BAD, "Duplicate symbol property");
                return NULL;
            }
            found = kc_arg(k, i, 1);
        }
    }
    return found;
}

/** @brief The text size of pin name or number @p n, from its own effects:
 *         square and in range, 1.27 mm when unset.  @return the size, or
 *         0 with the failure noted */
static double
pin_size(circuit_kc_t *k, size_t n)
{
    double value = KC_TEXT_SIZE;
    size_t effects = kc_child(k, n, "effects");
    size_t face = kc_child(k, effects, "font");

    if (!kc_fields(k, n, "effects", "", 1, "") ||
        (effects != SX_NONE && !kc_fields(k, effects, "font", "", 0, "")) ||
        (face != SX_NONE && !kc_fields(k, face, "size", "", 0, ""))) {
        return 0;
    }
    size_t size = kc_child(k, face, "size");

    if (size != SX_NONE) {
        double width;

        if (!kc_number(k, size, 0, &value) ||
            !kc_number(k, size, 1, &width)) {
            return 0;
        }
        if (value <= 0 || value > KC_TEXT_MAX || width != value) {
            kc_fail(k, n, TIKU_CIRCUIT_UNSUPPORTED,
                    "Invalid or unequal pin text dimensions");
            return 0;
        }
    }
    return value;
}

/** @brief Append pin @p n of library symbol @p lib through @p m: its line
 *         in a supported style, and the number and name the library shows.
 *  @return 1, or 0 with the failure noted */
static int
pin(circuit_kc_t *k, size_t n, size_t lib, const double m[6],
    const char *ref)
{
    if (!kc_fields(k, n, "at length name number alternate hide",
                   "alternate", 2, "hide")) {
        return 0;
    }
    if (kc_flag(k, n, 2, "hide")) {
        return 1;
    }
    const char *style = kc_arg(k, n, 1), *electrical = kc_arg(k, n, 0);

    if (!style || !electrical) {
        return kc_fail(k, n, TIKU_CIRCUIT_BAD,
                       "Pin requires electrical and drawing types");
    }
    int bubble = !strcmp(style, "inverted") ||
                 !strcmp(style, "inverted_clock");
    int clock = !strcmp(style, "clock") || !strcmp(style, "inverted_clock");

    if (strcmp(style, "line") && !bubble && !clock) {
        return kc_fail(k, n, TIKU_CIRCUIT_UNSUPPORTED,
                       "This pin drawing style is not supported yet");
    }
    double x, y, angle, length;

    if (!kc_at(k, n, &x, &y, &angle) ||
        !kc_number(k, kc_child(k, n, "length"), 0, &length)) {
        return 0;
    }
    if (length < 0 || length > KC_LENGTH_MAX || fmod(angle, 90) != 0) {
        return kc_fail(k, n, TIKU_CIRCUIT_BAD,
                       "Invalid pin length or direction");
    }
    double dx = cos(angle * M_PI / 180), dy = sin(angle * M_PI / 180);
    circuit_point_t a = kc_transform(m, x, y);
    circuit_point_t b = kc_transform(m, x + dx * length, y + dy * length);
    double vx = m[0] * dx + m[2] * dy, vy = m[1] * dx + m[3] * dy;
    const char *number = kc_arg(k, kc_child(k, n, "number"), 0);
    const char *name = kc_arg(k, kc_child(k, n, "name"), 0);

    if (!number || !name) {
        return kc_fail(k, n, TIKU_CIRCUIT_BAD,
                       "Pin requires a name and number");
    }
    char label[CIRCUIT_NAME_BYTES], value[CIRCUIT_NAME_BYTES];

    if (snprintf(label, sizeof label, "%s pin %s", ref, number) >=
        (int)sizeof label ||
        snprintf(value, sizeof value, "%s (%s)", name, electrical) >=
        (int)sizeof value) {
        return kc_fail(k, n, TIKU_CIRCUIT_LIMIT, "Pin identity is too long");
    }
    double radius = bubble ? KC_BUBBLE : 0;

    if (length < 2 * radius) {
        return kc_fail(k, n, TIKU_CIRCUIT_BAD,
                       "Pin is shorter than its inversion marker");
    }
    circuit_point_t points[] = {a, {b.x - 2 * radius * vx,
                                    b.y - 2 * radius * vy}};

    if (length &&
        !kc_result(k, n, circuit_add(k->doc, label, value, points, 2,
                                     KC_STROKE, KC_INK, 0, 0))) {
        return 0;
    }
    if (bubble &&
        !kc_result(k, n, circuit_arc(k->doc, label, value,
                                     b.x - radius * vx, b.y - radius * vy,
                                     radius, 0, 2 * M_PI, KC_STROKE,
                                     KC_INK, 0))) {
        return 0;
    }
    if (clock) {
        circuit_point_t p[] = {{b.x - vy * KC_CLOCK_HALF,
                                b.y + vx * KC_CLOCK_HALF},
                               {b.x + vx * KC_CLOCK_DEPTH,
                                b.y + vy * KC_CLOCK_DEPTH},
                               {b.x + vy * KC_CLOCK_HALF,
                                b.y - vx * KC_CLOCK_HALF}};

        if (!kc_result(k, n, circuit_add(k->doc, label, value, p, 3,
                                         KC_STROKE, KC_INK, 0, 0))) {
            return 0;
        }
    }
    size_t names = kc_child(k, lib, "pin_names");
    size_t numbers = kc_child(k, lib, "pin_numbers");
    double offset = KC_PIN_OFFSET;
    size_t off = kc_child(k, names, "offset");

    if (off != SX_NONE && !kc_number(k, off, 0, &offset)) {
        return 0;
    }
    if (offset < 0 || offset > KC_LENGTH_MAX) {
        return kc_fail(k, off, TIKU_CIRCUIT_BAD,
                       "Pin label offset outside supported range");
    }
    double text_angle = fabs(vx) > .5 ? 0 : 90;
    double mx = (a.x + b.x) / 2, my = (a.y + b.y) / 2;
    double gx = fabs(vx) < .5 ? KC_PIN_GAP : 0;
    double gy = fabs(vx) > .5 ? KC_PIN_GAP : 0;
    /* With a name offset, the name sits inside the body and the number
     * above or left of the line; with none, the name sits there and the
     * number below or right of it, as KiCad draws them. */
    int inside = offset > 0;

    if (!kc_flag(k, numbers, 0, "hide") && *number) {
        double size = pin_size(k, kc_child(k, n, "number"));
        int side = inside ? -1 : 1;

        if (k->error ||
            !kc_text_at(k, n, label, value, number, mx + side * gx,
                        my + side * gy, text_angle, size, 0, -side, 0, 0)) {
            return 0;
        }
    }
    if (!kc_flag(k, names, 0, "hide") && *name && strcmp(name, "~")) {
        double size = pin_size(k, kc_child(k, n, "name"));

        if (k->error) {
            return 0;
        }
        int just = vx > .5 || vy < -.5 ? -1 : 1;

        if (inside) {
            return kc_text_at(k, n, label, value, name, b.x + vx * offset,
                              b.y + vy * offset, text_angle, size, just, 0,
                              0, 0);
        }
        return kc_text_at(k, n, label, value, name, mx - gx, my - gy,
                          text_angle, size, 0, 1, 0, 0);
    }
    return 1;
}

/** @brief Read a unit definition's name, "<symbol>_<unit>_<style>", for
 *         its unit and body style; the symbol's own name may hold '_'.
 *  @return 1, or 0 for any other form */
static int
unit_id(const char *name, unsigned *unit, unsigned *style)
{
    if (!name) {
        return 0;
    }
    const char *last = strrchr(name, '_'), *before = NULL;

    if (!last || !last[1]) {
        return 0;
    }
    for (const char *p = name; p < last; p++) {
        if (*p == '_') {
            before = p;
        }
    }
    if (!before || before + 1 == last) {
        return 0;
    }
    unsigned a = 0, b = 0;

    for (const char *p = before + 1; p < last; p++) {
        if (*p < '0' || *p > '9' || a > UNIT_MAX) {
            return 0;
        }
        a = a * 10 + (*p - '0');
    }
    for (const char *p = last + 1; *p; p++) {
        if (*p < '0' || *p > '9' || b > STYLE_MAX) {
            return 0;
        }
        b = b * 10 + (*p - '0');
    }
    *unit = a;
    *style = b;
    return a <= UNIT_MAX && b <= STYLE_MAX;
}

/**
 * @brief Append library body @p lib for unit @p unit and body style
 *        @p style, with the parts shared by every unit or style, not
 *        another unit's.
 *
 * @p box grows by the extent a do-not-populate cross covers.
 *
 * @return 1, or 0 with the failure noted
 */
static int
body(circuit_kc_t *k, size_t lib, const double m[6], const char *ref,
     const char *value, unsigned unit, unsigned style,
     tiku_circuit_box_t *box)
{
    if (!kc_fields(k, lib, "property pin_numbers pin_names in_bom on_board "
                   "power symbol description keywords exclude_from_sim "
                   "embedded_fonts", "property symbol", 1, "")) {
        return 0;
    }
    size_t names = kc_child(k, lib, "pin_names");
    size_t numbers = kc_child(k, lib, "pin_numbers");

    if ((names != SX_NONE &&
         !kc_fields(k, names, "offset hide", "", 0, "hide")) ||
        (numbers != SX_NONE &&
         !kc_fields(k, numbers, "hide", "", 0, "hide"))) {
        return 0;
    }
    int found = 0, has_units = 0;

    for (size_t n = lib + 2; n < k->sx.node[lib].end;
         n = k->sx.node[n].end) {
        const char *head = circuit_sx_head(&k->sx, n);

        if (!head || strcmp(head, "symbol")) {
            continue;
        }
        unsigned u, s;

        if (!unit_id(kc_arg(k, n, 0), &u, &s)) {
            return kc_fail(k, n, TIKU_CIRCUIT_BAD,
                           "Invalid embedded symbol unit identifier");
        }
        if (u) {
            has_units = 1;
        }
        if ((u && u != unit) || (s && s != style)) {
            continue;
        }
        if (u == unit) {
            found = 1;
        }
        if (!kc_fields(k, n, "polyline rectangle circle arc text pin "
                       "unit_name", "polyline rectangle circle arc text pin",
                       1, "")) {
            return 0;
        }
        for (size_t i = n + 3; i < k->sx.node[n].end;
             i = k->sx.node[i].end) {
            if (!kc_step(k, i)) {
                return 0;
            }
            const char *kind = circuit_sx_head(&k->sx, i);

            if (!strcmp(kind, "unit_name")) {
                continue;
            }
            size_t first = k->doc->count;

            if (!strcmp(kind, "pin")) {
                if (!pin(k, i, lib, m, ref)) {
                    return 0;
                }
            } else if (!strcmp(kind, "text")) {
                if (!kc_text(k, i, m, ref, value, kc_arg(k, i, 0), 1)) {
                    return 0;
                }
            } else if (!kc_graphic(k, i, m, ref, value, KC_INK)) {
                return 0;
            }
            for (size_t j = first; j < k->doc->count; j++) {
                const circuit_shape_t *p = &k->doc->shapes[j];
                tiku_circuit_box_t b = p->info.bounds;

                if (!strcmp(kind, "pin")) {
                    if (p->kind == CIRCUIT_TEXT) {
                        continue;
                    }
                    /* The cross leaves out a pin's number and name, and
                     * covers its line to the centreline's ends, not the
                     * stroke's edge. */
                    if (p->kind == CIRCUIT_POLYLINE) {
                        b = (tiku_circuit_box_t){INFINITY, INFINITY,
                                                 -INFINITY, -INFINITY};
                        for (size_t q = 0; q < p->count; q++) {
                            b.x0 = fmin(b.x0, p->points[q].x);
                            b.y0 = fmin(b.y0, p->points[q].y);
                            b.x1 = fmax(b.x1, p->points[q].x);
                            b.y1 = fmax(b.y1, p->points[q].y);
                        }
                    }
                }
                box->x0 = fmin(box->x0, b.x0);
                box->y0 = fmin(box->y0, b.y0);
                box->x1 = fmax(box->x1, b.x1);
                box->y1 = fmax(box->y1, b.y1);
            }
        }
    }
    return !has_units || found ||
           kc_fail(k, lib, TIKU_CIRCUIT_MISSING,
                   "Requested symbol unit or body style is absent");
}

/**
 * @brief Reference @p ref as the sheet shows it, into @p out: with the
 *        unit's letter when library symbol @p lib has several units.
 *
 * A reference that does not fit in @p out, unit letters included, is
 * refused.
 *
 * @return 1, or 0 with LIMIT noted
 */
static int
visible_reference(circuit_kc_t *k, size_t n, size_t lib, const char *ref,
                  unsigned unit, char out[CIRCUIT_NAME_BYTES])
{
    if (snprintf(out, CIRCUIT_NAME_BYTES, "%s", ref) >= CIRCUIT_NAME_BYTES) {
        return kc_fail(k, n, TIKU_CIRCUIT_LIMIT, "Reference is too long");
    }
    int multi = 0;

    for (size_t i = lib + 3; i < k->sx.node[lib].end;
         i = k->sx.node[i].end) {
        unsigned lu, ls;
        const char *head = circuit_sx_head(&k->sx, i);

        if (head && !strcmp(head, "symbol") &&
            unit_id(kc_arg(k, i, 0), &lu, &ls) && lu > 1) {
            multi = 1;
        }
    }
    if (multi) {
        char suffix[SUFFIX_BYTES];
        size_t length = 0;

        do {
            unit--;
            suffix[length++] = (char)('A' + unit % UNIT_LETTERS);
            unit /= UNIT_LETTERS;
        } while (unit);
        size_t at = strlen(ref);

        if (at + length >= CIRCUIT_NAME_BYTES) {
            return kc_fail(k, n, TIKU_CIRCUIT_LIMIT,
                           "Reference is too long");
        }
        while (length) {
            out[at++] = suffix[--length];
        }
        out[at] = 0;
    }
    return 1;
}

int
kc_symbol(circuit_kc_t *k, size_t n)
{
    if (!kc_fields(k, n, "lib_id lib_name at unit convert mirror in_bom "
                   "on_board dnp uuid property pin instances "
                   "fields_autoplaced exclude_from_sim", "property pin", 0,
                   "")) {
        return 0;
    }
    int dnp = kc_flag(k, n, 0, "dnp");

    if (k->error) {
        return 0;
    }
    const char *id = kc_arg(k, kc_child(k, n, "lib_name"), 0);

    if (!id) {
        id = kc_arg(k, kc_child(k, n, "lib_id"), 0);
    }
    size_t lib = definition(k, n, id);

    if (lib == SX_NONE) {
        return 0;
    }
    const char *ref = property(k, n, "Reference");
    const char *value = property(k, n, "Value");

    if (!ref || !value || k->error) {
        return kc_fail(k, n, TIKU_CIRCUIT_BAD,
                       "Placed symbol requires Reference and Value");
    }
    double x, y, angle, u = 1, s = 1;

    if (!kc_at(k, n, &x, &y, &angle) ||
        !kc_number(k, kc_child(k, n, "unit"), 0, &u)) {
        return 0;
    }
    size_t instance = kc_instance(k, n, "reference unit");

    if (k->error) {
        return 0;
    }
    if (instance != SX_NONE) {
        size_t reference = kc_child(k, instance, "reference");

        ref = kc_arg(k, reference, 0);
        if (!ref || !*ref || !kc_fields(k, reference, "", "", 1, "") ||
            !kc_number(k, kc_child(k, instance, "unit"), 0, &u)) {
            return kc_fail(k, n, TIKU_CIRCUIT_BAD,
                           "Invalid per-instance symbol reference or unit");
        }
    } else if (k->child) {
        return kc_fail(k, n, TIKU_CIRCUIT_MISSING,
                       "Child symbol has no instance reference table");
    }
    size_t convert = kc_child(k, n, "convert");

    if (convert != SX_NONE && !kc_number(k, convert, 0, &s)) {
        return 0;
    }
    if (u < 1 || u > UNIT_MAX || floor(u) != u || s < 1 || s > STYLE_MAX ||
        floor(s) != s || fmod(angle, 90) != 0) {
        return kc_fail(k, n, TIKU_CIRCUIT_BAD,
                       "Invalid symbol unit, body style, or rotation");
    }
    double c = cos(angle * M_PI / 180), sn = sin(angle * M_PI / 180);
    double m[] = {c, -sn, -sn, -c, x, y};
    const char *mirror = kc_arg(k, kc_child(k, n, "mirror"), 0);

    if (mirror) {
        /* KiCad applies these sheet-axis reflections after rotation. */
        if (!strcmp(mirror, "x")) {
            m[1] = -m[1];
            m[3] = -m[3];
        } else if (!strcmp(mirror, "y")) {
            m[0] = -m[0];
            m[2] = -m[2];
        } else {
            return kc_fail(k, n, TIKU_CIRCUIT_UNSUPPORTED,
                           "Unknown symbol mirror axis");
        }
    }
    size_t first = k->doc->count;
    tiku_circuit_box_t box = {INFINITY, INFINITY, -INFINITY, -INFINITY};

    if (!body(k, lib, m, ref, value, (unsigned)u, (unsigned)s, &box)) {
        return 0;
    }
    char visible_ref[CIRCUIT_NAME_BYTES];

    if (!visible_reference(k, n, lib, ref, (unsigned)u, visible_ref)) {
        return 0;
    }
    for (size_t i = n + 2; i < k->sx.node[n].end; i = k->sx.node[i].end) {
        const char *head = circuit_sx_head(&k->sx, i);

        if (!head) {
            continue;
        }
        if (!strcmp(head, "property")) {
            const char *key = kc_arg(k, i, 0);
            const char *text = key && !strcmp(key, "Reference")
                               ? visible_ref : kc_arg(k, i, 1);

            if (!kc_property_text(k, i, m, ref, value, text)) {
                return 0;
            }
        }
        if (!strcmp(head, "pin") && !kc_fields(k, i, "uuid", "", 1, "")) {
            return 0;
        }
    }
    if (dnp) {
        if (!isfinite(box.x0)) {
            return kc_fail(k, n, TIKU_CIRCUIT_UNSUPPORTED,
                           "Cannot mark a do-not-populate symbol without "
                           "visible body or pins");
        }
        for (size_t i = first; i < k->doc->count; i++) {
            circuit_shape_t *p = &k->doc->shapes[i];

            p->colour = p->fill && p->kind != CIRCUIT_TEXT ? KC_DNP_FILL
                                                           : KC_DNP_INK;
        }
        const circuit_point_t a[] = {{box.x0, box.y0}, {box.x1, box.y1}};
        const circuit_point_t b[] = {{box.x1, box.y0}, {box.x0, box.y1}};

        if (!kc_result(k, n, circuit_add(k->doc, ref, value, a, 2,
                                         KC_DNP_WIDTH, KC_DNP_CROSS, 0,
                                         0)) ||
            !kc_result(k, n, circuit_add(k->doc, ref, value, b, 2,
                                         KC_DNP_WIDTH, KC_DNP_CROSS, 0,
                                         0))) {
            return 0;
        }
    }
    const char *footprint = property(k, n, "Footprint");
    const char *datasheet = property(k, n, "Datasheet");
    const char *bom = kc_child(k, n, "in_bom") == SX_NONE ? "Not specified"
                      : kc_flag(k, n, 0, "in_bom") ? "Yes" : "No";
    const char *pcb = kc_child(k, n, "on_board") == SX_NONE
                      ? "Not specified"
                      : kc_flag(k, n, 0, "on_board") ? "Yes" : "No";

    if (k->error) {
        return 0;
    }
    char details[CIRCUIT_DETAILS_MAX + 1], selected[CIRCUIT_DETAILS_MAX + 1];
    int bytes = snprintf(details, sizeof details,
                         "Component: %s\nReference: %s\nValue: %s\n"
                         "Library: %s\nUnit: %u; body style: %u\n"
                         "Position: %.4g, %.4g mm\n"
                         "Rotation: %.4g degrees; mirror: %s\n"
                         "Do not populate: %s\nInclude in BOM: %s\n"
                         "Include on PCB: %s\nFootprint: %s\nDatasheet: %s",
                         visible_ref, ref, value, id, (unsigned)u,
                         (unsigned)s, x, y, angle, mirror ? mirror : "None",
                         dnp ? "Yes" : "No", bom, pcb,
                         footprint && *footprint ? footprint
                                                 : "Not specified",
                         datasheet && *datasheet ? datasheet
                                                 : "Not specified");

    if (bytes < 0 || bytes >= (int)sizeof details) {
        return kc_fail(k, n, TIKU_CIRCUIT_LIMIT,
                       "Component metadata exceeds the inspection limit");
    }
    for (size_t i = first; i < k->doc->count; i++) {
        const tiku_circuit_object_t *p = &k->doc->shapes[i].info;
        const char *info = details;

        if (strcmp(p->name, ref)) {
            int length = snprintf(selected, sizeof selected,
                                  "Pin: %s\nElectrical: %s\n\n%s", p->name,
                                  p->value, details);

            if (length < 0 || length >= (int)sizeof selected) {
                return kc_fail(k, n, TIKU_CIRCUIT_LIMIT,
                               "Pin metadata exceeds the inspection limit");
            }
            info = selected;
        }
        if (!kc_result(k, n, circuit_details(k->doc, i, info))) {
            return 0;
        }
    }
    return !k->error;
}
