/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_app_resources_private.h - application-side captures and ordered transport.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_APP_RESOURCES_PRIVATE_H_
#define TIKU_APP_RESOURCES_PRIVATE_H_
#include "tiku_app_resources.h"
#include "tiku_dl.h"
#include <stddef.h>
#define APP_RESOURCE_WINDOWS 32
/** @brief Copied public native window and menu observations. */
typedef struct {
    uint32_t id;
    int width, height, controls_complete;
    char title[128];
    tiku_menuset_t *menus;
    char *controls;
} app_resource_window_t;
/** @brief One copied schema and last acknowledged observation or stream. */
typedef struct {
    tiku_resource_initial_t initial;
    void *stream;
    size_t bytes;
    int stream_ready, unavailable;
} app_resource_entry_t;
/** @brief A dispatched callback input retained until completion acknowledgment. */
typedef struct {
    tiku_resource_provider_call_t call;
    int64_t deadline;
} app_resource_call_t;
/** @brief Transport state whose callbacks run on the creating application thread. */
struct tiku_app_resources {
    const tiku_app_services_t *services;
    const tiku_app_descriptor_t *descriptor;
    const tiku_app_resource_export_t *extension;
    void *model;
    tiku_resource_provider_client_t *client;
    app_resource_window_t windows[APP_RESOURCE_WINDOWS];
    app_resource_entry_t *entries;
    size_t count, staged, cursor;
    app_resource_call_t calls[16];
    int phase, started, ready, failed, pending_kind, pending_index;
    int quit_requested,quit_acknowledged;
    uint64_t catalog_revision;
    size_t pending_count;
    tiku_resource_snapshot_t pending_value;
    void *upload;
    size_t upload_bytes, upload_at;
    char upload_id[33], catalog[33], scratch[TIKU_RESOURCE_PATH_MAX];
    int64_t now, next_capture, next_hello,quit_deadline;
    uint64_t connection;
    char document_seed[33],document_id[33];

};
/** @brief Build native runner-owned entries before module-owned schemas. */
int tiku_app_resource_base_catalog(tiku_app_resources_t *, size_t,
                                   tiku_resource_initial_t *);
/** @brief Copy one native runner observation, or unsupported for module paths. */
tiku_resource_error_t
tiku_app_resource_base_capture(tiku_app_resources_t *, const char *,
                               tiku_resource_snapshot_t *);
/** @brief Render complete runner-owned window, menu and control streams. */
tiku_resource_error_t tiku_app_resource_base_stream(tiku_app_resources_t *,
                                                    const char *, void **,
                                                    size_t *);
/** @brief Invoke a current published native menu command without an input grant. */
tiku_resource_error_t tiku_app_resource_base_change(tiku_app_resources_t *,
                                                    const char *,
                                                    const tiku_msg_t *, int,
                                                    tiku_resource_snapshot_t *,
                                                    tiku_resource_effect_t *);
#endif
