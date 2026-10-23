/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_state_pipeline.h - copying a node's state attributes onto
 * another node, filtered and transformed on the way.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_STATE_PIPELINE_H_
#define TIKU_TRK_STATE_PIPELINE_H_

#include "tiku_state.h"

/** @brief Whether attribute @p attr is copied: nonzero to take it. */
typedef int (*tiku_trk_state_accept_fn)(const char *attr, void *ctx);
/**
 * @brief Rewrite one value on its way to the destination.
 *
 * Writes at most @p out_capacity bytes to @p out, and their count to
 * @p out_len.
 *
 * @return 0, or nonzero to fail the copy.
 */
typedef int (*tiku_trk_state_transform_fn)(const char *attr,
                                           const void *in, size_t in_len,
                                           void *out, size_t out_capacity,
                                           size_t *out_len,
                                           void *ctx);

/**
 * @brief Copy the attributes of @p source that @p accept takes onto @p dest,
 *        through @p transform when one is given.
 *
 * Either hook may be NULL: then every attribute is taken, or copied as it
 * is.  A transformed value is at most 4096 bytes.
 *
 * @return how many attributes were copied, or -1 when a transform, a write
 *         or the walk failed, which may leave @p dest partly written.
 */
int tiku_trk_state_pipeline_copy(tiku_store_t *store,
                                 const char *source, const char *dest,
                                 tiku_trk_state_accept_fn accept,
                                 tiku_trk_state_transform_fn transform,
                                 void *ctx);

#endif
