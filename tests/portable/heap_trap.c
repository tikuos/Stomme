/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * heap_trap.c - count and reject heap calls from the host service workload.
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stddef.h>

unsigned tiku_service_heap_calls;
/** @brief Reject a heap allocation and record the call. */
void *__wrap_malloc(size_t size)
{ (void)size; ++tiku_service_heap_calls; return NULL; }
/** @brief Reject a zeroed heap allocation and record the call. */
void *__wrap_calloc(size_t count, size_t size)
{ (void)count; return __wrap_malloc(size); }
/** @brief Reject heap resizing and record the call. */
void *__wrap_realloc(void *data, size_t size)
{ (void)data; return __wrap_malloc(size); }
/** @brief Record an attempted heap release. */
void __wrap_free(void *data)
{ (void)data; ++tiku_service_heap_calls; }
/** @brief Reject heap string duplication and record the call. */
char *__wrap_strdup(const char *text)
{ (void)text; return __wrap_malloc(1); }
