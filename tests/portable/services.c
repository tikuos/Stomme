/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * services.c - ownership, bounded input, deferred shutdown and clock failures.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_test_pool.h"
#include "tiku_clock.h"
#include "tiku_event_queue.h"
#include <stdint.h>
#include <string.h>

#define CHECK(condition) do { if (!(condition)) return __LINE__; } while (0)

static _Alignas(64) unsigned char memory_a[8192], memory_b[8192];
static tiku_test_pool_slot_t slots_a[32], slots_b[32];
static tiku_test_pool_t pool_a, pool_b;

/** @brief Supply an invalidly aligned result for provider contract testing. */
static void *misaligned_allocate(void *context, size_t bytes, size_t alignment)
{
    (void)context; (void)bytes; (void)alignment;
    return memory_a + 1;
}
/** @brief Count disposal of the rejected provider result. */
static void misaligned_release(void *context, void *pointer, size_t bytes, size_t alignment)
{
    unsigned *released = context;
    (void)pointer; (void)bytes; (void)alignment;
    ++*released;
}

/** @brief Exercise alignment, resize failure, borrowing and independent owners. */
static int buffers(void)
{
    tiku_allocator_t a = tiku_test_pool_allocator(&pool_a);
    tiku_allocator_t b = tiku_test_pool_allocator(&pool_b);
    tiku_owned_buffer_t first, second, borrowed;
    unsigned char external[4] = {1, 2, 3, 4};
    void *original;
    size_t calls;
    unsigned released = 0;
    static const tiku_allocator_ops_t bad_ops = {misaligned_allocate, misaligned_release};
    tiku_owned_buffer_t rejected;
    CHECK(tiku_buffer_init(&rejected, (tiku_allocator_t){&bad_ops, &released}, 64));
    CHECK(!tiku_buffer_resize(&rejected, 8, 1, 0));
    CHECK(!rejected.data && released == 1);
    CHECK(!tiku_buffer_init(&first, a, 3));
    CHECK(tiku_buffer_init(&first, a, 64));
    CHECK(tiku_buffer_init(&second, b, 16));
    a.context = &pool_b;
    CHECK(tiku_buffer_resize(&first, 32, 1, 1));
    CHECK(!((uintptr_t)first.data & 63));
    CHECK(((unsigned char *)first.data)[31] == 0);
    memset(first.data, 0x5a, first.bytes);
    original = first.data;
    calls = pool_a.calls;
    CHECK(!tiku_buffer_resize(&first, SIZE_MAX, 2, 0));
    CHECK(pool_a.calls == calls && first.data == original);
    pool_a.fail_at = pool_a.calls + 1;
    CHECK(!tiku_buffer_resize(&first, 64, 1, 1));
    CHECK(first.data == original && first.bytes == 32);
    CHECK(((unsigned char *)first.data)[31] == 0x5a);
    CHECK(tiku_buffer_resize(&second, 128, 1, 1));
    CHECK(tiku_buffer_resize(&first, 64, 1, 1));
    CHECK(((unsigned char *)first.data)[31] == 0x5a);
    CHECK(((unsigned char *)first.data)[32] == 0);
    CHECK(!tiku_test_pool_reset(&pool_a));
    b.ops->release(b.context, first.data, first.bytes, first.alignment);
    CHECK(pool_b.faults == 1 && pool_a.live == 1 && pool_b.live == 1);
    CHECK(tiku_buffer_borrow(&borrowed, external, sizeof external));
    CHECK(!tiku_buffer_resize(&borrowed, 8, 1, 0));
    tiku_buffer_dispose(&borrowed);
    CHECK(external[3] == 4 && pool_b.live == 1);
    tiku_buffer_dispose(&first);
    CHECK(pool_a.live == 0 && pool_b.live == 1);
    CHECK(tiku_buffer_resize(&second, 0, 1, 0));
    CHECK(!pool_b.live && !pool_b.used);
    tiku_buffer_dispose(NULL);
    tiku_buffer_dispose(&second);
    CHECK(pool_a.peak >= 96 && !pool_a.faults);
    CHECK(tiku_test_pool_reset(&pool_a) && tiku_test_pool_reset(&pool_b));
    return 0;
}

/** @brief Exercise bounded gaps, allocation records, capacity and reuse. */
static int exhaustion(void)
{
    tiku_owned_buffer_t buffers[32];
    tiku_allocator_t allocator = tiku_test_pool_allocator(&pool_a);
    size_t i;
    for (i = 0; i < 32; ++i) {
        CHECK(tiku_buffer_init(&buffers[i], allocator, 8));
        CHECK(tiku_buffer_resize(&buffers[i], 256, 1, 1));
    }
    CHECK(pool_a.used == 8192);
    CHECK(!tiku_buffer_resize(&buffers[0], 257, 1, 0));
    for (i = 0; i < 32; i += 2) {
        tiku_buffer_dispose(&buffers[i]);
    }
    for (i = 0; i < 32; i += 2) {
        CHECK(tiku_buffer_resize(&buffers[i], 128, 1, 0));
    }
    for (i = 0; i < 32; ++i) {
        tiku_buffer_dispose(&buffers[i]);
    }
    CHECK(!pool_a.live && !pool_a.used && !pool_a.faults);
    return 0;
}

