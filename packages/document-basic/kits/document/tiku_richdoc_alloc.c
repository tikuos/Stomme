/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_richdoc_alloc.c - shared allocation lifetime and byte ceilings.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richdoc_private.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

/** @brief An aligned prefix carrying allocation size and allocator owner. */
typedef union {
    max_align_t align;
    struct {
        tiku_rd_memory_t *owner;
        size_t bytes;
        size_t visits; /**< owner-thread references in a temporary byte census */
    } value;
} header_t;

/** @brief Allocate a block from the C library. */
static void *
heap_allocate(void *ctx, size_t bytes)
{
    (void)ctx;
    return malloc(bytes);
}

/** @brief Release a block to the C library. */
static void
heap_release(void *ctx, void *pointer)
{
    (void)ctx;
    free(pointer);
}

tiku_rich_status_t
tiku_rd_limits(const tiku_rich_limits_t *source, tiku_rich_limits_t *out)
{
    memset(out, 0, sizeof *out);
    if (source)
        *out = *source;
    if (!!out->allocator.allocate != !!out->allocator.release)
        return TIKU_RICH_INVALID;
    if (!out->allocator.allocate) {
        out->allocator.allocate = heap_allocate;
        out->allocator.release = heap_release;
    }
    if (!out->text_bytes) out->text_bytes = (size_t)64 << 20;
    if (!out->paragraphs) out->paragraphs = 250000;
    if (!out->runs) out->runs = 1000000;
    if (!out->styles) out->styles = 4096;
    if (!out->memory_bytes) out->memory_bytes = (size_t)512 << 20;
    if (!out->history_bytes) out->history_bytes = (size_t)64 << 20;
    if (!out->anchors) out->anchors = 4096;
    return TIKU_RICH_OK;
}

tiku_rd_memory_t *
tiku_rd_memory_new(const tiku_rich_limits_t *limits,
                    tiku_rich_status_t *status)
{
    tiku_rd_memory_t *memory;

    if (limits->memory_bytes < sizeof *memory) {
        *status = TIKU_RICH_LIMIT;
        return NULL;
    }
    memory = limits->allocator.allocate(limits->allocator.ctx,
                                        sizeof *memory);
    if (!memory) {
        *status = TIKU_RICH_NO_MEMORY;
        return NULL;
    }
    atomic_init(&memory->refs, 1);
    atomic_init(&memory->used, sizeof *memory);
    memory->limit = limits->memory_bytes;
    memory->hooks = limits->allocator;
    return memory;
}

void
tiku_rd_memory_drop(tiku_rd_memory_t *memory)
{
    if (atomic_fetch_sub_explicit(&memory->refs, 1,
                                  memory_order_acq_rel) == 1)
        memory->hooks.release(memory->hooks.ctx, memory);
}

void *
tiku_rd_alloc(tiku_rd_memory_t *memory, size_t bytes,
              tiku_rich_status_t *status)
{
    header_t *header;
    size_t charge, used = atomic_load_explicit(&memory->used,
                                               memory_order_relaxed);

    if (bytes > SIZE_MAX - sizeof *header) {
        *status = TIKU_RICH_LIMIT;
        return NULL;
    }
    charge = bytes + sizeof *header;
    do {
        if (used > memory->limit || charge > memory->limit - used) {
            *status = TIKU_RICH_LIMIT;
            return NULL;
        }
    } while (!atomic_compare_exchange_weak_explicit(&memory->used, &used,
                used + charge, memory_order_relaxed, memory_order_relaxed));
    header = memory->hooks.allocate(memory->hooks.ctx, charge);
    if (!header) {
        atomic_fetch_sub_explicit(&memory->used, charge,
                                   memory_order_relaxed);
        *status = TIKU_RICH_NO_MEMORY;
        return NULL;
    }
    atomic_fetch_add_explicit(&memory->refs, 1, memory_order_relaxed);
    header->value.owner = memory;
    header->value.bytes = charge;
    header->value.visits = 0;
    memset(header + 1, 0, bytes);
    return header + 1;
}

void
tiku_rd_free(void *pointer)
{
    header_t *header;
    tiku_rd_memory_t *memory;
    size_t bytes;

    if (!pointer)
        return;
    header = (header_t *)pointer - 1;
    memory = header->value.owner;
    bytes = header->value.bytes;
    memory->hooks.release(memory->hooks.ctx, header);
    atomic_fetch_sub_explicit(&memory->used, bytes, memory_order_relaxed);
    tiku_rd_memory_drop(memory);
}

size_t
tiku_rd_size(const void *pointer)
{
    const header_t *header;

    if (!pointer)
        return 0;
    header = (const header_t *)pointer - 1;
    return header->value.bytes;
}

int
tiku_rd_account(const void *pointer, int add, size_t *bytes)
{
    header_t *header;

    if (!pointer) return 0;
    header = (header_t *)pointer - 1;
    if (add) {
        assert(header->value.visits < SIZE_MAX);
        if (header->value.visits++) return 0;
        assert(header->value.bytes <= SIZE_MAX - *bytes);
        *bytes += header->value.bytes;
    } else {
        assert(header->value.visits);
        if (--header->value.visits) return 0;
        assert(*bytes >= header->value.bytes);
        *bytes -= header->value.bytes;
    }
    return 1;
}
