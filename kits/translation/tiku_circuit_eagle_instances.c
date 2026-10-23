/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_circuit_eagle_instances.c - a sheet's instances, pins and nets.
 * Each instance resolves part, deviceset, gate and symbol in the
 * libraries embedded in the file, and its symbol is drawn where placed.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_circuit_eagle_private.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PIN_SHORT      2.54         /* pin lengths, mm                   */
#define PIN_MIDDLE     5.08
#define PIN_LONG       7.62
#define PIN_DOT_RADIUS .5           /* an inverted pin's dot, mm         */
#define PIN_CLOCK      .7           /* a clock pin's wedge, mm           */
#define EAGLE_TEXT_MAX 4096         /* attribute and details text bytes  */

/** @brief Resolve @p part_name's gate @p gate_name: set the part, device,
 *         name, gate, value and display name in @p e.
 *  @return the gate's symbol, or NULL with the failure recorded. */
static eagle_node_t *
symbol(circuit_eagle_t *e, const char *part_name, const char *gate_name)
{
    e->part = eagle_named(e, e->parts, "part", part_name);
    if (e->error) {
        return NULL;
    }
    eagle_node_t *library = eagle_named(e, e->libraries, "library",
                                        tiku_xml_attr(e->part, "library"));

    if (e->error) {
        return NULL;
    }
    eagle_node_t *set = eagle_named(e, tiku_xml_child(library, "devicesets"),
                                    "deviceset",
                                    tiku_xml_attr(e->part, "deviceset"));

    if (e->error) {
        return NULL;
    }
    eagle_node_t *gate = eagle_named(e, tiku_xml_child(set, "gates"), "gate",
                                     gate_name);

    if (e->error) {
        return NULL;
    }
    e->device = eagle_named(e, tiku_xml_child(set, "devices"), "device",
                            eagle_attr(e->part, "device", ""));
    if (e->error) {
        return NULL;
    }
    e->name = part_name;
    e->gate = gate_name;
    e->value = eagle_attr(e->part, "value", eagle_attr(set, "name", ""));
    /* The gate's name is added when the deviceset has more than one. */
    eagle_node_t *first = tiku_xml_child(tiku_xml_child(set, "gates"),
                                         "gate");
    int length = snprintf(e->display_name, sizeof e->display_name, "%s%s",
                          part_name,
                          tiku_xml_next(first, "gate") ? gate_name : "");

    if (length < 0 || (size_t)length >= sizeof e->display_name) {
        eagle_fail(e, TIKU_CIRCUIT_LIMIT, "Instance name exceeds limit");
        return NULL;
    }
    return eagle_named(e, tiku_xml_child(library, "symbols"), "symbol",
                       tiku_xml_attr(gate, "symbol"));
}

/** @brief qsort's order for connects: by pin name, then by their order
 *         in the file. */
static int
connect_order(const void *x, const void *y)
{
    const eagle_connect_t *a = x, *b = y;
    int c = strcmp(a->pin, b->pin);

    return c ? c : (a->order > b->order) - (a->order < b->order);
}

/** @brief Index the device's connects for the instance's gate, by pin
 *         name; each connect is a step of the budget.
 *  @return 1, or 0 with the failure recorded. */
static int
index_connects(circuit_eagle_t *e)
{
    eagle_node_t *list = tiku_xml_child(e->device, "connects");
    const char *gate = e->gate ? e->gate : "";
    size_t n = 0;

    for (eagle_node_t *c = tiku_xml_child(list, "connect"); c;
         c = tiku_xml_next(c, "connect")) {
        if (!eagle_step(e)) {
            return 0;
        }
        if (!strcmp(eagle_attr(c, "gate", ""), gate)) {
            n++;
        }
    }
    if (!n) {
        return 1;
    }
    e->connects = malloc(n * sizeof *e->connects);
    if (!e->connects) {
        return eagle_fail(e, TIKU_CIRCUIT_NOMEM,
                          "Cannot index EAGLE connects");
    }
    for (eagle_node_t *c = tiku_xml_child(list, "connect"); c;
         c = tiku_xml_next(c, "connect")) {
        if (!strcmp(eagle_attr(c, "gate", ""), gate)) {
            e->connects[e->connect_count] = (eagle_connect_t){
                eagle_attr(c, "pin", ""), eagle_attr(c, "pad", ""),
                e->connect_count
            };
            e->connect_count++;
        }
    }
    qsort(e->connects, e->connect_count, sizeof *e->connects,
          connect_order);
    return 1;
}

