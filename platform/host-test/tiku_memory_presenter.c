/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_memory_presenter.c - deferred conversion with checked row bounds.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_memory_presenter.h"

int tiku_memory_rgb565(const tiku_present_view_t *view, unsigned char *destination,
                        size_t bytes, size_t stride)
{
    int x, y;
    tiku_rect_t damage;
    if (!view || !view->pixels || !destination || view->format != TIKU_PIXELS_XRGB8888 ||
        (uintptr_t)view->pixels % _Alignof(tiku_rgb_t) || view->width < 1 || view->height < 1 || view->width > 32767 || view->height > 32767 ||
        view->stride < (size_t)view->width * 4 || view->stride % sizeof(tiku_rgb_t) ||
        stride < (size_t)view->width * 2 ||
        view->stride > SIZE_MAX / (size_t)view->height ||
        stride > SIZE_MAX / (size_t)view->height ||
        view->bytes < view->stride * (size_t)view->height ||
        bytes < stride * (size_t)view->height) return 0;
    damage = view->damage;
    if (damage.x < 0 || damage.y < 0 || damage.w < 0 || damage.h < 0 ||
        (int64_t)damage.x + damage.w > view->width ||
        (int64_t)damage.y + damage.h > view->height) return 0;
    for (y = damage.y; y < damage.y + damage.h; ++y) {
        const tiku_rgb_t *source = view->pixels + (size_t)y * (view->stride / sizeof(tiku_rgb_t));
        unsigned char *out = destination + (size_t)y * stride;
        for (x = damage.x; x < damage.x + damage.w; ++x) {
            tiku_rgb_t rgb = source[x];
            unsigned packed = ((rgb >> 8) & 0xf800u) | ((rgb >> 5) & 0x07e0u) | ((rgb >> 3) & 0x001fu);
            out[2 * x] = (unsigned char)packed;
            out[2 * x + 1] = (unsigned char)(packed >> 8);
        }
    }
    return 1;
}

/** @brief Retain accepted input until the owner explicitly finishes it. */
static tiku_present_result_t submit(void *context, const tiku_present_view_t *view)
{
    tiku_memory_presenter_t *sink = context;
    if (!sink) return TIKU_PRESENT_ERROR;
    if (sink->reject != TIKU_PRESENT_OK) return sink->reject;
    if (sink->pending) return TIKU_PRESENT_BUSY;
    sink->pending = view;
    return TIKU_PRESENT_OK;
}

tiku_presenter_t tiku_memory_presenter(tiku_memory_presenter_t *sink)
{
    return (tiku_presenter_t){submit, sink};
}

int tiku_memory_presenter_finish(tiku_memory_presenter_t *sink, tiku_framebuffer_t *frame)
{
    int converted;
    uint64_t token;
    if (!sink || !sink->pending || sink->pending->owner != frame) return 0;
    token = sink->pending->token;
    converted = tiku_memory_rgb565(sink->pending, sink->pixels, sink->bytes, sink->stride);
    if (!tiku_framebuffer_complete(frame, token, converted ? TIKU_PRESENT_OK : TIKU_PRESENT_ERROR)) return 0;
    sink->pending = NULL;
    if (converted) ++sink->frames;
    return 1;
}
