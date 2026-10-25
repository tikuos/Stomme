/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_desktop_devices_vfs_private.h - product-selected board resource owners.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_DESKTOP_DEVICES_VFS_PRIVATE_H_
#define TIKU_DESKTOP_DEVICES_VFS_PRIVATE_H_
#include "tiku_trk_desktop_private.h"
#include "tiku_trk_resource_values.h"
/** @brief Shared capability observation and optional product device controller. */
struct desktop_devices_vfs {
    desktop_t *app; /**< native desktop owner */
    tiku_resource_registry_t
        *registry; /**< resource session's borrowed registry */
    tiku_trk_resource_observer_t
        *capabilities; /**< product capability observation */
    struct desktop_device_resources
        *native; /**< optional product board controller */
};
#if TIKU_SHELL_BOARD_SERVICES
/** @brief Attach the product's native mount/configuration owner without probing ports. */
int desktop_device_resources_open(struct desktop_devices_vfs *);
/** @brief Reconcile mount lifetimes and collect serialized backend work. */
void desktop_device_resources_poll(struct desktop_devices_vfs *, uint64_t);
/** @brief Cancel and drain for five seconds, detaching work still running.
 * @return One when borrowed backends can be destroyed, zero when retained. */
int desktop_device_resources_close(struct desktop_devices_vfs *);
#endif
#endif