/** @brief The pads pin @p pin of the drawn instance reaches, or "".  Of
 *         connects naming the same pin, the last in the file counts. */
static const char *
pads_of(const circuit_eagle_t *e, const char *pin)
{
    size_t lo = 0, hi = e->connect_count;

    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;

        if (strcmp(e->connects[mid].pin, pin) <= 0) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return lo && !strcmp(e->connects[lo - 1].pin, pin)
           ? e->connects[lo - 1].pad : "";
}

int
eagle_instance(circuit_eagle_t *e, eagle_node_t *n)
{
    circuit_eagle_t saved = *e;

    e->instance = n;
    e->connects = NULL;
    e->connect_count = 0;
    eagle_node_t *s = symbol(e, tiku_xml_attr(n, "part"),
                             tiku_xml_attr(n, "gate"));
    double x = eagle_number(e, n, "x", 0, 1);
    double y = eagle_number(e, n, "y", 0, 1), angle;
    int mirror, spin;

    if (!e->error && index_connects(e)
        && eagle_rotation(e, tiku_xml_attr(n, "rot"), &angle, &mirror,
                          &spin)) {
        double a = angle * M_PI / 180, sign = mirror ? -1 : 1;

        e->m[0] = sign * cos(a);
        e->m[1] = sign * sin(a);
        e->m[2] = -sin(a);
        e->m[3] = cos(a);
        e->m[4] = x;
        e->m[5] = y;
        for (eagle_node_t *g = tiku_xml_child(s, NULL); g && !e->error;
             g = tiku_xml_next(g, NULL)) {
            if (tiku_xml_is(g, "description")) {
                continue;
            }
            eagle_graphic(e, g);
        }
        /* The instance's own attributes are placed in sheet axes. */
        for (eagle_node_t *a_node = tiku_xml_child(n, NULL);
             a_node && !e->error; a_node = tiku_xml_next(a_node, NULL)) {
            if (!tiku_xml_is(a_node, "attribute")) {
                eagle_fail(e, TIKU_CIRCUIT_UNSUPPORTED,
                           "Unsupported EAGLE instance element");
                break;
            }
            const char *display = eagle_attr(a_node, "display", "value");

            if (!strcmp(display, "off")) {
                continue;
            }
            if (strcmp(display, "name") && strcmp(display, "both")
                && strcmp(display, "value")) {
                eagle_fail(e, TIKU_CIRCUIT_BAD,
                           "Invalid EAGLE attribute display mode");
                break;
            }
            const char *name = eagle_attr(a_node, "name", "");
            const char *value = eagle_attr(a_node, "value", "");

            if (!strcmp(name, "NAME")) {
                value = e->display_name;
            } else if (!strcmp(name, "VALUE")) {
                value = e->value;
            } else if (!*value) {
                /* An empty value falls back to the part's. */
                for (eagle_node_t *p = tiku_xml_child(e->part, "attribute");
                     p; p = tiku_xml_next(p, "attribute")) {
                    if (!strcmp(eagle_attr(p, "name", ""), name)) {
                        value = eagle_attr(p, "value", "");
                    }
                }
            }
            char text[EAGLE_TEXT_MAX + 1];
            int length = snprintf(text, sizeof text,
                                  !strcmp(display, "name") ? "%s"
                                  : !strcmp(display, "both") ? "%s = %s"
                                  : "%s%s",
                                  !strcmp(display, "name")
                                  || !strcmp(display, "both") ? name : value,
                                  !strcmp(display, "both") ? value : "");

            if (length < 0 || (size_t)length >= sizeof text) {
                eagle_fail(e, TIKU_CIRCUIT_LIMIT,
                           "EAGLE attribute text exceeds limit");
                break;
            }
            double matrix[6];

            memcpy(matrix, e->m, sizeof matrix);
            memcpy(e->m, (double[]){1, 0, 0, 1, 0, 0}, sizeof e->m);
            size_t start = e->doc->count;

            eagle_text(e, a_node, text);
            eagle_metadata(e, start);
            memcpy(e->m, matrix, sizeof matrix);
        }
    }
    /* The state saved before the instance comes back, except the failure
     * and the work done. */
    tiku_circuit_err_t error = e->error;
    unsigned work = e->work;

    free(e->connects);
    *e = saved;
    e->error = error;
    e->work = work;
    return !error;
}

