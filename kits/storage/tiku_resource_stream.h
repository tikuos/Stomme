/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_resource_stream.h - owner-published immutable binary snapshots.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_RESOURCE_STREAM_H_
#define TIKU_RESOURCE_STREAM_H_
#include "tiku_resource_registry.h"
/** @brief A caller-bound frozen stream, released on the registry thread. */
typedef struct tiku_resource_stream tiku_resource_stream_t;
/** @brief Create owned stream metadata suitable for an initial snapshot. */
tiku_msg_t *tiku_resource_stream_metadata(size_t bytes, const char *content_id);
/** @brief Publish copied content and complete metadata at the expected revision. */
tiku_resource_error_t tiku_resource_stream_publish(tiku_resource_registry_t *,
    const tiku_resource_stamp_t *, const char *path, const void *, size_t bytes, tiku_resource_stamp_t *);
/** @brief Retain a scoped snapshot at the expected revision, within maximum_bytes. */
tiku_resource_error_t tiku_resource_stream_open(tiku_resource_registry_t *,
    const tiku_resource_caller_t *, const char *path, const tiku_resource_stamp_t *,
    size_t maximum_bytes, tiku_resource_stream_t **out, size_t *bytes);
/** @brief Read frozen bytes after current content/lifetime checks; no partial error. */
tiku_resource_error_t tiku_resource_stream_read(tiku_resource_stream_t *,
    const tiku_resource_caller_t *, uint64_t offset, void *out, size_t capacity, size_t *read);
/** @brief Release a frozen snapshot before its registry. */
void tiku_resource_stream_close(tiku_resource_stream_t *);
/** @brief Atomically replace descendants of a collection; copy new descriptors.
 *  @note Replacement invalidates child identities and catalog cursors. The
 *        collection and its provider retain their identities. Empty is valid. */
tiku_resource_error_t tiku_resource_collection_replace(tiku_resource_registry_t *,
    const tiku_resource_stamp_t *, const char *path, const tiku_resource_initial_t *,
    size_t count, tiku_resource_stamp_t *out);
#endif
