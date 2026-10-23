/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_sensor_paint.c - sensor displays in sunken frames, drawn with lines,
 * fills and text only.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_sensor_paint.h"
#include "tiku_font.h"
#include "tiku_text_layout_paint.h"
#include "tiku_ui.h"
#include <math.h>
#include <stdio.h>

/* One trace being painted; x and y are the previous span's last point. */
typedef struct {
    tiku_surface_t *surface;
    tiku_rect_t area;
    double min, max;
    tiku_rgb_t ink;
    int dashed, x, y;
} trace_paint_t;

/**
 * @brief Where @p value lies from @p min to @p max, clamped to 0..1;
 *        worked in doubles, so no finite float range overflows.
 */
static double fraction(double value, double min, double max)
{
    double f = (value - min) / (max - min);
    return f < 0 ? 0 : f > 1 ? 1 : f;
}

/** @brief The surface y of @p value in the plot area; y grows downward. */
static int ordinate(const trace_paint_t *p, float value)
{
    return p->area.y + p->area.h - 1 -
        (int)(fraction(value, p->min, p->max) * (p->area.h - 1));
}

/**
 * @brief Join the previous span's last point to (@p x, @p y) with vertical
 *        runs, which cross the display list.
 */
static void join(trace_paint_t *p, int x, int y)
{
    int previous = p->y;
    for (int at = p->x + 1; at <= x; at++) {
        int next = p->y + (int)((double)(y - p->y) *
                                (at - p->x) / (x - p->x));
        int top = previous < next ? previous : next;
        int height = previous < next ? next - previous + 1 : previous - next + 1;
        if (!p->dashed || (at - p->area.x) % 8 < 5)
            tiku_vline(p->surface, at, top, height, p->ink);
        previous = next;
    }
}

/**
 * @brief Draw one span from its min to its max, joined to the previous one,
 *        so a short spike is not averaged away.
 */
static void span_draw(void *ctx, const tiku_sensor_span_t *span)
{
    trace_paint_t *p = ctx;
    int x = p->area.x + span->column;
    int top = ordinate(p, span->max), bottom = ordinate(p, span->min);
    if (span->connect) join(p, x, ordinate(p, span->first));
    if (!p->dashed || span->column % 8 < 5)
        tiku_vline(p->surface, x, top, bottom - top + 1, p->ink);
    p->x = x;
    p->y = ordinate(p, span->last);
}

tiku_rect_t tiku_sensor_plot_area(tiku_rect_t bounds)
{
    tiku_rect_t r = { bounds.x + 64, bounds.y + 48,
                       bounds.w - 78, bounds.h - 80 };
    if (r.w < 1 || r.h < 1) r.w = r.h = 0;
    return r;
}

int tiku_sensor_plot_scale(const tiku_sensor_plot_t *plot, double *min, double *max)
{
    if (!plot || !min || !max || plot->count < 0 ||
        plot->count > TIKU_SENSOR_TRACES) return 0;
    if (!plot->autoscale) {
        *min = plot->min;
        *max = plot->max;
        return isfinite(*min) && isfinite(*max) && *max > *min;
    }
    int found = 0;
    for (int i = 0; i < plot->count; i++) {
        float lo, hi;
        if (plot->trace[i].hidden ||
            !tiku_history_range(plot->trace[i].history, plot->from_us,
                                 plot->to_us, &lo, &hi)) continue;
        if (!found || lo < *min) *min = lo;
        if (!found || hi > *max) *max = hi;
        found = 1;
    }
    if (!found) { *min = 0; *max = 1; }
    double pad = (*max - *min) * .05;
    if (pad == 0) pad = fabs(*min) * .05 + 1;
    *min -= pad;
    *max += pad;
    return 1;
}

/** @brief @p text as one elided line inside @p r, at any font size. */
static void label(tiku_surface_t *s, tiku_rect_t r, const char *text,
                   tiku_rgb_t ink)
{
    tiku_text_line_draw(s, r, TIKU_MEASURE_PLAIN, text ? text : "", ink);
}

/**
 * @brief Draw the unit, and each trace's number, label and line sample,
 *        along the top.
 */
