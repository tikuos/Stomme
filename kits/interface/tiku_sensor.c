/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_sensor.c - a bounded sensor history and its column summaries.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_sensor.h"
#include <math.h>
#include <string.h>

int tiku_history_init(tiku_history_t *h, tiku_sample_t *storage, size_t capacity)
{
    if (!h) return 0;
    memset(h, 0, sizeof *h);
    if (!storage || !capacity || capacity > TIKU_SENSOR_CAP_MAX) return 0;
    h->samples = storage;
    h->capacity = capacity;
    return 1;
}

const tiku_sample_t *tiku_history_at(const tiku_history_t *h, size_t index)
{
    if (!h || !h->samples || index >= h->count) return NULL;
    return &h->samples[(h->head + h->capacity - h->count + index) % h->capacity];
}

int tiku_history_push(tiku_history_t *h, int64_t time_us, float value, int valid)
{
    if (!h || !h->samples || !h->capacity || time_us < 0 ||
        (valid && !isfinite(value))) return 0;
    const tiku_sample_t *last = tiku_history_at(h, h->count - 1);
    if (last && time_us < last->time_us) return 0;
    size_t at = h->head;
    if (last && time_us == last->time_us) {
        at = (h->head + h->capacity - 1) % h->capacity;
    } else {
        h->head = (h->head + 1) % h->capacity;
        if (h->count < h->capacity) h->count++;
    }
    h->samples[at] = (tiku_sample_t){ time_us, valid ? value : 0, !!valid };
    return 1;
}

int tiku_history_copy(tiku_history_t *dest, const tiku_history_t *source)
{
    if (!dest || !source || !dest->samples || !source->samples ||
        !dest->capacity || dest == source || dest->samples == source->samples)
        return 0;
    uintptr_t d = (uintptr_t)dest->samples, s = (uintptr_t)source->samples;
    if (d < s ? s - d < dest->capacity * sizeof *dest->samples :
                d - s < source->capacity * sizeof *source->samples) return 0;
    dest->head = dest->count = 0;
    size_t first = source->count > dest->capacity ?
                   source->count - dest->capacity : 0;
    for (size_t i = first; i < source->count; i++) {
        const tiku_sample_t *s = tiku_history_at(source, i);
        tiku_history_push(dest, s->time_us, s->value, s->valid);
    }
    return 1;
}

tiku_sensor_status_t tiku_history_status(const tiku_history_t *h,
    int connected, int64_t now_us, int64_t stale_us)
{
    if (!connected) return TIKU_SENSOR_DISCONNECTED;
    const tiku_sample_t *s = h ? tiku_history_at(h, h->count - 1) : NULL;
    if (!s) return TIKU_SENSOR_EMPTY;
    if (!s->valid || now_us < s->time_us ||
        (stale_us > 0 && now_us - s->time_us > stale_us)) return TIKU_SENSOR_STALE;
    return TIKU_SENSOR_LIVE;
}

int tiku_history_range(const tiku_history_t *h, int64_t from, int64_t to,
                       float *min, float *max)
{
    int found = 0;
    if (!h || !min || !max || from < 0 || to < from) return 0;
    for (size_t i = 0; i < h->count; i++) {
        const tiku_sample_t *s = tiku_history_at(h, i);
        if (!s->valid || s->time_us < from || s->time_us > to) continue;
        if (!found || s->value < *min) *min = s->value;
        if (!found || s->value > *max) *max = s->value;
        found = 1;
    }
    return found;
}

const tiku_sample_t *tiku_history_nearest(const tiku_history_t *h, int64_t time)
{
    if (!h || !h->count || time < 0) return NULL;
    size_t lo = 0, hi = h->count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (tiku_history_at(h, mid)->time_us < time) lo = mid + 1;
        else hi = mid;
    }
    if (!lo) return tiku_history_at(h, 0);
    const tiku_sample_t *before = tiku_history_at(h, lo - 1);
    const tiku_sample_t *after = tiku_history_at(h, lo);
    if (!after || time - before->time_us <= after->time_us - time) return before;
    return after;
}

void tiku_history_reduce(const tiku_history_t *h, int64_t from, int64_t to,
    int width, int64_t gap_us, tiku_sensor_span_fn emit, void *ctx)
{
    tiku_sensor_span_t span = {0};
    int active = 0, connect = 0;
    int64_t previous = 0;
    if (!h || !emit || from < 0 || to <= from || width < 1 || width > 8192) return;
    for (size_t i = 0; i < h->count; i++) {
        const tiku_sample_t *s = tiku_history_at(h, i);
        if (s->time_us < from) continue;
        if (s->time_us > to) break;
        if (!s->valid || (active && gap_us > 0 &&
                          s->time_us - previous > gap_us)) {
            if (active) emit(ctx, &span);
            active = connect = 0;
        }
        if (!s->valid) continue;
        int x = (int)((double)(s->time_us - from) / (double)(to - from) *
                       (width - 1));
        if (active && x != span.column) {
            emit(ctx, &span);
            active = 0;
            connect = 1;
        }
        if (!active) {
            span = (tiku_sensor_span_t){ x, connect,
                        s->value, s->value, s->value, s->value };
            active = 1;
        } else {
            span.last = s->value;
            if (s->value < span.min) span.min = s->value;
            if (s->value > span.max) span.max = s->value;
        }
        previous = s->time_us;
    }
    if (active) emit(ctx, &span);
}

const char *tiku_sensor_status_text(tiku_sensor_status_t status)
{
    switch (status) {
    case TIKU_SENSOR_LIVE: return "Live";
    case TIKU_SENSOR_STALE: return "Stale";
    case TIKU_SENSOR_DISCONNECTED: return "Disconnected";
    default: return "No data";
    }
}
