/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_memory_presenter.h - delayed RGB565 presentation fixture.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_MEMORY_PRESENTER_H_
#define TIKU_MEMORY_PRESENTER_H_
#include "tiku_framebuffer.h"

/** @brief Owner-confined sink; initialize pending and counters to zero. */
typedef struct {
    unsigned char *pixels;
    size_t bytes, stride;
    const tiku_present_view_t *pending;
    size_t frames;
    tiku_present_result_t reject;
} tiku_memory_presenter_t;

/** @brief Convert damaged XRGB8888 pixels to little-endian RGB565; validate bounds first. */
int tiku_memory_rgb565(const tiku_present_view_t *view, unsigned char *destination,
                        size_t bytes, size_t stride);
/** @brief Bind a delayed sink; no copy is made until finish. */
tiku_presenter_t tiku_memory_presenter(tiku_memory_presenter_t *sink);
/** @brief Copy damage and complete the owner; conversion failure completes with ERROR. */
int tiku_memory_presenter_finish(tiku_memory_presenter_t *sink, tiku_framebuffer_t *frame);
#endif
