/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_resource_backend.h - read-only filesystem view of desktop resources.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_RESOURCE_BACKEND_H_
#define TIKU_RESOURCE_BACKEND_H_
#include "tiku_model.h"
#include "tiku_resource_registry.h"

/** @brief Open a read adapter bound to a copied caller context.
 *  @note Close the adapter before destroying its registry, on its owner thread.
 *        NULL context exposes resources marked for public reads.
 *        Legacy writes fail with requires_context and have no effects. */
tiku_backend_t *tiku_resource_backend_open(tiku_resource_registry_t *,
                                          const tiku_resource_caller_t *);
#endif
