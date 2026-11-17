/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_framebuffer.c - allocator ownership and presentation state transitions.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_framebuffer.h"

struct tiku_framebuffer {
    tiku_owned_buffer_t self, pixels;
    tiku_surface_t surface;
    tiku_present_view_t view;
    tiku_present_result_t result;
    enum { FRAME_IDLE, FRAME_DRAWING, FRAME_SUBMITTING, FRAME_IN_FLIGHT } state;
};

/** @brief Validate the fixed dimensions and compute the packed pixel byte count. */
static size_t pixel_bytes(int width, int height)
{
    if (width < 1 || height < 1 || width > 32767 || height > 32767 ||
        (size_t)width > SIZE_MAX / sizeof(tiku_rgb_t) / (size_t)height) return 0;
    return (size_t)width * (size_t)height * sizeof(tiku_rgb_t);
}

/** @brief Allocate an owner and bind its drawing/presentation views. */
static tiku_framebuffer_t *create(tiku_allocator_t allocator, int width, int height,
                                   tiku_rgb_t *external, size_t available, int borrowed)
{
    size_t bytes = pixel_bytes(width, height);
    tiku_owned_buffer_t self;
    tiku_framebuffer_t *frame;
    if (!bytes || (borrowed && (!external || available < bytes ||
        (uintptr_t)external % _Alignof(tiku_rgb_t))) ||
        !tiku_buffer_init(&self, allocator, _Alignof(tiku_framebuffer_t)) ||
        !tiku_buffer_resize(&self, 1, sizeof(*frame), 1)) return NULL;
    frame = self.data;
    frame->self = self;
    if (borrowed) {
        (void)tiku_buffer_borrow(&frame->pixels, external, bytes);
    } else if (!tiku_buffer_init(&frame->pixels, allocator, _Alignof(tiku_rgb_t)) ||
               !tiku_buffer_resize(&frame->pixels, bytes, 1, 0)) {
        tiku_buffer_dispose(&self);
        return NULL;
    }
    frame->surface.px = frame->pixels.data;
    frame->surface.w = width;
    frame->surface.h = height;
    frame->view = (tiku_present_view_t){frame->pixels.data, bytes,
        (size_t)width * sizeof(tiku_rgb_t), width, height,
        TIKU_PIXELS_XRGB8888, {0, 0, width, height}, 0, frame};
    tiku_clip_reset(&frame->surface);
    return frame;
}

tiku_framebuffer_t *tiku_framebuffer_create(tiku_allocator_t allocator,
                                             int width, int height, tiku_rgb_t background)
{
    tiku_framebuffer_t *frame = create(allocator, width, height, NULL, 0, 0);
    if (frame) tiku_fill(&frame->surface, frame->view.damage, background);
    return frame;
}

tiku_framebuffer_t *tiku_framebuffer_borrow(tiku_allocator_t allocator,
                                             tiku_rgb_t *pixels, size_t bytes,
                                             int width, int height)
{
    return create(allocator, width, height, pixels, bytes, 1);
}

tiku_surface_t *tiku_framebuffer_begin(tiku_framebuffer_t *frame, const tiku_rect_t *damage)
{
    tiku_rect_t region;
    if (!frame || frame->state != FRAME_IDLE) return NULL;
    region = (tiku_rect_t){0, 0, frame->view.width, frame->view.height};
    if (damage) {
        int64_t right = (int64_t)damage->x + damage->w;
        int64_t bottom = (int64_t)damage->y + damage->h;
        int left = damage->x > 0 ? damage->x : 0;
        int top = damage->y > 0 ? damage->y : 0;
        if (right > region.w) right = region.w;
        if (bottom > region.h) bottom = region.h;
        if (damage->w <= 0 || damage->h <= 0 || right <= left || bottom <= top) return NULL;
        region = (tiku_rect_t){left, top, (int)(right - left), (int)(bottom - top)};
    }
    frame->view.damage = region;
    tiku_clip_damage(&frame->surface, &region);
    frame->state = FRAME_DRAWING;
    return &frame->surface;
}

int tiku_framebuffer_abandon(tiku_framebuffer_t *frame)
{
    if (!frame || frame->state != FRAME_DRAWING) return 0;
    frame->state = FRAME_IDLE;
    return 1;
}

tiku_present_result_t tiku_framebuffer_present(tiku_framebuffer_t *frame,
                                                tiku_presenter_t presenter)
{
    tiku_present_result_t result;
    if (!frame || !presenter.submit || frame->state != FRAME_DRAWING ||
        frame->view.token == UINT64_MAX) return TIKU_PRESENT_ERROR;
    ++frame->view.token;
    frame->state = FRAME_SUBMITTING;
    result = presenter.submit(presenter.context, &frame->view);
    if (result != TIKU_PRESENT_OK && result != TIKU_PRESENT_BUSY) result = TIKU_PRESENT_ERROR;
    frame->state = result == TIKU_PRESENT_OK ? FRAME_IN_FLIGHT : FRAME_DRAWING;
    frame->result = result;
    return result;
}

int tiku_framebuffer_complete(tiku_framebuffer_t *frame, uint64_t token,
                               tiku_present_result_t result)
{
    if (!frame || frame->state != FRAME_IN_FLIGHT || token != frame->view.token ||
        (result != TIKU_PRESENT_OK && result != TIKU_PRESENT_ERROR)) return 0;
    frame->state = FRAME_IDLE;
    frame->result = result;
    return 1;
}

tiku_present_result_t tiku_framebuffer_result(const tiku_framebuffer_t *frame)
{
    return frame ? frame->result : TIKU_PRESENT_ERROR;
}

int tiku_framebuffer_destroy(tiku_framebuffer_t **frame)
{
    tiku_owned_buffer_t self;
    if (!frame || !*frame) return 1;
    if ((*frame)->state != FRAME_IDLE) return 0;
    self = (*frame)->self;
    tiku_buffer_dispose(&(*frame)->pixels);
    tiku_buffer_dispose(&self);
    *frame = NULL;
    return 1;
}
