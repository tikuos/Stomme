/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_test_pool.h - bounded allocator fixture with ownership and failure records.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TEST_POOL_H_
#define TIKU_TEST_POOL_H_
#include "tiku_allocator.h"

/** @brief One allocation record; bytes zero denotes an unused record. */
typedef struct {
    size_t offset, bytes, alignment;
} tiku_test_pool_slot_t;
/** @brief Owner-confined provider with caller-supplied storage and metadata. */
typedef struct {
    unsigned char *memory;
    size_t capacity, slot_count;
    tiku_test_pool_slot_t *slots;
    size_t used, peak, live, calls, fail_at, faults;
} tiku_test_pool_t;

/** @brief Initialize disjoint storage and records; storage must outlive allocations. */
int tiku_test_pool_init(tiku_test_pool_t *pool, void *memory, size_t capacity,
                        tiku_test_pool_slot_t *slots, size_t count);
/** @brief Return the immutable operations and this pool's context. */
tiku_allocator_t tiku_test_pool_allocator(tiku_test_pool_t *pool);
/** @brief Reset statistics if no allocation is live; otherwise leave state intact. */
int tiku_test_pool_reset(tiku_test_pool_t *pool);
#endif
