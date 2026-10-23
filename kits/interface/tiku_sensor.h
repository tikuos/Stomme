/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_sensor.h - a sensor's history of samples and the column summaries
 * a plot is drawn from, with no drawing, device or heap.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_SENSOR_H_
#define TIKU_SENSOR_H_
#include <stddef.h>
#include <stdint.h>

/** @brief The most samples one history holds. */
#define TIKU_SENSOR_CAP_MAX 4096
/** @brief The most traces one plot draws. */
#define TIKU_SENSOR_TRACES 4

/** @brief One reading, or a gap where a reading was missed. */
typedef struct {
    int64_t time_us;             /**< nonnegative, monotonic in a history */
    float value;
    int valid;                   /**< zero marks a gap, not a zero reading */
} tiku_sample_t;

/** @brief A ring of samples in storage the caller owns. */
typedef struct {
    tiku_sample_t *samples;       /**< caller-owned; the kit allocates none */
    size_t capacity, count, head; /**< head is the next slot written       */
} tiku_history_t;

/** @brief The state of a sensor's latest reading. */
typedef enum {
    TIKU_SENSOR_EMPTY, TIKU_SENSOR_LIVE,
    TIKU_SENSOR_STALE, TIKU_SENSOR_DISCONNECTED
} tiku_sensor_status_t;

/**
 * @brief One pixel column's summary of the samples in it, keeping its
 *        peaks.
 */
typedef struct {
    int column, connect;         /**< connect: joins the previous span */
    float first, last, min, max;
} tiku_sensor_span_t;

/** @brief Receives each span tiku_history_reduce() makes. */
typedef void (*tiku_sensor_span_fn)(void *ctx, const tiku_sensor_span_t *span);

/**
 * @brief Set @p h up empty over @p capacity samples of @p storage, 1 to
 *        TIKU_SENSOR_CAP_MAX.
 *
 * @return 1, or 0 for invalid arguments, leaving @p h empty
 */
int tiku_history_init(tiku_history_t *h, tiku_sample_t *storage, size_t capacity);
/**
 * @brief Append a sample, or replace the newest when its time is equal.
 *
 * A missing sample (@p valid 0) is kept and breaks the plotted line.
 *
 * @return 1, or 0 for a time negative or older than the newest, or a
 *         valid value that is not finite
 */
int tiku_history_push(tiku_history_t *h, int64_t time_us, float value, int valid);
/** @brief Sample @p index, 0 the oldest kept; NULL when out of range. */
const tiku_sample_t *tiku_history_at(const tiku_history_t *h, size_t index);
/**
 * @brief Replace @p dest's samples with the newest of @p source that fit.
 *
 * @return 1, or 0 when either is invalid or their storage overlaps
 * @note @p dest is initialized with storage of its own.
 */
int tiku_history_copy(tiku_history_t *dest, const tiku_history_t *source);
/**
 * @brief The state of the latest sample at @p now_us.
 *
 * Stale when that sample is a gap, lies after @p now_us or is older than
 * @p stale_us; a @p stale_us of 0 or less sets no age limit.
 */
tiku_sensor_status_t tiku_history_status(const tiku_history_t *h,
    int connected, int64_t now_us, int64_t stale_us);
/**
 * @brief Min and max of the valid samples from @p from to @p to inclusive.
 *
 * @return 1, or 0 when there are none
 */
int tiku_history_range(const tiku_history_t *h, int64_t from, int64_t to,
                       float *min, float *max);
/**
 * @brief The sample nearest @p time, a gap included, the earlier on a tie;
 *        nothing is interpolated.  NULL when empty or @p time is negative.
 */
const tiku_sample_t *tiku_history_nearest(const tiku_history_t *h, int64_t time);
/**
 * @brief Pass @p emit a span for each of the @p width pixel columns that
 *        holds samples from @p from to @p to.
 *
 * One pass over the samples in constant space.  A gap, or more than
 * @p gap_us between samples, ends a span and leaves the next one
 * unconnected, so a gap inside a column splits it; a @p gap_us of 0 or
 * less sets no limit.  @p width is 1 to 8192.
 *
 * @note @p emit does not change the history.
 */
void tiku_history_reduce(const tiku_history_t *h, int64_t from, int64_t to,
    int width, int64_t gap_us, tiku_sensor_span_fn emit, void *ctx);
/** @brief The status in words, for readouts and status rows. */
const char *tiku_sensor_status_text(tiku_sensor_status_t status);

#endif
