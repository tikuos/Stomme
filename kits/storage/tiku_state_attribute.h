/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_state_attribute.h - copied native attribute reads and guarded writes.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_STATE_ATTRIBUTE_H_
#define TIKU_STATE_ATTRIBUTE_H_
#include <stddef.h>
#include "tiku_state_update.h"

/** @brief Maximum copied native attribute size. */
#define TIKU_STATE_ATTRIBUTE_MAX 65536u
/** @brief Immutable attribute bytes and the resolved node identity. */
typedef struct tiku_state_attribute tiku_state_attribute_t;
/** @brief Copied proposal, cancellation flag and persistence result. */
typedef struct tiku_state_attribute_update tiku_state_attribute_update_t;

/** @brief Capture a native attribute on a worker; a missing attribute is a valid capture.
 *  @note Follows the original symlink at capture time. errno distinguishes failures.
 *        Nodes must be regular files or directories; attributes use the state store namespace. */
tiku_state_attribute_t *tiku_state_attribute_read(const char *node, const char *attribute);
/** @brief Release a capture after its readers have finished. */
void tiku_state_attribute_free(tiku_state_attribute_t *);
/** @brief Whether the captured attribute exists, including an empty value. */
int tiku_state_attribute_present(const tiku_state_attribute_t *);
/** @brief Borrow immutable bytes; length is zero for an absent or empty attribute. */
const void *tiku_state_attribute_bytes(const tiku_state_attribute_t *, size_t *length);
/** @brief Copy a proposal without I/O; present=0 removes the attribute.
 *  @note The capture may be freed after this returns. */
tiku_state_attribute_update_t *tiku_state_attribute_prepare(const tiku_state_attribute_t *,
    const void *bytes, size_t length, int present);
/** @brief Write on a worker after checking the resolved inode and complete saved bytes.
 *  @note Call once. The descriptor pins the target during the write. Other writers must
 *        cooperate: attribute comparison and replacement are separate filesystem calls.
 *        UNCERTAIN means a write occurred but its durability could not be confirmed. */
tiku_store_update_result_t tiku_state_attribute_run(tiku_state_attribute_update_t *);
/** @brief Request cancellation; a completed write cannot be undone. */
void tiku_state_attribute_cancel(tiku_state_attribute_update_t *);
/** @brief Release before run or after run returns. */
void tiku_state_attribute_update_free(tiku_state_attribute_update_t *);
#endif