int
eagle_pin(circuit_eagle_t *e, eagle_node_t *n)
{
    const char *length = eagle_attr(n, "length", "long");
    const char *function = eagle_attr(n, "function", "none");
    const char *visible = eagle_attr(n, "visible", "both");
    double len = !strcmp(length, "point") ? 0
               : !strcmp(length, "short") ? PIN_SHORT
               : !strcmp(length, "middle") ? PIN_MIDDLE
               : !strcmp(length, "long") ? PIN_LONG : -1;
    int dot = !strcmp(function, "dot") || !strcmp(function, "dotclk");
    int clock = !strcmp(function, "clk") || !strcmp(function, "dotclk");

    if (len < 0 || (!dot && !clock && strcmp(function, "none"))) {
        return eagle_fail(e, TIKU_CIRCUIT_UNSUPPORTED,
                          "Unknown EAGLE pin style");
    }
    if (strcmp(visible, "off") && strcmp(visible, "both")
        && strcmp(visible, "pin") && strcmp(visible, "pad")) {
        return eagle_fail(e, TIKU_CIRCUIT_BAD, "Invalid pin visibility");
    }
    double x = eagle_number(e, n, "x", 0, 1);
    double y = eagle_number(e, n, "y", 0, 1), angle;
    int mirror, spin;

    if (!eagle_rotation(e, tiku_xml_attr(n, "rot"), &angle, &mirror, &spin)
        || e->error) {
        return 0;
    }
    if (mirror || spin || fmod(angle, 90)) {
        return eagle_fail(e, TIKU_CIRCUIT_BAD,
                          "Pin rotation must be cardinal");
    }
    /* The pin's own axes: placed at (x, y), turned by its angle. */
    double saved[6];

    memcpy(saved, e->m, sizeof saved);
    double a = angle * M_PI / 180;

    e->m[0] = saved[0] * cos(a) + saved[2] * sin(a);
    e->m[1] = saved[1] * cos(a) + saved[3] * sin(a);
    e->m[2] = -saved[0] * sin(a) + saved[2] * cos(a);
    e->m[3] = -saved[1] * sin(a) + saved[3] * cos(a);
    e->m[4] = saved[0] * x + saved[2] * y + saved[4];
    e->m[5] = saved[1] * x + saved[3] * y + saved[5];
    size_t start = e->doc->count;
    tiku_circuit_err_t error = TIKU_CIRCUIT_OK;

    if (len) {
        circuit_point_t p[] = {
            eagle_point(e, 0, 0),
            eagle_point(e, fmax(0, len - (dot ? 1 : 0)), 0)
        };

        error = circuit_add(e->doc, e->name, e->value, p, 2,
                            EAGLE_LINE_WIDTH, EAGLE_INK_SYMBOL, 0, 0);
    }
    if (!error && dot) {
        circuit_point_t c = eagle_point(e, len - PIN_DOT_RADIUS, 0);

        error = circuit_arc(e->doc, e->name, e->value, c.x, c.y,
                            PIN_DOT_RADIUS, 0, 2 * M_PI, EAGLE_LINE_WIDTH,
                            EAGLE_INK_SYMBOL, 0);
    }
    if (!error && clock) {
        circuit_point_t p[] = {eagle_point(e, len, -PIN_CLOCK),
                               eagle_point(e, len + PIN_CLOCK, 0),
                               eagle_point(e, len, PIN_CLOCK)};

        error = circuit_add(e->doc, e->name, e->value, p, 3,
                            EAGLE_LINE_WIDTH, EAGLE_INK_SYMBOL, 0, 0);
    }
    if (error) {
        eagle_fail(e, error, "Cannot construct EAGLE pin");
    }
    const char *pin = eagle_attr(n, "name", "");
    const char *pad = pads_of(e, pin);

    /* The pin's name past its end, then its pads above its middle. */
    for (int i = 0; i < 2 && !e->error; i++) {
        if (!strcmp(visible, "off") || (i == 0 && !strcmp(visible, "pad"))
            || (i == 1 && !strcmp(visible, "pin"))) {
            continue;
        }
        char pos[64];

        snprintf(pos, sizeof pos, "%.8f", i ? len / 2 : len + 1);
        const char *attrs[] = {"x", pos, "y", i ? "0.4" : "0", "size",
                               i ? "1.016" : "1.27", "align",
                               i ? "bottom-center" : "center-left"};
        tiku_xml_node_t text = {.attr = attrs, .nattr = 4};

        eagle_text(e, &text, i ? pad : pin);
    }
    memcpy(e->m, saved, sizeof saved);
    if (!eagle_metadata(e, start)) {
        return 0;
    }
    for (size_t i = start; i < e->doc->count; i++) {
        char details[EAGLE_TEXT_MAX + 1];
        int size = snprintf(details, sizeof details,
                            "%s\n"
                            "Pin: %s\n"
                            "Pads: %s\n"
                            "Direction: %s\n"
                            "Connection: X %.8g, Y %.8g mm (symbol axes)",
                            tiku_circuit_details(e->doc, i), pin,
                            *pad ? pad : "Not specified",
                            eagle_attr(n, "direction", "io"), x, y);

        if (size < 0 || (size_t)size >= sizeof details) {
            return eagle_fail(e, TIKU_CIRCUIT_LIMIT,
                              "Pin metadata exceeds limit");
        }
        error = circuit_details(e->doc, i, details);
        if (error) {
            return eagle_fail(e, error, "Cannot retain pin details");
        }
    }
    return !e->error;
}

