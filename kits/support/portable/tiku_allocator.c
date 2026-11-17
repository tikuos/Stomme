/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_allocator.c - checked allocations with retained provider ownership.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_allocator.h"
#include <stdint.h>
#include <string.h>

int tiku_buffer_init(tiku_owned_buffer_t *buffer, tiku_allocator_t allocator,
                     size_t alignment)
{
    if (!buffer || !allocator.ops || !allocator.ops->allocate ||
        !allocator.ops->release || !alignment || (alignment & (alignment - 1))) {
        return 0;
    }
    *buffer = (tiku_owned_buffer_t){allocator, NULL, 0, alignment, 0};
    return 1;
}

int tiku_buffer_borrow(tiku_owned_buffer_t *buffer, void *data, size_t bytes)
{
    if (!buffer || (!data && bytes)) {
        return 0;
    }
    *buffer = (tiku_owned_buffer_t){{NULL, NULL}, data, bytes, 1, 1};
    return 1;
}

void tiku_buffer_dispose(tiku_owned_buffer_t *buffer)
{
    if (!buffer) {
        return;
    }
    if (buffer->data && !buffer->borrowed) {
        buffer->allocator.ops->release(buffer->allocator.context, buffer->data,
                                        buffer->bytes, buffer->alignment);
    }
    buffer->data = NULL;
    buffer->bytes = 0;
}

int tiku_buffer_resize(tiku_owned_buffer_t *buffer, size_t count, size_t size,
                       int zero_added)
{
    size_t bytes, copied;
    void *data;
    if (!buffer || buffer->borrowed || !buffer->allocator.ops ||
        (size && count > SIZE_MAX / size)) {
        return 0;
    }
    bytes = count * size;
    if (bytes == buffer->bytes) {
        return 1;
    }
    if (!bytes) {
        tiku_buffer_dispose(buffer);
        return 1;
    }
    data = buffer->allocator.ops->allocate(buffer->allocator.context, bytes,
                                            buffer->alignment);
    if (!data) {
        return 0;
    }
    if ((uintptr_t)data & (buffer->alignment - 1)) {
        buffer->allocator.ops->release(buffer->allocator.context, data, bytes,
                                        buffer->alignment);
        return 0;
    }
    copied = bytes < buffer->bytes ? bytes : buffer->bytes;
    if (copied) {
        memcpy(data, buffer->data, copied);
    }
    if (zero_added && bytes > copied) {
        memset((unsigned char *)data + copied, 0, bytes - copied);
    }
    tiku_buffer_dispose(buffer);
    buffer->data = data;
    buffer->bytes = bytes;
    return 1;
}
