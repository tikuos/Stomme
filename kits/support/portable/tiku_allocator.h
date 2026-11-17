/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_allocator.h - provider-bound ownership and checked buffer resizing.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_ALLOCATOR_H_
#define TIKU_ALLOCATOR_H_
#include <stddef.h>

/** @brief Immutable allocation operations; release accepts the original request. */
typedef struct {
    void *(*allocate)(void *context, size_t bytes, size_t alignment);
    void (*release)(void *context, void *pointer, size_t bytes, size_t alignment);
} tiku_allocator_ops_t;

/** @brief Provider binding; operations and context outlive its live allocations. */
typedef struct {
    const tiku_allocator_ops_t *ops;
    void *context;
} tiku_allocator_t;

/** @brief Noncopyable buffer owner, or borrowed span; initialize before use. */
typedef struct {
    tiku_allocator_t allocator;
    void *data;
    size_t bytes;
    size_t alignment;
    int borrowed;
} tiku_owned_buffer_t;

/** @brief Initialize an empty owner; invalid provider/alignment leaves it unchanged. */
int tiku_buffer_init(tiku_owned_buffer_t *buffer, tiku_allocator_t allocator,
                     size_t alignment);
/** @brief Initialize a borrowed span; its memory remains caller-owned. */
int tiku_buffer_borrow(tiku_owned_buffer_t *buffer, void *data, size_t bytes);
/**
 * @brief Resize to count * size; failure preserves data and ownership.
 * @note Zero bytes releases storage. zero_added clears newly added bytes.
 * Borrowed spans cannot resize. Allocator calls use the retained binding.
 */
int tiku_buffer_resize(tiku_owned_buffer_t *buffer, size_t count, size_t size,
                       int zero_added);
/** @brief Release owned storage or detach borrowed storage; NULL is accepted. */
void tiku_buffer_dispose(tiku_owned_buffer_t *buffer);
#endif
