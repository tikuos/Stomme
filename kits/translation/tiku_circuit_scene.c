/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_circuit_scene.c - one millimetre-to-pixel transform for drawing,
 * picking and navigation: hit tests against paths, filled interiors and
 * artwork coverage, search by name and text, fit and zoom.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_circuit_private.h"

#include <math.h>
#include <string.h>

#define FIT_MARGIN       24     /* most pixels fit leaves round a drawing */
#define FIT_MARGIN_SHARE 8.0    /* nor more than the shorter side / this  */
#define FIT_EXTENT_MIN   0.01   /* mm: a narrower drawing fits as this    */

/** @brief Distance from (@p x, @p y) to the segment from @p a to @p b. */
static double
distance(double x, double y, circuit_point_t a, circuit_point_t b)
{
    double dx = b.x - a.x, dy = b.y - a.y, length = dx * dx + dy * dy;
    double t = length ? ((x - a.x) * dx + (y - a.y) * dy) / length : 0;

    t = fmin(1, fmax(0, t));
    return hypot(x - a.x - t * dx, y - a.y - t * dy);
}

size_t
tiku_circuit_hit(const tiku_circuit_t *d, double x, double y,
                 double tolerance)
{
    size_t best = SIZE_MAX;

    if (!d || !isfinite(x) || !isfinite(y) || !isfinite(tolerance) ||
        tolerance < 0) {
        return best;
    }
    if (d->layer_count) {
        size_t offset = 0;

        for (size_t i = 0; i < d->layer_count; i++) {
            const tiku_circuit_layer_t *l = &d->layers[i];

            if (!l->scene || !l->visible || !l->opacity) {
                continue;
            }
            size_t hit = tiku_circuit_hit(l->scene, x, y, tolerance);

            if (hit != SIZE_MAX) {
                return offset + hit;
            }
            offset += l->scene->count;
        }
        return SIZE_MAX;
    }
    double nearest = tolerance;

    for (size_t i = 0; i < d->count; i++) {
        size_t index = d->coverage ? d->count - 1 - i : i;
        const circuit_shape_t *s = &d->shapes[index];
        double v = HUGE_VAL;
        int inside = 0;

        if (s->kind == CIRCUIT_GROUP) {
            if (tiku_circuit_hit(s->group, x - s->cx, y - s->cy,
                                 tolerance) != SIZE_MAX) {
                v = 0;
            }
            goto measured;
        }
        if (s->kind == CIRCUIT_OBROUND) {
            v = fmax(0, distance(x, y, s->points[0], s->points[1]) - s->r);
            goto measured;
        }
        if (s->kind == CIRCUIT_ARC) {
            double angle = atan2(y - s->cy, x - s->cx) - s->start;

            if (s->sweep < 0) {
                angle = -angle;
            }
            angle = fmod(angle + 4 * M_PI, 2 * M_PI);
            double radial = hypot(x - s->cx, y - s->cy);
            double target = s->end_r > 0 && s->sweep
                            ? s->r + (s->end_r - s->r) * angle /
                                     fabs(s->sweep)
                            : s->r;

            if (angle <= fabs(s->sweep)) {
                v = s->fill ? fmax(0, radial - target)
                            : fabs(radial - target);
            } else {
                for (int end = 0; end < 2; end++) {
                    double a = s->start + end * s->sweep;
                    double radius = end && s->end_r > 0 ? s->end_r : s->r;

                    v = fmin(v, hypot(x - s->cx - radius * cos(a),
                                      y - s->cy - radius * sin(a)));
                }
            }
            v = fmax(0, v - (s->fill ? 0 : s->width / 2));
            goto measured;
        }
        for (size_t j = 0; j < s->count; j++) {
            circuit_point_t a = s->points[j];
            circuit_point_t b = s->points[(j + 1) % s->count];

            if (j + 1 < s->count || s->closed || s->fill) {
                v = fmin(v, distance(x, y, a, b));
            }
            if ((a.y > y) != (b.y > y) &&
                x < (b.x - a.x) * (y - a.y) / (b.y - a.y) + a.x) {
                inside = !inside;
            }
        }
        v = s->fill && inside ? 0
                              : fmax(0, v - (s->fill ? 0 : s->width / 2));
measured:
        if (s->hole) {
            v = fmax(v, s->hole / 2 - hypot(x - s->cx, y - s->cy));
        }
        if (v <= nearest) {
            if (d->coverage) {
                return s->clear ? SIZE_MAX : index;
            }
            nearest = v;
            best = index;
        }
    }
    return best;
}