static void legend(const tiku_sensor_plot_t *plot, tiku_surface_t *s,
                   tiku_rect_t bounds)
{
    label(s, (tiku_rect_t){bounds.x + 10, bounds.y + 3, bounds.w - 20, 20},
          plot->unit, TIKU_C_TEXT);
    int width = (bounds.w - 20) / (plot->count ? plot->count : 1);
    for (int i = 0; i < plot->count; i++) {
        char text[128];
        int x = bounds.x + 10 + i * width;
        tiku_rgb_t ink = plot->trace[i].hidden ? TIKU_C_TEXT : plot->trace[i].ink;
        if (!plot->trace[i].hidden) {
            tiku_hline(s, x, bounds.y + 34, plot->trace[i].dashed ? 5 : 16, ink);
            if (plot->trace[i].dashed) tiku_hline(s, x + 9, bounds.y + 34, 7, ink);
        }
        snprintf(text, sizeof text, "%d %s%s", i + 1,
                 plot->trace[i].label ? plot->trace[i].label : "Sensor",
                 plot->trace[i].hidden ? " (hidden)" : "");
        label(s, (tiku_rect_t){x + 22, bounds.y + 24, width - 28, 20}, text, ink);
    }
}

/**
 * @brief Draw the grid: value labels at the quarters, and time labels at
 *        the ends and the middle, in seconds before @c to_us.
 */
static void axes(const tiku_sensor_plot_t *plot, trace_paint_t *p)
{
    tiku_rect_t r = p->area;
    tiku_rgb_t grid = tiku_tint(TIKU_C_PANEL, TIKU_DARKEN_2);
    char text[48];
    for (int i = 0; i <= 4; i++) {
        int y = r.y + i * (r.h - 1) / 4;
        tiku_hline(p->surface, r.x, y, r.w, grid);
        snprintf(text, sizeof text, "%.4g", p->max - (p->max - p->min) * i / 4);
        label(p->surface, (tiku_rect_t){r.x - 58, y - 9, 53, 18},
              text, TIKU_C_TEXT);
    }
    for (int i = 0; i <= 2; i++) {
        int x = r.x + i * (r.w - 1) / 2;
        tiku_vline(p->surface, x, r.y, r.h, grid);
        snprintf(text, sizeof text, "%.3gs", i == 2 ? 0.0 :
                 -(double)(plot->to_us - plot->from_us) / 1000000 * (2 - i) / 2);
        label(p->surface, (tiku_rect_t){x - (i == 2 ? 44 : 0),
                                       r.y + r.h + 6, 56, 20},
              text, TIKU_C_TEXT);
    }
}

void tiku_sensor_plot_draw(const tiku_sensor_plot_t *plot,
                          tiku_surface_t *s, tiku_rect_t bounds)
{
    if (!plot || !s || bounds.w < 1 || bounds.h < 1 ||
        bounds.w > 8192 || bounds.h > 8192) return;
    tiku_ui_sunken(s, bounds, TIKU_C_DOC);
    trace_paint_t p = { .surface = s, .area = tiku_sensor_plot_area(bounds) };
    if (p.area.w < 80 || p.area.h < 48) return;
    if (!tiku_sensor_plot_scale(plot, &p.min, &p.max) ||
        plot->from_us < 0 || plot->to_us <= plot->from_us) {
        label(s, p.area, "Invalid scale", TIKU_C_DANGER);
        return;
    }
    legend(plot, s, bounds);
    axes(plot, &p);
    if (plot->threshold_on && isfinite(plot->threshold) &&
        plot->threshold >= p.min && plot->threshold <= p.max) {
        int y = ordinate(&p, plot->threshold);
        for (int x = 0; x < p.area.w; x += 8)
            tiku_hline(s, p.area.x + x, y,
                       p.area.w - x < 4 ? p.area.w - x : 4, TIKU_C_DANGER);
    }
    int found = 0;
    for (int i = 0; i < plot->count; i++) {
        if (plot->trace[i].hidden) continue;
        float lo, hi;
        found |= tiku_history_range(plot->trace[i].history,
                                     plot->from_us, plot->to_us, &lo, &hi);
        p.ink = plot->trace[i].ink;
        p.dashed = plot->trace[i].dashed;
        tiku_history_reduce(plot->trace[i].history, plot->from_us,
            plot->to_us, p.area.w, plot->gap_us, span_draw, &p);
    }
    if (!found) label(s, p.area, "No data", TIKU_C_TEXT);
    if (plot->cursor_on && plot->cursor_us >= plot->from_us &&
        plot->cursor_us <= plot->to_us) {
        int x = p.area.x + (int)((double)(plot->cursor_us - plot->from_us) /
                    (double)(plot->to_us - plot->from_us) * (p.area.w - 1));
        tiku_vline(s, x, p.area.y, p.area.h, TIKU_C_TEXT);
    }
}

