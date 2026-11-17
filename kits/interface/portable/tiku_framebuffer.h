/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_framebuffer.h - owned raster buffers and deferred presentation.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_FRAMEBUFFER_H_
#define TIKU_FRAMEBUFFER_H_
#include "tiku_gfx.h"
#include "tiku_allocator.h"

/** @brief Opaque owner; its drawing surface is a borrowed view. */
typedef struct tiku_framebuffer tiku_framebuffer_t;
/** @brief Submission or completion outcome. */
typedef enum {
    TIKU_PRESENT_ERROR = -1,
    TIKU_PRESENT_OK = 0,
    TIKU_PRESENT_BUSY = 1
} tiku_present_result_t;
/** @brief The renderer stores 0x00RRGGBB words with an unused high byte. */
typedef enum { TIKU_PIXELS_XRGB8888 = 1 } tiku_pixel_format_t;
/** @brief Immutable submission; data remains valid until matching completion. */
typedef struct {
    const tiku_rgb_t *pixels;
    size_t bytes, stride;
    int width, height;
    tiku_pixel_format_t format;
    tiku_rect_t damage;
    uint64_t token;
    tiku_framebuffer_t *owner;
} tiku_present_view_t;
/** @brief Serialized provider; OK retains the view until a later completion call. */
typedef struct {
    tiku_present_result_t (*submit)(void *context, const tiku_present_view_t *view);
    void *context;
} tiku_presenter_t;

/** @brief Allocate a scale-one surface, dimensions 1..32767; NULL on failure. */
tiku_framebuffer_t *tiku_framebuffer_create(tiku_allocator_t allocator,
                                             int width, int height, tiku_rgb_t background);
/** @brief Borrow tightly packed pixels; their owner must outlive the framebuffer. */
tiku_framebuffer_t *tiku_framebuffer_borrow(tiku_allocator_t allocator,
                                             tiku_rgb_t *pixels, size_t bytes,
                                             int width, int height);
/**
 * @brief Acquire drawing ownership, clipping optional damage to the surface.
 * @return The borrowed drawing view, or NULL if busy or damage is empty.
 * @note Do not change geometry, scale, recorder or pixel pointer. The view is
 * invalid for drawing after successful submission or abandonment.
 */
tiku_surface_t *tiku_framebuffer_begin(tiku_framebuffer_t *frame,
                                       const tiku_rect_t *damage);
/** @brief Give back drawing ownership without submission; pixels are retained. */
int tiku_framebuffer_abandon(tiku_framebuffer_t *frame);
/**
 * @brief Submit the current damage; BUSY/error retains drawing ownership.
 * @note The provider cannot complete during submit. Failed submit retains no
 * view. An accepted provider must complete before framebuffer destruction.
 */
tiku_present_result_t tiku_framebuffer_present(tiku_framebuffer_t *frame,
                                                tiku_presenter_t presenter);
/** @brief Release a matching submission; stale tokens and BUSY results are rejected. */
int tiku_framebuffer_complete(tiku_framebuffer_t *frame, uint64_t token,
                               tiku_present_result_t result);
/** @brief Return the most recent submission/completion result; ERROR for NULL. */
tiku_present_result_t tiku_framebuffer_result(const tiku_framebuffer_t *frame);
/** @brief Destroy an idle owner; busy failure leaves the pointer intact. */
int tiku_framebuffer_destroy(tiku_framebuffer_t **frame);
#endif