/** @brief Whether @p text occurs in @p s, ASCII letters compared without
 *         case and every other byte, UTF-8 included, as it is. */
static int
contains(const char *s, const char *text)
{
    for (; *s; s++) {
        size_t i = 0;

        while (text[i] && s[i]) {
            unsigned a = (unsigned char)s[i], b = (unsigned char)text[i];

            if (a >= 'A' && a <= 'Z') {
                a += 'a' - 'A';
            }
            if (b >= 'A' && b <= 'Z') {
                b += 'a' - 'A';
            }
            if (a != b) {
                break;
            }
            i++;
        }
        if (!text[i]) {
            return 1;
        }
    }
    return 0;
}

size_t
tiku_circuit_find(const tiku_circuit_t *d, const char *text, size_t start)
{
    if (!d || !text || !*text) {
        return SIZE_MAX;
    }
    if (d->layer_count) {
        size_t offset = 0;

        for (size_t i = 0; i < d->layer_count; i++) {
            const tiku_circuit_layer_t *l = &d->layers[i];

            if (!l->scene || !l->visible || !l->opacity) {
                continue;
            }
            size_t n = l->scene->count;

            if (start < offset + n) {
                size_t hit = tiku_circuit_find(l->scene, text,
                                               start > offset
                                               ? start - offset : 0);

                if (hit != SIZE_MAX) {
                    return offset + hit;
                }
            }
            offset += n;
        }
        return SIZE_MAX;
    }
    for (size_t i = start; i < d->count; i++) {
        const circuit_shape_t *s = &d->shapes[i];

        if (contains(s->info.name, text) || contains(s->info.value, text) ||
            (s->text && contains(s->text, text))) {
            return i;
        }
    }
    return SIZE_MAX;
}

int
tiku_circuit_world(const tiku_circuit_view_t *v, double x, double y,
                   double *wx, double *wy)
{
    if (!v || !wx || !wy || !isfinite(v->scale) || v->scale <= 0 ||
        !isfinite(v->x) || !isfinite(v->y) || !isfinite(x) ||
        !isfinite(y)) {
        return 0;
    }
    double a = (x - v->x) / v->scale, b = (y - v->y) / v->scale;

    if (!isfinite(a) || !isfinite(b)) {
        return 0;
    }
    *wx = a;
    *wy = b;
    return 1;
}

int
tiku_circuit_fit(const tiku_circuit_t *d, int w, int h,
                 tiku_circuit_view_t *out)
{
    if (!d || !out || w < 1 || h < 1 || w > TIKU_CIRCUIT_RENDER_SIDE ||
        h > TIKU_CIRCUIT_RENDER_SIDE ||
        (size_t)w * h > TIKU_CIRCUIT_RENDER_PIXELS) {
        return 0;
    }
    tiku_circuit_box_t b = d->bounds;
    double margin = fmin(FIT_MARGIN, fmin(w, h) / FIT_MARGIN_SHARE);
    double scale = fmin((w - 2 * margin) /
                        fmax(FIT_EXTENT_MIN, b.x1 - b.x0),
                        (h - 2 * margin) /
                        fmax(FIT_EXTENT_MIN, b.y1 - b.y0));

    scale = fmin(CIRCUIT_SCALE_MAX, fmax(CIRCUIT_SCALE_MIN, scale));
    double x = w / 2.0 - (b.x0 + b.x1) * scale / 2;
    double y = h / 2.0 - (b.y0 + b.y1) * scale / 2;

    if (!isfinite(x) || !isfinite(y) || fabs(x) > CIRCUIT_SHIFT_MAX ||
        fabs(y) > CIRCUIT_SHIFT_MAX) {
        return 0;
    }
    *out = (tiku_circuit_view_t){w, h, scale, x, y};
    return 1;
}

int
tiku_circuit_zoom(tiku_circuit_view_t *v, double factor, double x, double y)
{
    double wx, wy;

    if (!v || !isfinite(factor) || factor <= 0 ||
        !tiku_circuit_world(v, x, y, &wx, &wy)) {
        return 0;
    }
    double scale = fmin(CIRCUIT_SCALE_MAX,
                        fmax(CIRCUIT_SCALE_MIN, v->scale * factor));
    double dx = x - wx * scale, dy = y - wy * scale;

    if (!isfinite(dx) || !isfinite(dy) || fabs(dx) > CIRCUIT_SHIFT_MAX ||
        fabs(dy) > CIRCUIT_SHIFT_MAX) {
        return 0;
    }
    v->scale = scale;
    v->x = dx;
    v->y = dy;
    return 1;
}
