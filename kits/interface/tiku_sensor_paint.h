/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_sensor_paint.h - sensor plots, sparklines, readouts and meters,
 * drawn with lines, fills and text only, so they cross the display list.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_SENSOR_PAINT_H_
#define TIKU_SENSOR_PAINT_H_
#include "tiku_sensor.h"
#include "tiku_gfx.h"

/** @brief One trace of a plot. */
typedef struct {
    const tiku_history_t *history;
    const char *label;            /**< borrowed for the draw call */
    tiku_rgb_t ink;
    int hidden, dashed;          /**< dashed tells traces apart without
                                      colour */
} tiku_sensor_trace_t;

/** @brief A time-series plot: its traces, time window, scale and marks. */
typedef struct {
    tiku_sensor_trace_t trace[TIKU_SENSOR_TRACES];
    int count, autoscale;
    int64_t from_us, to_us, gap_us;
    float min, max;              /**< the scale when not autoscaling */
    const char *unit;            /**< one unit and axis for all traces */
    int threshold_on;
    float threshold;
    int cursor_on;
    int64_t cursor_us;           /**< set by the application */
} tiku_sensor_plot_t;

/**
 * @brief The plot's interior inside @p bounds, for painting and for an
 *        application's cursor hit test; empty when @p bounds is too small.
 */
tiku_rect_t tiku_sensor_plot_area(tiku_rect_t bounds);
/**
 * @brief The axis range: the fixed one, or the visible traces' range with
 *        a margin of 5%.
 *
 * @return 1, or 0 for an invalid plot or fixed range
 */
int tiku_sensor_plot_scale(const tiku_sensor_plot_t *plot, double *min, double *max);
/**
 * @brief Draw a labelled time-series plot, its time labels in seconds
 *        before @c to_us.
 *
 * Bounds too small for the plot draw only the frame.  Hidden traces are
 * not drawn and do not affect autoscaling.
 */
void tiku_sensor_plot_draw(const tiku_sensor_plot_t *plot,
                          tiku_surface_t *s, tiku_rect_t bounds);
/** @brief Draw a history as a sparkline with no axes, by the plot's rules. */
void tiku_sensor_spark_draw(const tiku_history_t *h, tiku_surface_t *s,
    tiku_rect_t bounds, int64_t from, int64_t to, int64_t gap, tiku_rgb_t ink);
/**
 * @brief Draw a reading: its label, value, unit and status.  A reading that
 *        is not live shows a dash, not a zero.
 */
void tiku_sensor_readout_draw(tiku_surface_t *s, tiku_rect_t bounds,
    const char *label, const char *unit, float value, int decimals,
    tiku_sensor_status_t status);
/**
 * @brief Draw a horizontal or vertical meter, its range written under it;
 *        a range may be signed, and the value is clamped to it.
 *
 * With @p threshold_on a marker is drawn at @p threshold, and a value at
 * or above it fills in the danger colour.
 */
void tiku_sensor_meter_draw(tiku_surface_t *s, tiku_rect_t bounds,
    float value, float min, float max, const char *unit, int vertical,
    int threshold_on, float threshold, tiku_sensor_status_t status);

#endif
