/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_test_pool.c - first-fit storage with bounded metadata and allocation faults.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_test_pool.h"
#include <stdint.h>
#include <string.h>

/** @brief Find an aligned gap by scanning the finite allocation records. */
static void *pool_allocate(void *context, size_t bytes, size_t alignment)
{
    tiku_test_pool_t *pool = context;
    size_t offset = 0, vacant, i, pass;
    ++pool->calls;
    if (!bytes || !alignment || (alignment & (alignment - 1)) ||
        pool->calls == pool->fail_at) {
        return NULL;
    }
    for (vacant = 0; vacant < pool->slot_count; ++vacant) {
        if (!pool->slots[vacant].bytes) {
            break;
        }
    }
    if (vacant == pool->slot_count) {
        return NULL;
    }
    for (pass = 0; pass <= pool->slot_count; ++pass) {
        uintptr_t address = (uintptr_t)pool->memory + offset;
        size_t pad = (size_t)((0 - address) & (alignment - 1));
        int overlap = 0;
        if (pad > pool->capacity - offset) {
            return NULL;
        }
        offset += pad;
        if (bytes > pool->capacity - offset) {
            return NULL;
        }
        for (i = 0; i < pool->slot_count; ++i) {
            tiku_test_pool_slot_t *slot = &pool->slots[i];
            if (slot->bytes && offset < slot->offset + slot->bytes &&
                slot->offset < offset + bytes) {
                offset = slot->offset + slot->bytes;
                overlap = 1;
                break;
            }
        }
        if (!overlap) {
            pool->slots[vacant] = (tiku_test_pool_slot_t){offset, bytes, alignment};
            pool->used += bytes;
            ++pool->live;
            if (pool->peak < pool->used) {
                pool->peak = pool->used;
            }
            return pool->memory + offset;
        }
    }
    return NULL;
}

/** @brief Release a matching record; count wrong-context or malformed releases. */
static void pool_release(void *context, void *pointer, size_t bytes, size_t alignment)
{
    tiku_test_pool_t *pool = context;
    size_t i;
    if (!pointer) {
        return;
    }
    for (i = 0; i < pool->slot_count; ++i) {
        tiku_test_pool_slot_t *slot = &pool->slots[i];
        if (slot->bytes == bytes && slot->alignment == alignment && bytes &&
            pointer == pool->memory + slot->offset) {
            slot->bytes = 0;
            pool->used -= bytes;
            --pool->live;
            return;
        }
    }
    ++pool->faults;
}

static const tiku_allocator_ops_t pool_ops = {pool_allocate, pool_release};

int tiku_test_pool_init(tiku_test_pool_t *pool, void *memory, size_t capacity,
                        tiku_test_pool_slot_t *slots, size_t count)
{
    if (!pool || !memory || !capacity || !slots || !count ||
        count > SIZE_MAX / sizeof(*slots) || capacity > UINTPTR_MAX - (uintptr_t)memory) {
        return 0;
    }
    memset(slots, 0, count * sizeof(*slots));
    *pool = (tiku_test_pool_t){0};
    pool->memory = memory;
    pool->capacity = capacity;
    pool->slots = slots;
    pool->slot_count = count;
    return 1;
}

tiku_allocator_t tiku_test_pool_allocator(tiku_test_pool_t *pool)
{
    return (tiku_allocator_t){&pool_ops, pool};
}

int tiku_test_pool_reset(tiku_test_pool_t *pool)
{
    if (!pool || pool->live) {
        return 0;
    }
    pool->peak = pool->calls = pool->fail_at = pool->faults = 0;
    return 1;
}
