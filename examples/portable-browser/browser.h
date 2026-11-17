/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * browser.h - paged browser state and completion retention.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef PORTABLE_BROWSER_H_
#define PORTABLE_BROWSER_H_
#include "tiku_static_app.h"
#include "tiku_paged_storage.h"
/** @brief Browser launch binding; provider and caller scope are copied at start. */
typedef struct {
    tiku_allocator_t allocator;
    tiku_storage_provider_t provider;
    tiku_resource_caller_t caller;
} portable_browser_config_t;
/** @brief Page state retained by its application lease during provider work. */
typedef struct {
    tiku_static_app_t *app;
    tiku_paged_storage_t *store;
    tiku_app_lease_t request;
    uint64_t epoch, after, offset;
    size_t count, bytes;
    int eof;
    tiku_resource_error_t status;
} portable_browser_t;
/** @brief Static browser with sixteen entries and a 1024-byte read buffer. */
extern const tiku_static_app_ops_t portable_browser_ops;
/** @brief Start a page after the current cursor; no allocation per page. */
tiku_resource_error_t portable_browser_next(portable_browser_t *);
/** @brief Read a listed stream at offset; caller selects from the current page. */
tiku_resource_error_t portable_browser_read(portable_browser_t *, size_t index, uint64_t offset);
/** @brief Retire a completed request lease; call before application destruction.
 * @note The scheduler retains the browser pointer until this returns one.
 * It updates visible state only while the lease belongs to a running app. */
int portable_browser_poll(portable_browser_t *);
#endif
