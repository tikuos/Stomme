/*
 * KintsugiDesktop v0.01 Interface Kit.
 * Authors: Ambuj Varshney <ambuj@kinstugi-os.org>
 * tiku_access_native.h - native publication of immutable logical content.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_ACCESS_NATIVE_H_
#define TIKU_ACCESS_NATIVE_H_
#include "tiku_access.h"
typedef struct tiku_access_native tiku_access_native_t;
/** @brief Open the native accessibility endpoint; NULL when unavailable.
 * Native window is the platform host object on Cocoa, unused on Linux. */
tiku_access_native_t *tiku_access_native_open(const char *name,
                                              void *native_window);
/** @brief Release the endpoint and its trees; no actions occur after close. */
void tiku_access_native_close(tiku_access_native_t *native);
/** @brief Publish a window's logical tree at its content origin. Empty tree
 * removes the window. Callback/context survive until replacement/removal. */
int tiku_access_native_set(tiku_access_native_t *native, uint32_t window,
                           tiku_access_tree_t *tree, int x, int y, int focused,
                           tiku_access_action_fn action, void *context);
/** @brief Supply native screen origin and physical scale for the host canvas.
 */
void tiku_access_native_origin(tiku_access_native_t *native, int x, int y,
                               int scale);
/** @brief Pump pending native accessibility calls without waiting. */
void tiku_access_native_poll(tiku_access_native_t *native);
/** @brief Borrow the autoreleased native child array on Cocoa; NULL on Linux.
 */
void *tiku_access_native_children(tiku_access_native_t *native);
#endif
