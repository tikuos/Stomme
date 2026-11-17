/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * browser.c - bounded browser lifecycle and request polling.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "browser.h"

/** @brief Retain the application while an accepted storage request is pending. */
static tiku_resource_error_t retain(portable_browser_t *browser, tiku_resource_error_t status)
{
    browser->status = status;
    if (status != TIKU_RESOURCE_PENDING) tiku_app_lease_release(&browser->request);
    return browser->status;
}

tiku_resource_error_t portable_browser_next(portable_browser_t *browser)
{
    if (!browser || browser->request.app) return TIKU_RESOURCE_BUSY;
    if (!tiku_app_lease_begin(browser->app, &browser->request)) return TIKU_RESOURCE_CANCELLED;
    return retain(browser, tiku_paged_storage_list(browser->store, "/media", browser->epoch, browser->after));
}

tiku_resource_error_t portable_browser_read(portable_browser_t *browser, size_t index, uint64_t offset)
{
    size_t count;
    uint64_t epoch;
    int eof;
    const tiku_storage_request_t *result;
    if (!browser || browser->request.app) return TIKU_RESOURCE_BUSY;
    result = tiku_paged_storage_result(browser->store, &count, &epoch, &eof);
    if (!result || result->operation != TIKU_STORAGE_LIST || index >= count ||
        result->entries[index].kind != TIKU_RESOURCE_STREAM) return TIKU_RESOURCE_INVALID;
    if (!tiku_app_lease_begin(browser->app, &browser->request)) return TIKU_RESOURCE_CANCELLED;
    return retain(browser, tiku_paged_storage_read(browser->store, result->entries[index].path,
        epoch, result->entries[index].node_id, offset));
}

int portable_browser_poll(portable_browser_t *browser)
{
    size_t count;
    uint64_t epoch;
    int eof;
    const tiku_storage_request_t *result;
    if (!browser || tiku_paged_storage_pending(browser->store)) return 0;
    if (tiku_app_lease_state(&browser->request)) {
        browser->status = tiku_paged_storage_status(browser->store);
        result = tiku_paged_storage_result(browser->store, &count, &epoch, &eof);
        if (result) {
            browser->epoch = epoch;
            browser->eof = eof;
            if (result->operation == TIKU_STORAGE_LIST) {
                browser->count = count;
                if (count) browser->after = result->entries[count - 1].node_id;
            } else {
                browser->bytes = count;
                browser->offset = result->offset + count;
            }
        }
    }
    tiku_app_lease_release(&browser->request);
    return 1;
}

/** @brief Create storage and submit the first bounded page. */
static tiku_resource_error_t start(void *state, tiku_static_app_t *app, void *argument)
{
    portable_browser_t *browser = state;
    const portable_browser_config_t *config = argument;
    browser->app = app;
    if (!config) return TIKU_RESOURCE_INVALID;
    browser->store = tiku_paged_storage_create(config->allocator, config->provider, &config->caller, 16, 1024);
    if (!browser->store) return TIKU_RESOURCE_NO_MEMORY;
    return portable_browser_next(browser) == TIKU_RESOURCE_PENDING ? TIKU_RESOURCE_OK : browser->status;
}
/** @brief Tick polls completed work; the n key requests the next page. */
static void dispatch(void *state, tiku_queue_result_t result, const tiku_input_event_t *event)
{
    portable_browser_t *browser = state;
    if (result != TIKU_QUEUE_EVENT) return;
    if (event->type == TIKU_EVENT_TICK) portable_browser_poll(browser);
    if (event->type == TIKU_EVENT_KEY_DOWN && event->key == 'n') portable_browser_next(browser);
}
/** @brief Cancel pending provider work before application state is released. */
static void stop(void *state)
{ tiku_paged_storage_close(((portable_browser_t *)state)->store); }
/** @brief Release retired storage; the application lease has already drained. */
static void dispose(void *state)
{ tiku_paged_storage_destroy(&((portable_browser_t *)state)->store); }

const tiku_static_app_ops_t portable_browser_ops = {
    sizeof(portable_browser_t), start, dispatch, NULL, stop, dispose
};
