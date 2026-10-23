/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_circuit_eagle_frame.c - EAGLE drawing frames: the outline, the
 * borders and their column and row labels.  Numbering follows EAGLE; the
 * border width and label size are the preview's own.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_circuit_eagle_private.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define FRAME_COLUMNS_MAX 127       /* numbered columns                  */
#define FRAME_ROWS_MAX    26        /* rows, lettered A to Z             */
#define FRAME_BORDER      4         /* border width, mm, at most a tenth
                                       of the frame's shorter side       */

/** @brief A frame line from (@p x, @p y) to (@p xx, @p yy), sheet source
 *         axes.  @return 1, or 0 with the failure recorded. */
static int
line(circuit_eagle_t *e, double x, double y, double xx, double yy)
{
    circuit_point_t p[] = {eagle_point(e, x, y), eagle_point(e, xx, yy)};
    tiku_circuit_err_t error = circuit_add(e->doc, e->name, e->value, p, 2,
                                           EAGLE_LINE_WIDTH, EAGLE_INK_FRAME,
                                           0, 0);

    return error ? eagle_fail(e, error, "Cannot draw EAGLE frame") : 1;
}

/** @brief Column or row label @p text centred on (@p x, @p y).
 *  @return 1, or 0 with the failure recorded. */
static int
label(circuit_eagle_t *e, double x, double y, const char *text)
{
    char sx[64], sy[64];

    snprintf(sx, sizeof sx, "%.8f", x);
    snprintf(sy, sizeof sy, "%.8f", y);
    const char *attrs[] = {"x", sx, "y", sy, "size", "1.27", "align",
                           "center"};
    tiku_xml_node_t n = {.attr = attrs, .nattr = 4};

    return eagle_text(e, &n, text);
}

int
eagle_frame(circuit_eagle_t *e, eagle_node_t *n)
{
    double x = eagle_number(e, n, "x1", 0, 1);
    double y = eagle_number(e, n, "y1", 0, 1);
    double xx = eagle_number(e, n, "x2", 0, 1);
    double yy = eagle_number(e, n, "y2", 0, 1);
    double cols = eagle_number(e, n, "columns", 0, 1);
    double rows = eagle_number(e, n, "rows", 0, 1);

    if (e->error) {
        return 0;
    }
    /* A negative count numbers from the other end. */
    if (!cols || !rows || fabs(cols) > FRAME_COLUMNS_MAX
        || fabs(rows) > FRAME_ROWS_MAX || cols != floor(cols)
        || rows != floor(rows) || xx <= x || yy <= y) {
        return eagle_fail(e, TIKU_CIRCUIT_BAD,
                          "Invalid EAGLE frame geometry/grid");
    }
    double saved[6];

    memcpy(saved, e->m, sizeof saved);
    e->m[0] = e->m[3] = 1;
    e->m[1] = e->m[2] = 0;          /* frames do not turn with symbols */
    line(e, x, y, xx, y);
    line(e, xx, y, xx, yy);
    line(e, xx, yy, x, yy);
    line(e, x, yy, x, y);
    double border = fmin(FRAME_BORDER, fmin(xx - x, yy - y) / 10);
    int left = strcmp(eagle_attr(n, "border-left", "yes"), "no") != 0;
    int right = strcmp(eagle_attr(n, "border-right", "yes"), "no") != 0;
    int top = strcmp(eagle_attr(n, "border-top", "yes"), "no") != 0;
    int bottom = strcmp(eagle_attr(n, "border-bottom", "yes"), "no") != 0;
    double x0 = x + left * border, x1 = xx - right * border;
    double y0 = y + bottom * border, y1 = yy - top * border;

    if (left) {
        line(e, x0, y, x0, yy);
    }
    if (right) {
        line(e, x1, y, x1, yy);
    }
    if (top) {
        line(e, x, y1, xx, y1);
    }
    if (bottom) {
        line(e, x, y0, xx, y0);
    }
    for (int i = 0; i < (int)fabs(cols) && !e->error; i++) {
        if (!eagle_step(e)) {
            break;
        }
        char text[16];

        snprintf(text, sizeof text, "%d", cols > 0 ? i + 1 : (int)-cols - i);
        double at = x0 + (x1 - x0) * (i + .5) / fabs(cols);
        double edge = x0 + (x1 - x0) * i / fabs(cols);

        if (top) {
            label(e, at, yy - border / 2, text);
            if (i) {
                line(e, edge, y1, edge, yy);
            }
        }
        if (bottom) {
            label(e, at, y + border / 2, text);
            if (i) {
                line(e, edge, y, edge, y0);
            }
        }
    }
    for (int i = 0; i < (int)fabs(rows) && !e->error; i++) {
        if (!eagle_step(e)) {
            break;
        }
        char text[] = {(char)('A' + (rows > 0 ? i : (int)-rows - i - 1)), 0};
        double at = y1 - (y1 - y0) * (i + .5) / fabs(rows);
        double edge = y1 - (y1 - y0) * i / fabs(rows);

        if (left) {
            label(e, x + border / 2, at, text);
            if (i) {
                line(e, x, edge, x0, edge);
            }
        }
        if (right) {
            label(e, xx - border / 2, at, text);
            if (i) {
                line(e, x1, edge, xx, edge);
            }
        }
    }
    memcpy(e->m, saved, sizeof saved);
    return !e->error;
}
