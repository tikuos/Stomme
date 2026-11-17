/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * rendering.c - presentation ownership, pixel bounds and panel input.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "panel.h"
#include "tiku_test_pool.h"
#include "tiku_memory_presenter.h"
#include <limits.h>
#include <stdio.h>
#include <string.h>

#define CHECK(condition) do { if (!(condition)) return __LINE__; } while (0)
static _Alignas(64) unsigned char memory[2*1024*1024];
static tiku_test_pool_slot_t slots[32];
static tiku_test_pool_t pool;
static unsigned char display[800*480*2];
#ifdef TIKU_SERVICE_HEAP_TRAP
/** @brief Direct heap calls recorded by the link wrappers. */
extern unsigned tiku_service_heap_calls;
#endif

/** @brief Validate primary colors, padded strides, damaged conversion and bounds. */
static int conversion(void)
{
    const tiku_rgb_t source[8] = {0xff0000, 0x00ff00, 0x0000ff, 0,
                                  0xffffff, 0, 0x808080, 0};
    unsigned char output[20];
    const unsigned char expected[6] = {0x00,0xf8,0xe0,0x07,0x1f,0x00};
    tiku_present_view_t view = {source, sizeof source, 16, 3, 2,
        TIKU_PIXELS_XRGB8888, {0,0,3,1}, 0, NULL};
    memset(output, 0xa5, sizeof output);
    CHECK(tiku_memory_rgb565(&view, output + 2, 16, 8));
    CHECK(!memcmp(output + 2, expected, 6));
    CHECK(output[0] == 0xa5 && output[1] == 0xa5 && output[8] == 0xa5);
    CHECK(output[10] == 0xa5 && output[19] == 0xa5);
    CHECK(!tiku_memory_rgb565(&view, output, 15, 8));
    CHECK(!tiku_memory_rgb565(&view, output, 20, SIZE_MAX));
    view.bytes = 31;
    CHECK(!tiku_memory_rgb565(&view, output, 20, 8));
    view.bytes = sizeof source;
    view.damage.x = INT_MAX;
    CHECK(!tiku_memory_rgb565(&view, output, 20, 8));
    view.damage = (tiku_rect_t){0,1,3,1};
    CHECK(tiku_memory_rgb565(&view, output, 20, 8));
    CHECK(output[8] == 0xff && output[9] == 0xff && output[10] == 0 && output[11] == 0);
    CHECK(output[12] == 0x10 && output[13] == 0x84);
    return 0;
}

/** @brief Exercise borrowed pixels, clipping, alpha and deferred ownership. */
static int ownership(void)
{
    tiku_allocator_t allocator = tiku_test_pool_allocator(&pool);
    tiku_rgb_t external[18], red = 0xff0000;
    tiku_framebuffer_t *frame, *other;
    tiku_surface_t *surface, source = {0};
    tiku_memory_presenter_t sink = {display, sizeof display, 8, NULL, 0, TIKU_PRESENT_OK};
    uint64_t token;
    size_t fail;
    CHECK(!tiku_framebuffer_create(allocator, INT_MAX, 2, 0));
    for (fail = 1; fail <= 2; ++fail) {
        CHECK(tiku_test_pool_reset(&pool));
        pool.fail_at = fail;
        CHECK(!tiku_framebuffer_create(allocator, 4, 4, 0));
        CHECK(pool.live == 0);
    }
    CHECK(tiku_test_pool_reset(&pool));
    for (size_t i = 0; i < 18; ++i) external[i] = 0x123456;
    frame = tiku_framebuffer_borrow(allocator, external+1, 16*sizeof(tiku_rgb_t), 4, 4);
    CHECK(frame && pool.live == 1);
    CHECK(!tiku_framebuffer_begin(frame, &(tiku_rect_t){INT_MAX,0,INT_MAX,2}));
    surface = tiku_framebuffer_begin(frame, &(tiku_rect_t){-1,-1,3,3});
    CHECK(surface && surface->clip.w == 2 && surface->clip.h == 2);
    CHECK(!tiku_framebuffer_begin(frame, NULL));
    CHECK(!tiku_framebuffer_destroy(&frame));
    tiku_fill(surface, (tiku_rect_t){0,0,4,4}, 0xffffff);
    CHECK(external[1] == 0xffffff && external[2] == 0xffffff);
    CHECK(external[3] == 0x123456 && external[5] == 0xffffff);
    CHECK(external[0] == 0x123456 && external[17] == 0x123456);
    source.px = &red; source.w = source.h = 1;
    tiku_clip_reset(&source);
    tiku_blend(surface, 0, 0, &source, 0.5f);
    CHECK(external[1] == 0xff7f7f);
    sink.reject = TIKU_PRESENT_BUSY;
    CHECK(tiku_framebuffer_present(frame, tiku_memory_presenter(&sink)) == TIKU_PRESENT_BUSY);
    CHECK(!sink.pending && !tiku_framebuffer_begin(frame, NULL));
    sink.reject = TIKU_PRESENT_ERROR;
    CHECK(tiku_framebuffer_present(frame, tiku_memory_presenter(&sink)) == TIKU_PRESENT_ERROR);
    sink.reject = TIKU_PRESENT_OK;
    CHECK(tiku_framebuffer_present(frame, tiku_memory_presenter(&sink)) == TIKU_PRESENT_OK);
    token = sink.pending->token;
    CHECK(!tiku_framebuffer_begin(frame, NULL));
    CHECK(!tiku_framebuffer_abandon(frame));
    CHECK(!tiku_framebuffer_destroy(&frame));
    CHECK(!tiku_framebuffer_complete(frame, token + 1, TIKU_PRESENT_OK));
    CHECK(!tiku_framebuffer_complete(frame, token, TIKU_PRESENT_BUSY));
    other = tiku_framebuffer_create(allocator, 4, 4, 0);
    CHECK(other && !tiku_memory_presenter_finish(&sink, other));
    CHECK(tiku_framebuffer_destroy(&other));
    CHECK(tiku_memory_presenter_finish(&sink, frame) && sink.frames == 1);
    CHECK(!tiku_framebuffer_complete(frame, token, TIKU_PRESENT_OK));
    CHECK(tiku_framebuffer_begin(frame, NULL));
    CHECK(tiku_framebuffer_present(frame, tiku_memory_presenter(&sink)) == TIKU_PRESENT_OK);
    CHECK(!tiku_framebuffer_complete(frame, token, TIKU_PRESENT_OK));
    sink.bytes = 1;
    CHECK(tiku_memory_presenter_finish(&sink, frame));
    CHECK(tiku_framebuffer_result(frame) == TIKU_PRESENT_ERROR);
    CHECK(tiku_framebuffer_destroy(&frame));
    CHECK(pool.live == 0 && external[0] == 0x123456 && external[17] == 0x123456);
    return 0;
}