/** @brief Test partial startup, queue pressure, generations and deferred teardown. */
static int queues(void)
{
    tiku_allocator_t allocator = tiku_test_pool_allocator(&pool_a);
    tiku_event_queue_t *queue;
    tiku_event_ticket_t old = {0}, current = {0};
    tiku_input_event_t down = {0}, up = {0}, output = {0};
    size_t fail, cycle;
    down.type = TIKU_EVENT_POINTER_DOWN;
    up.type = TIKU_EVENT_POINTER_UP;
    CHECK(!tiku_event_queue_create(allocator, SIZE_MAX));
    for (fail = 1; fail <= 2; ++fail) {
        CHECK(tiku_test_pool_reset(&pool_a));
        pool_a.fail_at = fail;
        CHECK(!tiku_event_queue_create(allocator, 2));
        CHECK(!pool_a.live && !pool_a.used);
    }
    CHECK(tiku_test_pool_reset(&pool_a));
    for (cycle = 0; cycle < 100; ++cycle) {
        queue = tiku_event_queue_create(allocator, 2);
        CHECK(queue && pool_a.live == 2);
        CHECK(!tiku_event_queue_destroy(&queue));
        CHECK(tiku_event_queue_push(queue, &down));
        CHECK(tiku_event_queue_take(queue, &output) == TIKU_QUEUE_EVENT);
        CHECK(output.type == TIKU_EVENT_POINTER_DOWN);
        CHECK(tiku_event_ticket_begin(queue, &old));
        CHECK(tiku_event_queue_push(queue, &down));
        CHECK(tiku_event_queue_push(queue, &up));
        CHECK(!tiku_event_queue_push(queue, &down));
        CHECK(!tiku_event_queue_push(queue, &up));
        CHECK(tiku_event_queue_take(queue, &output) == TIKU_QUEUE_CANCEL);
        CHECK(!tiku_event_ticket_post(&old, &down));
        CHECK(tiku_event_queue_take(queue, &output) == TIKU_QUEUE_EMPTY);
        CHECK(tiku_event_ticket_begin(queue, &old));
        tiku_event_queue_close(queue);
        CHECK(!tiku_event_queue_destroy(&queue));
        CHECK(!tiku_test_pool_reset(&pool_a));
        CHECK(tiku_event_queue_open(queue));
        CHECK(tiku_event_queue_take(queue, &output) == TIKU_QUEUE_CANCEL);
        CHECK(!tiku_event_ticket_post(&old, &down));
        CHECK(tiku_event_queue_take(queue, &output) == TIKU_QUEUE_EMPTY);
        CHECK(tiku_event_ticket_begin(queue, &current));
        CHECK(tiku_event_ticket_post(&current, &down));
        CHECK(!tiku_event_ticket_post(&current, &down));
        CHECK(tiku_event_queue_take(queue, &output) == TIKU_QUEUE_EVENT);
        CHECK(tiku_event_ticket_begin(queue, &old));
        tiku_event_queue_close(queue);
        CHECK(!tiku_event_queue_destroy(&queue));
        tiku_event_ticket_cancel(&old);
        tiku_event_ticket_cancel(&old);
        CHECK(tiku_event_queue_destroy(&queue) && !queue);
        CHECK(!pool_a.live && !pool_a.used && !pool_a.faults);
    }
    return 0;
}

/** @brief Supply a caller-controlled monotonic test reading. */
static int read_time(void *context, uint64_t *time)
{
    uint64_t *now = context;
    *time = *now;
    return *now != 0;
}

/** @brief Reject clock regressions, provider failure and overflowing deadlines. */
static int clocks(void)
{
    uint64_t now = 40, output = 0, deadline = 5;
    tiku_clock_t clock = {read_time, &now, 0};
    CHECK(tiku_clock_now(&clock, &output) && output == 40);
    CHECK(tiku_clock_after(&clock, 20, &deadline) && deadline == 60);
    now = 39;
    CHECK(!tiku_clock_now(&clock, &output) && output == 40 && clock.last == 40);
    now = 0;
    CHECK(!tiku_clock_after(&clock, 1, &deadline) && deadline == 60);
    now = UINT64_MAX - 1;
    CHECK(!tiku_clock_after(&clock, 2, &deadline) && deadline == 60);
    CHECK(tiku_clock_after(&clock, 1, &deadline) && deadline == UINT64_MAX);
    return 0;
}

/** @brief Run the same bounded service workload for host execution and cross linking. */
int tiku_services_run_tests(void)
{
    int result;
    CHECK(tiku_test_pool_init(&pool_a, memory_a, sizeof memory_a, slots_a, 32));
    CHECK(tiku_test_pool_init(&pool_b, memory_b, sizeof memory_b, slots_b, 32));
    result = buffers();
    if (!result) result = exhaustion();
    if (!result) result = queues();
    if (!result) result = clocks();
    return result;
}

#ifndef TIKU_SERVICES_LINK_ONLY
#ifdef TIKU_SERVICE_HEAP_TRAP
/** @brief Number of direct calls caught by the host heap wrappers. */
extern unsigned tiku_service_heap_calls;
#endif
/** @brief Return failure if any service check or heap trap fails. */
int main(void)
{
    int result = tiku_services_run_tests();
#ifdef TIKU_SERVICE_HEAP_TRAP
    result |= tiku_service_heap_calls != 0;
#endif
    return result ? 1 : 0;
}
#endif
