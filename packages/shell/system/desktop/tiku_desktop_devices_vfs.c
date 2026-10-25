/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_desktop_devices_vfs.c - board capability policy and native current-device observations.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_desktop_devices_vfs_private.h"
/** @brief Capabilities are a scalar observation, with no stream rows. */
static int count(void *context, const char *path)
{
    (void)context;
    (void)path;
    return 0;
}
/** @brief State product policy without opening a port or constructing a device backend. */
static tiku_msg_t *capture(void *context, const char *path, int index)
{
    (void)context;
    (void)path;
    (void)index;
    tiku_msg_t *out = trk_value_container("object");
    int bad = trk_value_member(
        out, "supported",
        tiku_resource_value_integer(TIKU_RESOURCE_BOOLEAN,
                                    TIKU_SHELL_BOARD_SERVICES));
    bad |= trk_value_member(
        out, "reason",
        tiku_resource_value_string(TIKU_RESOURCE_TEXT,
                                   TIKU_SHELL_BOARD_SERVICES
                                       ? "native_board_owner"
                                       : "product_board_services_disabled"));
    bad |= trk_value_member(
        out, "browse_namespace",
        tiku_resource_value_string(TIKU_RESOURCE_TEXT, "/devices/<board>"));
    bad |= trk_value_member(out, "control_namespace",
                            tiku_resource_value_string(
                                TIKU_RESOURCE_TEXT, "/devices/<opaque-id>"));
    bad |= trk_value_member(
        out, "separate_control_namespace",
        tiku_resource_value_integer(TIKU_RESOURCE_BOOLEAN, 1));
    bad |= trk_value_member(
        out, "automatic_port_probes",
        tiku_resource_value_integer(TIKU_RESOURCE_BOOLEAN, 0));
    if (bad) {
        tiku_msg_free(out);
        return NULL;
    }
    return out;
}
int desktop_devices_vfs_open(desktop_t *app)
{
    tiku_resource_registry_t *registry =
        tiku_trk_resource_session_registry(app->resource_session);
    if (!registry)
        return 0;
    struct desktop_devices_vfs *owner = calloc(1, sizeof *owner);
    if (!owner)
        return -1;
    owner->app = app;
    owner->registry = registry;
    app->devices_vfs = owner;
    static const tiku_trk_observation_t fields[] = {
        {"/devices/capabilities", 0, 0}};
    owner->capabilities = tiku_trk_resource_observer_open(
        registry, "device-capabilities", "/devices/capabilities", fields, 1,
        count, capture, owner);
    if (!owner->capabilities)
        goto fail;
#if TIKU_SHELL_BOARD_SERVICES
    if (desktop_device_resources_open(owner))
        goto fail;
#endif
    return 0;
fail:
    desktop_devices_vfs_close(app);
    return -1;
}
void desktop_devices_vfs_poll(desktop_t *app, uint64_t now)
{
    struct desktop_devices_vfs *owner = app->devices_vfs;
    if (!owner)
        return;
    tiku_trk_resource_observer_poll(owner->capabilities, now);
#if TIKU_SHELL_BOARD_SERVICES
    tiku_trk_settings_observe_devices(app->settings, app->current_device,
                                      app->current_device_port, 1);
    desktop_device_resources_poll(owner, now);
#else
    tiku_trk_settings_observe_devices(app->settings, "", "", 0);
#endif
}
int desktop_devices_vfs_close(desktop_t *app)
{
    struct desktop_devices_vfs *owner = app->devices_vfs;
    if (!owner)
        return 1;
    int drained = 1;
#if TIKU_SHELL_BOARD_SERVICES
    drained = desktop_device_resources_close(owner);
#endif
    tiku_trk_resource_observer_close(owner->capabilities);
    free(owner);
    app->devices_vfs = NULL;
    return drained;
}
