/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * consumer.c - SDK-only panel, browser and calculator with bounded providers.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "panel.h"
#include "browser.h"
#include "calculator.h"
#include "tiku_test_pool.h"
#include "tiku_memory_presenter.h"
#include "tiku_word_model.h"
#include <string.h>

#define CHECK(x) do { if (!(x)) return __LINE__; } while (0)
static _Alignas(64) unsigned char memory[2 * 1024 * 1024], pixels[800 * 480 * 2];
static tiku_test_pool_slot_t slots[32];
volatile unsigned tiku_sdk_checksum;

/** @brief Retained provider view; caller drives retirement on its owner thread. */
typedef struct {
    tiku_paged_storage_t *store;
    const tiku_storage_request_t *request;
    int cancelled;
} local_storage_t;

/** @brief Retain one scoped request without calling back or reading media. */
static tiku_resource_error_t submit(void *context, tiku_paged_storage_t *store,
    const tiku_storage_request_t *request)
{
    local_storage_t *local = context;
    if (local->request) return TIKU_RESOURCE_BUSY;
    if (!request->caller.authenticated || !(request->caller.scopes & TIKU_RESOURCE_READ))
        return TIKU_RESOURCE_DENIED;
    local->store = store;
    local->request = request;
    local->cancelled = 0;
    return TIKU_RESOURCE_OK;
}
/** @brief Defer cancellation retirement until the provider's next step. */
static void cancel(void *context, tiku_paged_storage_t *store, uint64_t token)
{
    local_storage_t *local = context;
    if (local->store == store && local->request && local->request->token == token)
        local->cancelled = 1;
}
static const tiku_storage_provider_ops_t storage_ops = {submit, cancel};

/** @brief Complete a page from a fixed directory, or retire a cancelled read. */
static int storage_step(local_storage_t *local)
{
    const tiku_storage_request_t *request = local->request;
    size_t count = 0;
    uint64_t after;
    tiku_resource_error_t status = local->cancelled ? TIKU_RESOURCE_CANCELLED : TIKU_RESOURCE_OK;
    CHECK(request);
    after = request->after;
    if (!local->cancelled && request->operation == TIKU_STORAGE_LIST) {
        while (after < 37 && count < request->capacity) {
            tiku_resource_entry_t *entry = &request->entries[count++];
            char text[65];
            memset(entry, 0, sizeof *entry);
            entry->node_id = ++after;
            tiku_word_format(after, 10, text);
            strcpy(entry->id, text);
            strcpy(entry->path, "/media/item");
            strcpy(entry->path + 11, text);
            entry->kind = TIKU_RESOURCE_STREAM;
        }
    } else if (!local->cancelled) {
        request->bytes[0] = 42;
        count = 1;
    }
    CHECK(tiku_paged_storage_complete(local->store, request->token, status, count, 1,
        request->operation == TIKU_STORAGE_READ || after == 37));
    local->request = NULL;
    return 0;
}

/** @brief Exercise selected public APIs with a bounded allocator and deferred services. */
int tiku_sdk_probe(void)
{
    tiku_test_pool_t pool;
    tiku_portable_panel_t panel;
    tiku_memory_presenter_t sink = {pixels, sizeof pixels, 1600, NULL, 0, TIKU_PRESENT_OK};
    tiku_app_lease_t observer = {0};
    local_storage_t storage = {0};
    tiku_static_app_t *app;
    portable_browser_t *browser;
    size_t total = 0;
    unsigned checksum = 0;
    CHECK(tiku_test_pool_init(&pool, memory, sizeof memory, slots, 32));
    tiku_allocator_t allocator = tiku_test_pool_allocator(&pool);
    CHECK(tiku_portable_panel_init(&panel, allocator));
    tiku_input_event_t input = {.type = TIKU_EVENT_POINTER_DOWN, .x = 30, .y = 260};
    CHECK(tiku_event_queue_push(panel.events, &input));
    input.type = TIKU_EVENT_POINTER_UP;
    CHECK(tiku_event_queue_push(panel.events, &input));
    CHECK(tiku_portable_panel_dispatch(&panel, 2) == 2 && panel.recording);
    CHECK(tiku_portable_panel_render(&panel, tiku_memory_presenter(&sink)) == TIKU_PRESENT_OK);
    CHECK(!tiku_portable_panel_stop(&panel));
    CHECK(tiku_memory_presenter_finish(&sink, panel.frame));
    for (size_t i = 0; i < sizeof pixels; ++i) checksum = checksum * 33u + pixels[i];
    tiku_sdk_checksum = checksum;
    CHECK(tiku_portable_panel_stop(&panel) && pool.used == 0);

    portable_browser_config_t config = {allocator,
        {&storage_ops, &storage, TIKU_RESOURCE_FEATURE_READ | TIKU_RESOURCE_FEATURE_COLLECTION | TIKU_RESOURCE_FEATURE_STREAM},
        {.principal = "browser", .scopes = TIKU_RESOURCE_READ, .authenticated = 1}};
    app = tiku_static_app_create(allocator, &portable_browser_ops, 4);
    CHECK(app && tiku_static_app_start(app, &config) == TIKU_RESOURCE_OK);
    CHECK(tiku_app_lease_begin(app, &observer));
    browser = tiku_app_lease_state(&observer);
    do {
        CHECK(!storage_step(&storage) && portable_browser_poll(browser));
        total += browser->count;
        if (browser->eof) break;
        CHECK(portable_browser_next(browser) == TIKU_RESOURCE_PENDING);
    } while (total <= 37);
    CHECK(total == 37 && browser->eof);
    CHECK(portable_browser_read(browser, 0, 0) == TIKU_RESOURCE_PENDING);
    CHECK(tiku_static_app_stop(app));
    tiku_app_lease_release(&observer);
    CHECK(!tiku_static_app_destroy(&app));
    CHECK(!storage_step(&storage) && portable_browser_poll(browser));
    CHECK(tiku_static_app_destroy(&app) && pool.used == 0);

    app = tiku_static_app_create(allocator, &portable_calculator_ops, 4);
    CHECK(app && tiku_static_app_start(app, NULL) == TIKU_RESOURCE_OK);
    CHECK(tiku_app_lease_begin(app, &observer));
    input = (tiku_input_event_t){.type = TIKU_EVENT_KEY_DOWN, .key = 'F'};
    CHECK(tiku_static_app_post(app, &input) && tiku_static_app_dispatch(app, 1) == 1);
    CHECK(((portable_calculator_t *)tiku_app_lease_state(&observer))->value == 15);
    tiku_app_lease_release(&observer);
    CHECK(tiku_static_app_stop(app) && tiku_static_app_destroy(&app));
    CHECK(pool.used == 0 && pool.live == 0 && pool.faults == 0);
    return 0;
}

#ifndef TIKU_SDK_LINK_ONLY
/** @brief Return the SDK workload's first failing line, or success. */
int main(void) { return tiku_sdk_probe() != 0; }
#endif