/** @brief Post normalized pointer input to the panel. */
static int post(tiku_portable_panel_t *panel, tiku_event_type_t type, int x, int y)
{
    tiku_input_event_t event = {0};
    event.type = type; event.x = x; event.y = y;
    return tiku_event_queue_push(panel->events, &event);
}

/** @brief Verify partial startup, input cancellation and allocation-free repaint. */
static int panel_workload(void)
{
    tiku_portable_panel_t panel;
    tiku_allocator_t allocator = tiku_test_pool_allocator(&pool);
    tiku_memory_presenter_t sink = {display, sizeof display, 1600, NULL, 0, TIKU_PRESENT_OK};
    size_t fail, calls, live, peak, i;
    for (fail = 1; fail <= 4; ++fail) {
        CHECK(tiku_test_pool_reset(&pool));
        pool.fail_at = fail;
        CHECK(!tiku_portable_panel_init(&panel, allocator));
        CHECK(!pool.live && !pool.used);
    }
    CHECK(tiku_test_pool_reset(&pool));
    CHECK(tiku_portable_panel_init(&panel, allocator));
    live = pool.used; calls = pool.calls;
    CHECK(live < 1536000 + 524288);
    CHECK(tiku_portable_panel_render(&panel, tiku_memory_presenter(&sink)) == TIKU_PRESENT_OK);
    CHECK(!tiku_portable_panel_stop(&panel));
    CHECK(post(&panel, TIKU_EVENT_POINTER_DOWN, 300, 115));
    CHECK(tiku_portable_panel_dispatch(&panel, 1) == 1 && panel.slider.tracking);
    CHECK(tiku_portable_panel_render(&panel, tiku_memory_presenter(&sink)) == TIKU_PRESENT_BUSY);
    CHECK(tiku_memory_presenter_finish(&sink, panel.frame));
    CHECK(panel.slider.value > 50);
    for (i = 0; i < 17; ++i) (void)post(&panel, TIKU_EVENT_POINTER_MOVE, 350, 115);
    CHECK(tiku_portable_panel_dispatch(&panel, 1) == 1);
    CHECK(!panel.slider.tracking && !panel.press.id);
    CHECK(post(&panel, TIKU_EVENT_POINTER_DOWN, 40, 260));
    CHECK(post(&panel, TIKU_EVENT_POINTER_UP, 40, 260));
    CHECK(tiku_portable_panel_dispatch(&panel, 1) == 1 && !panel.recording);
    CHECK(tiku_portable_panel_dispatch(&panel, 1) == 1 && panel.recording);
    for (i = 0; i < 100; ++i) {
        CHECK(post(&panel, TIKU_EVENT_POINTER_DOWN, 40, 200));
        CHECK(post(&panel, TIKU_EVENT_POINTER_UP, 40, 200));
        CHECK(tiku_portable_panel_dispatch(&panel, 4) == 2);
        CHECK(panel.slider.value == 25);
        CHECK(tiku_portable_panel_render(&panel, tiku_memory_presenter(&sink)) == TIKU_PRESENT_OK);
        CHECK(sink.pending->damage.w < 800);
        CHECK(tiku_memory_presenter_finish(&sink, panel.frame));
        CHECK(pool.calls == calls && pool.used == live);
    }
    peak = pool.peak;
    CHECK(tiku_portable_panel_stop(&panel));
    CHECK(!pool.used && !pool.live && !pool.faults);
#ifdef TIKU_SERVICE_HEAP_TRAP
    CHECK(tiku_service_heap_calls == 0);
#endif
    printf("render_surface=1536000 live=%zu peak=%zu paint_allocations=0 frames=%zu\n", live, peak, sink.frames);
    return 0;
}

/** @brief Run raster and ownership fixtures without heap-backed test storage. */
int main(void)
{
    int result;
    CHECK(tiku_test_pool_init(&pool, memory, sizeof memory, slots, 32));
    result = conversion();
    if (!result) result = ownership();
    if (!result) result = panel_workload();
    if (result) fprintf(stderr, "rendering check failed at line %d\n", result);
    return result ? 1 : 0;
}