void tiku_sensor_spark_draw(const tiku_history_t *h, tiku_surface_t *s,
    tiku_rect_t bounds, int64_t from, int64_t to, int64_t gap, tiku_rgb_t ink)
{
    if (!s || bounds.w < 5 || bounds.h < 5 ||
        bounds.w > 8192 || bounds.h > 8192) return;
    tiku_ui_sunken(s, bounds, TIKU_C_DOC);
    float min, max;
    if (!tiku_history_range(h, from, to, &min, &max)) return;
    trace_paint_t p = { .surface = s, .area = tiku_inset(bounds, 2),
                        .min = min, .max = max, .ink = ink };
    if (p.min == p.max) {
        double pad = fabs(p.min) * .05 + 1;
        p.min -= pad; p.max += pad;
    }
    tiku_history_reduce(h, from, to, p.area.w, gap, span_draw, &p);
}

/**
 * @brief Draw @p text in large digits, or as an elided bold line when they
 *        do not fit.
 */
static void digits(tiku_surface_t *s, tiku_rect_t r, const char *text)
{
#ifdef TIKU_DISPLAYLESS
    const tiku_font_t *font = tiku_font_bold();
#else
    const tiku_font_t *font = tiku_font_at(20);
#endif
    if (font->height <= r.h && tiku_text_width(font, text) <= r.w) {
        tiku_text(s, font, r.x, r.y + (r.h - font->height) / 2 + font->ascent,
                  text, TIKU_C_TEXT);
    } else {
        tiku_text_line_draw(s, r, TIKU_MEASURE_BOLD, text, TIKU_C_TEXT);
    }
}

void tiku_sensor_readout_draw(tiku_surface_t *s, tiku_rect_t bounds,
    const char *name, const char *unit, float value, int decimals,
    tiku_sensor_status_t status)
{
    if (!s || bounds.w < 24 || bounds.h < 84) return;
    char text[96];
    if (!isfinite(value) && status == TIKU_SENSOR_LIVE) status = TIKU_SENSOR_EMPTY;
    if (decimals < 0) decimals = 0;
    if (decimals > 6) decimals = 6;
    tiku_ui_sunken(s, bounds, TIKU_C_DOC);
    label(s, (tiku_rect_t){bounds.x + 10, bounds.y + 6, bounds.w - 20, 20},
          name, TIKU_C_TEXT);
    if (status == TIKU_SENSOR_LIVE)
        snprintf(text, sizeof text, "%.*f %s", decimals, value, unit ? unit : "");
    else snprintf(text, sizeof text, "-- %s", unit ? unit : "");
    digits(s, (tiku_rect_t){bounds.x + 10, bounds.y + 28,
                            bounds.w - 20, bounds.h - 56}, text);
    label(s, (tiku_rect_t){bounds.x + 10, bounds.y + bounds.h - 25,
                           bounds.w - 20, 20}, tiku_sensor_status_text(status),
          status == TIKU_SENSOR_LIVE ? TIKU_C_TEXT : TIKU_C_DANGER);
}

void tiku_sensor_meter_draw(tiku_surface_t *s, tiku_rect_t bounds,
    float value, float min, float max, const char *unit, int vertical,
    int threshold_on, float threshold, tiku_sensor_status_t status)
{
    if (!s || bounds.w < 32 || bounds.h < 48) return;
    tiku_ui_sunken(s, bounds, TIKU_C_DOC);
    tiku_rect_t bar = {bounds.x + 6, bounds.y + 6, bounds.w - 12, bounds.h - 32};
    char text[96];
    int scale = isfinite(min) && isfinite(max) && max > min;
    if (!isfinite(value) && status == TIKU_SENSOR_LIVE) status = TIKU_SENSOR_EMPTY;
    if (scale && status == TIKU_SENSOR_LIVE) {
        tiku_rect_t fill = bar;
        double f = fraction(value, min, max);
        if (vertical) { fill.h = (int)(f * bar.h); fill.y += bar.h - fill.h; }
        else fill.w = (int)(f * bar.w);
        tiku_fill(s, fill, threshold_on && value >= threshold ?
                            TIKU_C_DANGER : TIKU_C_SELECT);
    }
    if (scale && threshold_on && isfinite(threshold) &&
        threshold >= min && threshold <= max) {
        double f = fraction(threshold, min, max);
        if (vertical) tiku_hline(s, bar.x,
            bar.y + bar.h - 1 - (int)(f * (bar.h - 1)), bar.w, TIKU_C_TEXT);
        else tiku_vline(s, bar.x + (int)(f * (bar.w - 1)),
                         bar.y, bar.h, TIKU_C_TEXT);
    }
    if (!scale) snprintf(text, sizeof text, "Invalid range");
    else if (status != TIKU_SENSOR_LIVE)
        snprintf(text, sizeof text, "%s", tiku_sensor_status_text(status));
    else snprintf(text, sizeof text, "%.3g .. %.3g %s", min, max, unit ? unit : "");
    label(s, (tiku_rect_t){bounds.x + 6, bounds.y + bounds.h - 24,
                           bounds.w - 12, 20}, text, TIKU_C_TEXT);
}
