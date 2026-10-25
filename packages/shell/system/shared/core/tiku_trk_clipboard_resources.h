/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_clipboard_resources.h - generation-bound native clipboard
 * resources.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_CLIPBOARD_RESOURCES_H_
#define TIKU_TRK_CLIPBOARD_RESOURCES_H_
#include "tiku_resource_registry.h"
/** @brief Owner-thread adapter; clipboard data remains owned by Interface
 * Kit.
 */
typedef struct tiku_trk_clipboard_resources tiku_trk_clipboard_resources_t;
/** @brief Register cached metadata and bounded typed transfer actions. */
tiku_trk_clipboard_resources_t *tiku_trk_clipboard_resources_open(tiku_resource_registry_t *);
/** @brief Observe native clipboard changes without importing external
 * selections.
 */
void tiku_trk_clipboard_resources_poll(tiku_trk_clipboard_resources_t *,
                                       uint64_t now);
/** @brief Retire resources before destroying the registry. */
void tiku_trk_clipboard_resources_close(tiku_trk_clipboard_resources_t *);
#endif