int
eagle_sheet(circuit_eagle_t *e, eagle_node_t *sheet)
{
    for (eagle_node_t *section = tiku_xml_child(sheet, NULL);
         section && !e->error; section = tiku_xml_next(section, NULL)) {
        if (tiku_xml_next(section, section->name)) {
            return eagle_fail(e, TIKU_CIRCUIT_BAD,
                              "Duplicate EAGLE sheet section");
        }
        if (tiku_xml_is(section, "description")) {
            continue;
        }
        int plain = tiku_xml_is(section, "plain");
        int instances = tiku_xml_is(section, "instances");
        int nets = tiku_xml_is(section, "nets");
        int buses = tiku_xml_is(section, "busses");

        if (!plain && !instances && !nets && !buses) {
            return eagle_fail(e, TIKU_CIRCUIT_UNSUPPORTED,
                              "Unsupported EAGLE sheet section");
        }
        for (eagle_node_t *n = tiku_xml_child(section, NULL);
             n && !e->error; n = tiku_xml_next(n, NULL)) {
            if (plain) {
                eagle_graphic(e, n);
            } else if (instances) {
                if (!tiku_xml_is(n, "instance")) {
                    return eagle_fail(e, TIKU_CIRCUIT_BAD,
                                      "Invalid EAGLE instance list");
                }
                for (eagle_node_t *other = tiku_xml_next(n, "instance");
                     other; other = tiku_xml_next(other, "instance")) {
                    if (!eagle_step(e)) {
                        return 0;
                    }
                    if (!strcmp(eagle_attr(n, "part", ""),
                                eagle_attr(other, "part", ""))
                        && !strcmp(eagle_attr(n, "gate", ""),
                                   eagle_attr(other, "gate", ""))) {
                        return eagle_fail(e, TIKU_CIRCUIT_BAD,
                                          "Duplicate part/gate instance on "
                                          "sheet");
                    }
                }
                eagle_instance(e, n);
            } else {
                if (!tiku_xml_is(n, nets ? "net" : "bus")) {
                    return eagle_fail(e, TIKU_CIRCUIT_BAD,
                                      "Invalid net/bus list");
                }
                e->net = eagle_attr(n, "name", "");
                e->name = e->net;
                for (eagle_node_t *segment = tiku_xml_child(n, NULL);
                     segment && !e->error;
                     segment = tiku_xml_next(segment, NULL)) {
                    if (!tiku_xml_is(segment, "segment")) {
                        return eagle_fail(e, TIKU_CIRCUIT_BAD,
                                          "Invalid net segment");
                    }
                    for (eagle_node_t *g = tiku_xml_child(segment, NULL);
                         g && !e->error; g = tiku_xml_next(g, NULL)) {
                        if (tiku_xml_is(g, "pinref")) {
                            /* A pin reference is checked, not drawn. */
                            circuit_eagle_t saved = *e;
                            eagle_node_t *s;

                            s = symbol(e, tiku_xml_attr(g, "part"),
                                       tiku_xml_attr(g, "gate"));
                            if (!e->error) {
                                (void)eagle_named(e, s, "pin",
                                                  tiku_xml_attr(g, "pin"));
                            }
                            tiku_circuit_err_t error = e->error;
                            unsigned work = e->work;

                            *e = saved;
                            e->error = error;
                            e->work = work;
                        } else {
                            eagle_graphic(e, g);
                        }
                    }
                }
                e->net = NULL;
                e->name = "Drawing";
            }
        }
    }
    return !e->error;
}
